/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/mouse.cpp
// Purpose      Mouse event converter
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/event.h"
#include "wx/log.h"
#include <emscripten/html5.h>

//#define HAS_MOUSE_DETAIL

namespace
{

wxEventType GetMouseDownEventType(int button, int numClicks)
{
    switch (button)
    {
        case 0:
            return numClicks % 2 == 1 ? wxEVT_LEFT_DOWN : wxEVT_LEFT_DCLICK;
            break;
        case 1:
            return numClicks % 2 == 1 ? wxEVT_MIDDLE_DOWN : wxEVT_MIDDLE_DCLICK;
            break;
        case 2:
            return numClicks % 2 == 1 ? wxEVT_RIGHT_DOWN : wxEVT_RIGHT_DCLICK;
            break;
        default:
            wxFAIL_MSG(wxT("invalid mouse button"));
            return wxEVT_NULL;
            break;
    }
}

wxEventType GetMouseUpEventType(int button)
{
    switch (button)
    {
        case 0:
            return wxEVT_LEFT_UP;
            break;
        case 1:
            return wxEVT_MIDDLE_UP;
            break;
        case 2:
            return wxEVT_RIGHT_UP;
            break;
        default:
            wxFAIL_MSG(wxT("invalid mouse button"));
            return wxEVT_NULL;
            break;
    }
}

wxEventType GetMouseEventType(int emscriptenEventType,
                              const EmscriptenMouseEvent &event)
{
    wxEventType eventType;
    std::string eventName;

#ifdef HAS_MOUSE_DETAIL
    int clickCount = event.detail;
#else
    int clickCount = 1;
#endif

    switch (emscriptenEventType)
    {
        case EMSCRIPTEN_EVENT_MOUSEDOWN:
            eventName = "MOUSEDOWN";
            eventType = GetMouseDownEventType(event.button, clickCount);
            break;
        case EMSCRIPTEN_EVENT_MOUSEUP:
            eventName = "MOUSEUP";
            eventType = GetMouseUpEventType(event.button);
            break;
        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            eventName = "MOUSEMOVE";
            eventType = wxEVT_MOTION;
            break;
        case EMSCRIPTEN_EVENT_MOUSEENTER:
            eventName = "MOUSEENTER";
            eventType = wxEVT_ENTER_WINDOW;
            break;
        case EMSCRIPTEN_EVENT_MOUSELEAVE:
            eventName = "MOUSELEAVE";
            eventType = wxEVT_LEAVE_WINDOW;
            break;
        case EMSCRIPTEN_EVENT_CLICK:
            eventName = "CLICK";
            eventType = wxEVT_NULL;
            break;
        case EMSCRIPTEN_EVENT_DBLCLICK:
            eventName = "DBLCLICK";
            eventType = wxEVT_NULL;
            break;
        default:
            wxFAIL_MSG(wxT("invalid mouse event type"));
            eventType = wxEVT_NULL;
            break;
    }

    //printf("event: %s\n", eventName.c_str());

    return eventType;
}

void SetKeyboardState(bool ctrlKey,
                      bool shiftKey,
                      bool altKey,
                      bool metaKey,
                      wxMouseEvent *event)
{
    if ((wxGetOsVersion() & wxOS_MAC) != 0)
    {
        event->SetControlDown(metaKey);
        event->SetMetaDown(false);
    }
    else
    {
        event->SetControlDown(ctrlKey);
        event->SetMetaDown(metaKey);
    }
    event->SetShiftDown(shiftKey);
    event->SetAltDown(altKey);
    event->SetRawControlDown(ctrlKey);
}

void SetMouseState(long targetX,
                   long targetY,
                   unsigned short buttons,
                   wxMouseState *event)
{
    event->SetX(targetX);
    event->SetY(targetY);
    event->SetLeftDown((buttons & 1) != 0);
    event->SetRightDown((buttons & 2) != 0);
    event->SetMiddleDown((buttons & 4) != 0);
    event->SetAux1Down(false);
    event->SetAux2Down(false);
}

void InitMouseEventCommon(const EmscriptenMouseEvent& emscriptenEvent, wxMouseEvent *event)
{
    SetKeyboardState(emscriptenEvent.ctrlKey,
                     emscriptenEvent.shiftKey,
                     emscriptenEvent.altKey,
                     emscriptenEvent.metaKey,
                     event);
    SetMouseState(emscriptenEvent.targetX,
                  emscriptenEvent.targetY,
                  emscriptenEvent.buttons,
                  event);
}

} // anonymous namespace

