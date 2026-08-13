///////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/private/execution_owner.h
// Purpose:     Semantic admission for mutable WASM execution
// Licence:     wxWindows licence
///////////////////////////////////////////////////////////////////////////

#ifndef _WX_WASM_PRIVATE_EXECUTION_OWNER_H_
#define _WX_WASM_PRIVATE_EXECUTION_OWNER_H_

#include <cstdint>
#include <cstddef>
#include <vector>

namespace wx_wasm_execution
{

// These are deliberately strong types. An Asyncify wait token identifies one
// suspension and a scheduler ContextId identifies one physical stack; neither
// is an execution owner or a child lease.
struct OwnerId
{
    std::uint64_t value = 0;

    explicit operator bool() const { return value != 0; }
};

inline bool operator==(OwnerId a, OwnerId b) { return a.value == b.value; }
inline bool operator!=(OwnerId a, OwnerId b) { return !(a == b); }

struct LeaseId
{
    std::uint64_t value = 0;

    explicit operator bool() const { return value != 0; }
};

inline bool operator==(LeaseId a, LeaseId b) { return a.value == b.value; }
inline bool operator!=(LeaseId a, LeaseId b) { return !(a == b); }

// A lease is permission for one audited wx target family, not a global
// capability for every callback of an allowed class. For the first Wasm
// implementation this is the stable address of the modal top-level, popup
// model, or explicitly selected nested-loop target. It is compared as an
// opaque identity and never dereferenced by the coordinator.
struct ScopeToken
{
    std::uintptr_t value = 0;
    std::uint32_t generation = 0;

    explicit operator bool() const { return value != 0 && generation != 0; }
};

inline bool operator==(ScopeToken a, ScopeToken b)
{
    return a.value == b.value && a.generation == b.generation;
}
inline bool operator!=(ScopeToken a, ScopeToken b) { return !(a == b); }

inline ScopeToken ScopeFromPointer(const void *scope, std::uint32_t generation)
{
    ScopeToken token;
    token.value = reinterpret_cast<std::uintptr_t>(scope);
    token.generation = generation;
    return token;
}

struct OwnerToken
{
    OwnerId id;
    OwnerId parent;
    std::uint64_t generation = 0;

    explicit operator bool() const { return static_cast<bool>(id); }
};

inline bool operator==(const OwnerToken& a, const OwnerToken& b)
{
    return a.id == b.id && a.parent == b.parent && a.generation == b.generation;
}

inline bool operator!=(const OwnerToken& a, const OwnerToken& b)
{
    return !(a == b);
}

struct LeaseToken
{
    LeaseId id;
    OwnerId parent;
    std::uint64_t generation = 0;
    ScopeToken targetScope;

    explicit operator bool() const { return static_cast<bool>(id); }
};

inline bool operator==(const LeaseToken& a, const LeaseToken& b)
{
    return a.id == b.id && a.parent == b.parent
           && a.generation == b.generation
           && a.targetScope == b.targetScope;
}

inline bool operator!=(const LeaseToken& a, const LeaseToken& b)
{
    return !(a == b);
}

// One external browser callback receives this token before any delayed
// delivery decision. The monotonic sequence orders custom JavaScript DOM
// adapters with Emscripten's generated canvas callbacks. snapshotAvailable
// distinguishes a verified "no modal was active" snapshot from failure to
// acquire provenance; the latter is admitted only as conservative Ordinary
// work. deferredBehindEarlier prevents a generated canvas callback from
// draining inline while an older JavaScript receipt is still queued.
struct BrowserIngressReceipt
{
    std::uint32_t sequence = 0;
    bool snapshotAvailable = false;
    bool deferredBehindEarlier = false;
    LeaseToken lease;

