/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/spinbutt.h
// Purpose:     wxSpinButton class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_SPINBUTT_H__
#define __WX_WASM_SPINBUTT_H__

class WXDLLIMPEXP_CORE wxSpinButton : public wxSpinButtonBase
{
public:
    wxSpinButton();
    wxSpinButton(wxWindow *parent,
                 wxWindowID id = -1,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 long style = wxSP_VERTICAL,
                 const wxString& name = wxSPIN_BUTTON_NAME);

    bool Create(wxWindow *parent,
                wxWindowID id = -1,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSP_VERTICAL,
                const wxString& name = wxSPIN_BUTTON_NAME);

    virtual int GetValue() const wxOVERRIDE;
    virtual void SetValue(int val) wxOVERRIDE;

private:
    int m_value;

    wxDECLARE_DYNAMIC_CLASS(wxSpinButton);
};

#endif // __WX_WASM_SPINBUTT_H__
