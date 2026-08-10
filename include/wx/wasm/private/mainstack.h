///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/mainstack.h
// Purpose:     Hook for moving a blocking call onto the main stack.
//              WASM port only.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_MAINSTACK_H_
#define _WX_WASM_PRIVATE_MAINSTACK_H_

// A nested (quasi-modal) event loop parks its whole stack for the dialog's
// lifetime. Doing that on a COROUTINE's stack strands the coroutine: the park
// suspends the fiber's body somewhere the fiber layer cannot see, so the
// stale-fiber guard quarantines the fiber and then refuses its own resume, and
// the dialog stops responding to clicks entirely (pcbjam
// docs/features/async/19).
//
// wx cannot know what a coroutine is here — that is the host application's
// concept (for KiCad, TOOL_MANAGER's tool coroutines). So the host registers a
// runner, and wx calls it before parking a nested loop that is standing on a
// non-main stack. The runner must execute aFunc on the main stack and return
// non-zero; returning zero means "not applicable", and wx parks in place as
// before.
//
// Unregistered (plain wx apps, the test harnesses) every nested loop simply
// parks where it stands, which is the pre-existing behaviour.

extern "C" {

typedef int (*wxWasmMainStackRunner)(void (*aFunc)(void *), void *aArg);

void wxWasmSetMainStackRunner(wxWasmMainStackRunner aRunner);

}  // extern "C"

#endif // _WX_WASM_PRIVATE_MAINSTACK_H_
