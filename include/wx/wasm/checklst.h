/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/checklst.h
// Purpose:     wxCheckListBox class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_CHECKLST_H__
#define __WX_WASM_CHECKLST_H__

class WXDLLIMPEXP_CORE wxCheckListBox : public wxCheckListBoxBase
{
public:
    wxCheckListBox();
    wxCheckListBox(wxWindow *parent, wxWindowID id,
                   const wxPoint& pos = wxDefaultPosition,
                   const wxSize& size = wxDefaultSize,
                   int nStrings = 0,
                   const wxString *choices = NULL,
                   long style = 0,
                   const wxValidator& validator = wxDefaultValidator,
                   const wxString& name = wxASCII_STR(wxListBoxNameStr));
    wxCheckListBox(wxWindow *parent, wxWindowID id,
                   const wxPoint& pos,
                   const wxSize& size,
                   const wxArrayString& choices,
                   long style = 0,
                   const wxValidator& validator = wxDefaultValidator,
                   const wxString& name = wxASCII_STR(wxListBoxNameStr));

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

    virtual bool IsChecked(unsigned int item) const wxOVERRIDE;
    virtual void Check(unsigned int item, bool check = true) wxOVERRIDE;

protected:
    // keep the checked-state cache in sync with the item cache
    virtual int DoInsertOneItem(const wxString& item, unsigned int pos) wxOVERRIDE;
    virtual void DoDeleteOneItem(unsigned int pos) wxOVERRIDE;
    virtual void DoClear() wxOVERRIDE;

private:
    // Cached checked state until the control becomes a real DOM element.
    // Always kept the same size as the item cache.
    wxArrayInt m_itemsChecked;

    wxDECLARE_DYNAMIC_CLASS(wxCheckListBox);
};

#endif // __WX_WASM_CHECKLST_H__
