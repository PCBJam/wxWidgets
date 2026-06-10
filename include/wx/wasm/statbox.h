/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/statbox.h
// Purpose:     wxStaticBox class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_STATBOX_H__
#define __WX_WASM_STATBOX_H__

class WXDLLIMPEXP_CORE wxStaticBox : public wxStaticBoxBase
{
public:
    wxStaticBox();
    wxStaticBox(wxWindow *parent, wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxStaticBoxNameStr));

    bool Create(wxWindow *parent, wxWindowID id,
                const wxString& label,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxStaticBoxNameStr));

    // writes the <fieldset>'s <legend>
    virtual void SetLabel(const wxString& label) wxOVERRIDE;

private:
    wxDECLARE_DYNAMIC_CLASS(wxStaticBox);
};

#endif // __WX_WASM_STATBOX_H__
