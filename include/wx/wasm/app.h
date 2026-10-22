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
struct wxWasmParkedMouse;

//-----------------------------------------------------------------------------
// wxApp
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxApp: public wxAppBase
{
public:
    wxApp();
    virtual ~wxApp();

#if wxUSE_EXCEPTIONS
    // Browser-port contract: an exception escaping an event handler must NOT
    // tear down the app (the base default exits the main loop, which in the
    // browser reads as a silent clean shutdown mid-session). Parity with the
    // pre-EH builds, where the throw escaped to the JS dispatch boundary and
    // was contained there. Surface it, keep running.
    virtual bool OnExceptionInMainLoop() wxOVERRIDE;
#endif

    // deferGLCanvasWindows: skip a synchronous repaint of any non-main window hosting a
    // wxGLCanvas (the 3D viewer, whose paint runs the multi-threaded CPU raytracer). Used
    // on the mouse-button repaint path (HandleMouseEvent), which is driven synchronously
    // from a DOM event callback where the raytracer's Worker boot would deadlock — the
    // window keeps NeedsPaint() and is repainted by the yielding per-frame pump instead.
    void Paint(bool deferGLCanvasWindows = false);

    bool IsKeyPressed(long keyCode);

    void GetMousePosition(int *x, int *y);
    void GetMouseState(wxMouseState *mouseState);
    // Update the cached mouse position. The browser cannot move the OS pointer,
    // so wxWindow::WarpPointer() calls this to keep wxGetMousePosition() in sync
    // with a programmatic warp (matching desktop, where the real pointer moves).
    void SetMousePosition(const wxPoint& screenPos);
    wxWindow *GetMouseWindow(const wxPoint& position) const;

    // Internal use only
    wxWasmDisplay* GetDisplay() { return m_display; }

    bool HandleKeyEvent(wxKeyEvent *event);
    void HandleMouseEvent(wxMouseEvent *event);
    void HandleMouseWheelEvent(wxMouseEvent *event);

    // Internal use only: deliver a wheel tick as if the pointer were at
    // screenPos (a tick replayed after a park carries the pointer position
    // it was made at, see HandleMouseWheelEvent).
    void DispatchMouseWheelEvent(wxMouseEvent *event, const wxPoint& screenPos);
    // Internal use only: replay the oldest mouse event queued while a chain
    // was parked, one per step (see HandleMouseEvent's parked branch).
    void ReplayParkedMouseStep();
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

    // Mouse. m_mouseState is what wxGetMouseState()/wxGetMousePosition()
    // report; m_liveMouseState is the browser pointer. They differ only while
    // mouse events queued during a park are still waiting to be replayed:
    // each replay sets m_mouseState to the state recorded with that event, so
    // a handler reading the "live" pointer sees the pointer as it was when
    // the event happened, and the step after the last replay restores the
    // live state.
    wxMouseState m_mouseState;
    wxMouseState m_liveMouseState;

    // The queued, not yet replayed hover motion (no button held) that later
    // hover motion is coalesced into, or NULL. Reset by every queued button,
    // key or held-button motion event, so motion is only coalesced between
    // them, never across.
    wxWasmParkedMouse *m_parkedMotion;

    friend class wxDropSource;
};

#endif // _WX_WASM_APP_H_
