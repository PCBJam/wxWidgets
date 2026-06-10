/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/radiobut.cpp
// Purpose:     wxRadioButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_RADIOBTN

#include "wx/radiobut.h"

wxRadioButton::wxRadioButton() :
    m_value(false)
{
}

wxRadioButton::wxRadioButton(wxWindow *parent,
                             wxWindowID id,
                             const wxString& label,
                             const wxPoint& pos,
                             const wxSize& size,
                             long style,
                             const wxValidator& validator,
                             const wxString& name) :
    m_value(false)
{
    Create(parent, id, label, pos, size, style, validator, name);
}

bool wxRadioButton::Create(wxWindow *parent,
                           wxWindowID id,
                           const wxString& label,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxValidator& validator,
                           const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    SetLabel(label);

    // TODO(dom-phase-2): create a real <input type="radio"> element, give
    // buttons of the same group (wxRB_GROUP/wxRB_SINGLE rules) a common DOM
    // name attribute and wire its change event through wx_dom_event.

    return true;
}

void wxRadioButton::SetValue(bool value)
{
    // TODO(dom-phase-2): reflect the state on the DOM element; checking one
    // button must uncheck the others in the same group.
    m_value = value;
}

bool wxRadioButton::GetValue() const
{
    return m_value;
}

#endif // wxUSE_RADIOBTN
