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
#include "wx/wasm/private/execution_owner.h"

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
// wx_window_resize_stage for why a synchronous repaint of such a window must be avoided.
// wxGLCanvas is looked up by NAME (wxClassInfo::FindClass) rather than referenced as a
// type, so this core translation unit does NOT create a link-time dependency on
// wxGLCanvas::ms_classInfo — the wxWidgets test apps link libwx_core but not the GL
// library. In an app that doesn't link a GL canvas, FindClass returns null → no match.
// Defined here (C++ linkage, NOT inside the extern "C" block below) and shared with
// wxApp::Paint() (app.cpp) to defer the raytracer on the synchronous mouse-button repaint
// path, the same reason wx_window_resize_stage avoids a synchronous Paint() of such a window.
bool wxWasmWindowHostsGLCanvas(wxWindow* win)
{
    return wxWindowTreeHasClass(win, wxClassInfo::FindClass(wxT("wxGLCanvas")));
}

namespace
{

enum class wxWasmTopLevelOperation
{
    Move,
    Close,
    Resize
};

struct wxWasmTopLevelJob
{
    wxWasmTopLevelOperation operation = wxWasmTopLevelOperation::Move;
    wx_wasm_execution::ScopeToken targetScope;
    int cssId = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

void wxWasmDiscardTopLevelJob(void *arg)
{
    delete static_cast<wxWasmTopLevelJob *>(arg);
}

void wxWasmRunTopLevelJob(void *arg)
{
    wxWasmTopLevelJob *job = static_cast<wxWasmTopLevelJob *>(arg);
    wxTopLevelWindow *win = wxFindTopLevelByCSSId(job->cssId);

    if (win && !win->IsMainFrame() && win->IsEnabled()
        && (!job->targetScope
            || wxWasmExecutionScopeForWindow(win) == job->targetScope))
    {
        switch (job->operation)
        {
            case wxWasmTopLevelOperation::Move:
                win->Move(job->x, job->y);
                break;

            case wxWasmTopLevelOperation::Close:
                win->Close(false);
                break;

            case wxWasmTopLevelOperation::Resize:
                win->SetSize(job->x, job->y, job->width, job->height);

                // The JS resize reassigned (and thus cleared) the 2D canvas.
                // Repaint ordinary frames now. A GL frame can start worker
                // threads while painting, so leave that repaint to the normal
                // frame pump where the browser can start those workers.
                win->Refresh();
                if (wxTheApp && !wxWasmWindowHostsGLCanvas(win))
                    wxTheApp->PaintCurrentExecutionScope();
                break;
        }
    }

    delete job;
}

bool wxWasmStageTopLevelJob(
        wxWasmTopLevelJob *job,
        wx_wasm_execution::BrowserIngressReceipt receipt)
{
    job->targetScope = receipt.lease
            ? receipt.lease.targetScope
            : wx_wasm_execution::ScopeToken{};
    const bool coalesceGeometry =
            job->operation == wxWasmTopLevelOperation::Move
            || job->operation == wxWasmTopLevelOperation::Resize;
    const wx_wasm_execution::CoalesceClass coalesce =
            coalesceGeometry
                ? wx_wasm_execution::CoalesceClass::LatestGeometry
                : wx_wasm_execution::CoalesceClass::None;
    // Move and resize share the latest-geometry coalescer class, but their
    // keys remain distinct so neither replaces the other for one window.
    const std::uintptr_t coalesceKey =
            static_cast<std::uintptr_t>(
                    static_cast<unsigned>(job->cssId)) * 4u
            + static_cast<std::uintptr_t>(job->operation);

    if (!wxWasmExecutionStageBrowserIngress(
            &wxWasmRunTopLevelJob, job,
            wx_wasm_execution::WorkClass::UserInput,
            job->targetScope, receipt, &wxWasmDiscardTopLevelJob,
            coalesce, coalesceKey))
    {
        delete job;
        return false;
    }

    return true;
}

} // namespace

extern "C"
{

// Move a non-main top-level window to wx screen coords (x, y). Reuses Move() so
// the frame's children (GL canvas, tool/status bars) reposition through the
// normal size-event -> Layout path. The browser callback only submits a typed
// user-input job; it never mutates wx state on the shared main stack.
int EMSCRIPTEN_KEEPALIVE wx_window_move_stage(
        int cssId, int x, int y, unsigned ingressReceiptToken)
{
    const wx_wasm_execution::BrowserIngressReceipt receipt =
            wxWasmExecutionTakeBrowserIngressReceipt(
                    ingressReceiptToken);
    wxWasmTopLevelJob *job = new wxWasmTopLevelJob();
    job->operation = wxWasmTopLevelOperation::Move;
    job->cssId = cssId;
    job->x = x;
    job->y = y;
    // CSS ids are monotonic stable ingress identities. Resolve the wx target
    // only inside the admitted job; while a lease is active, this candidate
    // scope prevents a different top-level from borrowing its capability.
    return wxWasmStageTopLevelJob(job, receipt) ? 1 : 0;
}

// Close a non-main top-level window via wxEVT_CLOSE (-> the frame's
// OnCloseWindow). Close() can show a modal, so the heap job outlives this
// browser callback when its admitted dispatch context parks.
int EMSCRIPTEN_KEEPALIVE wx_window_close_stage(
        int cssId, unsigned ingressReceiptToken)
{
    const wx_wasm_execution::BrowserIngressReceipt receipt =
            wxWasmExecutionTakeBrowserIngressReceipt(
                    ingressReceiptToken);
    wxWasmTopLevelJob *job = new wxWasmTopLevelJob();
    job->operation = wxWasmTopLevelOperation::Close;
    job->cssId = cssId;
    return wxWasmStageTopLevelJob(job, receipt) ? 1 : 0;
}

// Resize a non-main top-level window to wx screen rect (x, y, width, height).
// Reuses SetSize so children reflow via the normal wxSizeEvent -> Layout path
// (incl. a frame's wxGLCanvas -> setGLCanvasRect) and the DOM syncs via
// wxNonOwnedWindow::DoSetSize -> setWindowRect. The DOM resize handles drag the
// left/bottom edges + corners, so this takes a full rect (origin + size), unlike
// the move-only wx_window_move_stage. Admission preserves discrete barriers and
// replaces only adjacent resize records for this exact CSS id.
int EMSCRIPTEN_KEEPALIVE wx_window_resize_stage(
        int cssId, int x, int y, int width, int height,
        unsigned ingressReceiptToken)
{
    const wx_wasm_execution::BrowserIngressReceipt receipt =
            wxWasmExecutionTakeBrowserIngressReceipt(
                    ingressReceiptToken);
    wxWasmTopLevelJob *job = new wxWasmTopLevelJob();
    job->operation = wxWasmTopLevelOperation::Resize;
    job->cssId = cssId;
    job->x = x;
    job->y = y;
    job->width = width;
    job->height = height;
    return wxWasmStageTopLevelJob(job, receipt) ? 1 : 0;
}

} // extern "C"
