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

// Last resolved (effective tooltip window, text). Hover updates that don't change
// either are ignored, so a jitter over the same tool doesn't restart the delay.
wxWindow *gs_lastEffWin = NULL;
wxString gs_lastText;

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

    // Resolve the effective tooltip (inherited from the first non-TLW ancestor)
    // and its current text. wxAuiToolBar and other per-item widgets update their
    // own tooltip text on wxEVT_MOTION without changing the hovered wxWindow, so
    // we key off (window, text) rather than the window identity alone.
    wxWindow *eff = FindTooltipWindow(win);
    const wxString text = eff ? eff->GetToolTipText() : wxString();

    // No relevant change: leave any pending timer / shown tooltip untouched so a
    // jitter over the same tool doesn't endlessly restart the show delay.
    if ( eff == gs_lastEffWin && text == gs_lastText )
        return;

    gs_lastEffWin = eff;
    gs_lastText = text;

    if ( !gs_tooltipTimer )
        gs_tooltipTimer = new wxWasmTooltipTimer;

    gs_tooltipTimer->Stop();
    wxDomTooltipHide();

    if ( eff && !text.empty() )
        gs_tooltipTimer->StartOnce(wxDOM_TOOLTIP_DELAY_MS);
}

// Called from ~wxWindowWasm: a window being destroyed must not remain the hover
// target, or the pending tooltip timer would dereference a freed window.
void wxWasmTooltipForgetWindow(wxWindow *win)
{
    // Drop cached pointers so a pending timer can't read a freed window and a
    // stale (window, text) can't suppress a later genuine update.
    if ( gs_lastEffWin == win )
    {
        gs_lastEffWin = NULL;
        gs_lastText.clear();
    }

    if ( gs_hoverWindow == win )
    {
        gs_hoverWindow = NULL;
        if ( gs_tooltipTimer )
            gs_tooltipTimer->Stop();
        wxDomTooltipHide();
    }
}

// Test/diagnostic hook (tests/apps/standalone/tooltip-lifetime): exposes the
// current hover target so a test can assert the pointer never outlives its
// window. Not used by any production code path.
wxWindow *wxWasmTooltipDebugHoverWindow()
{
    return gs_hoverWindow;
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
