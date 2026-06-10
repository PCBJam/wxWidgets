/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/frame.h
// Purpose:     wxFrame class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_FRAME_H__
#define __WX_WASM_FRAME_H__

// wxFrameBase has no pure virtuals of its own: it stores the menu, tool and
// status bars itself and all the window machinery comes from
// wxTopLevelWindowWasm/wxWindowWasm.
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

private:
    wxDECLARE_DYNAMIC_CLASS(wxFrame);
};

#endif // __WX_WASM_FRAME_H__
