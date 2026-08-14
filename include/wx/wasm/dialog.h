/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dialog.h
// Purpose:     wxDialog class declaration for the WASM DOM port.
//              Implemented by the shared src/wasm/dialog.cpp (JSPI modal
//              loop); this declaration mirrors wx/univ/dialog.h, which that
//              file was originally written against.
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_DIALOG_H__
#define __WX_WASM_DIALOG_H__

#include <functional>

extern WXDLLIMPEXP_DATA_CORE(const char) wxDialogNameStr[];
class WXDLLIMPEXP_FWD_CORE wxWindowDisabler;
class WXDLLIMPEXP_FWD_CORE wxEventLoop;

// Dialog boxes
class WXDLLIMPEXP_CORE wxDialog : public wxDialogBase
{
public:
    wxDialog() { Init(); }

    // Constructor with no modal flag - the new convention.
    wxDialog(wxWindow *parent, wxWindowID id,
             const wxString& title,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             long style = wxDEFAULT_DIALOG_STYLE,
             const wxString& name = wxASCII_STR(wxDialogNameStr))
    {
        Init();
        Create(parent, id, title, pos, size, style, name);
    }

    bool Create(wxWindow *parent, wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxDEFAULT_DIALOG_STYLE,
                const wxString& name = wxASCII_STR(wxDialogNameStr));

    virtual ~wxDialog();

    // is the dialog in modal state right now?
    virtual bool IsModal() const wxOVERRIDE;

    // For now, same as Show(true) but returns return code
    virtual int ShowModal() wxOVERRIDE;

    virtual void ShowModal(std::function<void (int)> callback) wxOVERRIDE;

    // may be called to terminate the dialog with the given return code
    virtual void EndModal(int retCode) wxOVERRIDE;

    // returns true if we're in a modal loop
    bool IsModalShowing() const;

    virtual bool Show(bool show = true) wxOVERRIDE;

    // implementation only from now on
    // -------------------------------

    // event handlers
    void OnCloseWindow(wxCloseEvent& event);
    void OnOK(wxCommandEvent& event);
    void OnApply(wxCommandEvent& event);
    void OnCancel(wxCommandEvent& event);

protected:
    // common part of all ctors
    void Init();

private:
    // while we are showing a modal dialog we disable the other windows using
    // this object
    wxWindowDisabler *m_windowDisabler;

    // modal dialog runs its own event loop
    wxEventLoop *m_eventLoop;

    std::function<void (int)> m_modalCallback;

    // is modal right now?
    bool m_isShowingModal;

    wxDECLARE_DYNAMIC_CLASS(wxDialog);
    wxDECLARE_EVENT_TABLE();
};

#endif // __WX_WASM_DIALOG_H__
