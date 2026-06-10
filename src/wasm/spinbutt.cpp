/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/spinbutt.cpp
// Purpose:     wxSpinButton implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#if wxUSE_SPINBTN

#include "wx/spinbutt.h"

#include "wx/wasm/private/dom.h"

wxSpinButton::wxSpinButton() :
    m_value(0)
{
}

wxSpinButton::wxSpinButton(wxWindow *parent,
                           wxWindowID id,
                           const wxPoint& pos,
                           const wxSize& size,
                           long style,
                           const wxString& name) :
    m_value(0)
{
    Create(parent, id, pos, size, style, name);
}

bool wxSpinButton::Create(wxWindow *parent,
                          wxWindowID id,
                          const wxPoint& pos,
                          const wxSize& size,
                          long style,
                          const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, wxDefaultValidator, name))
        return false;

    // ▲▼ button pair; clicks arrive as SPIN_UP/SPIN_DOWN DOM events.
    WasmCreateDomNode("spinbutton");

    return true;
}

int wxSpinButton::GetValue() const
{
    return m_value;
}

void wxSpinButton::SetValue(int val)
{
    // the DOM element only shows the arrows, there is no value to reflect
    m_value = val;
}

void wxSpinButton::OnDomEvent(wxDomEventKind kind)
{
    if (kind == wxDOM_EVENT_SPIN_UP || kind == wxDOM_EVENT_SPIN_DOWN)
    {
        const bool up = kind == wxDOM_EVENT_SPIN_UP;
        const int oldValue = m_value;

        // Step by one, clamping to the range — or jumping to the other end
        // with wxSP_WRAP (equivalent to src/univ/spinbutt.cpp's
        // NormalizeValue() for single steps).
        int value = m_value + (up ? 1 : -1);
        if (value > m_max)
            value = HasFlag(wxSP_WRAP) ? m_min : m_max;
        else if (value < m_min)
            value = HasFlag(wxSP_WRAP) ? m_max : m_min;

        if (value == m_value)
            return; // already at the end of a non-wrapping range

        m_value = value;

        // The directional event goes first and is vetoable (wxSpinEvent is
        // a wxNotifyEvent): a vetoing handler restores the old value, like
        // the other ports do.
        wxSpinEvent dirEvent(up ? wxEVT_SPIN_UP : wxEVT_SPIN_DOWN, GetId());
        dirEvent.SetPosition(m_value);
        dirEvent.SetEventObject(this);
        if (HandleWindowEvent(dirEvent) && !dirEvent.IsAllowed())
        {
            m_value = oldValue;
            return;
        }

        // ... then the plain wxEVT_SPIN with the accepted position.
        wxSpinEvent event(wxEVT_SPIN, GetId());
        event.SetPosition(m_value);
        event.SetEventObject(this);
        HandleWindowEvent(event);
        return;
    }

    wxControl::OnDomEvent(kind);
}

#endif // wxUSE_SPINBTN
