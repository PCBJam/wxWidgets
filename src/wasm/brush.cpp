/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/brush.cpp
// Purpose:     wxBrush implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/brush.h"

#ifndef WX_PRECOMP
#include "wx/bitmap.h"
#include "wx/colour.h"
#endif

// ----------------------------------------------------------------------------
// wxBrushRefData
// ----------------------------------------------------------------------------

class wxBrushRefData: public wxGDIRefData
{
public:
    wxBrushRefData(const wxColour& colour = wxNullColour, wxBrushStyle style = wxBRUSHSTYLE_SOLID)
    {
        m_colour = colour;
        m_style = style;
        m_stipple = NULL;
    }

    wxBrushRefData(const wxBitmap& stipple)
    {
        m_stipple = NULL;
        DoSetStipple(stipple);
    }

    ~wxBrushRefData()
    {
        delete m_stipple;
    }

    wxBrushRefData(const wxBrushRefData& data)
        : wxGDIRefData()
    {
        m_colour = data.m_colour;
        m_style = data.m_style;
        m_stipple = data.m_stipple ? new wxBitmap(*data.m_stipple) : NULL;
    }

    bool operator==(const wxBrushRefData& data) const
    {
        return m_colour == data.m_colour &&
               m_style == data.m_style &&
               (m_style != wxBRUSHSTYLE_STIPPLE || m_stipple->IsSameAs(*data.m_stipple));
    }

    inline const wxColour& GetColour() const { return m_colour; }
    inline wxBrushStyle GetStyle() const { return m_style; }
    inline wxBitmap *GetStipple() { return m_stipple; }

    inline void SetColour(const wxColour& colour) { m_colour = colour; }
    inline void SetStyle(wxBrushStyle style) { m_style = style; }
    inline void SetStipple(const wxBitmap& stipple) { DoSetStipple(stipple); }

protected:
    void DoSetStipple(const wxBitmap& stipple)
    {
        delete m_stipple;
        m_stipple = new wxBitmap(stipple);
        m_style = wxBRUSHSTYLE_STIPPLE;
    }

    wxColour     m_colour;
    wxBrushStyle m_style;
    wxBitmap *   m_stipple;
};

// ----------------------------------------------------------------------------
// wxBrush
// ----------------------------------------------------------------------------

#define M_BRUSHDATA ((wxBrushRefData *)m_refData)

IMPLEMENT_DYNAMIC_CLASS(wxBrush, wxBrushBase)

wxBrush::wxBrush(const wxColour &colour, wxBrushStyle style)
{
    m_refData = new wxBrushRefData(colour, style);
}

wxBrush::wxBrush(const wxBitmap &stipple)
{
    m_refData = new wxBrushRefData(stipple);
}

wxBrush::~wxBrush()
{
    // m_refData unrefed in ~wxObject
}

wxGDIRefData *wxBrush::CreateGDIRefData() const
{
    return new wxBrushRefData();
}

wxGDIRefData *wxBrush::CloneGDIRefData(const wxGDIRefData *data) const
{
    return new wxBrushRefData(*(wxBrushRefData *)data);
}

bool wxBrush::operator==(const wxBrush& brush) const
{
    const wxBrushRefData *brushData = (wxBrushRefData *)brush.m_refData;

    // an invalid brush is considered to be only equal to another invalid brush
    return m_refData ? (brushData && *M_BRUSHDATA == *brushData) : !brushData;
}

wxBrushStyle wxBrush::GetStyle() const
{
    wxCHECK_MSG(IsOk(), wxBRUSHSTYLE_INVALID, wxT("invalid brush"));

    return M_BRUSHDATA->GetStyle();
}

wxColour wxBrush::GetColour() const
{
    wxCHECK_MSG(IsOk(), wxNullColour, wxT("invalid brush"));

    return M_BRUSHDATA->GetColour();
}

wxBitmap *wxBrush::GetStipple() const
{
    wxCHECK_MSG(IsOk(), NULL, wxT("invalid brush"));

    return M_BRUSHDATA->GetStipple();
}

void wxBrush::SetColour(const wxColour& col)
{
    AllocExclusive();

    M_BRUSHDATA->SetColour(col);
}

void wxBrush::SetColour(unsigned char r, unsigned char g, unsigned char b)
{
    AllocExclusive();

    M_BRUSHDATA->SetColour(wxColour(r, g, b));
}

void wxBrush::SetStyle(wxBrushStyle style)
{
    AllocExclusive();

    if (style != wxBRUSHSTYLE_SOLID &&
        style != wxBRUSHSTYLE_TRANSPARENT &&
        style != wxBRUSHSTYLE_STIPPLE)
    {
        // TODO: implement
        wxFAIL_MSG(wxT("Brush style is not implemented"));
    }

    M_BRUSHDATA->SetStyle(style);
}

void wxBrush::SetStipple(const wxBitmap& stipple)
{
    AllocExclusive();

    M_BRUSHDATA->SetStipple(stipple);
}
