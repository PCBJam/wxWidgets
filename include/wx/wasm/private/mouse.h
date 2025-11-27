/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/mouse.h
// Purpose:     Mouse event converter
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_MOUSE_H_
#define _WX_WASM_PRIVATE_MOUSE_H_

#include "wx/event.h"

#include <emscripten/html5.h>

bool EmscriptenMouseEventToWXEvent(int emscriptenEventType,
                                   const EmscriptenMouseEvent &emscriptenEvent,
                                   wxMouseEvent *mouseEvent);

bool EmscriptenWheelEventToWXEvent(const EmscriptenWheelEvent &emscriptenEvent,
                                   wxOrientation orientation,
                                   wxMouseEvent *mouseEvent);

bool EmscriptenTouchEventToWXEvent(int touchEventType,
                                   const EmscriptenTouchEvent &touchEvent,
                                   wxMouseEvent *mouseEvent);

#endif // _WX_WASM_PRIVATE_MOUSE_H_
