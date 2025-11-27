/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dcmemory.h
// Purpose:     wxMemoryDC class
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_DCMEMORY_H__
#define __WX_WASM_DCMEMORY_H__

#include "wx/wasm/dc.h"

#include "wx/bitmap.h"
#include "wx/dcmemory.h"

//-----------------------------------------------------------------------------
// wxMemoryDCImpl
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxMemoryDCImpl: public wxWasmDCImpl
{
public:
    wxMemoryDCImpl(wxMemoryDC *owner);
    wxMemoryDCImpl(wxMemoryDC *owner, wxBitmap& bitmap);
    wxMemoryDCImpl(wxMemoryDC *owner, wxDC *dc);

    virtual ~wxMemoryDCImpl(void);

    virtual void DoGetSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoSelect(const wxBitmap& WXUNUSED(bmp)) wxOVERRIDE;

    virtual const wxBitmap& GetSelectedBitmap() const wxOVERRIDE { return m_bitmap; }
    virtual wxBitmap& GetSelectedBitmap() wxOVERRIDE { return m_bitmap; }

protected:
    void Init();

    void Deselect();

    wxBitmap m_bitmap;

    wxDECLARE_DYNAMIC_CLASS(wxMemoryDCImpl);
    wxDECLARE_NO_COPY_CLASS(wxMemoryDCImpl);
};

#endif // __WX_WASM_DCMEMORY_H__
