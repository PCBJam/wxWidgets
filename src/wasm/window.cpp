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

#include "wx/settings.h"
#include "wx/wasm/private/dispatch.h"
#include "wx/wasm/private/dom.h"

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

// Rendered-element tracking (grid cells, list rows, AUI parts...) lives in
// src/wasm/elementtracker.cpp.

// ----------------------------------------------------------------------------
// wxWindowWasm
// ----------------------------------------------------------------------------

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
    if (m_domId)
    {
        wxDomUnregisterWindow(m_domId);
        wxDomDestroyControl(m_domId);
        m_domId = 0;
    }

    DestroyScrollbarDom();

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
#if wxUSE_TOOLTIPS
    // Drop the hovered-window pointer too, so a pending tooltip timer can't
    // fire on this freed window (src/wasm/tooltip.cpp).
    extern void wxWasmTooltipForgetWindow(wxWindow *win);
    wxWasmTooltipForgetWindow(static_cast<wxWindow *>(this));
#endif
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

    for ( int orient = 0; orient < 2; orient++ )
    {
        m_scrollPos[orient] = 0;
        m_scrollThumb[orient] = 0;
        m_scrollRange[orient] = 0;
        m_scrollbarDom[orient] = 0;
    }

    m_domId = 0;
    m_domClipped = false;
}

// ----------------------------------------------------------------------------
// DOM-backed native controls
// ----------------------------------------------------------------------------

bool wxWindowWasm::WasmCreateDomNode(const char *tag, const char *typeAttr)
{
    wxASSERT_MSG(m_domId == 0, wxT("window already has a DOM node"));

    wxNonOwnedWindow *tlw = GetTopLevelWindow();
    if ( !tlw )
        return false;

    m_domId = wxDomCreateControl(tlw->GetCSSId(), tag, typeAttr);
    if ( m_domId == 0 )
        return false;

    wxDomRegisterWindow(m_domId, this);

    wxDomSetFont(m_domId, GetFont().GetNativeFontInfoDesc());
    wxDomSetEnabled(m_domId, IsEnabled());
    wxDomSetShown(m_domId, IsShownOnScreen());
    UpdateDomGeometry();

    return true;
}

void wxWindowWasm::UpdateDomGeometry()
{
    // The starting clip is the intersection of every non-TLW ancestor's
    // client rect (TLW coords) — same semantics as the paint-DC clip walk
    // in dcclient.cpp. The TLW box itself is excluded: frame bars live at
    // negative client offsets and the container div's overflow:hidden
    // already bounds the TLW.
    wxRect clip;
    bool hasClip = false;
    ComputeAncestorClip(&clip, &hasClip);
    UpdateDomGeometryRecursive(hasClip ? &clip : NULL);
}

void wxWindowWasm::ComputeAncestorClip(wxRect *clip, bool *hasClip)
{
    *hasClip = false;

    const wxNonOwnedWindow *tlw = GetTopLevelWindow();
    if ( !tlw )
        return;

    const wxPoint tlwOrigin = tlw->GetScreenPosition();

    for ( const wxWindow *anc = GetParent();
          anc && anc != tlw && !anc->IsTopLevel();
          anc = anc->GetParent() )
    {
        // GetScreenPosition() is ClientToScreen(0,0), i.e. already the
        // client-area origin — do NOT add GetClientAreaOrigin() again
        // (that shifted clips down by the strip height for wxNotebook).
        const wxPoint clientTLW = anc->GetScreenPosition() - tlwOrigin;
        const wxRect clientRect(clientTLW, anc->GetClientSize());

        if ( !*hasClip )
        {
            *clip = clientRect;
            *hasClip = true;
        }
        else
        {
            clip->Intersect(clientRect);
        }
    }
}

