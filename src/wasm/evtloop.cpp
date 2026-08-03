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

#include <emscripten.h>
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
//   * nested (DoRun depth >0): wxWasmRunNestedLoop() suspends the stack and drives
//     ProcessEvents from a JS setTimeout pump (rAF isn't usable from a nested context).
//     Nested resolvers live on a LIFO (Module._wxNestedLoopExit); ScheduleExit() pops
//     the innermost. The top-level loop instead just sets m_shouldExit.

// Depth of nested wxGUIEventLoop::DoRun() calls. 0 = none running; 1 = the
// top-level main loop; >1 = a nested (quasi-modal) loop.
static int s_wxRunDepth = 0;

EM_ASYNC_JS(void, wxWasmRunNestedLoop, (), {
    var stopped = false;
    var timer = null;
    var finish = null;   // resolves THIS nested loop exactly once

    var pump = function () {
        if (stopped) return;
        timer = setTimeout(async function () {
            if (stopped) return;
            try {
                await ccall('ProcessEvents', 'void', [], [], { async: true });
            } catch (e) {
                // The pump must NEVER stop without resolving: an unresolved
                // promise leaves the nested DoRun (and the whole quasi-modal
                // C++ stack under it) parked forever — a silent freeze. Exit
                // the nested loop instead, loudly.
                console.error('[wxWasm] nested loop pump error - exiting nested loop: ' + e);
                if (finish) finish();
                return;
            }
            if (!stopped) pump();
        }, 17);
    };

    Module._wxNestedLoopExit = Module._wxNestedLoopExit || [];

    await new Promise(function (resolve) {
        finish = function () {
            if (stopped) return;
            stopped = true;
            if (timer !== null) { clearTimeout(timer); timer = null; }
            // Self-exit paths must remove our own entry (we may not be top of
            // the stack if an inner loop is open above us).
            var idx = Module._wxNestedLoopExit.indexOf(finish);
            if (idx !== -1) Module._wxNestedLoopExit.splice(idx, 1);
            resolve();
        };
        Module._wxNestedLoopExit.push(finish);
        pump();
    });
});

EM_JS(void, wxWasmExitNestedLoop, (), {
    var stack = Module._wxNestedLoopExit;
    if (stack && stack.length) {
        (stack.pop())();
    }
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
            // If a quasi-modal's nested loop is open, tear it down exactly as
            // the nested pump's own catch does. A handler can throw from EITHER
            // dispatcher, and whichever one catches it, the parked nested DoRun
            // must be released or it never returns — a silent stall (the
            // asyncify-races nested_quasi_modal_pump_error case).
            var exits = Module["_wxNestedLoopExit"];
            if (exits && exits.length) (exits.pop())();
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
    // (quasi-modal) loop is the Asyncify setTimeout pump — resolve it so its DoRun resumes
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

    // A nested loop (a quasi-modal dialog opened from a tool) pumps via Asyncify; the
    // first (top-level) DoRun registers the rAF main loop then parks. Neither throws
    // (see the header comment and docs/features/wasm-exceptions/09).
    if (s_wxRunDepth++ > 0)
    {
        // The opener's dispatch chain parks here for the nested loop's whole
        // lifetime; the nested pump is the legitimate dispatcher meanwhile, so
        // zero the interlock for the park's duration (manual save/restore:
        // destructors are not reliable across an Asyncify park).
        const int savedDispatchDepth = wxWasmDispatchDepth;
        wxWasmDispatchDepth = 0;
        wxWasmRunNestedLoop();   // suspends here until ScheduleExit()/Exit()
        wxWasmDispatchRestore(savedDispatchDepth, "NestedLoop");
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
        // ...but ONLY while this is the only loop running. Dispatching inline
        // used to park this loop inside ProcessEvents for a quasi-modal's whole
        // lifetime, which stopped it pumping; scheduling returns immediately, so
        // without this gate the loop would keep queueing dispatches that run
        // CONCURRENTLY with the nested pump (which zeroes the dispatch interlock
        // for the duration, so nothing else would catch them). The nested pump
        // is the legitimate dispatcher meanwhile; this loop just yields until it
        // exits.
        if (s_wxRunDepth <= 1)
        {
            wxWasmScheduleProcessEvents();
        }
        wxWasmYieldToBrowser();
    }
    --s_wxRunDepth;

    return 0;
}
