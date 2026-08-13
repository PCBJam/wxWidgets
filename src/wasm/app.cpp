/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/app.cpp
// Purpose      wxApp implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/app.h"

#include "wx/apptrait.h"
#include "wx/dnd.h"
#include "wx/log.h"
#include "wx/nonownedwnd.h"
#include "wx/toplevel.h"
#include "wx/utils.h"
#include "wx/window.h"

#include "wx/private/eventloopsourcesmanager.h"
#include "wx/wasm/private/dispatch.h"
#include "wx/wasm/private/mailbox.h"
#include "wx/wasm/private/mainloop.h"
#include "wx/wasm/private/display.h"

// Defined in evtloop.cpp: run work on a dispatch context instead of the stack
// it arrived on (doc 22 §10 — every entry that can reach a tool coroutine must
// go through the scheduler, or the rewind paths do not match).
extern "C" void wxWasmRunOnDispatchContext(void (*fn)(void *), void *arg);
#include "wx/wasm/private/keyboard.h"
#include "wx/wasm/private/mouse.h"
#include "wx/wasm/private/timer.h"

#include <emscripten.h>
#include <emscripten/html5.h>

void RegisterEmscriptenCallbacks(wxApp* app);

// WASM-specific logger that outputs to browser console
extern wxLog* wxCreateLogWasm();

// ----------------------------------------------------------------------------
// wxApp
// ----------------------------------------------------------------------------

IMPLEMENT_DYNAMIC_CLASS(wxApp, wxAppBase)

wxApp::wxApp()
    : m_display(new wxWasmDisplay())
{
    printf("Creating app\n");
    RegisterEmscriptenCallbacks(this);
}

wxApp::~wxApp()
{
    delete m_display;
}

int wxApp::OnRun()
{
#ifdef PCBJAM_JSPI
    // JSPI: main() is a promising export, so the loop runs in place and its
    // per-frame yield suspends main's own activation — the engine manages the
    // stack. The D5 detach existed to keep the ONE Asyncify slot free of
    // per-frame parks; there is no slot anymore.
    return wxAppBase::OnRun();
#else
    // D5 (pcbjam docs/features/async/22): the main loop moves onto a scheduler
    // context whose per-frame wait is a context park. The main stack is then
    // the scheduler's and nothing else — the per-frame Asyncify park this
    // replaces interleaved with scheduler transitions over one Asyncify slot
    // (the measured "overlapped-wake" class). This frame returns immediately;
    // wxEntry sees the detach and skips its teardown (the loop context runs it
    // when the app really exits).
    if (wxWasmDetachMainLoop(this))
        return 0;

    // Context creation failed: run the loop in place, exactly as before D5.
    return wxAppBase::OnRun();
#endif
}

// Defined in toplevel.cpp. True if `win` is, or contains, a wxGLCanvas — the 3D viewer,
// whose paint runs the multi-threaded CPU raytracer. Shared so Paint() can defer it on
// the synchronous mouse-button repaint path, mirroring wx_window_resize.
extern bool wxWasmWindowHostsGLCanvas(wxWindow* win);

void wxApp::Paint(bool deferGLCanvasWindows)
{
    wxWindow *topWindow = GetTopWindow();
    wxASSERT(topWindow != NULL);

    wxWindowList::iterator windowIter;

    for (windowIter = wxTopLevelWindows.begin();
         windowIter != wxTopLevelWindows.end();
         ++windowIter)
    {
        wxNonOwnedWindow* window = static_cast<wxNonOwnedWindow*>(*windowIter);

        // A non-main window hosting a wxGLCanvas is the 3D viewer, whose paint runs the
        // multi-threaded CPU raytracer: it spawns pthread Workers and busy-waits the main
        // thread for them. When this Paint() is a SYNCHRONOUS repaint driven from a DOM
        // event callback (a mouse button, see HandleMouseEvent) the main thread can't
        // return to the JS event loop to boot an on-demand Worker → the raytrace join
        // deadlocks (or aborts the nested Asyncify unwind). Defer it to the per-frame
        // ProcessEvents pump (evtloop.cpp), which yields between frames so Workers boot —
        // the same remedy wx_window_resize uses. The window keeps NeedsPaint() set, so the
        // next pump frame repaints it. The MAIN frame's wxGLCanvas is the GAL view (glemu/
        // WebGL, no CPU raytrace) so it is never deferred — its click feedback stays sync.
        if (deferGLCanvasWindows && !window->IsMainFrame()
                && wxWasmWindowHostsGLCanvas(window))
            continue;

        window->OnAnimationFrame();

        if (window->NeedsPaint())
        {
            window->HandlePaintRequests();
        }
    }
}

