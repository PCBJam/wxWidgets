///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/mailbox.h
// Purpose:     Scheduler mailbox: browser stimuli enqueue, the shim's
//              delivery tick delivers in order. WASM port only.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_MAILBOX_H_
#define _WX_WASM_PRIVATE_MAILBOX_H_

#include "wx/wasm/private/dispatch.h"

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

// Nested delivery: deliver due messages ON BEHALF OF the chain that holds the
// interlock, because that chain asked for it - wxYield(). Native wx's
// wxYield dispatches pending timer events; a handler that spins in
//   while (busy) { wxYield(); wxMilliSleep(1); }
// (KiCad's TOOL_MANAGER::RunSynchronousAction: the paste-move) is parked here
// for the whole interaction and, without this, its own refresh/auto-pan
// timers and any wheel ticks stayed queued until it finished (the pasted item
// was moved to the cursor in the model but not DRAWN until a mouse move forced
// a synchronous repaint). Called from wxGUIEventLoop::DoYieldFor ONLY for that
// spin signature: the yield asks for timer events AND the calling chain has
// slept (wxWasmNoteSleep, from the main-thread nanosleep shim) at this
// dispatch depth since the last such delivery. Any other yield - boot-time
// yields inside frame creation, wxProgressDialog updates (UI|USER_INPUT),
// even a bare wxEVT_CATEGORY_TIMER yield that KiCad's symbol-editor boot
// issues at depth 1 with timers pending - leaves the mailbox alone: a timer
// handler run nested there hung the boot (staging CI 2026-09-16, three of
// three runs on the mask-only gate).
extern "C" void wxWasmMailboxDeliverNested();

// Set by the main-thread sleep shim (pcbjam wasm/shims/nanosleep_yield.c,
// weak-linked): the dispatch depth at which the current chain last slept, or
// -1. The first DoYieldFor after it consumes it (matching or not), so only a
// yield immediately following a sleep qualifies; a chain that yields without
// sleeping between yields never matches.
extern int wxWasmMailboxSleptDepth;
extern "C" void wxWasmNoteSleep();

// Interlock depth at which the running nested delivery was started, or -1
// when none is active. A message handler that would otherwise defer on
// wxWasmDispatchParked() lets a nested delivery through when the depth is
// exactly that baseline: the parked chain is the caller. Once a delivered
// handler suspends, its own guard raises the depth above the baseline, so
// a fresh browser entry arriving meanwhile still defers - same rule as
// everywhere else.
extern int wxWasmMailboxNestedBaseline;

inline bool wxWasmMailboxMustDefer()
{
    return wxWasmDispatchParked() && wxWasmDispatchDepth != wxWasmMailboxNestedBaseline;
}

#endif // _WX_WASM_PRIVATE_MAILBOX_H_
