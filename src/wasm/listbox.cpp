/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/listbox.cpp
// Purpose:     wxListBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_LISTBOX

#include "wx/listbox.h"

#include "wx/tokenzr.h"
#include "wx/wasm/private/dom.h"

#define INVALID_INDEX_MESSAGE wxT("invalid listbox index")

// Parse wxDomGetSelectedIndices()'s comma-joined string ("" = none).
static void wxParseSelectedIndices(const wxString& joined, wxArrayInt& out)
{
    out.clear();

    wxStringTokenizer tok(joined, wxT(","));
    while (tok.HasMoreTokens())
    {
        long n;
        if (tok.GetNextToken().ToLong(&n))
            out.push_back(n);
    }
}

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

    // TODO(dom-phase-3): single-selection (wxLB_SINGLE) listboxes still use
    // the multiple <select>; switch on the style here.
    WasmCreateDomNode("listbox");

    // Append() goes through DoInsertItems() which pushes the items to the
    // DOM <select>.
    if (n > 0)
        Append(n, choices);

    return true;
}

void wxListBox::WasmSyncItems()
{
    if (!WasmGetDomId())
        return;

    // Rebuild the whole <option> list; this wipes the browser's selection
    // state, so re-apply the cached one.
    wxDomSetItems(WasmGetDomId(), m_items);
    WasmSyncSelection();
}

void wxListBox::WasmSyncSelection()
{
    if (!WasmGetDomId())
        return;

    // Push the whole cached selection state to the DOM <select>.
    for (size_t i = 0; i < m_itemsSelected.size(); ++i)
        wxDomSetItemSelected(WasmGetDomId(), i, m_itemsSelected[i] != 0);
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

    // The user can change the selection directly in the browser, so the
    // live <select> state is the truth when DOM-backed (the cache is
    // refreshed on every CHANGE event).
    if (WasmGetDomId())
    {
        wxArrayInt selections;
        wxParseSelectedIndices(wxDomGetSelectedIndices(WasmGetDomId()),
                               selections);
        return selections.Index(n) != wxNOT_FOUND;
    }

    return m_itemsSelected[n] != 0;
}

int wxListBox::GetSelections(wxArrayInt& aSelections) const
{
    aSelections.clear();

    // See IsSelected(): live state wins when DOM-backed.
    if (WasmGetDomId())
    {
        wxParseSelectedIndices(wxDomGetSelectedIndices(WasmGetDomId()),
                               aSelections);
        return aSelections.size();
    }

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

    m_items[n] = s;
    WasmSyncItems();

    InvalidateBestSize();
}

int wxListBox::GetSelection() const
{
    // See IsSelected(): live state wins when DOM-backed.
    if (WasmGetDomId())
    {
        wxArrayInt selections;
        wxParseSelectedIndices(wxDomGetSelectedIndices(WasmGetDomId()),
                               selections);
        return selections.empty() ? wxNOT_FOUND : selections[0];
    }

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
    if (n == wxNOT_FOUND)
    {
        // deselect everything
        for (size_t i = 0; i < m_itemsSelected.size(); ++i)
            m_itemsSelected[i] = 0;

        WasmSyncSelection();
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

    WasmSyncSelection();
}

int wxListBox::DoInsertItems(const wxArrayStringsAdapter& items,
                             unsigned int pos,
                             void **clientData,
                             wxClientDataType type)
{
    InvalidateBestSize();
    int n = DoInsertItemsInLoop(items, pos, clientData, type);
    UpdateOldSelections();
    WasmSyncItems();
    return n;
}

int wxListBox::DoInsertOneItem(const wxString& item, unsigned int pos)
{
    // only called from DoInsertItemsInLoop(); DoInsertItems() pushes the
    // rebuilt item list to the DOM once the loop is done
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
    m_items.Clear();
    m_itemsClientData.Clear();
    m_itemsSelected.Clear();

    WasmSyncItems();
}

void wxListBox::DoDeleteOneItem(unsigned int pos)
{
    wxCHECK_RET(IsValid(pos), INVALID_INDEX_MESSAGE);

    m_items.RemoveAt(pos);
    m_itemsClientData.RemoveAt(pos);
    m_itemsSelected.RemoveAt(pos);

    WasmSyncItems();
}

void wxListBox::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_CHANGE)
    {
        // Pull the live selection into the cache and fire wxEVT_LISTBOX
        // with the first selected index, like any port does for user
        // selection.
        wxArrayInt selections;
        wxParseSelectedIndices(wxDomGetSelectedIndices(WasmGetDomId()),
                               selections);

        for (size_t i = 0; i < m_itemsSelected.size(); ++i)
            m_itemsSelected[i] = 0;
        for (size_t i = 0; i < selections.size(); ++i)
        {
            const int n = selections[i];
            if (n >= 0 && n < static_cast<int>(m_itemsSelected.size()))
                m_itemsSelected[n] = 1;
        }

        const int sel = selections.empty() ? wxNOT_FOUND : selections[0];

        wxCommandEvent event(wxEVT_LISTBOX, GetId());
        event.SetInt(sel);
        if (sel != wxNOT_FOUND)
            event.SetString(GetString(sel));
        event.SetEventObject(this);
        HandleWindowEvent(event);
        return;
    }

    wxControl::OnDomEvent(kind);
}

#endif // wxUSE_LISTBOX
