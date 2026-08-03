/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/toplevel.cpp
// Purpose:     wxTopLevelWindowWasm implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/app.h"
#include "wx/dcclient.h"
#include "wx/frame.h"
#include "wx/settings.h"
#include "wx/toplevel.h"

#include "wx/wasm/private.h"
#include "wx/wasm/private/display.h"

#include <emscripten.h>
#include <emscripten/html5.h>

static const wxCoord TITLE_BAR_HEIGHT = 22;
static const wxColour TITLE_BAR_BACKGROUND_COLOUR(200, 200, 200);
static const wxColour TITLE_BAR_FOREGROUND_COLOUR(40, 40, 40);

static const wxCoord MINIMIZE_BUTTON_SIZE = 16;
static const wxCoord MINIMIZE_BUTTON_PADDING = 3;

// ----------------------------------------------------------------------------
// wxTopLevelWindowWasm
// ----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(wxTopLevelWindowWasm, wxTopLevelWindowBase)
    EVT_NC_PAINT(wxTopLevelWindowWasm::OnNcPaint)
    EVT_LEFT_DOWN(wxTopLevelWindowWasm::OnMouseDown)
    EVT_LEFT_UP(wxTopLevelWindowWasm::OnMouseUp)
    EVT_MOTION(wxTopLevelWindowWasm::OnMotion)
wxEND_EVENT_TABLE()

bool wxTopLevelWindowWasm::Create(wxWindow *parent,
                                  wxWindowID id,
                                  const wxString& title,
                                  const wxPoint& pos,
                                  const wxSize& sizeOrig,
                                  long style,
                                  const wxString& name)
{
    // Handle default size like GTK/MSW ports do - resolve to display size
    // before passing to base class. This ensures GetClientSize() returns
    // reasonable values even before Show() is called.
    wxSize size(sizeOrig);
    if (!size.IsFullySpecified())
    {
        // Query display size directly from wxTheApp if available.
        // This is safer than calling GetDefaultSize() which goes through
        // wxDisplay and can crash if the display system isn't initialized yet.
        wxSize defaultSize(1280, 720);  // Reasonable fallback
        if (wxTheApp && wxTheApp->GetDisplay())
        {
            defaultSize = wxTheApp->GetDisplay()->GetScreenSize();
        }
        size.SetDefaults(defaultSize);
    }

    if (!wxTopLevelWindowBase::Create(parent, id, pos, size, style, name))
    {
        wxFAIL_MSG(wxT("wxTopLevelWindowWasm creation failed"));
        return false;
    }

    SetTitle(title);

    // Non-main wxFrames get a real DOM title bar (drag handle + close "X")
    // instead of the canvas-painted one: a pointer-events:none canvas title bar
    // loses hit-testing to overlapping pointer-events:auto DOM controls from
    // another frame (e.g. the main editor's toolbar over the 3D viewer), so its
    // clicks never reach the #canvas mouse router. The DOM bar wins via normal
    // stacking. Created after SetTitle so the bar carries the current title.
    if (UseDomTitleBar())
    {
        EM_ASM({
            createWindowTitlebar($0, UTF8ToString($1), $2);
        }, GetCSSId(), static_cast<const char *>(title.utf8_str()), TITLE_BAR_HEIGHT);
    }

    // Resizable windows (wxRESIZE_BORDER) also get DOM edge-resize handles. Added
    // after the title bar so the side handles can start just below it (barHeight).
    if (UseDomResize())
    {
        EM_ASM({
            createWindowResizeHandles($0, $1);
        }, GetCSSId(), TITLE_BAR_HEIGHT);
    }

    return true;
}

void wxTopLevelWindowWasm::Init()
{
    m_isActive = false;
    m_minimizeButtonRect = wxRect(0, 0, MINIMIZE_BUTTON_SIZE, MINIMIZE_BUTTON_SIZE);
    m_isDragging = false;
}