    explicit operator bool() const { return sequence != 0; }
};

// Ordinary work is never admitted through a child lease. The remaining
// values are individual permission bits in a lease mask.
enum class WorkClass : std::uint32_t
{
    Ordinary = 0,
    UserInput = 1u << 0,
    PendingEvents = 1u << 1,
    ModalLifecycle = 1u << 2
};

constexpr bool IsValidWorkClass(WorkClass cls)
{
    return cls == WorkClass::Ordinary
           || cls == WorkClass::UserInput
           || cls == WorkClass::PendingEvents
           || cls == WorkClass::ModalLifecycle;
}

// A scoped non-ordinary ingress which arrives while its exact lease exists
// belongs to that lease generation. Keep this provenance even after close has
// begun: such work must be discarded, not reinterpreted as work for a later
// lease which happens to reuse the same target scope.
inline LeaseToken LeaseProvenanceForIngress(
        WorkClass cls, ScopeToken targetScope,
        const LeaseToken& activeLease)
{
    return cls != WorkClass::Ordinary && targetScope && activeLease
                   && targetScope == activeLease.targetScope
            ? activeLease
            : LeaseToken{};
}

inline bool LeaseProvenanceIsStale(
        const LeaseToken& provenance, const LeaseToken& activeLease,
        bool activeLeaseAccepting)
{
    return provenance
           && (provenance != activeLease || !activeLeaseAccepting);
}

// Only high-rate, loss-tolerant browser state may use adjacent latest-wins
// replacement: passive pointer motion and top-level geometry. Every other
// envelope is a discrete ordering barrier.
enum class CoalesceClass : std::uint8_t
{
    None,
    PassiveMouseMove,
    LatestGeometry
};

inline bool CanCoalesceAdjacent(
        CoalesceClass previousClass, WorkClass previousWork,
        ScopeToken previousScope, bool previousAffiliated,
        bool sameProducer, bool sameCoalesceKey, bool consecutive,
        CoalesceClass incomingClass, WorkClass incomingWork,
        ScopeToken incomingScope)
{
    return !previousAffiliated
           && sameProducer
           && sameCoalesceKey
           && consecutive
           && incomingClass != CoalesceClass::None
           && previousClass == incomingClass
           && previousWork == incomingWork
           && previousScope == incomingScope;
}

using DiscardCallback = void (*)(void *);

// A synchronous browser ingress briefly has split ownership: its payload is
// retained by the native queue, but its caller must still inspect the payload
// after a callback which completed before submission returned. Terminal queue
// cleanup can run inside that same submission (for example, an earlier queued
// callback fail-stops and discards the later one). A boolean cannot distinguish
// that case from a live accepted payload.
enum class DispatchSubmitDisposition : std::uint8_t
{
    // Admission rejected before the queue retained the payload.
    RejectedCallerOwns,
    // The queue accepted the payload and it was not discarded while submit was
    // on the stack. The caller may complete its normal finished/abandoned
    // handshake before returning to JavaScript.
    AcceptedPayloadLive,
    // The queue accepted and retired the payload during this submit. Its
    // discard callback, when supplied, performed cleanup; the caller must not
    // inspect or free it.
    AcceptedPayloadDiscarded
};

// Stack-scoped witness used only while wxWasmRunOnDispatchContextScoped() is
// active. A queued envelope points to it until exactly one of three events:
// callback start, caller return with the envelope still queued, or discard.
// Every event clears the envelope pointer before changing this witness, so no
// retained queue record can refer to the caller's stack after submit returns.
class DispatchSubmissionHandshake
{
public:
    DispatchSubmissionHandshake() = default;
    DispatchSubmissionHandshake(const DispatchSubmissionHandshake&) = delete;
    DispatchSubmissionHandshake& operator=(
            const DispatchSubmissionHandshake&) = delete;

    bool IsAttached() const { return m_state == State::Attached; }

    bool MarkPayloadLive()
    {
        if (!IsAttached())
            return false;

        m_state = State::PayloadLive;
        return true;
    }

    bool MarkPayloadDiscarded()
    {
        if (!IsAttached())
            return false;

        m_state = State::PayloadDiscarded;
        return true;
    }

    DispatchSubmitDisposition Disposition() const
    {
        return m_state == State::PayloadDiscarded
                ? DispatchSubmitDisposition::AcceptedPayloadDiscarded
                : DispatchSubmitDisposition::AcceptedPayloadLive;
    }

private:
    enum class State : std::uint8_t
    {
        Attached,
        PayloadLive,
        PayloadDiscarded
    };

