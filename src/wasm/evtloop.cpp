/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/evtloop.cpp
// Purpose:     wxGUIEventLoop implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/app.h"
#include "wx/evtloop.h"
#include "wx/init.h"
#include "wx/toplevel.h"
#include "wx/wasm/private/dispatch.h"
#include "wx/wasm/private/mailbox.h"
#include "wx/wasm/private/mainloop.h"
#include "wx/wasm/private/sched_context.h"
#include "wx/wasm/private/yieldwait.h"

#include <emscripten.h>
#include <emscripten/stack.h>
#include <stdio.h>   // printf: diagnostics land in the browser console
#include <string.h>  // strcmp: park-reason comparison

#include <map>
#include <vector>

// Run work on a dispatch context instead of the stack it arrived on (defined
// with the dispatch contexts below; declared here for the entries near the top
// of this file). See its definition for why every entry that can reach a tool
// coroutine must go through the scheduler.
extern "C" void wxWasmRunOnDispatchContext(void (*fn)(void *), void *arg);

// See wx/wasm/private/dispatch.h for the interlock contract.
int wxWasmDispatchDepth = 0;

void wxWasmDispatchAbandon()
{
    wxWasmDispatchDepth = 0;
}

void wxWasmDispatchRestore(int saved, const char *site)
{
    // Guards taken while the count was zeroed are about to be erased: the
    // interlock will read "nothing parked" although `erased` chains still are.
    const int erased = wxWasmDispatchDepth;

    wxWasmDispatchDepth = saved;

    if (erased != 0)
    {
        static int s_erasedCount = 0;
        ++s_erasedCount;
        // Loud for the first few, then sparse: the interesting fact is THAT it
        // happened and how often, not each instance.
        if (s_erasedCount <= 10 || s_erasedCount % 100 == 0)
        {
            printf("[wx-dispatch] ERASED %d held chain(s) restoring depth=%d at %s "
                   "(occurrence %d) - interlock now reads open while a chain is parked\n",
                   erased, saved, site, s_erasedCount);
        }
    }

    if (wxWasmDispatchDepth < 0)
    {
        printf("[wx-dispatch] NEGATIVE depth=%d at %s - accounting is corrupt\n",
               wxWasmDispatchDepth, site);
    }
}

// ----------------------------------------------------------------------------
// Scheduler mailbox (wx/wasm/private/mailbox.h; pcbjam docs/features/
// async/17 S1). The queue itself lives in the injected asyncify-scheduler.js
// shim — this side pushes deferred callbacks and pulls due messages from the
// pump's clean stack. The shim is the ONLY runtime (the legacy opt-out was
// deleted at doc 20 D-1): a glue without it is a broken build, caught loudly
// by wxWasmSchedulerAssertInstalled() at main-loop entry.
// ----------------------------------------------------------------------------

EM_JS(int, wxWasmMailboxJsEnabled, (), {
    return (typeof globalThis !== "undefined" &&
            globalThis.__wxSchedulerInstalled &&
            globalThis.__wxScheduler &&
            globalThis.__wxScheduler.mailbox) ? 1 : 0;
});

// The scheduler shim is injected into every glue by
// scripts/common/inject-dyncall-shims.sh; running without it means the build
// pipeline was skipped and every park/wait/timer lane below would die in
// obscure ways. Fail fast and name the culprit instead.
static void wxWasmSchedulerAssertInstalled()
{
    static bool s_checked = false;
    if (s_checked)
        return;
    s_checked = true;
    if (!wxWasmMailboxJsEnabled())
    {
        printf("[wx-scheduler] FATAL: asyncify-scheduler shim not present in "
               "this glue - run scripts/common/inject-dyncall-shims.sh "
               "(doc 20 D-1: the legacy runtime is gone)\n");
        abort();
    }
}

EM_JS(void, wxWasmMailboxJsEnqueue, (void *fn, void *arg, int ms), {
    globalThis.__wxScheduler.enqueueAfter(fn, arg, ms);
});

EM_JS(int, wxWasmMailboxJsPending, (), {
    return globalThis.__wxScheduler.mailbox.length;
});

// Pop the oldest due message into *fnOut/*argOut; 0 if the queue is empty.
EM_JS(int, wxWasmMailboxJsPop, (void **fnOut, void **argOut), {
    var m = globalThis.__wxScheduler.pop();
    if (!m) return 0;
    HEAPU32[fnOut >> 2] = m.fn;
    HEAPU32[argOut >> 2] = m.arg;
    return 1;
});

extern "C" void wxWasmMailboxEnqueueAfter(void (*fn)(void *), void *arg,
                                          int millisecs)
{
    wxWasmMailboxJsEnqueue(reinterpret_cast<void *>(fn), arg, millisecs);
}

extern "C" void wxWasmMailboxDeliver()
{
    // S6 teardown parity with ProcessEvents: after the main loop exits the
    // app object is being (or has been) destroyed — a queued timer message
    // delivered now calls into freed timer state.
    if (!wxTheApp)
        return;

    // Snapshot the count: a handler that re-arms its timer with delay 0 must
    // not extend this drain unboundedly.
    int budget = wxWasmMailboxJsPending();
    while (budget-- > 0)
    {
        // A delivered handler may itself park (a lib fetch inside a timer
        // handler): its chain then holds the interlock, and delivering more
        // messages would land them on that parked chain — exactly the
        // collision the mailbox exists to prevent. Leave the remainder
        // queued; the JS delivery tick retries after the resume.
        if (wxWasmDispatchParked())
            break;

        void *fn = NULL;
        void *arg = NULL;
        if (!wxWasmMailboxJsPop(&fn, &arg))
            break;
        reinterpret_cast<void (*)(void *)>(fn)(arg);
    }
}

