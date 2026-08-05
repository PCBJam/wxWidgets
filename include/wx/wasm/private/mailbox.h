///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/mailbox.h
// Purpose:     Scheduler-build mailbox: browser stimuli enqueue, the event
//              pump delivers from a clean dispatch context. WASM port only.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_MAILBOX_H_
#define _WX_WASM_PRIVATE_MAILBOX_H_

// The mailbox front-end of the asyncify-scheduler migration (pcbjam
// docs/features/async/17, step S1). On WX_SCHEDULER=1 builds the injected
// shim (scripts/common/shims/asyncify-scheduler.js) provides a JS-side FIFO;
// deferred browser callbacks (today: wx timers) are pushed there on expiry
// instead of entering the wasm as their own fresh dispatch chain. The event
// pump delivers queued messages only when the dispatch interlock is free, so
// a message can never land on a parked chain — the wait that timer.cpp's
// 17 ms retry loop implements today becomes "the message sits in the queue".
//
// On legacy builds (no shim injected) every function below is an inert no-op
// / returns 0, and callers fall back to the pre-mailbox path. The legacy
// behavior stays byte-for-byte identical — doc 17's dual-glue contract.

// True on WX_SCHEDULER=1 builds (the shim's marker is present in this JS
// context). Cached after the first call: the shim is installed at glue load,
// before any wx code runs.
extern "C" int wxWasmMailboxEnabled();

// Enqueue `fn(arg)` for delivery no sooner than `millisecs` from now, from
// the event pump's clean stack. Drop-in replacement for the delivery half of
// emscripten_async_call: same context, same exactly-once contract, but the
// call is made by wxWasmMailboxDeliver() instead of a bare setTimeout entry.
// Must only be called when wxWasmMailboxEnabled() is true.
extern "C" void wxWasmMailboxEnqueueAfter(void (*fn)(void *), void *arg,
                                          int millisecs);

// Deliver due messages. No-op on legacy builds or while the dispatch
// interlock is held; stops early if a delivered handler parks (the remainder
// stays queued for the next pump tick). Delivers at most the messages
// pending on entry, so a self-re-arming timer cannot starve the paint that
// follows in ProcessEvents.
extern "C" void wxWasmMailboxDeliver();

#endif // _WX_WASM_PRIVATE_MAILBOX_H_
