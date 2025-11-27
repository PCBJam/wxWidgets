
/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/fontenum.cpp
// Purpose:     wxFontEnumerator
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/fontenum.h"

#ifndef WX_PRECOMP
#endif

//-----------------------------------------------------------------------------
// wxFontEnumerator
//-----------------------------------------------------------------------------
bool wxFontEnumerator::EnumerateFacenames(wxFontEncoding WXUNUSED(encoding),
        bool WXUNUSED(fixedWidthOnly))
{
    // TODO: implement
    wxFAIL_MSG(wxT("EnumerateFacenames is not implemented"));
    return false;
}

bool wxFontEnumerator::EnumerateEncodings(const wxString& WXUNUSED(family))
{
    // TODO: implement
    wxFAIL_MSG(wxT("EnumerateEncodings is not implemented"));
    return false;
}

