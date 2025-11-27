/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/fontutil.cpp
// Purpose:
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/fontutil.h"

#ifndef WX_PRECOMP
#endif

#include "wx/encinfo.h"

//-----------------------------------------------------------------------------
// wxNativeEncodingInfo
//-----------------------------------------------------------------------------

bool wxNativeEncodingInfo::FromString(const wxString& WXUNUSED(s))
{
    // TODO: implement
    wxFAIL_MSG(wxT("FromString is not implemented"));
    return true;
}

wxString wxNativeEncodingInfo::ToString() const
{
    // TODO: implement
    wxFAIL_MSG(wxT("ToString is not implemented"));
    return wxEmptyString;
}

// ----------------------------------------------------------------------------
// common functions
// ----------------------------------------------------------------------------

bool wxGetNativeFontEncoding(wxFontEncoding WXUNUSED(encoding),
                             wxNativeEncodingInfo *info)
{
    // TODO: implement
    wxFAIL_MSG(wxT("wxGetNativeFontEncoding is not implemented"));
    *info = wxNativeEncodingInfo();

    return true;
}

bool wxTestFontEncoding(const wxNativeEncodingInfo& WXUNUSED(info))
{
    // TODO: implement
    wxFAIL_MSG(wxT("wxTestFontEncoding is not implemented"));
    return false;
}
