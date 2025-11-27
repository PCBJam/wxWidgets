/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/cursor.cpp
// Purpose:     wxCursor implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/cursor.h"

#ifndef WX_PRECOMP
#include "wx/window.h"
#include "wx/image.h"
#include "wx/bitmap.h"
#include "wx/log.h"
#endif // WX_PRECOMP

#include "wx/wasm/private.h"

#include <emscripten.h>

//-----------------------------------------------------------------------------
// wxCursorRefData
//-----------------------------------------------------------------------------

enum HTML5CursorType
{
    HTML5_CURSOR_TYPE_CUSTOM = -1,
    HTML5_CURSOR_TYPE_POINTER = 0,
    HTML5_CURSOR_TYPE_CROSS = 1,
    HTML5_CURSOR_TYPE_HAND = 2,
    HTML5_CURSOR_TYPE_IBEAM = 3,
    HTML5_CURSOR_TYPE_WAIT = 4,
    HTML5_CURSOR_TYPE_HELP = 5,
    HTML5_CURSOR_TYPE_EASTRESIZE = 6,
    HTML5_CURSOR_TYPE_NORTHRESIZE = 7,
    HTML5_CURSOR_TYPE_NORTHEASTRESIZE = 8,
    HTML5_CURSOR_TYPE_NORTHWESTRESIZE = 9,
    HTML5_CURSOR_TYPE_SOUTHRESIZE = 10,
    HTML5_CURSOR_TYPE_SOUTHEASTRESIZE = 11,
    HTML5_CURSOR_TYPE_SOUTHWESTRESIZE = 12,
    HTML5_CURSOR_TYPE_WESTRESIZE = 13,
    HTML5_CURSOR_TYPE_NORTHSOUTHRESIZE = 14,
    HTML5_CURSOR_TYPE_EASTWESTRESIZE = 15,
    HTML5_CURSOR_TYPE_NORTHEASTSOUTHWESTRESIZE = 16,
    HTML5_CURSOR_TYPE_NORTHWESTSOUTHEASTRESIZE = 17,
    HTML5_CURSOR_TYPE_COLUMNRESIZE = 18,
    HTML5_CURSOR_TYPE_ROWRESIZE = 19,
    HTML5_CURSOR_TYPE_MOVE = 20,
    HTML5_CURSOR_TYPE_VERTICALTEXT = 21,
    HTML5_CURSOR_TYPE_CELL = 22,
    HTML5_CURSOR_TYPE_CONTEXTMENU = 23,
    HTML5_CURSOR_TYPE_ALIAS = 24,
    HTML5_CURSOR_TYPE_PROGRESS = 25,
    HTML5_CURSOR_TYPE_NODROP = 26,
    HTML5_CURSOR_TYPE_COPY = 27,
    HTML5_CURSOR_TYPE_NONE = 28,
    HTML5_CURSOR_TYPE_NOTALLOWED = 29,
    HTML5_CURSOR_TYPE_ZOOMIN = 30,
    HTML5_CURSOR_TYPE_ZOOMOUT = 31,
    HTML5_CURSOR_TYPE_GRAB = 32,
    HTML5_CURSOR_TYPE_GRABBING = 33
};