extern "C" {

    // The mailbox's own dispatch entry (docs/features/async/17 S1). Called by
    // the shim's self-armed delivery tick as a PLAIN export call — never from
    // inside a pump's `await ccall('ProcessEvents')`. Delivering from the
    // pump's awaited export put a fiber swap inside the JS-awaits-a-
    // suspending-export boundary (Emscripten #13302) and trapped `unreachable`
    // in the coroutine-nested battery (fiber_create_run_destroy_inside_modal).
    // A fresh sync entry is exactly the context legacy timer callbacks always
    // ran in — the mailbox changes WHEN a message runs (queued, in order,
    // interlock free), not the kind of stack it runs on.
    void EMSCRIPTEN_KEEPALIVE wxWasmMailboxTick()
    {
        // A delivered timer handler can reach a tool coroutine exactly like a
        // DOM event can, so it takes the same route: run it on a dispatch
        // context rather than on the main stack this tick arrived on (doc 22
        // §10 — one entry path per coroutine, or the rewinds do not match).
        wxWasmRunOnDispatchContext([](void *) { wxWasmMailboxDeliver(); }, NULL);
    }

}  // extern "C"

// ----------------------------------------------------------------------------
// Scheduler-build token waits (wx/wasm/private/yieldwait.h; doc 17 S4).
// The wait registry lives in the injected scheduler shim; these are thin
// bridges. wxWasmYieldUntil is the ONE park primitive the migrated waits
// share — a handleSleep the S2 core manages like any other (deferred wakes,
// registry, recorder).
// ----------------------------------------------------------------------------

EM_JS(int, wxWasmBeginWaitJs, (const char *kind), {
    return globalThis.__wxScheduler.beginWait(UTF8ToString(kind));
});

EM_ASYNC_JS(int, wxWasmYieldUntilJs, (int token), {
    return await globalThis.__wxScheduler.waitPromise(token);
});

EM_JS(void, wxWasmResolveWaitJs, (int token, int result), {
    globalThis.__wxScheduler.resolveWait(token, result);
});

EM_JS(void, wxWasmResolveTopWaitJs, (const char *kind, int result), {
    globalThis.__wxScheduler.resolveTopWait(UTF8ToString(kind), result);
});

extern "C" int wxWasmBeginWait(const char *kind)
{
    return wxWasmBeginWaitJs(kind);
}

// Tell the shim that this token's waiter is a scheduler context, so
// resolveWait marks it ready instead of resolving a promise nobody awaits.
EM_JS(void, wxWasmNoteContextWaitJs, (int token), {
    globalThis.__wxScheduler.noteContextWait(token);
});

// Phase E early-resolve window: a bridge whose request settles before the C++
// frame reaches the park (a provider answering from cache, or a test page with
// no provider at all) resolves the wait first. The shim retains such entries
// with the result attached; peek-and-consume here instead of parking a context
// whose wake has already been spent — that park is unresumable by construction.
EM_JS(int, wxWasmWaitEarlyResolvedJs, (int token), {
    return globalThis.__wxScheduler.waitEarlyResolved(token);
});

// The wait's kind, readable only BEFORE the resolve deletes the entry — so it
// is copied out at park time for the per-kind telemetry below.
EM_JS(void, wxWasmWaitKindJs, (int token, char *out, int cap), {
    const e = globalThis.__wxScheduler.waits.get(token);
    stringToUTF8((e && e.kind) || "?", out, cap);
});

namespace {

// Phase E buffer sizing (doc 21 §2b): deepest observed context park per wait
// kind. A new maximum beacons, so each bridge's high-water can be read
// straight out of any suite log instead of being guessed from D1's synthetic
// floor. Fixed table: the kind set is small and known ("lib", "fp-lib", "3d",
// "occ", "ngspice", "modal", "nested", ...).
struct WaitKindHighWater
{
    char kind[16];
    size_t bytes;
};

WaitKindHighWater s_waitKindHw[16];

void noteWaitKindParkUse(const char *kind, size_t used)
{
    if (!used || !kind || !*kind)
        return;

    for (auto &slot : s_waitKindHw)
    {
        if (slot.kind[0] == '\0')
        {
            snprintf(slot.kind, sizeof(slot.kind), "%s", kind);
        }
        else if (strcmp(slot.kind, kind) != 0)
        {
            continue;
        }

        if (used > slot.bytes)
        {
            slot.bytes = used;
            EM_ASM({ console.log("[wx-wait] high-water " + UTF8ToString($0) + "=" + $1 + "B"); },
                   slot.kind, (int) used);
        }
        return;
    }
}

}  // namespace

EM_JS(int, wxWasmTakeWaitResultJs, (int token), {
    return globalThis.__wxScheduler.takeWaitResult(token);
});

namespace
{
// token -> the context parked on it (doc 22 Phase C). Small and short-lived:
// one entry per outstanding wait, erased on resolve.
std::map<int, pcbjam_sched::ContextId> &wxWasmContextWaits()
{
    static std::map<int, pcbjam_sched::ContextId> s_waits;
    return s_waits;
}
}  // namespace

