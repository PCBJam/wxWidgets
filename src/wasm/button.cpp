/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/button.cpp
// Purpose:     wxButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_BUTTON

#include "wx/button.h"

// RTTI for wxButton comes from wxIMPLEMENT_DYNAMIC_CLASS_XTI in
// src/common/btncmn.cpp (shared by all ports).

wxButton::wxButton()
{
}

wxButton::wxButton(wxWindow *parent, wxWindowID id,
                   const wxString& label,
                   const wxPoint& pos,
                   const wxSize& size, long style,
                   const wxValidator& validator,
                   const wxString& name)
{
    Create(parent, id, label, pos, size, style, validator, name);
}

bool wxButton::Create(wxWindow *parent, wxWindowID id,
                      const wxString& label,
                      const wxPoint& pos,
                      const wxSize& size, long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    SetLabel(label);

    // TODO(dom-phase-2): create a real <button> element and wire its
    // click event through wx_dom_event.

    return true;
}

wxWindow *wxButton::SetDefault()
{
    wxWindow *oldDefault = wxButtonBase::SetDefault();

    // TODO(dom-phase-2): reflect default-button styling on the DOM element.

    return oldDefault;
}

#endif // wxUSE_BUTTON
