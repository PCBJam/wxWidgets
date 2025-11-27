/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/keyboard.cpp
// Purpose      Keyboard event converter
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/event.h"
#include "wx/log.h"
#include "wx/utils.h"

#include <emscripten/html5.h>

namespace
{

struct KeyTranslationEntry
{
    const char *domKeyCode;
    int wxKeyCode;
};

const KeyTranslationEntry kKeyTranslationEntries[] =
{
    {"Digit1", '1'},
    {"Digit2", '2'},
    {"Digit3", '3'},
    {"Digit4", '4'},
    {"Digit5", '5'},
    {"Digit6", '6'},
    {"Digit7", '7'},
    {"Digit8", '8'},
    {"Digit9", '9'},
    {"Digit0", '0'},
    {"KeyA", 'A'},
    {"KeyB", 'B'},
    {"KeyC", 'C'},
    {"KeyD", 'D'},
    {"KeyE", 'E'},
    {"KeyF", 'F'},
    {"KeyG", 'G'},
    {"KeyH", 'H'},
    {"KeyI", 'I'},
    {"KeyJ", 'J'},
    {"KeyK", 'K'},
    {"KeyL", 'L'},
    {"KeyM", 'M'},
    {"KeyN", 'N'},
    {"KeyO", 'O'},
    {"KeyP", 'P'},
    {"KeyQ", 'Q'},
    {"KeyR", 'R'},
    {"KeyS", 'S'},
    {"KeyT", 'T'},
    {"KeyU", 'U'},
    {"KeyV", 'V'},
    {"KeyW", 'W'},
    {"KeyX", 'X'},
    {"KeyY", 'Y'},
    {"KeyZ", 'Z'},
    {"Comma", ','},
    {"Period", '.'},
    {"Semicolon", ';'},
    {"Quote", '\''},
    {"BracketLeft", '['},
    {"BracketRight", ']'},
    {"Backquote", '`'},
    {"Backslash", '\\'},
    {"Minus", '-'},
    {"Equal", '='},
    {"AltLeft", WXK_ALT},
    {"AltRight", WXK_ALT},
    {"CapsLock", WXK_CAPITAL},
    {"ControlLeft", WXK_CONTROL},
    {"ControlRight", WXK_CONTROL},
    {"OSLeft", WXK_CONTROL},
    {"OSRight", WXK_CONTROL},
    {"MetaLeft", WXK_CONTROL},
    {"MetaRight", WXK_CONTROL},
    {"ShiftLeft", WXK_SHIFT},
    {"ShiftRight", WXK_SHIFT},
    {"ContextMenu", WXK_WINDOWS_MENU},
    {"Enter", WXK_RETURN},
    {"Space", WXK_SPACE},
    {"Tab", WXK_TAB},
    {"Backspace", WXK_BACK},
    {"Delete", WXK_DELETE},
    {"End", WXK_END},
    {"Help", WXK_HELP},
    {"Home", WXK_HOME},
    {"Insert", WXK_INSERT},
    {"PageDown", WXK_PAGEDOWN},
    {"PageUp", WXK_PAGEUP},
    {"ArrowDown", WXK_DOWN},
    {"ArrowLeft", WXK_LEFT},
    {"ArrowRight", WXK_RIGHT},
    {"ArrowUp", WXK_UP},
    {"Escape", WXK_ESCAPE},
    {"PrintScreen", WXK_PRINT},
    {"ScrollLock", WXK_SCROLL},
    {"Pause", WXK_PAUSE},
    {"F1", WXK_F1},
    {"F2", WXK_F2},
    {"F3", WXK_F3},
    {"F4", WXK_F4},
    {"F5", WXK_F5},
    {"F6", WXK_F6},
    {"F7", WXK_F7},
    {"F8", WXK_F8},
    {"F9", WXK_F9},
    {"F10", WXK_F10},
    {"F11", WXK_F11},
    {"F12", WXK_F12},
    {"F13", WXK_F13},
    {"F14", WXK_F14},
    {"F15", WXK_F15},
    {"F16", WXK_F16},
    {"F17", WXK_F17},
    {"F18", WXK_F18},
    {"F19", WXK_F19},
    {"F20", WXK_F20},
    {"F21", WXK_F21},
    {"F22", WXK_F22},
    {"F23", WXK_F23},
    {"F24", WXK_F24},
    {"NumLock", WXK_NUMLOCK},
    {"Numpad0", WXK_NUMPAD0},
    {"Numpad1", WXK_NUMPAD1},
    {"Numpad2", WXK_NUMPAD2},
    {"Numpad3", WXK_NUMPAD3},
    {"Numpad4", WXK_NUMPAD4},
    {"Numpad5", WXK_NUMPAD5},
    {"Numpad6", WXK_NUMPAD6},
    {"Numpad7", WXK_NUMPAD7},
    {"Numpad8", WXK_NUMPAD8},
    {"Numpad9", WXK_NUMPAD9},
    {"NumpadAdd", WXK_NUMPAD_ADD},
    {"NumpadComma", WXK_NUMPAD_DECIMAL}, // ?
    {"NumpadDecimal", WXK_NUMPAD_DECIMAL},
    {"NumpadDivide", WXK_NUMPAD_DIVIDE},
    {"NumpadEnter", WXK_NUMPAD_ENTER},
    {"NumpadEqual", WXK_NUMPAD_EQUAL},
    {"NumpadMultiply", WXK_NUMPAD_MULTIPLY},
    {"NumpadSubtract", WXK_NUMPAD_SUBTRACT}
};

const int kNumKeyTranslationEntries = sizeof(kKeyTranslationEntries) / sizeof(kKeyTranslationEntries[0]);

WX_DECLARE_HASH_MAP(char *, long, wxStringHash, wxStringEqual, KeyTranslationMap);

KeyTranslationMap *CreateKeyTranslationMap()
{
    KeyTranslationMap *keyTranslationMap = new KeyTranslationMap();

    for (int i = 0; i < kNumKeyTranslationEntries; i++)
    {
        const KeyTranslationEntry &entry = kKeyTranslationEntries[i];
        (*keyTranslationMap)[entry.domKeyCode] = entry.wxKeyCode;
    }

    // OS specific translations.
    wxOperatingSystemId systemId = wxGetOsVersion();

    if (systemId & wxOS_WINDOWS)
    {
        (*keyTranslationMap)["OSLeft"] = WXK_WINDOWS_LEFT;
        (*keyTranslationMap)["OSRight"] = WXK_WINDOWS_RIGHT;
    }

    return keyTranslationMap;
}

const KeyTranslationMap &GetKeyTranslationMap()
{
    static KeyTranslationMap *keyTranslationMap = NULL;

    if (keyTranslationMap == NULL)
    {
        keyTranslationMap = CreateKeyTranslationMap();
    }
    return *keyTranslationMap;
}

int DOMKeyCodeToWXKeyCode(const char *domKeyCode)
{
    const KeyTranslationMap &keyTranslationMap = GetKeyTranslationMap();
    KeyTranslationMap::const_iterator it = keyTranslationMap.find(domKeyCode);
    return it != keyTranslationMap.end() ? it->second : WXK_NONE;
}

wxEventType GetKeyEventType(int emscriptenEventType)
{
    wxEventType eventType;

    switch (emscriptenEventType)
    {
        case EMSCRIPTEN_EVENT_KEYDOWN:
            //printf("key down event\n");
            eventType = wxEVT_KEY_DOWN;
            break;
        case EMSCRIPTEN_EVENT_KEYUP:
            //printf("key up event\n");
            eventType = wxEVT_KEY_UP;
            break;
        case EMSCRIPTEN_EVENT_KEYPRESS:
            //printf("key char event\n");
            eventType = wxEVT_CHAR;
            break;
        default:
            wxFAIL_MSG(wxT("invalid key event type"));
            eventType = wxEVT_NULL;
            break;
    }

    return eventType;
}

void SetKeyboardModifiers(const EmscriptenKeyboardEvent& emscriptenEvent,
                          wxKeyboardState *event)
{
    if ((wxGetOsVersion() & wxOS_MAC) != 0)
    {
        event->SetControlDown(emscriptenEvent.metaKey);
        event->SetMetaDown(false);
    }
    else
    {
        event->SetControlDown(emscriptenEvent.ctrlKey);
        event->SetMetaDown(emscriptenEvent.metaKey);
    }
    event->SetShiftDown(emscriptenEvent.shiftKey);
    event->SetAltDown(emscriptenEvent.altKey);
    event->SetRawControlDown(emscriptenEvent.ctrlKey);
}

void InitKeyEvent(int emscriptenEventType,
                  const EmscriptenKeyboardEvent& emscriptenEvent,
                  wxKeyEvent *event)
{
    static wxMBConvUTF8 converter;

    SetKeyboardModifiers(emscriptenEvent, event);

    event->m_rawCode = emscriptenEvent.keyCode;
    event->m_rawFlags = 0;

    const std::string domKeyCode(emscriptenEvent.code);
    const std::string charText(emscriptenEvent.key);

    //printf("key code: %lu\n", emscriptenEvent.keyCode);
    //printf("char code: %c\n", static_cast<char>(emscriptenEvent.keyCode));
    //printf("dom code: %s\n", domKeyCode.c_str());
    //printf("char text: %s\n", charText.c_str());

    if (emscriptenEventType == EMSCRIPTEN_EVENT_KEYPRESS &&
        charText.size() == 1 &&
        static_cast<wxKeyCode>(charText.at(0)) <= WXK_DELETE)
    {
        event->m_keyCode = charText.at(0);
    }
    else if (!domKeyCode.empty())
    {
        event->m_keyCode = DOMKeyCodeToWXKeyCode(domKeyCode.c_str());
    }
    else
    {
        event->m_keyCode = WXK_NONE;
    }

#if wxUSE_UNICODE
    if (event->m_keyCode <= WXK_DELETE && event->m_keyCode != WXK_NONE)
    {
        char temp = event->m_keyCode;
        converter.ToWChar(&event->m_uniChar, 1, &temp, 1);
    }
    else
    {
        event->m_uniChar = WXK_NONE;
    }
#endif

    event->SetTimestamp(0);
}

} // anonymous namespace

