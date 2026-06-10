/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/scrolbar.h
// Purpose:     wxScrollBar class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_SCROLBAR_H__
#define __WX_WASM_SCROLBAR_H__

class WXDLLIMPEXP_CORE wxScrollBar : public wxScrollBarBase
{
public:
    wxScrollBar();
    wxScrollBar(wxWindow *parent, wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSB_HORIZONTAL,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxScrollBarNameStr));

    bool Create(wxWindow *parent, wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxSB_HORIZONTAL,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxScrollBarNameStr));

    virtual int GetThumbPosition() const wxOVERRIDE;
    virtual int GetThumbSize() const wxOVERRIDE;
    virtual int GetPageSize() const wxOVERRIDE;
    virtual int GetRange() const wxOVERRIDE;

    virtual void SetThumbPosition(int viewStart) wxOVERRIDE;
    virtual void SetScrollbar(int position, int thumbSize,
                              int range, int pageSize,
                              bool refresh = true) wxOVERRIDE;

protected:
    virtual wxSize DoGetBestSize() const wxOVERRIDE;

private:
    int m_thumbPosition;
    int m_thumbSize;
    int m_range;
    int m_pageSize;

    wxDECLARE_DYNAMIC_CLASS(wxScrollBar);
};

#endif // __WX_WASM_SCROLBAR_H__
