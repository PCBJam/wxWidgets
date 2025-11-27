/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dc.cpp
// Purpose:     wxDC implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include <math.h>

#include "wx/app.h"
#include "wx/wasm/dc.h"
#include "wx/wasm/private/display.h"

#include <emscripten.h>

// ----------------------------------------------------------------------------
// wxWasmDCImpl
// ----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxWasmDCImpl, wxDCImpl)

wxWasmDCImpl::wxWasmDCImpl(wxDC *owner)
    : wxDCImpl(owner),
      m_jsId(-1),
      m_fontDirty(true)
{
    m_ok = false;

    m_pen = *wxBLACK_PEN;
    m_font = *wxNORMAL_FONT;
    m_brush = *wxWHITE_BRUSH;
    m_backgroundBrush = *wxWHITE_BRUSH;
}

void wxWasmDCImpl::Clear()
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    wxSize size = GetSize();

    EM_ASM({
        clearRect($0, $1, $2, $3);
    }, GetJavascriptId(),
       LogicalToDeviceXRel(size.x),
       LogicalToDeviceYRel(size.y),
       m_backgroundBrush.GetColour().GetRGBA());
}

void wxWasmDCImpl::SetFont(const wxFont& font)
{
    m_font = font;
    m_fontDirty = true;
}

enum HTML5LineJoin
{
    HTML5_LINE_JOIN_ROUND = 0,
    HTML5_LINE_JOIN_BEVEL = 1,
    HTML5_LINE_JOIN_MITER = 2
};

enum HTML5LineCap
{
    HTML5_LINE_CAP_BUTT = 0,
    HTML5_LINE_CAP_ROUND = 1,
    HTML5_LINE_CAP_SQUARE = 2
};

HTML5LineJoin wxPenJoinToHTML5LineJoin(wxPenJoin penJoin)
{
    switch (penJoin)
    {
        case wxJOIN_BEVEL:
            return HTML5_LINE_JOIN_BEVEL;
            break;
        case wxJOIN_MITER:
            return HTML5_LINE_JOIN_MITER;
            break;
        case wxJOIN_ROUND:
            return HTML5_LINE_JOIN_ROUND;
            break;
        default:
            wxFAIL_MSG(wxT("Invalid pen join"));
            return HTML5_LINE_JOIN_ROUND;
            break;
    }
}

HTML5LineCap wxPenCapToHTML5LineCap(wxPenCap penCap)
{
    switch (penCap)
    {
        case wxCAP_ROUND:
            return HTML5_LINE_CAP_ROUND;
            break;
        case wxCAP_PROJECTING:
            return HTML5_LINE_CAP_SQUARE;
            break;
        case wxCAP_BUTT:
            return HTML5_LINE_CAP_BUTT;
            break;
        default:
            wxFAIL_MSG(wxT("Invalid pen cap"));
            return HTML5_LINE_CAP_ROUND;
            break;
    }
}

void wxWasmDCImpl::SetPen(const wxPen& pen)
{
    if (pen != m_pen)
    {
        m_pen = pen;

        int bitmapId = -1;

        if (m_pen.GetStyle() == wxPENSTYLE_STIPPLE)
        {
            wxBitmap *stippleBitmap = m_pen.GetStipple();
            wxASSERT_MSG(stippleBitmap != NULL, "stipple pen without bitmap");

            stippleBitmap->SyncToJs();
            bitmapId = stippleBitmap->GetJavascriptId();
        }

        EM_ASM({
            setPen($0, $1, $2, $3, $4, $5, $6, $7);
        }, GetJavascriptId(),
           m_pen.GetColour().GetRGBA(),
           m_pen.GetWidth(),
           wxPenJoinToHTML5LineJoin(m_pen.GetJoin()),
           wxPenCapToHTML5LineCap(m_pen.GetCap()),
           m_pen.GetDashCount(),
           m_pen.GetDash(),
           bitmapId);
    }
}

void wxWasmDCImpl::SetBrush(const wxBrush& brush)
{
    m_brush = brush;

    int bitmapId = -1;

    if (m_brush.GetStyle() == wxBRUSHSTYLE_STIPPLE)
    {
        wxBitmap *stippleBitmap = m_brush.GetStipple();
        wxASSERT_MSG(stippleBitmap != NULL, "stipple brush without bitmap");

        stippleBitmap->SyncToJs();
        bitmapId = stippleBitmap->GetJavascriptId();
    }

    EM_ASM({
        setBrush($0, $1, $2);
    }, GetJavascriptId(), m_brush.GetColour().GetRGBA(), bitmapId);
}

