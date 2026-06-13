/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/scrolbar.cpp
// Purpose:     wxScrollBar implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_SCROLLBAR

#include "wx/scrolbar.h"

#ifndef WX_PRECOMP
    #include "wx/settings.h"
#endif

#include "wx/wasm/private/dom.h"

wxScrollBar::wxScrollBar() :
    m_thumbPosition(0),
    m_thumbSize(0),
    m_range(0),
    m_pageSize(0)
{
}

wxScrollBar::wxScrollBar(wxWindow *parent, wxWindowID id,
                         const wxPoint& pos,
                         const wxSize& size,
                         long style,
                         const wxValidator& validator,
                         const wxString& name) :
    m_thumbPosition(0),
    m_thumbSize(0),
    m_range(0),
    m_pageSize(0)
{
    Create(parent, id, pos, size, style, validator, name);
}

bool wxScrollBar::Create(wxWindow *parent, wxWindowID id,
                         const wxPoint& pos,
                         const wxSize& size,
                         long style,
                         const wxValidator& validator,
                         const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    // The DOM 'scrollbar' widget (track + draggable thumb) is the same one
    // wxWindow's built-in gutters use; orientation comes from the style.
    WasmCreateDomNode("scrollbar", IsVertical() ? "v" : "h");

    // Push the initial (possibly zero) metrics so the thumb lays out.
    if (WasmGetDomId())
        wxDomSetScrollbar(WasmGetDomId(), m_thumbPosition, m_thumbSize,
                          m_range, m_pageSize);

    return true;
}

int wxScrollBar::GetThumbPosition() const
{
    return m_thumbPosition;
}

int wxScrollBar::GetThumbSize() const
{
    return m_thumbSize;
}

int wxScrollBar::GetPageSize() const
{
    return m_pageSize;
}

int wxScrollBar::GetRange() const
{
    return m_range;
}

void wxScrollBar::SetThumbPosition(int viewStart)
{
    m_thumbPosition = viewStart;

    // The JS widget ignores this while the user is dragging, so a
    // programmatic update never fights an in-progress drag.
    if (WasmGetDomId())
        wxDomSetIntValue(WasmGetDomId(), viewStart);
}

void wxScrollBar::SetScrollbar(int position, int thumbSize,
                               int range, int pageSize,
                               bool WXUNUSED(refresh))
{
    m_thumbPosition = position;
    m_thumbSize = thumbSize;
    m_range = range;
    m_pageSize = pageSize;

    if (WasmGetDomId())
        wxDomSetScrollbar(WasmGetDomId(), position, thumbSize, range, pageSize);
}

void wxScrollBar::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_SCROLL)
    {
        const int domId = WasmGetDomId();
        m_thumbPosition = wxDomGetIntValue(domId);

        const int phase = wxDomGetScrollPhase(domId);
        const int orient = IsVertical() ? wxVERTICAL : wxHORIZONTAL;

        // Fire the wxScrollEvent family like the native ports: continuous
        // THUMBTRACK while dragging, THUMBRELEASE + CHANGED on release, and
        // CHANGED for a discrete page step (track click).
        wxEventType type;
        if (phase == 1)
            type = wxEVT_SCROLL_THUMBRELEASE;
        else if (phase == 2)
            type = wxEVT_SCROLL_CHANGED;
        else
            type = wxEVT_SCROLL_THUMBTRACK;

        wxScrollEvent event(type, GetId(), m_thumbPosition, orient);
        event.SetEventObject(this);
        HandleWindowEvent(event);

        if (phase == 1)
        {
            wxScrollEvent changed(wxEVT_SCROLL_CHANGED, GetId(),
                                  m_thumbPosition, orient);
            changed.SetEventObject(this);
            HandleWindowEvent(changed);
        }

        // Legacy aggregate command event (wxEVT_SCROLLBAR is a wxCommandEvent).
        wxCommandEvent cmd(wxEVT_SCROLLBAR, GetId());
        cmd.SetInt(m_thumbPosition);
        cmd.SetEventObject(this);
        HandleWindowEvent(cmd);
        return;
    }

    wxControl::OnDomEvent(kind);
}

wxSize wxScrollBar::DoGetBestSize() const
{
    // A scrollbar has no intrinsic content size; use the platform metric for
    // the cross-axis and a nominal length along the scrolling axis.
    const int m = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, this);
    const int sbWidth = m > 0 ? m : 17;

    return IsVertical() ? wxSize(sbWidth, 100) : wxSize(100, sbWidth);
}

#endif // wxUSE_SCROLLBAR
