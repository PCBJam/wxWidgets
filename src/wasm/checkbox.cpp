/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/checkbox.cpp
// Purpose:     wxCheckBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_CHECKBOX

#include "wx/checkbox.h"

#include "wx/wasm/private/dom.h"

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

    WasmCreateDomNode("checkbox");

    SetLabel(label);

    return true;
}

void wxCheckBox::SetLabel(const wxString& label)
{
    wxControl::SetLabel(label);

    if (WasmGetDomId())
    {
        // Strip the mnemonic marker; browser checkboxes have no accelerators yet.
        wxDomSetText(WasmGetDomId(), GetLabelText());
        InvalidateBestSize();
    }
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
    m_state = state;

    // TODO(dom-phase-3): reflect wxCHK_UNDETERMINED via the element's
    // indeterminate property.
    if (WasmGetDomId())
        wxDomSetBoolValue(WasmGetDomId(), m_state == wxCHK_CHECKED);
}

wxCheckBoxState wxCheckBox::DoGet3StateValue() const
{
    return m_state;
}

wxSize wxCheckBox::DoGetBestSize() const
{
    wxSize best = wxControl::DoGetBestSize();

    // Same rationale as wxChoice::DoGetBestSize(): the DOM intrinsic measure
    // can run before the element is laid out and report a degenerate height,
    // and dense sizers (wxGridBagSizer(0,0) in KiCad's selection filters) use
    // the best height as the full row pitch. Floor it to a font-derived
    // control height.
    const int minHeight = GetCharHeight() + 8;
    if (best.y < minHeight)
        best.y = minHeight;

    return best;
}

void wxCheckBox::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_CHANGE)
    {
        // Pull the clicked state into the cache and fire wxEVT_CHECKBOX,
        // like any port does for user toggles.
        m_state = wxDomGetBoolValue(WasmGetDomId()) ? wxCHK_CHECKED
                                                    : wxCHK_UNCHECKED;

        wxCommandEvent event(wxEVT_CHECKBOX, GetId());
        event.SetInt(IsChecked());
        event.SetEventObject(this);
        HandleWindowEvent(event);
        return;
    }

    wxControl::OnDomEvent(kind);
}

#endif // wxUSE_CHECKBOX
