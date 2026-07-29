# WASM dispatch interlock — no event dispatch while another chain is Asyncify-parked

This documents the interlock added in `include/wx/wasm/private/dispatch.h` and
the six `src/wasm/` files that use it. It is the wxWidgets-side fix for a class
of "index out of bounds" / heap-corruption wasm traps that fire when wx event
dispatch is re-entered while a *different* dispatch chain is suspended
mid-handler by Asyncify.

Some paths below (`output/*.wasm.debug.wasm`, `scripts/common/apply-asyncify.sh`,
the downstream `kicadLibs` bridge, CI run IDs) live in the KiCad-WASM build
repo that consumes this fork; they are named so the original investigation is
reproducible, but the fix and its contract are entirely inside this repo.

## The bug

The WASM port drives dialogs, nested event loops, popup menus, the clipboard,
font enumeration, and any downstream JS bridge through Emscripten Asyncify:
an `EM_ASYNC_JS` call suspends the whole C++ stack, runs a JS event loop, and
resumes when a promise settles. Because JS is single-threaded and cannot truly
block, a modal's own pump (`wxDialog::ShowModal` → `startModal` in
`src/wasm/dialog.cpp`) keeps ticking `ProcessEvents` while the *opener's* stack
is parked.

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

### Symbolizing a stripped release wasm (investigation aid)

The shipped `.wasm` has no name section. To turn Firefox's
`wasm-function[i]:0xoffset` frames into names: take the pre-asyncify linker
output (which still has a `name` section), strip its `.debug_*` custom
sections, and replay the host post-link pass with names kept
(`HOIST_KEEP_NAMES=1 apply-asyncify.sh`). The result keeps the **same function
indices** as the shipped binary (the shipped one only appends the `dynCall_*`
and `asyncify_*` exports), so `name`-section lookup resolves the release stack.
`emsymbolizer` against the DWARF does not work — wasm-opt rewrote every code
offset after the DWARF was emitted.

## The fix

`int wxWasmDispatchDepth` (defined in `src/wasm/evtloop.cpp`, declared in
`include/wx/wasm/private/dispatch.h`) counts live dispatch chains. A scope
guard, `wxWasmDispatchGuard`, brackets every fresh dispatch entry. Under
Asyncify the guard's destructor is exactly the right primitive: an unwind does
not run it and a rewind resumes past it, so **a parked chain keeps the count
held until it truly completes.**

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
- The three long-lived parks whose own pump is the legitimate dispatcher while
  the opener is parked: `wxDialog::ShowModal` (`dialog.cpp`), nested
  `wxGUIEventLoop::DoRun` (`evtloop.cpp`), and `wxWindowWasm::DoPopupMenu`
  (`window.cpp`). Each zeroes the count for the park's duration and restores it
  on resume — using plain `int` save/restore, **not** RAII, because destructors
  are not reliable across those parks.

Net effect: during a short park (bridge fetch, clipboard) input queues for the
park's round-trip instead of dispatching into a half-mutated UI; paints keep
running so the UI stays live. Previously the pump either await-blocked (park
inside a pump tick) or kept dispatching (park inside an input chain — the
crash).

## Residual notes

- If a parked chain never resumes, the interlock freezes all dispatch rather
  than one chain — but that was already a hung app (the parked stack holds
  arbitrary locks).
- A wasm trap escaping a dispatch chain leaks the held count (no destructors on
  a trap). Where the JS entry point *catches* the failure the chain is known to
  be dead and the interlock is released explicitly: `wx_dispatch_abandon`
  (`wxWasmDispatchAbandon()`, evtloop.cpp), called from the `dispatch()` catch
  in `build/wasm/wx-dom.js`. This matters because such a failure is not always
  fatal — the wxClipboard test app raises Emscripten's "cannot start an async
  operation when one is already in flight" abort (its `EM_ASYNC_JS` clipboard
  park suspends inside the *synchronous* `wx_dom_event` ccall), keeps running,
  and its later clicks work; without the release, the first abort would gate
  every later event behind a chain that no longer exists. Entry points with no
  catch (the Emscripten key/focus handlers) can still leak, but there the
  runtime is already poisoned.
- That clipboard abort is a **pre-existing** bug, unrelated to the interlock:
  it reproduces identically on builds before it. The real fix is to stop the
  clipboard parking inside a synchronous DOM callback (or ccall `wx_dom_event`
  with `{async: true}`); the spec's assertions are loose enough to pass either
  way, so CI green does not mean the clipboard round-trip works.
- Paint still runs during a park (it always has, and is needed to keep the UI
  alive). The crash class was pending-event *dispatch*, which is what's gated.
