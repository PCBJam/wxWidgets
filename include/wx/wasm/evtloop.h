///////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/evtloop.h
// Purpose:     
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
///////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_EVTLOOP_H_
#define _WX_WASM_EVTLOOP_H_

#include "wx/evtloop.h"

// ----------------------------------------------------------------------------
// wxGUIEventLoop for wxWebAssembly
// ----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxGUIEventLoop : public wxEventLoopBase
{
public:
    wxGUIEventLoop() {}

    virtual bool IsOk() const { return true; }

    virtual void ScheduleExit(int rc = 0);
    virtual bool Pending() const;
    virtual bool Dispatch();
    virtual int DispatchTimeout(unsigned long timeout);
    virtual void WakeUp();

protected:
    virtual int DoRun();
    virtual void DoYieldFor(long eventsToProcess);

private:
    wxDECLARE_NO_COPY_CLASS(wxGUIEventLoop);
};

#endif // _WX_WASM_EVTLOOP_H_
