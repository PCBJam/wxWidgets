/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/toolbar.cpp
// Purpose:     wxToolBar implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_TOOLBAR

#include "wx/toolbar.h"

// ----------------------------------------------------------------------------
// wxToolBarTool: data-only tool, all state lives in wxToolBarToolBase
// ----------------------------------------------------------------------------

class wxToolBarTool : public wxToolBarToolBase
{
public:
    wxToolBarTool(wxToolBar *tbar, int id, const wxString& label,
                  const wxBitmapBundle& bmpNormal,
                  const wxBitmapBundle& bmpDisabled,
                  wxItemKind kind, wxObject *clientData,
                  const wxString& shortHelp, const wxString& longHelp)
        : wxToolBarToolBase(tbar, id, label, bmpNormal, bmpDisabled, kind,
                            clientData, shortHelp, longHelp)
    {
        // TODO(dom-phase-2): keep a reference to the tool's DOM node here.
    }

    wxToolBarTool(wxToolBar *tbar, wxControl *control, const wxString& label)
        : wxToolBarToolBase(tbar, control, label)
    {
    }
};

// ----------------------------------------------------------------------------
// wxToolBar
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(wxToolBar, wxControl);

wxToolBar::wxToolBar()
{
}

wxToolBar::wxToolBar(wxWindow *parent, wxWindowID id,
                     const wxPoint& pos,
                     const wxSize& size, long style,
                     const wxString& name)
{
    Create(parent, id, pos, size, style, name);
}

bool wxToolBar::Create(wxWindow *parent, wxWindowID id,
                       const wxPoint& pos,
                       const wxSize& size, long style,
                       const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style,
                           wxDefaultValidator, name))
        return false;

    // make sure we have either wxTB_HORIZONTAL or wxTB_VERTICAL
    FixupStyle();

    // TODO(dom-phase-2): create a container DOM element for the tools.

    return true;
}

wxToolBarToolBase *wxToolBar::FindToolForPosition(wxCoord WXUNUSED(x),
                                                  wxCoord WXUNUSED(y)) const
{
    // TODO(dom-phase-2): hit-test the tools' DOM nodes.
    return NULL;
}

bool wxToolBar::DoInsertTool(size_t WXUNUSED(pos),
                             wxToolBarToolBase *WXUNUSED(tool))
{
    // TODO(dom-phase-2): create the tool's DOM node and insert it.

    InvalidateBestSize();

    return true;
}

bool wxToolBar::DoDeleteTool(size_t WXUNUSED(pos),
                             wxToolBarToolBase *WXUNUSED(tool))
{
    // TODO(dom-phase-2): remove the tool's DOM node.

    InvalidateBestSize();

    return true;
}

void wxToolBar::DoEnableTool(wxToolBarToolBase *WXUNUSED(tool),
                             bool WXUNUSED(enable))
{
    // the base class stores the state, nothing else to do without a DOM node
    // TODO(dom-phase-2): reflect the enabled state on the DOM node.
}

void wxToolBar::DoToggleTool(wxToolBarToolBase *WXUNUSED(tool),
                             bool WXUNUSED(toggle))
{
    // TODO(dom-phase-2): reflect the toggled state on the DOM node.
}

void wxToolBar::DoSetToggle(wxToolBarToolBase *WXUNUSED(tool),
                            bool WXUNUSED(toggle))
{
    // TODO(dom-phase-2): make the tool togglable (or not) in the DOM.
}

wxToolBarToolBase *wxToolBar::CreateTool(int toolid,
                                         const wxString& label,
                                         const wxBitmapBundle& bmpNormal,
                                         const wxBitmapBundle& bmpDisabled,
                                         wxItemKind kind,
                                         wxObject *clientData,
                                         const wxString& shortHelp,
                                         const wxString& longHelp)
{
    return new wxToolBarTool(this, toolid, label, bmpNormal, bmpDisabled,
                             kind, clientData, shortHelp, longHelp);
}

wxToolBarToolBase *wxToolBar::CreateTool(wxControl *control,
                                         const wxString& label)
{
    return new wxToolBarTool(this, control, label);
}

#endif // wxUSE_TOOLBAR
