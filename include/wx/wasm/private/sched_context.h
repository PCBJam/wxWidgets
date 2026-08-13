/*
 * Scheduler contexts — physical context registry and transfer core.
 *
 * HEADER-ONLY, and living in wx's wasm port rather than pcbjam's wasm/ layer,
 * because wx event dispatch itself runs on a context: evtloop.cpp must
 * see this, and adding a .cpp to wx's build means bakefile regeneration.
 * One implementation, included by wx, by KiCad's bridges, and by the
 * harness (tests/apps/standalone/sched-context).
 *
 * THE RULE THIS EXISTS TO ENFORCE: every stateful activity has one recorded
 * physical context and one semantic owner. Waits on an owned context yield
 * that context to the scheduler. A wait on a foreign or unregistered stack
 * may still use an in-place handleSleep, but the registry records that park
 * and makes the affected fiber unenterable until its own wake consumes it.
 *
 * HOW THIS DIFFERS FROM libcontext (kicad/thirdparty/libcontext), whose Wasm
 * backend is now a thin protocol adapter over this registry:
 *
 *   - libcontext is SYMMETRIC: any stack may jump_fcontext to any other. Its
 *     protocol flag is only a cross-check; this registry is authoritative for
 *     whether a physical target may be entered. This layer is a STAR:
 *     contexts only ever swap OUT to the scheduler, and only the scheduler
 *     swaps IN. A resume is therefore never a guess — the registry says the
 *     context is Parked/Ready and holds its buffer.
 *   - At most ONE transition is in flight, enforced here rather than hoped for.
 *
 * SCOPE: primitives, registry, transfer policy, and memory accounting.
 *
 * THREADING: main thread only, by construction (doc 21 §2 — every Asyncify
 * park in the tree is main-thread; the lib bridge's worker path is a blocking
 * proxy, not a park). Calling any of this from a pthread is a programming
 * error and is refused loudly.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <emscripten/emscripten.h>
#include <emscripten/fiber.h>
#include <emscripten/stack.h>
#include <emscripten/threading.h>


namespace pcbjam_sched
{

using ContextId = uint32_t;
using WakeToken = uint32_t;
using ParkCancel = bool ( * )( ContextId, WakeToken );

/** Who still owns a callback capable of waking a Parked context. */
enum class ParkWakeKind
{
    None,           ///< no delayed callback retains the context
    External,       ///< an ordinary callback retains it; no exact identity
    RetainedExact,  ///< an exact callback retains it but cannot be revoked
    Cancellable     ///< the exact token can be revoked through cancel
};

/** Explicit lifetime lease installed by yield_park(). */
struct ParkWake
{
    ParkWakeKind kind = ParkWakeKind::External;
    WakeToken token = 0;
    ParkCancel cancel = nullptr;

    static ParkWake None()
    {
        ParkWake wake;
        wake.kind = ParkWakeKind::None;
        return wake;
    }

    static ParkWake External()
    {
        return ParkWake();
    }

    static ParkWake RetainedExact( WakeToken aToken )
    {
        ParkWake wake;
        wake.kind = ParkWakeKind::RetainedExact;
        wake.token = aToken;
        return wake;
    }

    static ParkWake Cancellable( WakeToken aToken, ParkCancel aCancel )
    {
        ParkWake wake;
        wake.kind = ParkWakeKind::Cancellable;
        wake.token = aToken;
        wake.cancel = aCancel;
        return wake;
    }
};

/** Admission is separate from the full-width wasm32 result value. */
struct ParkResult
{
    bool accepted;
    int value;

    ParkResult( bool aAccepted, int aValue )
        : accepted( aAccepted ), value( aValue )
    {
    }
};

/** Registry truth for one context. Only the scheduler mutates it. */
enum class Status
{
    Fresh,      ///< created, never entered
    Running,    ///< currently executing (at most one, plus the scheduler)
    Parked,     ///< yielded, waiting for mark_ready()
    Ready,      ///< mark_ready() called, waiting for drain() to swap it in
    Finished,   ///< entry returned; stack/buffer reclaimable
    Suspended   ///< direct fiber lane only: suspended by a symmetric swap;
                ///< its saved rewind data is valid, so it is safe to enter
};

const char* status_name( Status aStatus );

/** Per-context sizes. Both are charged to the memory budget (doc 20 risk 1). */
struct Sizes
{
    size_t stack_bytes = 0;
    size_t asyncify_bytes = 0;
};

/**
 * Create a context. It does NOT run until drain() picks it up: creation
 * marks it Ready so the first drain() enters it at aEntry.
 *
 * aLabel is borrowed for the context's lifetime (use a literal); it exists so
 * a stuck context can be named in a beacon instead of only numbered.
 * Returns 0 if the limit is hit or allocation fails.
 */
ContextId create( void ( *aEntry )( void* ), void* aArg, const char* aLabel );

/**
 * Park the CURRENTLY RUNNING context and yield to the scheduler. On success,
 * accepted is true and value is the full-width result passed by the matching
 * wake. Must be called on a context, never on the scheduler stack. A refused
 * park returns {false, 0}; no integer result value is reserved as a sentinel.
 *
 * aReason is recorded in the registry: "why is this parked" is exactly the
 * question the doc-19 guessing layer could not answer.
 */
ParkResult yield_park( const char* aReason,
                       ParkWake aWake = ParkWake::External() );

/**
 * Mark a Parked context Ready with aResult. Callable from any stack (a JS
 * promise settling, a timer, another context). Does NOT resume it — resuming
 * is drain()'s job, so a wake can never rewind inside another transition
 * (doc 13 §1.4's deferred-wake law, applied to contexts).
 *
 * Returns false if the id is unknown or the context is not Parked.
 */
bool mark_ready( ContextId aId, int aResult );

/**
 * Consume the exact cancellable wake installed by yield_park(). The token
 * must match; an unrelated or stale callback cannot make the context Ready.
 */
bool mark_ready_owned( ContextId aId, int aResult, WakeToken aToken );

/** Consume an exact but deliberately uncancellable retained wake. */
bool mark_ready_retained( ContextId aId, int aResult, WakeToken aToken );

/** Reserve a non-zero, process-lifetime wake identity. Never wraps or reuses. */
WakeToken reserve_wake_token();

/**
 * Scheduler entry: resume at most ONE Ready context, running it until it
 * parks or finishes. Returns the id it ran, or 0 if there was nothing to run
 * (or a transition was already in flight). Call from a clean stack — a fresh
 * JS task, never from inside an awaited export (Emscripten #13302).
 */
ContextId drain();

/**
 * Would yield_park() succeed right now? True only when a context is running
 * AND the caller's frame lies inside that context's own stack — i.e. the
 * caller owns what it would be parking. Callers use this to choose the
 * context park over an in-place one without provoking a refusal beacon.
 */
bool can_yield_here();

/** True when an ordinary wake arrived before its External park. */
bool has_pending_wake( ContextId aId );

/** True while a swap is in flight; drain() refuses to start another. */
bool transition_in_flight();

/**
 * Containment for a context that died abnormally: an exception —
 * including a JS one thrown inside a handler — propagates out THROUGH the
 * scheduler's fiber swap, so drain()'s post-swap bookkeeping never runs and
 * the registry stays "transition in flight" forever, refusing every later
 * drain and wedging the whole pump.
 *
 * Call from the error path of whatever JS entry drove the pump. The same path
 * also fail-stops semantic execution ownership. This releases the low-level
 * transition and POISONS the context that was running — its C++ stack is
 * half-unwound, so it is marked Finished and must never be entered again.
 * Returns true if a transition was actually abandoned.
 */
bool abandon_transition();

/** The running context's id, or 0 when the scheduler stack is running. */
ContextId current();

Status status_of( ContextId aId );

/** Why a context is parked (the string yield_park was given), "" if unknown. */
const char* park_reason_of( ContextId aId );

/** Destroy a Finished context and release its stack + buffer. */
bool destroy( ContextId aId );

// ---------------------------------------------------------------------------
// The DIRECT FIBER LANE — libcontext's symmetric clients.
//
// These carry libcontext's SYMMETRIC semantics — any registered fiber may swap
// to any other, the caller decides — so that libcontext's wasm backend can
// become a thin adapter over this registry with identical observable
// behaviour. The registry then knows every tool fiber (stack range, buffer,
// status) and performs every emscripten_fiber_swap in one place; the star
// invariants above are untouched. A symmetric context uses the direct lane
// until a star transfer explicitly moves it through the ready FIFO. Direct-
// lane counters stay separate from scheduler-context memory counters.
// ---------------------------------------------------------------------------

/**
 * Adopt the CURRENT stack (libcontext's main context) as the fiber lane's
 * root. Allocates the asyncify buffer, marks the context Running, and makes it
 * fiber_current(). Call exactly once, while actually running on that stack.
 */
ContextId fiber_adopt_current( size_t aAsyncifyBytes, const char* aLabel );

/**
 * Register a fiber whose C stack the CALLER owns (KiCad allocates coroutine
 * stacks itself); the registry adopts the range [aStackBottom, aStackBottom +
 * aStackBytes) for bookkeeping and allocates the asyncify buffer. aEntry is
 * the raw emscripten fiber entry (it must never return — libcontext's
 * trampoline). Fresh, i.e. enterable: its first swap-in takes the entry path.
 */
ContextId fiber_create( void ( *aEntry )( void* ), void* aArg,
                        void* aStackBottom, size_t aStackBytes,
                        size_t aAsyncifyBytes, const char* aLabel );

/**
 * Is aId safe to enter? A registry lookup: Fresh is a first entry, Suspended
 * has valid saved rewind data, and Parked with no wake owner is a transfer
 * continuation. Ready already has a scheduler/FIFO claim and is not available
 * to a second caller. Running, wait-owned Parked, Finished, and unknown are
 * not enterable. This is the answer libcontext's swap_suspended flag guessed
 * at; the adapter keeps that flag only as a cross-check.
 */
bool fiber_enterable( ContextId aId );