bool wxApp::IsKeyPressed(long keyCode)
{
    switch (keyCode)
    {
        case WXK_NONE:
            return false;
            break;
        case WXK_CONTROL:
            return m_mouseState.RawControlDown();
            break;
        case WXK_SHIFT:
            return m_mouseState.ShiftDown();
            break;
        case WXK_ALT:
            return m_mouseState.AltDown();
            break;
        default:
            return m_keyCodeSet.find(keyCode) != m_keyCodeSet.end();
            break;
    }
}

void wxApp::SetKeyPressed(long keyCode, bool pressed)
{
    if (pressed)
    {
        m_keyCodeSet.insert(keyCode);
    }
    else
    {
        m_keyCodeSet.erase(keyCode);
    }
}

void wxApp::GetMousePosition(int *x, int *y)
{
    m_mouseState.GetPosition(x, y);
}

void wxApp::GetMouseState(wxMouseState *mouseState)
{
    *mouseState = m_mouseState;
}

void wxApp::SetMousePosition(const wxPoint& screenPos)
{
    m_mouseState.SetPosition(screenPos);
}

wxWindow *wxApp::GetMouseWindow(const wxPoint& position) const
{
    wxWindow *captureWindow = wxWindow::GetCapture();
    if (captureWindow != NULL)
    {
        return captureWindow;
    }
    else
    {
        return wxFindWindowAtPoint(position);
    }
}

void wxApp::UpdateMouseState(const wxKeyEvent& event)
{
    m_mouseState.SetControlDown(event.ControlDown());
    m_mouseState.SetShiftDown(event.ShiftDown());
    m_mouseState.SetAltDown(event.AltDown());
    m_mouseState.SetMetaDown(event.MetaDown());
    m_mouseState.SetRawControlDown(event.RawControlDown());
}

bool wxApp::HandleKeyEvent(wxKeyEvent *event)
{
    //printf("HandleKeyEvent: %d\n", event->GetEventType());

    wxWindow *window = wxWindow::FindFocus();
    //printf("KeyEvent: window %p\n", window);

    if (window != NULL && window->IsEnabled())
    {
        event->SetEventObject(window);
        event->SetId(window->GetId());

        if (event->GetEventType() == wxEVT_CHAR)
        {
            //printf("key char: %d\n", event->GetKeyCode());
        }
        else if (event->GetEventType() == wxEVT_KEY_DOWN)
        {
            //printf("key down: %d\n", event->GetKeyCode());
        }

        UpdateMouseState(*event);

        if (event->GetEventType() == wxEVT_CHAR_HOOK)
        {
            SetKeyPressed(event->GetKeyCode(), true);
        }
        else if (event->GetEventType() == wxEVT_KEY_UP)
        {
            SetKeyPressed(event->GetKeyCode(), false);
        }

        if (wxWasmDispatchParked())
        {
            // Another dispatch chain is Asyncify-parked mid-handler; running
            // key handlers now would interleave with its half-mutated widget
            // state. Queue the event for the first pump tick after resume.
            // CHAR_HOOK reports "not handled" so the caller still synthesizes
            // the follow-up KEY_DOWN (queued too, order preserved); other
            // types report "handled" so the browser default stays suppressed
            // and no duplicate CHAR is synthesized.
            wxPostEvent(window->GetEventHandler(), *event);
            return event->GetEventType() != wxEVT_CHAR_HOOK;
        }

        wxWasmDispatchGuard guard;
        return window->HandleWindowEvent(*event);
    }
    else
    {
        return false;
    }
}

void wxApp::SendMouseEventToWindow(wxMouseEvent *event, wxWindow *window)
{
    if (window->IsEnabled())
    {
        wxASSERT(window != NULL);
        wxASSERT(event != NULL);

        wxPoint mousePosition = event->GetPosition();
        wxPoint clientPosition = window->ScreenToClient(mousePosition);
        //wxPoint screenPosition = window->GetScreenPosition();
        //printf("mouse: %d %d\n", mousePosition.x, mousePosition.y);
        //printf("screen: %d %d %p\n", screenPosition.x, screenPosition.y, window);
        //printf("client: %d %d %p\n", clientPosition.x, clientPosition.y, window);
        event->SetPosition(clientPosition);

        event->SetEventObject(window);
        event->SetId(window->GetId());

        window->HandleWindowEvent(*event);
    }
}

void wxApp::UpdateMouseState(const wxMouseEvent& event)
{
    m_mouseState.SetControlDown(event.ControlDown());
    m_mouseState.SetShiftDown(event.ShiftDown());
    m_mouseState.SetAltDown(event.AltDown());
    m_mouseState.SetMetaDown(event.MetaDown());
    m_mouseState.SetRawControlDown(event.RawControlDown());

    m_mouseState.SetLeftDown(event.LeftIsDown());
    m_mouseState.SetMiddleDown(event.MiddleIsDown());
    m_mouseState.SetRightDown(event.RightIsDown());
    m_mouseState.SetAux1Down(event.Aux1IsDown());
    m_mouseState.SetAux2Down(event.Aux2IsDown());
    m_mouseState.SetPosition(event.GetPosition());
}