    State m_state = State::Attached;
};

// Every native queue record retains a callback and its payload until a clean
// dispatch stack can run it. The bound therefore applies to the complete
// queue, including affiliated continuations. An affiliated tail cannot be
// rejected and leave its owner parked forever: reaching this ceiling is a
// terminal scheduler invariant failure.
constexpr std::size_t MaxQueuedJobs = 4096;

inline bool QueueHasCapacity(std::size_t retainedPayloads)
{
    return retainedPayloads < MaxQueuedJobs;
}

// Some execution envelopes retain payloads which are much larger than their
// queue record.  A job-count bound alone is not a memory bound: one parked
// owner could otherwise retain thousands of multi-megabyte service batches.
// This budget follows the copied native payload from queue publication until
// its callback has returned or its discard tail has run.  It is deliberately
// independent of JavaScript's completion-transport budget: the JS reservation
// is consumed synchronously before the native copy takes this lease, so the
// same bytes are never charged to both layers at once.
constexpr std::size_t MaxQueuedRetainedBytes = 64u * 1024u * 1024u;

class RetainedByteBudget
{
public:
    bool TryRetain(std::size_t bytes)
    {
        if (bytes > MaxQueuedRetainedBytes
            || bytes > MaxQueuedRetainedBytes - m_retained)
        {
            ++m_rejections;
            return false;
        }

        m_retained += bytes;
        if (m_retained > m_highWater)
            m_highWater = m_retained;
        return true;
    }

    bool Release(std::size_t bytes)
    {
        if (bytes > m_retained)
            return false;

        m_retained -= bytes;
        return true;
    }

    std::size_t Retained() const { return m_retained; }
    std::size_t HighWater() const { return m_highWater; }
    std::uint64_t Rejections() const { return m_rejections; }

private:
    std::size_t m_retained = 0;
    std::size_t m_highWater = 0;
    std::uint64_t m_rejections = 0;
};

// A later generated Emscripten callback can stage its native envelope while
// an earlier custom-DOM receipt is still waiting in JavaScript for a safe Wasm
// entry.  The native queue sorts envelopes once both are present, but it must
// not consume the later one while that receipt gap is open.  Non-browser work
// is independent of this ordering lane and remains eligible.
inline bool BrowserIngressCanRun(
        std::uint32_t ingressSequence,
        std::uint32_t earliestUnstagedReceipt)
{
    return ingressSequence == 0 || earliestUnstagedReceipt == 0
           || ingressSequence < earliestUnstagedReceipt;
}

struct QueueCounters
{
    void NoteEnqueued(std::size_t depth)
    {
        if (depth > highWater)
            highWater = depth;
    }

    void NoteCoalesced() { ++coalesced; }
    void NoteRejected() { ++rejected; }

    std::size_t highWater = 0;
    std::uint64_t coalesced = 0;
    std::uint64_t rejected = 0;
};

// QueueEvent() transfers ownership through a void API, so a Wasm overload
// cannot report backpressure to its original producer. Pending-event
// retention therefore has a separate hard bound and terminal overflow policy:
// a newly refused pointer is deleted by QueueEvent(), while a duplicate is
// already owned by the physical pending list and must not be deleted twice.
constexpr std::size_t MaxPendingEvents = 4096;

enum class PendingEventTagDisposition : std::uint8_t
{
    Accepted,
    RejectedDeleteEvent,
    RejectedAlreadyOwned
};

enum class PendingEventFailure : std::uint8_t
{
    None,
    Overflow,
    DuplicatePointer,
    Allocation,
    MissingProvenance
};

// A callback which finishes after the initiating wx handler returns cannot
// recover modal provenance from its physical stack.  This capability carries
// the one exact lease generation captured by that handler.  It is deliberately
// not an owner reference: a long operation must not keep one modal child in
// flight and block the dialog's remaining input.
struct PendingEventDelegate
{
    LeaseToken lease;

    explicit operator bool() const
    {
        return static_cast<bool>(lease)
               && static_cast<bool>(lease.targetScope);
    }
};

struct PendingEventQueueStats
{
    std::size_t retained = 0;
    std::size_t highWater = 0;
    std::uint64_t accepted = 0;
    std::uint64_t forgotten = 0;
    std::uint64_t clearedOnShutdown = 0;
    std::uint64_t rejected = 0;
    std::uint64_t duplicateRejected = 0;
    std::uint64_t unmatchedForgets = 0;
    std::uint64_t processableQueries = 0;
    std::uint64_t avoidedPhysicalScans = 0;
    std::uint64_t dispatchChecks = 0;
    PendingEventFailure failure = PendingEventFailure::None;
};

using WorkMask = std::uint32_t;

constexpr WorkMask WorkBit(WorkClass cls)
{
    return static_cast<WorkMask>(cls);
}

constexpr WorkMask ModalWorkMask = WorkBit(WorkClass::UserInput)
                                   | WorkBit(WorkClass::PendingEvents)
                                   | WorkBit(WorkClass::ModalLifecycle);

enum class AdmissionKind
{
    Deferred,
    Root,
    Child
};

struct Admission
{
    AdmissionKind kind = AdmissionKind::Deferred;
    OwnerToken owner;
    LeaseToken lease;