/**
 * THE symmetric swap: suspend aFrom and enter aTo. Every libcontext swap
 * funnels through here, so the registry always knows who is on the CPU.
 *
 * aFrom is EXPLICIT, but it is only a candidate identity. The registry accepts
 * it as the physical source only when its Running status, fiber_running value,
 * exact C-stack range, and in-place-park count all agree. This proof prevents a
 * stale libcontext g_current_context from attributing a new browser-stack swap
 * to a fiber whose own Asyncify wake is still live.
 *
 * A non-enterable target is refused before any state change or physical swap.
 * This is the final authority even when libcontext's duplicate protocol bit
 * incorrectly says that a saved suspension is valid. Returns false without
 * swapping for every invalid source or target.
 */
bool fiber_swap( ContextId aFrom, ContextId aTo );

/** The fiber lane's current occupant (the adopted root counts), 0 if none. */
ContextId fiber_current();

/**
 * Unregister a fiber and free its asyncify buffer (its C stack belongs to the
 * caller). Fresh, Suspended, Parked, Ready, and Finished contexts can be
 * cancelled or retired. A Parked context with no external wake is safe; one
 * with a cancellable exact wake is first revoked; one with an uncancellable
 * external wake is refused. Running and in-place-parked contexts cannot be
 * released: a JS handleSleep wake still owns rewind data and may later restore onto both the
 * Context and caller-owned C stack. Such a release is refused without changing
 * the Context. libcontext treats that refusal as a terminal ownership failure,
 * so its caller cannot continue and free the C stack underneath the wake.
 */
bool fiber_release( ContextId aId );

/**
 * A symmetric swap expressed as a star transition: park aFrom, make
 * aTo runnable carrying aValue, and let the scheduler perform the entry.
 * Returns the value handed back when somebody later transfers to aFrom.
 *
 * This preserves libcontext's synchronous contract. To the
 * code running on aFrom the call still "returns when the other side yields
 * back" — but in between, aFrom is parked and the scheduler owns the CPU, so
 * nothing enters a context by inference and a wait can park anywhere without
 * stranding its resumer.
 *
 * Must be called ON aFrom's stack, and aFrom must be the lane's current
 * occupant; libcontext knows both, so neither is inferred here.
 */
intptr_t fiber_transfer( ContextId aFrom, ContextId aTo, intptr_t aValue );

/**
 * A coroutine's terminal star transfer: hand aValue to aTo, mark aFrom
 * Finished (never re-queued, never re-entered — a later transfer into it is
 * refused into the caller's ghost contract), and yield forever. Replaces the
 * former trampoline's ghost re-entry loop on the transfer lane.
 */
[[noreturn]] void fiber_finish_transfer( ContextId aFrom, ContextId aTo,
                                         intptr_t aValue );

/**
 * A coroutine's terminal direct swap. This is the fallback
 * for a symmetric chain that is not running as a star transfer: mark aFrom
 * Finished, enter aTo, and never make aFrom resumable again. Both the protocol
 * layer and this authoritative registry validate aTo before the physical swap.
 *
 * Unlike fiber_finish_transfer(), this does not touch the star scheduler's
 * running/transition fields. A direct libcontext chain can temporarily run
 * above a star-owned context, so those fields describe the stack below it.
 */
[[noreturn]] void fiber_finish_swap( ContextId aFrom, ContextId aTo,
                                     intptr_t aValue );

/**
 * Make a fiber-lane context runnable from the scheduler stack, carrying
 * aValue. This is the star lane's entry point: a transfer needs a
 * running context to park, so the first one — and every kick from a JS task,
 * e.g. the tick handing work to the dispatch context — has to come from here.
 */
bool fiber_start( ContextId aId, intptr_t aValue );

/**
 * One bounded scheduler-pump result.
 *
 * A browser task must not run an unbounded transfer chain.  Reaching the
 * per-task budget is therefore not quiescence: the caller must arrange one
 * coalesced continuation from a fresh JavaScript task.  Repeatedly exhausting
 * that budget without ever becoming quiescent is a scheduler livelock and is
 * terminal for the instance.
 */
enum class DrainDisposition
{
    Quiescent,
    ContinueOnFreshTask,
    Livelock
};

struct DrainResult
{
    size_t transitions = 0;
    DrainDisposition disposition = DrainDisposition::Quiescent;
};

// Public so deterministic reducers can cross the exact production boundary.
constexpr size_t DrainTransitionsPerPump = 4096;
constexpr size_t DrainMaxConsecutiveBudgetExhaustions = 64;

/** Pump one bounded batch of ready contexts. Scheduler stack only. */
DrainResult drain_all();

/**
 * Registry + memory snapshot as JSON, for tests and the context memory gate:
 *   live/peakLive/created/finished, transitions, bytes/peakBytes,
 *   perContextBytes, and asyncify high-water usage (asyncifyHighWater) —
 *   the measurement doc 20 risk 1 asks for so buffer sizes get re-derived
 *   from evidence instead of inherited from libcontext's 512K.
 */
std::string stats_json();

/** One line per live context: id, label, status, reason, asyncify usage. */
std::string registry_json();

/** Test hook: reset all counters (does not touch live contexts). */
void reset_stats();

/**
 * Test hook: make the next ready-FIFO publication fail at the same boundary as
 * a vector allocation failure. The hook is consume-once and does not mutate
 * the context or its wake lease.
 */
void fail_next_ready_fifo_allocation_for_test();

namespace detail
{

// Sizes are DELIBERATELY not libcontext's 512K asyncify buffer: doc 20 risk 1
// requires them re-derived from measurement, and this layer measures its own
// high-water use (see asyncify_used()). Start at a quarter of libcontext's and
// let the gate tell us whether that is generous or tight — an inherited
// constant nobody can justify is exactly what the risk warns about.
//
// The C stack is separate from the asyncify buffer: the former holds live
// frames while running, the latter holds their locals while parked.
constexpr size_t DEFAULT_C_STACK_BYTES = 128 * 1024;
constexpr size_t DEFAULT_ASYNCIFY_BYTES = 128 * 1024;

// A hard ceiling turns "contexts leaked until the tab died" into a loud,
// early failure with a registry dump. Deliberately low: measured application
// and reducer runs use far fewer than 64 simultaneous contexts.
constexpr size_t MAX_LIVE_CONTEXTS = 64;

/**
 * 16-byte-aligned buffer.
 *
 * NOT a nicety: a fiber's C stack must be 16-aligned or EM_ASM breaks. Its
 * argument buffer is allocated ON the running stack and the glue asserts
 * `buf % 16 == 0` ("the input buffer is allocated on the stack, so it must be
 * stack-aligned"). Emscripten's malloc is 8-byte aligned, so a std::vector<char>
 * stack lands on an 8-mod-16 address about half the time and EVERY EM_ASM on
 * that context traps with an unreachable inside readEmAsmArgs — which is
 * exactly how this was found: 107 wx tests failed before this alignment was
 * restored. libcontext has always carried `alignas(16)` on its buffers for
 * the same reason.
 */
struct AlignedBuffer
{
    void* raw = nullptr;
    char* base = nullptr;
    size_t size = 0;

    void allocate( size_t aBytes )
    {
        release();
        // Round the size up too: the fiber's stack BASE is base+size, and a
        // misaligned top misaligns every frame below it.
        size = ( aBytes + 15u ) & ~size_t( 15 );
        raw = std::calloc( 1, size + 16 );

        if( !raw )
        {
            size = 0;
            return;
        }

        base = reinterpret_cast<char*>(
                ( reinterpret_cast<uintptr_t>( raw ) + 15u ) & ~uintptr_t( 15 ) );
    }

    /**
     * Track a range someone else owns (a KiCad-allocated coroutine stack):
     * base/size describe it for bookkeeping and ownership checks, but
     * release() must not free it — raw stays null so free(nullptr) is a no-op.
     */
    void adopt( void* aBase, size_t aBytes )
    {
        release();
        base = static_cast<char*>( aBase );
        size = aBytes;
    }

    void release()
    {
        std::free( raw );
        raw = nullptr;
        base = nullptr;
        size = 0;
    }

    ~AlignedBuffer() { release(); }

    AlignedBuffer() = default;
    AlignedBuffer( const AlignedBuffer& ) = delete;
    AlignedBuffer& operator=( const AlignedBuffer& ) = delete;
};

struct Context
{
    ContextId id = 0;
    const char* label = "";
    const char* park_reason = "";
    Status status = Status::Fresh;
    int result = 0;
    // Fiber lane: the value a symmetric transfer hands to whoever is entered
    // (libcontext's INVOCATION_ARGS pointer). Separate from `result` because
    // it is pointer-width and carries the protocol, not a wake code.
    intptr_t transfer = 0;

    emscripten_fiber_t fiber {};
    AlignedBuffer c_stack;
    AlignedBuffer asyncify_stack;

    void ( *entry )( void* ) = nullptr;
    void* arg = nullptr;

    uint32_t parks = 0;
    uint32_t resumes = 0;
    size_t asyncify_high_water = 0;
    // Capture size of the most recent LIVE sample (a park's in-flight
    // asyncify use). Unlike the high-water it is per-park, so a waiter that
    // resumes can attribute the depth to its own wait kind. This is the
    // buffer-sizing input for real bridge waits.
    size_t last_park_use = 0;
    // In-flight in-place Asyncify parks on this context's stack
    // (handleSleep parks the registry cannot see through its own swaps). Fed
    // by the shim at park start/end; a context with one in flight holds a
    // STALE fiber capture and must not be entered by swap or transfer — its
    // own wake is the only legitimate resume. The physical registry owns this
    // cross-check, and the generated consume-once guard verifies it again at
    // the rewind boundary.
    int inplace_parks = 0;

    // A Parked context records exactly who may wake it. None is safe to
    // reclaim; External and RetainedExact must remain alive; Cancellable
    // carries the token and revoker which fiber_release() must consume before
    // reclaiming the registry object and caller-owned stack.
    ParkWake park_wake = ParkWake::None();