void wxApp::HandleMouseEvent(wxMouseEvent *event)
{
    if (wxWasmDispatchParked())
    {
        // Another dispatch chain is Asyncify-parked mid-handler; running
        // mouse handlers now would interleave with its half-mutated widget
        // state. Keep wxGetMouseState() truthful, queue button events for
        // the first pump tick after resume (targeted like
        // SendMouseEventToWindow would), and drop motion/hover synthesis -
        // the next real motion after resume re-syncs it.
        UpdateMouseState(*event);

        if (event->ButtonDown() || event->ButtonUp() || event->ButtonDClick())
        {
            wxWindow *target = GetMouseWindow(event->GetPosition());

            if (target != NULL && target->IsEnabled())
            {
                wxMouseEvent queued(*event);
                queued.SetPosition(target->ScreenToClient(event->GetPosition()));
                queued.SetEventObject(target);
                queued.SetId(target->GetId());
                wxPostEvent(target->GetEventHandler(), queued);
            }
        }

        return;
    }

    wxWasmDispatchGuard dispatchGuard;

    if (wxDropSource::IsDragInProgress())
    {
        wxDropSource::HandleMouseEvent(event);
    }
    else
    {
        wxPoint mousePosition = event->GetPosition();

        UpdateMouseState(*event);

        if (g_mouseWindow != GetMouseWindow(mousePosition))
        {
            if (g_mouseWindow != NULL)
            {
                wxMouseEvent leaveEvent(*event);
                leaveEvent.SetEventType(wxEVT_LEAVE_WINDOW);
                SendMouseEventToWindow(&leaveEvent, g_mouseWindow);
            }

            // Don't optimize away GetMouseWindow, it may have changed during
            // wxEVT_LEAVE_WINDOW processing.
            g_mouseWindow = GetMouseWindow(mousePosition);

            if (g_mouseWindow != NULL)
            {
                wxCursor cursor = g_mouseWindow->GetCursor();
                if (cursor.IsOk())
                {
                    wxSetCursor(cursor);
                }
                else
                {
                    wxSetCursor(*wxSTANDARD_CURSOR);
                }
                wxMouseEvent enterEvent(*event);
                enterEvent.SetEventType(wxEVT_ENTER_WINDOW);
                SendMouseEventToWindow(&enterEvent, g_mouseWindow);
            }
        }

        if (g_mouseWindow != NULL)
        {
            wxEventType eventType = event->GetEventType();
            // Enter window and leave window events are handled above.
            if (eventType != wxEVT_ENTER_WINDOW && eventType != wxEVT_LEAVE_WINDOW)
            {
                SendMouseEventToWindow(event, g_mouseWindow);
            }

            if (g_mouseWindow != NULL &&
                g_mouseWindow == GetMouseWindow(mousePosition) &&
                (eventType == wxEVT_LEFT_DOWN ||
                 eventType == wxEVT_RIGHT_DOWN ||
                 eventType == wxEVT_MIDDLE_DOWN))
            {
                // A click on a toolbar must NOT steal keyboard focus from the canvas. KiCad
                // binds its hotkey / Esc-to-cancel-tool handling (TOOL_DISPATCHER, via
                // CHAR_HOOK) on the GAL canvas panel, and CHAR_HOOK only reaches it while the
                // canvas holds focus. Grabbing focus to the toolbar on every tool click broke
                // Esc and other canvas hotkeys until the user re-clicked the canvas. (Can't
                // include the aui header from wx core, so detect toolbars by class name.)
                bool toolbarClick = false;

                for (wxWindow* w = g_mouseWindow; w != NULL; w = w->GetParent())
                {
                    if (w->GetClassInfo()->GetClassName() &&
                        wxString(w->GetClassInfo()->GetClassName()).Lower().Contains("toolbar"))
                    {
                        toolbarClick = true;
                        break;
                    }
                }

                if (g_mouseWindow->IsEnabled() && !toolbarClick)
                {
                    g_mouseWindow->SetFocus();
                }
            }
        }

        // Refresh the cursor on mouse motion by consulting wxEVT_SET_CURSOR
        // handlers. The window-change path above only updates the cursor when the
        // hovered WINDOW changes, and only from that window's static GetCursor();
        // regions painted inside a single window set their cursor through the
        // wxEVT_SET_CURSOR event instead (e.g. wxAuiManager paints a resize cursor
        // over a dock sash, which lives in the managed frame, not a child window).
        // Walk up like the native ports' HandleSetCursor and apply the cursor only
        // when a handler actually supplies one, so windows that manage their own
        // cursor (the GAL canvas, plain controls, busy cursor) are left untouched.
        if (g_mouseWindow != NULL &&
            event->GetEventType() == wxEVT_MOTION &&
            !wxIsBusy())
        {
            for (wxWindow *win = g_mouseWindow; win != NULL; win = win->GetParent())
            {
                const wxPoint clientPt = win->ScreenToClient(mousePosition);
                wxSetCursorEvent setCursorEvent(clientPt.x, clientPt.y);
                setCursorEvent.SetEventObject(win);

                if (win->GetEventHandler()->ProcessEvent(setCursorEvent))
                {
                    if (setCursorEvent.HasCursor())
                    {
                        wxSetCursor(setCursorEvent.GetCursor());
                    }
                    break;
                }

                if (win->IsTopLevel())
                {
                    break;
                }
            }
        }

#if wxUSE_TOOLTIPS
        // Re-evaluate the tooltip AFTER the motion has been dispatched to the
        // hovered window, so per-item widgets that update their own tooltip on
        // wxEVT_MOTION (e.g. wxAuiToolBar::OnMotion) have set the current text.
        // Driven on every move (not just window changes), since a toolbar's tool
        // buttons are islands within one wxWindow; the tooltip layer ignores
        // no-op changes so the show delay isn't restarted on every pixel.
        extern void wxWasmTooltipOnHoverChange(wxWindow *win);
        wxWasmTooltipOnHoverChange(g_mouseWindow);
#endif
    }

    // A button click can change UI state that (a) posts follow-up events via
    // wxPostEvent and (b) calls Refresh()/Invalidate(). Two examples in the symbol
    // chooser: expanding a wxDataViewCtrl row, and selecting a row — which posts
    // EVT_LIBITEM_SELECTED (preview + description update) / EVT_LIBITEM_CHOSEN
    // (double-click accept+close). Inside a modal's Asyncify-driven event pump
    // those queued events and their repaints are not flushed until the NEXT input
    // event, so the panel lags one selection behind and double-click doesn't
    // close. Process the queued events and repaint synchronously after a button
    // event so the interaction takes effect immediately. Motion/wheel are excluded
    // to avoid per-move churn; idle work stays with the pump.
    if (event->ButtonDown() || event->ButtonUp() || event->ButtonDClick())
    {
        // ProcessPendingEvents() runs synchronously: it flushes the click's queued
        // follow-ups (selection/preview updates, double-click accept+close), so interaction
        // LOGIC still takes effect immediately — the reason this block exists. Paint() then
        // repaints, but deferGLCanvasWindows SKIPS the synchronous repaint of any non-main
        // wxGLCanvas host: the 3D viewer, or a dialog with a 3D preview (e.g. the footprint
        // 3D-models tab). Painting those here would run the multi-threaded CPU raytracer
        // nested in this synchronous DOM mouse callback, where its on-demand pthread Worker
        // can't boot (main thread blocked) → deadlock. They keep NeedsPaint() and repaint
        // via the yielding per-frame pump one frame later (a GAL preview's pixels lag ≤1
        // frame; its logic already ran above). Non-GL windows still repaint synchronously.
        ProcessPendingEvents();
        Paint(/*deferGLCanvasWindows=*/true);
    }
}

