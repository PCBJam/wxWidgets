///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/mailbox.h
// Purpose:     Scheduler-build mailbox: browser stimuli enqueue, the event
//              pump delivers from a clean dispatch context. WASM port only.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_MAILBOX_H_
#define _WX_WASM_PRIVATE_MAILBOX_H_

// The mailbox front-end of the asyncify scheduler (pcbjam docs/features/
// async/17, step S1). The injected shim (scripts/common/shims/
// asyncify-scheduler.js) provides a JS-side FIFO; deferred browser callbacks
// (today: wx timers, replayed wheel ticks) are pushed there on expiry instead
// of entering the wasm as their own fresh dispatch chain. The event pump
// delivers queued messages only when the dispatch interlock is free, so a
// message can never land on a parked chain.
//
// The shim is the ONLY runtime (the legacy opt-out was deleted at doc 20
// D-1); a glue without it is a broken build, caught by the fail-fast probe
// at main-loop entry (evtloop.cpp).

// Enqueue `fn(arg)` for delivery no sooner than `millisecs` from now, from
// the event pump's clean stack. Drop-in replacement for the delivery half of
// emscripten_async_call: same context, same exactly-once contract, but the
// call is made by wxWasmMailboxDeliver() instead of a bare setTimeout entry.
extern "C" void wxWasmMailboxEnqueueAfter(void (*fn)(void *), void *arg,
                                          int millisecs);

// Deliver due messages. No-op while the dispatch interlock is held; stops
// early if a delivered handler parks (the remainder stays queued for the
// next pump tick). Delivers at most the messages pending on entry, so a
// self-re-arming timer cannot starve the paint that follows in
// ProcessEvents.
extern "C" void wxWasmMailboxDeliver();

#endif // _WX_WASM_PRIVATE_MAILBOX_H_
