///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/yieldwait.h
// Purpose:     Scheduler-build token waits: begin → park → resolve.
//              WASM port only.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_YIELDWAIT_H_
#define _WX_WASM_PRIVATE_YIELDWAIT_H_

// The doc-13 §2 yield API (pcbjam docs/features/async/17, step S4), backed by
// the jspi-scheduler.js shim's wait registry (the shim is the only runtime —
// no probe needed). The contract:
//
//   int token = wxWasmBeginWait("modal");   // BEFORE showing/suspending:
//                                           // a resolve racing ahead of the
//                                           // suspend pre-resolves the wait
//   ... show UI / start the async thing ...
//   int result = wxWasmYieldUntil(token);   // suspends THIS chain;
//                                           // scheduler-managed wake
//   ... later, from any JS/C++ path ...
//   wxWasmResolveWait(token, result);       // exact wait
//   wxWasmResolveTopWait("modal", result);  // or innermost of a kind (LIFO)
//
// No per-wait pump exists: the top-level tick is the only dispatcher (it runs
// at any DoRun depth), and the suspended chain's interlock slot is zeroed by
// its caller exactly as before. Implemented in evtloop.cpp.

extern "C" int  wxWasmBeginWait(const char *kind);
extern "C" int  wxWasmYieldUntil(int token);
extern "C" void wxWasmResolveWait(int token, int result);
extern "C" void wxWasmResolveTopWait(const char *kind, int result);

#endif // _WX_WASM_PRIVATE_YIELDWAIT_H_
