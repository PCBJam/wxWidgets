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
#include "wx/window.h"

#include "wx/wasm/private/execution_owner.h"
#include "wx/wasm/private/mailbox.h"
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

void DiscardTimerCallback(void *userData)
{
    static_cast<TimerCallbackFunc *>(userData)->Discard();
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

    // A zero mailbox identity means the scheduler did not accept ownership.
    // Its synchronous discard callback has already cleared m_callbackFunc.
    return ScheduleFirstInterval();
}

void wxWasmTimerImpl::Stop()
{
    if (!m_callbackFunc)
        return;

    TimerCallbackFunc * const callbackFunc = m_callbackFunc;
    m_callbackFunc = NULL;
    callbackFunc->Cancel();

    const wxWasmMailboxTimerId timerId = m_mailboxTimerId;
    m_mailboxTimerId = 0;

    // If JavaScript still owns the exact timer/mailbox record, cancellation
    // prevents all future native delivery and this callback can be discarded
    // now. Otherwise the typed native queue owns it and will run or discard it
    // exactly once; its canceled flag makes that later Run() inert.
    if (timerId && wxWasmMailboxCancel(timerId))
        callbackFunc->Discard();
}

bool wxWasmTimerImpl::ScheduleFirstInterval()
{
    int intervalMs = m_timer->GetInterval();
    m_deadlineMs = wxGetUTCTimeMillis() + intervalMs;

    return ScheduleTimerCallback(intervalMs, m_callbackFunc);
}

bool wxWasmTimerImpl::ScheduleNextInterval()
{
    const int intervalMs = wxMax(m_timer->GetInterval(), 1);

    m_deadlineMs += intervalMs;

    const wxLongLong now = wxGetUTCTimeMillis();

    // Preserve the periodic phase without replaying one callback for every
    // interval missed while owner admission was closed.
    if (m_deadlineMs <= now)
    {
        const int elapsedMs = (now - m_deadlineMs).ToLong();
        const int missedIntervals = elapsedMs / intervalMs + 1;
        m_deadlineMs += missedIntervals * intervalMs;
    }

    int timeLeftMs = (m_deadlineMs - now).ToLong();
    timeLeftMs = wxMax(timeLeftMs, 0);
    timeLeftMs = wxMin(timeLeftMs, intervalMs);

    return ScheduleTimerCallback(timeLeftMs, m_callbackFunc);
}

bool wxWasmTimerImpl::ScheduleTimerCallback(
        int millisecs, TimerCallbackFunc *callbackFunc)
{
    wx_wasm_execution::WorkClass workClass =
            wx_wasm_execution::WorkClass::Ordinary;
    wx_wasm_execution::ScopeToken targetScope;

    // A timer owned by a window is modal lifecycle work for that exact native
    // top-level family. This is required by the symbol chooser: its panel-owned
    // open-libraries timer completes the dialog's own workflow. A timer owned
    // by the main frame has a different scope and cannot borrow that lease.
    wxWindow *ownerWindow = wxDynamicCast(m_timer->GetOwner(), wxWindow);
    if (ownerWindow)
    {
        targetScope = wxWasmExecutionScopeForWindow(ownerWindow);
        if (targetScope)
            workClass = wx_wasm_execution::WorkClass::ModalLifecycle;
    }

    callbackFunc->SetDeliveryScope(targetScope);
    wxASSERT_MSG(m_mailboxTimerId == 0,
                 wxT("timer already owns a mailbox reservation"));
    m_mailboxTimerId = wxWasmMailboxEnqueueAfterScoped(
            TimerCallback, callbackFunc, millisecs,
            workClass, targetScope, DiscardTimerCallback);
    return m_mailboxTimerId != 0;
}

void TimerCallbackFunc::Discard()
{
    // Stop the timer's ownership link before deleting a delivery that the
    // bounded execution queue refused. A canceled callback can outlive its
    // timer implementation, so do not dereference m_timer in that case.
    if (!m_canceled && m_timer && m_timer->m_callbackFunc == this)
    {
        m_timer->m_callbackFunc = NULL;
        m_timer->m_mailboxTimerId = 0;
    }

    m_canceled = true;
    if (!m_executing)
        delete this;
}

void TimerCallbackFunc::Run()
{
    bool selfDestruct = true;
    m_executing = true;

    if (!IsCanceled())
    {
        wxWasmTimerImpl *timer = GetTimerImpl();
        wxASSERT_MSG(timer->m_callbackFunc == this,
                     wxT("timer callback ownership mismatch"));
        // Native transport popped the exact mailbox record before invoking
        // this callback, so JavaScript no longer owns a cancellable identity.
        timer->m_mailboxTimerId = 0;
        const wx_wasm_execution::ScopeToken expectedScope =
                GetDeliveryScope();
        wxWindow *ownerWindow = wxDynamicCast(
                timer->m_timer->GetOwner(), wxWindow);
        const wx_wasm_execution::ScopeToken currentScope =
                wxWasmExecutionScopeForWindow(ownerWindow);
        const bool targetMatches = currentScope == expectedScope;

        if (timer->IsOneShot())
        {
            timer->Stop();
        }
        else
        {
            selfDestruct = !timer->ScheduleNextInterval();
        }

        if (targetMatches)
            timer->m_timer->Notify();
    }

    m_executing = false;
    if (selfDestruct || IsCanceled())
        delete this;
}

#endif // wxUSE_TIMER