wxTopLevelWindowWasm::~wxTopLevelWindowWasm()
{
    // Notify the host page when the application's main window is destroyed
    // (File->Quit or last close). A vetoed close (e.g. a cancelled
    // unsaved-changes prompt) never reaches destruction, so this only fires
    // for a real quit. Must run before ~wxTopLevelWindowBase, which clears
    // wxTheApp's top-window pointer. Child frames and dialogs are never the
    // app top window and don't notify.
    // IsMainFrame() (== wxTopLevelWindows[0], the first TLW ever created) rather
    // than GetTopWindow(): wx re-points the top window at whatever TLW is left,
    // so a transient frame dying mid-session used to look exactly like an app
    // quit — and the host acts on that by navigating the user out of the editor.
    // Observed for real 2026-08-03: a frame torn down during a heavy board load
    // silently ejected the user (docs/features/async/16 round 6).
    if (wxTheApp && IsMainFrame() && wxTheApp->GetTopWindow() == this)
    {
        EM_ASM({
            if (typeof window !== 'undefined'
                    && typeof window.wxAppTopWindowClosed === 'function')
            {
                window.wxAppTopWindowClosed();
            }
        });
    }
}

bool wxTopLevelWindowWasm::HasTitleBar() const
{
    // Main frame already has a native title bar.
    return !IsMainFrame() && !(GetWindowStyle() & wxFRAME_NO_TASKBAR);
}

bool wxTopLevelWindowWasm::UseDomTitleBar() const
{
    // Every non-main top-level window with a title bar (secondary frames AND
    // dialogs) uses the real DOM title bar — consistent chrome (cursor, hover,
    // close X) and robust hit-testing over other frames' DOM controls. Popups /
    // tooltips carry wxFRAME_NO_TASKBAR, so HasTitleBar() is already false for
    // them and they get no bar.
    return HasTitleBar();
}

bool wxTopLevelWindowWasm::UseDomResize() const
{
    // Edge-resize handles only for windows wx considers resizable. wxRESIZE_BORDER
    // is the established signal: wxDEFAULT_FRAME_STYLE carries it (all frames) and
    // KiCad's DIALOG_SHIM defaults to it (all dialogs that don't opt out), so this
    // makes virtually every dialog/frame resizable while a deliberately fixed
    // dialog stays fixed.
    return UseDomTitleBar() && (GetWindowStyle() & wxRESIZE_BORDER);
}

wxPoint wxTopLevelWindowWasm::GetClientAreaOrigin() const
{
    wxPoint origin = wxTopLevelWindowBase::GetClientAreaOrigin();

    if (HasTitleBar())
    {
        origin.y += TITLE_BAR_HEIGHT;
    }

    return origin;
}

void wxTopLevelWindowWasm::DoGetClientSize(int *width, int *height) const
{
    wxTopLevelWindowBase::DoGetClientSize(width, height);

    if (height && HasTitleBar())
    {
        *height = wxMax(*height - TITLE_BAR_HEIGHT, 0);
    }
}

void wxTopLevelWindowWasm::DoSetClientSize(int width, int height)
{
    if (HasTitleBar())
    {
        height += TITLE_BAR_HEIGHT;
    }

    wxTopLevelWindowBase::DoSetClientSize(width, height);
}

void wxTopLevelWindowWasm::DoScreenToClient(int *x, int *y) const
{
    wxWindow::DoScreenToClient(x, y);
}

void wxTopLevelWindowWasm::DoClientToScreen(int *x, int *y) const
{
    wxWindow::DoClientToScreen(x, y);
}

void wxTopLevelWindowWasm::SetIcons(const wxIconBundle& icons)
{
    wxTopLevelWindowBase::SetIcons(icons);

    wxSize size = wxContentScaleFactor() >= 1.5 ? wxSize(32, 32) : wxSize(16, 16);

    wxIcon icon = icons.GetIcon(size, wxIconBundle::FALLBACK_NEAREST_LARGER);

    if (icon.IsOk())
    {
        icon.SyncToJs();

        EM_ASM({
            setIcon($0);
        }, icon.GetJavascriptId());
    }
}

