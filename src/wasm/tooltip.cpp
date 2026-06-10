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
#include "wx/wasm/private/dom.h"

wxIMPLEMENT_ABSTRACT_CLASS(wxToolTip, wxObject);

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
    // Browser-native tooltip via the title attribute; windows without a
    // DOM node (canvas islands) have no tooltip surface yet.
    if (m_window && m_window->WasmGetDomId())
        wxDomSetTooltip(m_window->WasmGetDomId(), m_text);
}

#endif // wxUSE_TOOLTIPS
