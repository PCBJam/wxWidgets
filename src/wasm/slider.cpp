/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/slider.cpp
// Purpose:     wxSlider implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_SLIDER

#include "wx/slider.h"

#include "wx/wasm/private/dom.h"

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

    WasmCreateDomNode("slider");

    // the overrides below push range/value to the <input type="range">;
    // the range must be set first so the browser doesn't clamp the value
    SetRange(minValue, maxValue);
    SetValue(value);
    SetPageSize(wxMax(1, (maxValue - minValue) / 10));

    return true;
}

int wxSlider::GetValue() const
{
    return m_value;
}

void wxSlider::SetValue(int value)
{
    m_value = value;

    if (WasmGetDomId())
        wxDomSetIntValue(WasmGetDomId(), value);
}

void wxSlider::SetRange(int minValue, int maxValue)
{
    m_min = minValue;
    m_max = maxValue;

    if (WasmGetDomId())
        wxDomSetRange(WasmGetDomId(), minValue, maxValue);
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

void wxSlider::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_INPUT)
    {
        // Pull the dragged position into the cache and fire wxEVT_SLIDER,
        // like any port does for user changes.
        m_value = wxDomGetIntValue(WasmGetDomId());

        wxCommandEvent event(wxEVT_SLIDER, GetId());
        event.SetInt(m_value);
        event.SetEventObject(this);
        HandleWindowEvent(event);

        // TODO(dom-phase-3): also fire the wxScrollEvent family
        // (wxEVT_SCROLL_THUMBTRACK/THUMBRELEASE/CHANGED).
        return;
    }

    wxControl::OnDomEvent(kind);
}

wxSize wxSlider::DoGetBestSize() const
{
    // TODO(dom-phase-2): measure the <input type="range"> DOM element instead.
    return HasFlag(wxSL_VERTICAL) ? wxSize(20, 100) : wxSize(100, 20);
}

#endif // wxUSE_SLIDER
