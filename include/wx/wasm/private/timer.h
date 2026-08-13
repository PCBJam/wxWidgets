/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/timer.h
// Purpose:     
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_TIMER_H_
#define _WX_WASM_PRIVATE_TIMER_H_

#if wxUSE_TIMER

#include "wx/private/timer.h"
#include "wx/wasm/private/mailbox.h"

class WXDLLIMPEXP_FWD_CORE TimerCallbackFunc;

//-----------------------------------------------------------------------------
// wxTimerImpl
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxWasmTimerImpl : public wxTimerImpl
{
public:
    wxWasmTimerImpl(wxTimer* timer)
      : wxTimerImpl(timer),
        m_callbackFunc(NULL),
        m_mailboxTimerId(0) { }

    virtual bool Start(int millisecs = -1, bool oneShot = false);
    virtual void Stop();
    virtual bool IsRunning() const { return m_callbackFunc != NULL; }

protected:
    bool ScheduleFirstInterval();
    bool ScheduleNextInterval();

    bool ScheduleTimerCallback(int millisecs, TimerCallbackFunc *callbackFunc);

    TimerCallbackFunc *m_callbackFunc;
    wxWasmMailboxTimerId m_mailboxTimerId;
    wxLongLong m_deadlineMs;

    friend class TimerCallbackFunc;
};

class WXDLLIMPEXP_CORE TimerCallbackFunc : public wxObject
{
public:
    TimerCallbackFunc(wxWasmTimerImpl* timer)
        : m_timer(timer),
          m_canceled(false),
          m_executing(false) { }

    void Run();
    void Discard();

    wxWasmTimerImpl *GetTimerImpl() const { return m_timer; }

    wx_wasm_execution::ScopeToken GetDeliveryScope() const
        { return m_deliveryScope; }
    void SetDeliveryScope(wx_wasm_execution::ScopeToken scope)
        { m_deliveryScope = scope; }

    bool IsCanceled() const { return m_canceled; }
    bool IsExecuting() const { return m_executing; }
    void Cancel() { m_canceled = true; }

private:
    wxWasmTimerImpl *m_timer;
    wx_wasm_execution::ScopeToken m_deliveryScope;
    bool m_canceled;
    bool m_executing;
};

#endif // wxUSE_TIMER

#endif // _WX_WASM_PRIVATE_TIMER_H_