    explicit operator bool() const
    {
        return kind != AdmissionKind::Deferred && static_cast<bool>(owner);
    }
};

/**
 * The logical UI/model owner tree.
 *
 * This class does not switch stacks and does not manipulate Asyncify. It only
 * answers whether a stateful execution slice may start. A root owner remains
 * present until its C++ body really completes. A modal can temporarily admit
 * one child transaction at a time without releasing that root owner.
 */
class Coordinator
{
public:
    Admission Admit(WorkClass cls, ScopeToken targetScope = {})
    {
        if (m_failed || !IsValidWorkClass(cls))
            return {};

        if (!m_leases.empty())
        {
            LeaseRecord& lease = m_leases.back();
            const WorkMask bit = WorkBit(cls);

            if (lease.accepting && bit != 0 && (lease.allowed & bit) != 0
                && targetScope && targetScope == lease.token.targetScope
                && !lease.child)
            {
                lease.child = NewOwner(lease.token.parent);
                lease.childRefs = 1;
                return {AdmissionKind::Child, lease.child, lease.token};
            }
        }

        if (!m_root)
        {
            m_root = NewOwner(OwnerId{});
            m_rootRefs = 1;
            return {AdmissionKind::Root, m_root, LeaseToken{}};
        }

        return {};
    }

    bool Release(const OwnerToken& owner)
    {
        if (!owner)
            return false;

        if (m_root == owner)
        {
            if (m_rootRefs > 1)
            {
                --m_rootRefs;
                return true;
            }

            if (m_rootRefs == 1)
            {
                m_rootRefs = 0;
                if (m_leases.empty())
                    m_root = {};
                return true;
            }

            return false;
        }

        for (size_t i = 0; i < m_leases.size(); ++i)
        {
            LeaseRecord& lease = m_leases[i];

            if (lease.child != owner)
                continue;

            if (lease.childRefs > 1)
            {
                --lease.childRefs;
                return true;
            }

            if (lease.childRefs == 1)
            {
                lease.childRefs = 0;
                // A nested lease still names this owner as its parent. Keep
                // the token as structural provenance until that descendant
                // closes, but it has no executing reference of its own.
                if (i + 1 == m_leases.size())
                    lease.child = {};
                return true;
            }

            return false;
        }

        return false;
    }

    // Affiliated executors retain the same semantic transaction after their
    // scheduling call returns. This does not create a new owner and cannot be
    // used to admit unrelated work.
    bool Retain(const OwnerToken& owner)
    {
        if (m_failed || !owner)
            return false;

        if (m_root == owner && m_rootRefs > 0)
        {
            ++m_rootRefs;
            return true;
        }

        for (LeaseRecord& lease : m_leases)
        {
            if (lease.child == owner && lease.childRefs > 0)
            {
                ++lease.childRefs;
                return true;
            }
        }

        return false;
    }

    LeaseToken OpenLease(const OwnerToken& parent, WorkMask allowed,
                         ScopeToken targetScope)
    {
        if (m_failed || !parent || allowed == 0 || !targetScope
            || (!m_leases.empty() && !m_leases.back().accepting)
            || CurrentBranchOwner() != parent)
            return {};

        LeaseRecord record;
        record.token.id.value = m_nextLeaseId++;
        record.token.parent = parent.id;
        record.token.generation = m_nextGeneration++;
        record.token.targetScope = targetScope;
        record.allowed = allowed;
        m_leases.push_back(record);
        return record.token;
    }