// Mailbox replay for a wheel tick that arrived while a dispatch chain was
// parked (docs/features/async/17 S1). Owns the heap copy; re-enters through
// the public handler so the parked re-check and the interlock guard apply.
static void WheelReplay(void *p)
{
    wxMouseEvent *event = static_cast<wxMouseEvent *>(p);
    if (wxTheApp)
        wxTheApp->HandleMouseWheelEvent(event);
    delete event;
}

void wxApp::HandleMouseWheelEvent(wxMouseEvent *event)
{
    if (wxWasmDispatchParked())
    {
        // Queue the tick for delivery when the interlock frees instead of
        // dropping it — every tick the user made scrolls, just later (a long
        // park replays them as a burst, which is the deliver-not-drop
        // contract). The wheel resolves its target window from the CURRENT
        // pointer position at delivery, matching what a fresh tick after
        // resume would do.
        wxWasmMailboxEnqueueAfter(WheelReplay, new wxMouseEvent(*event), 0);
        return;
    }

    wxWasmDispatchGuard dispatchGuard;

    wxPoint mousePosition = wxGetMousePosition();
    event->SetPosition(mousePosition);
    wxWindow *window = GetMouseWindow(mousePosition);

    // Native ports route unhandled wheel events up the window hierarchy
    // (GTK via GDK propagation, MSW via the focus window): a wheel over a
    // label inside a scrolled pane must reach the scroll helper. wx mouse
    // events don't propagate by themselves, so walk up explicitly until
    // some window handles it. Each hop gets a fresh copy with positions
    // in that window's client coordinates.
    for ( ; window != NULL; window = window->GetParent() )
    {
        if ( window->IsEnabled() )
        {
            wxMouseEvent evt(*event);
            evt.SetPosition(window->ScreenToClient(mousePosition));
            evt.SetEventObject(window);
            evt.SetId(window->GetId());

            if ( window->HandleWindowEvent(evt) )
                return;
        }

        if ( window->IsTopLevel() )
            return;
    }
}

