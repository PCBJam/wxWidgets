/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/tooltip.cpp
// Purpose:     wxToolTip implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_TOOLTIPS

#include "wx/tooltip.h"

#include "wx/window.h"
#include "wx/timer.h"
#include "wx/utils.h"
#include "wx/wasm/private/dom.h"

wxIMPLEMENT_ABSTRACT_CLASS(wxToolTip, wxObject);

// ----------------------------------------------------------------------------
// Universal tooltip layer: one styled div (wx-dom.js #wx-tooltip) driven
// from the mouse pipeline's hover hit-test (wxApp::HandleMouseEvent), so
// island-painted widgets without DOM nodes get tooltips too. The text is
// RE-READ at fire time, so dynamic SetToolTip/UnsetToolTip (KiCad's
// grid-cell tooltips) needs no extra bookkeeping.
// ----------------------------------------------------------------------------

namespace
{

const int wxDOM_TOOLTIP_DELAY_MS = 600;

wxWindow *gs_hoverWindow = NULL;

// MSW-style inheritance: a window without its own tooltip shows the
// first non-TLW ancestor's one (KiCad's row panels rely on this).
wxWindow *FindTooltipWindow(wxWindow *win)
{
    for ( ; win && !win->IsTopLevel(); win = win->GetParent() )
    {
        if ( win->GetToolTip() && !win->GetToolTipText().empty() )
            return win;
    }

    return NULL;
}

class wxWasmTooltipTimer : public wxTimer
{
public:
    virtual void Notify() wxOVERRIDE
    {
        wxWindow *win = FindTooltipWindow(gs_hoverWindow);
        if ( !win )
            return;

        const wxString text = win->GetToolTipText();
        if ( text.empty() )
            return;

        const wxPoint pos = wxGetMousePosition();
        wxDomTooltipShow(text, pos.x, pos.y);
    }
};

wxWasmTooltipTimer *gs_tooltipTimer = NULL;

} // anonymous namespace

// Called from wxApp::HandleMouseEvent whenever the hovered window changes.
void wxWasmTooltipOnHoverChange(wxWindow *win)
{
    gs_hoverWindow = win;

    if ( !gs_tooltipTimer )
        gs_tooltipTimer = new wxWasmTooltipTimer;

    gs_tooltipTimer->Stop();
    wxDomTooltipHide();

    if ( FindTooltipWindow(win) )
        gs_tooltipTimer->StartOnce(wxDOM_TOOLTIP_DELAY_MS);
}

// ----------------------------------------------------------------------------
// wxToolTip
// ----------------------------------------------------------------------------

wxToolTip::wxToolTip(const wxString& tip)
    : m_text(tip),
      m_window(NULL)
{
}

wxToolTip::~wxToolTip()
{
}

void wxToolTip::SetTip(const wxString& tip)
{
    m_text = tip;
    Push();
}

void wxToolTip::SetWindow(wxWindow *win)
{
    m_window = win;
    Push();
}

void wxToolTip::Push()
{
    // The tooltip itself is rendered by the hover-driven layer above; the
    // element only carries the text for accessibility. No title attribute
    // (it would show a second, browser-styled tooltip).
    if (m_window && m_window->WasmGetDomId())
        wxDomSetAriaLabel(m_window->WasmGetDomId(), m_text);
}

#endif // wxUSE_TOOLTIPS