    // Stop new child admission before the exact modal completion wakes the
    // parent. A caller can close an ancestor while a descendant modal is
    // active (wxEventLoop::ScheduleExit explicitly permits this). Mark that
    // exact lease now, but keep completion and popping strictly top-to-bottom.
    // When the descendant pops, wxWasmTryCompleteActiveModalLease() observes
    // the already-closing ancestor and wakes it at the first safe point.
    bool BeginClose(const LeaseToken& lease)
    {
        for (LeaseRecord& record : m_leases)
        {
            if (record.token != lease)
                continue;

            record.accepting = false;
            return true;
        }

        return false;
    }

    // A close requester may wake the parked parent only after admission is
    // closed and every reference to the exact top lease's child is gone.
    bool LeaseReady(const LeaseToken& lease) const
    {
        return !m_leases.empty() && m_leases.back().token == lease
               && !m_leases.back().accepting && !m_leases.back().child;
    }

    bool CloseLease(const LeaseToken& lease)
    {
        if (m_leases.empty() || m_leases.back().token != lease
            || m_leases.back().accepting || m_leases.back().child)
        {
            return false;
        }

        m_leases.pop_back();

        // A parent execution can finish while a descendant lease keeps its
        // branch structurally alive. Retire that zero-reference branch as soon
        // as the descendant closes.
        if (!m_leases.empty())
        {
            LeaseRecord& parent = m_leases.back();
            if (parent.child && parent.childRefs == 0)
                parent.child = {};
        }
        else if (m_root && m_rootRefs == 0)
        {
            m_root = {};
        }
        return true;
    }

    OwnerToken RootOwner() const { return m_root; }

    OwnerToken CurrentBranchOwner() const
    {
        if (m_leases.empty())
            return m_root;

        const LeaseRecord& lease = m_leases.back();
        return lease.child ? lease.child : FindOwner(lease.token.parent);
    }

    LeaseToken ActiveLease() const
    {
        return m_leases.empty() ? LeaseToken{} : m_leases.back().token;
    }

    // Browser receipt and native admission answer different questions. While
    // the exact top lease is closing, input for the nearest accepting ancestor
    // still needs that ancestor's generation stamped at browser arrival. This
    // query publishes only that capability; ActiveLease(), LeaseAccepting(),
    // and Admit() continue to describe and enforce the exact top lease.
    LeaseToken IngressReceiptLease() const
    {
        for (auto it = m_leases.rbegin(); it != m_leases.rend(); ++it)
        {
            if (it->accepting)
                return it->token;
        }

        return {};
    }

    bool IngressReceiptIsStale(const LeaseToken& provenance) const
    {
        const LeaseToken receiptLease = IngressReceiptLease();
        return LeaseProvenanceIsStale(
                provenance, receiptLease,
                static_cast<bool>(receiptLease));
    }

    ScopeToken ActiveLeaseScope() const
    {
        return m_leases.empty() ? ScopeToken{}
                                : m_leases.back().token.targetScope;
    }

    bool LeaseAccepting() const
    {
        return !m_leases.empty() && m_leases.back().accepting;
    }

    bool Owns(const OwnerToken& owner) const
    {
        if (!owner)
            return false;

        if (m_root == owner)
            return true;

        for (const LeaseRecord& lease : m_leases)
        {
            if (lease.child == owner)
                return true;
        }

        return false;
    }

    // Liveness is not execution permission. An affiliated continuation may
    // run only for the exact active branch: an ancestor retained behind an
    // open child lease stays blocked until that lease closes.
    bool CanRunAffiliated(const OwnerToken& owner) const
    {
        if (m_failed || !owner)
            return false;

        if (m_leases.empty())
            return m_root == owner && m_rootRefs > 0;

        const LeaseRecord& lease = m_leases.back();
        return lease.child == owner && lease.childRefs > 0;
    }

    // wxYield()/Dispatch() can continue browser input which arrived while the
    // current owner was exactly parked. This is recursion inside the existing
    // transaction, not admission of another transaction: ordinary service and
    // Embind work must wait for the owner to finish.
    //
    // A root can do this only when it has not delegated through a modal lease.
    // An active modal child needs both the exact lease generation captured at
    // ingress and the lease's target family. Scope equality alone is not a
    // capability because a later modal can reuse the same window address.
    bool CanRunNestedIngress(
            const OwnerToken& owner, WorkClass cls, ScopeToken targetScope,
            const LeaseToken& leaseProvenance) const
    {
        if (m_failed || !owner
            || (cls != WorkClass::UserInput
                && cls != WorkClass::PendingEvents))
        {
            return false;
        }

        if (owner == m_root)
        {
            return m_rootRefs > 0 && m_leases.empty()
                   && !leaseProvenance;
        }

        if (m_leases.empty())
            return false;

        const LeaseRecord& lease = m_leases.back();
        return lease.accepting && lease.child == owner
               && lease.childRefs > 0
               && leaseProvenance == lease.token
               && targetScope
               && targetScope == lease.token.targetScope;
    }

