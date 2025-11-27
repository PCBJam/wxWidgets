/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dcscreen.cpp
// Purpose:     wxScreenDC implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/wasm/dcscreen.h"

#include "wx/app.h"
#include "wx/nonownedwnd.h"
#include "wx/wasm/private/display.h"

#include <emscripten.h>

// ----------------------------------------------------------------------------
// wxScreenDCImpl
// ----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxScreenDCImpl, wxWasmDCImpl)

wxScreenDCImpl::wxScreenDCImpl(wxScreenDC *owner)
    : wxWasmDCImpl(owner)
{
    m_contentScaleFactor = wxContentScaleFactor();

    wxNonOwnedWindow *topWindow = wxTheApp->GetTopWindow()->GetTopLevelWindow();
    wxSize size = topWindow->GetSize();

    int windowId = topWindow->GetCSSId();

    int jsId = EM_ASM_INT({
        return createWindowContext($0, $1, $2, $3, $4, $5);
    }, windowId, 0, 0, size.x, size.y, m_contentScaleFactor);

    SetJavascriptId(jsId);
}

wxScreenDCImpl::~wxScreenDCImpl(void)
{
    EM_ASM({
        destroyWindowContext($0);
    }, GetJavascriptId());
}

void wxScreenDCImpl::DoGetSize(int *width, int *height) const
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    wxTheApp->GetTopWindow()->GetSize(width, height);
}
