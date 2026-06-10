/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/gauge.cpp
// Purpose:     wxGauge implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_GAUGE

#include "wx/gauge.h"

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

    // the base class caches the range/position in m_rangeMax/m_gaugePos
    SetRange(range);
    SetValue(0);

    // TODO(dom-phase-2): create a real <progress> element.

    return true;
}

wxSize wxGauge::DoGetBestSize() const
{
    // TODO(dom-phase-2): measure the <progress> DOM element instead.
    return IsVertical() ? wxSize(20, 100) : wxSize(100, 20);
}

#endif // wxUSE_GAUGE