extern "C" int wxWasmYieldUntil(int token)
{
    // Phase C: if this wait is running ON a scheduler context, park THAT
    // context rather than suspending the stack in place. yield_park verifies
    // the caller's frame really lies inside the running context's stack, so a
    // fiber swapped in above it (a tool coroutine) is refused rather than
    // silently saving the wrong stack — and falls back to the Asyncify park,
    // which is exactly the pre-Phase-C behaviour.
    const pcbjam_sched::ContextId self = pcbjam_sched::current();

    if (self && pcbjam_sched::can_yield_here())
    {
        // Already resolved before we could park (Phase E early-resolve
        // window): consume the retained result instead of parking a context
        // nobody will resume. Nothing can interleave between this check and
        // the park below — wasm holds the thread for the whole block.
        if (wxWasmWaitEarlyResolvedJs(token))
            return wxWasmTakeWaitResultJs(token);

        char kind[16];
        wxWasmWaitKindJs(token, kind, sizeof(kind));

        wxWasmContextWaits()[token] = self;
        wxWasmNoteContextWaitJs(token);

        const int result = pcbjam_sched::yield_park("wx-wait");
        wxWasmContextWaits().erase(token);

        // The registry sampled this park's live capture at swap-out; fold it
        // into the per-kind high-water now that we know whose park it was.
        noteWaitKindParkUse(kind, pcbjam_sched::last_park_use_of(self));
        return result;
    }

    return wxWasmYieldUntilJs(token);
}

extern "C" {

    // The shim's callback for a context-parked wait: mark it ready and let the
    // next pump resume it. Never resumes inline — a wake that rewound inside
    // the resolver's own JS turn is the whole class doc 13 §1.4 forbids.
    // The shim's pump entry: resume whatever the registry says is ready, from
    // a fresh JS task. Called after a context wake and by the top-level tick.
    void EMSCRIPTEN_KEEPALIVE wxWasmSchedPump()
    {
        pcbjam_sched::drain_all();
    }

    void EMSCRIPTEN_KEEPALIVE wxWasmSchedResolveContextWait(int token, int result)
    {
        auto &waits = wxWasmContextWaits();
        auto it = waits.find(token);

        if (it == waits.end())
        {
            // A resolve the shim routed here but no context is parked on: the
            // wake is dropped and the waiter (if any) hangs. Every legitimate
            // path registers the token before parking, so this must stay loud.
            EM_ASM({ console.warn("[wx-wait] resolve for unregistered context-wait token " + $0); },
                   token);
            return;
        }

        if (!pcbjam_sched::mark_ready(it->second, result))
        {
            // mark_ready beacons the generic refusal; add the wait identity so
            // a lost wake can be tied back to its token in the log.
            EM_ASM({ console.warn("[wx-wait] mark_ready refused for token " + $0 + " ctx " + $1); },
                   token, (int) it->second);
        }
    }

}  // extern "C"

extern "C" void wxWasmResolveWait(int token, int result)
{
    wxWasmResolveWaitJs(token, result);
}

extern "C" void wxWasmResolveTopWait(const char *kind, int result)
{
    wxWasmResolveTopWaitJs(kind, result);
}

// Ungated dispatch body: used by the pump once the interlock check passed and
// by wxGUIEventLoop::Dispatch()/wxYield, which deliberately dispatch NESTED
// inside a running handler chain (the interlock only forbids interleaving
// with a PARKED chain, not same-stack recursion).
static void wxWasmProcessEventsUngated()
{
    static int counter = 0;

    wxWasmDispatchGuard guard;
    wxTheApp->ProcessPendingEvents();
    wxTheApp->Paint();
    if (counter++ % 3 == 0)
    {
        wxTheApp->ProcessIdle();
    }
}

extern "C" {

    // Called by a JS entry point whose ccall into wx died abnormally (trap or
    // Emscripten abort): that chain's guard destructor never ran, so release
    // the interlock it still holds. Without this the first such failure wedges
    // every later event behind a chain that no longer exists.
    void EMSCRIPTEN_KEEPALIVE wx_dispatch_abandon()
    {
        wxWasmDispatchAbandon();
    }

    // The scheduler's half of the same containment (doc 22 Phase B): an
    // exception escaping a context propagates out THROUGH drain()'s fiber
    // swap, so its post-swap bookkeeping never runs and the registry would
    // refuse every later drain ("transition in flight") — a dead pump, and
    // with it every unresolved wait. Called from the same JS error paths as
    // wx_dispatch_abandon.
    void EMSCRIPTEN_KEEPALIVE wxWasmSchedAbandon()
    {
        pcbjam_sched::abandon_transition();
    }

    void EMSCRIPTEN_KEEPALIVE ProcessEvents()
    {
        if (!wxTheApp)
            return;

        if (wxWasmDispatchParked())
        {
            // Another dispatch chain is Asyncify-parked mid-handler (e.g. a
            // library bridge fetch suspended inside a key handler). Running
            // more handlers now would interleave two C++ stacks over the same
            // widget state. Keep painting so the UI stays live; queued events
            // dispatch on the first tick after the parked chain resumes.
            wxTheApp->Paint();
            return;
        }

        wxWasmProcessEventsUngated();
    }

}  // extern "C"

// ----------------------------------------------------------------------------
// Event loops via Asyncify (top-level and nested quasi-modal)
// ----------------------------------------------------------------------------
//
// Neither loop uses emscripten_set_main_loop's simulate_infinite_loop=1, which throws
// an "unwind" to ABANDON the C++ stack: that throw is fatal under native wasm-EH (the
// compiler's catch_all cleanup pads catch the foreign exception and run destructors
// that tear down the main frame before it paints — docs/features/wasm-exceptions/08+09).
//
//   * top level (DoRun depth 0): a plain C++ while-loop runs ProcessEvents() on the real
//     main C stack and yields ONE animation frame per tick via wxWasmYieldToBrowser (an
//     Asyncify suspend that COMPLETES each frame). Because nothing is permanently
//     suspended, a tool-coroutine fiber swap inside ProcessEvents runs from a clean
//     stack. A permanent handleAsync park here instead aborts coroutine swaps with
//     "cannot stop an async operation in flight" (docs/features/async/13).
//   * nested (DoRun depth >0): a registered "nested" scheduler wait (doc 17 S4) — no
//     pump; the top-level tick keeps dispatching at any depth. ScheduleExit() resolves
//     the innermost wait. The top-level loop instead just sets m_shouldExit.
//     (The legacy wxWasmRunNestedLoop setTimeout pump and its _wxNestedLoopExit
//     resolver stack were deleted at doc 20 D-1.)

