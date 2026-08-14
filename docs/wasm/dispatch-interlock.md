# WASM dispatch interlock — no event dispatch while another chain is suspended mid-handler

This documents the interlock added in `include/wx/wasm/private/dispatch.h` and
the six `src/wasm/` files that use it. It is the wxWidgets-side fix for a class
of "index out of bounds" / heap-corruption wasm traps that fire when wx event
dispatch is re-entered while a *different* dispatch chain is suspended
mid-handler (by Asyncify when this was investigated; by JSPI today — the
hazard is identical).

Some paths below (`output/*.wasm.debug.wasm`, `scripts/common/apply-asyncify.sh`,
the downstream `kicadLibs` bridge, CI run IDs) named artifacts of the
Asyncify-era build pipeline in the KiCad-WASM build repo that consumes this
fork; that pipeline is gone, but they are kept so the original investigation
reads as it happened. The fix and its contract are entirely inside this repo.

## The bug

At the time the WASM port drove dialogs, nested event loops, popup menus, the
clipboard, font enumeration, and any downstream JS bridge through Emscripten
Asyncify: an `EM_ASYNC_JS` call suspended the whole C++ stack, ran a JS event
loop, and resumed when a promise settled. Because JS is single-threaded and
cannot truly block, a modal's own pump (`wxDialog::ShowModal` → `startModal`
in `src/wasm/dialog.cpp`, both since retired in favor of registered scheduler
waits) kept ticking `ProcessEvents` while the *opener's* stack was parked.
Under JSPI the suspension primitive changed but the shape did not: a chain
still suspends mid-handler while the top-level tick keeps dispatching.

The hazard: a dispatch chain can suspend **mid-handler**, with a widget tree
left half-mutated on its saved stack. If any other dispatch runs before that
chain resumes, it walks that half-mutated state.

Observed downstream (KiCad eeschema symbol chooser), the sequence was:

1. An ArrowDown key event dispatches synchronously from the DOM callback
   (`wxApp::HandleKeyEvent` → `HandleWindowEvent`). Its selection handler
   reaches a JS library bridge (`EM_ASYNC_JS` suspend) and the whole chain
   **parks** mid-mutation of the chooser/preview widgets.
2. The parked chain is not the modal pump's, so the pump keeps ticking. A tick
   runs `ProcessPendingEvents`, which dispatches a queued timer event on the
   same chooser.
3. The timer handler walks the parked chain's half-mutated widget tree →
   garbage child pointer → wasm trap in `wxWindow::UpdateChildrenDOMVisibility`
   (the modal pump reports it as `modal event pump error - cancelling modal:
   RuntimeError: index out of bounds`).

It reproduced only under slow (software-GL) rendering, because the parked
window is the bridge fetch's round-trip and only slow execution made the
timer/park overlap likely — a classic timing-dependent reentrancy bug.

### Symbolizing a stripped release wasm (investigation aid, as performed then)

The shipped `.wasm` had no name section. To turn Firefox's
`wasm-function[i]:0xoffset` frames into names, the investigation took the
pre-asyncify linker output (which still had a `name` section), stripped its
`.debug_*` custom sections, and replayed the host post-link pass with names
kept (`HOIST_KEEP_NAMES=1 apply-asyncify.sh` — deleted along with the
Asyncify pipeline). The result kept the **same function indices** as the
shipped binary (the shipped one only appended the `dynCall_*` and
`asyncify_*` exports), so `name`-section lookup resolved the release stack.
`emsymbolizer` against the DWARF did not work — wasm-opt rewrote every code
offset after the DWARF was emitted.

## The fix

`int wxWasmDispatchDepth` (defined in `src/wasm/evtloop.cpp`, declared in
`include/wx/wasm/private/dispatch.h`) counts live dispatch chains. A scope
guard, `wxWasmDispatchGuard`, brackets every fresh dispatch entry. The guard's
destructor is exactly the right primitive: a JSPI suspension keeps the frame
alive and the destructor runs only on true completion, so **a suspended chain
keeps the count held until it truly completes.** (Under Asyncify, where the
interlock was born, the same property came from unwind skipping the destructor
and rewind resuming past it.)

