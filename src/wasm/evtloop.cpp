/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/evtloop.cpp
// Purpose:     wxGUIEventLoop implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/app.h"
#include "wx/evtloop.h"
#include "wx/toplevel.h"

#include <emscripten.h>

extern "C" {

    void EMSCRIPTEN_KEEPALIVE ProcessEvents()
    {
        static int counter = 0;

        if (wxTheApp)
        {
            wxTheApp->ProcessPendingEvents();
            wxTheApp->Paint();
            if (counter++ % 3 == 0)
            {
                wxTheApp->ProcessIdle();
            }
        }
    }

}  // extern "C"

// ----------------------------------------------------------------------------
// Event loops via Asyncify (top-level and nested quasi-modal)
// ----------------------------------------------------------------------------
//
// Neither loop uses emscripten_set_main_loop's simulate_infinite_loop=1, which throws
// an "unwind" to ABANDON the C++ stack: that throw is fatal under native wasm-EH (the
// compiler's catch_all cleanup pads catch the foreign exception and run destructors
// that tear down the main frame before it paints — docs/features/wasm-exceptions/08+09).
//
//   * top level (DoRun depth 0): a plain C++ while-loop runs ProcessEvents() on the real
//     main C stack and yields ONE animation frame per tick via wxWasmYieldToBrowser (an
//     Asyncify suspend that COMPLETES each frame). Because nothing is permanently
//     suspended, a tool-coroutine fiber swap inside ProcessEvents runs from a clean
//     stack. A permanent handleAsync park here instead aborts coroutine swaps with
//     "cannot stop an async operation in flight" (docs/features/async/13).
//   * nested (DoRun depth >0): wxWasmRunNestedLoop() suspends the stack and drives
//     ProcessEvents from a JS setTimeout pump (rAF isn't usable from a nested context).
//     Nested resolvers live on a LIFO (Module._wxNestedLoopExit); ScheduleExit() pops
//     the innermost. The top-level loop instead just sets m_shouldExit.

// Depth of nested wxGUIEventLoop::DoRun() calls. 0 = none running; 1 = the
// top-level main loop; >1 = a nested (quasi-modal) loop.
static int s_wxRunDepth = 0;

EM_ASYNC_JS(void, wxWasmRunNestedLoop, (), {
    var stopped = false;
    var timer = null;
    var finish = null;   // resolves THIS nested loop exactly once

    var pump = function () {
        if (stopped) return;
        timer = setTimeout(async function () {
            if (stopped) return;
            try {
                await ccall('ProcessEvents', 'void', [], [], { async: true });
            } catch (e) {
                // The pump must NEVER stop without resolving: an unresolved
                // promise leaves the nested DoRun (and the whole quasi-modal
                // C++ stack under it) parked forever — a silent freeze. Exit
                // the nested loop instead, loudly.
                console.error('[wxWasm] nested loop pump error - exiting nested loop: ' + e);
                if (finish) finish();
                return;
            }
            if (!stopped) pump();
        }, 17);
    };

    Module._wxNestedLoopExit = Module._wxNestedLoopExit || [];

    await new Promise(function (resolve) {
        finish = function () {
            if (stopped) return;
            stopped = true;
            if (timer !== null) { clearTimeout(timer); timer = null; }
            // Self-exit paths must remove our own entry (we may not be top of
            // the stack if an inner loop is open above us).
            var idx = Module._wxNestedLoopExit.indexOf(finish);
            if (idx !== -1) Module._wxNestedLoopExit.splice(idx, 1);
            resolve();
        };
        Module._wxNestedLoopExit.push(finish);
        pump();
    });
});

EM_JS(void, wxWasmExitNestedLoop, (), {
    var stack = Module._wxNestedLoopExit;
    if (stack && stack.length) {
        (stack.pop())();
    }
});

// Top-level main loop: yield to the browser for ONE animation frame, then return. It is
// called in a plain C++ while-loop in DoRun (below), so ProcessEvents() runs on the real
// main C stack and each Asyncify suspension COMPLETES every frame — unlike a permanent
// handleAsync park, which is "in flight" for the app's whole life and makes a tool-
// coroutine fiber swap abort ("cannot stop an async operation in flight"). With this
// per-frame yield the slot is free whenever ProcessEvents runs (docs/features/async/13).
EM_ASYNC_JS(void, wxWasmYieldToBrowser, (), {
    await new Promise(function (resolve) { requestAnimationFrame(resolve); });
});

// ----------------------------------------------------------------------------
// wxGUIEventLoop
// ----------------------------------------------------------------------------

void wxGUIEventLoop::ScheduleExit(int WXUNUSED(rc))
{
    wxCHECK_RET( IsInsideRun(), wxT("can't call ScheduleExit() if not started") );

    m_shouldExit = true;

    // The top-level loop is a plain while-loop that checks m_shouldExit (above). A nested
    // (quasi-modal) loop is the Asyncify setTimeout pump — resolve it so its DoRun resumes
    // and returns.
    if ( s_wxRunDepth > 1 )
    {
        wxWasmExitNestedLoop();
    }
}

bool wxGUIEventLoop::Pending() const
{
    return wxTheApp && wxTheApp->HasPendingEvents();
}

bool wxGUIEventLoop::Dispatch()
{
    ProcessEvents();
    return true;
}

int wxGUIEventLoop::DispatchTimeout(unsigned long WXUNUSED(timeout))
{
    // TODO: implement
    wxFAIL_MSG(wxT("DispatchTimeout is not implemented"));
    return 0;
}

void wxGUIEventLoop::WakeUp()
{
    // noop: browser doesn't block
}

void wxGUIEventLoop::DoYieldFor(long eventsToProcess)
{
    while (Pending())
    {
        Dispatch();
    }

    wxEventLoopBase::DoYieldFor(eventsToProcess);
}

int wxGUIEventLoop::DoRun()
{
    wxASSERT_MSG(IsOk(), wxT("invalid event loop"));

    // A nested loop (a quasi-modal dialog opened from a tool) pumps via Asyncify; the
    // first (top-level) DoRun registers the rAF main loop then parks. Neither throws
    // (see the header comment and docs/features/wasm-exceptions/09).
    if (s_wxRunDepth++ > 0)
    {
        wxWasmRunNestedLoop();   // suspends here until ScheduleExit()/Exit()
        --s_wxRunDepth;
        return 0;
    }

    if (!wxTopLevelWindows.empty())
    {
        wxWindow *topWindow = wxTopLevelWindows.front();

        int width = EM_ASM_INT({
            return window.innerWidth;
        });
        int height = EM_ASM_INT({
            return window.innerHeight - mainWindow.offsetTop;
        });
        topWindow->SetSize(0, 0, width, height);
        topWindow->Refresh();
    }

    // Run ProcessEvents on the real main C stack, yielding one animation frame between
    // ticks. No throw (fatal under native wasm-EH), no permanent handleAsync park (which
    // blocks coroutine fiber swaps — see wxWasmYieldToBrowser). m_shouldExit, set by
    // ScheduleExit(), ends the loop after the current tick.
    while (!m_shouldExit)
    {
        ProcessEvents();
        wxWasmYieldToBrowser();
    }
    --s_wxRunDepth;

    return 0;
}
