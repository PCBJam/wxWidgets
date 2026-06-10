/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/window.h
// Purpose:     wxWindowWasm
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_WINDOW_H__
#define __WX_WASM_WINDOW_H__

class wxNonOwnedWindow;

#ifndef __WXUNIVERSAL__
// DOM event kinds delivered from wx-dom.js via wx_dom_event()
// (src/wasm/domevents.cpp) to wxWindowWasm::OnDomEvent overrides.
enum wxDomEventKind
{
    wxDOM_EVENT_CLICK = 1,
    wxDOM_EVENT_INPUT = 2,
    wxDOM_EVENT_CHANGE = 3,
    wxDOM_EVENT_FOCUSIN = 4,
    wxDOM_EVENT_FOCUSOUT = 5,
    wxDOM_EVENT_ENTER = 6,
    wxDOM_EVENT_SPIN_UP = 7,
    wxDOM_EVENT_SPIN_DOWN = 8,
    wxDOM_EVENT_MENU = 9,
    wxDOM_EVENT_TOOL = 10
};
#endif // !__WXUNIVERSAL__

class WXDLLIMPEXP_CORE wxWindowWasm : public wxWindowBase
{
public:
    // creating the window
    // -------------------
    wxWindowWasm();
    wxWindowWasm(wxWindow *parent,
                 wxWindowID id,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 long style = 0,
                 const wxString& name = wxPanelNameStr);
    virtual ~wxWindowWasm();

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxPanelNameStr);

    // implement base class pure virtuals
    virtual void SetLabel(const wxString& label) wxOVERRIDE;
    virtual wxString GetLabel() const wxOVERRIDE { return m_label; }

    virtual void Raise() wxOVERRIDE;
    virtual void Lower() wxOVERRIDE;

    virtual bool Show(bool show = true) wxOVERRIDE;

    virtual void SetFocus() wxOVERRIDE;

    virtual void WarpPointer(int x, int y) wxOVERRIDE;

    virtual void Refresh(bool eraseBackground = true,
                         const wxRect *rect = (const wxRect *) NULL) wxOVERRIDE;

    virtual bool SetFont(const wxFont& font) wxOVERRIDE;

    virtual bool SetCursor(const wxCursor &cursor) wxOVERRIDE;

    virtual int GetCharHeight() const wxOVERRIDE;
    virtual int GetCharWidth() const wxOVERRIDE;

    virtual double GetContentScaleFactor() const wxOVERRIDE;
    virtual double GetDPIScaleFactor() const wxOVERRIDE;

#if wxUSE_DRAG_AND_DROP
    virtual void SetDropTarget(wxDropTarget *dropTarget) wxOVERRIDE;
#endif // wxUSE_DRAG_AND_DROP

    virtual bool IsDoubleBuffered() const wxOVERRIDE { return true; }

#ifndef __WXUNIVERSAL__
    // In universal mode these wxWindowBase pure virtuals are implemented by
    // the wxUniv wxWindow layer; in the native (DOM) build wxWindowWasm IS
    // wxWindow, so it must provide them itself.
    virtual void SetScrollbar(int orient, int pos, int thumbvisible,
                              int range, bool refresh = true) wxOVERRIDE;
    virtual void SetScrollPos(int orient, int pos, bool refresh = true) wxOVERRIDE;
    virtual int GetScrollPos(int orient) const wxOVERRIDE;
    virtual int GetScrollThumb(int orient) const wxOVERRIDE;
    virtual int GetScrollRange(int orient) const wxOVERRIDE;
    virtual void ScrollWindow(int dx, int dy, const wxRect* rect = NULL) wxOVERRIDE;
#if wxUSE_MENUS
    virtual bool DoPopupMenu(wxMenu *menu, int x, int y) wxOVERRIDE;
    virtual void DoPopupMenu(wxMenu *menu, int x, int y,
                             std::function<void (bool)> callback) wxOVERRIDE;
