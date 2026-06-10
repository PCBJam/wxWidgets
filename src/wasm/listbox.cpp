/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/listbox.cpp
// Purpose:     wxListBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_LISTBOX

#include "wx/listbox.h"

#define INVALID_INDEX_MESSAGE wxT("invalid listbox index")

wxListBox::wxListBox()
{
}

wxListBox::wxListBox(wxWindow *parent, wxWindowID id,
                     const wxPoint& pos,
                     const wxSize& size,
                     int n, const wxString choices[],
                     long style,
                     const wxValidator& validator,
                     const wxString& name)
{
    Create(parent, id, pos, size, n, choices, style, validator, name);
}

wxListBox::wxListBox(wxWindow *parent, wxWindowID id,
                     const wxPoint& pos,
                     const wxSize& size,
                     const wxArrayString& choices,
                     long style,
                     const wxValidator& validator,
                     const wxString& name)
{
    Create(parent, id, pos, size, choices, style, validator, name);
}

wxListBox::~wxListBox()
{
    // ensure that the client data objects are freed while the control is
    // still alive
    Clear();
}

bool wxListBox::Create(wxWindow *parent, wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       int n, const wxString choices[],
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    // TODO(dom-phase-2): create a real <select size=N multiple?> element and
    // wire its change/dblclick events through wx_dom_event.

    if (n > 0)
        Append(n, choices);

    return true;
}

bool wxListBox::Create(wxWindow *parent, wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size,
                       const wxArrayString& choices,
                       long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    return Create(parent, id, pos, size, choices.size(),
                  choices.empty() ? NULL : &choices[0],
                  style, validator, name);
}

bool wxListBox::IsSelected(int n) const
{
    wxCHECK_MSG(IsValid(n), false, INVALID_INDEX_MESSAGE);

    return m_itemsSelected[n] != 0;
}

int wxListBox::GetSelections(wxArrayInt& aSelections) const
{
    aSelections.clear();

    for (size_t i = 0; i < m_itemsSelected.size(); ++i)
    {
        if (m_itemsSelected[i])
            aSelections.push_back(i);
    }

    return aSelections.size();
}

unsigned int wxListBox::GetCount() const
{
    return m_items.size();
}

wxString wxListBox::GetString(unsigned int n) const
{
    wxCHECK_MSG(IsValid(n), wxString(), INVALID_INDEX_MESSAGE);

    return m_items[n];
}

void wxListBox::SetString(unsigned int n, const wxString& s)
{
    wxCHECK_RET(IsValid(n), INVALID_INDEX_MESSAGE);

    // TODO(dom-phase-2): update the item's DOM <option> label.
    m_items[n] = s;

    InvalidateBestSize();
}

int wxListBox::GetSelection() const
{
    for (size_t i = 0; i < m_itemsSelected.size(); ++i)
    {
        if (m_itemsSelected[i])
            return i;
    }

    return wxNOT_FOUND;
}

void wxListBox::DoSetFirstItem(int WXUNUSED(n))
{
    // TODO(dom-phase-2): scroll the DOM element so that the item is the
    // first visible one.
}

void wxListBox::DoSetSelection(int n, bool select)
{
    // TODO(dom-phase-2): reflect the selection on the DOM element.

    if (n == wxNOT_FOUND)
    {
        // deselect everything
        for (size_t i = 0; i < m_itemsSelected.size(); ++i)
            m_itemsSelected[i] = 0;
        return;
    }

    wxCHECK_RET(IsValid(n), INVALID_INDEX_MESSAGE);

    if (select && !HasMultipleSelection())
    {
        // only a single item can be selected at a time
        for (size_t i = 0; i < m_itemsSelected.size(); ++i)
            m_itemsSelected[i] = 0;
    }

    m_itemsSelected[n] = select ? 1 : 0;
}

int wxListBox::DoInsertItems(const wxArrayStringsAdapter& items,
                             unsigned int pos,
                             void **clientData,
                             wxClientDataType type)
{
    InvalidateBestSize();
    int n = DoInsertItemsInLoop(items, pos, clientData, type);
    UpdateOldSelections();
    return n;
}

int wxListBox::DoInsertOneItem(const wxString& item, unsigned int pos)
{
    // TODO(dom-phase-2): insert a DOM <option> element.
    m_items.Insert(item, pos);
    m_itemsClientData.Insert(NULL, pos);
    m_itemsSelected.Insert(0, pos);

    return pos;
}

void wxListBox::DoSetItemClientData(unsigned int n, void *clientData)
{
    m_itemsClientData[n] = clientData;
}

void *wxListBox::DoGetItemClientData(unsigned int n) const
{
    return m_itemsClientData[n];
}

void wxListBox::DoClear()
{
    // TODO(dom-phase-2): remove all DOM <option> elements.
    m_items.Clear();
    m_itemsClientData.Clear();
    m_itemsSelected.Clear();
}

void wxListBox::DoDeleteOneItem(unsigned int pos)
{
    wxCHECK_RET(IsValid(pos), INVALID_INDEX_MESSAGE);

    // TODO(dom-phase-2): remove the item's DOM <option> element.
    m_items.RemoveAt(pos);
    m_itemsClientData.RemoveAt(pos);
    m_itemsSelected.RemoveAt(pos);
}

#endif // wxUSE_LISTBOX
