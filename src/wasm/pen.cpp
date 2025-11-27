/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/pen.cpp
// Purpose:     wxPen implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/pen.h"

#ifndef WX_PRECOMP
#include "wx/colour.h"
#endif // WX_PRECOMP

//-----------------------------------------------------------------------------
// wxPenRefData
//-----------------------------------------------------------------------------

class wxPenRefData: public wxGDIRefData
{
public:
    wxPenRefData()
    {
        m_colour = *wxBLACK;
        m_width = 1;
        m_style = wxPENSTYLE_SOLID;
        m_join = wxJOIN_ROUND;
        m_cap = wxCAP_ROUND;
        m_stipple = NULL;
        m_dashCount = 0;
        m_dash = NULL;
    }

    wxPenRefData(const wxPenRefData& data)
        : wxGDIRefData()
    {
        m_colour = data.m_colour;
        m_width = data.m_width;
        m_style = data.m_style;
        m_join = data.m_join;
        m_cap = data.m_cap;
        m_stipple = data.m_stipple ? new wxBitmap(*data.m_stipple) : NULL;
        m_dashCount = data.m_dashCount;
        m_dash = data.m_dash;
    }

    ~wxPenRefData()
    {
        delete m_stipple;
    }

    bool operator==(const wxPenRefData& data) const
    {
        return m_colour == data.m_colour &&
               m_width == data.m_width &&
               m_style == data.m_style &&
               m_join == data.m_join &&
               m_cap == data.m_cap &&
               (m_style != wxPENSTYLE_STIPPLE || m_stipple->IsSameAs(*data.m_stipple)) &&
               (m_style != wxPENSTYLE_USER_DASH ||
                (m_dashCount == data.m_dashCount &&
                 memcmp(m_dash, data.m_dash, m_dashCount * sizeof(wxDash)) == 0));
    }

    inline const wxColour& GetColour() const { return m_colour; }
    inline int GetWidth() const { return m_width; }
    inline wxPenStyle GetStyle() const { return m_style; }
    inline wxPenJoin GetJoin() const { return m_join; }
    inline wxPenCap GetCap() const { return m_cap; }
    inline wxBitmap *GetStipple() const { return m_stipple; }
    inline int GetDashCount() const { return m_dashCount; }
    inline const wxDash *GetDash() const { return m_dash; }

    inline void SetColour(const wxColour& colour) { m_colour = colour; }
    inline void SetWidth(int width) { m_width = width; }
    inline void SetStyle(wxPenStyle style) { m_style = style; }
    inline void SetJoin(wxPenJoin join) { m_join = join; }
    inline void SetCap(wxPenCap cap) { m_cap = cap; }

    inline void SetStipple(const wxBitmap& stipple)
    {
        delete m_stipple;
        m_stipple = new wxBitmap(stipple);
        m_style = wxPENSTYLE_STIPPLE;
    }

    inline void SetDashes(int dashCount, const wxDash *dash)
    {
        m_dashCount = dashCount;
        m_dash = dash;
    }

protected:
    wxColour    m_colour;
    int         m_width;
    wxPenStyle  m_style;
    wxPenJoin   m_join;
    wxPenCap    m_cap;
    wxBitmap *  m_stipple;
    int         m_dashCount;
    const wxDash *m_dash;
};

//-----------------------------------------------------------------------------
// wxPen
//-----------------------------------------------------------------------------

#define M_PENDATA ((wxPenRefData *)m_refData)

IMPLEMENT_DYNAMIC_CLASS(wxPen, wxPenBase)

wxPen::wxPen(const wxColour &colour, int width, wxPenStyle style)
{
    m_refData = new wxPenRefData();
    SetColour(colour);
    SetWidth(width);
    SetStyle(style);
}

wxPen::wxPen(const wxBitmap& stipple, int width)
{
    m_refData = new wxPenRefData();
    SetStipple(stipple);
    SetWidth(width);
}

wxPen::~wxPen()
{
    // m_refData unrefed in ~wxObject
}

wxGDIRefData *wxPen::CreateGDIRefData() const
{
    return new wxPenRefData();
}

wxGDIRefData *wxPen::CloneGDIRefData(const wxGDIRefData *data) const
{
    return new wxPenRefData(*(wxPenRefData *)data);
}

