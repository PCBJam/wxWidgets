///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/yieldwait.h
// Purpose:     Scheduler-build token waits: begin → park → resolve.
//              WASM port only.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_YIELDWAIT_H_
#define _WX_WASM_PRIVATE_YIELDWAIT_H_

// The doc-13 §2 yield API (pcbjam docs/features/async/17, step S4), backed by
// the injected scheduler's wait registry (the shim is the only runtime since
// doc 20 D-1 — no probe needed). The contract:
//
//   int token = wxWasmBeginWait("modal");   // BEFORE showing/parking:
//                                           // a resolve racing ahead of the
//                                           // park pre-resolves the promise
//   ... show UI / start the async thing ...
//   int result = wxWasmYieldUntil(token);   // asyncify-parks THIS chain;
//                                           // scheduler-managed wake
//   ... later, from any JS/C++ path ...
//   wxWasmResolveWait(token, result);       // exact wait
//
// No per-wait pump exists: the top-level tick is the only dispatcher. The
// parked parent owner remains represented, while an explicit modal/nested
// lease admits only its scoped child work. Implemented in evtloop.cpp.

extern "C" int  wxWasmBeginWait(const char *kind);
extern "C" int  wxWasmYieldUntil(int token);
extern "C" bool wxWasmResolveWait(int token, int result);

#endif // _WX_WASM_PRIVATE_YIELDWAIT_H_
