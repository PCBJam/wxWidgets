/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/gauge.h
// Purpose:     wxGauge class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_GAUGE_H__
#define __WX_WASM_GAUGE_H__

class WXDLLIMPEXP_CORE wxGauge : public wxGaugeBase
{
public:
    wxGauge();
    wxGauge(wxWindow *parent,
            wxWindowID id,
            int range,
            const wxPoint& pos = wxDefaultPosition,
            const wxSize& size = wxDefaultSize,
            long style = wxGA_HORIZONTAL,
            const wxValidator& validator = wxDefaultValidator,
            const wxString& name = wxASCII_STR(wxGaugeNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                int range,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxGA_HORIZONTAL,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxGaugeNameStr));

    // push range/position to the <progress> element
    virtual void SetRange(int range) wxOVERRIDE;
    virtual void SetValue(int pos) wxOVERRIDE;

protected:
    virtual wxSize DoGetBestSize() const wxOVERRIDE;

private:
    wxDECLARE_DYNAMIC_CLASS(wxGauge);
};

#endif // __WX_WASM_GAUGE_H__
