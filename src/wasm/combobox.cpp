/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/combobox.cpp
// Purpose:     wxComboBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_COMBOBOX

#include "wx/combobox.h"

#include "wx/wasm/private/dom.h"

wxComboBox::wxComboBox()
{
}

wxComboBox::wxComboBox(wxWindow *parent,
                       wxWindowID id,
                       const wxString& value,
                       const wxPoint& pos,
                       const wxSize& size,
                       int n, const wxString choices[],
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    Create(parent, id, value, pos, size, n, choices, style, validator, name);
}

wxComboBox::wxComboBox(wxWindow *parent, wxWindowID id,
                       const wxString& value,
                       const wxPoint& pos,
                       const wxSize& size,
                       const wxArrayString& choices,
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    Create(parent, id, value, pos, size, choices, style, validator, name);
}

bool wxComboBox::Create(wxWindow *parent, wxWindowID id,
                        const wxString& value,
                        const wxPoint& pos,
                        const wxSize& size,
                        const wxArrayString& choices,
                        long style,
                        const wxValidator& validator,
                        const wxString& name)
{
    return Create(parent, id, value, pos, size, choices.size(),
                  choices.empty() ? NULL : &choices[0],
                  style, validator, name);
}

bool wxComboBox::Create(wxWindow *parent, wxWindowID id,
                        const wxString& value,
                        const wxPoint& pos,
                        const wxSize& size,
                        int n, const wxString choices[],
                        long style,
                        const wxValidator& validator,
                        const wxString& name)
{
    if (!wxChoice::Create(parent, id, pos, size, n, choices,
                          style, validator, name))
        return false;

    if (!value.empty())
        wxTextEntry::ChangeValue(value);

    return true;
}

const char *wxComboBox::WasmDomNodeType() const
{
    // Read-only combos behave like a choice; editable ones are a text
    // input with datalist autocomplete.
    return HasFlag(wxCB_READONLY) ? "choice" : "combobox";
}

void wxComboBox::DoSetValue(const wxString& value, int flags)
{
    wxTextEntry::DoSetValue(value, flags);

    // push into the DOM element — unless the new value just came FROM the
    // element ('input' event), where echoing it back would move the caret
    if (WasmGetDomId() && !m_inDomInput)
        wxDomSetValue(WasmGetDomId(), value);
}

void wxComboBox::OnDomEvent(wxDomEventKind kind)
{
    switch (kind)
    {
        case wxDOM_EVENT_INPUT:
        {
            m_inDomInput = true;
            DoSetValue(wxDomGetValue(WasmGetDomId()), SetValue_SendEvent);
            m_inDomInput = false;
            return;
        }

        case wxDOM_EVENT_CHANGE:
        {
            // Datalist pick or commit-on-blur: sync the choice selection to
            // the text and notify.
            const wxString value = wxDomGetValue(WasmGetDomId());
            const int sel = FindString(value);

            m_inDomInput = true;
            wxTextEntry::DoSetValue(value, 0);
            m_inDomInput = false;

            if (sel != wxNOT_FOUND)
                wxChoice::SetSelection(sel);

            wxCommandEvent event(wxEVT_COMBOBOX, GetId());
            event.SetEventObject(this);
            event.SetInt(sel);
            event.SetString(value);
            // attach the matched item's client object/data, like native ports
            InitCommandEventWithItems(event, sel);
            HandleWindowEvent(event);
            return;
        }

        default:
            wxChoice::OnDomEvent(kind);
            return;
    }
}

void wxComboBox::SetSelection(int n)
{
    wxChoice::SetSelection(n);

    // keep the text-entry part in sync with the selected item
    if (n != wxNOT_FOUND && IsValid(n))
        wxTextEntry::ChangeValue(wxChoice::GetString(n));
}

void wxComboBox::SetSelection(long from, long to)
{
    wxTextEntry::SetSelection(from, to);
}

void wxComboBox::GetSelection(long *from, long *to) const
{
    wxTextEntry::GetSelection(from, to);
}

void wxComboBox::Clear()
{
    wxTextEntry::Clear();
    wxItemContainer::Clear();
}

void wxComboBox::Popup()
{
    // TODO(dom-phase-2): show the DOM drop-down.
}

void wxComboBox::Dismiss()
{
    // TODO(dom-phase-2): hide the DOM drop-down.
}

#endif // wxUSE_COMBOBOX
