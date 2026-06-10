/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/frame.cpp
// Purpose:     wxFrame implementation for the WASM DOM port
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"

#include "wx/frame.h"

#ifndef WX_PRECOMP
    #include "wx/menu.h"
    #include "wx/toolbar.h"
    #include "wx/statusbr.h"
#endif

// Note: the wxClassInfo for wxFrame is implemented centrally in
// src/common/framecmn.cpp, so no wxIMPLEMENT_DYNAMIC_CLASS here.

wxFrame::wxFrame()
{
}

wxFrame::wxFrame(wxWindow *parent,
                 wxWindowID id,
                 const wxString& title,
                 const wxPoint& pos,
                 const wxSize& size,
                 long style,
                 const wxString& name)
{
    Create(parent, id, title, pos, size, style, name);
}

bool wxFrame::Create(wxWindow *parent,
                     wxWindowID id,
                     const wxString& title,
                     const wxPoint& pos,
                     const wxSize& size,
                     long style,
                     const wxString& name)
{
    if (!wxTopLevelWindow::Create(parent, id, title, pos, size, style, name))
        return false;

    // The bars are stored by wxFrameBase (SetMenuBar/SetToolBar/SetStatusBar).
    // TODO(dom-phase-2): position the bars and account for them in
    // DoGetClientSize()/DoSetClientSize().

    return true;
}
