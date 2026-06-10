/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/combobox.cpp
// Purpose:     wxComboBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_COMBOBOX

#include "wx/combobox.h"

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

    // TODO(dom-phase-2): create a real editable combo (<input> backed by a
    // <datalist>, or <select> when wxCB_READONLY) and wire its events
    // through wx_dom_event.

    if (!value.empty())
        wxTextEntry::ChangeValue(value);

    return true;
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
