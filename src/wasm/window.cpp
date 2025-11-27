/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/window.cpp
// Purpose:     wxWasmWindow implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/window.h"

#include "wx/app.h"
#include "wx/caret.h"
#include "wx/dcclient.h"
#include "wx/dnd.h"
#include "wx/log.h"
#include "wx/menu.h"
#include "wx/nonownedwnd.h"
#include "wx/wasm/private/display.h"

#define TRACE_WINDOW wxT("window")
#define TRACE_PAINT wxT("paint")

wxWindow *g_mouseWindow = NULL;

static wxWindowWasm *gs_focusWindow = NULL;
static wxWindowWasm *gs_nextFocusWindow = NULL;
static wxWindowWasm *gs_captureWindow = NULL;

// ----------------------------------------------------------------------------
// wxWindowWasm
// ----------------------------------------------------------------------------

// in wxUniv/MSW this class is abstract because it doesn't have DoPopupMenu()
// method
#ifdef __WXUNIVERSAL__
IMPLEMENT_ABSTRACT_CLASS(wxWindowWasm, wxWindowBase)
#endif // __WXUNIVERSAL__

wxWindowWasm::wxWindowWasm()
{
    Init();
}

wxWindowWasm::wxWindowWasm(wxWindow *parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxString& name)
{
    Init();
    bool retval = wxWindowWasm::Create(parent, id, pos, size, style, name);
    wxASSERT_MSG(retval, wxT("error creating window"));
}

wxWindowWasm::~wxWindowWasm()
{
    SendDestroyEvent();

    if (g_mouseWindow == this)
    {
        g_mouseWindow = NULL;
    }
    if (gs_focusWindow == this)
    {
        gs_focusWindow = NULL;
    }
    if (gs_nextFocusWindow == this)
    {
        gs_nextFocusWindow = NULL;
    }
    if (gs_captureWindow == this)
    {
        wxFAIL_MSG(wxT("Destroying window with mouse capture"));
        ReleaseMouse();
    }

    DestroyChildren();
}

void wxWindowWasm::Init()
{
    m_x = 0;
    m_y = 0;
    m_width = 0;
    m_height = 0;

    m_childNeedsPaint = true;
    m_selfNeedsPaint = true;
}

bool wxWindowWasm::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxPoint& pos,
                          const wxSize& size,
                          long style,
                          const wxString& name)
{
    //printf("wxWindowWasm::Create\n");
    if (!CreateBase(parent, id, pos, size, style, wxDefaultValidator, name))
    {
        return false;
    }

    if (parent)
    {
        parent->AddChild(this);
    }

    int x = pos.x;
    int y = pos.y;
    if (x == wxDefaultCoord)
    {
        x = 0;
    }
    if (y == wxDefaultCoord)
    {
        y = 0;
    }
    int w = WidthDefault(size.x);
    int h = HeightDefault(size.y);
    SetSize(x, y, w, h);

    return true;
}

void wxWindowWasm::Raise()
{
    if (GetParent())
    {
        wxWindowList& children = GetParent()->GetChildren();
        children.DeleteObject(this);
        children.Append(this);
    }
}

void wxWindowWasm::Lower()
{
    if (GetParent())
    {
        wxWindowList& children = GetParent()->GetChildren();
        children.DeleteObject(this);
        children.Insert(this);
    }
}

bool wxWindowWasm::Show(bool show)
{
    if (wxWindowBase::Show(show))
    {
        if (show)
        {
            Refresh();
        }
        else if (GetParent())
        {
            GetParent()->Refresh();
        }

        wxShowEvent eventShow(GetId(), show);
        eventShow.SetEventObject(this);
        HandleWindowEvent(eventShow);

        return true;
    }
    else
    {
        return false;
    }
}

void wxWindowWasm::SetFocus()
{
    if ( gs_focusWindow == this || !CanAcceptFocus() )
        return; // nothing to do, focused already

    wxWindowWasm *prevFocusWindow = gs_focusWindow;

    if (gs_focusWindow != NULL)
    {
        gs_nextFocusWindow = this;
        gs_focusWindow->KillFocus();
        gs_nextFocusWindow = NULL;
    }

    gs_focusWindow = this;

    wxChildFocusEvent eventFocus(static_cast<wxWindow*>(this));
    HandleWindowEvent(eventFocus);

    wxFocusEvent event(wxEVT_SET_FOCUS, GetId());
    event.SetEventObject(this);
    event.SetWindow(static_cast<wxWindow*>(prevFocusWindow));
    HandleWindowEvent(event);

#if wxUSE_CARET
    // caret needs to be informed about focus change
    wxCaret *caret = GetCaret();
    if ( caret )
        caret->OnSetFocus();
#endif // wxUSE_CARET
}

