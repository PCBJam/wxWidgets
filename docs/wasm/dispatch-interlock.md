# Retired WASM dispatch interlock

This document used to describe a scalar dispatch-depth interlock. That
interlock is retired. Do not restore `wxWasmDispatchDepth`,
`wxWasmDispatchGuard`, `wxWasmDispatchParked()`, modal save/zero/restore, timer
retry delays, or input drops from the old design.

The current implementation uses semantic execution ownership. Its public
contract and reducer model are in
`include/wx/wasm/private/execution_owner.h`; its wx adapter is in
`src/wasm/evtloop.cpp`.

## Why the old interlock existed

An Asyncify suspension saves one native stack. It does not copy or isolate the
C++ heap. A handler can therefore suspend after it has changed part of a wx or
KiCad object graph and before it restores the object's invariants. A fresh
browser callback can then enter the same module and observe that shared graph.

The failure first investigated here had this shape:

1. A chooser input handler changed selection and preview state.
2. The handler suspended during an asynchronous library operation.
3. A timer or pending event entered the module while that handler was parked.
4. The second handler traversed the same, temporarily inconsistent widget
   graph and trapped.

The scalar interlock reduced this race by counting live dispatch chains and by
suppressing selected entry points while the count was nonzero. It also made
special exceptions for modal and nested pumps.

## Why the scalar interlock was retired

A depth counter describes stack nesting, not authority over shared mutable
state. It cannot answer these questions:

- Is a callback a continuation of the parked operation or unrelated work?
- Is input allowed only for the modal window that the parked operation opened?
- Does a tool fiber still belong to the command whose shallow Embind entry has
  returned?
- Is a queued callback stale because its target window was destroyed and its
  address reused?
- Has native execution reached its real tail, or did only a JavaScript wrapper
  finish?

The old modal save/zero/restore mechanism was especially unsafe. It globally
reopened dispatch while a parent transaction remained parked. Timer retry and
input-drop rules also encoded policy independently at several wx entry points.
The trap-recovery path cleared the counter and reopened the application even
though a native trap makes the saved execution and object lifetimes
unknowable.

## Current ownership model

The coordinator admits one root owner for ordinary mutable work. The owner
remains live until every affiliated native body reaches its true tail. An
Asyncify wait token and a scheduler context ID are separate mechanisms:

- A wait token identifies one exact suspension and wake.
- A scheduler context identifies one physical stack.
- An owner token identifies permission to use shared mutable application
  state.

Modal and popup operations do not release the root. They open a narrow child
lease for an exact top-level target scope and generation. Only allowed work
classes for that scope can enter as a child transaction. Closing starts by
stopping new child admission; the lease is removed only after its current child
has completed.

All fresh producers stage typed work in the central execution queue. The queue
admits work when the owner tree makes it eligible. It does not poll and retry.
An owner release or lease transition schedules the next eligibility check.
DOM records hold copied event data and weak target identity so dispatch can
revalidate the target after a delay.

The native queue bounds non-affiliated envelopes at 4096. Affiliated owner
continuations are exempt because refusing the exact continuation which can
retire an owner would turn backpressure into deadlock. At the bound, a new
ordinary envelope is refused and the instance enters fail-stop. Queue transfer
is explicit: `false` leaves the argument with the caller; `true` transfers it
until the callback runs or its discard callback runs. The queue removes a
record before invoking either path, so a re-entrant failure cannot dispose the
same payload twice. This reclaimable ownership contract starts when the native
queue stages the record. On terminal scheduler shutdown, records still waiting
in the JavaScript delay mailbox are abandoned with that Wasm instance; JavaScript
must not create a second callback lane into an integrity-unknown runtime only
to free terminal memory.

Only adjacent, loss-tolerant state may use latest-wins replacement. The two
current classes are passive mouse movement with no button held and resize.
Replacement requires the same producer, coalescing key, work class, and exact
target scope. Its sequence numbers must also be consecutive, so a discrete
record remains a barrier even if an eligibility scan already ran and removed
that record. Replacement calls the older record's discard callback. A key,
wheel, button, drag, different target, affiliated continuation, other producer,
or any other discrete record is an ordering barrier. Diagnostics report queue
high-water, coalesced, and rejected counts.

Programmatic Embind commands and owned file opens use a shallow starter. The
real native body starts on a fresh dispatch context. Queue admission is
reported synchronously; completion is reported only after the native owner is
retired. Network requests themselves remain concurrent because only their
stateful completion handlers enter this ownership lane.

Initial `CallOnInit()` and main-loop publication run under a startup root
owner. Classified browser ingress does not inherit this owner through the
shared main-stack context. Only the audited modal and nested-loop adapters may
use it explicitly to open an exact target lease. The RAII guard releases it
after `OnRun()` has published the detached loop.

If a caught native ownership invariant fails, the coordinator enters a
terminal fail-stop. If a native trap escapes, JavaScript must not call back
into the integrity-unknown Wasm instance. It marks the instance failed and
shuts down the scheduler mirror. Both paths reject later work instead of
pretending that the interrupted transaction completed.

## Adapter locations

The important wx-side adapters are:

- `src/wasm/evtloop.cpp`: coordinator instance, owner-to-stack provenance,
  central job queue, startup owner, modal leases, and terminal fail-stop.
- `src/wasm/app.cpp`, `src/wasm/domevents.cpp`, and
  `src/wasm/toplevel.cpp`: copied, classified browser input with target
  resolution after admission.
- `src/wasm/timer.cpp`: timer staging and target-scope revalidation.
- `src/wasm/dialog.cpp` and `src/wasm/window.cpp`: modal and popup leases.
- `src/common/event.cpp`: pending-event provenance and dispatch filtering.
- `build/wasm/wx-dom.js`: copied browser event records and weak target lookup.

The scalar interlock names remain useful only when reading old commits and
investigation notes. They are not part of the current runtime contract.