    std::size_t LeaseDepth() const { return m_leases.size(); }
    bool Failed() const { return m_failed; }

    // Terminal-entry containment only. The caller has established that no
    // saved C++ execution can return, either because the scheduler failed or
    // because the application is shutting down. Retaining semantic owners
    // would only strand payloads after admission closes.
    void Abandon()
    {
        m_leases.clear();
        m_root = {};
        m_rootRefs = 0;
        m_failed = true;
    }

private:
    struct LeaseRecord
    {
        LeaseToken token;
        WorkMask allowed = 0;
        OwnerToken child;
        std::size_t childRefs = 0;
        bool accepting = true;
    };

    OwnerToken NewOwner(OwnerId parent)
    {
        OwnerToken token;
        token.id.value = m_nextOwnerId++;
        token.parent = parent;
        token.generation = m_nextGeneration++;
        return token;
    }

    OwnerToken FindOwner(OwnerId id) const
    {
        if (m_root.id == id)
            return m_root;

        for (const LeaseRecord& lease : m_leases)
        {
            if (lease.child.id == id)
                return lease.child;
        }

        return {};
    }

    std::uint64_t m_nextOwnerId = 1;
    std::uint64_t m_nextLeaseId = 1;
    std::uint64_t m_nextGeneration = 1;
    OwnerToken m_root;
    std::size_t m_rootRefs = 0;
    std::vector<LeaseRecord> m_leases;
    bool m_failed = false;
};

} // namespace wx_wasm_execution

using wxWasmExecutionLeaseReadyCallback = void (*)(const void *, int);

// Modal adapter used by src/wasm/dialog.cpp. The lease is a semantic token;
// waitToken or readyCallback supplies the separate exact completion identity.
// Exactly one must be non-zero.
wx_wasm_execution::LeaseToken wxWasmExecutionOpenModalLease(
        const void *scope, wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::WorkMask allowed =
                wx_wasm_execution::ModalWorkMask,
        int waitToken = 0,
        wxWasmExecutionLeaseReadyCallback readyCallback = nullptr);
// A nested wxGUIEventLoop can move its wait body from a tool fiber to the main
// stack. Carry the verified owner across that stack switch; never infer an
// owner from the shared main-stack context ID.
wx_wasm_execution::LeaseToken wxWasmExecutionOpenModalLeaseForOwner(
        const void *scope, wx_wasm_execution::OwnerToken owner,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::WorkMask allowed =
                wx_wasm_execution::ModalWorkMask,
        int waitToken = 0,
        wxWasmExecutionLeaseReadyCallback readyCallback = nullptr);
// Revoke new children and record the exact result. Completion is delivered
// now only if the lease has no child; otherwise the final child Release()
// delivers it. No timer or retry loop participates.
bool wxWasmExecutionRequestModalClose(const void *scope, int result);
void wxWasmExecutionCloseModalLease(const void *scope,
                                    wx_wasm_execution::LeaseToken lease);

// Affiliated execution (for example runOnFiber) can outlive the scheduling
// call that started it. CurrentOwner returns exact mapped stack provenance;
// the shared context-0 startup stack never supplies a public capability.
// Retain the token before the scheduling call returns and release it only at
// the real body tail or terminal cleanup.
wx_wasm_execution::OwnerToken wxWasmExecutionCurrentOwner();
bool wxWasmExecutionRetainOwner(wx_wasm_execution::OwnerToken owner);
bool wxWasmExecutionReleaseOwner(wx_wasm_execution::OwnerToken owner);
// True only on the physical dispatch context to which this owner was admitted.
// A tool/libcontext fiber can carry the same semantic owner but must not start
// another generic fiber inline from its different stack topology.
bool wxWasmExecutionOnOwnedDispatchContext();
// Associate a recoverable affiliated-executor failure with the owner's
// JavaScript command ticket before the last owner reference is released.
bool wxWasmExecutionRecordOwnerFailure(
        wx_wasm_execution::OwnerToken owner, const char *reason);
