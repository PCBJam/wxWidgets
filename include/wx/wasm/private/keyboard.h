/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/keyboard.h
// Purpose:     Keyboard event converter
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_KEYBOARD_H_
#define _WX_WASM_PRIVATE_KEYBOARD_H_

#include "wx/event.h"

bool EmscriptenKeyboardEventToWXEvent(int emscriptenEventType,
                                      const EmscriptenKeyboardEvent &emscriptenEvent,
                                      wxKeyEvent *keyEvent);

bool KeyCodeNeedsKeyDownEvent(int keyCode);
bool KeyCodeNeedsCharEvent(int keyCode);

#endif // _WX_WASM_PRIVATE_KEYBOARD_H_
