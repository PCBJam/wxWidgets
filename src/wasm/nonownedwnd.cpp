/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/nonownedwnd.cpp
// Purpose:     wxNonOwnedWindow implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/app.h"
#include "wx/nonownedwnd.h"
#include "wx/wasm/private.h"
#include "wx/wasm/private/display.h"

#include <emscripten.h>

void wxNonOwnedWindow::Init()
{
    m_cssId = wxID_NONE;
}

wxNonOwnedWindow::~wxNonOwnedWindow()
{
    if (m_cssId != wxID_NONE)
    {
        EM_ASM({
            destroyWindow($0);
        }, m_cssId);
    }
}

bool wxNonOwnedWindow::Create(wxWindow *parent,
                              wxWindowID id,
                              const wxPoint& pos,
                              const wxSize& size,
                              long style,
                              const wxString& name)
{
    wxString classList = GetCSSClassList();

    m_cssId = EM_ASM_INT({
        return createWindow(-1, true, $0, UTF8ToString($1));
    }, m_isShown, static_cast<const char*>(classList.utf8_str()));

    int x = pos.x;
    int y = pos.y;

    if (x == wxDefaultCoord)
    {
        x = 0;
    }
    if (y == wxDefaultCoord)
    {
        y = 0;
    }

    int width = WidthDefault(size.x);
    int height = HeightDefault(size.y);

    if (!wxNonOwnedWindowBase::Create(parent, id, wxPoint(x, y), wxSize(width, height), style, name))
    {
        wxFAIL_MSG(wxT("wxTopLevelWindowWasm creation failed"));
        return false;
    }

    wxTopLevelWindows.Append(this);

    //printf("CreateWindow: %d\n", m_cssId);

    return true;
}

void wxNonOwnedWindow::SetSizer(wxSizer *sizer, bool deleteOld)
{
    wxWindow::SetSizer(sizer, deleteOld);
    Layout();
}

void wxNonOwnedWindow::DoSetSize(int x, int y,
                                 int width, int height,
                                 int sizeFlags)
{
    //printf("DoSetSize: %d, %d, %d, %d, %d\n", GetCSSId(), x, y, width, height);

    wxRect oldRect = GetScreenRect();
    wxNonOwnedWindowBase::DoSetSize(x, y, width, height, sizeFlags);
    wxRect newRect = GetScreenRect();

    if (newRect != oldRect)
    {
        EM_ASM({
            return setWindowRect($0, $1, $2, $3, $4);
        }, GetCSSId(), newRect.x, newRect.y, newRect.width, newRect.height);
    }
}

bool wxNonOwnedWindow::Show(bool show)
{
    bool ret = wxNonOwnedWindowBase::Show(show);
    if (ret && !IsMainFrame())
    {
        EM_ASM({
            setWindowVisibility($0, $1);
        }, GetCSSId(), show);
    }

    return ret;
}

void wxNonOwnedWindow::Raise()
{
    EM_ASM({
        raiseWindow($0);
    }, GetCSSId());

    for (wxWindowList::iterator windowIter = wxTopLevelWindows.begin();
         windowIter != wxTopLevelWindows.end();
         ++windowIter)
    {
        wxWindow *window = *windowIter;
        if (window->GetParent() != NULL &&
            window->GetParent()->GetTopLevelWindow() == this)
        {
            window->Raise();
        }
    }
}

void wxNonOwnedWindow::Lower()
{
    for (wxWindowList::iterator windowIter = wxTopLevelWindows.begin();
         windowIter != wxTopLevelWindows.end();
         ++windowIter)
    {
        wxWindow *window = *windowIter;
        if (window->GetParent() != NULL &&
            window->GetParent()->GetTopLevelWindow() == this)
        {
            window->Lower();
        }
    }

    EM_ASM({
        lowerWindow($0);
    }, GetCSSId());
}

void wxNonOwnedWindow::HandlePaintRequests()
{
    if (NeedsPaint())
    {
        DoPaint(false);
    }
}

bool wxNonOwnedWindow::IsMainFrame() const
{
    return !wxTopLevelWindows.IsEmpty() && this == wxTopLevelWindows[0];
}
