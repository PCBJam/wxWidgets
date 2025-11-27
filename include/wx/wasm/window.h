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
    virtual void SetLabel(const wxString& label) wxOVERRIDE { m_label = label; }
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

    virtual WXWidget GetHandle() const wxOVERRIDE { return NULL; }

    virtual bool HasTransparentBackground() wxOVERRIDE;

    wxNonOwnedWindow* GetTopLevelWindow();

    bool NeedsPaint() const { return m_childNeedsPaint; }
    bool SelfNeedsPaint() const { return m_selfNeedsPaint; }
    void Invalidate(bool needsPaint);

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

private:
    void Init();

    int m_x, m_y;          // window position
    int m_width, m_height; // window size

    wxString m_label;

    bool m_childNeedsPaint;
    bool m_selfNeedsPaint;

    wxDECLARE_DYNAMIC_CLASS(wxWindowWasm);
    wxDECLARE_NO_COPY_CLASS(wxWindowWasm);
};

extern wxWindow *g_mouseWindow;

#endif // __WX_WASM_WINDOW_H__
