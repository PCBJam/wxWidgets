/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/app.h
// Purpose:     wxApp class
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_APP_H_
#define _WX_WASM_APP_H_

#include "wx/event.h"
#include "wx/hashset.h"
#include "wx/kbdstate.h"
#include "wx/mousestate.h"
#include "wx/timer.h"

class EmscriptenKeyboardEvent;
class wxWasmDisplay;

//-----------------------------------------------------------------------------
// wxApp
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxApp: public wxAppBase
{
public:
    wxApp();
    virtual ~wxApp();

    void Paint();

    bool IsKeyPressed(long keyCode);

    void GetMousePosition(int *x, int *y);
    void GetMouseState(wxMouseState *mouseState);
    wxWindow *GetMouseWindow(const wxPoint& position) const;

    // Internal use only
    wxWasmDisplay* GetDisplay() { return m_display; }

    bool HandleKeyEvent(wxKeyEvent *event);
    void HandleMouseEvent(wxMouseEvent *event);
    void HandleMouseWheelEvent(wxMouseEvent *event);
    void HandleSizeEvent(const wxSizeEvent& event);
    void HandleActivateEvent(wxActivateEvent *event);
    void HandleCloseEvent(wxCloseEvent* event);

protected:
    void SetKeyPressed(long keyCode, bool pressed);

    void SendMouseEventToWindow(wxMouseEvent *event, wxWindow *window);

    void UpdateMouseState(const wxMouseEvent& event);
    void UpdateMouseState(const wxKeyEvent& event);

private:
    wxDECLARE_DYNAMIC_CLASS(wxApp);

    // Display
    wxWasmDisplay *m_display; 

    // Keyboard
    WX_DECLARE_HASH_SET(long, wxIntegerHash, wxIntegerEqual, KeyCodeSet);
    KeyCodeSet m_keyCodeSet;

    // Mouse
    wxMouseState m_mouseState;

    friend class wxDropSource;
};

#endif // _WX_WASM_APP_H_
