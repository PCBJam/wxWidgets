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
// Nested event loops (quasi-modal dialogs) via Asyncify
// ----------------------------------------------------------------------------
//
// The top-level main loop (the first DoRun) drives the browser via
// emscripten_set_main_loop with simulate_infinite_loop=1, which throws an
// "unwind" to abandon the C++ stack and can NOT be nested or resumed. A nested
// Run() — e.g. DIALOG_SHIM::ShowQuasiModal() from a drawing tool — must NOT call
// it again: doing so leaks the unwind and freezes the app. Instead a nested loop
// suspends the C++ stack here and pumps wxWidgets events from a JS setTimeout
// loop until the loop's ScheduleExit()/Exit() resolves it. This mirrors
// wxDialog::ShowModal()'s startModal() (see src/wasm/dialog.cpp). Resolvers are
// kept on a LIFO stack so inner loops exit before outer ones.

// Depth of nested wxGUIEventLoop::DoRun() calls. 0 = none running; 1 = the
// top-level main loop; >1 = a nested (quasi-modal) loop.
static int s_wxRunDepth = 0;

EM_ASYNC_JS(void, wxWasmRunNestedLoop, (), {
    var stopped = false;
    var timer = null;
    var pump = function () {
        if (stopped) return;
        timer = setTimeout(async function () {
            if (stopped) return;
            try {
                await ccall('ProcessEvents', 'void', [], [], { async: true });
            } catch (e) {
                // Asyncify unwind/rewind hiccup: stop this pump cleanly rather
                // than cascade errors (same guard as startModal).
                stopped = true;
                if (timer !== null) { clearTimeout(timer); timer = null; }
                return;
            }
            if (!stopped) pump();
        }, 17);
    };

    Module._wxNestedLoopExit = Module._wxNestedLoopExit || [];

    await new Promise(function (resolve) {
        pump();
        Module._wxNestedLoopExit.push(function () {
            stopped = true;
            if (timer !== null) { clearTimeout(timer); timer = null; }
            resolve();
        });
    });
});

EM_JS(void, wxWasmExitNestedLoop, (), {
    var stack = Module._wxNestedLoopExit;
    if (stack && stack.length) {
        (stack.pop())();
    }
});

// ----------------------------------------------------------------------------
// wxGUIEventLoop
// ----------------------------------------------------------------------------

void wxGUIEventLoop::ScheduleExit(int WXUNUSED(rc))
{
    wxCHECK_RET( IsInsideRun(), wxT("can't call ScheduleExit() if not started") );

    m_shouldExit = true;

    if ( s_wxRunDepth > 1 )
    {
        // A nested (quasi-modal) loop: resolve its Asyncify pump so DoRun returns.
        wxWasmExitNestedLoop();
    }
    else
    {
        // Top-level loop: deschedule requestAnimationFrame. (Does not resume
        // execution in DoRun; see emscripten_cancel_main_loop docs.)
        emscripten_cancel_main_loop();
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

    // A nested loop (e.g. a quasi-modal dialog opened from a tool) must pump via
    // Asyncify rather than (re)entering emscripten_set_main_loop, which can't be
    // nested/resumed and would freeze the app. The first DoRun is the top-level
    // main loop and falls through to emscripten_set_main_loop below.
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

    // Simulates an infinite loop by throwing an exception to prevent
    // execution from continuing after this function call.
    //
    // See https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_set_main_loop
    emscripten_set_main_loop(ProcessEvents, 0, 1);

    return 0;
}