    // Deferred-wake law applied to the registry: a resolve that arrives while
    // this context is Running (a
    // re-entrant close — the footprint chooser resumes its own opener before
    // the modal's resolve lands) must NOT be dropped. Record it here; the
    // next yield_park delivers it, so the wait resumes instead of hanging.
    bool has_pending_wake = false;
    int  pending_wake_result = 0;

    // Direct fiber lane: a libcontext client under symmetric-swap
    // semantics. Never enters the ready FIFO, never picked by drain(),
    // counted separately from the star's memory gate.
    bool symmetric = false;

    // True only for the one browser/main-stack context adopted by libcontext.
    // Its exact bounds still live in c_stack; this tag permits the direct lane
    // to reclaim that same stack after a quiescent star drain published no
    // current direct occupant.
    bool adopted_root = false;
};

inline void release_generated_fiber_guard( const Context& aCtx )
{
    // The generated Emscripten compatibility guard keys live suspensions by
    // this raw emscripten_fiber_t address. Revoke that identity before the
    // Context is freed so allocator address reuse cannot alias an old fiber,
    // and so cancelled suspensions do not accumulate in JavaScript sets.
    EM_ASM( {
        var scheduler = globalThis.__wxScheduler;
        if( scheduler && typeof scheduler.releaseFiberGuard === "function" )
            scheduler.releaseFiberGuard( $0 >>> 0 );
    }, reinterpret_cast<std::uintptr_t>( &aCtx.fiber ) );
}

struct Registry
{
    // Emscripten rewrites its live stack-limit globals on every fiber switch.
    // Capture the browser main stack before this registry can perform one: the
    // first registry call is necessarily the main-thread creation/adoption
    // edge.  A zero lower limit is valid for STACK_FIRST standalone modules.
    const uintptr_t main_stack_base = emscripten_stack_get_base();
    const uintptr_t main_stack_end = emscripten_stack_get_end();

    std::map<ContextId, Context*> contexts;
    std::vector<ContextId> ready_fifo;   // FIFO: no starvation (doc 13 §1.5 inv. 8)

    ContextId next_id = 1;
    WakeToken next_wake_token = 1;
    ContextId running = 0;               // 0 = the scheduler stack is running
    bool transition = false;             // at most one swap in flight
    bool scheduler_initialized = false;
    emscripten_fiber_t scheduler_fiber {};
    AlignedBuffer scheduler_asyncify_stack;

    // Counters (stats_json)
    uint32_t created = 0;
    uint32_t finished = 0;
    uint32_t transitions = 0;
    uint32_t refusals = 0;
    uint32_t foreign_stack_refusals = 0;   // yield_park from a fiber above a context
    uint32_t deferred_wakes = 0;           // resolve-while-running: queued, not dropped
    uint32_t ready_publication_failures = 0;
    bool fail_next_ready_fifo_allocation = false;
    size_t drain_budget_exhaustion_streak = 0;
    uint32_t drain_budget_yields = 0;
    uint32_t drain_livelocks = 0;
    size_t live = 0;
    size_t peak_live = 0;
    size_t bytes = 0;
    size_t peak_bytes = 0;
    size_t asyncify_high_water = 0;

    // Direct fiber lane — deliberately separate from the star's counters so
    // the context memory gate (finished == created, live == 0 after the battery)
    // keeps meaning what it meant.
    ContextId fiber_running = 0;           // 0 = no fiber lane yet (root unadopted)
    uint32_t fiber_created = 0;
    uint32_t fiber_released = 0;
    uint32_t fiber_swaps = 0;
    uint32_t fiber_refusals = 0;
    uint32_t fiber_released_suspended = 0;   // legal (refcount drop mid-suspend), counted
    uint32_t fiber_released_running = 0;     // must remain zero: Running release is refused
    uint32_t fiber_release_refusals = 0;     // live/in-place-parked cancellation attempts
    uint32_t fiber_nonenterable_swaps = 0;   // TRIPWIRE: a swap into stale state
    size_t fiber_live = 0;
    size_t fiber_peak_live = 0;
    size_t fiber_bytes = 0;
    size_t fiber_peak_bytes = 0;
    size_t fiber_asyncify_high_water = 0;
};

inline Registry& reg()
{
    static Registry s_registry;
    return s_registry;
}

inline void beacon( const char* aWhat, const char* aDetail, unsigned aId )
{
    std::printf( "[sched-ctx] %s id=%u %s\n", aWhat, aId, aDetail ? aDetail : "" );
    std::fflush( stdout );
}

/**
 * Publish one runnable claim without allowing an allocation exception to
 * escape through a fiber swap. Callers must invoke this BEFORE changing the
 * Context's status, result, transfer value, or wake ownership. std::vector's
 * strong exception guarantee for the scalar ContextId makes a failed push
 * indistinguishable from the deterministic reducer hook: the FIFO and every
 * caller-owned field remain unchanged.
 */
inline bool publish_ready_id( ContextId aId, const char* aSite )
{
    Registry& r = reg();

    if( r.fail_next_ready_fifo_allocation )
    {
        r.fail_next_ready_fifo_allocation = false;
        ++r.ready_publication_failures;
        beacon( "READY-PUBLICATION-FAILED", aSite, aId );
        return false;
    }

    try
    {
        r.ready_fifo.push_back( aId );
    }
    catch( ... )
    {
        ++r.ready_publication_failures;
        beacon( "READY-PUBLICATION-FAILED", aSite, aId );
        return false;
    }

    return true;
}

inline bool on_main_thread()
{
    // Contexts are a main-thread concept by construction (doc 21 §2). A worker
    // has its own Asyncify state; a context swapped there would corrupt both.
    return emscripten_is_main_runtime_thread() != 0;
}

/**
 * Bytes of asyncify buffer this context has consumed at its deepest park.
 *
 * asyncify_data_t is {stack_ptr, stack_limit, rewind_id}; the unwind fills
 * from the buffer's base upward, so used = stack_ptr - base. This is the same
 * quantity the shim's `rem=` telemetry reports from the other direction
 * (limit - ptr), and it is what doc 20 risk 1 wants sizing derived from.
 */
inline size_t asyncify_used( const Context& aCtx )
{
    const char* base = aCtx.asyncify_stack.base;
    const char* ptr = static_cast<const char*>( aCtx.fiber.asyncify_data.stack_ptr );

    if( !base || !ptr || ptr < base )
        return 0;

    const size_t used = static_cast<size_t>( ptr - base );
    return used > aCtx.asyncify_stack.size ? aCtx.asyncify_stack.size : used;
}

inline void note_asyncify_use( Context& aCtx )
{
    const size_t used = asyncify_used( aCtx );

    // Only live captures are meaningful (after a resume consumes the capture
    // the pointer is back at base and this reads 0 — keep the last real one).
    if( used > 0 )
        aCtx.last_park_use = used;

    if( used > aCtx.asyncify_high_water )
        aCtx.asyncify_high_water = used;

    // Per-lane high-water: the star's number feeds the context sizing gate;
    // the fiber lane's number measures real bridge captures.
    size_t& lane_high_water =
            aCtx.symmetric ? reg().fiber_asyncify_high_water : reg().asyncify_high_water;

    if( used > lane_high_water )
        lane_high_water = used;

    // Overflow here is silent corruption (libcontext's 512K comment documents
    // exactly that failure), so shout well before the edge rather than after.
    if( used * 4 > aCtx.asyncify_stack.size * 3 )
    {
        char detail[128];
        std::snprintf( detail, sizeof( detail ),
                       "asyncify buffer >75%% used (%zu/%zu) - raise the size",
                       used, aCtx.asyncify_stack.size );
        beacon( "BUFFER-PRESSURE", detail, aCtx.id );
    }
}

/**
 * Is the caller's frame inside aCtx's own C stack?
 *
 * The address of a local is the cheapest exact witness of which stack we are
 * standing on. A context's stack occupies [base, base+size); a libcontext
 * fiber swapped in above it runs on a different allocation entirely, so this
 * separates "the context is running its own body" from "something else is
 * running on top of the context".
 */
inline bool on_context_stack( const Context& aCtx )
{
    char probe = 0;
    const uintptr_t here = reinterpret_cast<uintptr_t>( &probe );
    const uintptr_t low = reinterpret_cast<uintptr_t>( aCtx.c_stack.base );

    if( aCtx.c_stack.size == 0 )
        return false;

    return here >= low && here < low + aCtx.c_stack.size;
}


/**
 * Which registered context owns the CALLER'S stack frame? 0 when the frame is
 * on the main/scheduler stack (or an unregistered stack). Leaf-safe: a plain
 * range scan, no state changes — the shim calls this through an export at
 * in-place park start to attribute the park to its context.
 */
inline ContextId context_owning_current_stack()
{
    char probe = 0;
    const uintptr_t here = reinterpret_cast<uintptr_t>( &probe );

    // Registered stack ranges can overlap in Wasm's linear memory. Prefer a
    // context which the scheduler says is Running, but only when this frame
    // is actually inside its allocation and it has no in-place Asyncify park.
    // A logical identity alone is not provenance: it can remain set while a
    // parked fiber is unwound and a fresh browser callback runs.
    Registry& r = reg();
    const auto exactRunningContext = [&]( ContextId aId ) -> ContextId {
        const auto it = r.contexts.find( aId );

        if( it == r.contexts.end() )
            return 0;

        const Context* ctx = it->second;
        const uintptr_t low = reinterpret_cast<uintptr_t>( ctx->c_stack.base );

        if( ctx->status == Status::Running && ctx->inplace_parks == 0
            && ctx->c_stack.size != 0
            && here >= low && here < low + ctx->c_stack.size )
        {
            return it->first;
        }

        return 0;
    };

    if( const ContextId owner = exactRunningContext( r.fiber_running ) )
        return owner;

    if( const ContextId owner = exactRunningContext( r.running ) )
        return owner;

    // With no proved running fiber, select the tightest enclosing allocation.
    // unordered_map iteration order is not physical identity. Choosing its
    // first match made an overlapping older fiber impersonate the current
    // dispatch or tool stack.
    ContextId owner = 0;
    size_t ownerSize = static_cast<size_t>( -1 );

    for( auto& [id, ctx] : r.contexts )
    {
        const uintptr_t low = reinterpret_cast<uintptr_t>( ctx->c_stack.base );

        if( ctx->status == Status::Running && ctx->inplace_parks == 0
            && ctx->c_stack.size != 0
            && here >= low && here < low + ctx->c_stack.size
            && ctx->c_stack.size < ownerSize )
        {
            owner = id;
            ownerSize = ctx->c_stack.size;
        }
    }

    return owner;
}


/**
 * Does an exact registered context still own an in-place Asyncify wake?
 *
 * Stack-range membership alone is not execution provenance while such a wake
 * is live. A fresh browser entry can allocate frames in the same linear-memory
 * range after the parked stack unwinds to JavaScript. Callers which use
 * context_owning_current_stack() to attribute a native entry must therefore
 * reject this state until the matching handleSleep cleanup clears the park.
 */
inline bool context_has_inplace_park( ContextId aId )
{
    auto it = reg().contexts.find( aId );
    return it != reg().contexts.end() && it->second->inplace_parks > 0;
}


/**
 * Does any registered context still own an in-place Asyncify capture?
 *
 * A fresh browser callback runs on the scheduler/main stack after the parked
 * C stack has unwound to JavaScript.  At that point current() can correctly
 * be zero even though entering another context would make two branches share
 * the one browser-root continuation.  Native-entry admission must therefore
 * test the live capture itself, not infer it from the stack which happens to
 * be executing the readiness probe.
 */
inline bool any_context_has_inplace_park()
{
    for( const auto& entry : reg().contexts )
    {
        const Context* ctx = entry.second;

        if( ctx && ctx->inplace_parks > 0 )
            return true;
    }

    return false;
}


/** The shim reports in-place park start (+1) and end (-1) here. */
inline void note_inplace_park( ContextId aId, int aDelta )
{
    auto it = reg().contexts.find( aId );

    if( it == reg().contexts.end() )
        return;

    it->second->inplace_parks += aDelta;

    if( it->second->inplace_parks < 0 )
    {
        beacon( "INPLACE-PARK-UNDERFLOW", "more park ends than starts", aId );
        // This counter is physical ownership of an Asyncify capture.  Repairing
        // it to zero would assert that a stack is enterable when we no longer
        // know whether a wake still owns it.  The instance cannot continue.
        std::abort();
    }
}


inline Context* find( ContextId aId )
{
    auto it = reg().contexts.find( aId );
    return it == reg().contexts.end() ? nullptr : it->second;
}

inline ContextId reserve_context_id()
{
    Registry& r = reg();

    if( r.next_id == 0 )
    {
        beacon( "REFUSED", "context-id space exhausted", 0 );
        r.refusals++;
        return 0;
    }

    return r.next_id++;
}

inline void ensure_scheduler_context()
{
    Registry& r = reg();

    if( r.scheduler_initialized )
        return;

    // The scheduler runs on whatever stack called drain() first — a fresh JS
    // task entry. Contexts swap back INTO this fiber, which is what makes the
    // topology a star: every yield lands here, and only here decides who runs
    // next.
    r.scheduler_asyncify_stack.allocate( DEFAULT_ASYNCIFY_BYTES );
    emscripten_fiber_init_from_current_context( &r.scheduler_fiber,
                                                r.scheduler_asyncify_stack.base,
                                                r.scheduler_asyncify_stack.size );
    r.scheduler_initialized = true;
}

/**
 * Context trampoline. Per emscripten fiber.h the entry function must NEVER
 * return (returning ends the whole program), so it loops: run the body, mark
 * Finished, swap back to the scheduler, and if the scheduler ever enters this
 * context again, run again.
 */
inline void context_trampoline( void* aArg )
{
    auto* ctx = static_cast<Context*>( aArg );

    while( true )
    {
        ctx->status = Status::Running;

        if( ctx->entry )
            ctx->entry( ctx->arg );

        ctx->status = Status::Finished;
        ctx->park_reason = "finished";
        reg().finished++;
        reg().running = 0;
        reg().transition = false;

        // Back to the scheduler. A finished context is never re-entered by
        // drain() (it only picks Ready), so this swap is terminal in practice.
        emscripten_fiber_swap( &ctx->fiber, &reg().scheduler_fiber );
    }
}

} // namespace detail

