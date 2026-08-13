/////////////////////////////////////////////////////////////////////////////
// Name:        src/wasm/dialog.cpp
// Purpose:     wxDialog implementation for WASM using Asyncify for modal dialogs
// Author:      Robert Roebling, Vaclav Slavik (original univ)
//              Adam Hilss (WASM port), extended for Asyncify
// Copyright:   (c) 2001 SciTech Software, Inc. (www.scitechsoft.com)
//              (c) 2022 Adam Hilss
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

// This file provides the complete wxDialog implementation for WASM builds.
// It replaces src/univ/dialog.cpp entirely for WASM because the standard
// wxWidgets event loop approach doesn't work in WASM (JavaScript is
// single-threaded and cannot truly block).
//
// ShowModal() registers a "modal" wait with the injected scheduler shim and
// Asyncify-suspends the C++ stack on it; EndModal() resolves the innermost
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
#include "wx/wasm/private/execution_owner.h"
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
    m_focusBeforeModal.Release();
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
// WASM-specific modal implementation using Asyncify
// ----------------------------------------------------------------------------
// (The legacy startModal event pump and its Module._wxModalResolvers /
// _endModal / _pendingModalResult machinery were deleted at doc 20 D-1: the
// modal is a registered scheduler wait, and no per-modal pump exists.)

namespace
{

// Showing a modal from a mouse/key handler parks that handler before its normal
// post-dispatch paint epilogue can run.  A child frame tick must not compensate
// with wxApp::Paint(): that scans every top-level window and could read the
// parked parent's half-mutated model.  Instead, publish one lease-bound paint
// for the exact dialog.  The zero-delay mailbox supplies a browser-task
// boundary, so a dialog paint which starts a Worker is not nested in the input
// callback which opened it.
void wxWasmPaintShownModal(void *arg)
{
    wxDialog *dialog = static_cast<wxDialog *>(arg);

    if (dialog->IsShown() && dialog->NeedsPaint())
        dialog->HandlePaintRequests();
}

// The dialog remains alive until its exact modal wait resumes.  If close starts
// before this queued paint is admitted, stale-lease cleanup simply drops the
// borrowed pointer; it owns no allocation.
void wxWasmDiscardShownModalPaint(void *)
{
}

} // namespace

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

    if (waitToken <= 0)
        return wxID_CANCEL;

    const wx_wasm_execution::LeaseToken executionLease =
            wxWasmExecutionOpenModalLease(
                    this, wxWasmExecutionScopeForWindow(this),
                    wx_wasm_execution::ModalWorkMask, waitToken);

    if (!executionLease)
        return wxID_CANCEL;

    m_isShowingModal = true;
    m_focusBeforeModal = wxWeakRef<wxWindow>(wxWindow::FindFocus());
    Show(true);

    // Match the generic/native wxDialog contract: a true application-modal
    // dialog disables every other shown top-level window until it closes.
    // The DOM port implements the physical input barrier in wx.js; the normal
    // wxWindow enabled flag remains the source of truth.
    wxASSERT_MSG(!m_windowDisabler, wxT("disabling windows twice?"));
    m_windowDisabler = new wxWindowDisabler(this);
    SetFocus();

    wxWasmMailboxEnqueueAfterScoped(
            &wxWasmPaintShownModal, this, 0,
            wx_wasm_execution::WorkClass::ModalLifecycle,
            wxWasmExecutionScopeForWindow(this),
            &wxWasmDiscardShownModalPaint);

    // Suspend the C++ stack until EndModal() resolves it.
    //
    // The opener's owner remains represented for the modal's whole lifetime;
    // only transactions admitted by the scoped child lease can run meanwhile.
    const int result = wxWasmYieldUntil(waitToken);
    wxWasmExecutionCloseModalLease(this, executionLease);

    wxWindow * const previousFocus = m_focusBeforeModal.get();
    m_focusBeforeModal.Release();
    if (previousFocus && previousFocus->IsShown() && previousFocus->IsEnabled())
        previousFocus->SetFocus();

    return result;
}

void wxDialog::ShowModal(std::function<void (int)> callback)
{
    // The callback overload is a completion API, not an EndModal hook. Invoke
    // it only after the blocking modal call has resumed, closed its execution
    // lease, restored focus, and made this dialog reusable. This also lets a
    // callback safely open another modal without nesting under a half-closed
    // lease.
    const int result = ShowModal();
    if (callback)
        callback(result);
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

    // Revoke new child admission before the exact wait wake can resume the
    // opener. The parent owner itself remains present until ShowModal() and
    // the surrounding handler really return.
    // The exact wait resolves only when this lease has stopped admitting new
    // children and its current child transaction reaches zero references.
    wxWasmExecutionRequestModalClose(this, retCode);

    Show(false);
}
