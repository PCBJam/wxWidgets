/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/menu.cpp
// Purpose:     wxMenu and wxMenuBar implementations for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_MENUS

#include "wx/menu.h"

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
    // TODO(dom-phase-2): create a DOM node for the item.
    return wxMenuBase::DoAppend(item);
}

wxMenuItem *wxMenu::DoInsert(size_t pos, wxMenuItem *item)
{
    // TODO(dom-phase-2): create a DOM node for the item.
    return wxMenuBase::DoInsert(pos, item);
}

wxMenuItem *wxMenu::DoRemove(wxMenuItem *item)
{
    // TODO(dom-phase-2): remove the item's DOM node.
    return wxMenuBase::DoRemove(item);
}

// ----------------------------------------------------------------------------
// wxMenuBar
// ----------------------------------------------------------------------------

#if wxUSE_MENUBAR

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

    // TODO(dom-phase-2): create a DOM node for the menu.

    return true;
}

bool wxMenuBar::Insert(size_t pos, wxMenu *menu, const wxString& title)
{
    if (!wxMenuBarBase::Insert(pos, menu, title))
        return false;

    menu->SetTitle(title);
    m_enabledTop.insert(m_enabledTop.begin() + pos, true);

    // TODO(dom-phase-2): create a DOM node for the menu.

    return true;
}

wxMenu *wxMenuBar::Remove(size_t pos)
{
    wxMenu *menu = wxMenuBarBase::Remove(pos);
    if (menu)
    {
        m_enabledTop.erase(m_enabledTop.begin() + pos);

        // TODO(dom-phase-2): remove the menu's DOM node.
    }

    return menu;
}

void wxMenuBar::EnableTop(size_t pos, bool enable)
{
    wxCHECK_RET(pos < m_enabledTop.size(), wxT("invalid menu index"));

    m_enabledTop[pos] = enable;

    // TODO(dom-phase-2): reflect the enabled state on the DOM node.
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

    // TODO(dom-phase-2): update the DOM node's label.
}

wxString wxMenuBar::GetMenuLabel(size_t pos) const
{
    wxCHECK_MSG(pos < GetMenuCount(), wxString(), wxT("invalid menu index"));

    return GetMenu(pos)->GetTitle();
}

void wxMenuBar::Attach(wxFrame *frame)
{
    wxMenuBarBase::Attach(frame);

    // TODO(dom-phase-2): attach the menu bar's DOM node to the frame's.
}

void wxMenuBar::Detach()
{
    // TODO(dom-phase-2): detach the menu bar's DOM node from the frame's.

    wxMenuBarBase::Detach();
}

#endif // wxUSE_MENUBAR

#endif // wxUSE_MENUS
