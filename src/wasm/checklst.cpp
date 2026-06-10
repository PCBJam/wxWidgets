/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/checklst.cpp
// Purpose:     wxCheckListBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_CHECKLISTBOX

#include "wx/checklst.h"

#include "wx/tokenzr.h"
#include "wx/wasm/private/dom.h"

#define INVALID_INDEX_MESSAGE wxT("invalid checklistbox index")

wxCheckListBox::wxCheckListBox()
{
}

wxCheckListBox::wxCheckListBox(wxWindow *parent, wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size,
                               int nStrings,
                               const wxString *choices,
                               long style,
                               const wxValidator& validator,
                               const wxString& name)
{
    Create(parent, id, pos, size, nStrings, choices, style, validator, name);
}

wxCheckListBox::wxCheckListBox(wxWindow *parent, wxWindowID id,
                               const wxPoint& pos,
                               const wxSize& size,
                               const wxArrayString& choices,
                               long style,
                               const wxValidator& validator,
                               const wxString& name)
{
    Create(parent, id, pos, size, choices, style, validator, name);
}

bool wxCheckListBox::Create(wxWindow *parent, wxWindowID id,
                            const wxPoint& pos,
                            const wxSize& size,
                            int n, const wxString choices[],
                            long style,
                            const wxValidator& validator,
                            const wxString& name)
{
    // TODO(dom-phase-2): render the items with a checkbox in front of each
    // label instead of plain <option> elements.
    return wxCheckListBoxBase::Create(parent, id, pos, size, n, choices,
                                      style, validator, name);
}

bool wxCheckListBox::Create(wxWindow *parent, wxWindowID id,
                            const wxPoint& pos,
                            const wxSize& size,
                            const wxArrayString& choices,
                            long style,
                            const wxValidator& validator,
                            const wxString& name)
{
    return wxCheckListBoxBase::Create(parent, id, pos, size, choices,
                                      style, validator, name);
}

bool wxCheckListBox::IsChecked(unsigned int item) const
{
    wxCHECK_MSG(item < m_itemsChecked.size(), false, INVALID_INDEX_MESSAGE);

    return m_itemsChecked[item] != 0;
}

void wxCheckListBox::Check(unsigned int item, bool check)
{
    wxCHECK_RET(item < m_itemsChecked.size(), INVALID_INDEX_MESSAGE);

    m_itemsChecked[item] = check ? 1 : 0;

    if (WasmGetDomId())
        wxDomSetItemSelected(WasmGetDomId(), item, check);
}

void wxCheckListBox::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_CHANGE)
    {
        // a row checkbox toggled; the JS side remembers which one
        const int item = wxDomGetIntValue(WasmGetDomId());
        if (item >= 0 && static_cast<size_t>(item) < m_itemsChecked.size())
        {
            // re-read this item's live checked state
            m_itemsChecked[item] = 0;
            wxStringTokenizer tok(wxDomGetSelectedIndices(WasmGetDomId()),
                                  wxT(","));
            while (tok.HasMoreTokens())
            {
                long v;
                if (tok.GetNextToken().ToLong(&v) && v == item)
                {
                    m_itemsChecked[item] = 1;
                    break;
                }
            }

            wxCommandEvent event(wxEVT_CHECKLISTBOX, GetId());
            event.SetEventObject(this);
            event.SetInt(item);
            event.SetString(GetString(item));
            HandleWindowEvent(event);
        }
        return;
    }

    wxListBox::OnDomEvent(kind);
}

int wxCheckListBox::DoInsertOneItem(const wxString& item, unsigned int pos)
{
    m_itemsChecked.Insert(0, pos);

    return wxListBox::DoInsertOneItem(item, pos);
}

void wxCheckListBox::DoDeleteOneItem(unsigned int pos)
{
    wxListBox::DoDeleteOneItem(pos);

    if (pos < m_itemsChecked.size())
        m_itemsChecked.RemoveAt(pos);
}

void wxCheckListBox::DoClear()
{
    wxListBox::DoClear();

    m_itemsChecked.Clear();
}

#endif // wxUSE_CHECKLISTBOX
