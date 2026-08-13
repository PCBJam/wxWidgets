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
#include "wx/wasm/private/mainstack.h"
#include "wx/wasm/private/yieldwait.h"

#include <emscripten.h>
#include <emscripten/stack.h>
#include <stdio.h>   // printf: diagnostics land in the browser console
#include <string.h>  // strcmp: park-reason comparison

#include <map>
#include <deque>
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

extern "C" int wxWasmYieldUntil(int token)
{
    // JSPI: every activation suspends uniformly through the wait import; there
    // are no scheduler contexts to park. Early-resolve still short-circuits.
    if (wxWasmWaitEarlyResolvedJs(token))
        return wxWasmTakeWaitResultJs(token);

    return wxWasmYieldUntilJs(token);
}

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

// Set by the host application (pcbjam's binding layer) to whatever can move
// work onto the main stack — for KiCad, a tool coroutine's RunMainStack. wx
// must not know about TOOL_MANAGER, so this stays a plain hook: absent, every
// nested loop simply parks where it already stood.
wxWasmMainStackRunner s_mainStackRunner = NULL;

bool wxWasmRunOnMainStack(void (*aFunc)(void *), void *aArg)
{
    return s_mainStackRunner && s_mainStackRunner(aFunc, aArg) != 0;
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

extern "C" void wxWasmSetMainStackRunner(wxWasmMainStackRunner aRunner)
{
    s_mainStackRunner = aRunner;
}

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
// JSPI: route through the shim so the frame park shares the one shadow-stack
// discipline implementation (jspi-scheduler.js _suspendOn; emscripten #27364).
EM_ASYNC_JS(void, wxWasmYieldToBrowser, (), {
    await globalThis.__wxScheduler.frameYield();
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
        // A throwing handler must not leave the dispatch interlock held nor a
        // parked quasi-modal unresolved (silent stall — the asyncify-races
        // nested_quasi_modal_pump_error case). The containment releases the
        // innermost registered waits: the nested loop AND the top modal
        // (5101 = wxID_CANCEL).
        var contain = function (e) {
            if (Module["_wx_dispatch_abandon"]) Module["_wx_dispatch_abandon"]();
            // Asyncify only: the exception came out through drain()'s fiber
            // swap and the transition is still "in flight" (doc 22 Phase B).
            if (Module["_wxWasmSchedAbandon"]) Module["_wxWasmSchedAbandon"]();
            if (globalThis.__wxScheduler) {
                globalThis.__wxScheduler.resolveTopWait('nested', 0);
                globalThis.__wxScheduler.resolveTopWait('modal', 5101);
            }
        };
        var p;
        try {
            p = Module["_wxWasmTopLevelTick"]();
        } catch (e) {
            // Asyncify tick: dispatch throws synchronously.
            contain(e);
            throw e;
        }
        // JSPI tick: the export is promising, so ANY throw — even one before
        // the first suspension — arrives as a promise REJECTION, never the
        // sync catch above. Same containment, async path.
        Promise.resolve(p).catch(function (e) {
            contain(e);
            console.warn("[wx] top-level tick rejected: " + e);
        });
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
        // JSPI: the tick itself is a promising export — dispatch directly; a
        // handler that suspends parks this tick's own activation.
        ProcessEvents();
    }

}  // extern "C"

// The D5 main-loop detach is fiber-era machinery with no JSPI successor:
// under JSPI OnRun runs the loop inline on promising activations and wxEntry
// must do its own teardown. Shared init.cpp still probes the detach state.
extern "C" bool wxWasmMainLoopDetached()
{
    return false;
}

// JSPI plain-entry job lane: emscripten_set_*_callback entries cannot suspend
// (they are not promising exports), so their jobs queue here and the promising
// wxWasmJobTick delivers them from a fresh task, in order.
namespace
{
struct wxWasmJspiJob
{
    void (*fn)(void *);
    void *arg;
};

std::deque<wxWasmJspiJob> &wxWasmJspiJobs()
{
    static std::deque<wxWasmJspiJob> s_jobs;
    return s_jobs;
}
}  // namespace

EM_JS(int, wxWasmOnPromisingActivationJs, (), {
    const S = globalThis.__wxScheduler;
    return (S && S._actStack && S._actStack.length > 0) ? 1 : 0;
});

EM_JS(void, wxWasmArmJspiJobTickJs, (), {
    const S = globalThis.__wxScheduler;
    if (S.__jobTickArmed) return;
    S.__jobTickArmed = true;
    setTimeout(function () {
        S.__jobTickArmed = false;
        if (S.dead) return;
        var p = Module["_wxWasmJobTick"]();
        Promise.resolve(p).catch(function (e) {
            if (Module["_wx_dispatch_abandon"]) Module["_wx_dispatch_abandon"]();
            S.resolveTopWait('nested', 0);
            S.resolveTopWait('modal', 5101);
            console.warn("[wx-scheduler] job tick error: " + e);
        });
    }, 0);
});

extern "C" void EMSCRIPTEN_KEEPALIVE wxWasmJobTick()
{
    // Deliver ONE job per tick: a job that suspends (a click opening a modal)
    // parks THIS activation; the next job must not run beneath it on the same
    // activation, so re-arm and let a fresh tick (fresh activation) take it.
    if (wxWasmJspiJobs().empty())
        return;

    wxWasmJspiJob job = wxWasmJspiJobs().front();
    wxWasmJspiJobs().pop_front();

    if (!wxWasmJspiJobs().empty())
        wxWasmArmJspiJobTickJs();

    job.fn(job.arg);
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

    // JSPI: the two-path rewind mismatch this function existed to prevent is
    // unrepresentable — but WHERE the job may run still matters. Only a
    // PROMISING activation may suspend; the emscripten_set_*_callback DOM
    // entries (canvas mouse/key/wheel/touch) arrive as plain table calls and
    // trap with SuspendError if a handler below reaches a wait (#22493,
    // observed on the context-menu suite). So: on a tracked promising
    // activation run directly; on a plain entry, queue the job and let the
    // promising job tick deliver it — the same queued semantics the dispatch
    // interlock already gives these entries while a chain is parked, and the
    // job lifetime contract (heap-owned, abandoned == self-owned) is built
    // for exactly this.
    if (wxWasmOnPromisingActivationJs())
    {
        fn(arg);
    }
    else
    {
        wxWasmJspiJobs().push_back({fn, arg});
        wxWasmArmJspiJobTickJs();
    }
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

    // A nested loop (a quasi-modal dialog opened from a tool) pumps via Asyncify; the
    // first (top-level) DoRun registers the rAF main loop then parks. Neither throws
    // (see the header comment and docs/features/wasm-exceptions/09).
    if (s_wxRunDepth++ > 0)
    {
        // JSPI: a nested loop is a registered "nested" wait that suspends
        // whatever activation is running — a tool coroutine's own activation
        // included. There is no stale-fiber guard to trip and no capture to
        // misattribute, so the doc-19 mainstack bounce is unnecessary.
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

        // JSPI: main() is a promising export; this loop's activation suspends
        // for exactly one animation frame per tick. No contexts, no pumps.
        wxWasmYieldToBrowser();
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
