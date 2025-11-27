/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dcscreen.h
// Purpose:     wxScreenDC class
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_DCSCREEN_H__
#define __WX_WASM_DCSCREEN_H__

#include "wx/dcscreen.h"
#include "wx/wasm/dc.h"

//-----------------------------------------------------------------------------
// wxScreenDCImpl
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxScreenDCImpl: public wxWasmDCImpl
{
public:
    wxScreenDCImpl(wxScreenDC *owner);
    virtual ~wxScreenDCImpl(void);

    virtual void DoGetSize(int *width, int *height) const wxOVERRIDE;

protected:
    wxDECLARE_DYNAMIC_CLASS(wxScreenDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxScreenDCImpl);
};

#endif // __WX_WASM_DCSCREEN_H__
