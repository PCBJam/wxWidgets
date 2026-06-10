/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/menu.h
// Purpose:     wxMenu and wxMenuBar class declarations for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_MENU_H__
#define __WX_WASM_MENU_H__

#include "wx/vector.h"

class WXDLLIMPEXP_CORE wxMenu : public wxMenuBase
{
public:
    wxMenu(long style = 0);
    wxMenu(const wxString& title, long style = 0);

protected:
    virtual wxMenuItem *DoAppend(wxMenuItem *item) wxOVERRIDE;
    virtual wxMenuItem *DoInsert(size_t pos, wxMenuItem *item) wxOVERRIDE;
    virtual wxMenuItem *DoRemove(wxMenuItem *item) wxOVERRIDE;

private:
    wxDECLARE_DYNAMIC_CLASS(wxMenu);
};

#if wxUSE_MENUBAR

class WXDLLIMPEXP_CORE wxMenuBar : public wxMenuBarBase
{
public:
    wxMenuBar();
    wxMenuBar(long style);
    wxMenuBar(size_t n, wxMenu *menus[], const wxString titles[],
              long style = 0);

    virtual bool Append(wxMenu *menu, const wxString& title) wxOVERRIDE;
    virtual bool Insert(size_t pos, wxMenu *menu,
                        const wxString& title) wxOVERRIDE;
    virtual wxMenu *Remove(size_t pos) wxOVERRIDE;

    virtual void EnableTop(size_t pos, bool enable) wxOVERRIDE;
    virtual bool IsEnabledTop(size_t pos) const wxOVERRIDE;

    virtual void SetMenuLabel(size_t pos, const wxString& label) wxOVERRIDE;
    virtual wxString GetMenuLabel(size_t pos) const wxOVERRIDE;

    virtual void Attach(wxFrame *frame) wxOVERRIDE;
    virtual void Detach() wxOVERRIDE;

    // Re-serializes ALL menus to JSON and pushes the structure to the DOM
    // menubar node; no-op until Attach() created the node. Public so that
    // wxMenu mutations can refresh the bar they hang off.
    void WasmRebuildMenus();

    // wxEVT_MENU dispatch for clicks on the DOM menu popups
    virtual void OnDomEvent(wxDomEventKind kind) wxOVERRIDE;

protected:
    // intrinsic size of the DOM menubar, used by wxFrame::PositionMenuBar()
    // via the SetSize() call in Attach()
    virtual wxSize DoGetBestSize() const wxOVERRIDE;

private:
    // enabled state of the top level menus, kept parallel to m_menus
    wxVector<bool> m_enabledTop;

    wxDECLARE_DYNAMIC_CLASS(wxMenuBar);
};

#endif // wxUSE_MENUBAR

#endif // __WX_WASM_MENU_H__
