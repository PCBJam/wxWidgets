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
// Both the top-level main loop and nested Run()s (e.g. DIALOG_SHIM::ShowQuasiModal()
// from a drawing tool) keep their C++ stack alive by SUSPENDING it via Asyncify,
// never by emscripten_set_main_loop's simulate_infinite_loop=1, which throws an
// "unwind" to ABANDON the stack. That throw is fatal under native wasm-EH: the
// compiler's catch_all cleanup pads catch the foreign exception and run destructors
// that tear down the main frame before it paints (docs/features/wasm-exceptions/
// 08+09). Asyncify's return-based unwind saves the stack without running cleanup, so
// the frame survives, and it behaves identically under -fexceptions and
// -fwasm-exceptions.
//
//   * top level (DoRun depth 0): wxWasmParkMainLoop() suspends the stack and drives
//     ProcessEvents from a requestAnimationFrame pump (vsync-aligned).
//   * nested (DoRun depth >0): wxWasmRunNestedLoop() suspends and drives ProcessEvents
//     from a JS setTimeout pump.
//
// Both drive ProcessEvents via the ASYNC ccall, which is Asyncify-aware and so works
// while the C++ stack is parked. emscripten_set_main_loop is NOT used: its rAF
// callback calls ProcessEvents synchronously, which cannot drive a parked runtime
// (the loop dies after a few frames — the white-background bug; see 09).
//
// Resolvers for both live on one LIFO (Module._wxNestedLoopExit) so inner loops exit
// before outer ones; ScheduleExit() pops the innermost.

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

// Top-level main-loop pump: drives ProcessEvents from a requestAnimationFrame loop
// (vsync) via the ASYNC ccall — Asyncify-aware, so it works even though main's C++
// stack is parked here. (emscripten_set_main_loop's rAF callback calls ProcessEvents
// SYNCHRONOUSLY and cannot drive a parked runtime; it stalls after a few frames — see
// docs/features/wasm-exceptions/09.) Differs from wxWasmRunNestedLoop only in rAF vs
// setTimeout; joins the same LIFO so ScheduleExit()/wxWasmExitNestedLoop() resolves it.
EM_ASYNC_JS(void, wxWasmParkMainLoop, (), {
    var stopped = false;
    var finish = null;

    var pump = function () {
        if (stopped) return;
        requestAnimationFrame(async function () {
            if (stopped) return;
            try {
                await ccall('ProcessEvents', 'void', [], [], { async: true });
            } catch (e) {
                console.error('[wxWasm] main loop pump error: ' + e);
                if (finish) finish();
                return;
            }
            if (!stopped) pump();
        });
    };

    Module._wxNestedLoopExit = Module._wxNestedLoopExit || [];
    await new Promise(function (resolve) {
        finish = function () {
            if (stopped) return;
            stopped = true;
            var idx = Module._wxNestedLoopExit.indexOf(finish);
            if (idx !== -1) Module._wxNestedLoopExit.splice(idx, 1);
            resolve();
        };
        Module._wxNestedLoopExit.push(finish);
        pump();
    });
});

// ----------------------------------------------------------------------------
// wxGUIEventLoop
// ----------------------------------------------------------------------------

void wxGUIEventLoop::ScheduleExit(int WXUNUSED(rc))
{
    wxCHECK_RET( IsInsideRun(), wxT("can't call ScheduleExit() if not started") );

    m_shouldExit = true;

    // Resolve the innermost loop's Asyncify pump (the top-level rAF pump or a nested
    // setTimeout pump) so its DoRun resumes and returns. finish() sets stopped=true
    // first, so the pump stops scheduling before the teardown runs.
    wxWasmExitNestedLoop();
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

    // Suspend the C++ stack here; ProcessEvents is driven by the rAF pump inside
    // wxWasmParkMainLoop. No throw (abandoning the stack is fatal under native
    // wasm-EH) and no emscripten_set_main_loop (its synchronous rAF can't drive a
    // parked runtime). The pump resolves on ScheduleExit().
    wxWasmParkMainLoop();
    --s_wxRunDepth;

    return 0;
}
