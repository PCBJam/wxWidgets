/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/toolbar.h
// Purpose:     wxToolBar class declaration for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#ifndef __WX_WASM_TOOLBAR_H__
#define __WX_WASM_TOOLBAR_H__

class WXDLLIMPEXP_CORE wxToolBar : public wxToolBarBase
{
public:
    wxToolBar();
    wxToolBar(wxWindow *parent, wxWindowID id,
              const wxPoint& pos = wxDefaultPosition,
              const wxSize& size = wxDefaultSize,
              long style = wxTB_DEFAULT_STYLE,
              const wxString& name = wxASCII_STR(wxToolBarNameStr));

    bool Create(wxWindow *parent, wxWindowID id,
                const wxPoint& pos = wxDefaultPosition,
                const wxSize& size = wxDefaultSize,
                long style = wxTB_DEFAULT_STYLE,
                const wxString& name = wxASCII_STR(wxToolBarNameStr));

    // pushes the tools to the DOM toolbar node
    virtual bool Realize() wxOVERRIDE;

    // wxEVT_TOOL dispatch for clicks on the DOM tool buttons
    virtual void OnDomEvent(wxDomEventKind kind) wxOVERRIDE;

    virtual wxToolBarToolBase *FindToolForPosition(wxCoord x,
                                                   wxCoord y) const wxOVERRIDE;

    virtual wxToolBarToolBase *CreateTool(int toolid,
                                          const wxString& label,
                                          const wxBitmapBundle& bmpNormal,
                                          const wxBitmapBundle& bmpDisabled = wxNullBitmap,
                                          wxItemKind kind = wxITEM_NORMAL,
                                          wxObject *clientData = NULL,
                                          const wxString& shortHelp = wxEmptyString,
                                          const wxString& longHelp = wxEmptyString) wxOVERRIDE;

    virtual wxToolBarToolBase *CreateTool(wxControl *control,
                                          const wxString& label) wxOVERRIDE;

protected:
    virtual bool DoInsertTool(size_t pos, wxToolBarToolBase *tool) wxOVERRIDE;
    virtual bool DoDeleteTool(size_t pos, wxToolBarToolBase *tool) wxOVERRIDE;

    virtual void DoEnableTool(wxToolBarToolBase *tool, bool enable) wxOVERRIDE;
    virtual void DoToggleTool(wxToolBarToolBase *tool, bool toggle) wxOVERRIDE;
    virtual void DoSetToggle(wxToolBarToolBase *tool, bool toggle) wxOVERRIDE;

    // intrinsic size of the DOM toolbar, applied by Realize() and stretched
    // by wxFrame::PositionToolBar()
    virtual wxSize DoGetBestSize() const wxOVERRIDE;

private:
    // Re-serializes m_tools to JSON and pushes them to the DOM toolbar
    // node; no-op until Create() made the node.
    void WasmRebuildTools();

    wxDECLARE_DYNAMIC_CLASS(wxToolBar);
};

#endif // __WX_WASM_TOOLBAR_H__
