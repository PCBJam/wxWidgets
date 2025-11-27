/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/popupwin.h
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_POPUPWIN_H_
#define _WX_WASM_POPUPWIN_H_

//-----------------------------------------------------------------------------
// wxPopUpWindow
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxPopupWindow: public wxPopupWindowBase
{
public:
    wxPopupWindow() { }
    wxPopupWindow(wxWindow *parent, int flags = wxBORDER_NONE)
        { (void)Create(parent, flags); }
    virtual ~wxPopupWindow();

    bool Create(wxWindow *parent, int flags = wxBORDER_NONE);

    virtual wxString GetCSSClassList() const wxOVERRIDE {
      return wxNonOwnedWindow::GetCSSClassList() + " popup";
    }

protected:
#ifdef __WXUNIVERSAL__
    wxDECLARE_EVENT_TABLE();
#endif
    wxDECLARE_DYNAMIC_CLASS(wxPopupWindow);
};

#endif // _WX_WASM_POPUPWIN_H_
