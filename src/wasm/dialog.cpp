/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/dialog.cpp
// Purpose:     wxDialog implementation for WASM using JSPI for modal dialogs
// Author:      Robert Roebling, Vaclav Slavik (original univ)
//              Adam Hilss (WASM port), extended for suspending modals
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
//              (c) 2022 Adam Hilss
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// This file provides the complete wxDialog implementation for WASM builds.
// It replaces src/univ/dialog.cpp entirely for WASM because the standard
// wxWidgets event loop approach doesn't work in WASM (JavaScript is
// single-threaded and cannot truly block).
//
// ShowModal() registers a "modal" wait with the scheduler shim and
// JSPI-suspends the C++ stack on it; EndModal() resolves the innermost
// registered wait and the stack resumes (docs/features/async/17 S4). The
// top-level tick is the sole event dispatcher while the modal is open.

// ============================================================================
// declarations
// ============================================================================

// ----------------------------------------------------------------------------
// headers
// ----------------------------------------------------------------------------

// For compilers that support precompilation, includes "wx.h".
#include "wx/wxprec.h"


#include "wx/dialog.h"

#ifndef WX_PRECOMP
    #include "wx/utils.h"
    #include "wx/app.h"
#endif

#include "wx/evtloop.h"
#include "wx/modalhook.h"
#include "wx/wasm/private/dispatch.h"
#include "wx/wasm/private/mailbox.h"
#include "wx/wasm/private/yieldwait.h"

#include <emscripten.h>
#include <cstdio>

//-----------------------------------------------------------------------------
// wxDialog
//-----------------------------------------------------------------------------

wxBEGIN_EVENT_TABLE(wxDialog,wxDialogBase)
    EVT_BUTTON  (wxID_OK,       wxDialog::OnOK)
    EVT_BUTTON  (wxID_CANCEL,   wxDialog::OnCancel)
    EVT_BUTTON  (wxID_APPLY,    wxDialog::OnApply)
    EVT_CLOSE   (wxDialog::OnCloseWindow)
wxEND_EVENT_TABLE()

void wxDialog::Init()
{
    m_returnCode = 0;
    m_windowDisabler = NULL;
    m_eventLoop = NULL;
    m_isShowingModal = false;
    m_modalCallback = NULL;
}

wxDialog::~wxDialog()
{
    // if the dialog is modal, this will end its event loop
    Show(false);

    delete m_eventLoop;
}

bool wxDialog::Create(wxWindow *parent,
                      wxWindowID id, const wxString &title,
                      const wxPoint &pos, const wxSize &size,
                      long style, const wxString &name)
{
    SetExtraStyle(GetExtraStyle() | wxTOPLEVEL_EX_DIALOG);

    // all dialogs should have tab traversal enabled
    style |= wxTAB_TRAVERSAL;

    return wxTopLevelWindow::Create(parent, id, title, pos, size, style, name);
}

void wxDialog::OnApply(wxCommandEvent &WXUNUSED(event))
{
    if ( Validate() )
        TransferDataFromWindow();
}

void wxDialog::OnCancel(wxCommandEvent &WXUNUSED(event))
{
    if ( IsModal() )
    {
        EndModal(wxID_CANCEL);
    }
    else
    {
        SetReturnCode(wxID_CANCEL);
        Show(false);
    }
}

void wxDialog::OnOK(wxCommandEvent &WXUNUSED(event))
{
    if ( Validate() && TransferDataFromWindow() )
    {
        if ( IsModal() )
        {
            EndModal(wxID_OK);
        }
        else
        {
            SetReturnCode(wxID_OK);
            Show(false);
        }
    }
}

void wxDialog::OnCloseWindow(wxCloseEvent& WXUNUSED(event))
{
    // We'll send a Cancel message by default,
    // which may close the dialog.
    // Check for looping if the Cancel event handler calls Close().

    // Note that if a cancel button and handler aren't present in the dialog,
    // nothing will happen when you close the dialog via the window manager, or
    // via Close().
    // We wouldn't want to destroy the dialog by default, since the dialog may have been
    // created on the stack.
    // However, this does mean that calling dialog->Close() won't delete the dialog
    // unless the handler for wxID_CANCEL does so. So use Destroy() if you want to be
    // sure to destroy the dialog.
    // The default OnCancel (above) simply ends a modal dialog, and hides a modeless dialog.

    static wxList s_closing;

    if (s_closing.Member(this))
        return;   // no loops

    s_closing.Append(this);

    wxCommandEvent cancelEvent(wxEVT_BUTTON, wxID_CANCEL);
    cancelEvent.SetEventObject(this);
    GetEventHandler()->ProcessEvent(cancelEvent);
    s_closing.DeleteObject(this);
}

