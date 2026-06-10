/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/spinbutt.cpp
// Purpose:     wxSpinButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_SPINBTN

#include "wx/spinbutt.h"

wxSpinButton::wxSpinButton() :
    m_value(0)
{
}

wxSpinButton::wxSpinButton(wxWindow *parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxString& name) :
    m_value(0)
{
    Create(parent, id, pos, size, style, name);
}

bool wxSpinButton::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxPoint& pos,
                          const wxSize& size,
                          long style,
                          const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, wxDefaultValidator, name))
        return false;

    // TODO(dom-phase-2): create real up/down <button> elements and wire
    // their click events through wx_dom_event.

    return true;
}

int wxSpinButton::GetValue() const
{
    return m_value;
}

void wxSpinButton::SetValue(int val)
{
    // TODO(dom-phase-2): reflect the value on the DOM element.
    m_value = val;
}

#endif // wxUSE_SPINBTN
