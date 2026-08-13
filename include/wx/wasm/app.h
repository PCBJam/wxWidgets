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

    // deferGLCanvasWindows: skip a synchronous repaint of any non-main window hosting a
    // wxGLCanvas (the 3D viewer, whose paint runs the multi-threaded CPU raytracer). Used
    // on the mouse-button repaint path (HandleMouseEvent), which is driven synchronously
    // from a DOM event callback where the raytracer's Worker boot would deadlock — the
    // window keeps NeedsPaint() and is repainted by the yielding per-frame pump instead.
    void Paint(bool deferGLCanvasWindows = false);

    // Paint the same set as Paint() while root execution is active. During a
    // modal child execution, paint only the exact active lease's top-level
    // window. This keeps synchronous input and browser-chrome paint tails
    // from traversing a parked parent's model.
    void PaintCurrentExecutionScope(bool deferGLCanvasWindows = false);

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
    void PaintTopLevelWindows(bool deferGLCanvasWindows,
                              bool currentExecutionScopeOnly);

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
