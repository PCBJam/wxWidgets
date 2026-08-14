///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/mailbox.h
// Purpose:     Scheduler mailbox: browser stimuli enqueue, the shim's
//              delivery tick delivers in order. WASM port only.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_MAILBOX_H_
#define _WX_WASM_PRIVATE_MAILBOX_H_

// The mailbox front-end of the wx scheduler (pcbjam docs/features/
// async/17, step S1). The shim (scripts/common/shims/jspi-scheduler.js,
// linked as a --pre-js) provides a JS-side FIFO; deferred browser callbacks
// (today: wx timers, replayed wheel ticks) are pushed there on expiry instead
// of entering the wasm as their own fresh dispatch chain. The delivery tick
// delivers queued messages only when the dispatch interlock is free, so a
// message can never interleave with a suspended chain.
//
// The shim is the ONLY runtime; a glue without it is a broken build, caught
// by the fail-fast probe at main-loop entry (evtloop.cpp).

// Enqueue `fn(arg)` for delivery no sooner than `millisecs` from now, from
// the event pump's clean stack. Drop-in replacement for the delivery half of
// emscripten_async_call: same context, same exactly-once contract, but the
// call is made by wxWasmMailboxDeliver() instead of a bare setTimeout entry.
extern "C" void wxWasmMailboxEnqueueAfter(void (*fn)(void *), void *arg,
                                          int millisecs);

// Deliver due messages. No-op while the dispatch interlock is held; stops
// early if a delivered handler suspends (the remainder stays queued for the
// next delivery tick). Delivers at most the messages pending on entry, so a
// self-re-arming timer cannot starve the paint that follows in
// ProcessEvents.
extern "C" void wxWasmMailboxDeliver();

#endif // _WX_WASM_PRIVATE_MAILBOX_H_
