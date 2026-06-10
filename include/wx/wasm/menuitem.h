/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/menuitem.h
// Purpose:     wxMenuItem class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_MENUITEM_H__
#define __WX_WASM_MENUITEM_H__

class WXDLLIMPEXP_FWD_CORE wxMenu;

// Pure data stub: the base class already stores the label, help string,
// kind, checked and enabled state.
class WXDLLIMPEXP_CORE wxMenuItem : public wxMenuItemBase
{
public:
    wxMenuItem(wxMenu *parentMenu = NULL,
               int id = wxID_SEPARATOR,
               const wxString& text = wxEmptyString,
               const wxString& help = wxEmptyString,
               wxItemKind kind = wxITEM_NORMAL,
               wxMenu *subMenu = NULL);

private:
    wxDECLARE_DYNAMIC_CLASS(wxMenuItem);
};

#endif // __WX_WASM_MENUITEM_H__
