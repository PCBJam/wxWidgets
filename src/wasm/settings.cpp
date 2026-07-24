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

#include <stdlib.h>

static wxFont gs_fontDefault(10, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

//-----------------------------------------------------------------------------
// wxSystemSettings
//-----------------------------------------------------------------------------

namespace {

// Light/dark chrome appearance. Initialized from the PCBJAM_DARK_CHROME env
// var (the embedder sets Module.ENV before main() — an env var rather than a
// DOM probe because main() runs in a pthread worker with no `document`, and
// wasm `environ` lives in shared linear memory so every thread agrees) —
// the very FIRST widget paint matches the shell theme. Flipped at runtime via
// wxWasmSetDarkAppearance + a wxSysColourChangedEvent broadcast (the embedder
// owns that — see the app layer's theme bridge).
// wxSystemAppearance::IsDark() needs no extra work: it compares the WINDOW /
// WINDOWTEXT luminance, which this table inverts.
bool wasmInitialDark()
{
    const char* v = getenv("PCBJAM_DARK_CHROME");
    return v != NULL && v[0] == '1';
}

bool& wasmDarkChrome()
{
    static bool s_dark = wasmInitialDark();
    return s_dark;
}

} // anonymous namespace

extern "C" void wxWasmSetDarkAppearance(bool dark)
{
    wasmDarkChrome() = dark;
}

extern "C" bool wxWasmGetDarkAppearance()
{
    return wasmDarkChrome();
}

wxColour wxSystemSettingsNative::GetColour(wxSystemColour index)
{
    // Default window backgrounds come from wxSYS_COLOUR_BTNFACE — without
    // this, canvas islands and dialog bodies erase to black.
    if (wasmDarkChrome())
    {
        // Dark scheme, anchored on the pcbjam platform's #1a1a2e surface.
        switch (index)
        {
            case wxSYS_COLOUR_WINDOW:
            case wxSYS_COLOUR_LISTBOX:
            case wxSYS_COLOUR_INFOBK:
                return wxColour(30, 30, 44);

            case wxSYS_COLOUR_BTNHIGHLIGHT:   // == wxSYS_COLOUR_3DHILIGHT
                return wxColour(82, 82, 102);

            case wxSYS_COLOUR_BTNFACE:        // == wxSYS_COLOUR_3DFACE, _FRAMEBK
            case wxSYS_COLOUR_MENU:
            case wxSYS_COLOUR_MENUBAR:
            case wxSYS_COLOUR_SCROLLBAR:
            case wxSYS_COLOUR_ACTIVEBORDER:
            case wxSYS_COLOUR_INACTIVEBORDER:
                return wxColour(45, 45, 62);

            case wxSYS_COLOUR_BTNSHADOW:      // == wxSYS_COLOUR_3DSHADOW
            case wxSYS_COLOUR_INACTIVECAPTION:
                return wxColour(18, 18, 28);

            case wxSYS_COLOUR_GRAYTEXT:
                return wxColour(142, 142, 160);

            case wxSYS_COLOUR_HIGHLIGHT:
            case wxSYS_COLOUR_ACTIVECAPTION:
                return wxColour(61, 106, 224);

            case wxSYS_COLOUR_HIGHLIGHTTEXT:
            case wxSYS_COLOUR_CAPTIONTEXT:
                return *wxWHITE;

            default:
                // text colours and everything else
                return wxColour(228, 228, 238);
        }
    }

    // Classic light scheme.
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
