/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/slider.cpp
// Purpose:     wxSlider implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_SLIDER

#include "wx/slider.h"

wxSlider::wxSlider() :
    m_value(0),
    m_min(0),
    m_max(100),
    m_lineSize(1),
    m_pageSize(1),
    m_thumbLength(0)
{
}

wxSlider::wxSlider(wxWindow *parent,
                   wxWindowID id,
                   int value, int minValue, int maxValue,
                   const wxPoint& pos,
                   const wxSize& size,
                   long style,
                   const wxValidator& validator,
                   const wxString& name) :
    m_value(0),
    m_min(0),
    m_max(100),
    m_lineSize(1),
    m_pageSize(1),
    m_thumbLength(0)
{
    Create(parent, id, value, minValue, maxValue, pos, size, style,
           validator, name);
}

bool wxSlider::Create(wxWindow *parent,
                      wxWindowID id,
                      int value, int minValue, int maxValue,
                      const wxPoint& pos,
                      const wxSize& size,
                      long style,
                      const wxValidator& validator,
                      const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    SetRange(minValue, maxValue);
    SetValue(value);
    SetPageSize(wxMax(1, (maxValue - minValue) / 10));

    // TODO(dom-phase-2): create a real <input type="range"> element and
    // wire its input event through wx_dom_event.

    return true;
}

int wxSlider::GetValue() const
{
    return m_value;
}

void wxSlider::SetValue(int value)
{
    // TODO(dom-phase-2): update the DOM element's value.
    m_value = value;
}

void wxSlider::SetRange(int minValue, int maxValue)
{
    // TODO(dom-phase-2): update the DOM element's min/max attributes.
    m_min = minValue;
    m_max = maxValue;
}

int wxSlider::GetMin() const
{
    return m_min;
}

int wxSlider::GetMax() const
{
    return m_max;
}

void wxSlider::SetLineSize(int lineSize)
{
    m_lineSize = lineSize;
}

void wxSlider::SetPageSize(int pageSize)
{
    m_pageSize = pageSize;
}

int wxSlider::GetLineSize() const
{
    return m_lineSize;
}

int wxSlider::GetPageSize() const
{
    return m_pageSize;
}

void wxSlider::SetThumbLength(int lenPixels)
{
    m_thumbLength = lenPixels;
}

int wxSlider::GetThumbLength() const
{
    return m_thumbLength;
}

wxSize wxSlider::DoGetBestSize() const
{
    // TODO(dom-phase-2): measure the <input type="range"> DOM element instead.
    return HasFlag(wxSL_VERTICAL) ? wxSize(20, 100) : wxSize(100, 20);
}

#endif // wxUSE_SLIDER
