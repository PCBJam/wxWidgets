/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/statline.cpp
// Purpose:     wxStaticLine implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_STATLINE

#include "wx/statline.h"

#include "wx/wasm/private/dom.h"

wxStaticLine::wxStaticLine()
{
}

wxStaticLine::wxStaticLine(wxWindow *parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxString& name)
{
    Create(parent, id, pos, size, style, name);
}

bool wxStaticLine::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxPoint& pos,
                          const wxSize& size,
                          long style,
                          const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, AdjustSize(size), style,
                           wxDefaultValidator, name))
        return false;

    WasmCreateDomNode("statline");

    return true;
}

#endif // wxUSE_STATLINE
