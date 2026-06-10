/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/button.cpp
// Purpose:     wxButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_BUTTON

#include "wx/button.h"

#include "wx/dcscreen.h"
#include "wx/stockitem.h"
#include "wx/wasm/private/dom.h"

// RTTI for wxButton comes from wxIMPLEMENT_DYNAMIC_CLASS_XTI in
// src/common/btncmn.cpp (shared by all ports).

wxButton::wxButton()
{
}

wxButton::wxButton(wxWindow *parent, wxWindowID id,
                   const wxString& label,
                   const wxPoint& pos,
                   const wxSize& size, long style,
                   const wxValidator& validator,
                   const wxString& name)
{
    Create(parent, id, label, pos, size, style, validator, name);
}

bool wxButton::Create(wxWindow *parent, wxWindowID id,
                      const wxString& label,
                      const wxPoint& pos,
                      const wxSize& size, long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    WasmCreateDomNode("button");

    // Buttons created with a stock id and no label use the stock label
    // ("OK", "Cancel", ...), like every other port.
    wxString lbl = label;
    if (lbl.empty() && wxIsStockID(id))
        lbl = wxGetStockLabel(id);

    SetLabel(lbl);

    return true;
}

void wxButton::SetLabel(const wxString& label)
{
    wxControl::SetLabel(label);

    if (WasmGetDomId())
    {
        // Strip the mnemonic marker; browser buttons have no accelerators yet.
        wxDomSetText(WasmGetDomId(), GetLabelText());
        InvalidateBestSize();
    }
}

void wxButton::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_CLICK)
    {
        wxCommandEvent event(wxEVT_BUTTON, GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
        return;
    }

    wxControl::OnDomEvent(kind);
}

wxWindow *wxButton::SetDefault()
{
    wxWindow *oldDefault = wxButtonBase::SetDefault();

    // TODO(dom-phase-3): reflect default-button styling on the DOM element.

    return oldDefault;
}

/* static */
wxSize wxButtonBase::GetDefaultSize(wxWindow* win)
{
    // Standard dialog buttons should never collapse below a comfortable
    // click target; derived from the default GUI font like other ports.
    static wxSize size = wxDefaultSize;
    if (size == wxDefaultSize)
    {
        wxScreenDC dc;
        if (win)
            dc.SetFont(win->GetFont());
        const wxSize ext = dc.GetTextExtent(wxT("OK Cancel"));
        size.x = ext.x + 2 * 9 + 4;   // label margins + border
        size.y = ext.y + 2 * 4 + 4;
    }
    return size;
}

#endif // wxUSE_BUTTON