void wxWasmDCImpl::SetBackground(const wxBrush& brush)
{
    m_backgroundBrush = brush;
}

void wxWasmDCImpl::SetPalette(const wxPalette& WXUNUSED(palette))
{
    // TODO: implement
    wxFAIL_MSG(wxT("SetPalette is not implemented"));
}

wxCoord wxWasmDCImpl::GetCharWidth() const
{
    wxCoord charWidth;
    m_font.GetCharSize(&charWidth, NULL);
    return charWidth;
}

wxCoord wxWasmDCImpl::GetCharHeight() const
{
    wxCoord charHeight;
    m_font.GetCharSize(NULL, &charHeight);
    return charHeight;
}

void wxWasmDCImpl::DoGetTextExtent(const wxString& string,
                                   wxCoord *x, wxCoord *y,
                                   wxCoord *descent,
                                   wxCoord *externalLeading,
                                   const wxFont *theFont) const
{
    const wxFont *font = theFont != NULL ? theFont : &m_font;
    font->GetTextExtent(string, x, y, descent, externalLeading);
}

bool wxWasmDCImpl::CanDrawBitmap() const
{
    return true;
}

bool wxWasmDCImpl::CanGetTextExtent() const
{
    return true;
}

int wxWasmDCImpl::GetDepth() const
{
    return 32;
}

wxSize wxWasmDCImpl::GetPPI() const
{
    return wxSize(static_cast<int>(m_mm_to_pix_x * 25.4 + 0.5),
                  static_cast<int>(m_mm_to_pix_y * 25.4 + 0.5));
}

void wxWasmDCImpl::DoGetSizeMM(int* width, int* height) const
{
    int wPixels, hPixels;
    DoGetSize(&wPixels, &hPixels);

    if (width)
    {
        *width = wPixels / m_mm_to_pix_x;
    }
    if (height)
    {
        *height = hPixels / m_mm_to_pix_y;
    }
}

void wxWasmDCImpl::SetLogicalFunction(wxRasterOperationMode WXUNUSED(function))
{
    // TODO: implement
    //wxFAIL_MSG(wxT("SetLogicalFunction is not implemented"));
}

void wxWasmDCImpl::SetTextForeground(const wxColour& colour)
{
    m_textForegroundColour = colour;
}

void wxWasmDCImpl::SetTextBackground(const wxColour& colour)
{
    m_textBackgroundColour = colour;
}

void wxWasmDCImpl::DoSetDeviceClippingRegion(const wxRegion& region)
{
    // TODO: implement for non-rectangular regions
    wxRect rect = region.GetBox();

    EM_ASM({
        clipRect($0, $1, $2, $3, $4);
    }, GetJavascriptId(), rect.x, rect.y, rect.width, rect.height);
}

void wxWasmDCImpl::DoSetClippingRegion(wxCoord x, wxCoord y,
                                       wxCoord width, wxCoord height)
{
    wxDCImpl::DoSetClippingRegion(x, y, width, height);

    EM_ASM({
        clipRect($0, $1, $2, $3, $4);
    }, GetJavascriptId(),
       LogicalToDeviceDoubleX(m_clipX1),
       LogicalToDeviceDoubleY(m_clipY1),
       LogicalToDeviceXRel(m_clipX2 - m_clipX1),
       LogicalToDeviceYRel(m_clipY2 - m_clipY1));
}

void wxWasmDCImpl::DestroyClippingRegion()
{
    m_clipping = false;

    EM_ASM({
        destroyClip($0);
    }, GetJavascriptId());
}

bool wxWasmDCImpl::DoGetPixel(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y), wxColour *WXUNUSED(col)) const
{
    wxCHECK_MSG(IsOk(), false, wxT("invalid dc"));
    // TODO: implement
    wxFAIL_MSG(wxT("DoGetPixel is not implemented"));

    return true;
}

void wxWasmDCImpl::DoDrawPoint(wxCoord x, wxCoord y)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    if (m_pen.IsNonTransparent())
    {
        EM_ASM({
            drawPoint($0, $1, $2);
        }, GetJavascriptId(), LogicalToDeviceDoubleX(x), LogicalToDeviceDoubleY(y));
    }
}

void wxWasmDCImpl::DoDrawLine(wxCoord x1, wxCoord y1, wxCoord x2, wxCoord y2)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    if (m_pen.IsNonTransparent())
    {
        EM_ASM({
            drawLine($0, $1, $2, $3, $4);
        }, GetJavascriptId(),
           LogicalToDeviceDoubleX(x1),
           LogicalToDeviceDoubleY(y1),
           LogicalToDeviceDoubleX(x2),
           LogicalToDeviceDoubleY(y2));
    }
}

