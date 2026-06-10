/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/radiobox.cpp
// Purpose:     wxRadioBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_RADIOBOX

#include "wx/radiobox.h"

#include "wx/wasm/private/dom.h"

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

    // <fieldset> owning a <legend> and one radio row per item.
    WasmCreateDomNode("radiobox");

    if (WasmGetDomId())
    {
        wxDomSetText(WasmGetDomId(), GetLabelText());  // the <legend>
        wxDomSetItems(WasmGetDomId(), m_items);
        if (m_selection != wxNOT_FOUND)
            wxDomSetIntValue(WasmGetDomId(), m_selection);
    }

    return true;
}

bool wxRadioBox::Enable(unsigned int n, bool enable)
{
    wxCHECK_MSG(IsValid(n), false, INVALID_INDEX_MESSAGE);

    // TODO(dom-phase-3): reflect the state on the item's DOM radio row.
    m_itemsEnabled[n] = enable ? 1 : 0;

    return true;
}

bool wxRadioBox::Show(unsigned int n, bool show)
{
    wxCHECK_MSG(IsValid(n), false, INVALID_INDEX_MESSAGE);

    // TODO(dom-phase-3): reflect the state on the item's DOM radio row.
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

    m_items[n] = s;

    if (WasmGetDomId())
    {
        // Rebuilding the radio rows wipes the checked state, so re-apply
        // the cached selection.
        wxDomSetItems(WasmGetDomId(), m_items);
        if (m_selection != wxNOT_FOUND)
            wxDomSetIntValue(WasmGetDomId(), m_selection);
    }

    InvalidateBestSize();
}

void wxRadioBox::SetSelection(int n)
{
    wxCHECK_RET(IsValid(n), INVALID_INDEX_MESSAGE);

    m_selection = n;

    if (WasmGetDomId())
        wxDomSetIntValue(WasmGetDomId(), n);
}

int wxRadioBox::GetSelection() const
{
    // The user can change the selection directly in the browser, so the
    // live checked row is the truth when DOM-backed (-1 == wxNOT_FOUND).
    if (WasmGetDomId())
        return wxDomGetIntValue(WasmGetDomId());

    return m_selection;
}

void wxRadioBox::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_CHANGE)
    {
        // Pull the picked row into the cache and fire wxEVT_RADIOBOX,
        // like any port does for user selection.
        m_selection = wxDomGetIntValue(WasmGetDomId());

        wxCommandEvent event(wxEVT_RADIOBOX, GetId());
        event.SetInt(m_selection);
        if (m_selection != wxNOT_FOUND)
            event.SetString(GetString(m_selection));
        event.SetEventObject(this);
        HandleWindowEvent(event);
        return;
    }

    wxControl::OnDomEvent(kind);
}

#endif // wxUSE_RADIOBOX