HTML5CursorType wxCursorToHTML5Cursor(wxStockCursor wxCursorId)
{
    HTML5CursorType html5Cursor = HTML5_CURSOR_TYPE_POINTER;

    switch (wxCursorId)
    {
        case wxCURSOR_NONE:
            html5Cursor = HTML5_CURSOR_TYPE_NONE;
            break;
        case wxCURSOR_ARROW:
            html5Cursor = HTML5_CURSOR_TYPE_POINTER;
            break;
        case wxCURSOR_RIGHT_ARROW:
            html5Cursor = HTML5_CURSOR_TYPE_POINTER;
            break;
        case wxCURSOR_BULLSEYE:
            break;
        case wxCURSOR_CHAR:
            html5Cursor = HTML5_CURSOR_TYPE_IBEAM;
            break;
        case wxCURSOR_CROSS:
            html5Cursor = HTML5_CURSOR_TYPE_CROSS;
            break;
        case wxCURSOR_HAND:
            html5Cursor = HTML5_CURSOR_TYPE_HAND;
            break;
        case wxCURSOR_IBEAM:
            html5Cursor = HTML5_CURSOR_TYPE_IBEAM;
            break;
        case wxCURSOR_LEFT_BUTTON:
            break;
        case wxCURSOR_MAGNIFIER:
            html5Cursor = HTML5_CURSOR_TYPE_IBEAM;
            break;
        case wxCURSOR_MIDDLE_BUTTON:
            break;
        case wxCURSOR_NO_ENTRY:
            html5Cursor = HTML5_CURSOR_TYPE_NOTALLOWED;
            break;
        case wxCURSOR_PAINT_BRUSH:
            break;
        case wxCURSOR_PENCIL:
            break;
        case wxCURSOR_POINT_LEFT:
            html5Cursor = HTML5_CURSOR_TYPE_WESTRESIZE;
            break;
        case wxCURSOR_POINT_RIGHT:
            html5Cursor = HTML5_CURSOR_TYPE_EASTRESIZE;
            break;
        case wxCURSOR_QUESTION_ARROW:
            html5Cursor = HTML5_CURSOR_TYPE_HELP;
            break;
        case wxCURSOR_RIGHT_BUTTON:
            break;
        case wxCURSOR_SIZENESW:
            html5Cursor = HTML5_CURSOR_TYPE_NORTHEASTSOUTHWESTRESIZE;
            break;
        case wxCURSOR_SIZENS:
            html5Cursor = HTML5_CURSOR_TYPE_NORTHSOUTHRESIZE;
            break;
        case wxCURSOR_SIZENWSE:
            html5Cursor = HTML5_CURSOR_TYPE_NORTHWESTSOUTHEASTRESIZE;
            break;
        case wxCURSOR_SIZEWE:
            html5Cursor = HTML5_CURSOR_TYPE_EASTWESTRESIZE;
            break;
        case wxCURSOR_SIZING:
            html5Cursor = HTML5_CURSOR_TYPE_MOVE;
            break;
        case wxCURSOR_SPRAYCAN:
            break;
        case wxCURSOR_WAIT:
            html5Cursor = HTML5_CURSOR_TYPE_WAIT;
            break;
        case wxCURSOR_WATCH:
            html5Cursor = HTML5_CURSOR_TYPE_WAIT;
            break;
        case wxCURSOR_BLANK:
            html5Cursor = HTML5_CURSOR_TYPE_NONE;
            break;
        case wxCURSOR_ARROWWAIT:
            break;
        case wxCURSOR_OPEN_HAND:
            html5Cursor = HTML5_CURSOR_TYPE_GRAB;
            break;
        case wxCURSOR_CLOSED_HAND:
            html5Cursor = HTML5_CURSOR_TYPE_GRABBING;
            break;
        default:
            wxFAIL_MSG(wxT("Invalid cursor type"));
            break;
    }

    return html5Cursor;
}

class wxCursorRefData: public wxGDIRefData
{
public:
    wxCursorRefData() : m_cursorType(HTML5_CURSOR_TYPE_POINTER) { }
    wxCursorRefData(wxStockCursor cursorId)
        : m_cursorType(wxCursorToHTML5Cursor(cursorId)),
          m_hotSpotX(0),
          m_hotSpotY(0) { }
    wxCursorRefData(const wxBitmap& bitmap, int hotSpotX, int hotSpotY)
        : m_cursorType(HTML5_CURSOR_TYPE_CUSTOM),
          m_bitmap(bitmap),
          m_hotSpotX(hotSpotX),
          m_hotSpotY(hotSpotY) { }
    wxCursorRefData(const wxCursorRefData& cursor)
    {
        m_cursorType = cursor.m_cursorType;
        m_bitmap = cursor.m_bitmap;
        m_hotSpotX = cursor.m_hotSpotX;
        m_hotSpotY = cursor.m_hotSpotY;
    }
    wxCursorRefData(HTML5CursorType cursorType)
        : m_cursorType(cursorType),
          m_hotSpotX(0),
          m_hotSpotY(0) { }

    virtual ~wxCursorRefData() { }

    HTML5CursorType GetCursorType() const { return m_cursorType; }

    bool HasBitmap() const { return m_bitmap.IsOk(); }
    const wxBitmap& GetBitmap() { return m_bitmap; }