void wxApp::HandleSizeEvent(const wxSizeEvent &event)
{
    wxSize newSize = event.GetSize();
    //printf("HandleSizeEvent: %d %d\n", newSize.GetWidth(), newSize.GetHeight());

    GetDisplay()->SetScreenSize(newSize);
    GetDisplay()->UpdateScaleFactor();

    wxWindow *topWindow = GetTopWindow();
    if (topWindow != NULL)
    {
        //printf("SetSize %d %d\n", newSize.GetWidth(), newSize.GetHeight());
        topWindow->SetSize(0, 0, newSize.GetWidth(), newSize.GetHeight());
        topWindow->Refresh();
    }
}

void wxApp::HandleActivateEvent(wxActivateEvent *event)
{
    //printf("HandleActivateEvent\n");
    wxWindow *topWindow = GetTopWindow();

    if (topWindow != NULL)
    {
        event->SetId(topWindow->GetId());
        event->SetEventObject(topWindow);
        topWindow->HandleWindowEvent(*event);
    }
}

void wxApp::HandleCloseEvent(wxCloseEvent *event)
{
    //printf("close message\n");
    wxWindow *topWindow = GetTopWindow();

    if (topWindow != NULL)
    {
        event->SetId(topWindow->GetId());
        event->SetEventObject(topWindow);
        topWindow->HandleWindowEvent(*event);
    }
}

// ===========================================================================
// wxGUIAppTraits
// ===========================================================================

wxPortId wxGUIAppTraits::GetToolkitVersion(int *verMaj,
        int *verMin,
        int* verMicro) const
{
    *verMaj = __EMSCRIPTEN_major__;
    *verMin = __EMSCRIPTEN_minor__;
    *verMicro = __EMSCRIPTEN_tiny__;

    return wxPORT_WASM;
}

#if wxUSE_TIMER
wxTimerImpl *wxGUIAppTraits::CreateTimerImpl(wxTimer *timer)
{
    return new wxWasmTimerImpl(timer);
}
#endif

#if wxUSE_EVENTLOOP_SOURCE

class wxWasmEventLoopSourcesManager : public wxEventLoopSourcesManagerBase
{
public:
    wxEventLoopSource *
    AddSourceForFD(int WXUNUSED(fd),
                   wxEventLoopSourceHandler* WXUNUSED(handler),
                   int WXUNUSED(flags))
    {
        wxFAIL_MSG("Monitoring FDs in the main loop is not supported");

        return NULL;
    }
};

wxEventLoopSourcesManagerBase* wxGUIAppTraits::GetEventLoopSourcesManager()
{
    static wxWasmEventLoopSourcesManager s_eventLoopSourcesManager;

    return &s_eventLoopSourcesManager;
}

#endif // wxUSE_EVENTLOOP_SOURCE


wxEventLoopBase* wxGUIAppTraits::CreateEventLoop()
{
    return new wxEventLoop();
}

bool wxGUIAppTraits::ShowAssertDialog(const wxString& WXUNUSED(msg))
{
    return false;
}

#if wxUSE_LOG
wxLog* wxGUIAppTraits::CreateLogTarget()
{
    // Use WASM-specific logger that outputs to browser console
    return wxCreateLogWasm();
}
#endif

