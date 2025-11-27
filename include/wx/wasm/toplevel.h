/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/toplevel.h
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_TOPLEVEL_H_
#define _WX_WASM_TOPLEVEL_H_

//-----------------------------------------------------------------------------
// wxTopLevelWindowWasm
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxTopLevelWindowWasm : public wxTopLevelWindowBase
{
    typedef wxTopLevelWindowBase base_type;
public:
    // construction
    wxTopLevelWindowWasm() { Init(); }
    wxTopLevelWindowWasm(wxWindow *parent,
                         wxWindowID id,
                         const wxString& title,
                         const wxPoint& pos = wxDefaultPosition,
                         const wxSize& size = wxDefaultSize,
                         long style = wxDEFAULT_FRAME_STYLE,
                         const wxString& name = wxFrameNameStr)
    {
        Init();
        Create(parent, id, title, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxFrameNameStr);

    virtual ~wxTopLevelWindowWasm() { }

    virtual wxPoint GetClientAreaOrigin() const wxOVERRIDE;

    // implement base class pure virtuals
    virtual void Maximize(bool WXUNUSED(maximize) = true) wxOVERRIDE { }
    virtual bool IsMaximized() const wxOVERRIDE { return false; }
    virtual bool IsAlwaysMaximized() const wxOVERRIDE { return IsMainFrame(); }
    virtual void Iconize(bool WXUNUSED(iconize) = true) wxOVERRIDE { }
    virtual bool IsIconized() const wxOVERRIDE { return false; }
    virtual void Restore() wxOVERRIDE { }

    virtual void SetIcons(const wxIconBundle& icons) wxOVERRIDE;

    virtual void ShowWithoutActivating() wxOVERRIDE;
    virtual bool ShowFullScreen(bool show, long style = wxFULLSCREEN_ALL) wxOVERRIDE;
    virtual bool IsFullScreen() const wxOVERRIDE;

    virtual bool IsActive() wxOVERRIDE { return m_isActive; }

    virtual void SetTitle(const wxString &title) wxOVERRIDE;
    virtual wxString GetTitle() const wxOVERRIDE { return m_title; }

    virtual wxString GetCSSClassList() const wxOVERRIDE {
      return wxNonOwnedWindow::GetCSSClassList() + " toplevel";
    }

protected:
    virtual void DoGetClientSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoSetClientSize(int width, int height) wxOVERRIDE;

    virtual void DoScreenToClient(int *x, int *y) const wxOVERRIDE;
    virtual void DoClientToScreen(int *x, int *y) const wxOVERRIDE;

    virtual bool HasTitleBar() const;

private:
    void Init();

    void SetActive(bool active) { m_isActive = active; }

    void DrawTitleText(wxDC& dc, const wxRect& rect);
    void DrawMinimizeButton(wxDC& dc, const wxRect& rect);

    void StartDrag(const wxPoint& pos);
    void EndDrag();
    void DragMove(const wxPoint& pos);

    void OnNcPaint(wxNcPaintEvent& event);
    void OnMouseDown(wxMouseEvent& event);
    void OnMouseUp(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);

    bool m_isActive;
    wxString m_title;

    wxRect m_minimizeButtonRect;

    bool m_isDragging;
    wxPoint m_dragOffset;

    friend class wxApp;

    wxDECLARE_EVENT_TABLE();
};

#endif // _WX_WASM_TOPLEVEL_H_