// The public entry points below are thin policy over detail's registry.
// (No name in detail collides with a public one; see the declarations above.)
using namespace detail;


inline const char* status_name( Status aStatus )
{
    switch( aStatus )
    {
    case Status::Fresh:    return "fresh";
    case Status::Running:  return "running";
    case Status::Parked:   return "parked";
    case Status::Ready:    return "ready";
    case Status::Finished: return "finished";
    case Status::Suspended: return "suspended";
    }

    return "?";
}


inline ContextId create( void ( *aEntry )( void* ), void* aArg, const char* aLabel )
{
    Registry& r = reg();

    if( !on_main_thread() )
    {
        beacon( "REFUSED", "create() off the main thread", 0 );
        r.refusals++;
        return 0;
    }

    if( !aEntry )
    {
        beacon( "REFUSED", "create() with a null entry", 0 );
        r.refusals++;
        return 0;
    }

    if( r.live >= MAX_LIVE_CONTEXTS )
    {
        char detail[160];
        std::snprintf( detail, sizeof( detail ),
                       "context ceiling reached (%zu live) - registry: %s",
                       r.live, registry_json().c_str() );
        beacon( "REFUSED", detail, 0 );
        r.refusals++;
        return 0;
    }

    auto* ctx = new( std::nothrow ) Context();

    if( !ctx )
    {
        beacon( "REFUSED", "context allocation failed", 0 );
        r.refusals++;
        return 0;
    }

    ctx->id = reserve_context_id();

    if( !ctx->id )
    {
        delete ctx;
        return 0;
    }
    ctx->label = aLabel ? aLabel : "";
    ctx->entry = aEntry;
    ctx->arg = aArg;
    ctx->c_stack.allocate( DEFAULT_C_STACK_BYTES );
    ctx->asyncify_stack.allocate( DEFAULT_ASYNCIFY_BYTES );

    if( !ctx->c_stack.base || !ctx->asyncify_stack.base )
    {
        beacon( "REFUSED", "context stack allocation failed", 0 );
        r.refusals++;
        delete ctx;
        return 0;
    }

    emscripten_fiber_init( &ctx->fiber, context_trampoline, ctx,
                           ctx->c_stack.base, ctx->c_stack.size,
                           ctx->asyncify_stack.base, ctx->asyncify_stack.size );

    ctx->status = Status::Ready;   // enters at aEntry on the first drain()
    ctx->park_reason = "created";

    // Publish both discoverability and runnable ownership atomically.  A map
    // allocation followed by a failing FIFO allocation used to leave a live
    // Context in the registry which no scheduler edge could ever consume.
    std::map<ContextId, Context*>::iterator inserted = r.contexts.end();

    try
    {
        const auto result = r.contexts.emplace( ctx->id, ctx );

        if( !result.second )
        {
            beacon( "REFUSED", "duplicate context id during publication", ctx->id );
            r.refusals++;
            delete ctx;
            return 0;
        }

        inserted = result.first;

        if( !publish_ready_id( ctx->id, "create() ready FIFO allocation failed" ) )
        {
            r.contexts.erase( inserted );
            inserted = r.contexts.end();
            beacon( "REFUSED", "context publication allocation failed", ctx->id );
            r.refusals++;
            delete ctx;
            return 0;
        }
    }
    catch( ... )
    {
        if( inserted != r.contexts.end() )
            r.contexts.erase( inserted );

        beacon( "REFUSED", "context publication allocation failed", ctx->id );
        r.refusals++;
        delete ctx;
        return 0;
    }

    r.created++;
    r.live++;

    if( r.live > r.peak_live )
        r.peak_live = r.live;

    r.bytes += ctx->c_stack.size + ctx->asyncify_stack.size;

    if( r.bytes > r.peak_bytes )
        r.peak_bytes = r.bytes;

    return ctx->id;
}


inline WakeToken reserve_wake_token()
{
    Registry& r = reg();

    if( r.next_wake_token == 0 )
    {
        beacon( "REFUSED", "wake-token space exhausted", 0 );
        r.refusals++;
        return 0;
    }

    return r.next_wake_token++;
}


