/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/radiobut.cpp
// Purpose:     wxRadioButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_RADIOBTN

#include "wx/radiobut.h"

#include "wx/wasm/private/dom.h"

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

    WasmCreateDomNode("radio");

    SetLabel(label);

    if (WasmGetDomId())
    {
        // HTML radios are exclusive per name attribute: give every button of
        // the same wx group (wxRB_GROUP/wxRB_SINGLE rules) a common name
        // derived from the group's first button.
        wxRadioButton *groupStart = this;
        if (!HasFlag(wxRB_GROUP) && !HasFlag(wxRB_SINGLE))
        {
            // Walk the parent's children backwards from this control: the
            // group starts at the nearest preceding wxRadioButton with
            // wxRB_GROUP (or the earliest preceding one if no flag is used).
            for (wxWindowList::compatibility_iterator node =
                     parent->GetChildren().Find(this);
                 node; node = node->GetPrevious())
            {
                wxRadioButton *btn =
                    wxDynamicCast(node->GetData(), wxRadioButton);
                if (!btn)
                    continue;

                groupStart = btn;
                if (btn->HasFlag(wxRB_GROUP))
                    break;
            }
        }

        wxDomSetGroupName(WasmGetDomId(),
                          wxString::Format(wxT("wxrb-%p"), (void*)groupStart));
    }

    return true;
}

void wxRadioButton::SetLabel(const wxString& label)
{
    wxControl::SetLabel(label);

    if (WasmGetDomId())
    {
        // Strip the mnemonic marker; browser radios have no accelerators yet.
        wxDomSetText(WasmGetDomId(), GetLabelText());
        InvalidateBestSize();
    }
}

void wxRadioButton::SetValue(bool value)
{
    m_value = value;

    if (WasmGetDomId())
        wxDomSetBoolValue(WasmGetDomId(), value);
}

bool wxRadioButton::GetValue() const
{
    // Read the live state: the browser un-checks the other radios of the
    // group without firing their change events, so the cache of a deselected
    // sibling goes stale.
    if (WasmGetDomId())
        return wxDomGetBoolValue(WasmGetDomId());

    return m_value;
}

void wxRadioButton::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_CHANGE)
    {
        // Pull the clicked state into the cache and fire wxEVT_RADIOBUTTON,
        // like any port does for user selection.
        m_value = wxDomGetBoolValue(WasmGetDomId());

        wxCommandEvent event(wxEVT_RADIOBUTTON, GetId());
        event.SetInt(m_value);
        event.SetEventObject(this);
        HandleWindowEvent(event);
        return;
    }

    wxControl::OnDomEvent(kind);
}

#endif // wxUSE_RADIOBTN