void wxTopLevelWindowWasm::ShowWithoutActivating()
{
    Show(true);
}

bool wxTopLevelWindowWasm::ShowFullScreen(bool show, long WXUNUSED(style))
{
    if (show != IsFullScreen())
    {
        EM_ASM({
            showFullscreen($0);
        }, show);
    }

    return true;
}

bool wxTopLevelWindowWasm::IsFullScreen() const
{
    EmscriptenFullscreenChangeEvent fullscreenStatus;
    emscripten_get_fullscreen_status(&fullscreenStatus);
    return fullscreenStatus.isFullscreen;
}

void wxTopLevelWindowWasm::SetTitle(const wxString &title)
{
    m_title = title;

    if (IsMainFrame())
    {
        EM_ASM({
            document.title = UTF8ToString($0);
        }, static_cast<const char *>(title.utf8_str()));
    }
    else if (UseDomTitleBar())
    {
        // Push to the DOM title bar's text. No-op if the bar isn't built yet
        // (the Create-time SetTitle precedes createWindowTitlebar, which then
        // builds the bar with the current title).
        EM_ASM({
            setWindowTitle($0, UTF8ToString($1));
        }, GetCSSId(), static_cast<const char *>(title.utf8_str()));
    }
}

void wxTopLevelWindowWasm::DrawTitleText(wxDC& dc, const wxRect& rect)
{
    wxCoord textWidth;
    wxCoord textHeight;
    dc.GetTextExtent(GetTitle(), &textWidth, &textHeight);

    int textX = wxMax((rect.width - textWidth) / 2, 0);
    int textY = wxMax((rect.height - textHeight) / 2, 0);

    wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold();

    dc.SetTextBackground(TITLE_BAR_BACKGROUND_COLOUR);
    dc.SetTextForeground(TITLE_BAR_FOREGROUND_COLOUR);
    dc.SetFont(font);

    dc.DrawText(GetTitle(), textX, textY);
}

void wxTopLevelWindowWasm::DrawMinimizeButton(wxDC& dc, const wxRect& rect)
{
    wxCoord buttonWidth = m_minimizeButtonRect.width - 2 * MINIMIZE_BUTTON_PADDING;
    wxCoord buttonHeight = m_minimizeButtonRect.height - 2 * MINIMIZE_BUTTON_PADDING;
    wxCoord buttonMargin = (rect.height - buttonHeight) / 2;

    wxCoord buttonX = wxMax(rect.x + rect.width - buttonWidth - buttonMargin, 0);
    wxCoord buttonY = rect.y + buttonMargin;

    m_minimizeButtonRect.x = buttonX - MINIMIZE_BUTTON_PADDING;
    m_minimizeButtonRect.y = buttonY - MINIMIZE_BUTTON_PADDING;

    dc.SetPen(wxPen(TITLE_BAR_FOREGROUND_COLOUR, 2));

    dc.DrawLine(buttonX, buttonY, buttonX + buttonWidth, buttonY + buttonHeight);
    dc.DrawLine(buttonX, buttonY + buttonHeight, buttonX + buttonWidth, buttonY);
}

void wxTopLevelWindowWasm::StartDrag(const wxPoint& pos)
{
    m_isDragging = true;
    m_dragOffset = pos;
    CaptureMouse();
}

void wxTopLevelWindowWasm::EndDrag()
{
    m_isDragging = false;
    ReleaseMouse();
}

void wxTopLevelWindowWasm::DragMove(const wxPoint& pos)
{
    Move(pos - m_dragOffset);
}

