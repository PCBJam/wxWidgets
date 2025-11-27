/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/nonownedwnd.h
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2019 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_NONOWNEDWND_H_
#define _WX_WASM_NONOWNEDWND_H_

#include <queue>

class WXDLLIMPEXP_CORE wxNonOwnedWindow : public wxNonOwnedWindowBase
{
public:
    // construction
    wxNonOwnedWindow() { Init(); }
    wxNonOwnedWindow(wxWindow *parent,
                     wxWindowID id,
                     const wxPoint& pos = wxDefaultPosition,
                     const wxSize& size = wxDefaultSize,
                     long style = 0,
                     const wxString& name = wxPanelNameStr)
    {
        Init();
        Create(parent, id, pos, size, style, name);
    }

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxPanelNameStr);

    virtual ~wxNonOwnedWindow();

    virtual bool Show(bool show = true) wxOVERRIDE;

    virtual void Raise() wxOVERRIDE;
    virtual void Lower() wxOVERRIDE;

    virtual void SetSizer(wxSizer *sizer, bool deleteOld = true);

    virtual wxString GetCSSClassList() const { return "window"; }

    bool IsMainFrame() const;

    int GetCSSId() const { return m_cssId; }

    void HandlePaintRequests();

protected:
    virtual void DoSetSize(int x, int y,
                           int width, int height,
                           int sizeFlags = wxSIZE_AUTO) wxOVERRIDE;

    virtual void OnAnimationFrame() {}

    void SetCSSId(int cssId) { m_cssId = cssId; }

private:
    void Init();

    int m_cssId;

    friend class wxApp;
};

#endif // _WX_WASM_NONOWNEDWND_H_
