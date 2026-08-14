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
#include "wx/wasm/private/yieldwait.h"

#include <emscripten.h>
#include <stdio.h>   // printf: diagnostics land in the browser console

#include <deque>

// Run work on an activation that may suspend (defined below; declared here
// for the entries near the top of this file). See its definition for why
// plain entries must queue their jobs for the promising job tick.
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
// async/17 S1). The queue itself lives in the jspi-scheduler.js shim linked
// as a --pre-js — this side pushes deferred callbacks and pulls due messages
// from a fresh delivery tick. The shim is the ONLY runtime: a glue without it
// is a broken build, caught loudly by wxWasmSchedulerAssertInstalled() at
// main-loop entry.
// ----------------------------------------------------------------------------

EM_JS(int, wxWasmMailboxJsEnabled, (), {
    return (typeof globalThis !== "undefined" &&
            globalThis.__wxSchedulerInstalled &&
            globalThis.__wxScheduler &&
            globalThis.__wxScheduler.mailbox) ? 1 : 0;
});

// The scheduler shim is linked into every glue as a --pre-js
// (scripts/kicad/build-kicad-target.sh, tests/apps/Makefile.wasm); running
// without it means the link flags were dropped and every suspend/wait/timer
// lane below would die in obscure ways. Fail fast and name the culprit.
static void wxWasmSchedulerAssertInstalled()
{
    static bool s_checked = false;
    if (s_checked)
        return;
    s_checked = true;
    if (!wxWasmMailboxJsEnabled())
    {
        printf("[wx-scheduler] FATAL: jspi-scheduler shim not present in this "
               "glue - the --pre-js scripts/common/shims/jspi-scheduler.js "
               "link flag is missing (see scripts/kicad/build-kicad-target.sh "
               "and tests/apps/Makefile.wasm)\n");
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
        // A delivered handler may itself suspend (a lib fetch inside a timer
        // handler): its chain then holds the interlock, and delivering more
        // messages would interleave them with that suspended chain — exactly
        // the collision the mailbox exists to prevent. Leave the remainder
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
    // the shim's self-armed delivery tick from a fresh JS task — never from
    // inside another activation's awaited export. A fresh entry is exactly
    // the context bare setTimeout timer callbacks always ran in — the mailbox
    // changes WHEN a message runs (queued, in order, interlock free), not the
    // kind of entry it runs on.
    void EMSCRIPTEN_KEEPALIVE wxWasmMailboxTick()
    {
        // A delivered timer handler can suspend exactly like a DOM handler
        // can, so it takes the same route: wxWasmRunOnDispatchContext runs it
        // here when this tick is a promising activation and defers it to the
        // promising job tick otherwise (a plain entry would trap with
        // SuspendError at the first wait below it).
        wxWasmRunOnDispatchContext([](void *) { wxWasmMailboxDeliver(); }, NULL);
    }

}  // extern "C"

// ----------------------------------------------------------------------------
// Scheduler token waits (wx/wasm/private/yieldwait.h; doc 17 S4).
// The wait registry lives in the jspi-scheduler.js shim; these are thin
// bridges. wxWasmYieldUntil is the ONE suspend primitive the waits share — a
// JSPI await the scheduler core manages like any other (deferred wakes,
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

// Early-resolve window: a bridge whose request settles before the C++ frame
// reaches the suspend (a provider answering from cache, or a test page with
// no provider at all) resolves the wait first. The shim retains such entries
// with the result attached; peek-and-consume here instead of suspending on a
// wait whose resolve has already been spent.
EM_JS(int, wxWasmWaitEarlyResolvedJs, (int token), {
    return globalThis.__wxScheduler.waitEarlyResolved(token);
});

EM_JS(int, wxWasmTakeWaitResultJs, (int token), {
    return globalThis.__wxScheduler.takeWaitResult(token);
});

extern "C" int wxWasmYieldUntil(int token)
{
    // Every activation suspends uniformly through the wait import.
    // Early-resolve still short-circuits.
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

    void EMSCRIPTEN_KEEPALIVE ProcessEvents()
    {
        if (!wxTheApp)
            return;

        if (wxWasmDispatchParked())
        {
            // Another dispatch chain is suspended mid-handler (e.g. a library
            // bridge fetch inside a key handler). Running more handlers now
            // would interleave two C++ stacks over the same widget state.
            // Keep painting so the UI stays live; queued events dispatch on
            // the first tick after the suspended chain resumes.
            wxTheApp->Paint();
            return;
        }

        wxWasmProcessEventsUngated();
    }

}  // extern "C"

// ----------------------------------------------------------------------------
// Event loops via JSPI (top-level and nested quasi-modal)
// ----------------------------------------------------------------------------
//
// Neither loop uses emscripten_set_main_loop's simulate_infinite_loop=1, which throws
// an "unwind" to ABANDON the C++ stack: that throw is fatal under native wasm-EH (the
// compiler's catch_all cleanup pads catch the foreign exception and run destructors
// that tear down the main frame before it paints — docs/features/wasm-exceptions/08+09).
//
//   * top level (DoRun depth 0): a plain C++ while-loop on main()'s promising
//     activation. Each tick schedules dispatch from a fresh JS task and suspends for
//     ONE animation frame via wxWasmYieldToBrowser — a JSPI suspension the engine
//     resumes on the next frame.
//   * nested (DoRun depth >0): a registered "nested" scheduler wait (doc 17 S4) — no
//     pump; the top-level tick keeps dispatching at any depth. ScheduleExit() resolves
//     the innermost wait. The top-level loop instead just sets m_shouldExit.

// Depth of nested wxGUIEventLoop::DoRun() calls. 0 = none running; 1 = the
// top-level main loop; >1 = a nested (quasi-modal) loop.
static int s_wxRunDepth = 0;

namespace
{

// The nested loop's suspension. The opener's chain suspends here for the
// dialog's whole lifetime; the interlock is zeroed for that whole span so the
// legitimate dispatcher keeps running meanwhile (manual save/restore:
// wxWasmDispatchRestore centralizes the erased-guard reporting). The nested
// loop is a registered WAIT, not a pump (doc 17 S4): the top-level tick is
// the sole dispatcher at any depth, and ScheduleExit()/Exit() resolves the
// innermost "nested" wait to resume this stack.
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

// Top-level main loop: yield to the browser for ONE animation frame, then
// return. Called in a plain C++ while-loop in DoRun (below), so each tick
// suspends main()'s promising activation for exactly one frame and resumes.
// Route through the shim so the frame yield shares the one shadow-stack
// discipline implementation (jspi-scheduler.js _suspendOn; emscripten #27364).
EM_ASYNC_JS(void, wxWasmYieldToBrowser, (), {
    await globalThis.__wxScheduler.frameYield();
});

// Deliver the tick's events from a FRESH JS task instead of inline in the main
// loop (docs/features/async/16 round 6). Dispatching from a fresh entry is
// exactly what every HEALTHY dispatch already does (the DOM handlers and timer
// callbacks that run while main is suspended), and it keeps a handler's own
// suspension out of the main loop's frame-yield continuation. ProcessEvents
// itself is re-entrancy-safe: it no-ops into a repaint whenever a chain is
// parked.
EM_JS(void, wxWasmScheduleProcessEvents, (), {
    setTimeout(function () {
        // A throwing handler must not leave the dispatch interlock held nor a
        // suspended quasi-modal unresolved (silent stall — the asyncify-races
        // nested_quasi_modal_pump_error case). The containment releases the
        // innermost registered waits: the nested loop AND the top modal
        // (5101 = wxID_CANCEL).
        var contain = function (e) {
            if (Module["_wx_dispatch_abandon"]) Module["_wx_dispatch_abandon"]();
            if (globalThis.__wxScheduler) {
                globalThis.__wxScheduler.resolveTopWait('nested', 0);
                globalThis.__wxScheduler.resolveTopWait('modal', 5101);
            }
        };
        var p;
        try {
            p = Module["_wxWasmTopLevelTick"]();
        } catch (e) {
            // Defensive: a promising export delivers throws as rejections,
            // but keep the sync arm for a call that fails before the export
            // even runs (dead runtime, missing export).
            contain(e);
            throw e;
        }
        // The export is promising, so ANY throw — even one before the first
        // suspension — arrives as a promise REJECTION, never the sync catch
        // above. Same containment, async path.
        Promise.resolve(p).catch(function (e) {
            contain(e);
            console.warn("[wx] top-level tick rejected: " + e);
        });
    }, 0);
});

extern "C" {

    // The top-level loop's scheduled dispatch. A separate entry point from
    // ProcessEvents so the JS side has one obvious name to schedule, and so
    // any future top-level-only policy has a home that direct ProcessEvents
    // calls do not share.
    //
    // Deliberately NOT gated on s_wxRunDepth: nested loops are registered
    // waits, not pumps (doc 17 S4), so while a long operation is suspended
    // inside a quasi-modal loop this tick is the only dispatcher there is;
    // refusing to dispatch there would stall exactly the loads this entry
    // exists to keep moving. A throwing handler tears a nested loop down from
    // the error path in wxWasmScheduleProcessEvents, which releases the
    // suspended nested DoRun.
    void EMSCRIPTEN_KEEPALIVE wxWasmTopLevelTick()
    {
        // The tick itself is a promising export — dispatch directly; a
        // handler that suspends parks this tick's own activation.
        ProcessEvents();
    }

}  // extern "C"

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
    if (!fn)
        return;

    // WHERE the job runs matters: only a PROMISING activation may suspend;
    // the emscripten_set_*_callback DOM entries (canvas mouse/key/wheel/
    // touch) arrive as plain table calls and trap with SuspendError if a
    // handler below reaches a wait (#22493, observed on the context-menu
    // suite). So: on a tracked promising activation run directly; on a plain
    // entry, queue the job and let the promising job tick deliver it — the
    // same queued semantics the dispatch interlock already gives these
    // entries while a chain is parked, and the job lifetime contract
    // (heap-owned, abandoned == self-owned) is built for exactly this.
    // Callers that need an answer (a key handler's preventDefault) read it
    // from their own job struct; callers whose job suspends (a click that
    // opens a modal) get "returns while the work continues" semantics.
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

    // A nested loop (a quasi-modal dialog opened from a tool) suspends on a
    // registered wait; the first (top-level) DoRun runs the per-frame loop on
    // main()'s promising activation. Neither throws (see the header comment
    // and docs/features/wasm-exceptions/09).
    if (s_wxRunDepth++ > 0)
    {
        // A nested loop is a registered "nested" wait that suspends whatever
        // activation is running — a tool coroutine's own activation included.
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

    // One tick per animation frame. No throw (fatal under native wasm-EH):
    // this loop runs inline on main()'s promising activation, and the
    // per-frame wait suspends that activation until the next frame.
    // m_shouldExit, set by ScheduleExit(), ends the loop after the current tick.
    while (!m_shouldExit)
    {
        // Schedule, don't dispatch: see wxWasmScheduleProcessEvents. The tick's
        // events run from a fresh JS task while this loop is suspended below,
        // so a handler's own suspension never nests inside this activation.
        //
        // Unconditional at any DoRun depth: nested loops are waits, not pumps
        // (doc 17 S4), so gating on depth would leave a quasi-modal with no
        // dispatcher at all.
        wxWasmScheduleProcessEvents();

        // main() is a promising export; this loop's activation suspends for
        // exactly one animation frame per tick.
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
