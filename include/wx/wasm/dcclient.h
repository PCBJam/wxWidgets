/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dcclient.h
// Purpose:     wxWindowDC, wxClientDC, wxPaintDC classes
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_DCCLIENT_H__
#define __WX_WASM_DCCLIENT_H__

#include "wx/wasm/dc.h"

//-----------------------------------------------------------------------------
// wxWindowDCImpl
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxWindowDCImpl: public wxWasmDCImpl
{
public:
    wxWindowDCImpl(wxDC *owner, wxWindow *win, bool isClient = false);
    virtual ~wxWindowDCImpl(void);

    virtual void DoGetSize(int *width, int *height) const wxOVERRIDE;

protected:
    wxDECLARE_DYNAMIC_CLASS(wxWindowDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxWindowDCImpl);

    void Create(const wxRect& rect);
};

//-----------------------------------------------------------------------------
// wxClientDCImpl
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxClientDCImpl: public wxWindowDCImpl
{
public:
    wxClientDCImpl(wxDC *owner, wxWindow *win);
    virtual ~wxClientDCImpl(void);

protected:
    wxDECLARE_DYNAMIC_CLASS(wxClientDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxClientDCImpl);
};

//-----------------------------------------------------------------------------
// wxPaintDCImpl
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxPaintDCImpl: public wxClientDCImpl
{
public:
    wxPaintDCImpl(wxDC *owner, wxWindow *win);
    virtual ~wxPaintDCImpl(void);

protected:
    wxDECLARE_DYNAMIC_CLASS(wxPaintDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxPaintDCImpl);
};

#endif // __WX_WASM_DCCLIENT_H__
