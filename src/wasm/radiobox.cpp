/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/radiobox.cpp
// Purpose:     wxRadioBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_RADIOBOX

#include "wx/radiobox.h"

#define INVALID_INDEX_MESSAGE wxT("invalid radio box index")

wxIMPLEMENT_DYNAMIC_CLASS(wxRadioBox, wxControl);

wxRadioBox::wxRadioBox() :
    m_selection(wxNOT_FOUND)
{
}

wxRadioBox::wxRadioBox(wxWindow *parent,
                       wxWindowID id,
                       const wxString& title,
                       const wxPoint& pos,
                       const wxSize& size,
                       int n, const wxString choices[],
                       int majorDim,
                       long style,
                       const wxValidator& val,
                       const wxString& name) :
    m_selection(wxNOT_FOUND)
{
    Create(parent, id, title, pos, size, n, choices, majorDim, style,
           val, name);
}

wxRadioBox::wxRadioBox(wxWindow *parent,
                       wxWindowID id,
                       const wxString& title,
                       const wxPoint& pos,
                       const wxSize& size,
                       const wxArrayString& choices,
                       int majorDim,
                       long style,
                       const wxValidator& val,
                       const wxString& name) :
    m_selection(wxNOT_FOUND)
{
    Create(parent, id, title, pos, size, choices, majorDim, style, val, name);
}

bool wxRadioBox::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxString& title,
                        const wxPoint& pos,
                        const wxSize& size,
                        const wxArrayString& choices,
                        int majorDim,
                        long style,
                        const wxValidator& val,
                        const wxString& name)
{
    return Create(parent, id, title, pos, size, choices.size(),
                  choices.empty() ? NULL : &choices[0],
                  majorDim, style, val, name);
}

bool wxRadioBox::Create(wxWindow *parent,
                        wxWindowID id,
                        const wxString& title,
                        const wxPoint& pos,
                        const wxSize& size,
                        int n, const wxString choices[],
                        int majorDim,
                        long style,
                        const wxValidator& val,
                        const wxString& name)
{
    if (!(style & (wxRA_SPECIFY_ROWS | wxRA_SPECIFY_COLS)))
        style |= wxRA_SPECIFY_COLS;

    if (!wxControl::Create(parent, id, pos, size, style, val, name))
        return false;

    SetLabel(title);

    for (int i = 0; i < n; ++i)
    {
        m_items.Add(choices[i]);
        m_itemsEnabled.Add(1);
        m_itemsShown.Add(1);
    }

    // the first button is initially selected, as in the other ports
    m_selection = n > 0 ? 0 : wxNOT_FOUND;

    SetMajorDim(majorDim == 0 ? n : majorDim, style);

    // TODO(dom-phase-2): create a real <fieldset>/<legend> element with one
    // <input type="radio"> per item and wire its change events through
    // wx_dom_event.

    return true;
}

bool wxRadioBox::Enable(unsigned int n, bool enable)
{
    wxCHECK_MSG(IsValid(n), false, INVALID_INDEX_MESSAGE);

    // TODO(dom-phase-2): reflect the state on the item's DOM element.
    m_itemsEnabled[n] = enable ? 1 : 0;

    return true;
}

bool wxRadioBox::Show(unsigned int n, bool show)
{
    wxCHECK_MSG(IsValid(n), false, INVALID_INDEX_MESSAGE);

    // TODO(dom-phase-2): reflect the state on the item's DOM element.
    m_itemsShown[n] = show ? 1 : 0;

    return true;
}

bool wxRadioBox::IsItemEnabled(unsigned int n) const
{
    wxCHECK_MSG(IsValid(n), false, INVALID_INDEX_MESSAGE);

    return m_itemsEnabled[n] != 0;
}

bool wxRadioBox::IsItemShown(unsigned int n) const
{
    wxCHECK_MSG(IsValid(n), false, INVALID_INDEX_MESSAGE);

    return m_itemsShown[n] != 0;
}

unsigned int wxRadioBox::GetCount() const
{
    return m_items.size();
}

wxString wxRadioBox::GetString(unsigned int n) const
{
    wxCHECK_MSG(IsValid(n), wxEmptyString, INVALID_INDEX_MESSAGE);

    return m_items[n];
}

void wxRadioBox::SetString(unsigned int n, const wxString& s)
{
    wxCHECK_RET(IsValid(n), INVALID_INDEX_MESSAGE);

    // TODO(dom-phase-2): update the item's DOM label.
    m_items[n] = s;

    InvalidateBestSize();
}

void wxRadioBox::SetSelection(int n)
{
    wxCHECK_RET(IsValid(n), INVALID_INDEX_MESSAGE);

    // TODO(dom-phase-2): check the item's DOM radio input.
    m_selection = n;
}

int wxRadioBox::GetSelection() const
{
    return m_selection;
}

#endif // wxUSE_RADIOBOX
