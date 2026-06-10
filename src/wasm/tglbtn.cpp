/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/tglbtn.cpp
// Purpose:     wxToggleButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_TOGGLEBTN

#ifndef WX_PRECOMP
    #include "wx/bitmap.h"
#endif // WX_PRECOMP

#include "wx/stockitem.h"
#include "wx/tglbtn.h"

#include "wx/wasm/private/dom.h"

wxDEFINE_EVENT( wxEVT_TOGGLEBUTTON, wxCommandEvent );

wxIMPLEMENT_DYNAMIC_CLASS(wxToggleButton, wxControl);

wxToggleButton::wxToggleButton() :
    m_value(false)
{
}

wxToggleButton::wxToggleButton(wxWindow *parent,
                               wxWindowID id,
                               const wxString& label,
                               const wxPoint& pos,
                               const wxSize& size,
                               long style,
                               const wxValidator& validator,
                               const wxString& name) :
    m_value(false)
{
    Create(parent, id, label, pos, size, style, validator, name);
}

bool wxToggleButton::Create(wxWindow *parent,
                            wxWindowID id,
                            const wxString& label,
                            const wxPoint& pos,
                            const wxSize& size, long style,
                            const wxValidator& validator,
                            const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    WasmCreateDomNode("toggle");

    SetLabel(wxIsStockID(id) ? wxGetStockLabel(id) : label);

    return true;
}

void wxToggleButton::SetLabel(const wxString& label)
{
    wxControl::SetLabel(label);

    if (WasmGetDomId())
    {
        // Strip the mnemonic marker; browser buttons have no accelerators yet.
        wxDomSetText(WasmGetDomId(), GetLabelText());
        InvalidateBestSize();
    }
}

void wxToggleButton::SetValue(bool state)
{
    m_value = state;

    if (WasmGetDomId())
        wxDomSetBoolValue(WasmGetDomId(), state);
}

bool wxToggleButton::GetValue() const
{
    return m_value;
}

void wxToggleButton::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_CLICK)
    {
        // Clicking a plain <button> doesn't change aria-pressed by itself:
        // C++ owns the toggle and pushes it back to the element.
        m_value = !m_value;
        wxDomSetBoolValue(WasmGetDomId(), m_value);

        wxCommandEvent event(wxEVT_TOGGLEBUTTON, GetId());
        event.SetInt(GetValue());
        event.SetEventObject(this);
        HandleWindowEvent(event);
        return;
    }

    wxControl::OnDomEvent(kind);
}

//##############################################################################

wxIMPLEMENT_DYNAMIC_CLASS(wxBitmapToggleButton, wxToggleButton);

wxBitmapToggleButton::wxBitmapToggleButton()
{
}

wxBitmapToggleButton::wxBitmapToggleButton(wxWindow *parent,
                                           wxWindowID id,
                                           const wxBitmapBundle& label,
                                           const wxPoint& pos,
                                           const wxSize& size,
                                           long style,
                                           const wxValidator& validator,
                                           const wxString& name)
{
    Create(parent, id, label, pos, size, style, validator, name);
}

bool wxBitmapToggleButton::Create(wxWindow *parent,
                                  wxWindowID id,
                                  const wxBitmapBundle& label,
                                  const wxPoint& pos,
                                  const wxSize& size, long style,
                                  const wxValidator& validator,
                                  const wxString& name)
{
    if (!wxToggleButton::Create(parent, id, wxString(),
                                pos, size, style, validator, name))
        return false;

    // this button is toggleable and has a bitmap label:
    if (label.IsOk())
    {
        SetBitmapLabel(label);

        // we need to adjust the size after setting the bitmap as it may be
        // too big for the default button size
        SetInitialSize(size);
    }

    return true;
}

#endif // wxUSE_TOGGLEBTN
