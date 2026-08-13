///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/mailbox.h
// Purpose:     Scheduler-build mailbox: browser stimuli enqueue, the event
//              pump delivers from a clean dispatch context. WASM port only.
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_MAILBOX_H_
#define _WX_WASM_PRIVATE_MAILBOX_H_

#include "wx/wasm/private/execution_owner.h"

#include <cstdint>

using wxWasmMailboxTimerId = std::uint32_t;

// The mailbox front-end of the asyncify scheduler (pcbjam docs/features/
// async/17, step S1). The injected shim (scripts/common/shims/
// asyncify-scheduler.js) provides a JS-side FIFO; deferred browser callbacks
// (today: wx timers, replayed wheel ticks) are pushed there on expiry instead
// of entering the wasm as their own fresh dispatch chain. A plain mailbox
// tick transports due messages into the typed execution-owner queue; each
// callback runs later only after its own class and target scope are admitted.
//
// The shim is the ONLY runtime (the legacy opt-out was deleted at doc 20
// D-1); a glue without it is a broken build, caught by the fail-fast probe
// at main-loop entry (evtloop.cpp).

// Enqueue `fn(arg)` for delivery no sooner than `millisecs` from now, from
// a fresh scheduler task. Drop-in replacement for the delivery half of
// emscripten_async_call: the callback is staged as Ordinary work instead of
// entering wasm directly from setTimeout. This unscoped compatibility form
// intentionally does not expose the cancellation identity.
extern "C" void wxWasmMailboxEnqueueAfter(void (*fn)(void *), void *arg,
                                          int millisecs);

// Typed messages capture their exact active lease, if any, when scheduled and
// carry that opaque native token across the browser delay. They are staged
// into the central execution-owner queue when the delay expires. The mailbox
// tick itself is transport/control work; it never gives every message one
// blanket PendingEvents capability or rebinds an old message to a newer lease.
wxWasmMailboxTimerId wxWasmMailboxEnqueueAfterScoped(
        void (*fn)(void *), void *arg, int millisecs,
        wx_wasm_execution::WorkClass workClass,
        wx_wasm_execution::ScopeToken targetScope = {},
        wx_wasm_execution::DiscardCallback discard = nullptr,
        wx_wasm_execution::CoalesceClass coalesce =
                wx_wasm_execution::CoalesceClass::None);

// Revoke one exact delayed message. This succeeds while the message is still
// a browser timer or a due JS-mailbox record. It returns false after native
// transport has popped the message and taken responsibility for its discard.
bool wxWasmMailboxCancel(wxWasmMailboxTimerId timerId);

#endif // _WX_WASM_PRIVATE_MAILBOX_H_
