/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/radiobut.h
// Purpose:     wxRadioButton class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_RADIOBUT_H__
#define __WX_WASM_RADIOBUT_H__

class WXDLLIMPEXP_CORE wxRadioButton : public wxRadioButtonBase
{
public:
    wxRadioButton();
    wxRadioButton(wxWindow *parent,
                  wxWindowID id,
                  const wxString& label,
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize,
                  long style = 0,
                  const wxValidator& validator = wxDefaultValidator,
                  const wxString& name = wxASCII_STR(wxRadioButtonNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxRadioButtonNameStr));

    virtual void SetValue(bool value) wxOVERRIDE;
    virtual bool GetValue() const wxOVERRIDE;

    virtual void SetLabel(const wxString& label) wxOVERRIDE;

    // wxEVT_RADIOBUTTON from the real <input type="radio">'s change
    virtual void OnDomEvent(wxDomEventKind kind) wxOVERRIDE;

private:
    // Cached checked state; GetValue() prefers the live DOM state because
    // the browser un-checks group siblings without notifying them.
    bool m_value;

    wxDECLARE_DYNAMIC_CLASS(wxRadioButton);
};

#endif // __WX_WASM_RADIOBUT_H__
