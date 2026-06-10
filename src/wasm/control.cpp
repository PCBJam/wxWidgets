/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/control.cpp
// Purpose:     wxControl implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_CONTROLS

#include "wx/control.h"

wxIMPLEMENT_DYNAMIC_CLASS(wxControl, wxWindow);

wxControl::wxControl()
{
}

wxControl::wxControl(wxWindow *parent, wxWindowID id,
                     const wxPoint& pos,
                     const wxSize& size, long style,
                     const wxValidator& validator,
                     const wxString& name)
{
    Create(parent, id, pos, size, style, validator, name);
}

bool wxControl::Create(wxWindow *parent, wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size, long style,
                       const wxValidator& validator,
                       const wxString& name)
{
    bool isCreated = wxWindow::Create(parent, id, pos, size, style, name);

#if wxUSE_VALIDATORS
    SetValidator(validator);
#endif

    return isCreated;
}

wxSize wxControl::DoGetBestSize() const
{
    // TODO(dom-phase-2): measure the control's DOM element instead.
    // Until then, size to the label text so sizer layouts stay sane.
    int w = 0;
    int h = 0;
    GetTextExtent(GetLabel(), &w, &h);
    return wxSize(w + 16, h + 8);
}

#endif // wxUSE_CONTROLS
