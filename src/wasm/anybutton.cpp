/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/anybutton.cpp
// Purpose:     wxAnyButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#ifdef wxHAS_ANY_BUTTON

#include "wx/anybutton.h"

wxBitmap wxAnyButton::DoGetBitmap(State state) const
{
    return m_bitmaps[state].GetBitmap(wxDefaultSize);
}

void wxAnyButton::DoSetBitmap(const wxBitmapBundle& bitmap, State which)
{
    // TODO(dom-phase-2): render the bitmap into the DOM element.
    m_bitmaps[which] = bitmap;
    InvalidateBestSize();
}

#endif // wxHAS_ANY_BUTTON
