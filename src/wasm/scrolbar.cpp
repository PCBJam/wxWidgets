/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/scrolbar.cpp
// Purpose:     wxScrollBar implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_SCROLLBAR

#include "wx/scrolbar.h"

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

    // TODO(dom-phase-2): create a real scrollbar DOM element and wire its
    // scroll events through wx_dom_event.

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
    // TODO(dom-phase-2): update the DOM element's scroll position.
    m_thumbPosition = viewStart;
}

void wxScrollBar::SetScrollbar(int position, int thumbSize,
                               int range, int pageSize,
                               bool WXUNUSED(refresh))
{
    // TODO(dom-phase-2): update the DOM element's scroll metrics.
    m_thumbPosition = position;
    m_thumbSize = thumbSize;
    m_range = range;
    m_pageSize = pageSize;
}

wxSize wxScrollBar::DoGetBestSize() const
{
    // TODO(dom-phase-2): measure the scrollbar DOM element instead.
    return IsVertical() ? wxSize(20, 100) : wxSize(100, 20);
}

#endif // wxUSE_SCROLLBAR
