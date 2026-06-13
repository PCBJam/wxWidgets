/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/textctrl.h
// Purpose:     wxTextCtrl class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_TEXTCTRL_H__
#define __WX_WASM_TEXTCTRL_H__

class WXDLLIMPEXP_CORE wxTextCtrl : public wxTextCtrlBase
{
public:
    wxTextCtrl();
    wxTextCtrl(wxWindow *parent, wxWindowID id,
               const wxString& value = wxEmptyString,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize, long style = 0,
               const wxValidator& validator = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxTextCtrlNameStr));

    bool Create(wxWindow *parent, wxWindowID id,
                const wxString& value = wxEmptyString,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize, long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxTextCtrlNameStr));

    // implement wxTextAreaBase pure virtuals
    virtual int GetLineLength(long lineNo) const wxOVERRIDE;
    virtual wxString GetLineText(long lineNo) const wxOVERRIDE;
    virtual int GetNumberOfLines() const wxOVERRIDE;

    virtual bool IsModified() const wxOVERRIDE;
    virtual void MarkDirty() wxOVERRIDE;
    virtual void DiscardEdits() wxOVERRIDE;

    virtual long XYToPosition(long x, long y) const wxOVERRIDE;
    virtual bool PositionToXY(long pos, long *x, long *y) const wxOVERRIDE;

    virtual void ShowPosition(long pos) wxOVERRIDE;

    // editing the control marks it as dirty
    virtual void WriteText(const wxString& text) wxOVERRIDE;

    // typed text / enter / focus from the real <input>/<textarea>
    virtual void OnDomEvent(wxDomEventKind kind) wxOVERRIDE;

protected:
    // setting the value programmatically resets the modified flag
    // and pushes the new value into the DOM element
    virtual void DoSetValue(const wxString& value, int flags = 0) wxOVERRIDE;

private:
    bool m_modified;

    wxDECLARE_DYNAMIC_CLASS(wxTextCtrl);
};

#endif // __WX_WASM_TEXTCTRL_H__