inline ParkResult yield_park( const char* aReason, ParkWake aWake )
{
    Registry& r = reg();
    Context* ctx = find( r.running );

    if( !ctx )
    {
        // Called on the scheduler stack: there is nothing to yield. This is
        // the "parked in place" mistake the whole design forbids, so it is a
        // loud refusal rather than a silent no-op.
        beacon( "REFUSED", "yield_park() with no running context", 0 );
        r.refusals++;
        return ParkResult( false, 0 );
    }

    // STACK OWNERSHIP. The registry says which context is running, but a
    // libcontext fiber may have been swapped in ON TOP of it — which is
    // exactly the shape of a KiCad tool coroutine opening a dialog: the
    // dispatch context is "running" while the live stack belongs to the tool
    // fiber. Yielding here would save the TOOL fiber's stack into the DISPATCH
    // context's fiber struct and hand it to the next resume: silent, total
    // corruption, and by construction undetectable afterwards.
    //
    // So verify the frame we are standing on actually lies inside this
    // context's C stack, and refuse if not. Owner-aware suspension routes this
    // case through the active tool context. If that attribution is absent,
    // this refusal keeps the failure loud and local.
    if( !on_context_stack( *ctx ) )
    {
        beacon( "REFUSED",
                "yield_park() from a foreign stack (a fiber swapped in above this "
                "context) - the wait must yield ITS OWN context",
                ctx->id );
        r.refusals++;
        ++r.foreign_stack_refusals;
        return ParkResult( false, 0 );
    }

    const bool exact = aWake.kind == ParkWakeKind::RetainedExact
                       || aWake.kind == ParkWakeKind::Cancellable;

    if( exact && aWake.token == 0 )
    {
        beacon( "REFUSED", "exact park has no wake token", ctx->id );
        r.refusals++;
        return ParkResult( false, 0 );
    }

    if( aWake.kind == ParkWakeKind::Cancellable && !aWake.cancel )
    {
        beacon( "REFUSED", "cancellable park has no revoker", ctx->id );
        r.refusals++;
        return ParkResult( false, 0 );
    }

    if( aWake.kind != ParkWakeKind::Cancellable && aWake.cancel )
    {
        beacon( "REFUSED", "non-cancellable park carries a revoker", ctx->id );
        r.refusals++;
        return ParkResult( false, 0 );
    }

    if( !exact && aWake.token != 0 )
    {
        beacon( "REFUSED", "ordinary/no-wake park carries an exact token", ctx->id );
        r.refusals++;
        return ParkResult( false, 0 );
    }

    // A deferred ordinary wake belongs to the next External park. It cannot
    // satisfy a newly-created exact lease: doing so would leave that timer or
    // wait callback pointing at a context which already resumed. Refuse before
    // changing registry state so the caller can revoke its new lease.
    if( ctx->has_pending_wake && aWake.kind != ParkWakeKind::External )
    {
        beacon( "REFUSED", "exact/no-wake park conflicts with a deferred wake", ctx->id );
        r.refusals++;
        return ParkResult( false, 0 );
    }

    // A deferred wake needs a FIFO claim. Publish that claim before consuming
    // the wake or changing the running context. If allocation fails, the
    // caller is still executing the exact same Running context and may
    // terminalize or retry without a lost wake.
    if( ctx->has_pending_wake
        && !publish_ready_id( ctx->id,
                              "yield_park() deferred-wake FIFO allocation failed" ) )
    {
        r.refusals++;
        return ParkResult( false, 0 );
    }

    ctx->status = Status::Parked;
    ctx->park_reason = aReason ? aReason : "";
    ctx->park_wake = aWake;
    ctx->parks++;
    r.running = 0;
    r.transition = false;   // the swap below completes this transition

    // A wake that arrived while this context was still Running
    // (a re-entrant resolve that raced ahead of this very park) was queued
    // rather than dropped. Deliver it now — the context is Parked, so it is
    // immediately eligible: mark it Ready and let the scheduler's drain swap
    // it back in on the next turn. Without this the wait hangs forever (the
    // footprint chooser's dead-app-after-close).
    if( ctx->has_pending_wake )
    {
        ctx->has_pending_wake = false;
        ctx->result = ctx->pending_wake_result;
        ctx->park_wake = ParkWake::None();
        ctx->status = Status::Ready;
    }

    // Yield to the scheduler. Control returns here when drain() swaps us back
    // in after mark_ready() — and ONLY then, because Parked→Ready→resume is
    // the single path in.
    emscripten_fiber_swap( &ctx->fiber, &r.scheduler_fiber );

    // Resumed. The registry set status/result before swapping in.
    note_asyncify_use( *ctx );
    return ParkResult( true, ctx->result );
}


inline bool mark_ready( ContextId aId, int aResult )
{
    Registry& r = reg();
    Context* ctx = find( aId );

    if( !ctx )
    {
        beacon( "REFUSED", "mark_ready() for an unknown context", aId );
        r.refusals++;
        return false;
    }

    if( ctx->status == Status::Running )
    {
        // The resolve raced ahead of the park — this context is
        // running a re-entrant chain that has not yet reached the yield it
        // will resume from (the footprint chooser resuming its own modal
        // opener). Dropping the wake hangs the wait; QUEUE it and let the
        // next yield_park deliver it. This is the deferred-wake law applied to
        // the registry. A second pending wake for the same context keeps the LAST
        // result (the innermost resolve), which is what a LIFO wait stack
        // wants.
        ctx->has_pending_wake = true;
        ctx->pending_wake_result = aResult;
        ++r.deferred_wakes;
        return true;
    }

    if( ctx->status != Status::Parked )
    {
        char detail[96];
        std::snprintf( detail, sizeof( detail ), "mark_ready() on a %s context",
                       status_name( ctx->status ) );
        beacon( "REFUSED", detail, aId );
        r.refusals++;
        return false;
    }

    if( ctx->park_wake.kind != ParkWakeKind::External )
    {
        beacon( "REFUSED", "ordinary wake does not own this park lease", aId );
        r.refusals++;
        return false;
    }

    if( !publish_ready_id( aId, "mark_ready() FIFO allocation failed" ) )
    {
        r.refusals++;
        return false;
    }

    ctx->result = aResult;
    ctx->park_wake = ParkWake::None();
    ctx->status = Status::Ready;
    return true;
}


inline bool mark_ready_owned( ContextId aId, int aResult, WakeToken aToken )
{
    Registry& r = reg();
    Context* ctx = find( aId );

    if( !ctx )
    {
        beacon( "REFUSED", "owned wake for an unknown context", aId );
        r.refusals++;
        return false;
    }

    if( ctx->status != Status::Parked
        || ctx->park_wake.kind != ParkWakeKind::Cancellable
        || aToken == 0 || ctx->park_wake.token != aToken )
    {
        beacon( "REFUSED", "owned wake does not match the parked lease", aId );
        r.refusals++;
        return false;
    }

    if( !publish_ready_id( aId, "mark_ready_owned() FIFO allocation failed" ) )
    {
        r.refusals++;
        return false;
    }

    ctx->result = aResult;
    ctx->park_wake = ParkWake::None();
    ctx->status = Status::Ready;
    return true;
}


inline bool mark_ready_retained( ContextId aId, int aResult, WakeToken aToken )
{
    Registry& r = reg();
    Context* ctx = find( aId );

    if( !ctx )
    {
        beacon( "REFUSED", "retained wake for an unknown context", aId );
        r.refusals++;
        return false;
    }

    if( ctx->status != Status::Parked
        || ctx->park_wake.kind != ParkWakeKind::RetainedExact
        || aToken == 0 || ctx->park_wake.token != aToken )
    {
        beacon( "REFUSED", "retained wake does not match the parked lease", aId );
        r.refusals++;
        return false;
    }

    if( !publish_ready_id( aId, "mark_ready_retained() FIFO allocation failed" ) )
    {
        r.refusals++;
        return false;
    }

    ctx->result = aResult;
    ctx->park_wake = ParkWake::None();
    ctx->status = Status::Ready;
    return true;
}


inline ContextId drain()
{
    Registry& r = reg();

    if( !on_main_thread() )
    {
        beacon( "REFUSED", "drain() off the main thread", 0 );
        r.refusals++;
        return 0;
    }

    // One transition at a time, and never re-entrantly from a context (a
    // context reaching drain() would make the star a cycle).
    if( r.transition || r.running != 0 )
        return 0;

    ensure_scheduler_context();

    // Skip ids that died or were consumed while queued.
    ContextId id = 0;
    Context* ctx = nullptr;

    while( !r.ready_fifo.empty() )
    {
        id = r.ready_fifo.front();
        r.ready_fifo.erase( r.ready_fifo.begin() );
        ctx = find( id );

        if( ctx && ctx->status == Status::Ready )
            break;

        ctx = nullptr;
    }

    if( !ctx )
        return 0;

    r.transition = true;
    r.transitions++;
    r.running = id;
    ctx->status = Status::Running;
    ctx->resumes++;

    // A fiber-lane context entered by the scheduler is also the lane's
    // current occupant, so libcontext's view of "who is on the CPU" stays
    // exact across a star transition.
    if( ctx->symmetric )
        r.fiber_running = id;

    // Swap in. Returns when the context parks (yield_park) or finishes; both
    // clear running/transition before swapping back.
    emscripten_fiber_swap( &r.scheduler_fiber, &ctx->fiber );

    r.transition = false;
    r.running = 0;
    r.fiber_running = 0;

    // The context object may still exist (parked) or be finished; either way
    // its buffer use is now measurable.
    if( Context* back = find( id ) )
        note_asyncify_use( *back );

    return id;
}


/**
 * Run ready contexts until the scheduler is quiescent.
 *
 * A star transition is not a swap-and-return: when A transfers to B, A parks
 * and B merely becomes RUNNABLE, so somebody has to keep draining or the work
 * stalls. That somebody must be the scheduler stack — this is the top-level
 * pump, called from a fresh JS task, never from a context.
 *
 * One task receives a fixed transition budget.  If ready work remains, report
 * that fact to the caller: only the caller owns the JavaScript arbiter which
 * can schedule a non-recursive continuation on a fresh task.  A finite chain
 * therefore cannot be stranded at an arbitrary batching boundary.
 *
 * Quiescence resets the cross-task streak.  Exhausting too many consecutive
 * batches without quiescing is a deterministic livelock, not ordinary
 * backpressure; report it as terminal instead of scheduling forever.
 */
inline DrainResult drain_all()
{
    Registry& r = reg();
    DrainResult result;

    while( result.transitions < DrainTransitionsPerPump && drain() )
        ++result.transitions;

    bool readyRemains = false;

    for( ContextId id : r.ready_fifo )
    {
        Context* ctx = find( id );

        if( ctx && ctx->status == Status::Ready )
        {
            readyRemains = true;
            break;
        }
    }

    if( !readyRemains )
    {
        r.drain_budget_exhaustion_streak = 0;
        return result;
    }

    ++r.drain_budget_exhaustion_streak;

    if( r.drain_budget_exhaustion_streak
            >= DrainMaxConsecutiveBudgetExhaustions )
    {
        ++r.drain_livelocks;
        result.disposition = DrainDisposition::Livelock;
        beacon( "DRAIN-CAP",
                "ready work survived 64 bounded pumps - transfer livelock",
                0 );
        return result;
    }

    ++r.drain_budget_yields;
    result.disposition = DrainDisposition::ContinueOnFreshTask;
    beacon( "DRAIN-CONTINUE",
            "ready work remains after 4096 transitions - fresh task required",
            0 );
    return result;
}


inline bool can_yield_here()
{
    Context* ctx = find( reg().running );
    return ctx && on_context_stack( *ctx );
}


inline bool has_pending_wake( ContextId aId )
{
    Context* ctx = find( aId );
    return ctx && ctx->has_pending_wake;
}


