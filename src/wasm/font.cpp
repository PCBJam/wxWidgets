/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/font.cpp
// Purpose:     wxFont implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/font.h"
#include "wx/fontutil.h"

#ifndef WX_PRECOMP
#endif // WX_PRECOMP

#include <emscripten.h>

static const float DEFAULT_POINT_SIZE = 10;

namespace
{

const char* GetStyleString(wxFontStyle style)
{
    switch (style)
    {
        case wxFONTSTYLE_NORMAL:
            return "normal";
            break;
        case wxFONTSTYLE_ITALIC:
            return "italic";
            break;
        case wxFONTSTYLE_SLANT:
            return "oblique";
            break;
        default:
            wxFAIL_MSG("invalid font style");
            return "normal";
            break;
    }
}

const char* GetFamilyString(wxFontFamily family)
{
    switch (family)
    {
        case wxFONTFAMILY_DEFAULT:
            return "Open Sans, sans-serif";
            break;
        case wxFONTFAMILY_DECORATIVE:
            return "cursive";
            break;
        case wxFONTFAMILY_ROMAN:
            return "serif";
            break;
        case wxFONTFAMILY_SCRIPT:
            return "cursive";
            break;
        case wxFONTFAMILY_SWISS:
            return "Open Sans, sans-serif";
            break;
        case wxFONTFAMILY_MODERN:
            return "monospace";
            break;
        case wxFONTFAMILY_TELETYPE:
            return "monospace";
            break;
        case wxFONTFAMILY_MAX:
            wxFAIL_MSG("invalid font family");
            return "serif";
            break;
    }
}

} // anonymous namespace

// ----------------------------------------------------------------------------
// wxFontRefData
// ----------------------------------------------------------------------------

class wxFontRefData : public wxGDIRefData
{
public:
    wxFontRefData()
    {
        m_nativeFontInfo.SetFractionalPointSize(DEFAULT_POINT_SIZE);
    }
    wxFontRefData(const wxFontInfo& info)
    {
        m_nativeFontInfo.SetFaceName(info.GetFaceName());
        m_nativeFontInfo.SetFamily(info.GetFamily());
        // TODO: support font size in pixels
        if (info.IsUsingSizeInPixels())
        {
          m_nativeFontInfo.SetFractionalPointSize(info.GetPixelSize().y);
        }
        else
        {
          m_nativeFontInfo.SetFractionalPointSize(info.GetFractionalPointSize());
        }
        m_nativeFontInfo.SetStyle(info.GetStyle());
        m_nativeFontInfo.SetWeight(info.GetWeight());
        m_nativeFontInfo.SetUnderlined(info.IsUnderlined());
        m_nativeFontInfo.SetStrikethrough(info.IsStrikethrough());
    }

    wxFontRefData(const wxFontRefData& data)
    {
        m_nativeFontInfo = data.m_nativeFontInfo;
    }

    const wxNativeFontInfo *GetNativeFontInfo() const
    {
        return &m_nativeFontInfo;
    }

    void SetNativeFontInfo(const wxNativeFontInfo& info)
    {
        m_nativeFontInfo = info;
    }

    void GetTextExtent(const wxString &string,
                       wxCoord *x, wxCoord *y,
                       wxCoord *descent,
                       wxCoord *externalLeading) const;

    wxNativeFontInfo m_nativeFontInfo;
};

void wxNativeFontInfo::SetFractionalPointSize(double pointsize)
{
    if (pointsize != pointSize)
    {
        pointSize = pointsize;
        m_isRendered = false;
    }
}

void wxNativeFontInfo::SetStyle(wxFontStyle style_)
{
    if (style_ != style)
    {
        style = style_;
        m_isRendered = false;
    }
}

void wxNativeFontInfo::SetNumericWeight(int weight_)
{
    if (weight_ != weight)
    {
        weight = weight_;
        m_isRendered = false;
    }
}

void wxNativeFontInfo::SetUnderlined(bool underlined_)
{
    if (underlined_ != underlined)
    {
        underlined = underlined_;
        m_isRendered = false;
    }
}

void wxNativeFontInfo::SetStrikethrough(bool strikethrough_)
{
    if (strikethrough_ != strikethrough)
    {
        strikethrough = strikethrough_;
        m_isRendered = false;
    }
}

