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

#include <emscripten.h>

#if wxUSE_COMBOBOX || wxUSE_COMBOCTRL
#include "wx/combo.h"
#endif

#define TRACE_WINDOW wxT("window")
#define TRACE_PAINT wxT("paint")

wxWindow *g_mouseWindow = NULL;

static wxWindowWasm *gs_focusWindow = NULL;
static wxWindowWasm *gs_nextFocusWindow = NULL;
static wxWindowWasm *gs_captureWindow = NULL;

// ----------------------------------------------------------------------------
// Element Tracking for E2E Tests
// ----------------------------------------------------------------------------

// Helper to update element in JS registry
static void UpdateElementRegistry(wxWindowWasm* window, bool isNew)
{
    if (!window) return;

    // Skip updates (isNew=false) if window isn't fully created yet.
    // This prevents calling virtual methods on partially constructed objects
    // during the construction chain (e.g., when SetSize calls DoMoveWindow).
    if (!isNew && !window->IsWasmCreated()) {
        return;
    }

    // Get element info
    uintptr_t id = reinterpret_cast<uintptr_t>(window);

    wxString typeName;

    // Get class name from RTTI first - we need it to check for problematic widgets
    wxClassInfo* classInfo = window->GetClassInfo();
    if (classInfo) {
        typeName = classInfo->GetClassName();
    }

    // If RTTI returns a base class type (like "wxControl" or "wxWindow"), the object
    // is still being constructed and its derived class vtable isn't set up yet.
    // Calling virtual methods on such objects can cause null function pointer calls.
    // Skip registration entirely for these partially constructed objects.
    bool isBaseClassType = (typeName == wxT("wxControl") ||
                            typeName == wxT("wxWindow") ||
                            typeName == wxT("wxWindowWasm") ||
                            typeName == wxT("wxPanel"));
    if (isBaseClassType && isNew) {
        // The object is still being constructed. Skip registration now;
        // the derived class will handle proper registration when fully constructed.
        return;
    }

    // Some widgets (e.g. wxCollapsiblePane) override GetLabel() to access child
    // widgets that don't exist yet during base class construction. Skip GetLabel()
    // for these widgets to avoid WASM memory access errors (NULL pointer dereference).
    wxString label;
    wxString name;
    bool skipGetLabel = (typeName == wxT("wxGenericCollapsiblePane") ||
                         typeName == wxT("wxCollapsiblePane") ||
                         typeName == wxT("WX_COLLAPSIBLE_PANE") ||
                         typeName == wxT("WX_COLLAPSIBLE_PANE_HEADER"));
    if (!skipGetLabel) {
        label = window->GetLabel();
    }
    name = window->GetName();

    // Get screen position
    wxPoint screenPos = window->GetScreenPosition();
    wxSize size = window->GetSize();

    // Check visibility and enabled state
    bool visible = window->IsShownOnScreen();
    bool enabled = window->IsEnabled();

    // Get parent ID
    uintptr_t parentId = 0;
    if (window->GetParent()) {
        parentId = reinterpret_cast<uintptr_t>(window->GetParent());
    }

    // Call the appropriate JavaScript helper function (defined in wx.js)
    if (isNew) {
        EM_ASM({
            wxElementRegister(
                $0.toString(),
                UTF8ToString($1),
                UTF8ToString($2),
                UTF8ToString($3),
                $4, $5, $6, $7,
                $8 ? $8.toString() : null,
                $9 ? true : false,
                $10 ? true : false
            );
        },
        id,
        label.utf8_str().data(),
        name.utf8_str().data(),
        typeName.utf8_str().data(),
        screenPos.x, screenPos.y,
        size.GetWidth(), size.GetHeight(),
        parentId,
        visible ? 1 : 0,
        enabled ? 1 : 0);
    } else {
        EM_ASM({
            wxElementUpdate(
                $0.toString(),
                UTF8ToString($1),
                UTF8ToString($2),
                UTF8ToString($3),
                $4, $5, $6, $7,
                $8 ? $8.toString() : null,
                $9 ? true : false,
                $10 ? true : false
            );
        },
        id,
        label.utf8_str().data(),
        name.utf8_str().data(),
        typeName.utf8_str().data(),
        screenPos.x, screenPos.y,
        size.GetWidth(), size.GetHeight(),
        parentId,
        visible ? 1 : 0,
        enabled ? 1 : 0);
    }
}

// Helper to remove element from JS registry
static void UnregisterElement(wxWindowWasm* window)
{
    if (!window) return;

    uintptr_t id = reinterpret_cast<uintptr_t>(window);

    EM_ASM({
        wxElementUnregister($0.toString());
    }, id);
}

// SetLabel implementation - updates element registry when label changes
void wxWindowWasm::SetLabel(const wxString& label)
{
    m_label = label;

    // Update element registry so tests can find elements by new label
    if (IsWasmCreated()) {
        UpdateElementRegistry(this, false);
    }
}