#endif // wxUSE_MENUS

    // ----- DOM-backed native controls -----
    // A control becomes DOM-backed by calling WasmCreateDomNode() from its
    // Create(); geometry, visibility, enabled state, font and destruction
    // then sync automatically from the shared window machinery.
    bool WasmCreateDomNode(const char *tag, const char *typeAttr = NULL);
    int WasmGetDomId() const { return m_domId; }

    // DOM events (click/input/focus...) routed here by src/wasm/domevents.cpp.
    virtual void OnDomEvent(wxDomEventKind kind);
#endif // !__WXUNIVERSAL__

    virtual WXWidget GetHandle() const wxOVERRIDE { return NULL; }

    virtual bool HasTransparentBackground() wxOVERRIDE;

    wxNonOwnedWindow* GetTopLevelWindow();

    bool NeedsPaint() const { return m_childNeedsPaint; }
    bool SelfNeedsPaint() const { return m_selfNeedsPaint; }
    void Invalidate(bool needsPaint);

    // Returns true after wxWindowWasm::Create() completes
    bool IsWasmCreated() const { return m_isCreated; }

protected:
    virtual void DoGetTextExtent(const wxString& string,
                                 int *x, int *y,
                                 int *descent = NULL,
                                 int *externalLeading = NULL,
                                 const wxFont *theFont = NULL) const wxOVERRIDE;

    virtual void DoClientToScreen(int *x, int *y) const wxOVERRIDE;
    virtual void DoScreenToClient(int *x, int *y) const wxOVERRIDE;

    virtual void DoGetPosition(int *x, int *y) const wxOVERRIDE;
    virtual void DoGetSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoGetClientSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) wxOVERRIDE;
    virtual void DoSetClientSize(int width, int height) wxOVERRIDE;

    virtual void DoMoveWindow(int x, int y, int width, int height) wxOVERRIDE;
    virtual void DoEnable(bool enable) wxOVERRIDE;

    virtual void DoCaptureMouse() wxOVERRIDE;
    virtual void DoReleaseMouse() wxOVERRIDE;

    virtual void DoFreeze() wxOVERRIDE { }
    virtual void DoThaw() wxOVERRIDE;

    // implementation
    void KillFocus();

    void EraseBackgroundWindow();
    void PaintSelf();
    void PaintChildren(bool selfWasPainted);
    void DoPaint(bool parentWasPainted);

    // Notify child windows to update their platform-specific DOM visibility
    // based on IsShownOnScreen() (used when parent visibility changes)
    void UpdateChildrenDOMVisibility();

private:
    void Init();

    int m_x, m_y;          // window position
    int m_width, m_height; // window size

#ifndef __WXUNIVERSAL__
    // built-in scrollbar state caches (index 0 = horizontal, 1 = vertical);
    // honest enough for wxScrollHelper users until the DOM port renders
    // real scrollbars. TODO(dom-phase-2)
    int m_scrollPos[2];
    int m_scrollThumb[2];
    int m_scrollRange[2];

    // JS-side element id for DOM-backed native controls (0 = none).
    int m_domId;

    // Push the wx rect (in top-level-window coordinates) to the DOM element.
    void UpdateDomGeometry();

    // Push IsShownOnScreen() to this window's and all descendants' DOM
    // elements (a hidden ancestor — e.g. an unselected notebook page —
    // must hide the whole DOM subtree).
    void UpdateDomVisibility();
#endif // !__WXUNIVERSAL__

    wxString m_label;

    bool m_childNeedsPaint;
    bool m_selfNeedsPaint;
    bool m_isCreated;      // true after Create() completes, used to avoid calling
                           // UpdateElementRegistry during construction

    wxDECLARE_DYNAMIC_CLASS(wxWindowWasm);
    wxDECLARE_NO_COPY_CLASS(wxWindowWasm);
};

extern wxWindow *g_mouseWindow;

#endif // __WX_WASM_WINDOW_H__