void wxWindowWasm::UpdateDomGeometryRecursive(const wxRect *ancestorClip)
{
    wxNonOwnedWindow *tlw = GetTopLevelWindow();
    if ( !tlw )
        return;

    // GetScreenPosition() is the CLIENT-AREA origin (ClientToScreen(0,0)),
    // which equals the top-left corner only while GetClientAreaOrigin() is
    // (0,0). The element box needs the top-left: without the correction a
    // wxNotebook's box (tab strip included) rendered a strip-height too
    // low, overlapping its own page area.
    const wxPoint pos = GetScreenPosition() - GetClientAreaOrigin()
                        - tlw->GetScreenPosition();

    if ( m_domId )
    {
        // Element is absolutely positioned inside the TLW container div.
        wxDomSetRect(m_domId, pos.x, pos.y, m_width, m_height);

        // Clip to the accumulated ancestor viewport (clip-path insets are
        // relative to the element's own box). Cached: the common case is
        // "unclipped", which must not cost a JS crossing per layout.
        int t = 0, r = 0, b = 0, l = 0;
        if ( ancestorClip )
        {
            const wxRect own(pos.x, pos.y, m_width, m_height);
            wxRect vis = own;
            vis.Intersect(*ancestorClip);
            if ( vis.IsEmpty() )
            {
                t = m_height > 0 ? m_height : 1;
            }
            else
            {
                t = vis.y - own.y;
                l = vis.x - own.x;
                b = (own.y + own.height) - (vis.y + vis.height);
                r = (own.x + own.width) - (vis.x + vis.width);
            }
        }

        const wxRect newClip(l, t, r, b); // abuse wxRect as a 4-int tuple
        const bool clipped = t > 0 || r > 0 || b > 0 || l > 0;
        if ( clipped != m_domClipped || (clipped && newClip != m_domClip) )
        {
            wxDomSetClip(m_domId, t, r, b, l);
            m_domClip = newClip;
            m_domClipped = clipped;
        }
    }

    // Built-in scrollbar gutters (if any) ride along this window's client
    // edges in the same TLW coordinate space — reproject + reclip them too.
    PositionScrollbarDom(pos, ancestorClip);

    // Children are clipped by this window's client area as well (unless
    // this is a TLW, whose children start unclipped — see above).
    wxRect childClip;
    const wxRect *childClipPtr = NULL;
    if ( !IsTopLevel() )
    {
        childClip = wxRect(pos + GetClientAreaOrigin(), GetClientSize());
        if ( ancestorClip )
            childClip.Intersect(*ancestorClip);
        childClipPtr = &childClip;
    }

    // DOM rects are TLW-relative, so when THIS window moves, every
    // DOM-backed descendant's on-screen position changes even though its
    // wx (parent-relative) rect didn't — refresh them all.
    for ( wxWindowList::compatibility_iterator node = GetChildren().GetFirst();
          node; node = node->GetNext() )
    {
        wxWindowWasm *child = static_cast<wxWindowWasm *>(node->GetData());
        child->UpdateDomGeometryRecursive(childClipPtr);
    }
}

void wxWindowWasm::UpdateDomVisibility()
{
    if ( m_domId )
        wxDomSetShown(m_domId, IsShownOnScreen());

    // Built-in scrollbar gutters track the owning window's visibility too: a
    // wxScrolledWindow on a notebook page that's just become the active tab
    // needs its gutters (re)shown and positioned. The m_domId line above only
    // covers the window's own node, not the auxiliary gutter elements, and a
    // pure show/hide doesn't move the window (so the geometry walk that would
    // otherwise reposition them never runs).
    RefreshScrollbarGeometry();

    for ( wxWindowList::compatibility_iterator node = GetChildren().GetFirst();
          node; node = node->GetNext() )
    {
        wxWindowWasm *child = static_cast<wxWindowWasm *>(node->GetData());
        child->UpdateDomVisibility();
    }
}

