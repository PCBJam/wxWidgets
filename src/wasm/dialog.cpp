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
// ShowModal() and EndModal() use Emscripten Asyncify to suspend the C++ stack,
// run a JavaScript event loop that processes wxWidgets events via setTimeout,
// and resume when the dialog is closed.

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

// JavaScript function that implements the modal event loop using Asyncify.
// This function:
// 1. Starts a setTimeout-based event loop that calls ProcessEvents
// 2. Returns a Promise that resolves when endModal() is called
// 3. Asyncify suspends the C++ stack until the Promise resolves
//
// Note: When consecutive modals run (e.g. wizard pages), the second modal's
// asyncify operation starts inside the first modal's doRewind. This is an
// inherent limitation of Emscripten's asyncify — the errors are non-fatal
// and both modals complete correctly. The try/catch in the event loop
// prevents cascading errors after the asyncify state corruption.
//
// The setTimeout callback is async because ccall('ProcessEvents', …,
// {async:true}) returns a Promise whenever ProcessEvents asyncify-suspends
// (e.g. a tool coroutine yields).  Without `await`, that Promise rejects
// with the "unwind" sentinel after the callback returns, surfacing as an
// "Uncaught (in promise) unwind" page error in Chrome; with `await`, the
// try/catch sees the rejection and stops the loop cleanly.
EM_ASYNC_JS(int, startModal, (int aCancelCode), {
    var timer = null;
    var stopped = false;
    var tickCount = 0;
    var finish = null;   // resolves THIS modal exactly once

    var runEventLoop = function () {
        if (stopped) return;
        timer = setTimeout(async function () {
            if (stopped) return;
            tickCount++;
            // Scheduler builds (docs/features/async/17 S3): drive ProcessEvents
            // as a PLAIN export call — never `await ccall(...,{async:true})`.
            // JS awaiting a suspending export is the Emscripten #13302
            // corruption boundary (a fiber swap inside the awaited chain
            // traps); a chain that parks completes via its own wake, and
            // ProcessEvents is parked-safe (Paint-only) for overlapping ticks.
            if (globalThis.__wxSchedulerInstalled) {
                try {
                    Module['_ProcessEvents']();
                } catch (e) {
                    console.error('[wxWasm] modal event pump error - cancelling modal: ' + e +
                                  '\nSTACK: ' + (e && e.stack));
                    if (finish) finish(aCancelCode);
                    return;
                }
                if (!stopped) runEventLoop();
                return;
            }
            try {
                await ccall('ProcessEvents', 'void', [], [], { async: true });
            } catch (e) {
                // The pump must NEVER stop without resolving: a stopped pump
                // with an unresolved promise leaves this ShowModal parked
                // forever (silent stall). Cancel the modal instead, loudly.
                console.error('[wxWasm] modal event pump error - cancelling modal: ' + e +
                              '\nSTACK: ' + (e && e.stack));
                if (finish) finish(aCancelCode);
                return;
            }
            if (!stopped) runEventLoop();
        }, 17);
    };

    // EndModal resolves the INNERMOST live modal (LIFO), matching wx modal
    // semantics. The previous single-slot resolver (Module._endModal = fn,
    // delete after use) lost the middle resolver with 3+ nested modals: its
    // EndModal resolved nothing and its ShowModal parked forever.
    Module._wxModalResolvers = Module._wxModalResolvers || [];
    if (typeof Module._endModal !== 'function') {
        Module._endModal = function(code) {
            var stack = Module._wxModalResolvers;
            if (stack && stack.length) {
                (stack.pop())(code);
            } else {
                Module._pendingModalResult = code;
            }
        };
    }

    // EndModal fired before this loop started (stored as pending): consume it.
    if (Module._pendingModalResult !== undefined) {
        var pending = Module._pendingModalResult;
        delete Module._pendingModalResult;
        return pending;
    }

    const result = await new Promise((resolve) => {
        finish = function(code) {
            if (stopped) return;   // resolve exactly once
            stopped = true;
            if (timer !== null) {
                clearTimeout(timer);
                timer = null;
            }
            // Self-cancel paths must remove our own entry (we may not be top
            // of the stack if an inner modal is open above us).
            var idx = Module._wxModalResolvers.indexOf(finish);
            if (idx !== -1) Module._wxModalResolvers.splice(idx, 1);
            resolve(code);
        };
        Module._wxModalResolvers.push(finish);
        runEventLoop();
    });

    return result;
});

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

    m_isShowingModal = true;
    Show(true);

    // Check if EndModal was called during Show(true)
    if ( !m_isShowingModal )
    {
        return GetReturnCode();
    }

    // Call the Asyncify-based modal event loop
    // This suspends the C++ stack until endModal() is called from EndModal()
    //
    // The opener's dispatch chain parks here for the modal's whole lifetime;
    // the modal pump is the legitimate dispatcher meanwhile, so zero the
    // dispatch interlock for the park's duration (manual save/restore:
    // destructors are not reliable across an Asyncify park).
    const int savedDispatchDepth = wxWasmDispatchDepth;
    wxWasmDispatchDepth = 0;
    int result = startModal(wxID_CANCEL);
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

    // Resolve the modal promise via Module._endModal callback.
    // If _endModal isn't set yet (EndModal called before startModal's event
    // loop started), store the result as pending so ShowModal can pick it up.
    EM_ASM({
        if (typeof Module._endModal === 'function') {
            Module._endModal($0);
        } else {
            Module._pendingModalResult = $0;
        }
    }, retCode);

    Show(false);

    if (m_modalCallback)
    {
        auto callback = m_modalCallback;
        m_modalCallback = NULL;
        callback(retCode);
    }
}
