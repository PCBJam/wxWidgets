/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/statbox.cpp
// Purpose:     wxStaticBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_STATBOX

#include "wx/statbox.h"

#include "wx/wasm/private/dom.h"

wxStaticBox::wxStaticBox()
{
}

wxStaticBox::wxStaticBox(wxWindow *parent, wxWindowID id,
                         const wxString& label,
                         const wxPoint& pos,
                         const wxSize& size,
                         long style,
                         const wxString& name)
{
    Create(parent, id, label, pos, size, style, name);
}

bool wxStaticBox::Create(wxWindow *parent, wxWindowID id,
                         const wxString& label,
                         const wxPoint& pos,
                         const wxSize& size,
                         long style,
                         const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, wxDefaultValidator, name))
        return false;

    WasmCreateDomNode("statbox");

    SetLabel(label);

    return true;
}

void wxStaticBox::SetLabel(const wxString& label)
{
    wxControl::SetLabel(label);

    if (WasmGetDomId())
    {
        // Writes the <fieldset>'s <legend>; strip the mnemonic marker.
        wxDomSetText(WasmGetDomId(), GetLabelText());
        InvalidateBestSize();
    }
}

#endif // wxUSE_STATBOX
