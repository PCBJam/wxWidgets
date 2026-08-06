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
#include "wx/toplevel.h"
#include "wx/wasm/private/dispatch.h"
#include "wx/wasm/private/mailbox.h"
#include "wx/wasm/private/mainstack.h"
#include "wx/wasm/private/yieldwait.h"

#include <emscripten.h>
#include <emscripten/stack.h>
#include <stdio.h>   // printf: diagnostics land in the browser console

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
        wxWasmMailboxDeliver();
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

extern "C" int wxWasmYieldUntil(int token)
{
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
        ProcessEvents();
    }

}  // extern "C"

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

    // Top-level DoRun runs on the main stack by construction: record its bounds
    // while that is true, so nested loops can later tell whether they are
    // standing somewhere else (see wxWasmOnCoroutineStack).
    if (s_wxRunDepth == 0)
    {
        s_mainStackBase = emscripten_stack_get_base();
        s_mainStackEnd = emscripten_stack_get_end();
    }

    // A nested loop (a quasi-modal dialog opened from a tool) pumps via Asyncify; the
    // first (top-level) DoRun registers the rAF main loop then parks. Neither throws
    // (see the header comment and docs/features/wasm-exceptions/09).
    if (s_wxRunDepth++ > 0)
    {
        // A nested loop parks its whole stack for the dialog's lifetime, and
        // WHICH stack that is decides whether the app survives it. On a tool
        // coroutine's stack the park suspends the fiber's body where the fiber
        // layer cannot see it: the stale-fiber guard quarantines the fiber and
        // then refuses its own resume, so the dialog can never be closed by a
        // click (docs/features/async/19). Bounce onto the main stack first —
        // that suspends the coroutine the legitimate way, through a fiber swap
        // the layer records, and leaves the park exactly where every
        // non-tool dialog already puts it.
        if (!(wxWasmOnCoroutineStack() && wxWasmRunOnMainStack(&wxWasmNestedWaitBody, NULL)))
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

    // Run ProcessEvents on the real main C stack, yielding one animation frame between
    // ticks. No throw (fatal under native wasm-EH), no permanent handleAsync park (which
    // blocks coroutine fiber swaps — see wxWasmYieldToBrowser). m_shouldExit, set by
    // ScheduleExit(), ends the loop after the current tick.
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