// Depth of nested wxGUIEventLoop::DoRun() calls. 0 = none running; 1 = the
// top-level main loop; >1 = a nested (quasi-modal) loop.
static int s_wxRunDepth = 0;

namespace
{

// The MAIN stack's bounds, captured once at top-level DoRun — the one moment
// we are provably standing on it.
//
// They must be captured rather than queried live: emscripten_fiber_swap's
// finishContextSwitch calls emscripten_stack_set_limits with the INCOMING
// fiber's bounds, so emscripten_stack_get_base()/end() always describe
// whatever stack is current, including a coroutine's. Querying them live
// therefore reports "on the main stack" from everywhere and detects nothing —
// which is exactly how a first attempt at this silently did nothing at all.
uintptr_t s_mainStackBase = 0;
uintptr_t s_mainStackEnd = 0;

/** Is the caller's frame OUTSIDE the main stack, i.e. on a fiber? */
bool wxWasmOnCoroutineStack()
{
    if( !s_mainStackBase )
        return false;   // the main loop has not started; nothing else can be running

    char probe = 0;
    const uintptr_t here = reinterpret_cast<uintptr_t>(&probe);
    // The main stack grows down from base to end; a coroutine's stack is a
    // separate allocation, so its frames fall outside that interval.
    return here > s_mainStackBase || here < s_mainStackEnd;
}

extern "C" {

    // Phase E telemetry: the shim's handleSleep wrapper calls this as a leaf
    // probe when a FRESH in-place park starts, counting parks that begin on a
    // non-main stack (a tool coroutine or a scheduler context). Doc 22 §5's
    // Phase E invariant is that this count reaches ZERO at the flip; until
    // then it measures exactly how much in-place-park-on-fiber-stack exposure
    // remains (the doc-19 class). Leaf-safe: called from the import frame
    // before any unwind begins.
    int EMSCRIPTEN_KEEPALIVE wxWasmProbeOnFiberStack()
    {
        return wxWasmOnCoroutineStack() ? 1 : 0;
    }

}

// The nested loop's actual park, extracted so it can run either in place or on
// the main stack. The opener's chain parks here for the dialog's whole
// lifetime; the interlock is zeroed for the park's duration (manual
// save/restore: destructors are not reliable across an Asyncify park) so the
// legitimate dispatcher keeps running meanwhile. The nested loop is a
// registered WAIT, not a pump (doc 17 S4): the top-level tick is the sole
// dispatcher at any depth, and ScheduleExit()/Exit() resolves the innermost
// "nested" wait to resume this stack.
void wxWasmNestedWaitBody(void *)
{
    const int savedDispatchDepth = wxWasmDispatchDepth;
    wxWasmDispatchDepth = 0;
    const int token = wxWasmBeginWait("nested");
    wxWasmYieldUntil(token);   // suspends until resolved
    wxWasmDispatchRestore(savedDispatchDepth, "NestedLoop");
}

}  // namespace

EM_JS(void, wxWasmExitNestedLoop, (), {
    // The nested loop is a registered wait (doc 17 S4).
    globalThis.__wxScheduler.resolveTopWait('nested', 0);
});

// Top-level main loop: yield to the browser for ONE animation frame, then return. It is
// called in a plain C++ while-loop in DoRun (below), so ProcessEvents() runs on the real
// main C stack and each Asyncify suspension COMPLETES every frame — unlike a permanent
// handleAsync park, which is "in flight" for the app's whole life and makes a tool-
// coroutine fiber swap abort ("cannot stop an async operation in flight"). With this
// per-frame yield the slot is free whenever ProcessEvents runs (docs/features/async/13).
EM_ASYNC_JS(void, wxWasmYieldToBrowser, (), {
    await new Promise(function (resolve) { requestAnimationFrame(resolve); });
});

// Deliver the tick's events from a FRESH JS task instead of inline in the main
// loop (docs/features/async/16 round 6). Everything after wxWasmYieldToBrowser()
// returns runs inside that park's synchronous wake continuation, and a coroutine
// resumed there swaps main OUT inside its own live wake: emscripten_fiber_swap
// then stamps the wake's re-invoked __main_argc_argv as the rewind entry of a
// capture that only spans the swap-site frames — unrewindable by construction,
// and the reproduced cause of the production board-load death. Dispatching from
// a fresh entry is exactly what every HEALTHY dispatch already does (the DOM
// handlers and timer callbacks that run while main is parked; the flight
// recorder shows all of them at wake-depth 0). ProcessEvents itself is
// re-entrancy-safe: it no-ops into a repaint whenever a chain is parked.
EM_JS(void, wxWasmScheduleProcessEvents, (), {
    setTimeout(function () {
        try {
            Module["_wxWasmTopLevelTick"]();
        } catch (e) {
            // Mirror the DOM handlers' guard: a trap here would otherwise leave
            // the dispatch interlock held by a chain that no longer exists.
            if (Module["_wx_dispatch_abandon"]) Module["_wx_dispatch_abandon"]();
            // Same for the scheduler: the exception came out through drain()'s
            // fiber swap, so the transition it started is still "in flight"
            // and every later pump would refuse to run (doc 22 Phase B).
            if (Module["_wxWasmSchedAbandon"]) Module["_wxWasmSchedAbandon"]();
            // If a quasi-modal's nested loop is open, tear it down. A throwing
            // handler must not leave the parked nested DoRun unresolved or it
            // never returns — a silent stall (the asyncify-races
            // nested_quasi_modal_pump_error case). The containment releases
            // the innermost registered waits: the nested loop AND the top
            // modal (5101 = wxID_CANCEL).
            if (globalThis.__wxScheduler) {
                globalThis.__wxScheduler.resolveTopWait('nested', 0);
                globalThis.__wxScheduler.resolveTopWait('modal', 5101);
            }
            throw e;
        }
    }, 0);
});

