/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/checkbox.cpp
// Purpose:     wxCheckBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_CHECKBOX

#include "wx/checkbox.h"

wxCheckBox::wxCheckBox() :
    m_state(wxCHK_UNCHECKED)
{
}

wxCheckBox::wxCheckBox(wxWindow *parent, wxWindowID id, const wxString& label,
                       const wxPoint& pos,
                       const wxSize& size, long style,
                       const wxValidator& validator,
                       const wxString& name) :
    m_state(wxCHK_UNCHECKED)
{
    Create(parent, id, label, pos, size, style, validator, name);
}

bool wxCheckBox::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxString& label,
                        const wxPoint& pos,
                        const wxSize& size,
                        long style,
                        const wxValidator& validator,
                        const wxString& name)
{
    WXValidateStyle(&style);

    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    SetLabel(label);

    // TODO(dom-phase-2): create a real <input type="checkbox"> element plus
    // label and wire its change event through wx_dom_event.

    return true;
}

void wxCheckBox::SetValue(bool value)
{
    DoSet3StateValue(value ? wxCHK_CHECKED : wxCHK_UNCHECKED);
}

bool wxCheckBox::GetValue() const
{
    return m_state == wxCHK_CHECKED;
}

void wxCheckBox::DoSet3StateValue(wxCheckBoxState state)
{
    // TODO(dom-phase-2): reflect the state on the DOM element
    // (checked/indeterminate).
    m_state = state;
}

wxCheckBoxState wxCheckBox::DoGet3StateValue() const
{
    return m_state;
}

#endif // wxUSE_CHECKBOX
