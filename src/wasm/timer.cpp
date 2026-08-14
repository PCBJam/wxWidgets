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

#include "wx/wasm/private/dispatch.h"
#include "wx/wasm/private/mailbox.h"
#include "wx/wasm/private/timer.h"

#include <emscripten.h>
#include <stdio.h>   // printf: diagnostics land in the browser console

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
    // The expiry lands in the mailbox and the shim's delivery tick invokes
    // it through the promising _wxWasmMailboxTick export, so a timer can no
    // longer enter the wasm on top of a suspended chain — Run()'s
    // parked-retry below is a tripwire that should never fire.
    wxWasmMailboxEnqueueAfter(TimerCallback, callbackFunc, millisecs);
}

void TimerCallbackFunc::Run()
{
    bool selfDestruct = true;

    if (!IsCanceled() && wxWasmDispatchParked())
    {
        // TRIPWIRE (should never fire): the mailbox only delivers when the
        // dispatch interlock is free, so Run() cannot be entered parked via
        // the mailbox lane. If it fires anyway, re-queue rather than run the
        // handler over the parked chain's half-mutated widget state -
        // ScheduleNextInterval()'s deadline bookkeeping keeps periodic
        // timers on cadence afterwards. Reported at escalating thresholds
        // (~59 retries/second at 17ms).
        ++m_parkRetries;
        if (m_parkRetries == 60 || m_parkRetries == 300 || m_parkRetries == 1200 ||
            (m_parkRetries > 1200 && m_parkRetries % 1200 == 0))
        {
            printf("[wx-timer] retry storm: %d retries (~%ds parked, depth=%d) "
                   "- a dispatch chain has been parked this whole time\n",
                   m_parkRetries, (m_parkRetries * 17) / 1000, wxWasmDispatchDepth);
        }
        wxWasmMailboxEnqueueAfter(TimerCallback, this, 17);
        return;
    }

    if (m_parkRetries >= 60)
    {
        printf("[wx-timer] retry storm ended after %d retries (~%ds)\n",
               m_parkRetries, (m_parkRetries * 17) / 1000);
    }
    m_parkRetries = 0;

    if (!IsCanceled())
    {
        wxWasmDispatchGuard dispatchGuard;

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
