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
#include "wx/nonownedwnd.h"
#include "wx/toplevel.h"
#include "wx/window.h"

#include "wx/private/eventloopsourcesmanager.h"
#include "wx/wasm/private/display.h"
#include "wx/wasm/private/keyboard.h"
#include "wx/wasm/private/mouse.h"
#include "wx/wasm/private/timer.h"

#include <emscripten.h>
#include <emscripten/html5.h>

void RegisterEmscriptenCallbacks(wxApp* app);

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

void wxApp::Paint()
{
    wxWindow *topWindow = GetTopWindow();
    wxASSERT(topWindow != NULL);

    wxWindowList::iterator windowIter;

    for (windowIter = wxTopLevelWindows.begin();
         windowIter != wxTopLevelWindows.end();
         ++windowIter)
    {
        wxNonOwnedWindow* window = static_cast<wxNonOwnedWindow*>(*windowIter);
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
                if (g_mouseWindow->IsEnabled())
                {
                    g_mouseWindow->SetFocus();
                }
            }
        }
    }
}

void wxApp::HandleMouseWheelEvent(wxMouseEvent *event)
{
    wxPoint mousePosition = wxGetMousePosition();
    event->SetPosition(mousePosition);
    wxWindow *window = GetMouseWindow(mousePosition);

    if (window != NULL)
    {
        SendMouseEventToWindow(event, window);
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

EM_BOOL KeyCallback(int eventType,
                    const EmscriptenKeyboardEvent *emscriptenEvent,
                    void *userData)
{
    //printf("KeyCallback: %d\n", eventType);

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
                    preventDefault = app->HandleKeyEvent(&event);
                }
            }
            else
            {
                preventDefault = false;
            }
        }
        else
        {
            app->HandleKeyEvent(&event);
        }
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
        app->HandleMouseEvent(&event);
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
        if (event.GetEventType() == wxEVT_LEFT_DOWN)
        {
            // Mirroring browser behavior, move the mouse to the new location
            // before sending the mouse down event.
            wxMouseEvent moveEvent(event);
            moveEvent.SetEventType(wxEVT_MOTION);
            moveEvent.SetLeftDown(false);
            moveEvent.m_clickCount = 0;
            app->HandleMouseEvent(&moveEvent);

        }
        app->HandleMouseEvent(&event);
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
        app->HandleMouseWheelEvent(&event);
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
}