void wxWindowWasm::KillFocus()
{
    wxCHECK_RET(gs_focusWindow == this,
                "killing focus on window that doesn't have it" );

    gs_focusWindow = NULL;

    if ( m_isBeingDeleted )
        return; // don't send any events from dtor

#if wxUSE_CARET
    // caret needs to be informed about focus change
    wxCaret *caret = GetCaret();
    if ( caret )
        caret->OnKillFocus();
#endif // wxUSE_CARET

    wxFocusEvent event(wxEVT_KILL_FOCUS, GetId());
    event.SetEventObject(this);
    event.SetWindow(static_cast<wxWindow*>(gs_nextFocusWindow));
    HandleWindowEvent(event);
}

void wxWindowWasm::WarpPointer(int WXUNUSED(x), int WXUNUSED(y))
{
    wxFAIL_MSG("WarpPointer is not supported");
}

void wxWindowWasm::Refresh(bool WXUNUSED(eraseBackground), const wxRect *WXUNUSED(rect))
{
    //printf("Refresh: %p %d %d\n", this, IsShown(), IsFrozen());
    if (!IsShown() || IsFrozen())
    {
        return;
    }

    Invalidate(true);
}

bool wxWindowWasm::HasTransparentBackground()
{
    return GetBackgroundStyle() == wxBG_STYLE_TRANSPARENT ||
           GetBackgroundColour().Alpha() == 0;
}

void wxWindowWasm::Invalidate(bool needsPaint)
{
    if (!m_childNeedsPaint || m_selfNeedsPaint != needsPaint)
    {
        m_selfNeedsPaint |= needsPaint;
        m_childNeedsPaint = true;

        if (GetParent())
        {
            bool parentNeedsPaint = needsPaint && HasTransparentBackground();
            GetParent()->Invalidate(parentNeedsPaint);
        }
    }
}

void wxWindowWasm::EraseBackgroundWindow()
{
    //printf("EraseBackgroundWindow\n");
    wxWindowDC dc(static_cast<wxWindow *>(this));
    wxEraseEvent eraseEvent(GetId(), &dc);
    eraseEvent.SetEventObject(this);
    HandleWindowEvent(eraseEvent);
}

void wxWindowWasm::PaintSelf()
{
    //wxRect r = GetScreenRect();
    //printf("PaintSelf: %p %d %d %d %d\n",
    //       this, r.GetX(), r.GetY(), r.GetWidth(), r.GetHeight());

    EraseBackgroundWindow();

    if (GetClientRect() != GetRect())
    {
        wxNcPaintEvent ncPaintEvent(this);
        HandleWindowEvent(ncPaintEvent);
    }

    wxPaintEvent paintEvent(this);
    HandleWindowEvent(paintEvent);

    m_selfNeedsPaint = false;
}

void wxWindowWasm::PaintChildren(bool selfWasPainted)
{
    //printf("PaintChildren: %p\n", this);
    wxWindowList& children = GetChildren();

    for (wxWindowList::iterator i = children.begin(); i != children.end(); ++i)
    {
        wxWindow *child = *i;

        wxASSERT(child);

        if (!child->IsFrozen() && child->IsShown())
        {
            if (child->NeedsPaint() || selfWasPainted)
            {
                child->DoPaint(selfWasPainted);
            }
        }
    }

    m_childNeedsPaint = false;
}

void wxWindowWasm::DoPaint(bool parentWasPainted)
{
    wxSize clientSize = GetClientSize();
    //printf("DoPaint: %p %d %d\n",
    //           this, clientSize.GetWidth(), clientSize.GetHeight());

    if (clientSize.GetWidth() <= 0 || clientSize.GetHeight() <= 0)
    {
        return;
    }

    if (IsShown() && !IsFrozen())
    {
        m_updateRegion = wxRect(GetSize());

        bool selfWasPainted;
        if (m_selfNeedsPaint || parentWasPainted)
        {
            PaintSelf();
            selfWasPainted = true;
        }
        else
        {
            selfWasPainted = false;
        }

        //if (m_childNeedsPaint || selfWasPainted) {
        PaintChildren(selfWasPainted);
        //}

        m_updateRegion.Clear();
    }
}

