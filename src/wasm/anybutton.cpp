/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/anybutton.cpp
// Purpose:     wxAnyButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef wxHAS_ANY_BUTTON

#include "wx/anybutton.h"

#include "wx/wasm/private/dom.h"

wxBitmap wxAnyButton::DoGetBitmap(State state) const
{
    return m_bitmaps[state].GetBitmap(wxDefaultSize);
}

void wxAnyButton::DoSetBitmap(const wxBitmapBundle& bitmap, State which)
{
    m_bitmaps[which] = bitmap;

    // Push the normal-state bitmap to the DOM <button>'s leading <img>.
    // Note: wxDomSetText replaces the element's children, so any SetLabel
    // must come before the bitmap (wxButton::Create guarantees that order).
    // TODO(dom-phase-3): reflect the other states (hover/pressed/disabled).
    if (which == State_Normal && WasmGetDomId())
    {
        const wxBitmap bmp = m_bitmaps[which].GetBitmap(wxDefaultSize);
        if (bmp.IsOk())
            wxDomSetImageDataURL(WasmGetDomId(), wxDomBitmapToDataURL(bmp),
                                 bmp.GetWidth(), bmp.GetHeight());
    }

    InvalidateBestSize();
}

#endif // wxHAS_ANY_BUTTON