bool wxNativeFontInfo::SetFaceName(const wxString& facename_)
{
    if (facename_ != faceName)
    {
        faceName = facename_;
        m_isRendered = false;
    }

    return true;
}

void wxNativeFontInfo::SetFamily(wxFontFamily family_)
{
    if (family_ != family)
    {
        family = family_;
        m_isRendered = false;
    }
}

void wxNativeFontInfo::SetEncoding(wxFontEncoding encoding_)
{
    if (encoding_ != encoding)
    {
        encoding = encoding_;
        m_isRendered = false;
    }
}

bool wxNativeFontInfo::FromString(const wxString& WXUNUSED(s))
{
    return false;
}

wxString wxNativeFontInfo::ToString() const
{
    if (!m_isRendered)
    {
        wxString fontFaceAndFamily;
        if (GetFaceName().empty())
        {
            fontFaceAndFamily = GetFamilyString(GetFamily());
        }
        else
        {
            fontFaceAndFamily = wxString::Format("\"%s\", %s",
                                                 GetFaceName(),
                                                 GetFamilyString(GetFamily()));
        }

        m_renderedString = wxString::Format(wxT("%s %d %fpt/1 %s"),
                                            GetStyleString(GetStyle()),
                                            GetNumericWeight(),
                                            GetFractionalPointSize(),
                                            fontFaceAndFamily.utf8_str());
        m_isRendered = true;
    }
    return m_renderedString;
}

void wxFontRefData::GetTextExtent(const wxString &string,
                                  wxCoord *x, wxCoord *y,
                                  wxCoord *descent,
                                  wxCoord *externalLeading) const
{
    wxString fontInfoDesc = m_nativeFontInfo.ToString();
    const char *fontString = fontInfoDesc.utf8_str();

    const char *s = string.utf8_str();

    if (x != NULL)
    {
        *x = EM_ASM_INT({
            return measureText(UTF8ToString($0), UTF8ToString($1));
        }, s, fontString);
    }

    if (y != NULL)
    {
        *y = static_cast<int>(round(1.6 * m_nativeFontInfo.GetFractionalPointSize()));
    }

    if (descent != NULL)
    {
        *descent = 0;
    }

    if (externalLeading != NULL)
    {
        *externalLeading = 0;
    }
}

//-----------------------------------------------------------------------------
// wxFont
//-----------------------------------------------------------------------------

#define M_FONTDATA ((wxFontRefData*)m_refData)
#define M_FONTINFO (M_FONTDATA->m_nativeFontInfo)

wxFont::wxFont()
{
    m_refData = new wxFontRefData();
}

wxFont::wxFont(const wxFontInfo& info)
{
    m_refData = new wxFontRefData(info);
}

wxFont::wxFont(const wxString& nativeFontInfoString)
{
    wxNativeFontInfo info;
    if (info.FromString(nativeFontInfoString))
    {
        wxFontRefData *fontRefData = new wxFontRefData();
        m_refData = fontRefData;
        fontRefData->SetNativeFontInfo(info);
    }
}

wxFont::wxFont(const wxNativeFontInfo& info)
{
    M_FONTINFO = info;
}

wxFont::wxFont(int size,
               wxFontFamily family,
               wxFontStyle style,
               wxFontWeight weight,
               bool underlined,
               const wxString& face,
               wxFontEncoding encoding)
{
    Create(size, family, style, weight, underlined, face, encoding);
}

wxFont::wxFont(const wxSize& pixelSize,
               wxFontFamily family,
               wxFontStyle style,
               wxFontWeight weight,
               bool underlined,
               const wxString& face,
               wxFontEncoding encoding)
{
    Create(pixelSize.GetHeight(), family, style, weight, underlined, face, encoding);
}

wxFont::~wxFont()
{
}

bool wxFont::Create(int size,
                    wxFontFamily family,
                    wxFontStyle style,
                    wxFontWeight weight,
                    bool underlined,
                    const wxString& face,
                    wxFontEncoding encoding)
{
    UnRef();

    m_refData = new wxFontRefData(
        InfoFromLegacyParams(size, family, style, weight, underlined, face, encoding));

    return true;
}

