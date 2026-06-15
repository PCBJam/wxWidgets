/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/choice.h
// Purpose:     wxChoice class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_CHOICE_H__
#define __WX_WASM_CHOICE_H__

#include "wx/dynarray.h"

class WXDLLIMPEXP_CORE wxChoice : public wxChoiceBase
{
public:
    wxChoice();

    wxChoice(wxWindow *parent, wxWindowID id,
             const wxPoint& pos = wxDefaultPosition,
             const wxSize& size = wxDefaultSize,
             int n = 0, const wxString choices[] = NULL,
             long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxChoiceNameStr));

    wxChoice(wxWindow *parent, wxWindowID id,
             const wxPoint& pos,
             const wxSize& size,
             const wxArrayString& choices,
             long style = 0,
             const wxValidator& validator = wxDefaultValidator,
             const wxString& name = wxASCII_STR(wxChoiceNameStr));

    virtual ~wxChoice();

    bool Create(wxWindow *parent, wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                int n = 0, const wxString choices[] = NULL,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxChoiceNameStr));

    bool Create(wxWindow *parent, wxWindowID id,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxChoiceNameStr));

    virtual unsigned int GetCount() const wxOVERRIDE;
    virtual wxString GetString(unsigned int n) const wxOVERRIDE;
    virtual void SetString(unsigned int n, const wxString& s) wxOVERRIDE;

    virtual void SetSelection(int n) wxOVERRIDE;
    virtual int GetSelection() const wxOVERRIDE;

    // wxEVT_CHOICE from the real <select>'s change
    virtual void OnDomEvent(wxDomEventKind kind) wxOVERRIDE;

protected:
    // Floor the DOM-measured best size with a font-derived control height. A
    // <select>'s block-size only resolves once it has been laid out, but the
    // best size is often queried earlier (e.g. wxAuiToolBar freezes a
    // control's min size at AddControl time, and panel sizers query it during
    // construction). The measured height then comes back ~0, which the layout
    // pins — collapsing toolbar dropdowns and overlapping stacked combos.
    virtual wxSize DoGetBestSize() const wxOVERRIDE;

    // DOM node type built by Create(); wxComboBox overrides to get an
    // editable <input>+<datalist> instead of the <select>.
    virtual const char *WasmDomNodeType() const { return "choice"; }

    virtual int DoInsertItems(const wxArrayStringsAdapter& items,
                              unsigned int pos,
                              void **clientData,
                              wxClientDataType type) wxOVERRIDE;
    virtual int DoInsertOneItem(const wxString& item, unsigned int pos) wxOVERRIDE;

    virtual void DoSetItemClientData(unsigned int n, void *clientData) wxOVERRIDE;
    virtual void *DoGetItemClientData(unsigned int n) const wxOVERRIDE;

    virtual void DoClear() wxOVERRIDE;
    virtual void DoDeleteOneItem(unsigned int pos) wxOVERRIDE;

    // Cached items, per-item client data and selection until the control
    // becomes a real DOM element. The arrays are always kept the same size.
    wxArrayString  m_items;
    wxArrayPtrVoid m_itemsClientData;
    int            m_selection;

private:
    // Push the whole cached item list + selection to the DOM <select>.
    void WasmSyncItems();

    wxDECLARE_DYNAMIC_CLASS(wxChoice);
};

#endif // __WX_WASM_CHOICE_H__
