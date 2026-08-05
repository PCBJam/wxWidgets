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
    // Scheduler builds (docs/features/async/17 S1): the expiry lands in the
    // mailbox and the event pump delivers it from a clean dispatch context, so
    // a timer can no longer enter the wasm on top of a parked chain — Run()'s
    // parked-retry below becomes a tripwire that should never fire. Legacy
    // builds keep the direct emscripten_async_call entry unchanged.
    if (wxWasmMailboxEnabled())
    {
        wxWasmMailboxEnqueueAfter(TimerCallback, callbackFunc, millisecs);
        return;
    }
    emscripten_async_call(TimerCallback, callbackFunc, millisecs);
}

void TimerCallbackFunc::Run()
{
    bool selfDestruct = true;

    if (!IsCanceled() && wxWasmDispatchParked())
    {
        // Another dispatch chain is Asyncify-parked mid-handler; Notify()
        // would run the timer handler over its half-mutated widget state.
        // Retry shortly instead - ScheduleNextInterval()'s deadline
        // bookkeeping keeps periodic timers on cadence afterwards.

        // Diagnostic: how LONG this retry loop spins is a direct measure of the
        // window in which the load-time trap occurs (a board open parks for the
        // whole inline footprint-library preload, and the GAL refresh timer
        // re-arms every 100ms throughout). Reported per callback at escalating
        // thresholds, so a normal short park stays silent and a multi-second one
        // is impossible to miss. ~59 retries/second at 17ms.
        ++m_parkRetries;
        if (m_parkRetries == 60 || m_parkRetries == 300 || m_parkRetries == 1200 ||
            (m_parkRetries > 1200 && m_parkRetries % 1200 == 0))
        {
            printf("[wx-timer] retry storm: %d retries (~%ds parked, depth=%d) "
                   "- a dispatch chain has been parked this whole time\n",
                   m_parkRetries, (m_parkRetries * 17) / 1000, wxWasmDispatchDepth);
        }
        emscripten_async_call(TimerCallback, this, 17);
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
