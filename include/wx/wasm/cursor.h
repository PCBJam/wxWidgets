/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/cursor.h
// Purpose:     wxCursor class
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_CURSOR_H_
#define _WX_WASM_CURSOR_H_

#include "wx/gdiobj.h"
#include "wx/gdicmn.h"

class WXDLLIMPEXP_FWD_CORE wxColour;
class WXDLLIMPEXP_FWD_CORE wxImage;

//-----------------------------------------------------------------------------
// wxCursor
//-----------------------------------------------------------------------------

class WXDLLIMPEXP_CORE wxCursor : public wxGDIObject
{
public:
    wxCursor();
    wxCursor(wxStockCursor id) { InitFromStock(id); }
#if wxUSE_IMAGE
    wxCursor(const wxImage & image);
    wxCursor(const wxString& filename,
             wxBitmapType type = wxCURSOR_DEFAULT_TYPE,
             int hotSpotX = 0, int hotSpotY = 0);
#endif
    wxCursor(const char bits[], int width, int height,
             int hotSpotX = -1, int hotSpotY = -1,
             const char maskBits[] = NULL);
    wxCursor(int cursorType);
    virtual ~wxCursor() {}

    void Install() const;

protected:
    void InitFromStock(wxStockCursor);
#if wxUSE_IMAGE
    void InitFromImage(const wxImage& image);
#endif

    virtual wxGDIRefData *CreateGDIRefData() const;
    virtual wxGDIRefData *CloneGDIRefData(const wxGDIRefData *data) const;

private:
    wxDECLARE_DYNAMIC_CLASS(wxCursor);
};

wxCursor wxGetCursor();

#endif // _WX_WASM_CURSOR_H_
