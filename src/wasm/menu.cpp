/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/menu.cpp
// Purpose:     wxMenu and wxMenuBar implementations for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_MENUS

#include "wx/menu.h"

#ifndef WX_PRECOMP
    #include "wx/frame.h"
#endif

#include "wx/wasm/private/dom.h"

#if wxUSE_MENUBAR

// Refresh the DOM menubar (if any) that `menu` ultimately hangs off: a
// submenu's GetMenuBar() walks up to the root menu's bar.
static void DomRefreshMenuBarOf(const wxMenuBase *menu)
{
    wxMenuBar *bar = menu->GetMenuBar();
    if (bar && bar->WasmGetDomId())
        bar->WasmRebuildMenus();
}

#else // !wxUSE_MENUBAR

static inline void DomRefreshMenuBarOf(const wxMenuBase *WXUNUSED(menu)) { }

#endif // wxUSE_MENUBAR/!wxUSE_MENUBAR

// ----------------------------------------------------------------------------
// wxMenu
// ----------------------------------------------------------------------------

// Note: the wxClassInfo for wxMenu, wxMenuBar and wxMenuItem is implemented
// centrally in src/common/menucmn.cpp, so no wxIMPLEMENT_DYNAMIC_CLASS here.

wxMenu::wxMenu(long style)
      : wxMenuBase(style)
{
}

wxMenu::wxMenu(const wxString& title, long style)
      : wxMenuBase(title, style)
{
}

wxMenuItem *wxMenu::DoAppend(wxMenuItem *item)
{
    wxMenuItem *ret = wxMenuBase::DoAppend(item);

    if (ret)
        DomRefreshMenuBarOf(this);

    return ret;
}

wxMenuItem *wxMenu::DoInsert(size_t pos, wxMenuItem *item)
{
    wxMenuItem *ret = wxMenuBase::DoInsert(pos, item);

    if (ret)
        DomRefreshMenuBarOf(this);

    return ret;
}

wxMenuItem *wxMenu::DoRemove(wxMenuItem *item)
{
    wxMenuItem *ret = wxMenuBase::DoRemove(item);

    if (ret)
        DomRefreshMenuBarOf(this);

    return ret;
}

// ----------------------------------------------------------------------------
// wxMenuBar
// ----------------------------------------------------------------------------

#if wxUSE_MENUBAR

// Serializes a menu's items (recursing into submenus) to the JSON array
// consumed by wxDomMenuSetStructure: [{id,label,kind,checked,enabled,items}]
// with kind one of "normal" | "separator" | "check" | "radio" | "submenu".
static wxString DomMenuItemsToJson(const wxMenu *menu)
{
    wxString json(wxT("["));

    bool first = true;
    for (wxMenuItemList::compatibility_iterator
            node = menu->GetMenuItems().GetFirst();
         node;
         node = node->GetNext())
    {
        wxMenuItem *item = node->GetData();

        if (!first)
            json += wxT(",");
        first = false;

        const char *kind;
        if (item->IsSeparator())
            kind = "separator";
        else if (item->GetSubMenu())
            kind = "submenu";
        else if (item->GetKind() == wxITEM_CHECK)
            kind = "check";
        else if (item->GetKind() == wxITEM_RADIO)
            kind = "radio";
        else
            kind = "normal";

        json += wxString::Format(
            wxT("{\"id\":%d,\"label\":\"%s\",\"kind\":\"%s\",")
            wxT("\"checked\":%s,\"enabled\":%s"),
            item->GetId(),
            // no mnemonics/accelerators in the browser menus (yet)
            wxDomJsonEscape(item->GetItemLabelText()),
            kind,
            item->IsCheckable() && item->IsChecked() ? "true" : "false",
            item->IsEnabled() ? "true" : "false");

        if (item->GetSubMenu())
            json += wxT(",\"items\":") + DomMenuItemsToJson(item->GetSubMenu());

        json += wxT("}");
    }

    json += wxT("]");

    return json;
}

wxMenuBar::wxMenuBar()
{
}

wxMenuBar::wxMenuBar(long WXUNUSED(style))
{
}

wxMenuBar::wxMenuBar(size_t n, wxMenu *menus[], const wxString titles[],
                     long WXUNUSED(style))
{
    for (size_t i = 0; i < n; i++)
        Append(menus[i], titles[i]);
}

bool wxMenuBar::Append(wxMenu *menu, const wxString& title)
{
    if (!wxMenuBarBase::Append(menu, title))
        return false;

    // the base class only stores the menu, not its title
    menu->SetTitle(title);
    m_enabledTop.push_back(true);

    WasmRebuildMenus();

    return true;
}