// implement base class pure virtuals
double wxFont::GetFractionalPointSize() const
{
    wxCHECK_MSG(IsOk(), 0, wxT("invalid font"));
    return M_FONTINFO.GetFractionalPointSize();
}

wxFontStyle wxFont::GetStyle() const
{
    wxCHECK_MSG(IsOk(), wxFONTSTYLE_NORMAL, wxT("invalid font"));
    return M_FONTINFO.GetStyle();
}

wxFontWeight wxFont::GetWeight() const
{
    wxCHECK_MSG(IsOk(), wxFONTWEIGHT_NORMAL, wxT("invalid font"));
    return M_FONTINFO.GetWeight();
}

int wxFont::GetNumericWeight() const
{
    wxCHECK_MSG(IsOk(), false, wxT("invalid font"));
    return M_FONTINFO.GetNumericWeight();
}

bool wxFont::GetUnderlined() const
{
    wxCHECK_MSG(IsOk(), false, wxT("invalid font"));
    return M_FONTINFO.GetUnderlined();
}

bool wxFont::GetStrikethrough() const
{
    wxCHECK_MSG(IsOk(), false, wxT("invalid font"));
    return M_FONTINFO.GetStrikethrough();
}

wxString wxFont::GetFaceName() const
{
    wxCHECK_MSG(IsOk(), wxEmptyString, wxT("invalid font"));
    return M_FONTINFO.GetFaceName();
}

wxFontEncoding wxFont::GetEncoding() const
{
    wxCHECK_MSG(IsOk(), wxFONTENCODING_DEFAULT, wxT("invalid font"));
    return M_FONTINFO.GetEncoding();
}

const wxNativeFontInfo *wxFont::GetNativeFontInfo() const
{
    wxCHECK_MSG(IsOk(), NULL, wxT("invalid font"));
    return &M_FONTINFO;
}

wxFontFamily wxFont::DoGetFamily() const
{
    wxCHECK_MSG(IsOk(), wxFONTFAMILY_DEFAULT, wxT("invalid font"));
    return M_FONTINFO.GetFamily();
}

void wxFont::SetFractionalPointSize(double pointSize)
{
    AllocExclusive();
    M_FONTINFO.SetFractionalPointSize(pointSize);
}

void wxFont::SetFamily(wxFontFamily family)
{
    AllocExclusive();
    M_FONTINFO.SetFamily(family);
}

void wxFont::SetStyle(wxFontStyle style)
{
    AllocExclusive();
    M_FONTINFO.SetStyle(style);
}

void wxFont::SetWeight(wxFontWeight weight)
{
    AllocExclusive();
    M_FONTINFO.SetWeight(weight);
}

void wxFont::SetNumericWeight(int weight)
{
    AllocExclusive();
    M_FONTINFO.SetNumericWeight(weight);
}

void wxFont::SetUnderlined(bool underlined)
{
    AllocExclusive();
    M_FONTINFO.SetUnderlined(underlined);
}

void wxFont::SetStrikethrough(bool strikethrough)
{
    AllocExclusive();
    M_FONTINFO.SetStrikethrough(strikethrough);
}

bool wxFont::SetFaceName(const wxString& faceName)
{
    AllocExclusive();
    return M_FONTINFO.SetFaceName(faceName);
}

void wxFont::SetEncoding(wxFontEncoding encoding)
{
    AllocExclusive();
    return M_FONTINFO.SetEncoding(encoding);
}

wxGDIRefData* wxFont::CreateGDIRefData() const
{
    return new wxFontRefData();
}

wxGDIRefData* wxFont::CloneGDIRefData(const wxGDIRefData* data) const
{
    return new wxFontRefData(*static_cast<const wxFontRefData *>(data));
}

void wxFont::GetTextExtent(const wxString &string,
                           wxCoord *x, wxCoord *y,
                           wxCoord *descent,
                           wxCoord *externalLeading) const
{
    M_FONTDATA->GetTextExtent(string, x, y, descent, externalLeading);
}

void wxFont::GetCharSize(wxCoord *x, wxCoord *y) const
{
    M_FONTDATA->GetTextExtent(wxT("M"), x, y, NULL, NULL);
}
