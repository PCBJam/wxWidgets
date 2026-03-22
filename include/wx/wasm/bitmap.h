/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/bitmap.h
// Purpose:     wxBitmap class
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_BITMAP_H_
#define _WX_WASM_BITMAP_H_

class WXDLLIMPEXP_FWD_CORE wxPixelDataBase;

//-----------------------------------------------------------------------------
// wxBitmap
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxBitmap: public wxBitmapBase
{
public:
    wxBitmap();
    wxBitmap(int width, int height, int depth = wxBITMAP_SCREEN_DEPTH);
    wxBitmap(const wxSize& sz, int depth = wxBITMAP_SCREEN_DEPTH);
    wxBitmap(const char bits[], int width, int height, int depth = 1);
    wxBitmap(const char* const* bits);
    wxBitmap(const wxString &filename, wxBitmapType type = wxBITMAP_TYPE_XPM);
    wxBitmap(const wxImage& image, int depth = wxBITMAP_SCREEN_DEPTH, double scale = 1.0);
    wxBitmap(const wxImage& image, const wxDC& dc);

    virtual bool Create(int width, int height, int depth = wxBITMAP_SCREEN_DEPTH);
    virtual bool Create(const wxSize& sz, int depth = wxBITMAP_SCREEN_DEPTH);
    virtual bool Create(int width, int height, const wxDC& dc);
    virtual bool Create(const char bits[], int width, int height, int depth = 1);
    virtual bool CreateScaled(int width, int height, int depth, double scale);

    virtual int GetHeight() const;
    virtual int GetWidth() const;
    virtual int GetDepth() const;

    virtual void SetScaleFactor(double scale);
    virtual double GetScaleFactor() const;
    virtual double GetScaledWidth() const;
    virtual double GetScaledHeight() const;

    wxSize GetDIPSize() const { return GetSize(); }
    double GetLogicalWidth() const { return GetWidth(); }
    double GetLogicalHeight() const { return GetHeight(); }
    wxSize GetLogicalSize() const { return wxSize(GetWidth(), GetHeight()); }

#if wxUSE_IMAGE
    virtual bool Create(const wxImage& image,
                        int depth = wxBITMAP_SCREEN_DEPTH,
                        double scale = 1.0);
    virtual wxImage ConvertToImage() const;
#endif // wxUSE_IMAGE

    virtual wxMask *GetMask() const;
    virtual void SetMask(wxMask *mask);

    virtual wxBitmap GetSubBitmap(const wxRect& rect) const;

    virtual bool SaveFile(const wxString &name, wxBitmapType type,
                          const wxPalette *palette = NULL) const;
    virtual bool LoadFile(const wxString &name, wxBitmapType type);

    virtual void* GetRawData(wxPixelDataBase& data, int bpp);
    virtual void UngetRawData(wxPixelDataBase& data);

    virtual void *BeginRawAccess() const;
    virtual void EndRawAccess() const;

    int GetBytesPerPixel() const;
    int GetBytesPerRow() const;

#if wxUSE_PALETTE
    virtual wxPalette *GetPalette() const;
    virtual void SetPalette(const wxPalette& palette);
#endif // wxUSE_PALETTE

    // copies the contents and mask of the given (colour) icon to the bitmap
    virtual bool CopyFromIcon(const wxIcon& icon);

    // implementation:
    virtual void SetHeight(int height);
    virtual void SetWidth(int width);
    virtual void SetDepth(int depth);

    static void InitStandardHandlers();

    bool HasAlpha() const;

    void SyncToCpp() const;
    void SyncToJs() const;
    int GetJavascriptId() const;

protected:
    virtual wxGDIRefData* CreateGDIRefData() const;
    virtual wxGDIRefData* CloneGDIRefData(const wxGDIRefData* data) const;

    // implementation
    void AllocateData() const;

private:
    wxDECLARE_DYNAMIC_CLASS(wxBitmap);
};

//-----------------------------------------------------------------------------
// wxMask
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxMask : public wxMaskBase
{
public:
    wxMask();
    wxMask(const wxMask& mask);
    wxMask(const wxBitmap& bitmap, const wxColour& colour);
#if wxUSE_PALETTE
    wxMask(const wxBitmap& bitmap, int paletteIndex);
#endif // wxUSE_PALETTE
    wxMask(const wxBitmap& bitmap);
    virtual ~wxMask();

    wxBitmap GetBitmap() const;

    int GetDataSize() const { return m_dataSize; }
    uint32_t* GetData() const;

private:
    virtual void FreeData();
    virtual bool InitFromColour(const wxBitmap& bitmap, const wxColour& colour);
    virtual bool InitFromMonoBitmap(const wxBitmap& bitmap);

    wxDECLARE_DYNAMIC_CLASS(wxMask);

    int m_dataSize;
    uint32_t* m_data;
    wxBitmap m_bitmap;
};

#endif // _WX_WASM_BITMAP_H_
