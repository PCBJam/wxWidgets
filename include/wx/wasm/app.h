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

    // D5 (pcbjam docs/features/async/22): hand the main loop to a scheduler
    // context and return immediately — the app keeps running, driven by
    // browser frame ticks, and main() returns with the runtime alive.
    virtual int OnRun() wxOVERRIDE;

    // The detached main-loop context body runs the stock main loop through
    // this thunk; public only for the context entry in evtloop.cpp.
    int RunMainLoopOnContext() { return wxAppBase::OnRun(); }

#if wxUSE_EXCEPTIONS
    // Browser-port contract: an exception escaping an event handler must NOT
    // tear down the app (the base default exits the main loop, which under
    // the detached D5 loop reads as a silent clean shutdown mid-session).
    // Parity with the pre-EH builds, where the throw escaped to the JS
    // dispatch boundary and was contained there. Surface it, keep running.
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
