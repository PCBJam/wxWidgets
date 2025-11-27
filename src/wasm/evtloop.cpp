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
// wxGUIEventLoop
// ----------------------------------------------------------------------------

void wxGUIEventLoop::ScheduleExit(int WXUNUSED(rc))
{
    wxCHECK_RET( IsInsideRun(), wxT("can't call ScheduleExit() if not started") );

    m_shouldExit = true;

    // Deschedules requestAnimationFrame, but does not resume execution in DoRun
    //
    // See https://emscripten.org/docs/api_reference/emscripten.h.html#c.emscripten_cancel_main_loop
    emscripten_cancel_main_loop();
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