void wxWindowWasm::OnDomEvent(wxDomEventKind kind)
{
    switch ( kind )
    {
        case wxDOM_EVENT_FOCUSIN:
            // Keep the wx focus model truthful when the browser moves focus.
            if ( gs_focusWindow != this && CanAcceptFocus() )
                SetFocus();
            break;

        case wxDOM_EVENT_SCROLL:
        {
            // A built-in scrollbar gutter dragged: discriminate which one by
            // the firing element id, then drive wxScrollHelperBase via a
            // wxScrollWinEvent (it calls SetScrollPos + ScrollWindow, so the
            // existing child-move path runs unchanged).
            const int firedId = wxDomCurrentEventDomId();
            int i = -1;
            if ( firedId && firedId == m_scrollbarDom[1] )
                i = 1;
            else if ( firedId && firedId == m_scrollbarDom[0] )
                i = 0;
            if ( i < 0 )
                break;

            const int pos = wxDomGetIntValue(firedId);
            const int phase = wxDomGetScrollPhase(firedId);
            m_scrollPos[i] = pos;

            const int orient = (i == 1) ? wxVERTICAL : wxHORIZONTAL;

            wxScrollWinEvent track(wxEVT_SCROLLWIN_THUMBTRACK, pos, orient);
            track.SetEventObject(this);
            HandleWindowEvent(track);

            if ( phase == 1 ) // thumbrelease: settle on the final position
            {
                wxScrollWinEvent rel(wxEVT_SCROLLWIN_THUMBRELEASE, pos, orient);
                rel.SetEventObject(this);
                HandleWindowEvent(rel);
            }
            break;
        }

        default:
            // Controls override for click/input/change behavior.
            break;
    }
}

// ----------------------------------------------------------------------------
// Built-in scrollbars and popup menus. State is cached so
// wxScrollHelper-style callers behave consistently.
// TODO(dom-phase-2): render real scrollbars.
// ----------------------------------------------------------------------------

namespace
{
inline int wxScrollOrientIndex(int orient)
{
    return orient == wxVERTICAL ? 1 : 0;
}
} // anonymous namespace

void wxWindowWasm::SetScrollbar(int orient, int pos, int thumbvisible,
                                int range, bool refresh)
{
    const int i = wxScrollOrientIndex(orient);
    m_scrollPos[i] = pos;
    m_scrollThumb[i] = thumbvisible;
    m_scrollRange[i] = range;

    // Built-in gutter: appear only when the content overflows the viewport
    // (range > thumb), mirroring native scrollbar auto-hide. wx uses
    // thumbvisible as the gutter's page size.
    if ( range > thumbvisible && range > 0 )
    {
        EnsureScrollbarDom(i);
        if ( m_scrollbarDom[i] )
        {
            wxDomSetScrollbar(m_scrollbarDom[i], pos, thumbvisible, range,
                              thumbvisible);
            // Reproject/show the gutter at the (possibly new) client edge.
            UpdateDomGeometry();
        }
    }
    else if ( m_scrollbarDom[i] )
    {
        wxDomSetShown(m_scrollbarDom[i], false);
    }

    if ( refresh )
        Refresh();
}

void wxWindowWasm::SetScrollPos(int orient, int pos, bool refresh)
{
    const int i = wxScrollOrientIndex(orient);
    m_scrollPos[i] = pos;

    // Cheap thumb move (no geometry walk). The JS widget ignores this while
    // the user is actively dragging, so it never fights the drag.
    if ( m_scrollbarDom[i] )
        wxDomSetIntValue(m_scrollbarDom[i], pos);

    if ( refresh )
        Refresh();
}

int wxWindowWasm::GetScrollPos(int orient) const
{
    return m_scrollPos[wxScrollOrientIndex(orient)];
}

int wxWindowWasm::GetScrollThumb(int orient) const
{
    return m_scrollThumb[wxScrollOrientIndex(orient)];
}

int wxWindowWasm::GetScrollRange(int orient) const
{
    return m_scrollRange[wxScrollOrientIndex(orient)];
}

