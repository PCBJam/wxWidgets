///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/dispatch.h
// Purpose:     Interlock between concurrent wx event-dispatch chains under
//              Asyncify. WASM port only.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_DISPATCH_H_
#define _WX_WASM_PRIVATE_DISPATCH_H_

// Number of live wx event-dispatch chains: incremented when a fresh JS entry
// (event pump tick, DOM key/mouse/element callback) starts running handlers,
// decremented when that chain completes. A chain that Asyncify-parks inside a
// handler (a library bridge fetch, the clipboard, any EM_ASYNC_JS suspend)
// has NOT completed: its saved C++ stack may be mid-mutation of arbitrary
// widget state, so the count stays held until the park resumes and the chain
// finishes.
//
// While the count is non-zero, no OTHER dispatch chain may start: the event
// pump paints but skips dispatch, and fresh DOM input is queued (posted) for
// the first tick after resume. Interleaving two dispatch chains over the same
// widget tree is how the symbol chooser crashed on CI: the open-libs timer
// was dispatched by the modal pump while the symbol-select chain was parked
// in a bridge fetch, and the timer handler walked half-mutated windows
// ("index out of bounds" wasm trap; see PANEL_SYMBOL_CHOOSER::onOpenLibsTimer
// -> SYMBOL_PREVIEW_WIDGET::SetStatusText -> UpdateChildrenDOMVisibility).
//
// Long-lived "modal" parks (wxDialog::ShowModal, nested wxGUIEventLoop runs,
// DOM popup menus) are the exception: their own pump is the legitimate
// dispatcher while the opener chain is parked, so they zero the count for the
// park's duration and restore it on resume (plain ints, no RAII: destructors
// are not reliable across an Asyncify park).
extern int wxWasmDispatchDepth;

// True when a dispatch chain is live or parked: a would-be fresh dispatch
// must defer (queue/skip) instead of running handlers.
inline bool wxWasmDispatchParked() { return wxWasmDispatchDepth > 0; }

// Scope guard for a dispatch chain. Under Asyncify the destructor runs when
// the chain truly completes (unwind skips it, rewind resumes past it), so
// the count is held across parks - exactly the property the interlock needs.
class wxWasmDispatchGuard
{
public:
    wxWasmDispatchGuard() { ++wxWasmDispatchDepth; }
    ~wxWasmDispatchGuard() { --wxWasmDispatchDepth; }

    wxDECLARE_NO_COPY_CLASS(wxWasmDispatchGuard);
};

#endif // _WX_WASM_PRIVATE_DISPATCH_H_