bool wxPen::operator==(const wxPen& pen) const
{
    if (m_refData == pen.m_refData)
    {
        return true;
    }

    if (!m_refData || !pen.m_refData)
    {
        return false;
    }

    return (*(wxPenRefData*)m_refData == *(wxPenRefData*)pen.m_refData);
}

void wxPen::SetColour(const wxColour &colour)
{
    AllocExclusive();

    M_PENDATA->SetColour(colour.GetRGBA());
}

void wxPen::SetColour(unsigned char red, unsigned char green, unsigned char blue)
{
    AllocExclusive();

    M_PENDATA->SetColour(wxColour(red, green, blue));
}

void wxPen::SetWidth(int width)
{
    AllocExclusive();

    M_PENDATA->SetWidth(width);
}

static const wxDash dotted[] = {1, 3};
static const wxDash longDash[] = {19, 9};
static const wxDash shortDash[] = {9, 9};
static const wxDash dotDash[] = {9, 6, 1, 6};

void wxPen::SetStyle(wxPenStyle style)
{
    AllocExclusive();

    switch (style)
    {
        case wxPENSTYLE_SOLID:
        case wxPENSTYLE_TRANSPARENT:
        case wxPENSTYLE_STIPPLE:
        case wxPENSTYLE_USER_DASH:
            break;
        case wxPENSTYLE_DOT:
            SetDashes(sizeof(dotted) / sizeof(wxDash), dotted);
            break;
        case wxPENSTYLE_LONG_DASH:
            SetDashes(sizeof(longDash) / sizeof(wxDash), longDash);
            break;
        case wxPENSTYLE_SHORT_DASH:
            SetDashes(sizeof(shortDash) / sizeof(wxDash), shortDash);
            break;
        case wxPENSTYLE_DOT_DASH:
            SetDashes(sizeof(dotDash) / sizeof(wxDash), dotDash);
            break;
        default:
            wxFAIL_MSG(wxT("Pen style is not implemented"));
    }

    M_PENDATA->SetStyle(style);
}

void wxPen::SetCap(wxPenCap cap)
{
    AllocExclusive();

    M_PENDATA->SetCap(cap);
}

void wxPen::SetJoin(wxPenJoin join)
{
    AllocExclusive();

    M_PENDATA->SetJoin(join);
}

void wxPen::SetStipple(const wxBitmap& stipple)
{
    AllocExclusive();

    M_PENDATA->SetStipple(stipple);
}

void wxPen::SetDashes(int dashCount, const wxDash *dash)
{
    AllocExclusive();

    M_PENDATA->SetDashes(dashCount, dash);
}

wxColour wxPen::GetColour() const
{
    wxCHECK_MSG(IsOk(), wxNullColour, wxT("invalid pen"));

    return M_PENDATA->GetColour();
}

int wxPen::GetWidth() const
{
    wxCHECK_MSG(IsOk(), -1, wxT("invalid pen"));

    return M_PENDATA->GetWidth();
}

wxPenStyle wxPen::GetStyle() const
{
    wxCHECK_MSG(IsOk(), wxPENSTYLE_INVALID, wxT("invalid pen"));

    return M_PENDATA->GetStyle();
}

wxPenCap wxPen::GetCap() const
{
    wxCHECK_MSG(IsOk(), wxCAP_INVALID, wxT("invalid pen"));

    return M_PENDATA->GetCap();
}

wxPenJoin wxPen::GetJoin() const
{
    wxCHECK_MSG(IsOk(), wxJOIN_INVALID, wxT("invalid pen"));

    return M_PENDATA->GetJoin();
}

wxBitmap *wxPen::GetStipple() const
{
    wxCHECK_MSG(IsOk(), NULL, wxT("invalid pen"));

    return M_PENDATA->GetStipple();
}

int wxPen::GetDashes(wxDash **ptr) const
{
    wxCHECK_MSG(IsOk(), 0, wxT("invalid pen"));

    *ptr = const_cast<wxDash*>(M_PENDATA->GetDash());
    return M_PENDATA->GetDashCount();
}

int wxPen::GetDashCount() const
{
    wxCHECK_MSG(IsOk(), -1, wxT("invalid pen"));

    return M_PENDATA->GetDashCount();
}

wxDash *wxPen::GetDash() const
{
    wxCHECK_MSG(IsOk(), NULL, wxT("invalid pen"));

    return const_cast<wxDash*>(M_PENDATA->GetDash());
}

