/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/brush.h
// Purpose:     wxBrush class
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_BRUSH_H_
#define _WX_WASM_BRUSH_H_

class WXDLLIMPEXP_FWD_CORE wxBitmap;
class WXDLLIMPEXP_FWD_CORE wxColour;

//-----------------------------------------------------------------------------
// wxBrush
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxBrush: public wxBrushBase
{
public:
    wxBrush() : wxBrush(*wxBLACK) { }
    wxBrush(const wxColour &colour, wxBrushStyle style = wxBRUSHSTYLE_SOLID);
    wxBrush(const wxBitmap &stippleBitmap);
    virtual ~wxBrush();

    bool operator==(const wxBrush& brush) const;
    bool operator!=(const wxBrush& brush) const { return !(*this == brush); }

    void SetColour(const wxColour& col);
    void SetColour(unsigned char r, unsigned char g, unsigned char b);

    void SetStyle(wxBrushStyle style);
    void SetStipple(const wxBitmap& stipple);

    wxColour GetColour() const;
    wxBrushStyle GetStyle() const;
    wxBitmap *GetStipple() const;

    wxDEPRECATED_MSG("use wxBRUSHSTYLE_XXX constants")
    wxBrush(const wxColour& col, int style);
    
    wxDEPRECATED_MSG("use wxBRUSHSTYLE_XXX constants")
    void SetStyle(int style) { SetStyle((wxBrushStyle)style); }

protected:
    virtual wxGDIRefData *CreateGDIRefData() const;
    virtual wxGDIRefData *CloneGDIRefData(const wxGDIRefData *data) const;

    wxDECLARE_DYNAMIC_CLASS(wxBrush);
};

#endif // _WX_WASM_BRUSH_H_