bool wxWindowWasm::SetFont(const wxFont& font)
{
    m_font = font;
    return true;
}

bool wxWindowWasm::SetCursor(const wxCursor &cursor)
{
    if (!wxWindowBase::SetCursor(cursor))
    {
        return false;
    }

    bool mouseInsideWindow = GetScreenRect().Contains(wxGetMousePosition());

    if (GetCapture() == NULL && mouseInsideWindow)
    {
        if (cursor.IsOk())
        {
            wxSetCursor(cursor);
        }
        else
        {
            wxSetCursor(*wxSTANDARD_CURSOR);
        }
    }

    return true;
}

int wxWindowWasm::GetCharWidth() const
{
    wxCoord charWidth;
    m_font.GetCharSize(&charWidth, NULL);
    return charWidth;
}

int wxWindowWasm::GetCharHeight() const
{
    wxCoord charHeight;
    m_font.GetCharSize(NULL, &charHeight);
    return charHeight;
}

double wxWindowWasm::GetContentScaleFactor() const
{
    return wxContentScaleFactor();
}

double wxWindowWasm::GetDPIScaleFactor() const
{

    return GetContentScaleFactor();
}

void wxWindowWasm::DoGetTextExtent(const wxString& string,
                                   int *x, int *y,
                                   int *descent,
                                   int *externalLeading,
                                   const wxFont *theFont) const
{
    const wxFont *font = (!theFont || !theFont->IsOk()) ? &m_font : theFont;
    font->GetTextExtent(string, x, y, descent, externalLeading);
}

#if wxUSE_DRAG_AND_DROP
void wxWindowWasm::SetDropTarget(wxDropTarget *dropTarget)
{
    delete m_dropTarget;
    m_dropTarget = dropTarget;
}
#endif // wxUSE_DRAG_AND_DROP

wxNonOwnedWindow* wxWindowWasm::GetTopLevelWindow()
{
    wxWindowWasm* window = this;

    while (!window->IsTopLevel())
    {
        window = window->GetParent();
    }

    return static_cast<wxNonOwnedWindow*>(window);
}

static wxPoint GetScreenPositionOfClientOrigin(const wxWindowWasm *win)
{
    wxCHECK_MSG(win, wxPoint(0, 0), "no window provided");

    wxPoint pt(win->GetPosition() + win->GetClientAreaOrigin());

    if (!win->IsTopLevel())
    {
        pt += GetScreenPositionOfClientOrigin(win->GetParent());
    }

    return pt;
}

void wxWindowWasm::DoClientToScreen(int *x, int *y) const
{
    wxPoint origin = GetScreenPositionOfClientOrigin(this);

    if (x)
    {
        *x += origin.x;
    }
    if (y)
    {
        *y += origin.y;
    }
}

void wxWindowWasm::DoScreenToClient(int *x, int *y) const
{
    wxPoint origin = GetScreenPositionOfClientOrigin(this);

    if (x)
    {
        *x -= origin.x;
    }
    if (y)
    {
        *y -= origin.y;
    }
}

void wxWindowWasm::DoGetPosition(int *x, int *y) const
{
    if (x)
    {
        *x = m_x;
    }
    if (y)
    {
        *y = m_y;
    }
}

void wxWindowWasm::DoGetSize(int *width, int *height) const
{
    if (width)
    {
        *width = m_width;
    }
    if (height)
    {
        *height = m_height;
    }
}

void wxWindowWasm::DoGetClientSize(int *width, int *height) const
{
    DoGetSize(width, height);
}