bool wxMenuBar::Insert(size_t pos, wxMenu *menu, const wxString& title)
{
    if (!wxMenuBarBase::Insert(pos, menu, title))
        return false;

    menu->SetTitle(title);
    m_enabledTop.insert(m_enabledTop.begin() + pos, true);

    WasmRebuildMenus();

    return true;
}

wxMenu *wxMenuBar::Remove(size_t pos)
{
    wxMenu *menu = wxMenuBarBase::Remove(pos);
    if (menu)
    {
        m_enabledTop.erase(m_enabledTop.begin() + pos);

        WasmRebuildMenus();
    }

    return menu;
}

void wxMenuBar::EnableTop(size_t pos, bool enable)
{
    wxCHECK_RET(pos < m_enabledTop.size(), wxT("invalid menu index"));

    m_enabledTop[pos] = enable;

    WasmRebuildMenus();
}

bool wxMenuBar::IsEnabledTop(size_t pos) const
{
    wxCHECK_MSG(pos < m_enabledTop.size(), true, wxT("invalid menu index"));

    return m_enabledTop[pos];
}

void wxMenuBar::SetMenuLabel(size_t pos, const wxString& label)
{
    wxCHECK_RET(pos < GetMenuCount(), wxT("invalid menu index"));

    GetMenu(pos)->SetTitle(label);

    WasmRebuildMenus();
}

wxString wxMenuBar::GetMenuLabel(size_t pos) const
{
    wxCHECK_MSG(pos < GetMenuCount(), wxString(), wxT("invalid menu index"));

    return GetMenu(pos)->GetTitle();
}

void wxMenuBar::Attach(wxFrame *frame)
{
    wxCHECK_RET(frame, wxT("wxMenuBar::Attach(NULL) called"));

    wxMenuBarBase::Attach(frame);

    // The menubar window is created lazily here: applications construct
    // wxMenuBar without a parent, so the wxWindow part (and its DOM node)
    // can only exist once the frame is known (as in src/univ/menu.cpp).
    if (!IsWasmCreated())
        Create(frame, wxID_ANY);

    if (!WasmGetDomId())
        WasmCreateDomNode("menubar");

    WasmRebuildMenus();

    // give the bar its intrinsic height right away: the frame's
    // PositionMenuBar() only positions and stretches, it never measures
    SetSize(wxDefaultCoord, GetBestSize().y);
}

void wxMenuBar::Detach()
{
    // the DOM node is left in place: the frame owns the bar's positioning
    // and window destruction handles the cleanup

    wxMenuBarBase::Detach();
}

void wxMenuBar::WasmRebuildMenus()
{
    if (!WasmGetDomId())
        return;

    wxString json(wxT("["));

    for (size_t pos = 0; pos < GetMenuCount(); pos++)
    {
        if (pos > 0)
            json += wxT(",");

        json += wxString::Format(wxT("{\"title\":\"%s\",\"items\":"),
                                 wxDomJsonEscape(GetMenuLabelText(pos)));
        json += DomMenuItemsToJson(GetMenu(pos));
        json += wxT("}");
    }

    json += wxT("]");

    wxDomMenuSetStructure(WasmGetDomId(), json);
    InvalidateBestSize();
}

void wxMenuBar::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_MENU)
    {
        // Dispatch the activated item like the univ port does: toggle
        // checkable items first, then let the menu fire wxEVT_MENU.
        const int id = wxDomGetLastCommandId(WasmGetDomId());

        wxMenu *menu = NULL;
        wxMenuItem *item = FindItem(id, &menu);
        if (item)
        {
            const bool checkable = item->IsCheckable();
            if (checkable)
                item->Toggle();

            if (menu)
                menu->SendEvent(id, checkable ? item->IsChecked() : -1);

            // refresh the check mark in the DOM structure
            if (checkable)
                WasmRebuildMenus();
        }
        return;
    }

    wxMenuBarBase::OnDomEvent(kind);
}

wxSize wxMenuBar::DoGetBestSize() const
{
    // DOM-backed bars report their intrinsic (content-driven) size,
    // measured on the live element, like wxControl::DoGetBestSize().
    if (WasmGetDomId())
    {
        int w = 0;
        int h = 0;
        wxDomGetIntrinsicSize(WasmGetDomId(), &w, &h);
        if (w > 0 && h > 0)
            return wxSize(w, h);
    }

    // stub bars (no DOM node yet): a plausible menubar height so
    // wxFrame::PositionMenuBar() keeps the layout sane
    return wxSize(100, 24);
}

#endif // wxUSE_MENUBAR

#endif // wxUSE_MENUS