inline bool transition_in_flight()
{
    return reg().transition;
}


inline bool abandon_transition()
{
    Registry& r = reg();

    if( !r.transition && !r.running )
        return false;

    if( Context* ctx = find( r.running ) )
    {
        // The context's stack is half-unwound by the escaping exception: its
        // saved capture describes frames that no longer exist. Finished means
        // "terminal" everywhere in this layer — drain() never picks it,
        // fiber_transfer refuses into it, fiber_release lets it go quietly.
        beacon( "TRANSITION-ABANDONED",
                "a context died abnormally (exception through the swap) - poisoned",
                ctx->id );
        ctx->status = Status::Finished;
        ctx->park_reason = "abandoned";
        r.finished++;
    }

    r.transition = false;
    r.running = 0;
    r.fiber_running = 0;
    return true;
}


inline ContextId current()
{
    return reg().running;
}


inline Status status_of( ContextId aId )
{
    Context* ctx = find( aId );
    return ctx ? ctx->status : Status::Finished;
}


inline size_t last_park_use_of( ContextId aId )
{
    Context* ctx = find( aId );
    return ctx ? ctx->last_park_use : 0;
}


inline const char* park_reason_of( ContextId aId )
{
    Context* ctx = find( aId );
    return ctx && ctx->park_reason ? ctx->park_reason : "";
}


inline bool destroy( ContextId aId )
{
    Registry& r = reg();
    Context* ctx = find( aId );

    if( !ctx )
        return false;

    if( ctx->symmetric )
    {
        // Fiber-lane lifetimes go through fiber_release(): its rules (a
        // suspended fiber MAY be dropped) are libcontext's, not the star's.
        beacon( "REFUSED", "destroy() on a fiber-lane context - use fiber_release()", aId );
        r.fiber_refusals++;
        return false;
    }

    if( ctx->status != Status::Finished )
    {
        // Freeing a parked context's stack would strand whatever is on it —
        // the very failure mode this layer exists to make impossible.
        char detail[96];
        std::snprintf( detail, sizeof( detail ), "destroy() on a %s context",
                       status_name( ctx->status ) );
        beacon( "REFUSED", detail, aId );
        r.refusals++;
        return false;
    }

    r.bytes -= ctx->c_stack.size + ctx->asyncify_stack.size;
    r.live--;
    release_generated_fiber_guard( *ctx );
    r.contexts.erase( aId );
    delete ctx;
    return true;
}


// ---------------------------------------------------------------------------
// Direct fiber lane — implementation
// ---------------------------------------------------------------------------

namespace detail
{

// Not a ceiling (a refusal here would change libcontext behaviour, which is
// unbounded); a beacon threshold that turns "coroutines are leaking" into a
// loud line long before the tab dies.
constexpr size_t FIBER_POPULATION_BEACON = 256;

inline bool register_fiber_context( Context* aCtx )
{
    Registry& r = reg();

    try
    {
        const auto inserted = r.contexts.emplace( aCtx->id, aCtx );

        if( !inserted.second )
        {
            beacon( "REFUSED", "duplicate fiber id during publication", aCtx->id );
            r.fiber_refusals++;
            return false;
        }
    }
    catch( ... )
    {
        beacon( "REFUSED", "fiber registry allocation failed", aCtx->id );
        r.fiber_refusals++;
        return false;
    }

    r.fiber_created++;
    r.fiber_live++;

    if( r.fiber_live > r.fiber_peak_live )
        r.fiber_peak_live = r.fiber_live;

    if( r.fiber_live == FIBER_POPULATION_BEACON )
        beacon( "FIBER-POPULATION-HIGH", "live fiber count crossed the beacon threshold",
                aCtx->id );

    r.fiber_bytes += aCtx->c_stack.size + aCtx->asyncify_stack.size;

    if( r.fiber_bytes > r.fiber_peak_bytes )
        r.fiber_peak_bytes = r.fiber_bytes;

    return true;
}

} // namespace detail


inline ContextId fiber_adopt_current( size_t aAsyncifyBytes, const char* aLabel )
{
    Registry& r = reg();

    if( !on_main_thread() )
    {
        beacon( "REFUSED", "fiber_adopt_current() off the main thread", 0 );
        r.fiber_refusals++;
        return 0;
    }

    auto* ctx = new( std::nothrow ) Context();

    if( !ctx )
    {
        beacon( "REFUSED", "fiber_adopt_current() allocation failed", 0 );
        r.fiber_refusals++;
        return 0;
    }

    ctx->id = reserve_context_id();

    if( !ctx->id )
    {
        delete ctx;
        return 0;
    }
    ctx->label = aLabel ? aLabel : "";
    ctx->symmetric = true;
    ctx->asyncify_stack.allocate( aAsyncifyBytes );

    if( !ctx->asyncify_stack.base )
    {
        beacon( "REFUSED", "fiber_adopt_current() asyncify allocation failed", 0 );
        r.fiber_refusals++;
        delete ctx;
        return 0;
    }

    ctx->adopted_root = true;

    // Emscripten changes its live stack limits on every fiber switch. The
    // registry captured the browser main range before its first switch. Adopt
    // only when both the live limits and this physical frame prove that exact
    // range. Do not call emscripten_stack_init() here: doing so would overwrite
    // evidence if a caller accidentally arrived on a foreign stack.
    emscripten_fiber_init_from_current_context( &ctx->fiber,
                                                ctx->asyncify_stack.base,
                                                ctx->asyncify_stack.size );

    const uintptr_t stackBase = reinterpret_cast<uintptr_t>( ctx->fiber.stack_base );
    const uintptr_t stackEnd = reinterpret_cast<uintptr_t>( ctx->fiber.stack_limit );
    char stackProbe = 0;
    const uintptr_t here = reinterpret_cast<uintptr_t>( &stackProbe );

    if( stackBase <= stackEnd || stackBase != r.main_stack_base
        || stackEnd != r.main_stack_end || here < stackEnd || here >= stackBase )
    {
        char detail[192];
        std::snprintf( detail, sizeof( detail ),
                       "fiber_adopt_current() main-stack authority failed "
                       "captured=[%zu,%zu] live=[%zu,%zu] here=%zu",
                       static_cast<size_t>( r.main_stack_end ),
                       static_cast<size_t>( r.main_stack_base ),
                       static_cast<size_t>( stackEnd ),
                       static_cast<size_t>( stackBase ),
                       static_cast<size_t>( here ) );
        beacon( "REFUSED", detail, 0 );
        r.fiber_refusals++;
        delete ctx;
        return 0;
    }

    ctx->c_stack.adopt( reinterpret_cast<void*>( stackEnd ),
                        static_cast<size_t>( stackBase - stackEnd ) );

    ctx->status = Status::Running;
    ctx->park_reason = "adopted";

    if( !register_fiber_context( ctx ) )
    {
        delete ctx;
        return 0;
    }

    r.fiber_running = ctx->id;
    return ctx->id;
}


inline ContextId fiber_create( void ( *aEntry )( void* ), void* aArg,
                               void* aStackBottom, size_t aStackBytes,
                               size_t aAsyncifyBytes, const char* aLabel )
{
    Registry& r = reg();

    if( !on_main_thread() )
    {
        beacon( "REFUSED", "fiber_create() off the main thread", 0 );
        r.fiber_refusals++;
        return 0;
    }

    // A null stack pointer is legal and means "allocate one for me" (below);
    // a zero SIZE never is.
    if( !aEntry || !aStackBytes )
    {
        beacon( "REFUSED", "fiber_create() with a null entry or zero stack size", 0 );
        r.fiber_refusals++;
        return 0;
    }

    auto* ctx = new( std::nothrow ) Context();

    if( !ctx )
    {
        beacon( "REFUSED", "fiber_create() allocation failed", 0 );
        r.fiber_refusals++;
        return 0;
    }

    ctx->id = reserve_context_id();

    if( !ctx->id )
    {
        delete ctx;
        return 0;
    }
    ctx->label = aLabel ? aLabel : "";
    ctx->symmetric = true;
    ctx->entry = aEntry;
    ctx->arg = aArg;

    // A null stack means "you own it": the scheduler's own long-lived contexts
    // (the dispatch context) have no KiCad allocation behind them.
    if( aStackBottom )
    {
        // Trap 1 (doc 22 §7): a fiber C stack must be 16-aligned or every
        // EM_ASM on it traps in readEmAsmArgs. Adopted stacks are whatever
        // the caller allocated — KiCad's COROUTINE maps pages (aligned), but
        // a plain new char[] is 8-aligned and lands on 8-mod-16 by heap-
        // history luck, which reads as an "environmental" failure because any
        // unrelated allocation change moves it. Adopt the largest aligned
        // sub-range instead of trusting luck: bottom rounds up, size rounds
        // down, top stays 16-aligned (≤30 bytes lost).
        char* rawBottom = static_cast<char*>( aStackBottom );
        char* alignedBottom = reinterpret_cast<char*>(
                ( reinterpret_cast<uintptr_t>( rawBottom ) + 15u ) & ~uintptr_t( 15 ) );
        const size_t dropped = static_cast<size_t>( alignedBottom - rawBottom );

        if( aStackBytes <= dropped )
        {
            beacon( "REFUSED", "fiber_create() stack too small to align", 0 );
            r.fiber_refusals++;
            delete ctx;
            return 0;
        }

        ctx->c_stack.adopt( alignedBottom, ( aStackBytes - dropped ) & ~size_t( 15 ) );
    }
    else
    {
        ctx->c_stack.allocate( aStackBytes );
    }

    aStackBottom = ctx->c_stack.base;
    aStackBytes = ctx->c_stack.size;

    if( !aStackBottom )
    {
        beacon( "REFUSED", "fiber_create() stack allocation failed", 0 );
        r.fiber_refusals++;
        delete ctx;
        return 0;
    }

    ctx->asyncify_stack.allocate( aAsyncifyBytes );

    if( !ctx->asyncify_stack.base )
    {
        beacon( "REFUSED", "fiber_create() asyncify allocation failed", 0 );
        r.fiber_refusals++;
        delete ctx;
        return 0;
    }

    // The entry is the caller's own trampoline (libcontext's), not the star's
    // context_trampoline: the fiber lane preserves the caller's protocol.
    emscripten_fiber_init( &ctx->fiber, aEntry, aArg,
                           aStackBottom, aStackBytes,
                           ctx->asyncify_stack.base, ctx->asyncify_stack.size );

    ctx->status = Status::Fresh;   // enterable: first swap-in takes the entry path
    ctx->park_reason = "created";

    if( !register_fiber_context( ctx ) )
    {
        delete ctx;
        return 0;
    }

    return ctx->id;
}