    int GetHotSpotX() const { return m_hotSpotX; }
    int GetHotSpotY() const { return m_hotSpotY; }

private:
    HTML5CursorType m_cursorType;
    wxBitmap m_bitmap;
    int m_hotSpotX;
    int m_hotSpotY;
};

//-----------------------------------------------------------------------------
// wxCursor
//-----------------------------------------------------------------------------

#define M_CURSORDATA static_cast<wxCursorRefData*>(m_refData)

IMPLEMENT_DYNAMIC_CLASS(wxCursor, wxGDIObject)

wxCursor::wxCursor()
{
}

#if wxUSE_IMAGE
wxCursor::wxCursor(const wxImage &image)
{
    InitFromImage(image);
}

wxCursor::wxCursor(const wxString& filename,
                   wxBitmapType type,
                   int hotSpotX, int hotSpotY)
{
    wxImage img;
    bool retval = img.LoadFile(filename, type);
    wxASSERT(retval);

    // eventually set the hotspot:
    if (!img.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_X))
        img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_X, hotSpotX);
    if (!img.HasOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y))
        img.SetOption(wxIMAGE_OPTION_CUR_HOTSPOT_Y, hotSpotY);

    InitFromImage(img);
}
#endif

wxCursor::wxCursor(const char WXUNUSED(bits)[],
                   int WXUNUSED(width), int WXUNUSED(height),
                   int WXUNUSED(hotSpotX), int WXUNUSED(hotSpotY),
                   const char WXUNUSED(maskBits)[])
{
    // TODO: implement
    wxFAIL_MSG(wxT("wxCursor from XBM is not implemented"));
}

wxCursor::wxCursor(int cursorType)
{
    m_refData = new wxCursorRefData(static_cast<HTML5CursorType>(cursorType));
}

void wxCursor::Install() const
{
    HTML5CursorType cursorType = M_CURSORDATA->GetCursorType();
    //printf("setcursor: %d\n", cursorType);

    if (cursorType != HTML5_CURSOR_TYPE_CUSTOM)
    {
        EM_ASM({
            setCursor($0);
        }, cursorType);
    }
    else
    {
        M_CURSORDATA->GetBitmap().SyncToJs();

        EM_ASM({
            setCursor($0, $1, $2, $3);
        }, cursorType,
           M_CURSORDATA->GetBitmap().GetJavascriptId(),
           M_CURSORDATA->GetHotSpotX(),
           M_CURSORDATA->GetHotSpotY());
    }
}

void wxCursor::InitFromStock(wxStockCursor cursorId)
{
    m_refData = new wxCursorRefData(cursorId);
}

#if wxUSE_IMAGE
void wxCursor::InitFromImage(const wxImage& image)
{
    int hotSpotX = image.GetOptionInt(wxIMAGE_OPTION_CUR_HOTSPOT_X);
    int hotSpotY = image.GetOptionInt(wxIMAGE_OPTION_CUR_HOTSPOT_Y);
    if (hotSpotX < 0 || hotSpotX > image.GetWidth()) hotSpotX = 0;
    if (hotSpotY < 0 || hotSpotY > image.GetHeight()) hotSpotY = 0;

    m_refData = new wxCursorRefData(wxBitmap(image), hotSpotX, hotSpotY);
}
#endif

wxGDIRefData *wxCursor::CreateGDIRefData() const
{
    return new wxCursorRefData();
}

wxGDIRefData *wxCursor::CloneGDIRefData(const wxGDIRefData *data) const
{
    return new wxCursorRefData(*static_cast<const wxCursorRefData *>(data));
}

wxCursor g_globalCursor;
static wxCursor gs_storedCursor;
static int gs_busyCount = 0;

void wxBeginBusyCursor(const wxCursor *cursor)
{
    if (gs_busyCount++ == 0)
    {
        gs_storedCursor = g_globalCursor;
        wxSetCursor(*cursor);
    }
}

void wxEndBusyCursor()
{
    if (gs_busyCount && --gs_busyCount == 0)
    {
        wxSetCursor(gs_storedCursor);
        gs_storedCursor = wxCursor();
    }
}

bool wxIsBusy()
{
    return gs_busyCount > 0;
}

void wxSetCursor(const wxCursor& cursor)
{
    cursor.Install();
    g_globalCursor = cursor;
}

wxCursor wxGetCursor()
{
    return g_globalCursor;
}
