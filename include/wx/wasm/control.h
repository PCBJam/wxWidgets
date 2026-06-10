/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/control.h
// Purpose:     wxControl class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_CONTROL_H__
#define __WX_WASM_CONTROL_H__

class WXDLLIMPEXP_CORE wxControl : public wxControlBase
{
public:
    wxControl();
    wxControl(wxWindow *parent, wxWindowID id,
              const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize, long style = 0,
              const wxValidator& validator = wxDefaultValidator,
              const wxString& name = wxASCII_STR(wxControlNameStr));

    bool Create(wxWindow *parent, wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize, long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxControlNameStr));

protected:
    virtual wxSize DoGetBestSize() const wxOVERRIDE;

private:
    wxDECLARE_DYNAMIC_CLASS(wxControl);
};

#endif // __WX_WASM_CONTROL_H__