namespace
{

const char *GetEventName(int eventType)
{
    switch (eventType)
    {
        case EMSCRIPTEN_EVENT_KEYPRESS:
            return "keypress";
            break;
        case EMSCRIPTEN_EVENT_KEYDOWN:
            return "keydown";
            break;
        case EMSCRIPTEN_EVENT_KEYUP:
            return "keyup";
            break;
        case EMSCRIPTEN_EVENT_CLICK:
            return "click";
            break;
        case EMSCRIPTEN_EVENT_MOUSEDOWN:
            return "mousedown";
            break;
        case EMSCRIPTEN_EVENT_MOUSEUP:
            return "mouseup";
            break;
        case EMSCRIPTEN_EVENT_DBLCLICK:
            return "dblclick";
            break;
        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            return "mousemove";
            break;
        case EMSCRIPTEN_EVENT_WHEEL:
            return "wheel";
            break;
        case EMSCRIPTEN_EVENT_RESIZE:
            return "resize";
            break;
        case EMSCRIPTEN_EVENT_MOUSEENTER:
            return "mouseenter";
            break;
        case EMSCRIPTEN_EVENT_MOUSELEAVE:
            return "mouseleave";
            break;
        case EMSCRIPTEN_EVENT_TOUCHSTART:
            return "touchstart";
            break;
        case EMSCRIPTEN_EVENT_TOUCHEND:
            return "touchend";
            break;
        case EMSCRIPTEN_EVENT_TOUCHMOVE:
            return "touchmove";
            break;
        case EMSCRIPTEN_EVENT_TOUCHCANCEL:
            return "touchcancel";
            break;
        default:
            break;
    }
    return "(Unknown)";
}

// ----------------------------------------------------------------------------
// DOM entries run on a dispatch context, not on the stack they arrive on
// (pcbjam docs/features/async/22 §10).
//
// A DOM handler enters wasm on the MAIN stack. Dispatching there reaches a
// KiCad tool coroutine through libcontext's DIRECT symmetric swap, while the
// tick reaches that same coroutine through the dispatch context as a STAR
// TRANSFER — and a capture written by one path cannot be rewound by the other
// ("index out of bounds" in doRewind, measured on every canvas tool). So the
// handler bodies below are packaged as jobs and handed to the scheduler.
//
// LIFETIME. A job usually runs to completion inside wxWasmRunOnDispatchContext
// (the context parks again and the pump returns), but it MAY park — a click
// that opens a modal keeps the job alive for the dialog's lifetime. The job is
// therefore heap-owned, and ownership goes to whoever finishes last: the job
// deletes itself if the caller has already given up on it, otherwise the
// caller deletes it and reads its result.
// ----------------------------------------------------------------------------
namespace
{

struct wxWasmDomJob
{
    wxApp *app = NULL;
    bool finished = false;
    bool abandoned = false;
};

/** Run aJob via the scheduler; true if it completed before returning. */
bool wxWasmRunDomJob(void (*aFn)(void *), wxWasmDomJob *aJob)
{
    wxWasmRunOnDispatchContext(aFn, aJob);

    if (aJob->finished)
        return true;

    // Still parked (a modal, a lib fetch): it owns itself from here.
    aJob->abandoned = true;
    return false;
}

/** Job epilogue: hand the allocation to whoever is still around. */
void wxWasmFinishDomJob(wxWasmDomJob *aJob)
{
    if (aJob->abandoned)
        delete aJob;
    else
        aJob->finished = true;
}

struct wxWasmMouseJob : wxWasmDomJob
{
    wxMouseEvent event;
    bool wheel = false;
    // Touch-down synthesises a motion event before the button (see
    // TouchCallback); both must run on the same stack, in order.
    bool precedingMotion = false;
};

void wxWasmRunMouseJob(void *arg)
{
    wxWasmMouseJob *job = static_cast<wxWasmMouseJob *>(arg);

    if (job->wheel)
    {
        job->app->HandleMouseWheelEvent(&job->event);
    }
    else
    {
        if (job->precedingMotion)
        {
            wxMouseEvent moveEvent(job->event);
            moveEvent.SetEventType(wxEVT_MOTION);
            moveEvent.SetLeftDown(false);
            moveEvent.m_clickCount = 0;
            job->app->HandleMouseEvent(&moveEvent);
        }

        job->app->HandleMouseEvent(&job->event);
    }

    wxWasmFinishDomJob(job);
}

struct wxWasmKeyJob : wxWasmDomJob
{
    wxKeyEvent event;
    // The browser needs a synchronous answer; the job writes it here before it
    // can park, and a job that parks anyway leaves the caller's default.
    bool preventDefault = true;
};

void wxWasmRunKeyJob(void *arg)
{
    wxWasmKeyJob *job = static_cast<wxWasmKeyJob *>(arg);
    wxApp *app = job->app;
    wxKeyEvent &event = job->event;

    if (event.GetEventType() == wxEVT_KEY_DOWN)
    {
        wxKeyEvent charHookEvent(wxEVT_CHAR_HOOK, event);

        if (!app->HandleKeyEvent(&charHookEvent) ||
            charHookEvent.IsNextEventAllowed())
        {
            // The browser does not generate char events for some key codes
            if (KeyCodeNeedsCharEvent(event.GetKeyCode()))
            {
                if (!app->HandleKeyEvent(&event))
                {
                    wxKeyEvent charEvent(wxEVT_CHAR, event);
                    app->HandleKeyEvent(&charEvent);
                }
            }
            else
            {
                // By default, emscripten generates char events
                job->preventDefault = app->HandleKeyEvent(&event);
            }
        }
        else
        {
            job->preventDefault = false;
        }
    }
    else
    {
        app->HandleKeyEvent(&event);
    }

    wxWasmFinishDomJob(job);
}

}  // namespace

EM_BOOL KeyCallback(int eventType,
                    const EmscriptenKeyboardEvent *emscriptenEvent,
                    void *userData)
{
    //printf("KeyCallback: %d\n", eventType);

    // While a DOM editable (<input>/<textarea>) owns browser
    // focus, the keystroke belongs to it — no wx dispatch, no
    // preventDefault, or typing would be swallowed. Escape still goes to
    // wx so modal dialogs can close. The check is STATELESS
    // (document.activeElement), not the focusin/focusout-maintained flag:
    // Firefox does not fire focusout when a focused element is removed
    // (e.g. a wizard page destroyed mid-typing), which left the flag
    // stuck and swallowed every key for the rest of the session.
    if (EM_ASM_INT({
            if (typeof document === 'undefined') return 0;
            var ae = document.activeElement;
            return (ae && (ae.tagName === 'INPUT' ||
                           ae.tagName === 'TEXTAREA' ||
                           ae.tagName === 'SELECT' ||
                           ae.isContentEditable)) ? 1 : 0;
        }))
    {
        if (strcmp(emscriptenEvent->key, "Escape") != 0)
            return EM_FALSE;
    }

    wxApp* app = static_cast<wxApp*>(userData);
    wxKeyEvent event;
    bool preventDefault = true;

    if (EmscriptenKeyboardEventToWXEvent(eventType, *emscriptenEvent, &event))
    {
        /*
                wxString key_char(event.GetUnicodeKey());
                printf("type: %d, key_code: %d, char: %s\n",
                       event.GetEventType(),
                       event.GetKeyCode(),
                       static_cast<const char*>(key_char.utf8_str()));
        */

        wxWasmKeyJob* job = new wxWasmKeyJob();
        job->app = app;
        job->event = event;

        if (wxWasmRunDomJob(&wxWasmRunKeyJob, job))
        {
            preventDefault = job->preventDefault;
            delete job;
        }
        // else: the handler parked (a modal opened from a key). It owns
        // itself now, and preventDefault keeps its default — the browser
        // cannot be kept waiting for a dialog.
    }

    return preventDefault;
}

EM_BOOL MouseCallback(int eventType,
                      const EmscriptenMouseEvent *emscriptenEvent,
                      void *userData)
{
    //const char *eventName = GetEventName(eventType);
    //printf("MouseCallback: %s %d %ld %ld\n", eventName, emscriptenEvent->button, emscriptenEvent->targetX, emscriptenEvent->targetY);

    wxApp* app = static_cast<wxApp*>(userData);
    wxMouseEvent event;

    if (EmscriptenMouseEventToWXEvent(eventType, *emscriptenEvent, &event))
    {
        wxWasmMouseJob* job = new wxWasmMouseJob();
        job->app = app;
        job->event = event;

        if (wxWasmRunDomJob(&wxWasmRunMouseJob, job))
            delete job;
    }

    return true;
}

EM_BOOL TouchCallback(int eventType,
                      const EmscriptenTouchEvent *emscriptenEvent,
                      void *userData)
{
    //const char *eventName = GetEventName(eventType);
    //printf("TouchCallback: %s %d %ld %ld\n", eventName, emscriptenEvent->numTouches, emscriptenEvent->touches[0].targetX, emscriptenEvent->touches[0].targetY);

    wxApp* app = static_cast<wxApp*>(userData);
    wxMouseEvent event;

    if (EmscriptenTouchEventToWXEvent(eventType, *emscriptenEvent, &event))
    {
        wxWasmMouseJob* job = new wxWasmMouseJob();
        job->app = app;
        job->event = event;
        // Mirroring browser behavior, move the mouse to the new location
        // before sending the mouse down event (synthesised inside the job so
        // both events reach wx on the same stack, in order).
        job->precedingMotion = (event.GetEventType() == wxEVT_LEFT_DOWN);

        if (wxWasmRunDomJob(&wxWasmRunMouseJob, job))
            delete job;

        return true;
    } else {
        return false;
    }
}

EM_BOOL WheelCallback(int WXUNUSED(eventType),
                      const EmscriptenWheelEvent *emscriptenEvent,
                      void *userData)
{
    //printf("WheelCallback: %f %f %ld %ld\n", event->deltaX, event->deltaY, event->mouse.targetX, event->mouse.targetY);

    wxApp* app = static_cast<wxApp*>(userData);
    wxMouseEvent event;

    if (EmscriptenWheelEventToWXEvent(*emscriptenEvent, wxHORIZONTAL, &event))
    {
    }

    if (EmscriptenWheelEventToWXEvent(*emscriptenEvent, wxVERTICAL, &event))
    {
        wxWasmMouseJob* job = new wxWasmMouseJob();
        job->app = app;
        job->event = event;
        job->wheel = true;

        if (wxWasmRunDomJob(&wxWasmRunMouseJob, job))
            delete job;
    }

    return true;
}

EM_BOOL ResizeCallback(int WXUNUSED(eventType),
                       const EmscriptenUiEvent *emscriptenEvent,
                       void *userData)
{
    //printf("ResizeCallback: %d %d\n", event->windowInnerWidth, event->windowInnerHeight);
    wxApp* app = static_cast<wxApp*>(userData);
    int offset = EM_ASM_INT({
        return mainWindow.offsetTop;
    });
    wxSize size(emscriptenEvent->windowInnerWidth, emscriptenEvent->windowInnerHeight - offset);
    wxSizeEvent event(size);

    app->HandleSizeEvent(event);

    return true;
}

EM_BOOL FocusCallback(int eventType,
                      const EmscriptenFocusEvent *WXUNUSED(emscriptenEvent),
                      void *userData)
{
    //printf("FocusCallback\n");
    wxApp* app = static_cast<wxApp*>(userData);

    wxActivateEvent event(wxEVT_ACTIVATE, eventType == EMSCRIPTEN_EVENT_FOCUS);
    app->HandleActivateEvent(&event);

    return true;
}

const char *UnloadCallback(int WXUNUSED(eventType),
                           const void *WXUNUSED(emscriptenEvent),
                           void *userData)
{
    //printf("UnloadCallback\n");
    wxApp* app = static_cast<wxApp*>(userData);

    wxCloseEvent event(wxEVT_CLOSE_WINDOW);
    event.SetCanVeto(true);
    app->HandleCloseEvent(&event);

    return event.GetVeto() ? "veto" : "";
}

}

