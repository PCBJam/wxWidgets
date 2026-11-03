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

#if wxUSE_FILEDLG
    #include "wx/arrstr.h"
    #include "wx/button.h"
    #include "wx/filedlg.h"
    #include "wx/generic/filectrlg.h"
    #include "wx/sizer.h"
    #include "wx/translation.h"
    #include "wx/weakref.h"
    #include <emscripten.h>
    #include <iterator>
    #include <map>
#endif

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

#if wxUSE_FILEDLG

// ----------------------------------------------------------------------------
// "Upload from this computer..." in the generic file-open dialog
// ----------------------------------------------------------------------------
//
// The generic dialog browses MEMFS only, so a file on the user's disk can be
// neither opened, imported nor embedded. Every wxFD_OPEN dialog gets a button
// that opens the browser's own file picker; the picked file is written to
// wxWASM_UPLOAD_DIR, selected, and the dialog closes as if OK had been
// pressed on it. The picker opens from a JS listener on the button's own
// element, inside the trusted click, as every browser requires. A
// wxFD_MULTIPLE dialog takes several files in one pick: they are selected in
// the dialog's file list, which GetPaths() returns once no name is typed.

namespace
{

const char *const wxWASM_UPLOAD_DIR = "/tmp/uploads";

std::map<int, wxWeakRef<wxGenericFileDialog> > s_uploadDialogs;
int s_uploadSeq = 0;

// The picker's accept list from a wx wildcard ("Fonts|*.ttf;*.otf|All|*.*"):
// empty when any filter takes every file.
wxString UploadAccept(const wxString& wildcard)
{
    const wxArrayString parts = wxSplit(wildcard, '|');
    wxArrayString exts;

    for ( size_t i = 1; i < parts.size(); i += 2 )
    {
        for ( wxString pattern : wxSplit(parts[i], ';') )
        {
            pattern.Trim().Trim(false);

            if ( pattern == "*" || pattern == "*.*" )
                return wxString();

            if ( pattern.StartsWith("*.") && exts.Index(pattern.Mid(1), false) == wxNOT_FOUND )
                exts.push_back(pattern.Mid(1));
        }
    }

    return wxJoin(exts, ',');
}

} // anonymous namespace

// clang-format off
EM_JS(void, wxWasmWireUploadButton, (int domId, int token, const char *accept, const char *dir, int multiple), {
    const el = window.wxDomControls && window.wxDomControls.get(domId);
    if (!el)
        return;
    const acceptAttr = UTF8ToString(accept);
    const target = UTF8ToString(dir);
    el.addEventListener('click', function () {
        const input = document.createElement('input');
        input.type = 'file';
        input.multiple = !!multiple;
        if (acceptAttr)
            input.accept = acceptAttr;
        input.addEventListener('change', function () {
            const files = Array.prototype.slice.call(input.files || []);
            if (!files.length)
                return;
            Promise.all(files.map(function (file) {
                return file.arrayBuffer();
            })).then(function (bufs) {
                FS.mkdirTree(target);
                // No path separators in the MEMFS names (no backslash escapes in EM_JS),
                // so '/' can join them for the call.
                const names = files.map(function (file, i) {
                    const name = file.name.split('/').join('_').split(String.fromCharCode(92)).join('_');
                    FS.writeFile(target + '/' + name, new Uint8Array(bufs[i]));
                    return name;
                });
                Module['ccall']('wxWasmFileDialogUploaded', null, ['number', 'string'], [token, names.join('/')]);
            }).catch(function (e) {
                console.error('[wx] upload failed: ' + e);
            });
        });
        input.click();
    });
});
// clang-format on

