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

// Abandon every held chain: the count drops to zero and dispatch reopens.
//
// A chain that dies ABNORMALLY - a wasm trap, or an Emscripten abort() such as
// the "cannot start an async operation when one is already in flight" assert a
// park inside a non-async ccall raises - never runs its guard destructor, so
// its count would be held forever and every later event would defer against a
// chain that is already gone (input wedged for good). The JS entry points that
// catch such a failure know the chain is dead and call this, exported as
// wx_dispatch_abandon; see src/wasm/evtloop.cpp and the dispatch() wrapper in
// build/wasm/wx-dom.js.
void wxWasmDispatchAbandon();

// Restore the count at the end of a "modal" park (ShowModal, nested loop run,
// DOM popup menu), REPORTING the accounting anomaly those three sites can hit.
//
// Each does `saved = depth; depth = 0; ...park...; depth = saved`. Any guard
// taken while the count was zeroed is ERASED by that restore, so the interlock
// reads "nothing parked" while a chain is still parked - a fresh dispatch then
// runs handlers over half-mutated widget state, which is the documented cause
// of the "index out of bounds" class of trap. Repeated nesting can also drive
// the count negative.
//
// Neither condition is recoverable here (correcting the count would change
// behaviour, and the right fix is composable save/restore) - this only makes
// the anomaly VISIBLE in a production console log, which is the missing
// evidence for a load-time trap we have never reproduced locally. Rate-limited
// so a pathological load cannot flood the console.
//
// `site` is a static string naming the caller, e.g. "ShowModal".
void wxWasmDispatchRestore(int saved, const char *site);

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