void wxWindowWasm::ScrollWindow(int dx, int dy, const wxRect *rect)
{
    // Move children like the universal port does (src/univ/winuniv.cpp):
    // wxScrollHelperBase relies on ScrollWindow physically moving child
    // windows. Their moves flow through DoMoveWindow → UpdateDomGeometry,
    // which repositions and re-clips the DOM subtree.
    const wxPoint offset(dx, dy);

    for ( wxWindowList::compatibility_iterator node = GetChildren().GetFirst();
          node; node = node->GetNext() )
    {
        wxWindow *child = node->GetData();

        // Univ semantics: with a rect and single-axis scrolling, move only
        // children intersecting the scrolled shaft.
        bool shouldMove = true;
        if ( rect && (dx * dy == 0) )
        {
            const wxRect childRect = child->GetRect();
            if ( dx == 0 )
            {
                shouldMove = childRect.GetLeft() <= rect->GetRight() &&
                             childRect.GetRight() >= rect->GetLeft();
            }
            else // dy == 0
            {
                shouldMove = childRect.GetTop() <= rect->GetBottom() &&
                             childRect.GetBottom() >= rect->GetTop();
            }
        }

        if ( shouldMove )
            child->Move(child->GetPosition() + offset, wxSIZE_ALLOW_MINUS_ONE);
    }

    // No incremental blit support; repaint the whole window.
    Refresh();
}

// ----------------------------------------------------------------------------
// Built-in scrollbar gutters: auxiliary DOM elements owned by this window
// (m_scrollbarDom), reusing the same 'scrollbar' widget as wxScrollBar. They
// are NOT wx children, so ScrollWindow()'s child-move walk never touches them.
// ----------------------------------------------------------------------------

void wxWindowWasm::EnsureScrollbarDom(int i)
{
    if ( m_scrollbarDom[i] )
        return;

    wxNonOwnedWindow *tlw = GetTopLevelWindow();
    if ( !tlw )
        return;

    const int id = wxDomCreateControl(tlw->GetCSSId(), "scrollbar",
                                      i == 1 ? "v" : "h");
    if ( id == 0 )
        return;

    m_scrollbarDom[i] = id;
    wxDomRegisterWindow(id, this);
}

void wxWindowWasm::PositionScrollbarDom(const wxPoint& tlwTopLeft,
                                        const wxRect *ancestorClip)
{
    if ( !m_scrollbarDom[0] && !m_scrollbarDom[1] )
        return;

    const int metric = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, this);
    const int sbWidth = metric > 0 ? metric : 17;

    const wxSize client = GetClientSize();
    const wxPoint clientTL = tlwTopLeft + GetClientAreaOrigin();

    for ( int i = 0; i < 2; i++ )
    {
        if ( !m_scrollbarDom[i] )
            continue;

        const bool needed = m_scrollRange[i] > m_scrollThumb[i] &&
                            m_scrollRange[i] > 0;
        const bool shown = needed && IsShownOnScreen();
        wxDomSetShown(m_scrollbarDom[i], shown);
        if ( !shown )
            continue;

        // Vertical gutter rides the right edge; horizontal the bottom edge.
        wxRect r;
        if ( i == 1 )
            r = wxRect(clientTL.x + client.x - sbWidth, clientTL.y,
                       sbWidth, client.y);
        else
            r = wxRect(clientTL.x, clientTL.y + client.y - sbWidth,
                       client.x, sbWidth);

        wxDomSetRect(m_scrollbarDom[i], r.x, r.y, r.width, r.height);

        // Clip to the ancestor viewport (same inset math as m_domId).
        int t = 0, rr = 0, b = 0, l = 0;
        if ( ancestorClip )
        {
            wxRect vis = r;
            vis.Intersect(*ancestorClip);
            if ( vis.IsEmpty() )
            {
                t = r.height > 0 ? r.height : 1;
            }
            else
            {
                t = vis.y - r.y;
                l = vis.x - r.x;
                b = (r.y + r.height) - (vis.y + vis.height);
                rr = (r.x + r.width) - (vis.x + vis.width);
            }
        }
        wxDomSetClip(m_scrollbarDom[i], t, rr, b, l);
    }
}

