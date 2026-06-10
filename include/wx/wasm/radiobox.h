/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/radiobox.h
// Purpose:     wxRadioBox class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_RADIOBOX_H__
#define __WX_WASM_RADIOBOX_H__

#include "wx/dynarray.h"

class WXDLLIMPEXP_CORE wxRadioBox : public wxControl, public wxRadioBoxBase
{
public:
    wxRadioBox();

    wxRadioBox(wxWindow *parent,
               wxWindowID id,
               const wxString& title,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               int n = 0, const wxString choices[] = NULL,
               int majorDim = 0,
               long style = wxRA_SPECIFY_COLS,
               const wxValidator& val = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxRadioBoxNameStr));

    wxRadioBox(wxWindow *parent,
               wxWindowID id,
               const wxString& title,
               const wxPoint& pos,
               const wxSize& size,
               const wxArrayString& choices,
               int majorDim = 0,
               long style = wxRA_SPECIFY_COLS,
               const wxValidator& val = wxDefaultValidator,
               const wxString& name = wxASCII_STR(wxRadioBoxNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                int n = 0, const wxString choices[] = NULL,
                int majorDim = 0,
                long style = wxRA_SPECIFY_COLS,
                const wxValidator& val = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxRadioBoxNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxString& title,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                int majorDim = 0,
                long style = wxRA_SPECIFY_COLS,
                const wxValidator& val = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxRadioBoxNameStr));

    using wxWindowBase::Show;
    using wxWindowBase::Enable;
    using wxRadioBoxBase::GetDefaultBorder;

    virtual bool Enable(unsigned int n, bool enable = true) wxOVERRIDE;
    virtual bool Show(unsigned int n, bool show = true) wxOVERRIDE;
    virtual bool IsItemEnabled(unsigned int n) const wxOVERRIDE;
    virtual bool IsItemShown(unsigned int n) const wxOVERRIDE;

    virtual unsigned int GetCount() const wxOVERRIDE;
    virtual wxString GetString(unsigned int n) const wxOVERRIDE;
    virtual void SetString(unsigned int n, const wxString& s) wxOVERRIDE;

    virtual void SetSelection(int n) wxOVERRIDE;
    virtual int GetSelection() const wxOVERRIDE;

    // wxEVT_RADIOBOX from the fieldset's radio rows' change
    virtual void OnDomEvent(wxDomEventKind kind) wxOVERRIDE;

private:
    // Cached items and per-item state until the control becomes a real DOM
    // element.
    wxArrayString m_items;
    wxArrayInt    m_itemsEnabled;
    wxArrayInt    m_itemsShown;
    int           m_selection;

    wxDECLARE_DYNAMIC_CLASS(wxRadioBox);
};

#endif // __WX_WASM_RADIOBOX_H__