void wxWasmDCImpl::DoDrawLines(int n, const wxPoint points[],
                               wxCoord xoffset, wxCoord yoffset)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    if (n > 0 && m_pen.IsNonTransparent())
    {
        int coords[2 * n];

        for (int i = 0, j = 0; i < n; i++)
        {
            coords[j++] = LogicalToDeviceDoubleX(points[i].x + xoffset);
            coords[j++] = LogicalToDeviceDoubleY(points[i].y + yoffset);
        }

        EM_ASM({
            drawLines($0, $1, $2);
        }, GetJavascriptId(), n, coords);
    }
}

void wxWasmDCImpl::DoDrawPolygon(int n, const wxPoint points[],
                                 wxCoord xoffset, wxCoord yoffset,
                                 wxPolygonFillMode fillMode)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    if (n > 0)
    {
        int coords[2 * n];

        for (int i = 0, j = 0; i < n; i++)
        {
            coords[j++] = LogicalToDeviceDoubleX(points[i].x + xoffset);
            coords[j++] = LogicalToDeviceDoubleY(points[i].y + yoffset);
        }

        EM_ASM({
            drawPolygon($0, $1, $2, $3, $4, $5);
        }, GetJavascriptId(),
        n,
        coords,
        fillMode == wxODDEVEN_RULE,
        m_brush.IsNonTransparent(),
        m_pen.IsNonTransparent());
    }
}

void wxWasmDCImpl::DoDrawRectangle(wxCoord x, wxCoord y,
                                   wxCoord width, wxCoord height)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    EM_ASM({
        drawRect($0, $1, $2, $3, $4, $5, $6);
    }, GetJavascriptId(),
       LogicalToDeviceDoubleX(x),
       LogicalToDeviceDoubleY(y),
       LogicalToDeviceXRel(width),
       LogicalToDeviceYRel(height),
       m_brush.IsNonTransparent(),
       m_pen.IsNonTransparent());
}

void wxWasmDCImpl::DoDrawRoundedRectangle(wxCoord x, wxCoord y,
        wxCoord width, wxCoord height,
        double radius)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    EM_ASM({
        drawRoundedRect($0, $1, $2, $3, $4, $5, $6, $7);
    }, GetJavascriptId(),
       LogicalToDeviceDoubleX(x),
       LogicalToDeviceDoubleY(y),
       LogicalToDeviceXRel(width),
       LogicalToDeviceYRel(height),
       radius,
       m_brush.IsNonTransparent(),
       m_pen.IsNonTransparent());
}

void wxWasmDCImpl::DoDrawEllipse(wxCoord x, wxCoord y,
                                 wxCoord width, wxCoord height)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    EM_ASM({
        drawEllipse($0, $1, $2, $3, $4, $5, $6);
    }, GetJavascriptId(),
       LogicalToDeviceDoubleX(x),
       LogicalToDeviceDoubleY(y),
       LogicalToDeviceXRel(width),
       LogicalToDeviceYRel(height),
       m_brush.IsNonTransparent(),
       m_pen.IsNonTransparent());
}

void wxWasmDCImpl::DoDrawArc(wxCoord x1, wxCoord y1,
                             wxCoord x2, wxCoord y2,
                             wxCoord xc, wxCoord yc)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    double dx1 = x1 - xc;
    double dy1 = y1 - yc;
    double radius = sqrt(dx1 * dx1 + dy1 * dy1);
    double startAngle = atan2(dy1, dx1);
    double endAngle = atan2(y2 - yc, x2 - xc);

    EM_ASM({
        drawArc($0, $1, $2, $3, $4, $5, $6, $7);
    }, GetJavascriptId(),
       LogicalToDeviceDoubleX(xc),
       LogicalToDeviceDoubleY(yc),
       radius,
       startAngle,
       endAngle,
       m_brush.IsNonTransparent(),
       m_pen.IsNonTransparent());
}

void wxWasmDCImpl::DoDrawEllipticArc(wxCoord x, wxCoord y, wxCoord w, wxCoord h,
                                     double startAngle, double endAngle)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    EM_ASM({
        drawEllipticArc($0, $1, $2, $3, $4, $5, $6, $7, $8);
    }, GetJavascriptId(),
       LogicalToDeviceDoubleX(x),
       LogicalToDeviceDoubleY(y),
       LogicalToDeviceXRel(w),
       LogicalToDeviceYRel(h),
       startAngle,
       endAngle,
       m_brush.IsNonTransparent(),
       m_pen.IsNonTransparent());
}

