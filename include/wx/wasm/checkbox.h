/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/checkbox.h
// Purpose:     wxCheckBox class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_CHECKBOX_H__
#define __WX_WASM_CHECKBOX_H__

class WXDLLIMPEXP_CORE wxCheckBox : public wxCheckBoxBase
{
public:
    wxCheckBox();
    wxCheckBox(wxWindow *parent, wxWindowID id, const wxString& label,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize, long style = 0,
               const wxValidator& validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxCheckBoxNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxCheckBoxNameStr));

    virtual void SetValue(bool value) wxOVERRIDE;
    virtual bool GetValue() const wxOVERRIDE;

    virtual void SetLabel(const wxString& label) wxOVERRIDE;

    // wxEVT_CHECKBOX from the real <input type="checkbox">'s change
    virtual void OnDomEvent(wxDomEventKind kind) wxOVERRIDE;

protected:
    virtual void DoSet3StateValue(wxCheckBoxState state) wxOVERRIDE;
    virtual wxCheckBoxState DoGet3StateValue() const wxOVERRIDE;

private:
    // Cached state, kept in sync with the DOM element's checked property.
    wxCheckBoxState m_state;

    wxDECLARE_DYNAMIC_CLASS(wxCheckBox);
};

#endif // __WX_WASM_CHECKBOX_H__
