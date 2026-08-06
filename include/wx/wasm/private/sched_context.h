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
    Finished    ///< entry returned; stack/buffer reclaimable
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

/** True while a swap is in flight; drain() refuses to start another. */
bool transition_in_flight();

/** The running context's id, or 0 when the scheduler stack is running. */
ContextId current();

Status status_of( ContextId aId );

/** Destroy a Finished context and release its stack + buffer. */
bool destroy( ContextId aId );

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

    emscripten_fiber_t fiber {};
    AlignedBuffer c_stack;
    AlignedBuffer asyncify_stack;

    void ( *entry )( void* ) = nullptr;
    void* arg = nullptr;

    uint32_t parks = 0;
    uint32_t resumes = 0;
    size_t asyncify_high_water = 0;
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

    if( used > reg().asyncify_high_water )
        reg().asyncify_high_water = used;

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

    // Swap in. Returns when the context parks (yield_park) or finishes; both
    // clear running/transition before swapping back.
    emscripten_fiber_swap( &r.scheduler_fiber, &ctx->fiber );

    r.transition = false;
    r.running = 0;

    // The context object may still exist (parked) or be finished; either way
    // its buffer use is now measurable.
    if( Context* back = find( id ) )
        note_asyncify_use( *back );

    return id;
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


inline bool destroy( ContextId aId )
{
    Registry& r = reg();
    Context* ctx = find( aId );

    if( !ctx )
        return false;

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


inline std::string stats_json()
{
    Registry& r = reg();
    char buf[640];
    std::snprintf( buf, sizeof( buf ),
                   "{\"live\":%zu,\"peakLive\":%zu,\"created\":%u,\"finished\":%u,"
                   "\"transitions\":%u,\"refusals\":%u,\"foreignStackRefusals\":%u,\"running\":%u,"
                   "\"transitionInFlight\":%s,\"readyQueued\":%zu,"
                   "\"bytes\":%zu,\"peakBytes\":%zu,"
                   "\"perContextBytes\":%zu,\"cStackBytes\":%zu,\"asyncifyBytes\":%zu,"
                   "\"asyncifyHighWater\":%zu}",
                   r.live, r.peak_live, r.created, r.finished,
                   r.transitions, r.refusals, r.foreign_stack_refusals, r.running,
                   r.transition ? "true" : "false", r.ready_fifo.size(),
                   r.bytes, r.peak_bytes,
                   DEFAULT_C_STACK_BYTES + DEFAULT_ASYNCIFY_BYTES,
                   DEFAULT_C_STACK_BYTES, DEFAULT_ASYNCIFY_BYTES,
                   r.asyncify_high_water );
    return buf;
}


inline std::string registry_json()
{
    std::string out = "[";
    bool first = true;

    for( const auto& [id, ctx] : reg().contexts )
    {
        char entry[256];
        std::snprintf( entry, sizeof( entry ),
                       "%s{\"id\":%u,\"label\":\"%s\",\"status\":\"%s\",\"reason\":\"%s\","
                       "\"parks\":%u,\"resumes\":%u,\"asyncifyHighWater\":%zu}",
                       first ? "" : ",", id, ctx->label, status_name( ctx->status ),
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
}

} // namespace pcbjam_sched