void RegisterEmscriptenCallbacks(wxApp* app)
{
    EMSCRIPTEN_RESULT result;

    result = emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, false, KeyCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, false, KeyCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, false, KeyCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_mousedown_callback("#canvas", app, false, MouseCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_mouseup_callback("#canvas", app, false, MouseCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    //result = emscripten_set_click_callback("#canvas", app, false, MouseCallback);
    //wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    //result = emscripten_set_dblclick_callback("#canvas", app, false, MouseCallback);
    //wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_mouseenter_callback("#canvas", app, false, MouseCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_mouseleave_callback("#canvas", app, false, MouseCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_mousemove_callback("#canvas", app, false, MouseCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_wheel_callback("#canvas", app, false, WheelCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_touchstart_callback("#canvas", app, false, TouchCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_touchend_callback("#canvas", app, false, TouchCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_touchmove_callback("#canvas", app, false, TouchCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_touchcancel_callback("#canvas", app, false, TouchCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, false, ResizeCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, false, FocusCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, app, false, FocusCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    result = emscripten_set_beforeunload_callback(app, UnloadCallback);
    wxASSERT(result == EMSCRIPTEN_RESULT_SUCCESS);

    // Initialize HTML5 drag and drop handlers
    EM_ASM({
        if (typeof registerDragDropHandlers === 'function') {
            registerDragDropHandlers();
        }
    });
}

// ============================================================================
// HTML5 Drag and Drop Support
// ============================================================================

extern "C" {

EMSCRIPTEN_KEEPALIVE
void OnDragEnter(int x, int y)
{
    // Optional: could send a custom event for visual feedback
    // printf("[DND] OnDragEnter: %d, %d\n", x, y);
}

EMSCRIPTEN_KEEPALIVE
void OnDragLeave()
{
    // Optional: could send a custom event to clear visual feedback
    // printf("[DND] OnDragLeave\n");
}

EMSCRIPTEN_KEEPALIVE
void OnFileDropped(const char* path, int x, int y)
{
    // printf("[DND] OnFileDropped: %s at (%d, %d)\n", path, x, y);

    // Find the window that should receive the drop event
    wxPoint dropPoint(x, y);
    wxWindow* target = wxFindWindowAtPoint(dropPoint);

    // Fall back to top window if no window found at point
    if (target == nullptr && wxTheApp != nullptr)
    {
        target = wxTheApp->GetTopWindow();
    }

    if (target == nullptr)
    {
        // printf("[DND] No target window found\n");
        return;
    }

    // Create file path array (wxDropFilesEvent takes ownership)
    wxString* files = new wxString[1];
    files[0] = wxString::FromUTF8(path);

    // Create and dispatch the drop files event
    wxDropFilesEvent event(wxEVT_DROP_FILES, 1, files);
    event.SetEventObject(target);

    // Set drop position relative to target window
    wxPoint clientPos = target->ScreenToClient(dropPoint);
    event.m_pos = clientPos;

    target->HandleWindowEvent(event);
}

} // extern "C"
