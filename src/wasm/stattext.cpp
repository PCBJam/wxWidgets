/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/stattext.cpp
// Purpose:     wxStaticText implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_STATTEXT

#include "wx/stattext.h"

#include "wx/wasm/private/dom.h"

wxStaticText::wxStaticText()
{
}

wxStaticText::wxStaticText(wxWindow *parent,
                           wxWindowID id,
                           const wxString& label,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxString& name)
{
    Create(parent, id, label, pos, size, style, name);
}

bool wxStaticText::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxString& label,
                          const wxPoint& pos,
                          const wxSize& size,
                          long style,
                          const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, wxDefaultValidator, name))
        return false;

    WasmCreateDomNode("span");

    SetLabel(label);

    return true;
}

void wxStaticText::SetLabel(const wxString& label)
{
    if (label == m_labelOrig)
        return;

    // stores m_labelOrig and invalidates the best size
    wxControl::SetLabel(label);

    WXSetVisibleLabel(GetEllipsizedLabel());

    AutoResizeIfNecessary();
}

wxString wxStaticText::WXGetVisibleLabel() const
{
    return m_visibleLabel;
}

void wxStaticText::WXSetVisibleLabel(const wxString& str)
{
    m_visibleLabel = str;

    if (WasmGetDomId())
        wxDomSetText(WasmGetDomId(), m_visibleLabel);
}

#endif // wxUSE_STATTEXT