bool EmscriptenKeyboardEventToWXEvent(int emscriptenEventType,
                                      const EmscriptenKeyboardEvent &emscriptenEvent,
                                      wxKeyEvent *keyEvent)
{
    wxEventType eventType = GetKeyEventType(emscriptenEventType);

    keyEvent->SetEventType(eventType);
    InitKeyEvent(emscriptenEventType, emscriptenEvent, keyEvent);

    return true;
}

bool KeyCodeNeedsCharEvent(int keyCode)
{
    switch (keyCode)
    {
        case WXK_ESCAPE:
        case WXK_BACK:
        case WXK_DELETE:
        case WXK_CLEAR:
        case WXK_PAUSE:
        case WXK_END:
        case WXK_HOME:
        case WXK_LEFT:
        case WXK_UP:
        case WXK_RIGHT:
        case WXK_DOWN:
        case WXK_SELECT:
        case WXK_PRINT:
        case WXK_SNAPSHOT:
        case WXK_INSERT:
        case WXK_HELP:
        case WXK_NUMLOCK:
        case WXK_SCROLL:
        case WXK_PAGEUP:
        case WXK_PAGEDOWN:
        case WXK_F1:
        case WXK_F2:
        case WXK_F3:
        case WXK_F4:
        case WXK_F5:
        case WXK_F6:
        case WXK_F7:
        case WXK_F8:
        case WXK_F9:
        case WXK_F10:
        case WXK_F11:
        case WXK_F12:
        case WXK_F13:
        case WXK_F14:
        case WXK_F15:
        case WXK_F16:
        case WXK_F17:
        case WXK_F18:
        case WXK_F19:
        case WXK_F20:
        case WXK_F21:
        case WXK_F22:
        case WXK_F23:
        case WXK_F24:
            // case WXK_SELECT: ??
            // case WXK_EXECUTE: ??
            return true;
            break;
        default:
            break;
    }

    return false;
}
