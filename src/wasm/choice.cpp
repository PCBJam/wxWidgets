/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/choice.cpp
// Purpose:     wxChoice implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_CHOICE

#include "wx/choice.h"

#define INVALID_INDEX_MESSAGE wxT("invalid choice index")

wxChoice::wxChoice() :
    m_selection(wxNOT_FOUND)
{
}

wxChoice::wxChoice(wxWindow *parent, wxWindowID id,
                   const wxPoint& pos,
                   const wxSize& size,
                   int n, const wxString choices[],
                   long style,
                   const wxValidator& validator,
                   const wxString& name) :
    m_selection(wxNOT_FOUND)
{
    Create(parent, id, pos, size, n, choices, style, validator, name);
}

wxChoice::wxChoice(wxWindow *parent, wxWindowID id,
                   const wxPoint& pos,
                   const wxSize& size,
                   const wxArrayString& choices,
                   long style,
                   const wxValidator& validator,
                   const wxString& name) :
    m_selection(wxNOT_FOUND)
{
    Create(parent, id, pos, size, choices, style, validator, name);
}

wxChoice::~wxChoice()
{
    // ensure that the client data objects are freed while the control is
    // still alive
    Clear();
}

bool wxChoice::Create(wxWindow *parent, wxWindowID id,
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

bool wxChoice::Create(wxWindow *parent, wxWindowID id,
                      const wxPoint& pos,
                      const wxSize& size,
                      int n, const wxString choices[],
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    // TODO(dom-phase-2): create a real <select> element and wire its change
    // event through wx_dom_event.

    if (n > 0)
        Append(n, choices);

    return true;
}

unsigned int wxChoice::GetCount() const
{
    return m_items.size();
}

wxString wxChoice::GetString(unsigned int n) const
{
    wxCHECK_MSG(IsValid(n), wxString(), INVALID_INDEX_MESSAGE);

    return m_items[n];
}

void wxChoice::SetString(unsigned int n, const wxString& s)
{
    wxCHECK_RET(IsValid(n), INVALID_INDEX_MESSAGE);

    // TODO(dom-phase-2): update the item's DOM <option> label.
    m_items[n] = s;

    InvalidateBestSize();
}

void wxChoice::SetSelection(int n)
{
    // TODO(dom-phase-2): reflect the selection on the DOM element.
    m_selection = n;
}

int wxChoice::GetSelection() const
{
    return m_selection;
}

int wxChoice::DoInsertItems(const wxArrayStringsAdapter& items,
                            unsigned int pos,
                            void **clientData,
                            wxClientDataType type)
{
    InvalidateBestSize();

    return DoInsertItemsInLoop(items, pos, clientData, type);
}

int wxChoice::DoInsertOneItem(const wxString& item, unsigned int pos)
{
    // TODO(dom-phase-2): insert a DOM <option> element.
    m_items.Insert(item, pos);
    m_itemsClientData.Insert(NULL, pos);

    // keep the same item selected
    if (m_selection >= static_cast<int>(pos))
        ++m_selection;

    return pos;
}

void wxChoice::DoSetItemClientData(unsigned int n, void *clientData)
{
    m_itemsClientData[n] = clientData;
}

void *wxChoice::DoGetItemClientData(unsigned int n) const
{
    return m_itemsClientData[n];
}

void wxChoice::DoClear()
{
    // TODO(dom-phase-2): remove all DOM <option> elements.
    m_items.Clear();
    m_itemsClientData.Clear();
    m_selection = wxNOT_FOUND;
}

void wxChoice::DoDeleteOneItem(unsigned int pos)
{
    wxCHECK_RET(IsValid(pos), INVALID_INDEX_MESSAGE);

    // TODO(dom-phase-2): remove the item's DOM <option> element.
    m_items.RemoveAt(pos);
    m_itemsClientData.RemoveAt(pos);

    if (m_selection == static_cast<int>(pos))
        m_selection = wxNOT_FOUND;
    else if (m_selection > static_cast<int>(pos))
        --m_selection;
}

#endif // wxUSE_CHOICE