// Selects the uploaded files in the dialog's file list, with no typed name
// left, so GetPaths() returns all of them.
static void wxWasmSelectUploads(wxGenericFileDialog *dlg, const wxArrayString& files)
{
    dlg->SetFilename(wxString()); // clears the typed name and the selection

    for ( wxWindow *child : dlg->GetChildren() )
    {
        wxGenericFileCtrl *ctrl = wxDynamicCast(child, wxGenericFileCtrl);

        if ( !ctrl )
            continue;

        wxFileListCtrl *list = ctrl->GetFileList();

        // Each selection event types that file's name, and clears it again
        // once more than one is selected.
        for ( long item = 0; item < list->GetItemCount(); ++item )
        {
            if ( files.Index(list->GetItemText(item), true) != wxNOT_FOUND )
                list->SetItemState(item, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        }

        return;
    }
}

// JS -> wasm once the picked files are in MEMFS, their names joined by '/'.
// A fresh entry while the modal's chain is parked, so it only queues; the
// selection and the OK run on the dispatcher.
extern "C" EMSCRIPTEN_KEEPALIVE void wxWasmFileDialogUploaded(int token, const char *names)
{
    const wxArrayString files = wxSplit(wxString::FromUTF8(names), '/', '\0');

    wxTheApp->CallAfter([token, files]()
    {
        const auto it = s_uploadDialogs.find(token);

        if ( files.empty() || it == s_uploadDialogs.end() || !it->second
             || !it->second->IsModal() )
        {
            return;
        }

        wxGenericFileDialog *dlg = it->second;
        dlg->SetDirectory(wxWASM_UPLOAD_DIR);

        if ( files.size() == 1 )
            dlg->SetFilename(files[0]);
        else
            wxWasmSelectUploads(dlg, files);

        wxCommandEvent ok(wxEVT_BUTTON, wxID_OK);
        ok.SetEventObject(dlg);
        dlg->GetEventHandler()->ProcessEvent(ok);
    });
}

static void wxWasmAddUploadButton(wxDialog *dialog)
{
    wxGenericFileDialog *dlg = wxDynamicCast(dialog, wxGenericFileDialog);

    if ( !dlg || !dlg->HasFdFlag(wxFD_OPEN) || !dlg->GetSizer()
         || dlg->FindWindow("wxWasmUpload") )
    {
        return;
    }

    wxButton *upload = new wxButton(dlg, wxID_ANY, _("Upload from this computer..."),
                                    wxDefaultPosition, wxDefaultSize, 0,
                                    wxDefaultValidator, "wxWasmUpload");

    // Above the OK/Cancel row, the generic dialog's last sizer item.
    wxSizer *sizer = dlg->GetSizer();
    sizer->Insert(sizer->GetItemCount() - 1, upload,
                  wxSizerFlags().Left().Border(wxLEFT | wxRIGHT, 10));
    sizer->Fit(dlg);

    for ( auto it = s_uploadDialogs.begin(); it != s_uploadDialogs.end(); )
        it = it->second ? std::next(it) : s_uploadDialogs.erase(it);

    const int token = ++s_uploadSeq;
    s_uploadDialogs[token] = dlg;
    wxWasmWireUploadButton(upload->WasmGetDomId(), token,
                           UploadAccept(dlg->GetWildcard()).utf8_str(), wxWASM_UPLOAD_DIR,
                           dlg->HasFdFlag(wxFD_MULTIPLE));
}

#endif // wxUSE_FILEDLG

int wxDialog::ShowModal()
{
    WX_HOOK_MODAL_DIALOG();

    if ( IsModal() )
    {
        wxFAIL_MSG( wxT("wxDialog:ShowModal called twice") );
        return GetReturnCode();
    }

    // Never re-point m_parent at GetParentForModalDialog() (wxUniv does; the
    // native ports don't). The dialog stays in its creation parent's child
    // list, so a dialog created on a control inside another dialog (KiCad's
    // Set Hotkey prompt, parented to the hotkey list) would be unlinked from
    // the WRONG parent on destruction, leaving a dangling child: the next
    // mouse hit-test (wxFindWindowAtPoint) and the outer dialog's teardown
    // then trap on the freed window.

    // The modal is a registered WAIT begun BEFORE Show(true) — an EndModal
    // running synchronously inside Show() resolves the wait early and
    // wxWasmYieldUntil returns immediately (doc 17 S4). No modal pump exists:
    // the top-level tick is the sole dispatcher (dialogs on the DOM port are
    // real HTML, so they render without a paint loop even pre-main-loop).
    const int waitToken = wxWasmBeginWait("modal");

    // Token 0 = the scheduler refused the wait (dead or terminal instance):
    // never show a modal that no EndModal can ever dismiss. Cancel matches
    // the containment convention (wxWasmExitNestedLoop / the shim's error
    // containment both resolve modals with wxID_CANCEL).
    if (waitToken <= 0)
    {
        SetReturnCode(wxID_CANCEL);
        return wxID_CANCEL;
    }

#if wxUSE_FILEDLG
    wxWasmAddUploadButton(this);
#endif

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
