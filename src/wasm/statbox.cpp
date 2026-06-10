/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/statbox.cpp
// Purpose:     wxStaticBox implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_STATBOX

#include "wx/statbox.h"

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

    SetLabel(label);

    // TODO(dom-phase-2): create a real <fieldset>/<legend> element.

    return true;
}

#endif // wxUSE_STATBOX
