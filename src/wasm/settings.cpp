/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/settings.cpp
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/log.h"
#include "wx/settings.h"

#ifndef WX_PRECOMP
#endif

static wxFont gs_fontDefault(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

//-----------------------------------------------------------------------------
// wxSystemSettings
//-----------------------------------------------------------------------------

wxColour wxSystemSettingsNative::GetColour(wxSystemColour index)
{
    // Classic light scheme. Default window backgrounds come from
    // wxSYS_COLOUR_BTNFACE — without this, canvas islands and dialog
    // bodies erase to black.
    switch (index)
    {
        case wxSYS_COLOUR_WINDOW:
        case wxSYS_COLOUR_LISTBOX:
        case wxSYS_COLOUR_INFOBK:
        case wxSYS_COLOUR_BTNHIGHLIGHT:   // == wxSYS_COLOUR_3DHILIGHT
            return *wxWHITE;

        case wxSYS_COLOUR_BTNFACE:        // == wxSYS_COLOUR_3DFACE, _FRAMEBK
        case wxSYS_COLOUR_MENU:
        case wxSYS_COLOUR_MENUBAR:
        case wxSYS_COLOUR_SCROLLBAR:
        case wxSYS_COLOUR_ACTIVEBORDER:
        case wxSYS_COLOUR_INACTIVEBORDER:
            return wxColour(212, 208, 200);

        case wxSYS_COLOUR_BTNSHADOW:      // == wxSYS_COLOUR_3DSHADOW
        case wxSYS_COLOUR_GRAYTEXT:
        case wxSYS_COLOUR_INACTIVECAPTION:
            return wxColour(128, 128, 128);

        case wxSYS_COLOUR_HIGHLIGHT:
        case wxSYS_COLOUR_ACTIVECAPTION:
            return wxColour(0, 0, 128);

        case wxSYS_COLOUR_HIGHLIGHTTEXT:
        case wxSYS_COLOUR_CAPTIONTEXT:
            return *wxWHITE;

        default:
            // text colours and everything else
            return *wxBLACK;
    }
}

wxFont wxSystemSettingsNative::GetFont(wxSystemFont WXUNUSED(index))
{
    // TODO: implement
    return gs_fontDefault;
}

int wxSystemSettingsNative::GetMetric(wxSystemMetric WXUNUSED(index), const wxWindow* WXUNUSED(win))
{
    // TODO: implement
    return 0;
}

bool wxSystemSettingsNative::HasFeature(wxSystemFeature index)
{
    switch (index)
    {
        case wxSYS_CAN_ICONIZE_FRAME:
            return false;
        case wxSYS_CAN_DRAW_FRAME_DECORATIONS:
            // Suppresses drawing of frame border and title bar.
            return true;
        default:
            return false;
    }
}
