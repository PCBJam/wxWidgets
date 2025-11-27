
/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/dcclient.cpp
// Purpose:     wxClientDC implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/wasm/dcclient.h"

#include "wx/app.h"
#include "wx/nonownedwnd.h"
#include "wx/window.h"
#include "wx/wasm/private/display.h"

#include <emscripten.h>

// ----------------------------------------------------------------------------
// wxWindowDCImpl
// ----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxWindowDCImpl, wxWasmDCImpl)

wxWindowDCImpl::wxWindowDCImpl(wxDC *owner, wxWindow *win, bool isClient)
    : wxWasmDCImpl(owner)
{
    wxASSERT(win);

    m_window = win;
    m_contentScaleFactor = wxContentScaleFactor();

    if (win->IsTopLevel())
    {
        if (isClient)
            Create(wxRect(win->GetClientAreaOrigin(), win->GetSize()));
        else
            Create(wxRect(wxPoint(0, 0), win->GetSize()));
    }
    else
    {
        wxRect origRect(win->GetPosition(), win->GetSize());

        wxRect parentRect = origRect;
        wxPoint origin = win->GetClientAreaOrigin();

        if (isClient)
        {
            origRect.width -= origin.x;
            origRect.height -= origin.y;
        }
        else
        {
            origRect.x -= origin.x;
            origRect.y -= origin.y;
        }

        wxRect clipRect = origRect;
        wxWindow* child;
        wxWindow* parent;

        for (child = win, parent = win->GetParent();
             !parent->IsTopLevel();
             child = parent, parent = parent->GetParent())
        {
            parentRect.Offset(-child->GetPosition());
            parentRect.Offset(-child->GetClientAreaOrigin());
            parentRect.SetSize(parent->GetSize());

            wxPoint origin = parent->GetClientAreaOrigin();
            parentRect.width -= origin.x;
            parentRect.height -= origin.y;

            clipRect.Intersect(parentRect);
        }

        parentRect.Offset(-child->GetPosition());
        parentRect.Offset(-child->GetClientAreaOrigin());
        // TODO: we should clip for top-level rect, but wxUniversal renders
        // the menu bar and toolbars in the top-level window's non-client area.
        parentRect.Offset(-parent->GetClientAreaOrigin());

        if (origRect.x == clipRect.x && origRect.y == clipRect.y)
        {
            clipRect.x = origRect.x - parentRect.x;
            clipRect.y = origRect.y - parentRect.y;
        }
        else
        {
            SetDeviceOrigin(origRect.x - clipRect.x, origRect.y - clipRect.y);
            clipRect.x -= parentRect.x;
            clipRect.y -= parentRect.y;
        }

        Create(clipRect);
    }
}

wxWindowDCImpl::~wxWindowDCImpl(void)
{
    EM_ASM({
        destroyWindowContext($0);
    }, GetJavascriptId());
}

void wxWindowDCImpl::Create(const wxRect& rect)
{
    int windowId = m_window->GetTopLevelWindow()->GetCSSId();

    int jsId = EM_ASM_INT({
        return createWindowContext($0, $1, $2, $3, $4, $5);
    }, windowId, rect.x, rect.y, rect.width, rect.height, m_contentScaleFactor);

    SetJavascriptId(jsId);
}

void wxWindowDCImpl::DoGetSize(int *width, int *height) const
{
    wxCHECK_RET(IsOk(), wxT("invalid dc"));

    m_window->GetSize(width, height);
}

// ----------------------------------------------------------------------------
// wxClientDCImpl
// ----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxClientDCImpl, wxWindowDCImpl)

wxClientDCImpl::wxClientDCImpl(wxDC *owner, wxWindow *win)
    : wxWindowDCImpl(owner, win, true)
{
}

wxClientDCImpl::~wxClientDCImpl(void)
{
}

// ----------------------------------------------------------------------------
// wxPaintDCImpl
// ----------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_CLASS(wxPaintDCImpl, wxClientDCImpl)

wxPaintDCImpl::wxPaintDCImpl(wxDC *owner, wxWindow *win)
    : wxClientDCImpl(owner, win)
{
}

wxPaintDCImpl::~wxPaintDCImpl(void)
{
}