// Enter the terminal state after an ownership invariant failure. This rejects
// future admissions/tickets; it is not a recoverable owner abort.
void wxWasmExecutionFailStop(const char *reason);

// Initial wx construction is a stateful transaction even before the browser
// event pump exists. It does not inherit through the shared main-stack ID;
// these hooks only keep ordinary external admission closed until OnInit and
// main-loop publication have completed.
wx_wasm_execution::OwnerToken wxWasmExecutionBeginStartup();
bool wxWasmExecutionEndStartup(wx_wasm_execution::OwnerToken owner);

// Resolve a wx target to its native top-level family. The returned token is an
// opaque identity only and does not extend the target's lifetime.
class wxWindow;
class wxEvtHandler;
class wxEvent;
wx_wasm_execution::ScopeToken wxWasmExecutionScopeForWindow(
        const wxWindow *window);
wx_wasm_execution::ScopeToken wxWasmExecutionActiveLeaseScope();

// The JavaScript ingress lane owns consume-once receipt tokens. Custom DOM
// staging exports take the token supplied by runNativeIngressReceipt().
// Emscripten-generated canvas callbacks capture one immediately on native
// entry. A missing snapshot deliberately returns an unavailable receipt; the
// caller must use Ordinary/no-lease admission rather than ambient discovery.
wx_wasm_execution::BrowserIngressReceipt
wxWasmExecutionTakeBrowserIngressReceipt(unsigned token);
wx_wasm_execution::BrowserIngressReceipt
wxWasmExecutionCaptureBrowserIngressReceipt();

// A non-window wxEvtHandler can be an auxiliary controller for one exact
// window family (for example KiCad's UNIT_BINDER inside a property dialog).
// QueueEvent cannot infer that relationship from the handler's C++ type. The
// owner must register it explicitly for the handler's lifetime so CallAfter
// receives the same narrow modal scope as its controls. This is not authority
// to borrow whatever lease happens to be active: TagPendingEvent snapshots the
// associated top-level generation at queue time.
bool wxWasmExecutionAssociatePendingEventHandler(
        const void *handler, const wxWindow *ownerWindow);
void wxWasmExecutionForgetPendingEventHandler(const void *handler);

// Capture permission for future pending events which are continuations of the
// current modal operation.  Capture succeeds only on the active modal child
// and only when ownerWindow resolves to that lease's exact top-level family.
// A fresh callback therefore cannot borrow whichever modal happens to be open.
wx_wasm_execution::PendingEventDelegate
wxWasmExecutionCapturePendingEventDelegate(const wxWindow *ownerWindow);

// Queue one exact event with previously captured modal provenance.  The
// delegation exists only for this handler/event pair during this QueueEvent
// call; it is not installed on the handler and does not affect another event
// posted by the same process-wide singleton.  An empty delegate has ordinary
// QueueEvent semantics.
void wxWasmExecutionQueueDelegatedPendingEvent(
        wxEvtHandler *handler, wxEvent *event,
        wx_wasm_execution::PendingEventDelegate delegate);

// Application commands which arrive through Embind or another non-wx entry
// use an always-deferred ordinary job. The fresh-task boundary keeps a fiber
// switch out of the JS stack that submitted the command. Queue ownership is
// explicit: false means the caller still owns arg; true transfers arg to the
// queue. The queue calls fn when admitted; on native fail-stop it calls
// discard when one was supplied. A record is removed before either callback,
// so a re-entrant failure cannot dispose the same arg twice.
bool wxWasmExecutionQueueOrdinary(
        void (*fn)(void *), void *arg,
        wx_wasm_execution::DiscardCallback discard = nullptr);

// The retained form is for copied service payloads whose memory remains live
// while an ordinary job waits behind another execution owner. `retainedBytes`
// is admitted against one process-wide 64 MiB native budget and remains
// charged through callback return. False leaves both arg and its byte claim
// with the caller; discard owns arg only after successful queue publication.
bool wxWasmExecutionQueueOrdinaryRetained(
        void (*fn)(void *), void *arg, std::size_t retainedBytes,
        wx_wasm_execution::DiscardCallback discard = nullptr);