void wxWindowWasm::RefreshScrollbarGeometry()
{
    if ( !m_scrollbarDom[0] && !m_scrollbarDom[1] )
        return;

    wxNonOwnedWindow *tlw = GetTopLevelWindow();
    if ( !tlw )
        return;

    const wxPoint pos = GetScreenPosition() - GetClientAreaOrigin()
                        - tlw->GetScreenPosition();
    wxRect clip;
    bool hasClip = false;
    ComputeAncestorClip(&clip, &hasClip);
    PositionScrollbarDom(pos, hasClip ? &clip : NULL);
}

void wxWindowWasm::DestroyScrollbarDom()
{
    for ( int i = 0; i < 2; i++ )
    {
        if ( m_scrollbarDom[i] )
        {
            wxDomUnregisterWindow(m_scrollbarDom[i]);
            wxDomDestroyControl(m_scrollbarDom[i]);
            m_scrollbarDom[i] = 0;
        }
    }
}

#if wxUSE_MENUS

// Shows the DOM context menu and BLOCKS until an item is chosen or the menu
// is dismissed, returning the chosen command id (-1 = cancelled). The whole
// modal lifetime lives in JS (Module.wxShowContextMenu), mirroring
// wxDialog::ShowModal: the C++ side is a single suspended frame with no
// per-popup pump — the top-level tick dispatches while this chain is
// suspended (doc 17 S4). Route the menu promise through the shim's
// shadow-stack discipline (jspi-scheduler.js promiseYield; emscripten
// #27364).
EM_ASYNC_JS(int, wxDomPopupMenuModal,
            (const char *json, int invokerDomId, int x, int y), {
    return await globalThis.__wxScheduler.promiseYield(
        Module['wxShowContextMenu'](UTF8ToString(json), invokerDomId, x, y),
        'popup');
});

bool wxWindowWasm::DoPopupMenu(wxMenu *menu, int x, int y)
{
    wxCHECK_MSG(menu, false, wxT("DoPopupMenu: NULL menu"));

    // Build the JSON up front; the suspended frame keeps it alive for the
    // menu's whole lifetime.
    const wxString json = menu->WasmItemsToJson();

    // x/y are client coords of this window, OR wxDefaultCoord meaning "at the
    // mouse position" — passed through as -1 so JS uses the last pointer.
    const int vx = (x == wxDefaultCoord) ? -1 : x;
    const int vy = (y == wxDefaultCoord) ? -1 : y;

    // The invoking dispatch chain suspends for the menu's whole lifetime;
    // event dispatch must keep running meanwhile (the menu itself and the
    // rest of the UI), so zero the dispatch interlock for that whole span
    // (manual save/restore: wxWasmDispatchRestore centralizes the
    // erased-guard reporting).
    const int savedDispatchDepth = wxWasmDispatchDepth;
    wxWasmDispatchDepth = 0;
    const int chosenId =
        wxDomPopupMenuModal(json.utf8_str(), WasmGetDomId(), vx, vy);
    wxWasmDispatchRestore(savedDispatchDepth, "PopupMenu");

    if ( chosenId < 0 )
        return false; // cancelled

    // Dispatch like wxMenuBar::OnDomEvent: toggle checkables, then SendEvent
    // from the menu that actually contains the item (routes wxEVT_MENU up to
    // the invoking window set by wxWindowBase::PopupMenu).
    wxMenu *containing = NULL;
    wxMenuItem *item = menu->FindItem(chosenId, &containing);
    if ( !item )
        return false;

    const bool checkable = item->IsCheckable();
    if ( checkable )
        item->Toggle();

    if ( containing )
        containing->SendEvent(chosenId, checkable ? item->IsChecked() : -1);

    return true;
}

void wxWindowWasm::DoPopupMenu(wxMenu *menu, int x, int y,
                               std::function<void (bool)> callback)
{
    callback(DoPopupMenu(menu, x, y));
}
#endif // wxUSE_MENUS

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

        // Sync the whole DOM subtree: showing/hiding a container (e.g. a
        // notebook page) changes IsShownOnScreen() for every descendant.
        UpdateDomVisibility();

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

    // Keep browser focus in sync (no-op if the element already has it).
    if (m_domId)
        wxDomFocus(m_domId);
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

