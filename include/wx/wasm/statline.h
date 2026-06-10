/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/statline.h
// Purpose:     wxStaticLine class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_STATLINE_H__
#define __WX_WASM_STATLINE_H__

class WXDLLIMPEXP_CORE wxStaticLine : public wxStaticLineBase
{
public:
    wxStaticLine();
    wxStaticLine(wxWindow *parent,
                 wxWindowID id = wxID_ANY,
                 const wxPoint& pos = wxDefaultPosition,
                 const wxSize& size = wxDefaultSize,
                 long style = wxLI_HORIZONTAL,
                 const wxString& name = wxASCII_STR(wxStaticLineNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id = wxID_ANY,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxLI_HORIZONTAL,
                const wxString& name = wxASCII_STR(wxStaticLineNameStr));

private:
    wxDECLARE_DYNAMIC_CLASS(wxStaticLine);
};

#endif // __WX_WASM_STATLINE_H__
