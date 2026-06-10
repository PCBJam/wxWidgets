/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/listbox.h
// Purpose:     wxListBox class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_LISTBOX_H__
#define __WX_WASM_LISTBOX_H__

#include "wx/dynarray.h"

class WXDLLIMPEXP_CORE wxListBox : public wxListBoxBase
{
public:
    wxListBox();
    wxListBox(wxWindow *parent, wxWindowID id,
              const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize,
              int n = 0, const wxString choices[] = NULL,
              long style = 0,
              const wxValidator& validator = wxDefaultValidator,
              const wxString& name = wxASCII_STR(wxListBoxNameStr));

    wxListBox(wxWindow *parent, wxWindowID id,
              const wxPoint& pos,
              const wxSize& size,
              const wxArrayString& choices,
              long style = 0,
              const wxValidator& validator = wxDefaultValidator,
              const wxString& name = wxASCII_STR(wxListBoxNameStr));

    virtual ~wxListBox();

    bool Create(wxWindow *parent, wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                int n = 0, const wxString choices[] = NULL,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxListBoxNameStr));
    bool Create(wxWindow *parent, wxWindowID id,
                const wxPoint& pos,
                const wxSize& size,
                const wxArrayString& choices,
                long style = 0,
                const wxValidator& validator = wxDefaultValidator,
                const wxString& name = wxASCII_STR(wxListBoxNameStr));

    virtual bool IsSelected(int n) const wxOVERRIDE;
    virtual int GetSelections(wxArrayInt& aSelections) const wxOVERRIDE;

    virtual unsigned int GetCount() const wxOVERRIDE;
    virtual wxString GetString(unsigned int n) const wxOVERRIDE;
    virtual void SetString(unsigned int n, const wxString& s) wxOVERRIDE;

    virtual int GetSelection() const wxOVERRIDE;

    // wxEVT_LISTBOX from the real <select multiple>'s change
    virtual void OnDomEvent(wxDomEventKind kind) wxOVERRIDE;

protected:
    virtual void DoSetFirstItem(int n) wxOVERRIDE;

    virtual void DoSetSelection(int n, bool select) wxOVERRIDE;

    virtual int DoInsertItems(const wxArrayStringsAdapter& items,
                              unsigned int pos,
                              void **clientData,
                              wxClientDataType type) wxOVERRIDE;
    virtual int DoInsertOneItem(const wxString& item, unsigned int pos) wxOVERRIDE;

    virtual void DoSetItemClientData(unsigned int n, void *clientData) wxOVERRIDE;
    virtual void *DoGetItemClientData(unsigned int n) const wxOVERRIDE;

    virtual void DoClear() wxOVERRIDE;
    virtual void DoDeleteOneItem(unsigned int pos) wxOVERRIDE;

    // Cached items, per-item client data and selection state until the
    // control becomes a real DOM element. The arrays are always kept the
    // same size.
    wxArrayString  m_items;
    wxArrayPtrVoid m_itemsClientData;
    wxArrayInt     m_itemsSelected;

private:
    // Push the whole cached item list (+ selection) to the DOM <select>.
    void WasmSyncItems();
    void WasmSyncSelection();

    wxDECLARE_DYNAMIC_CLASS(wxListBox);
};

#endif // __WX_WASM_LISTBOX_H__
