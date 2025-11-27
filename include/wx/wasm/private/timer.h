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

class WXDLLIMPEXP_FWD_CORE TimerCallbackFunc;

//-----------------------------------------------------------------------------
// wxTimerImpl
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxWasmTimerImpl : public wxTimerImpl
{
public:
    wxWasmTimerImpl(wxTimer* timer)
      : wxTimerImpl(timer),
        m_callbackFunc(NULL) { }

    virtual bool Start(int millisecs = -1, bool oneShot = false);
    virtual void Stop();
    virtual bool IsRunning() const { return m_callbackFunc != NULL; }

protected:
    void ScheduleFirstInterval();
    void ScheduleNextInterval();

    void ScheduleTimerCallback(int millisecs, TimerCallbackFunc *callbackFunc);

    TimerCallbackFunc *m_callbackFunc;
    wxLongLong m_deadlineMs;

    friend class TimerCallbackFunc;
};

class WXDLLIMPEXP_CORE TimerCallbackFunc : public wxObject
{
public:
    TimerCallbackFunc(wxWasmTimerImpl* timer)
        : m_timer(timer),
          m_canceled(false) { }

    void Run();

    wxWasmTimerImpl *GetTimerImpl() const { return m_timer; }

    bool IsCanceled() const { return m_canceled; }
    void Cancel() { m_canceled = true; }

private:
    wxWasmTimerImpl *m_timer;
    bool m_canceled;
};

#endif // wxUSE_TIMER

#endif // _WX_WASM_PRIVATE_TIMER_H_