// Defined below with the dispatch context it drives.
extern "C" void wxWasmDispatchOnContext();

// Doc 22 flip staging switch — see wxWasmTopLevelTick.
//
// OFF. At D-on the wx battery is green (395/1, the 1 pre-existing), but the
// KiCad suite loses four canvas-tool specs to `index out of bounds` in
// doRewind — the blue screen itself. Cause (doc 22 §10 "Phase B at D-on"):
// real tool coroutines PARK IN PLACE inside their bodies, and a star transfer
// over an in-place-parked stack rewinds state the fiber layer cannot see.
// The harness never modelled that, which is why it goes green while KiCad
// does not. D turns back on when the tool-body park sites are contexts too
// (C+E completion) — not before.
// THE FLIP (docs/features/async/22 §5, landed 2026-08-08): dispatch contexts,
// context waits and star transfers are ON permanently. Every prior increment
// (D5 main-loop context, DOM entries on dispatch contexts, K1-K7 bridges as
// token waits, gaps 1+2) was gated individually at this setting; the lever
// specs are re-pinned to the post-flip invariant in the same commit.
#define wxWASM_STAR_DISPATCH 1

extern "C" {

    // The top-level loop's scheduled dispatch. A separate entry point from
    // ProcessEvents so the JS side has one obvious name to schedule, and so any
    // future top-level-only policy has a home that the nested pump's direct
    // ProcessEvents calls do not share.
    //
    // Deliberately NOT gated on s_wxRunDepth. The nested pump re-arms only after
    // its awaited ccall returns, so while a long operation is parked inside a
    // quasi-modal loop this tick is the only dispatcher left; refusing to
    // dispatch there risks stalling exactly the loads this change exists to fix.
    // The genuine hazard of running alongside the pump is an error being
    // delivered to the wrong catch, and that is handled where it belongs — the
    // error path in wxWasmScheduleProcessEvents releases the parked nested
    // DoRun, so a throwing handler tears the loop down from either dispatcher.
    void EMSCRIPTEN_KEEPALIVE wxWasmTopLevelTick()
    {
        // Doc 22 flip staging: Phase D (dispatch on a context) is built but
        // held OFF until D5 (the main loop on a context) gates green alone —
        // the measured overlapped-wake came from running both while the main
        // loop still Asyncify-parked per frame. Flip to 1 to re-enable D on
        // top of a proven scheduler-only main stack.
#if wxWASM_STAR_DISPATCH
        wxWasmDispatchOnContext();
#else
        ProcessEvents();
#endif
    }

}  // extern "C"

// ----------------------------------------------------------------------------
// The dispatch contexts (doc 22 Phase D) — an IDLE-REUSE set, bounded by
// nesting depth, not by tick rate.
//
// Every wx handler chain runs here instead of on the main stack, which is what
// lets a wait inside a handler PARK (Phase C) instead of suspending the stack
// the whole runtime stands on.
//
// WHY NOT ONE (measured 2026-08-07, races nested_quasi_modal_pump_error):
// a nested quasi-modal loop is a WAIT that only some LATER dispatch can
// resolve — the pending event that closes the dialog, or the throwing handler
// whose error path releases it. With a single context, the loop's own park
// consumes the only dispatcher, so nothing ever dispatches that event and the
// wait is unresolvable: the app wedges. "A blocked dispatch no longer blocks
// the runtime" only holds if something else can dispatch.
//
// WHY THIS IS NOT D2's POOL (doc 20 §10 — 8 contexts burned in 30 ms): D2 took
// a FRESH context per tick while one sat suspended, so the count grew with the
// tick rate — a leak wearing a cap. Here a tick REUSES any context parked at
// "dispatch-idle" and only creates one when every existing context is parked
// deeper (i.e. inside a wait). A context finishes its ProcessEvents and
// returns to idle as soon as its wait resolves, so the live count is bounded
// by actual modal-nesting depth (1 in steady state, 2-3 under nested dialogs).
//
// ORDERING, load-bearing: this must exist before libcontext adopts its root,
// so the scheduler owns the main stack alone and the root adopts the RUNNING
// context instead. Two emscripten_fiber_t describing the main stack corrupt
// each other on first entry (doc 22 §5, the one-root constraint).
// ----------------------------------------------------------------------------
namespace
{
// Deeper than the scheduler's 128K default: a wx dispatch chain reaches deep
// into KiCad (commit -> connectivity -> font work) before anything parks.
constexpr size_t DISPATCH_STACK_BYTES = 1024 * 1024;
constexpr size_t DISPATCH_ASYNCIFY_BYTES = 512 * 1024;

// Nesting deeper than this is a bug, not a UI: beacon and drop the tick
// rather than allocating without end.
constexpr size_t MAX_DISPATCH_CONTEXTS = 16;

std::vector<pcbjam_sched::ContextId> &wxWasmDispatchContexts()
{
    static std::vector<pcbjam_sched::ContextId> s_contexts;
    return s_contexts;
}

// Work handed to a dispatch context by an entry that arrived on the MAIN
// stack — a DOM event handler, a mailbox timer delivery. See
// wxWasmRunOnDispatchContext for why those may not run where they land.
struct wxWasmDispatchJob
{
    void (*fn)(void *);
    void *arg;
};

std::vector<wxWasmDispatchJob> &wxWasmDispatchJobs()
{
    static std::vector<wxWasmDispatchJob> s_jobs;
    return s_jobs;
}

void wxWasmRunQueuedJobs()
{
    auto &jobs = wxWasmDispatchJobs();

    // Index-based: a job may queue another (a handler that posts an event),
    // and erase-front while running would invalidate the iterator.
    while (!jobs.empty())
    {
        const wxWasmDispatchJob job = jobs.front();
        jobs.erase(jobs.begin());
        job.fn(job.arg);
    }
}

void wxWasmDispatchEntry(void *)
{
    // Never returns: an emscripten fiber entry that returns ends the program.
    for (;;)
    {
        // Handed-off entries first: they are the reason this context exists
        // for anyone but the tick, and they are already ordered.
        wxWasmRunQueuedJobs();
        ProcessEvents();

        // One tick done. Park until the next kick rather than spinning; the
        // scheduler resumes us from a fresh JS task. Parking HERE is what
        // returns this context to the reusable set.
        pcbjam_sched::yield_park("dispatch-idle");
    }
}

/** Is this context ready to take a tick (never entered, or idle)? */
bool wxWasmDispatchAvailable(pcbjam_sched::ContextId id)
{
    const pcbjam_sched::Status st = pcbjam_sched::status_of(id);

    if (st == pcbjam_sched::Status::Fresh)
        return true;

    return st == pcbjam_sched::Status::Parked
           && strcmp(pcbjam_sched::park_reason_of(id), "dispatch-idle") == 0;
}
}  // namespace

