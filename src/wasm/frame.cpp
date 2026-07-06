/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/frame.cpp
// Purpose:     wxFrame implementation for the WASM DOM port.
//              Bar positioning mirrors src/univ/framuniv.cpp so DOM and
//              canvas builds produce the same frame geometry.
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/frame.h"

#ifndef WX_PRECOMP
    #include "wx/menu.h"
    #include "wx/toolbar.h"
    #include "wx/statusbr.h"
#endif

// Note: the wxClassInfo for wxFrame is implemented centrally in
// src/common/framecmn.cpp, so no wxIMPLEMENT_DYNAMIC_CLASS here.

wxBEGIN_EVENT_TABLE(wxFrame, wxFrameBase)
    EVT_SIZE(wxFrame::OnSize)
wxEND_EVENT_TABLE()

wxFrame::wxFrame()
{
}

wxFrame::wxFrame(wxWindow *parent,
                 wxWindowID id,
                 const wxString& title,
                 const wxPoint& pos,
                 const wxSize& size,
                 long style,
                 const wxString& name)
{
    Create(parent, id, title, pos, size, style, name);
}

bool wxFrame::Create(wxWindow *parent,
                     wxWindowID id,
                     const wxString& title,
                     const wxPoint& pos,
                     const wxSize& size,
                     long style,
                     const wxString& name)
{
    return wxTopLevelWindow::Create(parent, id, title, pos, size, style, name);
}

void wxFrame::OnSize(wxSizeEvent& event)
{
#if wxUSE_MENUS
    PositionMenuBar();
#endif // wxUSE_MENUS
#if wxUSE_STATUSBAR
    PositionStatusBar();
#endif // wxUSE_STATUSBAR
#if wxUSE_TOOLBAR
    PositionToolBar();
#endif // wxUSE_TOOLBAR

    event.Skip();
}

#if wxUSE_MENUS

// Hidden bars take no space: every bar below is guarded with IsShown(), the
// native-port semantics (see wxMSW wxFrame::DoGetClientSize) — a Hide()den
// menubar/statusbar/toolbar releases its rows back to the client area.

void wxFrame::PositionMenuBar()
{
    if ( m_frameMenuBar && m_frameMenuBar->IsShown() )
    {
        // the menubar is positioned above the client area, hence the negative
        // y coord
        wxCoord heightMbar = m_frameMenuBar->GetSize().y;

        wxCoord heightTbar = 0;

#if wxUSE_TOOLBAR
        if ( m_frameToolBar && m_frameToolBar->IsShown() )
            heightTbar = m_frameToolBar->GetSize().y;
#endif // wxUSE_TOOLBAR

        m_frameMenuBar->SetSize(0,
                                - (heightMbar + heightTbar),
                                GetClientSize().x, heightMbar);
    }
}

void wxFrame::DetachMenuBar()
{
    wxFrameBase::DetachMenuBar();
    SendSizeEvent();
}

void wxFrame::AttachMenuBar(wxMenuBar *menubar)
{
    wxFrameBase::AttachMenuBar(menubar);
    SendSizeEvent();
}

#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR

void wxFrame::PositionStatusBar()
{
    if ( m_frameStatusBar && m_frameStatusBar->IsShown() )
    {
        wxSize size = GetClientSize();
        m_frameStatusBar->SetSize(0, size.y, size.x, wxDefaultCoord);
    }
}

wxStatusBar* wxFrame::CreateStatusBar(int number, long style,
                                      wxWindowID id, const wxString& name)
{
    wxStatusBar *bar = wxFrameBase::CreateStatusBar(number, style, id, name);
    SendSizeEvent();
    return bar;
}

#endif // wxUSE_STATUSBAR

#if wxUSE_TOOLBAR

wxToolBar* wxFrame::CreateToolBar(long style, wxWindowID id, const wxString& name)
{
    if ( wxFrameBase::CreateToolBar(style, id, name) )
    {
        PositionToolBar();
    }

    return m_frameToolBar;
}

void wxFrame::PositionToolBar()
{
    if ( m_frameToolBar && m_frameToolBar->IsShown() )
    {
        wxSize size = GetClientSize();
        int tw, th, tx, ty;

        tx = ty = 0;
        m_frameToolBar->GetSize(&tw, &th);
        if ( m_frameToolBar->GetWindowStyleFlag() & wxTB_VERTICAL )
        {
            tx = -tw;
            th = size.y;
        }
        else
        {
            ty = -th;
            tw = size.x;
        }

        m_frameToolBar->SetSize(tx, ty, tw, th);
    }
}
#endif // wxUSE_TOOLBAR

wxPoint wxFrame::GetClientAreaOrigin() const
{
    wxPoint pt = wxFrameBase::GetClientAreaOrigin();

#if wxUSE_MENUS
    if ( m_frameMenuBar && m_frameMenuBar->IsShown() )
    {
        pt.y += m_frameMenuBar->GetSize().y;
    }
#endif // wxUSE_MENUS

#if wxUSE_TOOLBAR
    if ( m_frameToolBar && m_frameToolBar->IsShown() )
    {
        if ( m_frameToolBar->GetWindowStyleFlag() & wxTB_VERTICAL )
            pt.x += m_frameToolBar->GetSize().x;
        else
            pt.y += m_frameToolBar->GetSize().y;
    }
#endif // wxUSE_TOOLBAR

    return pt;
}

void wxFrame::DoGetClientSize(int *width, int *height) const
{
    wxFrameBase::DoGetClientSize(width, height);

#if wxUSE_MENUS
    if ( m_frameMenuBar && m_frameMenuBar->IsShown() && height )
    {
        (*height) -= m_frameMenuBar->GetSize().y;
    }
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    if ( m_frameStatusBar && m_frameStatusBar->IsShown() && height )
    {
        (*height) -= m_frameStatusBar->GetSize().y;
    }
#endif // wxUSE_STATUSBAR

#if wxUSE_TOOLBAR
    if ( m_frameToolBar && m_frameToolBar->IsShown() )
    {
        if ( width && (m_frameToolBar->GetWindowStyleFlag() & wxTB_VERTICAL) )
            (*width) -= m_frameToolBar->GetSize().x;
        else if ( height )
            (*height) -= m_frameToolBar->GetSize().y;
    }
#endif // wxUSE_TOOLBAR
}

void wxFrame::DoSetClientSize(int width, int height)
{
#if wxUSE_MENUS
    if ( m_frameMenuBar && m_frameMenuBar->IsShown() )
    {
        height += m_frameMenuBar->GetSize().y;
    }
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    if ( m_frameStatusBar && m_frameStatusBar->IsShown() )
    {
        height += m_frameStatusBar->GetSize().y;
    }
#endif // wxUSE_STATUSBAR

#if wxUSE_TOOLBAR
    if ( m_frameToolBar && m_frameToolBar->IsShown() )
    {
        if ( m_frameToolBar->GetWindowStyleFlag() & wxTB_VERTICAL )
            width += m_frameToolBar->GetSize().x;
        else
            height += m_frameToolBar->GetSize().y;
    }
#endif // wxUSE_TOOLBAR

    wxFrameBase::DoSetClientSize(width, height);
}
