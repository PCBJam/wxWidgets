/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/toolbar.cpp
// Purpose:     wxToolBar implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_TOOLBAR

#include "wx/toolbar.h"

#include "wx/wasm/private/dom.h"

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
        // no per-tool DOM node: the whole strip is pushed as JSON by
        // wxToolBar::WasmRebuildTools()
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

    WasmCreateDomNode("toolbar");

    return true;
}

bool wxToolBar::Realize()
{
    if (!wxToolBarBase::Realize())
        return false;

    WasmRebuildTools();

    // adopt the intrinsic size; wxFrame::PositionToolBar() then stretches
    // the bar along the frame (as in src/univ/toolbar.cpp)
    SetInitialSize(wxDefaultSize);

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
    // the DOM tools are pushed wholesale by Realize(), nothing to do here

    InvalidateBestSize();

    return true;
}

bool wxToolBar::DoDeleteTool(size_t WXUNUSED(pos),
                             wxToolBarToolBase *WXUNUSED(tool))
{
    // The tool is still in m_tools at this point, so the DOM refresh can't
    // happen here; like the other ports we rely on the app calling
    // Realize() after batch changes.

    InvalidateBestSize();

    return true;
}

void wxToolBar::DoEnableTool(wxToolBarToolBase *WXUNUSED(tool),
                             bool WXUNUSED(enable))
{
    // the base class already updated the tool's flag, just push the change
    WasmRebuildTools();
}

void wxToolBar::DoToggleTool(wxToolBarToolBase *WXUNUSED(tool),
                             bool WXUNUSED(toggle))
{
    // the base class already updated the tool's flag, just push the change
    WasmRebuildTools();
}

void wxToolBar::DoSetToggle(wxToolBarToolBase *WXUNUSED(tool),
                            bool WXUNUSED(toggle))
{
    // the tool's kind changed (button <-> toggle), just push the change
    WasmRebuildTools();
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

void wxToolBar::WasmRebuildTools()
{
    if (!WasmGetDomId())
        return;

    wxString json(wxT("["));

    bool first = true;
    for (wxToolBarToolsList::compatibility_iterator node = m_tools.GetFirst();
         node;
         node = node->GetNext())
    {
        wxToolBarToolBase *tool = node->GetData();

        // control tools render through their own wxControl DOM node
        if (tool->IsControl())
            continue;

        if (!first)
            json += wxT(",");
        first = false;

        const char *kind;
        if (tool->IsSeparator())
            kind = "separator";
        else if (tool->CanBeToggled())
            kind = "toggle";
        else
            kind = "button";

        wxString img;
        int imgW = 0;
        int imgH = 0;
        const wxBitmap bmp = tool->GetNormalBitmap();
        if (bmp.IsOk())
        {
            // empty string if the encoding fails; the DOM tool button then
            // falls back to showing the label
            img = wxDomBitmapToDataURL(bmp);
            if (!img.empty())
            {
                imgW = bmp.GetWidth();
                imgH = bmp.GetHeight();
            }
        }

        json += wxString::Format(
            wxT("{\"id\":%d,\"label\":\"%s\",\"tooltip\":\"%s\",")
            wxT("\"kind\":\"%s\",\"toggled\":%s,\"enabled\":%s,")
            wxT("\"img\":\"%s\",\"imgW\":%d,\"imgH\":%d}"),
            tool->GetId(),
            wxDomJsonEscape(tool->GetLabel()),
            wxDomJsonEscape(tool->GetShortHelp()),
            kind,
            tool->IsToggled() ? "true" : "false",
            tool->IsEnabled() ? "true" : "false",
            img,
            imgW, imgH);
    }

    json += wxT("]");

    wxDomToolbarSetTools(WasmGetDomId(), json);
    InvalidateBestSize();
}

void wxToolBar::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_TOOL)
    {
        const int id = wxDomGetLastCommandId(WasmGetDomId());

        wxToolBarToolBase *tool = FindById(id);
        if (!tool)
            return;

        // toggle first, then fire wxEVT_TOOL, like src/univ/toolbar.cpp
        const bool canToggle = tool->CanBeToggled();
        if (canToggle)
        {
            if (tool->GetKind() == wxITEM_RADIO)
            {
                UnToggleRadioGroup(tool);
                tool->Toggle(true);
            }
            else
            {
                tool->Toggle();
            }

            WasmRebuildTools();
        }

        if (!OnLeftClick(id, canToggle ? tool->IsToggled() : false) &&
                canToggle)
        {
            // the handler vetoed the click: restore the previous state
            tool->Toggle();
            WasmRebuildTools();
        }

        return;
    }

    wxControl::OnDomEvent(kind);
}

wxSize wxToolBar::DoGetBestSize() const
{
    // DOM-backed bars report their intrinsic (content-driven) size,
    // measured on the live element, like wxControl::DoGetBestSize().
    if (WasmGetDomId())
    {
        int w = 0;
        int h = 0;
        wxDomGetIntrinsicSize(WasmGetDomId(), &w, &h);
        if (w > 0 && h > 0)
            return wxSize(w, h);
    }

    // stub bars (no DOM node yet): a plausible toolbar height so
    // wxFrame::PositionToolBar() keeps the layout sane
    return wxSize(100, 28);
}

#endif // wxUSE_TOOLBAR