extern "C" void wxWasmDispatchOnContext()
{
    if (!wxTheApp)
        return;

    auto &contexts = wxWasmDispatchContexts();

    // Drop contexts poisoned by abandon_transition (a handler died abnormally
    // and left a half-unwound stack): they are Finished and must never be
    // entered again, and keeping them would count against the ceiling.
    for (size_t i = contexts.size(); i-- > 0;)
    {
        if (pcbjam_sched::status_of(contexts[i]) == pcbjam_sched::Status::Finished)
            contexts.erase(contexts.begin() + i);
    }

    // Reuse an idle context if there is one; only allocate when every context
    // we own is parked deeper (inside a wait), which is exactly the nested
    // case that needs an additional dispatcher.
    for (pcbjam_sched::ContextId id : contexts)
    {
        if (!wxWasmDispatchAvailable(id))
            continue;

        if (pcbjam_sched::status_of(id) == pcbjam_sched::Status::Fresh)
            pcbjam_sched::fiber_start(id, 0);
        else
            pcbjam_sched::mark_ready(id, 0);

        pcbjam_sched::drain_all();
        return;
    }

    if (contexts.size() >= MAX_DISPATCH_CONTEXTS)
    {
        printf("[wx-dispatch] %zu dispatch contexts all parked in waits - "
               "dropping this tick (nesting runaway?)\n", contexts.size());
        pcbjam_sched::drain_all();
        return;
    }

    const pcbjam_sched::ContextId fresh = pcbjam_sched::fiber_create(
        wxWasmDispatchEntry, NULL, NULL, DISPATCH_STACK_BYTES,
        DISPATCH_ASYNCIFY_BYTES, "wx-dispatch");

    if (!fresh)
    {
        // Fall back to the pre-Phase-D path rather than losing the tick.
        ProcessEvents();
        return;
    }

    contexts.push_back(fresh);
    pcbjam_sched::fiber_start(fresh, 0);
    pcbjam_sched::drain_all();
}

extern "C" bool wxWasmOnDispatchContext()
{
    const pcbjam_sched::ContextId cur = pcbjam_sched::current();

    if (!cur)
        return false;

    for (pcbjam_sched::ContextId id : wxWasmDispatchContexts())
    {
        if (id == cur)
            return true;
    }

    return false;
}

// ----------------------------------------------------------------------------
// The main-loop context (doc 22 D5).
//
// The top-level loop's per-frame wait used to be an Asyncify park of the MAIN
// stack (wxWasmYieldToBrowser) — doc 21's W2, "safe by construction" only
// while dispatch also ran there. The moment the scheduler swaps contexts from
// a tick, that park and the transitions interleave over one Asyncify slot
// (the measured overlapped-wake). So the loop itself moves onto a context
// whose per-frame wait is yield_park; OnRun and main() RETURN, the runtime
// stays alive (EXIT_RUNTIME=0), and a rAF-armed pump drives the loop from a
// clean main stack that is only ever the scheduler.
// ----------------------------------------------------------------------------
namespace
{
pcbjam_sched::ContextId g_mainLoopContext = 0;
bool g_mainLoopDetached = false;
int g_mainLoopResult = 0;

// The loop context carries DoRun's one-time setup (top-window layout) and the
// whole app teardown at exit, so size it like the dispatch context rather
// than the scheduler default.
constexpr size_t MAIN_LOOP_STACK_BYTES = 1024 * 1024;
constexpr size_t MAIN_LOOP_ASYNCIFY_BYTES = 512 * 1024;

void wxWasmMainLoopEntry(void *arg)
{
    wxApp *app = static_cast<wxApp *>(arg);

    g_mainLoopResult = app->RunMainLoopOnContext();

    // The loop exited: the app really is ending. Run the teardown wxEntry
    // skipped when OnRun detached — OnExit, then the wxUninitialize that
    // releases the pinned init count and performs wxEntryCleanup (which
    // deletes the app; `app` is dangling below this point).
    app->OnExit();
    wxUninitialize();

    // A raw fiber entry must never return (emscripten ends the program), and
    // nothing marks this context ready again, so the park is terminal.
    for (;;)
        pcbjam_sched::yield_park("main-loop-exited");
}
}  // namespace