void wxTopLevelWindowWasm::OnNcPaint(wxNcPaintEvent& WXUNUSED(event))
{
    // Frames use a real DOM title bar (see UseDomTitleBar / createWindowTitlebar);
    // only dialogs still canvas-paint their title bar here.
    if (HasTitleBar() && !UseDomTitleBar())
    {
        wxWindowDC dc(this);
        wxRect ncRect(0, 0, GetSize().x, TITLE_BAR_HEIGHT);

        dc.SetBrush(TITLE_BAR_BACKGROUND_COLOUR);
        dc.SetPen(*wxTRANSPARENT_PEN);

        dc.DrawRectangle(ncRect);

        DrawTitleText(dc, ncRect);
        DrawMinimizeButton(dc, ncRect);
    }
}

void wxTopLevelWindowWasm::OnMouseDown(wxMouseEvent& event)
{
    // Only dialogs reach the canvas title bar here; frames are driven by the DOM
    // title bar (UseDomTitleBar), whose events never propagate to #canvas.
    if (HasTitleBar() && !UseDomTitleBar())
    {
        wxPoint pos = event.GetPosition() + GetClientAreaOrigin();

        if (pos.y < TITLE_BAR_HEIGHT && !m_minimizeButtonRect.Contains(pos))
        {
            StartDrag(pos);
        }
    }
}

void wxTopLevelWindowWasm::OnMouseUp(wxMouseEvent& event)
{
    if (m_isDragging)
    {
        EndDrag();
    }

    // Canvas close button is dialogs-only; frames use the DOM title bar's ×.
    if (HasTitleBar() && !UseDomTitleBar())
    {
        wxPoint pos = event.GetPosition() + GetClientAreaOrigin();

        if (m_minimizeButtonRect.Contains(pos))
        {
            Close();
        }
    }
}

void wxTopLevelWindowWasm::OnMotion(wxMouseEvent& event)
{
    if (m_isDragging)
    {
        if (event.Dragging())
        {
            DragMove(ClientToScreen(event.GetPosition()));
        }
        else
        {
            EndDrag();
        }
    }
}

// ----------------------------------------------------------------------------
// JS -> C++ hooks for the DOM title bar (see createWindowTitlebar in wx.js).
// The DOM title bar drives the SAME C++ paths as the retired canvas chrome:
// drag -> Move() (one reposition source of truth), X -> Close() (-> EVT_CLOSE).
// ----------------------------------------------------------------------------

static wxTopLevelWindow* wxFindTopLevelByCSSId(int cssId)
{
    for (wxWindowList::iterator it = wxTopLevelWindows.begin();
         it != wxTopLevelWindows.end(); ++it)
    {
        wxTopLevelWindow* tlw = wxDynamicCast(*it, wxTopLevelWindow);
        if (tlw && tlw->GetCSSId() == cssId)
            return tlw;
    }
    return NULL;
}

// True if `win` is, or contains anywhere in its child tree, a window of class `cls`.
static bool wxWindowTreeHasClass(wxWindow* win, const wxClassInfo* cls)
{
    if (!win || !cls)
        return false;
    if (win->IsKindOf(cls))
        return true;
    for (wxWindowList::compatibility_iterator node = win->GetChildren().GetFirst();
         node; node = node->GetNext())
    {
        if (wxWindowTreeHasClass(node->GetData(), cls))
            return true;
    }
    return false;
}

// True if `win` is, or contains, a wxGLCanvas. The 3D viewer's EDA_3D_CANVAS is a
// wxGLCanvas whose paint runs the (slow, multi-threaded) CPU raytracer — see
// wx_window_resize for why a synchronous repaint of such a window must be avoided.
// wxGLCanvas is looked up by NAME (wxClassInfo::FindClass) rather than referenced as a
// type, so this core translation unit does NOT create a link-time dependency on
// wxGLCanvas::ms_classInfo — the wxWidgets test apps link libwx_core but not the GL
// library. In an app that doesn't link a GL canvas, FindClass returns null → no match.
// Defined here (C++ linkage, NOT inside the extern "C" block below) and shared with
// wxApp::Paint() (app.cpp) to defer the raytracer on the synchronous mouse-button repaint
// path, the same reason wx_window_resize avoids a synchronous Paint() of such a window.
bool wxWasmWindowHostsGLCanvas(wxWindow* win)
{
    return wxWindowTreeHasClass(win, wxClassInfo::FindClass(wxT("wxGLCanvas")));
}