`wxWasmDispatchParked()` is true whenever a chain is live or parked. While it
is true, a would-be fresh dispatch must not run handlers:

| Entry point (file) | Behavior while another chain is parked |
| --- | --- |
| `ProcessEvents` pump tick (`evtloop.cpp`) | `Paint()` only — no `ProcessPendingEvents`/`ProcessIdle`; events stay queued for the first tick after resume |
| `wxApp::HandleKeyEvent` (`app.cpp`) | state bookkeeping runs, the event is `wxPostEvent`'d to the focus window; `CHAR_HOOK` returns "not handled" so the caller still synthesizes the (also queued) `KEY_DOWN`, other types return "handled" so browser defaults stay suppressed |
| `wxApp::HandleMouseEvent` (`app.cpp`) | `UpdateMouseState` runs; button events are posted to the resolved target; motion/hover synthesis dropped — the next real motion re-syncs |
| `wxApp::HandleMouseWheelEvent` (`app.cpp`) | dropped |
| `wx_dom_event` (`domevents.cpp`) | deferred via `CallAfter` (bound to the window's queue, so it dies with the window) |
| wx timer fire (`timer.cpp`) | retried 17 ms later; `ScheduleNextInterval`'s deadline bookkeeping keeps periodic timers on cadence |

Deliberately **ungated**:

- `wxGUIEventLoop::Dispatch()` / `wxYield` — same-stack *nested* dispatch is
  legal; the interlock only forbids interleaving with a *parked* chain, not
  recursion on one live stack.
- The three long-lived suspensions during which the top-level tick is the
  legitimate dispatcher: `wxDialog::ShowModal` (`dialog.cpp`), nested
  `wxGUIEventLoop::DoRun` (`evtloop.cpp`), and `wxWindowWasm::DoPopupMenu`
  (`window.cpp`). Each zeroes the count for the suspension's whole span and
  restores it on resume — using plain `int` save/restore, **not** RAII,
  because `wxWasmDispatchRestore` centralizes the erased-guard reporting.

Net effect: during a short suspension (bridge fetch, clipboard) input queues
for the round-trip instead of dispatching into a half-mutated UI; paints keep
running so the UI stays live. Previously the pump either await-blocked (park
inside a pump tick) or kept dispatching (park inside an input chain — the
crash).

## Residual notes

- If a suspended chain never resumes, the interlock freezes all dispatch
  rather than one chain — but that was already a hung app (the suspended stack
  holds arbitrary locks).
- A wasm trap escaping a dispatch chain leaks the held count (no destructors on
  a trap). Where the JS entry point *catches* the failure the chain is known to
  be dead and the interlock is released explicitly: `wx_dispatch_abandon`
  (`wxWasmDispatchAbandon()`, evtloop.cpp), called from the `dispatch()` catch
  in `build/wasm/wx-dom.js`. This matters because such a failure is not always
  fatal — at the time, the wxClipboard test app raised Emscripten's "cannot
  start an async operation when one is already in flight" abort (its clipboard
  park suspended inside the then-*synchronous* `wx_dom_event` ccall), kept
  running, and its later clicks worked; without the release, the first abort
  gated every later event behind a chain that no longer existed. The JSPI
  member of that failure class is a `SuspendError` from a wait reached below a
  plain (non-promising) entry. Entry points with no catch can still leak, but
  there the runtime is already poisoned.
- That clipboard abort was a **pre-existing** bug, unrelated to the interlock:
  it reproduced identically on builds before it. The real fix — do not suspend
  below a synchronous DOM callback — landed when `wx_dom_event` became an
  awaited call on a promising export; the spec's assertions were loose enough
  to pass either way, so CI green alone never proved the clipboard round-trip
  worked.
- Paint still runs during a park (it always has, and is needed to keep the UI
  alive). The crash class was pending-event *dispatch*, which is what's gated.
