/*
 * Scheduler contexts — Design B core (pcbjam docs/features/async/20 §5, D1/D2).
 *
 * HEADER-ONLY, and living in wx's wasm port rather than pcbjam's wasm/ layer,
 * because D2 puts the wx event dispatch itself on a context: evtloop.cpp must
 * see this, and adding a .cpp to wx's build means bakefile regeneration.
 * One implementation, included by wx, by KiCad's bridges (D4), and by the
 * harness (tests/apps/standalone/sched-context).
 *
 * THE RULE THIS EXISTS TO ENFORCE: no activity may park "in place". A
 * suspendable activity runs on a scheduler-owned context (its own stack + its
 * own asyncify buffer) and suspends by YIELDING THAT CONTEXT back to the
 * scheduler. The registry below is then authoritative about what is parked and
 * why — never a fiber struct saying one thing while its body sits in a
 * handleSleep the fiber layer cannot see (docs/features/async/19).
 *
 * HOW THIS DIFFERS FROM libcontext (kicad/thirdparty/libcontext), which stays
 * exactly as it is:
 *
 *   - libcontext is SYMMETRIC: any stack may jump_fcontext to any other, so
 *     "is the target safe to enter?" has no recorded answer and is guessed
 *     (swap_suspended, the parked/hot-main refusals). This layer is a STAR:
 *     contexts only ever swap OUT to the scheduler, and only the scheduler
 *     swaps IN. A resume is therefore never a guess — the registry says the
 *     context is Parked/Ready and holds its buffer.
 *   - At most ONE transition is in flight, enforced here rather than hoped for.
 *
 * SCOPE: primitives + registry + memory accounting. The wx tick's dispatch
 * runs on a context as of D2; waits move at D3, bridges at D4.
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

/** Registry truth for one context. Only the scheduler mutates it. */
enum class Status
{
    Fresh,      ///< created, never entered
    Running,    ///< currently executing (at most one, plus the scheduler)
    Parked,     ///< yielded, waiting for mark_ready()
    Ready,      ///< mark_ready() called, waiting for drain() to swap it in
    Finished,   ///< entry returned; stack/buffer reclaimable
    Suspended   ///< fiber lane only: suspended by a symmetric swap; its saved
                ///< rewind data is valid, so it is safe to enter (Phase A)
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
 * Park the CURRENTLY RUNNING context and yield to the scheduler. Returns the
 * result passed to mark_ready(). Must be called on a context (never on the
 * scheduler stack) — returns -1 immediately otherwise.
 *
 * aReason is recorded in the registry: "why is this parked" is exactly the
 * question the doc-19 guessing layer could not answer.
 */
int yield_park( const char* aReason );

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
 * Scheduler entry: resume at most ONE Ready context, running it until it
 * parks or finishes. Returns the id it ran, or 0 if there was nothing to run
 * (or a transition was already in flight). Call from a clean stack — a fresh
 * JS task, never from inside an awaited export (#13302, doc 17 S3).
 */
ContextId drain();

/**
 * Would yield_park() succeed right now? True only when a context is running
 * AND the caller's frame lies inside that context's own stack — i.e. the
 * caller owns what it would be parking. Callers use this to choose the
 * context park over an in-place one without provoking a refusal beacon.
 */
bool can_yield_here();

/** True while a swap is in flight; drain() refuses to start another. */
bool transition_in_flight();

/** The running context's id, or 0 when the scheduler stack is running. */
ContextId current();

Status status_of( ContextId aId );

/** Why a context is parked (the string yield_park was given), "" if unknown. */
const char* park_reason_of( ContextId aId );

/** Destroy a Finished context and release its stack + buffer. */
bool destroy( ContextId aId );

// ---------------------------------------------------------------------------
// The FIBER LANE (doc 22 Phase A) — libcontext's clients, absorbed.
//
// These carry libcontext's SYMMETRIC semantics — any registered fiber may swap
// to any other, the caller decides — so that libcontext's wasm backend can
// become a thin adapter over this registry with identical observable
// behaviour. The registry then knows every tool fiber (stack range, buffer,
// status) and performs every emscripten_fiber_swap in one place; the star
// invariants above are untouched. Phase B collapses the two lanes into the
// star; until then a symmetric context never enters the ready FIFO, is never
// picked by drain(), and never counts against the star's memory gate (it has
// its own counters).
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
 * Is aId safe to enter? A registry lookup — Fresh (first entry) or Suspended
 * (valid saved rewind data). Running/unknown means entering would rewind
 * stale or foreign state. This is the answer libcontext's swap_suspended
 * flag guessed at; the adapter keeps that flag only as a cross-check.
 */
bool fiber_enterable( ContextId aId );

/**
 * THE symmetric swap: suspend aFrom and enter aTo. Every libcontext swap
 * funnels through here, so the registry always knows who is on the CPU.
 *
 * aFrom is EXPLICIT, never inferred from fiber_current(): after a handleSleep
 * park the lane's "current" is stale in exactly the way libcontext's
 * g_current_context is (a wasm-only state neither layer can see), and an
 * inferred from would mark the WRONG context Suspended — measured 2026-08-06,
 * eeschema-collab: the real swapper stayed "Running" forever and every later
 * jump into it was wrongly refused. The caller knows who is swapping out;
 * mirroring its answer keeps the two layers in lockstep by construction.
 *
 * Deliberately does NOT refuse a non-enterable target (Phase A is
 * behaviour-preserving; the policy refusal stays in jump_fcontext, reading
 * fiber_enterable()) — it beacons and counts such a swap as a tripwire
 * instead. Returns false (no swap) only for an unknown / non-fiber endpoint.
 */
bool fiber_swap( ContextId aFrom, ContextId aTo );

/** The fiber lane's current occupant (the adopted root counts), 0 if none. */
ContextId fiber_current();

/**
 * Unregister a fiber and free its asyncify buffer (its C stack belongs to the
 * caller). ALWAYS releases — libcontext's refcount drop deleted the struct
 * unconditionally, and a registry that refuses while the caller frees anyway
 * holds a permanent ghost (measured 2026-08-06: the ghost then poisoned
 * every later enterability answer). A release in the Suspended or Running
 * state is counted; Running additionally beacons as a tripwire.
 */
bool fiber_release( ContextId aId );

/**
 * A symmetric swap expressed as a STAR transition (Phase B): park aFrom, make
 * aTo runnable carrying aValue, and let the scheduler perform the entry.
 * Returns the value handed back when somebody later transfers to aFrom.
 *
 * This is what lets libcontext's synchronous contract survive the flip. To the
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
 * Make a fiber-lane context runnable FROM THE SCHEDULER STACK, carrying
 * aValue (Phase B). This is the lane's entry point: a transfer needs a
 * running context to park, so the first one — and every kick from a JS task,
 * e.g. the tick handing work to the dispatch context — has to come from here.
 */
bool fiber_start( ContextId aId, intptr_t aValue );

/** Pump ready contexts until quiescent. Scheduler stack only. */
size_t drain_all();

/**
 * Registry + memory snapshot as JSON, for tests and the D1 memory gate:
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
// early failure with a registry dump. Deliberately low for D1: nothing in
// production runs on contexts yet, and the test app's worst battery uses ~8.
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
 * exactly how this was found (D2b, 107 wx failures). libcontext has always
 * carried `alignas(16)` on its buffers for the same reason.
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

    // Fiber lane (Phase A): a libcontext client under symmetric-swap
    // semantics. Never enters the ready FIFO, never picked by drain(),
    // counted separately from the star's memory gate.
    bool symmetric = false;
};

struct Registry
{
    std::map<ContextId, Context*> contexts;
    std::vector<ContextId> ready_fifo;   // FIFO: no starvation (doc 13 §1.5 inv. 8)

    ContextId next_id = 1;
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
    size_t live = 0;
    size_t peak_live = 0;
    size_t bytes = 0;
    size_t peak_bytes = 0;
    size_t asyncify_high_water = 0;

    // Fiber lane (Phase A) — deliberately separate from the star's counters so
    // the D1 memory gate (finished == created, live == 0 after the battery)
    // keeps meaning what it meant.
    ContextId fiber_running = 0;           // 0 = no fiber lane yet (root unadopted)
    uint32_t fiber_created = 0;
    uint32_t fiber_released = 0;
    uint32_t fiber_swaps = 0;
    uint32_t fiber_refusals = 0;
    uint32_t fiber_released_suspended = 0;   // legal (refcount drop mid-suspend), counted
    uint32_t fiber_released_running = 0;     // TRIPWIRE: refcount drop of a "running" fiber
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

    if( used > aCtx.asyncify_high_water )
        aCtx.asyncify_high_water = used;

    // Per-lane high-water: the star's number feeds the D1 sizing gate, the
    // fiber lane's feeds the Phase E "size from real bridges" decision.
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
    const char* here = &probe;
    const char* low = aCtx.c_stack.base;

    if( !low )
        return false;

    return here >= low && here < low + aCtx.c_stack.size;
}


inline Context* find( ContextId aId )
{
    auto it = reg().contexts.find( aId );
    return it == reg().contexts.end() ? nullptr : it->second;
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

    ctx->id = r.next_id++;
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

    r.contexts[ctx->id] = ctx;
    r.ready_fifo.push_back( ctx->id );
    r.created++;
    r.live++;

    if( r.live > r.peak_live )
        r.peak_live = r.live;

    r.bytes += ctx->c_stack.size + ctx->asyncify_stack.size;

    if( r.bytes > r.peak_bytes )
        r.peak_bytes = r.bytes;

    return ctx->id;
}


inline int yield_park( const char* aReason )
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
        return -1;
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
    // context's C stack, and refuse if not. D3 must route such a wait
    // differently (the tool coroutine has to become a context of its own);
    // until it does, this refusal is what keeps the failure loud and local.
    if( !on_context_stack( *ctx ) )
    {
        beacon( "REFUSED",
                "yield_park() from a foreign stack (a fiber swapped in above this "
                "context) - the wait must yield ITS OWN context",
                ctx->id );
        r.refusals++;
        ++r.foreign_stack_refusals;
        return -1;
    }

    ctx->status = Status::Parked;
    ctx->park_reason = aReason ? aReason : "";
    ctx->parks++;
    r.running = 0;
    r.transition = false;   // the swap below completes this transition

    // Yield to the scheduler. Control returns here when drain() swaps us back
    // in after mark_ready() — and ONLY then, because Parked→Ready→resume is
    // the single path in.
    emscripten_fiber_swap( &ctx->fiber, &r.scheduler_fiber );

    // Resumed. The registry set status/result before swapping in.
    note_asyncify_use( *ctx );
    return ctx->result;
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

    if( ctx->status != Status::Parked )
    {
        char detail[96];
        std::snprintf( detail, sizeof( detail ), "mark_ready() on a %s context",
                       status_name( ctx->status ) );
        beacon( "REFUSED", detail, aId );
        r.refusals++;
        return false;
    }

    ctx->result = aResult;
    ctx->status = Status::Ready;
    r.ready_fifo.push_back( aId );
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
    // exact across a star transition (Phase B).
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
 * Run ready contexts until the scheduler is quiescent (Phase B).
 *
 * A star transition is not a swap-and-return: when A transfers to B, A parks
 * and B merely becomes RUNNABLE, so somebody has to keep draining or the work
 * stalls. That somebody must be the scheduler stack — this is the top-level
 * pump, called from a fresh JS task, never from a context.
 *
 * The cap is a livelock backstop, not a policy: a pair of contexts
 * transferring to each other forever would otherwise hang the tab with no
 * evidence. Hitting it beacons and leaves the rest queued for the next tick.
 */
inline size_t drain_all()
{
    size_t ran = 0;

    while( drain() )
    {
        if( ++ran >= 4096 )
        {
            beacon( "DRAIN-CAP", "4096 transitions in one pump - suspected livelock", 0 );
            break;
        }
    }

    return ran;
}


inline bool can_yield_here()
{
    Context* ctx = find( reg().running );
    return ctx && on_context_stack( *ctx );
}


inline bool transition_in_flight()
{
    return reg().transition;
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
    r.contexts.erase( aId );
    delete ctx;
    return true;
}


// ---------------------------------------------------------------------------
// Fiber lane (doc 22 Phase A) — implementation
// ---------------------------------------------------------------------------

namespace detail
{

// Not a ceiling (a refusal here would change libcontext behaviour, which is
// unbounded); a beacon threshold that turns "coroutines are leaking" into a
// loud line long before the tab dies.
constexpr size_t FIBER_POPULATION_BEACON = 256;

inline Context* register_fiber_context( Context* aCtx )
{
    Registry& r = reg();

    r.contexts[aCtx->id] = aCtx;
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

    return aCtx;
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

    ctx->id = r.next_id++;
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

    // Adoption runs ON the stack being adopted, so the live limits describe it
    // (the one situation where a live query is trustworthy — doc 22 §7 trap 2).
    // Range is informational: the root's swaps are driven by its own frames.
    ctx->c_stack.adopt( reinterpret_cast<void*>( emscripten_stack_get_end() ),
                        static_cast<size_t>( emscripten_stack_get_base()
                                             - emscripten_stack_get_end() ) );

    emscripten_fiber_init_from_current_context( &ctx->fiber,
                                                ctx->asyncify_stack.base,
                                                ctx->asyncify_stack.size );

    ctx->status = Status::Running;
    ctx->park_reason = "adopted";

    register_fiber_context( ctx );
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

    ctx->id = r.next_id++;
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

    register_fiber_context( ctx );
    return ctx->id;
}


inline bool fiber_enterable( ContextId aId )
{
    Context* ctx = find( aId );

    if( !ctx || !ctx->symmetric )
        return false;

    return ctx->status == Status::Fresh || ctx->status == Status::Suspended;
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

    // Phase A is behaviour-preserving, so a swap into stale state is COUNTED,
    // not vetoed: the policy refusal lives in jump_fcontext (which reads
    // fiber_enterable() before calling here). Any firing is a tripwire —
    // a caller bypassed the policy.
    if( to->status != Status::Fresh && to->status != Status::Suspended )
    {
        char detail[96];
        std::snprintf( detail, sizeof( detail ),
                       "swap into a %s context - stale rewind state",
                       status_name( to->status ) );
        beacon( "FIBER-SWAP-NONENTERABLE", detail, aTo );
        r.fiber_nonenterable_swaps++;
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
    // the high-water would always read 0 (the Phase E sizing input).
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

    if( ctx->status == Status::Running || ctx->status == Status::Ready )
        return false;   // already runnable; not an error

    ctx->transfer = aValue;
    ctx->status = Status::Ready;
    r.ready_fifo.push_back( aId );
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

    // Hand the protocol value over and make the target RUNNABLE — not running.
    // The scheduler performs every entry, which is the whole difference from
    // Phase A's direct swap: nobody enters a context by deciding to.
    to->transfer = aValue;

    if( to->status != Status::Ready )
    {
        to->status = Status::Ready;
        r.ready_fifo.push_back( aTo );
    }

    from->status = Status::Parked;
    from->park_reason = "fiber-transfer";
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


inline bool fiber_release( ContextId aId )
{
    Registry& r = reg();
    Context* ctx = find( aId );

    if( !ctx || !ctx->symmetric )
        return false;

    if( ctx->status == Status::Running )
    {
        // "Running" here usually means the registry's view is stale (the
        // fiber is asyncify-parked below a JS turn, or an aborted tool is
        // being torn down). libcontext's refcount drop always deleted the
        // struct in this state, so the registry must let go too — refusing
        // while the caller frees anyway leaves a permanent ghost that
        // poisons every later enterability answer (measured 2026-08-06,
        // eeschema-collab). Beacon as a tripwire, then release.
        beacon( "FIBER-RELEASE-RUNNING", "released while the registry says running", aId );
        r.fiber_released_running++;

        if( r.fiber_running == aId )
            r.fiber_running = 0;
    }

    if( ctx->status == Status::Suspended )
        r.fiber_released_suspended++;

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
                   "\"transitions\":%u,\"refusals\":%u,\"foreignStackRefusals\":%u,\"running\":%u,"
                   "\"transitionInFlight\":%s,\"readyQueued\":%zu,"
                   "\"bytes\":%zu,\"peakBytes\":%zu,"
                   "\"perContextBytes\":%zu,\"cStackBytes\":%zu,\"asyncifyBytes\":%zu,"
                   "\"asyncifyHighWater\":%zu,"
                   "\"fiberLive\":%zu,\"fiberPeakLive\":%zu,\"fiberCreated\":%u,"
                   "\"fiberReleased\":%u,\"fiberSwaps\":%u,\"fiberRefusals\":%u,"
                   "\"fiberReleasedSuspended\":%u,\"fiberReleasedRunning\":%u,"
                   "\"fiberNonEnterableSwaps\":%u,"
                   "\"fiberRunning\":%u,\"fiberBytes\":%zu,\"fiberPeakBytes\":%zu,"
                   "\"fiberAsyncifyHighWater\":%zu}",
                   r.live, r.peak_live, r.created, r.finished,
                   r.transitions, r.refusals, r.foreign_stack_refusals, r.running,
                   r.transition ? "true" : "false", r.ready_fifo.size(),
                   r.bytes, r.peak_bytes,
                   DEFAULT_C_STACK_BYTES + DEFAULT_ASYNCIFY_BYTES,
                   DEFAULT_C_STACK_BYTES, DEFAULT_ASYNCIFY_BYTES,
                   r.asyncify_high_water,
                   r.fiber_live, r.fiber_peak_live, r.fiber_created,
                   r.fiber_released, r.fiber_swaps, r.fiber_refusals,
                   r.fiber_released_suspended, r.fiber_released_running,
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
    r.peak_live = r.live;
    r.peak_bytes = r.bytes;
    r.asyncify_high_water = 0;
    r.fiber_created = 0;
    r.fiber_released = 0;
    r.fiber_swaps = 0;
    r.fiber_refusals = 0;
    r.fiber_released_suspended = 0;
    r.fiber_released_running = 0;
    r.fiber_nonenterable_swaps = 0;
    r.fiber_peak_live = r.fiber_live;
    r.fiber_peak_bytes = r.fiber_bytes;
    r.fiber_asyncify_high_water = 0;
}

} // namespace pcbjam_sched
