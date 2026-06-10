/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/menuitem.cpp
// Purpose:     wxMenuItem implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_MENUS

#include "wx/menuitem.h"

#ifndef WX_PRECOMP
    #include "wx/menu.h"
#endif

// ----------------------------------------------------------------------------
// wxMenuItem
// ----------------------------------------------------------------------------

// Note: the wxClassInfo for wxMenuItem is implemented centrally in
// src/common/menucmn.cpp, so no wxIMPLEMENT_DYNAMIC_CLASS here.

wxMenuItem *wxMenuItemBase::New(wxMenu *parentMenu,
                                int id,
                                const wxString& name,
                                const wxString& help,
                                wxItemKind kind,
                                wxMenu *subMenu)
{
    return new wxMenuItem(parentMenu, id, name, help, kind, subMenu);
}

wxMenuItem::wxMenuItem(wxMenu *parentMenu, int id, const wxString& text,
                       const wxString& help, wxItemKind kind, wxMenu *subMenu)
    : wxMenuItemBase(parentMenu, id, text, help, kind, subMenu)
{
    // TODO(dom-phase-2): create a DOM node for the item when it is appended
    // to a menu.
}

#endif // wxUSE_MENUS
