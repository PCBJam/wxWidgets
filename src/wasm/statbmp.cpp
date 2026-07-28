/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/statbmp.cpp
// Purpose:     wxStaticBitmap implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_STATBMP

#include "wx/statbmp.h"

#include "wx/wasm/private/dom.h"

wxStaticBitmap::wxStaticBitmap()
{
}

wxStaticBitmap::wxStaticBitmap(wxWindow *parent,
                               wxWindowID id,
                               const wxBitmapBundle& label,
                               const wxPoint& pos,
                               const wxSize& size,
                               long style,
                               const wxString& name)
{
    Create(parent, id, label, pos, size, style, name);
}

bool wxStaticBitmap::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxBitmapBundle& label,
                            const wxPoint& pos,
                            const wxSize& size,
                            long style,
                            const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, wxDefaultValidator, name))
        return false;

    WasmCreateDomNode("image");

    // pushes the initial bitmap to the <img> created above
    SetBitmap(label);

    return true;
}

void wxStaticBitmap::SetBitmap(const wxBitmapBundle& bitmap)
{
    m_bitmapBundle = bitmap;

    if (WasmGetDomId())
    {
        // Ship the resolution matching the display's DPI scale, but size the
        // <img> in logical CSS px (the bundle's default/DIP size) so the
        // browser downsamples a hi-res asset instead of upscaling the 1x one.
        const wxBitmap bmp = m_bitmapBundle.GetBitmapFor(this);
        if (bmp.IsOk())
        {
            const wxSize cssSize = m_bitmapBundle.GetDefaultSize();
            wxDomSetImageDataURL(WasmGetDomId(), wxDomBitmapToDataURL(bmp),
                                 cssSize.x, cssSize.y);
        }
    }

    InvalidateBestSize();
}

wxSize wxStaticBitmap::DoGetBestSize() const
{
    // The base implementation converts through FromPhys() with the DPI scale
    // factor, which inflates the layout box to physical pixels; DOM layout is
    // in CSS px, so the logical bundle size is the correct best size here.
    if (m_bitmapBundle.IsOk())
        return m_bitmapBundle.GetDefaultSize();

    return wxSize(1, 1);
}

#endif // wxUSE_STATBMP