inline bool fiber_enterable( ContextId aId )
{
    Context* ctx = find( aId );

    if( !ctx || !ctx->symmetric )
        return false;

    // A context whose body holds an in-flight in-place Asyncify park
    // is unenterable regardless of status — its fiber capture is stale (the
    // park suspended the body without a fiber swap) and only its own wake may
    // resume it. This registry fact cannot be changed by the attribution
    // laundering path (a stale
    // g_current_context re-marking swap_suspended on the parked fiber) cannot
    // deceive it, because it never consults the protocol's own flags.
    if( ctx->inplace_parks > 0 )
        return false;

    // A transfer-parked context has no external wake and holds a valid capture
    // exactly like a symmetric Suspended one. A wait-parked context is owned
    // by its recorded wake and must not be entered through libcontext.
    return ctx->status == Status::Fresh || ctx->status == Status::Suspended
        || ( ctx->status == Status::Parked
             && ctx->park_wake.kind == ParkWakeKind::None );
}


inline bool fiber_swap( ContextId aFrom, ContextId aTo )
{
    Registry& r = reg();
    Context* to = find( aTo );

    if( !to || !to->symmetric )
    {
        // Unknown target = the caller holds a stale id (today: use-after-free
        // and a crash; here: a loud refusal the caller can contain).
        beacon( "REFUSED", "fiber_swap() unknown or non-fiber target", aTo );
        r.fiber_refusals++;
        return false;
    }

    Context* from = find( aFrom );

    if( !from || !from->symmetric )
    {
        beacon( "REFUSED", "fiber_swap() unknown or non-fiber source", aFrom );
        r.fiber_refusals++;
        return false;
    }

    if( from == to )
    {
        beacon( "REFUSED", "fiber_swap() self-swap", aTo );
        r.fiber_refusals++;
        return false;
    }

    // The explicit source is an identity, not authority. Prove all three
    // representations of physical occupancy before letting Emscripten save
    // the current rewind into `from->fiber`: registry status, direct-lane
    // occupant, and the address range of this exact C stack must agree.
    const bool sourceOnStack = on_context_stack( *from );

    // The scheduler and the adopted libcontext root use the same physical
    // browser/main stack. drain() deliberately publishes fiber_running=0 while
    // quiescent. A later direct call from that exact stack may re-establish the
    // adopted root as occupant, but only when no star transition or context is
    // active. Allocated/suspended fibers can never take this recovery path.
    if( r.fiber_running == 0 && r.running == 0 && !r.transition
        && from->status == Status::Running && from->adopted_root
        && sourceOnStack && from->inplace_parks == 0 )
    {
        r.fiber_running = aFrom;
    }

    if( from->status != Status::Running || r.fiber_running != aFrom
        || !sourceOnStack || from->inplace_parks > 0 )
    {
        char detail[160];
        std::snprintf( detail, sizeof( detail ),
                       "fiber_swap() source authority failed status=%s occupant=%u "
                       "onStack=%s inplaceParks=%d",
                       status_name( from->status ), r.fiber_running,
                       sourceOnStack ? "true" : "false", from->inplace_parks );
        beacon( "REFUSED", detail, aFrom );
        r.fiber_refusals++;
        return false;
    }

    // This is the authoritative stale-rewind gate. The protocol adapter keeps
    // a duplicate suspension bit, but disagreement must never reach
    // emscripten_fiber_swap: only the registry records in-place parks, ready
    // claims, terminal contexts, and exact wake ownership together.
    if( !fiber_enterable( aTo ) )
    {
        char detail[96];
        std::snprintf( detail, sizeof( detail ),
                       "fiber_swap() target is %s - stale rewind refused",
                       status_name( to->status ) );
        beacon( "REFUSED", detail, aTo );
        r.fiber_refusals++;
        return false;
    }

    from->status = Status::Suspended;
    from->park_reason = "fiber-swap-out";
    from->parks++;
    to->status = Status::Running;
    to->resumes++;
    r.fiber_running = aTo;
    r.fiber_swaps++;

    // Measure the TARGET's buffer now, while it still holds its suspended
    // capture — after the swap consumes it the pointer is back at base and
    // the high-water would always read 0 (the real-capture sizing input).
    note_asyncify_use( *to );

    emscripten_fiber_swap( &from->fiber, &to->fiber );

    // Resumed: whoever swapped back in already set our status and
    // fiber_running through this same funnel.
    return true;
}


inline ContextId fiber_current()
{
    return reg().fiber_running;
}


inline bool fiber_start( ContextId aId, intptr_t aValue )
{
    Registry& r = reg();
    Context* ctx = find( aId );

    if( !ctx || !ctx->symmetric )
    {
        beacon( "REFUSED", "fiber_start() unknown or non-fiber context", aId );
        r.fiber_refusals++;
        return false;
    }

    if( r.running != 0 )
    {
        // Starting from a context would be a transfer, and a transfer must
        // park its source; going through here instead would leave two
        // contexts runnable and the source's frame stranded.
        beacon( "REFUSED", "fiber_start() from a context - use fiber_transfer()", aId );
        r.fiber_refusals++;
        return false;
    }

    if( ( ctx->status != Status::Fresh && ctx->status != Status::Suspended )
        || !fiber_enterable( aId ) )
        return false;

    if( !publish_ready_id( aId, "fiber_start() FIFO allocation failed" ) )
    {
        r.fiber_refusals++;
        return false;
    }

    ctx->transfer = aValue;
    ctx->status = Status::Ready;
    return true;
}


inline intptr_t fiber_transfer( ContextId aFrom, ContextId aTo, intptr_t aValue )
{
    Registry& r = reg();
    Context* from = find( aFrom );
    Context* to = find( aTo );

    if( !from || !to || !from->symmetric || !to->symmetric )
    {
        beacon( "REFUSED", "fiber_transfer() with an unknown or non-fiber party", aTo );
        r.fiber_refusals++;
        return 0;
    }

    if( from == to )
    {
        beacon( "REFUSED", "fiber_transfer() self-transfer", aTo );
        r.fiber_refusals++;
        return 0;
    }

    if( r.running != aFrom || from->status != Status::Running
        || !can_yield_here() )
    {
        beacon( "REFUSED", "fiber_transfer() source does not own the running stack", aFrom );
        r.fiber_refusals++;
        return 0;
    }

    // A Finished context is terminal — its trampoline took the
    // finish-transfer and will never run again. Transferring into it is a
    // ghost jump (a stale handle); refuse WITHOUT parking the source, so the
    // caller's jump_fcontext sees an unchanged epoch and takes its
    // established ghost contract (null INVOCATION_ARGS).
    if( !fiber_enterable( aTo ) )
    {
        beacon( "FIBER-TRANSFER-INTO-NONENTERABLE", "ghost transfer refused", aTo );
        r.fiber_refusals++;
        return 0;
    }

    // Hand the protocol value over and make the target RUNNABLE — not running.
    // The scheduler performs every star-lane entry. Unlike the direct lane,
    // no caller enters a star context by itself.
    if( !publish_ready_id( aTo, "fiber_transfer() FIFO allocation failed" ) )
    {
        r.fiber_refusals++;
        return 0;
    }

    to->transfer = aValue;
    to->status = Status::Ready;

    from->status = Status::Parked;
    from->park_reason = "fiber-transfer";
    from->park_wake = ParkWake::None();
    from->parks++;
    r.fiber_swaps++;
    r.running = 0;
    r.fiber_running = 0;
    r.transition = false;   // the swap below completes this transition

    // Yield to the scheduler, which will enter the target. Control returns
    // here only when somebody later transfers to US.
    emscripten_fiber_swap( &from->fiber, &r.scheduler_fiber );

    note_asyncify_use( *from );
    return from->transfer;
}


/**
 * A coroutine's terminal star transfer: its entry has returned, so hand
 * aValue to aTo, mark aFrom Finished — never re-queueable, never re-entered —
 * and yield to the scheduler forever. The former trampoline's "if someone
 * swaps back to us, loop" ghost re-entry is replaced by the registry refusing
 * transfers into Finished contexts (the caller's ghost contract handles it).
 * Must be called ON aFrom's stack; never returns control to the caller.
 */
[[noreturn]] inline void fiber_finish_transfer( ContextId aFrom, ContextId aTo,
                                                intptr_t aValue )
{
    Registry& r = reg();
    Context* from = find( aFrom );
    Context* to = find( aTo );

    if( !from || !from->symmetric )
    {
        beacon( "REFUSED", "fiber_finish_transfer() unknown source", aFrom );
        r.fiber_refusals++;
        std::abort();
    }

    if( r.running != aFrom || !can_yield_here() )
    {
        beacon( "REFUSED", "fiber_finish_transfer() source does not own the running stack",
                aFrom );
        r.fiber_refusals++;
        std::abort();
    }

    if( !to || !to->symmetric || to == from || !fiber_enterable( aTo ) )
    {
        beacon( "REFUSED", "fiber_finish_transfer() target is not enterable", aTo );
        r.fiber_refusals++;
        std::abort();
    }

    if( !publish_ready_id( aTo,
                           "fiber_finish_transfer() FIFO allocation failed" ) )
    {
        r.fiber_refusals++;
        std::abort();
    }

    to->transfer = aValue;
    to->status = Status::Ready;

    from->status = Status::Finished;
    from->park_reason = "finished";
    from->parks++;
    r.fiber_swaps++;
    r.running = 0;
    r.fiber_running = 0;
    r.transition = false;

    // Terminal yield: drain() only picks Ready contexts, so this swap never
    // returns. The fiber's stack and buffer are reclaimed by fiber_release
    // (libcontext's refcount) — Finished status makes that release quiet.
    emscripten_fiber_swap( &from->fiber, &r.scheduler_fiber );

    // Unreachable in practice; a raw fiber entry must never return, so if a
    // buggy resume ever lands here, park forever rather than fall out.
    for( ;; )
    {
        from->status = Status::Finished;
        emscripten_fiber_swap( &from->fiber, &r.scheduler_fiber );
    }
}