// ----------------------------------------------------------------------------
// Rendered Element Tracking (toolbar tools, menu items, splitter sashes, etc.)
// ----------------------------------------------------------------------------

// Register a rendered element (not a wxWindow, but drawn by a parent control)
void WasmRegisterRenderedElement(
    wxWindow* parent,
    const char* elementType,  // "tool", "menuitem", "sash", "auipart"
    const char* subType,      // e.g., "button", "separator", "caption"
    int index,
    const wxString& label,
    const wxString& tooltip,
    int screenX, int screenY,
    int width, int height,
    bool enabled)
{
    if (!parent) return;

    uintptr_t parentId = reinterpret_cast<uintptr_t>(parent);

    // Create unique ID: parentId:elementType:index
    EM_ASM({
        var id = $0.toString() + ':' + UTF8ToString($1) + ':' + $2;
        wxRenderedElementRegister(
            id,
            $0.toString(),
            UTF8ToString($1),
            UTF8ToString($3),
            UTF8ToString($4),
            UTF8ToString($5),
            $6, $7, $8, $9,
            $10 ? true : false,
            $2
        );
    },
    parentId,
    elementType,
    index,
    subType,
    label.utf8_str().data(),
    tooltip.utf8_str().data(),
    screenX, screenY,
    width, height,
    enabled ? 1 : 0);
}

// Unregister all rendered elements for a parent
void WasmUnregisterRenderedElementsByParent(wxWindow* parent)
{
    if (!parent) return;

    uintptr_t parentId = reinterpret_cast<uintptr_t>(parent);

    EM_ASM({
        wxRenderedElementUnregisterByParent($0.toString());
    }, parentId);
}

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
    // Unregister from JS tracking system before destruction
    UnregisterElement(this);

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
    m_isCreated = false;
}

bool wxWindowWasm::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxPoint& pos,
                          const wxSize& size,
                          long style,
                          const wxString& name)
{
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

    // Mark window as created before registering
    m_isCreated = true;

    // Register element in JS tracking system
    UpdateElementRegistry(this, true);

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

// Helper to dismiss any combo popups in a window's children when the window is hidden
static void DismissChildPopups(wxWindowWasm* window)
{
#if wxUSE_COMBOBOX || wxUSE_COMBOCTRL
    wxWindowList& children = window->GetChildren();
    for (wxWindowList::iterator i = children.begin(); i != children.end(); ++i)
    {
        wxWindow* child = *i;
        if (child)
        {
            // Check if this child is a wxComboCtrl with an open popup
            wxComboCtrlBase* combo = dynamic_cast<wxComboCtrlBase*>(child);
            if (combo && combo->IsPopupShown())
            {
                combo->HidePopup(true);
            }

            // Recursively check grandchildren
            wxWindowWasm* wasmChild = dynamic_cast<wxWindowWasm*>(child);
            if (wasmChild)
            {
                DismissChildPopups(wasmChild);
            }
        }
    }
#else
    wxUnusedVar(window);
#endif
}

bool wxWindowWasm::Show(bool show)
{
    if (wxWindowBase::Show(show))
    {
        // When hiding a window, dismiss any popup menus/combos in children
        // before they become invisible. This fixes the issue where dropdown
        // popups stay visible when switching notebook tabs.
        if (!show)
        {
            DismissChildPopups(this);
        }

        // Notify children that parent visibility changed so they can update
        // their platform-specific state (e.g., wxGLCanvas DOM visibility).
        // We call Show() with the child's current state to trigger any overrides
        // without changing the child's logical show state.
        UpdateChildrenDOMVisibility();

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

        // Update visibility in element registry
        UpdateElementRegistry(this, false);

        return true;
    }
    else
    {
        return false;
    }
}

void wxWindowWasm::UpdateChildrenDOMVisibility()
{
    wxWindowList& children = GetChildren();
    for (wxWindowList::iterator i = children.begin(); i != children.end(); ++i)
    {
        wxWindow *child = *i;
        if (child)
        {
            // Call Show() on child with its current state to trigger any
            // platform-specific visibility updates (like wxGLCanvas DOM update).
            // This doesn't change the child's logical state.
            child->Show(child->IsShown());

            // Update element registry for child - visibility may have changed
            // when parent becomes visible, even if child's own state didn't change
            wxWindowWasm* wasmChild = dynamic_cast<wxWindowWasm*>(child);
            if (wasmChild)
            {
                UpdateElementRegistry(wasmChild, false);

                // Recursively update grandchildren
                wasmChild->UpdateChildrenDOMVisibility();
            }
        }
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

        // Update element position in JS registry
        UpdateElementRegistry(this, false);
    }
}

void wxWindowWasm::DoEnable(bool enable)
{
    if (!enable && HasFocus())
    {
        KillFocus();
    }

    // Update enabled state in element registry
    UpdateElementRegistry(this, false);
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