bool EmscriptenWheelEventToWXEvent(const EmscriptenWheelEvent &emscriptenEvent,
                                   wxOrientation orientation,
                                   wxMouseEvent *mouseEvent)
{
    float delta = orientation == wxHORIZONTAL ? delta = emscriptenEvent.deltaX
                  : delta = -emscriptenEvent.deltaY;

    if (delta == 0)
    {
        return false;
    }

    mouseEvent->SetEventType(wxEVT_MOUSEWHEEL);
    InitMouseEventCommon(emscriptenEvent.mouse, mouseEvent);

    mouseEvent->m_wheelAxis =
        orientation == wxHORIZONTAL ? wxMOUSE_WHEEL_HORIZONTAL
        : wxMOUSE_WHEEL_VERTICAL;

    mouseEvent->m_wheelRotation = delta;
    mouseEvent->m_wheelDelta = 10;

    mouseEvent->m_linesPerAction = 1;
    mouseEvent->m_columnsPerAction = 1;

    return true;
}

bool EmscriptenMouseEventToWXEvent(int emscriptenEventType,
                                   const EmscriptenMouseEvent &emscriptenEvent,
                                   wxMouseEvent *mouseEvent)
{
    wxEventType eventType = GetMouseEventType(emscriptenEventType, emscriptenEvent);
    if (eventType == wxEVT_NULL)
    {
        return false;
    }

    mouseEvent->SetEventType(eventType);
    InitMouseEventCommon(emscriptenEvent, mouseEvent);

    if (eventType == wxEVT_MOTION)
    {
        mouseEvent->m_clickCount = 0;
    }
    else if (eventType == wxEVT_RIGHT_DCLICK ||
             eventType == wxEVT_MIDDLE_DCLICK ||
             eventType == wxEVT_LEFT_DCLICK)
    {
        mouseEvent->m_clickCount = 2;
    }
    else
    {
        mouseEvent->m_clickCount = 1;
    }

    //printf("mouse event type: %d\n", emscriptenEventType);
    //printf("mouse button: %d\n", emscriptenEvent.button);
    //printf("click count: %ld\n", emscriptenEvent.detail);
    //printf("mouse: left down: %d, right down: %d\n",
    //        mouseEvent->LeftIsDown(), mouseEvent->RightIsDown());

    return true;
}

bool EmscriptenTouchEventToWXEvent(int touchEventType,
                                   const EmscriptenTouchEvent &touchEvent,
                                   wxMouseEvent *mouseEvent)
{
    static bool hasTouch = false;
    static long firstTouchId = 0;

    wxEventType eventType = wxEVT_NULL;
    unsigned short buttons;
    int clickCount;
    int touchIndex;

    if (touchEventType == EMSCRIPTEN_EVENT_TOUCHSTART)
    {
        if (!hasTouch && touchEvent.numTouches == 1)
        {
            hasTouch = true;
            firstTouchId = touchEvent.touches[0].identifier;

            eventType = wxEVT_LEFT_DOWN;
            buttons = 1;
            clickCount = 1;
            touchIndex = 0;
        }
    }
    else if (touchEventType == EMSCRIPTEN_EVENT_TOUCHEND ||
             touchEventType == EMSCRIPTEN_EVENT_TOUCHCANCEL)
    {
        if (hasTouch)
        {
            for (int i = 0; i < touchEvent.numTouches; i++)
            {
                if (firstTouchId == touchEvent.touches[i].identifier &&
                    (touchEvent.touches[i].isChanged ||
                     touchEventType == EMSCRIPTEN_EVENT_TOUCHCANCEL))
                {
                    hasTouch = false;

                    eventType = wxEVT_LEFT_UP;
                    buttons = 0;
                    clickCount = 1;
                    touchIndex = i;
                }
            }
        }
    }
    else if (touchEventType == EMSCRIPTEN_EVENT_TOUCHMOVE)
    {
        if (hasTouch)
        {
            for (int i = 0; i < touchEvent.numTouches; i++)
            {
                if (firstTouchId == touchEvent.touches[i].identifier &&
                    touchEvent.touches[i].isChanged)
                {
                    eventType = wxEVT_MOTION;
                    buttons = 1;
                    clickCount = 0;
                    touchIndex = i;
                }
            }
        }
    }

    if (eventType != wxEVT_NULL)
    {
        mouseEvent->SetEventType(eventType);
        mouseEvent->m_clickCount = clickCount;

        SetKeyboardState(touchEvent.ctrlKey,
                         touchEvent.shiftKey,
                         touchEvent.altKey,
                         touchEvent.metaKey,
                         mouseEvent);
        SetMouseState(touchEvent.touches[touchIndex].targetX,
                      touchEvent.touches[touchIndex].targetY,
                      buttons,
                      mouseEvent);
        return true;
    }
    else
    {
        return false;
    }
}
