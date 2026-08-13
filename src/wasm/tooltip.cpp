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

// Last arm key. The exact hovered window is part of the key: a delayed callback
// must never borrow a later hover merely because both windows inherit the same
// tooltip from one ancestor.
wxWindow *gs_lastHoverWindow = NULL;
wxWindow *gs_lastEffWin = NULL;
wxString gs_lastText;
bool gs_tooltipVisible = false;

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
    wxWasmTooltipTimer()
        : m_hoverWindow(NULL),
          m_tooltipWindow(NULL)
    {
    }

    bool Arm(wxWindow *hoverWindow, wxWindow *tooltipWindow)
    {
        Cancel();

        m_hoverWindow = hoverWindow;
        m_tooltipWindow = tooltipWindow;

        // A derived wxTimer normally owns itself. Make this delivery belong to
        // the exact hovered window instead. wxWasmTimerImpl then captures this
        // top-level family's scope and the active modal lease generation at arm
        // time; it never grants modal admission to unrelated, unowned timers.
        SetOwner(hoverWindow, GetId());

        if ( StartOnce(wxDOM_TOOLTIP_DELAY_MS) )
            return true;

        ClearTargets();
        return false;
    }

    void Cancel()
    {
        Stop();
        ClearTargets();
    }

    bool ForgetWindow(wxWindow *win)
    {
        if ( win != m_hoverWindow && win != m_tooltipWindow )
            return false;

        Cancel();
        return true;
    }

    virtual void Notify() wxOVERRIDE
    {
        // wxWasmTimerImpl has already verified the captured top-level scope and
        // modal lease generation. Verify the finer tooltip provenance before
        // dereferencing either arm-time target: this callback is for one exact
        // hover, not whichever window happens to be under the pointer now.
        wxWindow * const hoverWindow = m_hoverWindow;
        wxWindow * const tooltipWindow = m_tooltipWindow;

        if ( !hoverWindow || !tooltipWindow
             || gs_hoverWindow != hoverWindow
             || FindTooltipWindow(hoverWindow) != tooltipWindow )
        {
            ClearTargets();
            return;
        }

        const wxString text = tooltipWindow->GetToolTipText();
        if ( text.empty() )
        {
            ClearTargets();
            return;
        }

        ClearTargets();
        const wxPoint pos = wxGetMousePosition();
        wxDomTooltipShow(text, pos.x, pos.y);
        gs_tooltipVisible = true;
    }

private:
    void ClearTargets()
    {
        m_hoverWindow = NULL;
        m_tooltipWindow = NULL;

        // Do not retain a raw wxWindow pointer after the pending delivery has
        // ended. Notify() is overridden, so restoring self-ownership is safe.
        SetOwner(this, GetId());
    }

    wxWindow *m_hoverWindow;
    wxWindow *m_tooltipWindow;
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
    // the key includes both the exact hover and (effective window, text).
    wxWindow *eff = FindTooltipWindow(win);
    const wxString text = eff ? eff->GetToolTipText() : wxString();

    // No relevant change: leave any pending timer / shown tooltip untouched so a
    // jitter over the same tool doesn't endlessly restart the show delay. A
    // lease-stale delivery is discarded by wxWasmTimerImpl, which also makes the
    // timer non-running; the next motion can therefore arm it for the new lease.
    const bool sameTarget = win == gs_lastHoverWindow
                            && eff == gs_lastEffWin
                            && text == gs_lastText;
    if ( sameTarget
         && (!eff || gs_tooltipVisible
             || (gs_tooltipTimer && gs_tooltipTimer->IsRunning())) )
        return;

    gs_lastHoverWindow = win;
    gs_lastEffWin = eff;
    gs_lastText = text;

    if ( !gs_tooltipTimer )
        gs_tooltipTimer = new wxWasmTooltipTimer;

    gs_tooltipTimer->Cancel();
    wxDomTooltipHide();
    gs_tooltipVisible = false;

    if ( eff && !text.empty() )
        gs_tooltipTimer->Arm(win, eff);
}

// Called from ~wxWindowWasm: a window being destroyed must not remain the hover
// target, or the pending tooltip timer would dereference a freed window.
void wxWasmTooltipForgetWindow(wxWindow *win)
{
    bool referenced = false;

    // Drop cached pointers so a pending timer can't read a freed window and a
    // stale arm key can't suppress a later genuine update.
    if ( gs_lastHoverWindow == win )
    {
        gs_lastHoverWindow = NULL;
        referenced = true;
    }

    if ( gs_lastEffWin == win )
    {
        gs_lastEffWin = NULL;
        gs_lastText.clear();
        referenced = true;
    }

    if ( gs_hoverWindow == win )
    {
        gs_hoverWindow = NULL;
        referenced = true;
    }

    // The hovered child and the ancestor supplying an inherited tooltip are
    // independent lifetime edges. Either one's destruction cancels the exact
    // arm before wxWasmTimerImpl can inspect its wxWindow owner.
    if ( gs_tooltipTimer && gs_tooltipTimer->ForgetWindow(win) )
        referenced = true;

    if ( referenced )
    {
        gs_tooltipVisible = false;
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