/**
 * A coroutine's terminal handoff on the symmetric/direct lane. Unlike an
 * ordinary jump, completion cannot ghost-return to its finished caller. A
 * stale target is therefore terminal: abort before changing either context or
 * writing a rewind capture.
 */
[[noreturn]] inline void fiber_finish_swap( ContextId aFrom, ContextId aTo,
                                            intptr_t aValue )
{
    Registry& r = reg();
    Context* from = find( aFrom );
    Context* to = find( aTo );

    if( !from || !to || !from->symmetric || !to->symmetric )
    {
        beacon( "REFUSED", "fiber_finish_swap() with an unknown or non-fiber party", aTo );
        r.fiber_refusals++;
        std::abort();
    }

    if( from == to )
    {
        beacon( "REFUSED", "fiber_finish_swap() self-swap", aTo );
        r.fiber_refusals++;
        std::abort();
    }

    const bool sourceOnStack = on_context_stack( *from );

    if( r.fiber_running == 0 && r.running == 0 && !r.transition
        && from->status == Status::Running && from->adopted_root
        && sourceOnStack && from->inplace_parks == 0 )
    {
        r.fiber_running = aFrom;
    }

    if( from->status != Status::Running || r.fiber_running != aFrom
        || !sourceOnStack || from->inplace_parks > 0 )
    {
        beacon( "REFUSED",
                "fiber_finish_swap() source does not own the running direct-lane stack",
                aFrom );
        r.fiber_refusals++;
        std::abort();
    }

    if( !fiber_enterable( aTo ) )
    {
        char detail[96];
        std::snprintf( detail, sizeof( detail ),
                       "terminal swap target is %s - stale rewind refused",
                       status_name( to->status ) );
        beacon( "REFUSED", detail, aTo );
        r.fiber_refusals++;
        std::abort();
    }

    to->transfer = aValue;
    from->status = Status::Finished;
    from->park_reason = "finished";
    from->parks++;
    to->status = Status::Running;
    to->resumes++;
    r.fiber_running = aTo;
    r.fiber_swaps++;

    note_asyncify_use( *to );
    emscripten_fiber_swap( &from->fiber, &to->fiber );

    // A Finished source is terminal. Reaching this point means somebody
    // illegally re-entered it, so stop before its caller-owned stack can be
    // mistaken for live state again.
    beacon( "REFUSED", "a Finished fiber resumed after terminal swap", aFrom );
    std::abort();
}


inline bool fiber_release( ContextId aId )
{
    Registry& r = reg();
    Context* ctx = find( aId );

    if( !ctx || !ctx->symmetric )
        return false;

    if( ctx->status == Status::Running || ctx->inplace_parks > 0 )
    {
        // An in-place handleSleep wake owns a malloc'd Asyncify capture whose
        // frames point into this fiber's caller-owned C stack. The delayed JS
        // callback will restore that capture. Freeing either allocation here
        // is therefore a deterministic use-after-free. Do not mutate any
        // registry state: the wake must still be able to end its park. The
        // libcontext adapter fail-stops when it sees this refusal, preventing
        // its COROUTINE destructor from freeing the C stack.
        beacon( "FIBER-RELEASE-LIVE",
                ctx->inplace_parks > 0
                        ? "release refused: in-place Asyncify wake still owns the stack"
                        : "release refused: context is still running",
                aId );
        r.fiber_refusals++;
        r.fiber_release_refusals++;
        return false;
    }

    if( ctx->status == Status::Suspended )
        r.fiber_released_suspended++;

    if( ctx->status == Status::Parked
        && ( ctx->park_wake.kind == ParkWakeKind::External
             || ctx->park_wake.kind == ParkWakeKind::RetainedExact ) )
    {
        beacon( "FIBER-RELEASE-WAKE-LIVE",
                "release refused: external wake cannot be cancelled",
                aId );
        r.fiber_refusals++;
        r.fiber_release_refusals++;
        return false;
    }

    if( ctx->status == Status::Parked
        && ctx->park_wake.kind == ParkWakeKind::Cancellable )
    {
        // The timer/Promise source still retains this monotonic ContextId.
        // Reclaim the Context only after that source confirms its callback is
        // no longer delayed or queued. A refusal leaves all native state
        // intact so the legitimate wake can still complete safely.
        if( !ctx->park_wake.cancel( aId, ctx->park_wake.token ) )
        {
            beacon( "FIBER-RELEASE-WAKE-LIVE",
                    "release refused: external wake could not be cancelled",
                    aId );
            r.fiber_refusals++;
            r.fiber_release_refusals++;
            return false;
        }
        ctx->park_wake = ParkWake::None();
    }

    release_generated_fiber_guard( *ctx );

    r.fiber_bytes -= ctx->c_stack.size + ctx->asyncify_stack.size;
    r.fiber_live--;
    r.fiber_released++;
    r.contexts.erase( aId );
    delete ctx;
    return true;
}


inline std::string stats_json()
{
    Registry& r = reg();
    char buf[1024];
    std::snprintf( buf, sizeof( buf ),
                   "{\"live\":%zu,\"peakLive\":%zu,\"created\":%u,\"finished\":%u,"
                   "\"transitions\":%u,\"refusals\":%u,\"foreignStackRefusals\":%u,"
                   "\"readyPublicationFailures\":%u,\"running\":%u,"
                   "\"transitionInFlight\":%s,\"readyQueued\":%zu,"
                   "\"drainBudgetExhaustionStreak\":%zu,"
                   "\"drainBudgetYields\":%u,\"drainLivelocks\":%u,"
                   "\"bytes\":%zu,\"peakBytes\":%zu,"
                   "\"perContextBytes\":%zu,\"cStackBytes\":%zu,\"asyncifyBytes\":%zu,"
                   "\"asyncifyHighWater\":%zu,"
                   "\"fiberLive\":%zu,\"fiberPeakLive\":%zu,\"fiberCreated\":%u,"
                   "\"fiberReleased\":%u,\"fiberSwaps\":%u,\"fiberRefusals\":%u,"
                   "\"fiberReleasedSuspended\":%u,\"fiberReleasedRunning\":%u,"
                   "\"fiberReleaseRefusals\":%u,"
                   "\"fiberNonEnterableSwaps\":%u,"
                   "\"fiberRunning\":%u,\"fiberBytes\":%zu,\"fiberPeakBytes\":%zu,"
                   "\"fiberAsyncifyHighWater\":%zu}",
                   r.live, r.peak_live, r.created, r.finished,
                   r.transitions, r.refusals, r.foreign_stack_refusals,
                   r.ready_publication_failures, r.running,
                   r.transition ? "true" : "false", r.ready_fifo.size(),
                   r.drain_budget_exhaustion_streak,
                   r.drain_budget_yields, r.drain_livelocks,
                   r.bytes, r.peak_bytes,
                   DEFAULT_C_STACK_BYTES + DEFAULT_ASYNCIFY_BYTES,
                   DEFAULT_C_STACK_BYTES, DEFAULT_ASYNCIFY_BYTES,
                   r.asyncify_high_water,
                   r.fiber_live, r.fiber_peak_live, r.fiber_created,
                   r.fiber_released, r.fiber_swaps, r.fiber_refusals,
                   r.fiber_released_suspended, r.fiber_released_running,
                   r.fiber_release_refusals,
                   r.fiber_nonenterable_swaps,
                   r.fiber_running, r.fiber_bytes, r.fiber_peak_bytes,
                   r.fiber_asyncify_high_water );
    return buf;
}


inline std::string registry_json()
{
    std::string out = "[";
    bool first = true;

    for( const auto& [id, ctx] : reg().contexts )
    {
        char entry[288];
        std::snprintf( entry, sizeof( entry ),
                       "%s{\"id\":%u,\"kind\":\"%s\",\"label\":\"%s\",\"status\":\"%s\","
                       "\"reason\":\"%s\","
                       "\"parks\":%u,\"resumes\":%u,\"asyncifyHighWater\":%zu}",
                       first ? "" : ",", id, ctx->symmetric ? "fiber" : "star",
                       ctx->label, status_name( ctx->status ),
                       ctx->park_reason, ctx->parks, ctx->resumes,
                       ctx->asyncify_high_water );
        out += entry;
        first = false;
    }

    out += "]";
    return out;
}


inline void reset_stats()
{
    Registry& r = reg();
    r.created = 0;
    r.finished = 0;
    r.transitions = 0;
    r.refusals = 0;
    r.foreign_stack_refusals = 0;
    r.ready_publication_failures = 0;
    r.fail_next_ready_fifo_allocation = false;
    r.drain_budget_exhaustion_streak = 0;
    r.drain_budget_yields = 0;
    r.drain_livelocks = 0;
    r.peak_live = r.live;
    r.peak_bytes = r.bytes;
    r.asyncify_high_water = 0;
    r.fiber_created = 0;
    r.fiber_released = 0;
    r.fiber_swaps = 0;
    r.fiber_refusals = 0;
    r.fiber_released_suspended = 0;
    r.fiber_released_running = 0;
    r.fiber_release_refusals = 0;
    r.fiber_nonenterable_swaps = 0;
    r.fiber_peak_live = r.fiber_live;
    r.fiber_peak_bytes = r.fiber_bytes;
    r.fiber_asyncify_high_water = 0;
}


inline void fail_next_ready_fifo_allocation_for_test()
{
    reg().fail_next_ready_fifo_allocation = true;
}

} // namespace pcbjam_sched
