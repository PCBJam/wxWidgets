/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/stattext.h
// Purpose:     wxStaticText class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_STATTEXT_H__
#define __WX_WASM_STATTEXT_H__

class WXDLLIMPEXP_CORE wxStaticText : public wxStaticTextBase
{
public:
    wxStaticText();
    wxStaticText(wxWindow *parent,
                 wxWindowID id,
                 const wxString& label,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 long style = 0,
                 const wxString& name = wxASCII_STR(wxStaticTextNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxStaticTextNameStr));

    virtual void SetLabel(const wxString& label) wxOVERRIDE;

protected:
    virtual wxString WXGetVisibleLabel() const wxOVERRIDE;
    virtual void WXSetVisibleLabel(const wxString& str) wxOVERRIDE;

private:
    // the label currently shown (possibly ellipsized)
    wxString m_visibleLabel;

    wxDECLARE_DYNAMIC_CLASS(wxStaticText);
};

#endif // __WX_WASM_STATTEXT_H__