bool wxDialog::Show(bool show)
{
    if ( !show )
    {
        // if we had disabled other app windows, reenable them back now because
        // if they stay disabled Windows will activate another window (one
        // which is enabled, anyhow) and we will lose activation
        wxDELETE(m_windowDisabler);

        if ( IsModal() )
            EndModal(wxID_CANCEL);
    }

    if (show && CanDoLayoutAdaptation())
        DoLayoutAdaptation();

    // Native ports centre dialogs created at wxDefaultPosition (the window
    // manager / CW_USEDEFAULT does it for them); this port maps
    // wxDefaultPosition to a literal (0, 0) (nonownedwnd.cpp) with no platform
    // placement, dropping every unpositioned dialog in the top-left corner.
    // That was masked for years by DIALOG_SHIM::Show unconditionally
    // re-centring while wxDisplay::GetFromWindow() returned wxNOT_FOUND here;
    // with the display lookup fixed, provide the native default ourselves:
    // centre (on the display, matching the old visual behavior) any dialog
    // still at the default spot when first shown. A dialog whose position was
    // restored or set explicitly is not at (0, 0) and is left alone.
    if ( show && !IsShown() && GetPosition() == wxPoint(0, 0) )
        Centre();

    bool ret = wxDialogBase::Show(show);

    if ( show )
        InitDialog();

    return ret;
}

bool wxDialog::IsModal() const
{
    return m_isShowingModal;
}

// ----------------------------------------------------------------------------
// WASM-specific modal implementation using JSPI
// ----------------------------------------------------------------------------

int wxDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();

    if ( IsModal() )
    {
        wxFAIL_MSG( wxT("wxDialog:ShowModal called twice") );
        return GetReturnCode();
    }

    // Use the app's top level window as parent if none given unless explicitly
    // forbidden
    wxWindow * const parent = GetParentForModalDialog();
    if ( parent && parent != this )
    {
        m_parent = parent;
    }

    // The modal is a registered WAIT begun BEFORE Show(true) — an EndModal
    // running synchronously inside Show() resolves the wait early and
    // wxWasmYieldUntil returns immediately (doc 17 S4). No modal pump exists:
    // the top-level tick is the sole dispatcher (dialogs on the DOM port are
    // real HTML, so they render without a paint loop even pre-main-loop).
    const int waitToken = wxWasmBeginWait("modal");

    m_isShowingModal = true;
    Show(true);

    // Suspend the C++ stack until EndModal() resolves it.
    //
    // The opener's dispatch chain suspends here for the modal's whole
    // lifetime; the legitimate dispatcher keeps running meanwhile, so zero
    // the dispatch interlock for that whole span (manual save/restore:
    // wxWasmDispatchRestore centralizes the erased-guard reporting).
    const int savedDispatchDepth = wxWasmDispatchDepth;
    wxWasmDispatchDepth = 0;
    const int result = wxWasmYieldUntil(waitToken);
    wxWasmDispatchRestore(savedDispatchDepth, "ShowModal");

    return result;
}

void wxDialog::ShowModal(std::function<void (int)> callback)
{
    ShowModal();

    m_modalCallback = callback;
}

void wxDialog::EndModal(int retCode)
{
    wxLogDebug(wxT("EndModal: %d"), retCode);

    SetReturnCode(retCode);

    if ( !IsModal() )
    {
        wxFAIL_MSG( wxT("wxDialog:EndModal called twice") );
        return;
    }

    m_isShowingModal = false;

    // Resolve the innermost registered modal wait (wx LIFO semantics). A
    // resolve racing ahead of ShowModal's park pre-resolves the promise.
    wxWasmResolveTopWait("modal", retCode);

    Show(false);

    if (m_modalCallback)
    {
        auto callback = m_modalCallback;
        m_modalCallback = NULL;
        callback(retCode);
    }
}
