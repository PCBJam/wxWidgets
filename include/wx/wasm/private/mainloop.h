/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/mainloop.h
// Purpose:     the detached main loop (pcbjam docs/features/async/22, D5)
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_MAINLOOP_H_
#define _WX_WASM_PRIVATE_MAINLOOP_H_

// D5: the top-level main loop runs on a scheduler context whose per-frame wait
// is a context park, so the MAIN stack is only ever the scheduler's — never
// Asyncify-parked. That requires OnRun (and main()) to RETURN while the app
// keeps running, driven by browser frame ticks; the teardown that wxEntry
// normally runs after OnRun is deferred to the loop context's own exit path.

class WXDLLIMPEXP_FWD_CORE wxApp;

// Hand the main loop to a scheduler context and arm its first pump (a fresh
// JS task, after main() has returned). Returns false if the context could not
// be created — the caller must then run the loop inline, as before D5.

// True from a successful detach until the app really exits. wxEntry reads
// this to skip OnExit + wxEntryCleanup on its return path (the loop context
// runs both when the main loop actually ends).
extern "C" bool wxWasmMainLoopDetached();

#endif // _WX_WASM_PRIVATE_MAINLOOP_H_
