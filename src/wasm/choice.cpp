/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/choice.cpp
// Purpose:     wxChoice implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_CHOICE

#include "wx/choice.h"

#include "wx/wasm/private/dom.h"

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

    WasmCreateDomNode(WasmDomNodeType());

    // Append() goes through DoInsertItems() which pushes the items to the
    // DOM <select>.
    if (n > 0)
        Append(n, choices);

    return true;
}

void wxChoice::WasmSyncItems()
{
    if (!WasmGetDomId())
        return;

    // Rebuild the whole <option> list; this wipes the browser's selection
    // state, so re-apply the cached one (selectedIndex = -1 clears it for
    // wxNOT_FOUND).
    wxDomSetItems(WasmGetDomId(), m_items);
    wxDomSetIntValue(WasmGetDomId(), m_selection);
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

    m_items[n] = s;
    WasmSyncItems();

    InvalidateBestSize();
}

void wxChoice::SetSelection(int n)
{
    m_selection = n;

    if (WasmGetDomId())
        wxDomSetIntValue(WasmGetDomId(), n);
}

int wxChoice::GetSelection() const
{
    // The user can change the selection directly in the browser, so the
    // live selectedIndex is the truth when DOM-backed (-1 == wxNOT_FOUND).
    if (WasmGetDomId())
        return wxDomGetIntValue(WasmGetDomId());

    return m_selection;
}

int wxChoice::DoInsertItems(const wxArrayStringsAdapter& items,
                            unsigned int pos,
                            void **clientData,
                            wxClientDataType type)
{
    InvalidateBestSize();

    const int ret = DoInsertItemsInLoop(items, pos, clientData, type);

    WasmSyncItems();

    return ret;
}

int wxChoice::DoInsertOneItem(const wxString& item, unsigned int pos)
{
    // only called from DoInsertItemsInLoop(); DoInsertItems() pushes the
    // rebuilt item list to the DOM once the loop is done
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
    m_items.Clear();
    m_itemsClientData.Clear();
    m_selection = wxNOT_FOUND;

    WasmSyncItems();
}

void wxChoice::DoDeleteOneItem(unsigned int pos)
{
    wxCHECK_RET(IsValid(pos), INVALID_INDEX_MESSAGE);

    m_items.RemoveAt(pos);
    m_itemsClientData.RemoveAt(pos);

    if (m_selection == static_cast<int>(pos))
        m_selection = wxNOT_FOUND;
    else if (m_selection > static_cast<int>(pos))
        --m_selection;

    WasmSyncItems();
}

void wxChoice::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_CHANGE)
    {
        // Pull the picked index into the cache and fire wxEVT_CHOICE,
        // like any port does for user selection.
        m_selection = wxDomGetIntValue(WasmGetDomId());

        wxCommandEvent event(wxEVT_CHOICE, GetId());
        event.SetInt(m_selection);
        if (m_selection >= 0)
            event.SetString(GetString(m_selection));
        event.SetEventObject(this);
        HandleWindowEvent(event);
        return;
    }

    wxControl::OnDomEvent(kind);
}

#endif // wxUSE_CHOICE