extern "C"
{

// Move a non-main top-level window to wx screen coords (x, y). Reuses Move() so
// the frame's children (GL canvas, tool/status bars) reposition through the
// normal size-event -> Layout path. Safe as a synchronous ccall (Move does not
// suspend the stack).
void EMSCRIPTEN_KEEPALIVE wx_window_move(int cssId, int x, int y)
{
    wxTopLevelWindow* win = wxFindTopLevelByCSSId(cssId);
    if (win && !win->IsMainFrame())
        win->Move(x, y);
}

// Close a non-main top-level window via wxEVT_CLOSE (-> the frame's
// OnCloseWindow). MUST be invoked as an ASYNC ccall: Close() runs the handler
// synchronously and may pump the event loop / show a modal, which aborts
// Asyncify if dispatched from a synchronous DOM-event ccall.
void EMSCRIPTEN_KEEPALIVE wx_window_close(int cssId)
{
    wxTopLevelWindow* win = wxFindTopLevelByCSSId(cssId);
    if (win && !win->IsMainFrame())
        win->Close(false);
}

// Resize a non-main top-level window to wx screen rect (x, y, width, height).
// Reuses SetSize so children reflow via the normal wxSizeEvent -> Layout path
// (incl. a frame's wxGLCanvas -> setGLCanvasRect) and the DOM syncs via
// wxNonOwnedWindow::DoSetSize -> setWindowRect. The DOM resize handles drag the
// left/bottom edges + corners, so this takes a full rect (origin + size), unlike
// the move-only wx_window_move. Safe as a synchronous ccall (SetSize does not
// suspend the stack).
void EMSCRIPTEN_KEEPALIVE wx_window_resize(int cssId, int x, int y, int width, int height)
{
    wxTopLevelWindow* win = wxFindTopLevelByCSSId(cssId);
    if (win && !win->IsMainFrame())
    {
        win->SetSize(x, y, width, height);

        // The JS resize reassigned (and thus CLEARED) the window's 2D canvas, so the
        // whole window must repaint — not just the strip SetSize invalidated. And
        // inside a modal dialog's Asyncify event pump the repaint is otherwise
        // deferred until the next input event (the dialog shows its black background
        // until the user clicks). Force a full, synchronous repaint now — the same
        // remedy wxApp uses after a button event (HandleMouseButtonEvent -> Paint).
        // wxApp::Paint() only repaints windows whose NeedsPaint() is set, so this
        // refreshes just the resized window.
        //
        // EXCEPTION — a window hosting a wxGLCanvas (the 3D viewer, whose paint runs the
        // multi-threaded CPU raytracer). Painting it synchronously here runs the raytrace
        // NESTED inside this resize ccall (itself driven from a JS requestAnimationFrame
        // callback in wx.js). If that raytrace then has to spawn an on-demand pthread
        // Worker — the pre-warmed pool drained by earlier renders, e.g. camera moves —
        // booting the Worker needs the main thread back in the event loop, which it can't
        // reach while blocked in this synchronous Paint(): the join busy-waits for a Worker
        // that can never start → deadlock/freeze. The frame and its GL canvas have already
        // been resized (SetSize -> setGLCanvasRect); only the RE-RENDER is at stake, so let
        // Refresh() above repaint it through the normal per-frame event-loop pump instead —
        // exactly the path a camera move takes, where the Worker CAN boot.
        win->Refresh();
        if (wxTheApp && !wxWasmWindowHostsGLCanvas(win))
            wxTheApp->Paint();
    }
}

} // extern "C"