// One frame's wake: rAF is the same cadence the old in-place park awaited.
// The callback is a fresh JS task, which is exactly what drain_all() requires.
EM_JS(void, wxWasmArmFrameWake, (), {
    requestAnimationFrame(function () {
        Module["_wxWasmMainLoopPump"]();
    });
});

// The FIRST entry must also come from a clean JS task, AFTER main() has
// returned: entering from OnRun's own frame would capture main()/wxEntry
// frames into the scheduler fiber's buffer, and main would then "return"
// inside some later pump's rewind.
EM_JS(void, wxWasmArmMainLoopKick, (), {
    setTimeout(function () {
        Module["_wxWasmMainLoopPump"]();
    }, 0);
});

extern "C" {

    // Enter or resume the main-loop context from a clean stack. Mirrors
    // wxWasmDispatchOnContext's shape: Fresh means first entry, a "frame"
    // park means the per-frame wait — anything else (a wait parked deeper)
    // is left alone and the pump just runs whatever else is ready.
    void EMSCRIPTEN_KEEPALIVE wxWasmMainLoopPump()
    {
        if (!g_mainLoopContext)
            return;

        const pcbjam_sched::Status st = pcbjam_sched::status_of(g_mainLoopContext);

        if (st == pcbjam_sched::Status::Fresh)
            pcbjam_sched::fiber_start(g_mainLoopContext, 0);
        else if (st == pcbjam_sched::Status::Parked
                 && strcmp(pcbjam_sched::park_reason_of(g_mainLoopContext),
                           "frame") == 0)
            pcbjam_sched::mark_ready(g_mainLoopContext, 0);

        pcbjam_sched::drain_all();
    }

}  // extern "C"

// Staging toggle for A/B diagnosis: 0 = run the loop inline (pre-D5 shape).
#define wxWASM_D5_DETACH 1

extern "C" bool wxWasmDetachMainLoop(wxApp *app)
{
    if (!wxWASM_D5_DETACH || g_mainLoopContext || !app)
        return false;

    // Capture the MAIN stack's bounds here: OnRun is the last moment we are
    // provably standing on it. DoRun now runs on the loop context, where a
    // live query would record the CONTEXT's bounds and every later stack
    // classification would silently be wrong (doc 22 §7 trap 2).
    s_mainStackBase = emscripten_stack_get_base();
    s_mainStackEnd = emscripten_stack_get_end();

    g_mainLoopContext = pcbjam_sched::fiber_create(
        wxWasmMainLoopEntry, app, NULL, MAIN_LOOP_STACK_BYTES,
        MAIN_LOOP_ASYNCIFY_BYTES, "wx-main-loop");

    if (!g_mainLoopContext)
        return false;

    g_mainLoopDetached = true;
    wxWasmArmMainLoopKick();
    return true;
}

extern "C" bool wxWasmMainLoopDetached()
{
    return g_mainLoopDetached;
}

extern "C" void wxWasmRunOnDispatchContext(void (*fn)(void *), void *arg)
{
    // WHY THIS EXISTS (doc 22 §10, measured 2026-08-07). A DOM event handler
    // enters wasm on the MAIN stack, and if it dispatches there it reaches a
    // KiCad tool coroutine through libcontext's DIRECT symmetric swap — while
    // the tick reaches that same coroutine through the dispatch context as a
    // STAR TRANSFER. A capture written by one path cannot be rewound by the
    // other: `index out of bounds` in doRewind, on every canvas tool. Every
    // entry into a coroutine must therefore go through the scheduler.
    //
    // The job runs SYNCHRONOUSLY in the common case: drain_all returns once
    // the context parks again, which for a job that does not itself park is
    // after it completed. Callers that need an answer (a key handler's
    // preventDefault) read it from their own job struct; callers whose job
    // parks (a click that opens a modal) get the same "returns while the work
    // continues" semantics the pre-D DOM handlers already had.
    if (!fn)
        return;

#if wxWASM_STAR_DISPATCH
    // Already on a dispatch context: same-stack recursion is what wxYield and
    // nested Dispatch() already do, and it keeps the ordering the caller
    // expects.
    if (!wxTheApp || wxWasmOnDispatchContext())
    {
        fn(arg);
        return;
    }

    // Some OTHER context is what the registry calls running — which happens
    // while a context's stack is Asyncify-parked in place (the bridges, until
    // Phase E). drain() would refuse, so the job would never run: dispatch
    // here instead. This is the narrow mixed-mode window Phase E closes.
    if (pcbjam_sched::current() != 0 || pcbjam_sched::transition_in_flight())
    {
        fn(arg);
        return;
    }

    wxWasmDispatchJobs().push_back({fn, arg});
    wxWasmDispatchOnContext();
#else
    fn(arg);
#endif
}

extern "C" int wxWasmContextWakeIsPumpOwned(unsigned id)
{
    // The main-loop context is resumed by the rAF pump and each dispatch
    // context by the tick; those parks are the pumps' own contract. A second
    // party parking them with its own wake source gives one context two
    // owners — measured 2026-08-07: the main-thread-sleep shim parked the
    // main-loop context, and the frame wake then resumed a capture the sleep
    // wake had already consumed (a doRewind trap arriving through
    // wxWasmArmFrameWake). Contexts listed here must keep sleeping in place;
    // everything else (tool coroutines) may park (wasm/shims/context_sleep.cpp).
    if (!id)
        return 0;

    if (id == g_mainLoopContext)
        return 1;

    for (pcbjam_sched::ContextId dispatchId : wxWasmDispatchContexts())
    {
        if (dispatchId == id)
            return 1;
    }

    return 0;
}