void wxWasmDCImpl::DoDrawIcon(const wxIcon& icon, wxCoord x, wxCoord y)
{
    wxBitmap bitmap;
    bitmap.CopyFromIcon(icon);
    DoDrawBitmap(bitmap, x, y);
}

void wxWasmDCImpl::DoDrawBitmap(const wxBitmap &bitmap, wxCoord x, wxCoord y,
                                bool WXUNUSED(useMask))
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    bitmap.SyncToJs();

    EM_ASM({
        drawBitmap($0, $1, $2, $3);
    }, GetJavascriptId(),
    bitmap.GetJavascriptId(),
    LogicalToDeviceDoubleX(x),
    LogicalToDeviceDoubleY(y));
}

bool wxWasmDCImpl::DoBlit(wxCoord xdest, wxCoord ydest,
                          wxCoord width, wxCoord height,
                          wxDC *source,
                          wxCoord xsrc, wxCoord ysrc,
                          wxRasterOperationMode WXUNUSED(rop),
                          bool WXUNUSED(useMask),
                          wxCoord WXUNUSED(xsrcMask), wxCoord WXUNUSED(ysrcMask))
{
    wxCHECK_MSG(IsOk(), false, wxT("invalid dc"));

    wxWasmDCImpl *srcImpl = static_cast<wxWasmDCImpl*>(source->GetImpl());

    EM_ASM({
        blit($0, $1, $2, $3, $4, $5, $6, $7);
    }, srcImpl->GetJavascriptId(),
    GetJavascriptId(),
    xsrc, ysrc,
    width, height,
    xdest, ydest);

    return true;
}

void wxWasmDCImpl::DoCrossHair(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y))
{
    // TODO: implement
    wxFAIL_MSG(wxT("DoCrossHair is not implemented"));
}

void wxWasmDCImpl::DoDrawText(const wxString& text, wxCoord x, wxCoord y)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    if (m_fontDirty)
    {
        wxString fontInfoDesc = m_font.GetNativeFontInfoDesc();
        const char *fontString = fontInfoDesc.utf8_str();

        // TODO: set underline and strikethrough when context supports textDecoration attribute
        EM_ASM({
            setFont($0, UTF8ToString($1));
        }, GetJavascriptId(), fontString);

        m_fontDirty = false;
    }

    const char *s = text.utf8_str();

    wxCoord devX = LogicalToDeviceDoubleX(x);
    wxCoord devY = LogicalToDeviceDoubleY(y);

    wxCoord textWidth;
    wxCoord textHeight;

    DoGetTextExtent(text, &textWidth, &textHeight);

    if (m_backgroundMode == wxSOLID && m_textBackgroundColour.Alpha() != 0)
    {
        wxBrush saveBrush = m_brush;
        SetBrush(m_textBackgroundColour);

        EM_ASM({
            drawRect($0, $1, $2, $3, $4, $5, $6);
        }, GetJavascriptId(),
           devX,
           devY,
           LogicalToDeviceXRel(textWidth),
           LogicalToDeviceYRel(textHeight),
           true,
           false);

        SetBrush(saveBrush);
    }

    wxCoord textY = devY + textHeight * (5.0 / 6.0);

    EM_ASM({
        drawText($0, UTF8ToString($1), $2, $3, $4);
    }, GetJavascriptId(), s, devX, textY, m_textForegroundColour.GetRGBA());
}

void wxWasmDCImpl::DoDrawRotatedText(const wxString& text,
                                     wxCoord x, wxCoord y,
                                     double angle)
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    wxCoord devX = LogicalToDeviceDoubleX(x);
    wxCoord devY = LogicalToDeviceDoubleY(y);

    EM_ASM({
        rotateAtPoint($0, $1, $2, $3);
    }, GetJavascriptId(), devX, devY, angle);

    DoDrawText(text, 0, 0);

    EM_ASM({
        clearRotation($0);
    }, GetJavascriptId(), devX, devY, angle);
}

bool wxWasmDCImpl::DoFloodFill(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y),
                               const wxColour& WXUNUSED(col),
                               wxFloodFillStyle WXUNUSED(style))
{
    wxCHECK_MSG(IsOk(), false, wxT("invalid dc"));
    // TODO: implement
    wxFAIL_MSG(wxT("DoFloodFill is not implemented"));

    return true;
}
