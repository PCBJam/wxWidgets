/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/frame.h
// Purpose:     wxFrame class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_FRAME_H__
#define __WX_WASM_FRAME_H__

// Bar geometry mirrors src/univ/framuniv.cpp (what the canvas port runs):
// menu/tool bars live above the client-area origin, the status bar below
// the client area, and DoGetClientSize subtracts all of them.
class WXDLLIMPEXP_CORE wxFrame : public wxFrameBase
{
public:
    wxFrame();
    wxFrame(wxWindow *parent,
            wxWindowID id,
            const wxString& title,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            long style = wxDEFAULT_FRAME_STYLE,
            const wxString& name = wxASCII_STR(wxFrameNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_FRAME_STYLE,
                const wxString& name = wxASCII_STR(wxFrameNameStr));

    virtual wxPoint GetClientAreaOrigin() const wxOVERRIDE;

#if wxUSE_MENUS
    virtual void DetachMenuBar() wxOVERRIDE;
    virtual void AttachMenuBar(wxMenuBar *menubar) wxOVERRIDE;
    virtual void PositionMenuBar() wxOVERRIDE;
#endif // wxUSE_MENUS

#if wxUSE_STATUSBAR
    virtual void PositionStatusBar() wxOVERRIDE;
    virtual wxStatusBar* CreateStatusBar(int number = 1, long style = wxSTB_DEFAULT_STYLE,
                                         wxWindowID id = 0,
                                         const wxString& name = wxASCII_STR(wxStatusBarNameStr)) wxOVERRIDE;
#endif // wxUSE_STATUSBAR

#if wxUSE_TOOLBAR
    virtual wxToolBar* CreateToolBar(long style = -1,
                                     wxWindowID id = wxID_ANY,
                                     const wxString& name = wxASCII_STR(wxToolBarNameStr)) wxOVERRIDE;
    virtual void PositionToolBar() wxOVERRIDE;
#endif // wxUSE_TOOLBAR

protected:
    void OnSize(wxSizeEvent& event);

    virtual void DoGetClientSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoSetClientSize(int width, int height) wxOVERRIDE;

private:
    wxDECLARE_DYNAMIC_CLASS(wxFrame);
    wxDECLARE_EVENT_TABLE();
};

#endif // __WX_WASM_FRAME_H__