// Queue terminal/control work for an owner that the caller already retained.
// This function neither retains nor releases the token. The callback receives
// the transferred reference and must release it exactly once. It runs on a
// fresh dispatch context even while ordinary admission is closed. If native
// fail-stop abandons the queue, discard is called exactly once when supplied;
// owner references are abandoned by the terminal coordinator transition.
// False leaves arg with the caller (and can mean that this admission attempt
// itself put the scheduler into that terminal state).
bool wxWasmExecutionQueueAffiliated(wx_wasm_execution::OwnerToken owner,
                                    void (*fn)(void *), void *arg,
                                    wx_wasm_execution::DiscardCallback discard = nullptr);
// beforeunload needs a synchronous answer and must not enqueue cleanup behind
// existing work. True only when a fresh dispatch can start immediately.
bool wxWasmExecutionCanStartFreshEntry();

// Receipt-time transport for browser ingress. This is deliberately not an
// execution entry: it may only copy an owned payload, capture the exact active
// lease generation, append one typed queue record, and arm the existing fresh
// execution tick. It must never admit work, drain a context, or suspend.
// The JavaScript caller first proves that Asyncify is Normal and no Emscripten
// fiber trampoline is running. The native scheduler transition flag is not a
// physical-entry predicate: it remains set while a context is asynchronously
// parked and the browser is free to deliver a new task. False leaves arg with
// the caller.
bool wxWasmExecutionStageIngress(
        void (*fn)(void *), void *arg,
        wx_wasm_execution::WorkClass cls,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::DiscardCallback discard,
        wx_wasm_execution::CoalesceClass coalesce =
                wx_wasm_execution::CoalesceClass::None,
        std::uintptr_t coalesceKey = 0);

// Stage one external receipt with its immutable JavaScript-time sequence and
// lease snapshot. The caller-selected scope must be empty or exactly match
// the snapshot lease scope. No active lease is rediscovered here.
bool wxWasmExecutionStageBrowserIngress(
        void (*fn)(void *), void *arg,
        wx_wasm_execution::WorkClass cls,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::BrowserIngressReceipt receipt,
        wx_wasm_execution::DiscardCallback discard,
        wx_wasm_execution::CoalesceClass coalesce =
                wx_wasm_execution::CoalesceClass::None,
        std::uintptr_t coalesceKey = 0);

// wx's physical pending-event list mixes CallAfter, posted UI follow-ups and
// worker notifications. Tag at QueueEvent() and filter immediately before
// dispatch so a modal child sees only work from its own lease. These hooks use
// opaque pointers to keep generic event.cpp independent of Wasm policy types.
wx_wasm_execution::PendingEventTagDisposition
wxWasmExecutionTagPendingEvent(const void *event, const void *handler);
// O(1) owner/lease index used before wx traverses the physical pending list.
// False with retained events means that every item belongs to another owner;
// callers must leave the list untouched until an ownership edge changes.
bool wxWasmExecutionHasProcessablePendingEvents();
bool wxWasmExecutionMayProcessPendingEvent(const void *event);
void wxWasmExecutionForgetPendingEvent(const void *event);
// Roll back a provenance record whose physical wxList publication failed,
// and latch one terminal Allocation failure. Safe from QueueEvent pthreads;
// only a main-runtime observer performs native fail-stop cleanup.
void wxWasmExecutionRejectPendingEventStorage(const void *event);
wx_wasm_execution::PendingEventQueueStats
wxWasmExecutionPendingEventQueueStats();

// Explicit entry classification and target scope for Wasm dispatch producers.
// The three-way result tells a synchronous caller whether it may still inspect
// arg: terminal cleanup can discard an accepted envelope before this function
// returns. If discard is supplied, an accepted queued record ends by exactly
// one call to fn or discard.
wx_wasm_execution::DispatchSubmitDisposition
wxWasmRunOnDispatchContextScoped(
        void (*fn)(void *), void *arg,
        wx_wasm_execution::WorkClass cls,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::DiscardCallback discard = nullptr,
        wx_wasm_execution::CoalesceClass coalesce =
                wx_wasm_execution::CoalesceClass::None,
        std::uintptr_t coalesceKey = 0,
        wx_wasm_execution::BrowserIngressReceipt receipt = {});

#endif // _WX_WASM_PRIVATE_EXECUTION_OWNER_H_
