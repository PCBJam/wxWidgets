/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/timer.cpp
// Purpose:     wxTimer implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#if wxUSE_TIMER

#include "wx/app.h"
#include "wx/evtloop.h"
#include "wx/log.h"

#include "wx/wasm/private/timer.h"

#include <emscripten.h>

// ----------------------------------------------------------------------------
// wxTimerImpl
// ----------------------------------------------------------------------------

void TimerCallback(void *userData)
{
    TimerCallbackFunc *callbackFunc = static_cast<TimerCallbackFunc *>(userData);
    callbackFunc->Run();
}

bool wxWasmTimerImpl::Start(int millisecs, bool oneShot)
{
    if (!wxTimerImpl::Start(millisecs, oneShot))
    {
        return false;
    }

    wxASSERT_MSG(m_callbackFunc == NULL, wxT("timer should be stopped"));

    // Data gets freed by callback.
    m_callbackFunc = new TimerCallbackFunc(this);

    ScheduleFirstInterval();

    return true;
}

void wxWasmTimerImpl::Stop()
{
    wxASSERT_MSG(m_callbackFunc != NULL, wxT("timer should be running"));

    // Set a flag that tells the callback to cancel when it fires.
    m_callbackFunc->Cancel();
    m_callbackFunc = NULL;
}

void wxWasmTimerImpl::ScheduleFirstInterval()
{
    int intervalMs = m_timer->GetInterval();
    m_deadlineMs = wxGetUTCTimeMillis() + intervalMs;

    ScheduleTimerCallback(intervalMs, m_callbackFunc);
}

void wxWasmTimerImpl::ScheduleNextInterval()
{
    int intervalMs = m_timer->GetInterval();

    m_deadlineMs += intervalMs;

    int timeLeftMs = (m_deadlineMs - wxGetUTCTimeMillis()).ToLong();
    timeLeftMs = wxMax(timeLeftMs, 0);
    timeLeftMs = wxMin(timeLeftMs, intervalMs);

    ScheduleTimerCallback(timeLeftMs, m_callbackFunc);
}

void wxWasmTimerImpl::ScheduleTimerCallback(int millisecs, TimerCallbackFunc *callbackFunc)
{
    emscripten_async_call(TimerCallback, callbackFunc, millisecs);
}

void TimerCallbackFunc::Run()
{
    bool selfDestruct = true;

    if (!IsCanceled())
    {
        wxWasmTimerImpl *timer = GetTimerImpl();

        if (timer->IsOneShot())
        {
            timer->Stop();
        }
        else
        {
            timer->ScheduleNextInterval();
            selfDestruct = false;
        }

        timer->m_timer->Notify();
    }

    if (selfDestruct)
    {
        delete this;
    }
}

#endif // wxUSE_TIMER