void wxWindowWasm::DoSetSize(int x, int y,
                             int width, int height,
                             int sizeFlags)
{
    //printf("DoSetSize: %d %d %d %d\n", x, y, width, height);
    int currentX, currentY;
    DoGetPosition(&currentX, &currentY);
    int currentW, currentH;
    DoGetSize(&currentW, &currentH);

    if ((x == wxDefaultCoord) && !(sizeFlags & wxSIZE_ALLOW_MINUS_ONE))
    {
        x = currentX;
    }
    if ((y == wxDefaultCoord) && !(sizeFlags & wxSIZE_ALLOW_MINUS_ONE))
    {
        y = currentY;
    }


    wxSize size(wxDefaultSize);

    if (width == wxDefaultCoord)
    {
        if (sizeFlags & wxSIZE_AUTO_WIDTH)
        {
            size = DoGetBestSize();
            width = size.x;
        }
        else
        {
            width = currentW;
        }
    }
    if (height == wxDefaultCoord)
    {
        if (sizeFlags & wxSIZE_AUTO_HEIGHT)
        {
            if (size.x == wxDefaultCoord)
            {
                size = DoGetBestSize();
            }
            height = size.y;
        }
        else
        {
            height = currentH;
        }
    }

    /*
        int maxWidth = GetMaxWidth();
        int minWidth = GetMinWidth();
        int maxHeight = GetMaxHeight();
        int minHeight = GetMinHeight();
        if (minWidth != wxDefaultCoord && width < minWidth)
            width = minWidth;
        if (maxWidth != wxDefaultCoord && width > maxWidth)
            width = maxWidth;
        if (minHeight != wxDefaultCoord && height < minHeight)
            height = minHeight;
        if (maxHeight != wxDefaultCoord && height > maxHeight)
            height = maxHeight;
    */

    if (x != currentX || y != currentY || width != currentW || height != currentH)
    {
        Invalidate(true);

        AdjustForParentClientOrigin(x, y, sizeFlags);
        DoMoveWindow(x, y, width, height);

        wxSize newSize(width, height);
        wxSizeEvent event(newSize, GetId());
        event.SetEventObject(this);
        HandleWindowEvent(event);
    }
}

void wxWindowWasm::DoSetClientSize(int width, int height)
{
    SetSize(width, height);
}

void wxWindowWasm::DoMoveWindow(int x, int y, int width, int height)
{
    if (IsTopLevel() && GetTopLevelWindow()->IsMainFrame())
    {
        x = 0;
        y = 0;
    }

    wxPoint parentOrigin(0, 0);
    AdjustForParentClientOrigin(parentOrigin.x, parentOrigin.y);

    int clientX = x - parentOrigin.x;
    int clientY = y - parentOrigin.y;

    if (m_x != clientX || m_y != clientY || m_width != width || m_height != height)
    {
        wxRect oldPos = wxRect(m_x, m_y, m_width, m_height);
        oldPos.Offset(parentOrigin);

        wxRect newPos = wxRect(x, y, width, height);

        m_x = clientX;
        m_y = clientY;
        m_width = width;
        m_height = height;

        wxWindow *parent = GetParent();

        if (parent != NULL)
        {
            parent->RefreshRect(oldPos);
            parent->RefreshRect(newPos);
        }
    }
}

void wxWindowWasm::DoEnable(bool enable)
{
    if (!enable && HasFocus())
    {
        KillFocus();
    }
}

void wxWindowWasm::DoCaptureMouse()
{
    gs_captureWindow = this;
}

void wxWindowWasm::DoReleaseMouse()
{
    wxASSERT_MSG(gs_captureWindow == this, wxT("attempt to release mouse, but this window hasn't captured it"));

    gs_captureWindow = NULL;
}

void wxWindowWasm::DoThaw()
{
    if (IsShown())
    {
        Invalidate(true);
    }
}

// ----------------------------------------------------------------------------
// this wxWindowBase function is implemented here (in platform-specific file)
// because it is static and so couldn't be made virtual
// ----------------------------------------------------------------------------

/* static */
wxWindow *wxWindowBase::DoFindFocus()
{
    return static_cast<wxWindow*>(gs_focusWindow);
}

/* static */
wxWindow *wxWindowBase::GetCapture()
{
    return static_cast<wxWindow*>(gs_captureWindow);
}

wxWindow *wxGetActiveWindow()
{
    return wxWindow::FindFocus();
}

void wxGetMousePosition(int* x, int* y)
{
    wxTheApp->GetMousePosition(x, y);
}

wxPoint wxGetMousePosition()
{
    wxPoint point;
    wxGetMousePosition(&point.x, &point.y);
    return point;
}

wxMouseState wxGetMouseState()
{
    wxMouseState mouseState;
    wxTheApp->GetMouseState(&mouseState);
    return mouseState;
}

bool wxGetKeyState(wxKeyCode keyCode)
{
    return wxTheApp->IsKeyPressed(keyCode);
}

wxWindow* wxFindWindowAtPoint(const wxPoint& pt)
{
    return wxGenericFindWindowAtPoint(pt);
}

