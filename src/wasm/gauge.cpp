/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/gauge.cpp
// Purpose:     wxGauge implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_GAUGE

#include "wx/gauge.h"

#include "wx/wasm/private/dom.h"

wxGauge::wxGauge()
{
}

wxGauge::wxGauge(wxWindow *parent,
                 wxWindowID id,
                 int range,
                 const wxPoint& pos,
                 const wxSize& size,
                 long style,
                 const wxValidator& validator,
                 const wxString& name)
{
    Create(parent, id, range, pos, size, style, validator, name);
}

bool wxGauge::Create(wxWindow *parent,
                     wxWindowID id,
                     int range,
                     const wxPoint& pos,
                     const wxSize& size,
                     long style,
                     const wxValidator& validator,
                     const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    WasmCreateDomNode("gauge");

    // the base class caches the range/position in m_rangeMax/m_gaugePos;
    // the overrides below push them to the <progress> element
    SetRange(range);
    SetValue(0);

    return true;
}

void wxGauge::SetRange(int range)
{
    wxGaugeBase::SetRange(range);

    if (WasmGetDomId())
        wxDomSetRange(WasmGetDomId(), 0, range);
}

void wxGauge::SetValue(int pos)
{
    wxGaugeBase::SetValue(pos);

    if (WasmGetDomId())
        wxDomSetIntValue(WasmGetDomId(), pos);
}

wxSize wxGauge::DoGetBestSize() const
{
    // The intrinsic size of a <progress> element is not a useful wx best
    // size; use the conventional gauge proportions instead.
    return IsVertical() ? wxSize(20, 100) : wxSize(100, 20);
}

#endif // wxUSE_GAUGE
