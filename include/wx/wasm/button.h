/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/button.h
// Purpose:     wxButton class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_BUTTON_H__
#define __WX_WASM_BUTTON_H__

class WXDLLIMPEXP_CORE wxButton : public wxButtonBase
{
public:
    wxButton();
    wxButton(wxWindow *parent, wxWindowID id,
             const wxString& label = wxEmptyString,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize, long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxButtonNameStr));

    bool Create(wxWindow *parent, wxWindowID id,
                const wxString& label = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize, long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxButtonNameStr));

    virtual wxWindow *SetDefault() wxOVERRIDE;

    virtual void SetLabel(const wxString& label) wxOVERRIDE;

    // wxEVT_BUTTON from the real <button>'s click
    virtual void OnDomEvent(wxDomEventKind kind) wxOVERRIDE;

private:
    wxDECLARE_DYNAMIC_CLASS_NO_COPY(wxButton);
};

#endif // __WX_WASM_BUTTON_H__