// ----------------------------------------------------------------------------
// wxGUIEventLoop
// ----------------------------------------------------------------------------

void wxGUIEventLoop::ScheduleExit(int WXUNUSED(rc))
{
    wxCHECK_RET( IsInsideRun(), wxT("can't call ScheduleExit() if not started") );

    m_shouldExit = true;

    // The top-level loop is a plain while-loop that checks m_shouldExit (above). A nested
    // (quasi-modal) loop is a registered scheduler wait — resolve it so its DoRun resumes
    // and returns.
    if ( s_wxRunDepth > 1 )
    {
        wxWasmExitNestedLoop();
    }
}

bool wxGUIEventLoop::Pending() const
{
    return wxTheApp && wxTheApp->HasPendingEvents();
}

bool wxGUIEventLoop::Dispatch()
{
    // Ungated on purpose: Dispatch()/wxYield run nested within the calling
    // handler chain (same C++ stack), which the interlock permits.
    if (wxTheApp)
        wxWasmProcessEventsUngated();
    return true;
}

int wxGUIEventLoop::DispatchTimeout(unsigned long WXUNUSED(timeout))
{
    // TODO: implement
    wxFAIL_MSG(wxT("DispatchTimeout is not implemented"));
    return 0;
}

void wxGUIEventLoop::WakeUp()
{
    // noop: browser doesn't block
}

void wxGUIEventLoop::DoYieldFor(long eventsToProcess)
{
    while (Pending())
    {
        Dispatch();
    }

    wxEventLoopBase::DoYieldFor(eventsToProcess);
}

int wxGUIEventLoop::DoRun()
{
    wxASSERT_MSG(IsOk(), wxT("invalid event loop"));

    wxWasmSchedulerAssertInstalled();

    // Record the main stack's bounds so nested loops can later tell whether
    // they are standing somewhere else (see wxWasmOnCoroutineStack). Under D5
    // the detach already captured them in wxWasmDetachMainLoop — top-level
    // DoRun runs on the loop CONTEXT there, so a live query here would record
    // the context's bounds and misclassify every later stack. Only the
    // non-detached fallback still captures here, where depth-0 DoRun really
    // is on the main stack.
    if (s_wxRunDepth == 0 && !s_mainStackBase)
    {
        s_mainStackBase = emscripten_stack_get_base();
        s_mainStackEnd = emscripten_stack_get_end();
    }

    // A nested loop (a quasi-modal dialog opened from a tool) pumps via Asyncify; the
    // first (top-level) DoRun registers the rAF main loop then parks. Neither throws
    // (see the header comment and docs/features/wasm-exceptions/09).
    if (s_wxRunDepth++ > 0)
    {
        // Phase F (docs/features/async/22 §10): the doc-19 mainstack bounce is
        // GONE. Post-flip the wait body's wxWasmYieldUntil parks the OWNING
        // scheduler context — a tool coroutine included (context_sleep set the
        // precedent) — so a nested loop opened from a tool suspends that
        // tool's context through the registry, which is what the bounce
        // approximated from outside.
        wxWasmNestedWaitBody(NULL);

        --s_wxRunDepth;
        return 0;
    }

    if (!wxTopLevelWindows.empty())
    {
        wxWindow *topWindow = wxTopLevelWindows.front();

        int width = EM_ASM_INT({
            return window.innerWidth;
        });
        int height = EM_ASM_INT({
            return window.innerHeight - mainWindow.offsetTop;
        });
        topWindow->SetSize(0, 0, width, height);
        topWindow->Refresh();
    }

    // One tick per animation frame. No throw (fatal under native wasm-EH), and
    // under D5 no Asyncify park of the stack we stand on either: this loop runs
    // on the main-loop CONTEXT, so the per-frame wait is a context park the
    // rAF pump resolves — the main stack stays the scheduler's alone.
    // m_shouldExit, set by ScheduleExit(), ends the loop after the current tick.
    while (!m_shouldExit)
    {
        // Schedule, don't dispatch: see wxWasmScheduleProcessEvents. The tick's
        // events run from a fresh JS task while this loop is parked below, so a
        // tool coroutine resumed by them never swaps main out inside main's own
        // wake continuation.
        //
        // Unconditional at any DoRun depth: nested loops are waits, not pumps
        // (doc 17 S4), so gating on depth would leave a quasi-modal with no
        // dispatcher at all. The June double-driver hazard (§6e) required TWO
        // pumps re-driving one parked context under awaited semantics; with a
        // single plain-call tick and the scheduler's consume-once/deferred-wake
        // guards it is closed.
        wxWasmScheduleProcessEvents();

        if (pcbjam_sched::can_yield_here())
        {
            // D5: arm the next frame's wake, then yield this context to the
            // scheduler. The rAF callback (a fresh JS task) marks us ready and
            // drains — indistinguishable, in this frame, from the old await.
            wxWasmArmFrameWake();
            pcbjam_sched::yield_park("frame");
        }
        else
        {
            // Non-detached fallback (context creation failed): the pre-D5
            // in-place park of the main stack.
            wxWasmYieldToBrowser();
        }
    }
    --s_wxRunDepth;

    // S6 (doc 17): the main loop has ended — wx cleanup follows. Latch the
    // scheduler DEAD so already-queued ticks, messages, mutators, and wakes
    // are dropped/rejected loudly instead of delivering into teardown. Any
    // stranded work is beaconed ("shutdown ... stranded:"), which is the
    // visibility the plan's lifetime step asks for.
    EM_ASM({
        if (globalThis.__wxScheduler) globalThis.__wxScheduler.shutdown("main loop exited");
    });

    return 0;
}