void wxWindowWasm::WarpPointer(int x, int y)
{
    // The browser cannot move the OS pointer, so we cannot warp it physically.
    // But wx and KiCad treat WarpPointer as authoritative: after warping they
    // read the position back via wxGetMousePosition(). On desktop the real
    // pointer move keeps that read in sync; here we must update the cached mouse
    // position ourselves. Without this, KiCad's arrow-key cursor nudge (which
    // warps the pointer then re-reads it) never moves, so a grabbed item won't
    // follow the arrow keys and snaps to the stale cursor on grab (pcbnew #9).
    // (x, y) are client coords of this window; the cache holds screen coords.
    wxTheApp->SetMousePosition(ClientToScreen(wxPoint(x, y)));
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
    m_selfNeedsPaint |= needsPaint;
    m_childNeedsPaint = true;

    // Always walk up to the top-level window, even when this window's flags
    // are already set. An early-out here (skip the walk if m_childNeedsPaint
    // is already true) assumes "descendant flagged => ancestor chain flagged",
    // but the paint sweep breaks that invariant: DoPaint() clears a window's
    // flags on entry while a flagged child can be skipped (hidden/frozen/
    // zero-sized), stranding the child with flags set and its ancestor chain
    // clear. A window stuck like that had every later Refresh() swallowed by
    // the early-out, so wxApp::Paint() (gated on the top-level's
    // NeedsPaint()) never descended to it again. This permanently blanked
    // the GAL canvas when the repaint it dropped was a lost-WebGL-context
    // recovery (SwiftShader CI: the occ-export blank-board flake).
    if (GetParent())
    {
        bool parentNeedsPaint = needsPaint && HasTransparentBackground();
        GetParent()->Invalidate(parentNeedsPaint);
    }
}

void wxWindowWasm::EraseBackgroundWindow()
{
    //printf("EraseBackgroundWindow\n");
    wxWindowDC dc(static_cast<wxWindow *>(this));
    wxEraseEvent eraseEvent(GetId(), &dc);
    eraseEvent.SetEventObject(this);

    if (!HandleWindowEvent(eraseEvent))
    {
        // An unhandled erase must fill the canvas with the background
        // colour — otherwise the window paints over uninitialised
        // (black) pixels.
        if (!HasTransparentBackground())
        {
            wxColour bg = GetBackgroundColour();
            if (!bg.IsOk())
                bg = wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE);

            dc.SetBackground(wxBrush(bg));
            dc.Clear();
        }
    }
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

        bool selfWasPainted = m_selfNeedsPaint || parentWasPainted;

        // Clear both flags BEFORE dispatching any paint event, not after the
        // sweep: paint handlers can call Refresh()/Invalidate() reentrantly
        // (widget churn during a paint, a canvas re-created mid-frame), and a
        // post-sweep clear would silently swallow that request — with nothing
        // ever re-issuing it, the window stays stale forever. Cleared up
        // front, a mid-paint Invalidate re-arms the flags and the next pump
        // frame repaints.
        m_selfNeedsPaint = false;
        m_childNeedsPaint = false;

        if (selfWasPainted)
        {
            PaintSelf();
        }

        PaintChildren(selfWasPainted);

        m_updateRegion.Clear();
    }
}

bool wxWindowWasm::SetFont(const wxFont& font)
{
    m_font = font;

    if (m_domId && font.IsOk())
    {
        wxDomSetFont(m_domId, font.GetNativeFontInfoDesc());
        InvalidateBestSize();
    }

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
    // Keep logical layout units in CSS pixels on WASM.
    return 1.0;
}

double wxWindowWasm::GetDPIScaleFactor() const
{
    return wxContentScaleFactor();
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

        UpdateDomGeometry();
    }
}

void wxWindowWasm::DoEnable(bool enable)
{
    if (!enable && HasFocus())
    {
        KillFocus();
    }

    if (m_domId)
        wxDomSetEnabled(m_domId, enable);

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
