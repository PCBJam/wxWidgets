/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/notebook.h
// Purpose:     wxNotebook for the WASM DOM port: a native DOM tab strip
//              (real <button role=tab> elements) above the page area.
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_NOTEBOOK_H__
#define __WX_WASM_NOTEBOOK_H__

#include "wx/arrstr.h"
#include "wx/vector.h"

class WXDLLIMPEXP_CORE wxNotebook : public wxNotebookBase
{
public:
    wxNotebook();
    wxNotebook(wxWindow *parent,
               wxWindowID id,
               const wxPoint& pos = wxDefaultPosition,
               const wxSize& size = wxDefaultSize,
               long style = 0,
               const wxString& name = wxASCII_STR(wxNotebookNameStr));

    bool Create(wxWindow *parent,
                wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = 0,
                const wxString& name = wxASCII_STR(wxNotebookNameStr));

    // wxBookCtrlBase pure virtuals
    virtual bool SetPageText(size_t n, const wxString& strText) wxOVERRIDE;
    virtual wxString GetPageText(size_t n) const wxOVERRIDE;
    virtual int GetPageImage(size_t n) const wxOVERRIDE;
    virtual bool SetPageImage(size_t n, int imageId) wxOVERRIDE;
    virtual bool InsertPage(size_t n,
                            wxWindow *page,
                            const wxString& text,
                            bool bSelect = false,
                            int imageId = NO_IMAGE) wxOVERRIDE;
    virtual int SetSelection(size_t n) wxOVERRIDE
        { return DoSetSelection(n, SetSelection_SendEvent); }
    virtual int ChangeSelection(size_t n) wxOVERRIDE
        { return DoSetSelection(n); }
    virtual bool DeleteAllPages() wxOVERRIDE;

    // wxNotebookBase additions (no-ops: the DOM strip has no fixed-width
    // tabs or padding knobs)
    virtual void SetPadding(const wxSize& WXUNUSED(padding)) wxOVERRIDE { }
    virtual void SetTabSize(const wxSize& WXUNUSED(sz)) wxOVERRIDE { }

    virtual wxSize CalcSizeFromPage(const wxSize& sizePage) const wxOVERRIDE;

    // the page area lies below the tab strip
    virtual wxPoint GetClientAreaOrigin() const wxOVERRIDE;

    virtual void OnDomEvent(wxDomEventKind kind) wxOVERRIDE;

protected:
    virtual wxWindow *DoRemovePage(size_t page) wxOVERRIDE;

    // DoSetSelection() plumbing
    virtual void UpdateSelectedPage(size_t newsel) wxOVERRIDE;
    virtual wxBookCtrlEvent* CreatePageChangingEvent() const wxOVERRIDE;
    virtual void MakeChangedEvent(wxBookCtrlEvent& event) wxOVERRIDE;

    virtual void DoGetClientSize(int *width, int *height) const wxOVERRIDE;
    virtual void DoSetClientSize(int width, int height) wxOVERRIDE;

    // base DoSize() early-returns without m_bookctrl; size pages ourselves
    virtual void DoSize() wxOVERRIDE;

private:
    void Init();

    int StripHeight() const;

    // push [{label, selected}] to the JS tab strip (wholesale rebuild)
    void WasmRebuildTabs();

    // re-assert the selected page's size/layout after a page change (restores
    // a scrolled child collapsed by a PAGE_CHANGED handler's Fit())
    void WasmRelayoutSelectedPage();

    wxArrayString m_titles;
    wxVector<int> m_images;
    mutable int m_stripHeight; // measured lazily; -1 = unknown

    wxDECLARE_DYNAMIC_CLASS(wxNotebook);
};

#endif // __WX_WASM_NOTEBOOK_H__
