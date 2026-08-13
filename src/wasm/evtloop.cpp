/////////////////////////////////////////////////////////////////////////////
// Name:        wx/wasm/evtloop.cpp
// Purpose:     wxGUIEventLoop implementation
// Author:      Adam Hilss
// Copyright:   (c) 2022 Adam Hilss
// Licence:     LGPL v2
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"

#include "wx/app.h"
#include "wx/dialog.h"
#include "wx/evtloop.h"
#include "wx/init.h"
#include "wx/nonownedwnd.h"
#include "wx/toplevel.h"
#include "wx/weakref.h"
#include "wx/wasm/private/execution_owner.h"
#include "wx/wasm/private/mailbox.h"
#include "wx/wasm/private/mainloop.h"
#include "wx/wasm/private/mainstack.h"
#include "wx/wasm/private/sched_context.h"
#include "wx/wasm/private/yieldwait.h"

#include <emscripten.h>
#include <emscripten/stack.h>
#include <stdio.h>   // printf: diagnostics land in the browser console
#include <string.h>  // strcmp: park-reason comparison

#include <cstdlib>
#include <map>
#include <mutex>
#include <new>
#include <string>
#include <tuple>
#include <vector>

namespace
{
// The browser-delay mailbox is transport only. Its fresh callback stages each
// typed item in this owner queue; admission happens later on a dispatch
// context. Defined with the dispatch-job storage below.
bool wxWasmStageExecutionJob(
        void (*fn)(void *), void *arg,
        wx_wasm_execution::WorkClass workClass,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::LeaseToken leaseProvenance,
        std::uint32_t ingressSequence = 0,
        wx_wasm_execution::DiscardCallback discard = nullptr,
        wx_wasm_execution::CoalesceClass coalesce =
                wx_wasm_execution::CoalesceClass::None,
        std::uintptr_t coalesceKey = 0,
        std::size_t retainedBytes = 0);
void wxWasmDiscardQueuedJobs();

wx_wasm_execution::Coordinator& wxWasmExecutionCoordinator()
{
    static wx_wasm_execution::Coordinator s_coordinator;
    return s_coordinator;
}

// Context IDs identify physical stacks only. This separate map records which
// semantic owner, if any, is executing on each stack.
std::map<pcbjam_sched::ContextId, wx_wasm_execution::OwnerToken>&
wxWasmContextOwners()
{
    static std::map<pcbjam_sched::ContextId, wx_wasm_execution::OwnerToken> s_owners;
    return s_owners;
}

struct wxWasmModalLeaseRecord
{
    wx_wasm_execution::LeaseToken lease;
    int waitToken = 0;
    wxWasmExecutionLeaseReadyCallback readyCallback = nullptr;
    int closeResult = 0;
    bool closeRequested = false;
    bool completionDelivered = false;
};

std::map<const void *, wxWasmModalLeaseRecord>& wxWasmModalLeases()
{
    static std::map<const void *, wxWasmModalLeaseRecord> s_leases;
    return s_leases;
}

bool wxWasmTryCompleteModalLease(const void *scope)
{
    const auto it = wxWasmModalLeases().find(scope);

    if (it == wxWasmModalLeases().end())
        return false;

    wxWasmModalLeaseRecord& record = it->second;

    if (!record.closeRequested || record.completionDelivered
        || !wxWasmExecutionCoordinator().LeaseReady(record.lease))
    {
        return false;
    }

    // Both completion mechanisms defer the actual parent continuation:
    // scheduler context waits arm a fresh pump and Promise resolution queues a
    // microtask. Neither can re-enter the child stack being released here.
    if (record.waitToken > 0)
    {
        if (!wxWasmResolveWait(record.waitToken, record.closeResult))
        {
            wxWasmExecutionFailStop("modal lease lost its exact wait token");
            return false;
        }

        record.completionDelivered = true;
    }
    else
    {
        // The callback may fail-stop and clear wxWasmModalLeases(). Mark the
        // record before invoking it so we never write through an invalidated
        // map reference. Callback failure is terminal and needs no rollback.
        record.completionDelivered = true;
        record.readyCallback(scope, record.closeResult);
    }

    return true;
}

void wxWasmTryCompleteActiveModalLease()
{
    const wx_wasm_execution::LeaseToken active =
            wxWasmExecutionCoordinator().ActiveLease();

    if (!active)
        return;

    for (const auto& entry : wxWasmModalLeases())
    {
        if (entry.second.lease == active)
        {
            wxWasmTryCompleteModalLease(entry.first);
            return;
        }
    }
}

wx_wasm_execution::OwnerToken& wxWasmStartupOwner()
{
    static wx_wasm_execution::OwnerToken s_owner;
    return s_owner;
}

struct wxWasmEmbindOwnerRecord
{
    wx_wasm_execution::OwnerToken owner;
    unsigned jobId = 0;
    std::string failure;
};

std::map<std::uint64_t, wxWasmEmbindOwnerRecord>& wxWasmEmbindOwners()
{
    static std::map<std::uint64_t, wxWasmEmbindOwnerRecord> s_owners;
    return s_owners;
}

struct wxWasmPendingEventProvenance
{
    wx_wasm_execution::OwnerToken owner;
    wx_wasm_execution::LeaseToken lease;
    wx_wasm_execution::ScopeToken targetScope;
    bool nextModal = false;
};

using wxWasmPendingEventIndexKey =
        std::tuple<std::uint64_t, std::uintptr_t, std::uint32_t>;

struct wxWasmPendingEventState
{
    std::map<const void *, wxWasmPendingEventProvenance> events;
    std::map<wxWasmPendingEventIndexKey, size_t> byOwner;
    std::map<wxWasmPendingEventIndexKey, size_t> byLease;
    wx_wasm_execution::PendingEventQueueStats stats;
    bool closed = false;
    bool failureReported = false;
};

wxWasmPendingEventState& wxWasmPendingEvents()
{
    static wxWasmPendingEventState s_state;
    return s_state;
}

// Explicit window-family capabilities for auxiliary, non-window handlers.
// Registration and lookup are main-runtime-thread only; worker QueueEvent
// submissions deliberately remain unowned and do not inspect this map.
std::map<const void *, wx_wasm_execution::ScopeToken>&
wxWasmPendingEventHandlerScopes()
{
    static std::map<const void *, wx_wasm_execution::ScopeToken> s_scopes;
    return s_scopes;
}

wx_wasm_execution::ScopeToken wxWasmPendingEventScopeForHandler(
        const void *handler)
{
    if (!handler)
        return {};

    const auto it = wxWasmPendingEventHandlerScopes().find(handler);
    return it == wxWasmPendingEventHandlerScopes().end()
            ? wx_wasm_execution::ScopeToken{} : it->second;
}

// A delegated operation scopes one exact QueueEvent call.  Keep the frame on
// the posting thread's C++ stack instead of installing authority on a
// process-wide wxEvtHandler.  Matching both pointers prevents a nested or
// unrelated post from observing the capability.
struct wxWasmPendingEventDelegateFrame
{
    const void *handler = nullptr;
    const void *event = nullptr;
    wx_wasm_execution::PendingEventDelegate delegate;
    const wxWasmPendingEventDelegateFrame *previous = nullptr;
};

thread_local const wxWasmPendingEventDelegateFrame *
        wxWasmActivePendingEventDelegate = nullptr;

class wxWasmPendingEventDelegateScope
{
public:
    wxWasmPendingEventDelegateScope(
            const void *handler, const void *event,
            wx_wasm_execution::PendingEventDelegate delegate)
    {
        m_frame.handler = handler;
        m_frame.event = event;
        m_frame.delegate = delegate;
        m_frame.previous = wxWasmActivePendingEventDelegate;
        wxWasmActivePendingEventDelegate = &m_frame;
    }

    ~wxWasmPendingEventDelegateScope()
    {
        wxWasmActivePendingEventDelegate = m_frame.previous;
    }

    wxDECLARE_NO_COPY_CLASS(wxWasmPendingEventDelegateScope);

private:
    wxWasmPendingEventDelegateFrame m_frame;
};

wx_wasm_execution::PendingEventDelegate wxWasmPendingEventDelegateFor(
        const void *event, const void *handler)
{
    const wxWasmPendingEventDelegateFrame *frame =
            wxWasmActivePendingEventDelegate;

    if (!frame || frame->event != event || frame->handler != handler)
        return {};

    return frame->delegate;
}

std::mutex& wxWasmPendingEventOwnersMutex()
{
    static std::mutex s_mutex;
    return s_mutex;
}

wxWasmPendingEventIndexKey wxWasmPendingEventIndex(
        std::uint64_t id, wx_wasm_execution::ScopeToken scope)
{
    return std::make_tuple(id, scope.value, scope.generation);
}

void wxWasmDecrementPendingEventIndex(
        std::map<wxWasmPendingEventIndexKey, size_t>& index,
        const wxWasmPendingEventIndexKey& key)
{
    const auto it = index.find(key);

    if (it == index.end())
        return;

    if (it->second > 1)
        --it->second;
    else
        index.erase(it);
}

void wxWasmRemovePendingEventIndexes(
        wxWasmPendingEventState& state,
        const wxWasmPendingEventProvenance& provenance)
{
    if (provenance.owner && provenance.targetScope)
    {
        wxWasmDecrementPendingEventIndex(
                state.byOwner,
                wxWasmPendingEventIndex(
                        provenance.owner.id.value, provenance.targetScope));
    }

    if (provenance.lease && provenance.targetScope)
    {
        wxWasmDecrementPendingEventIndex(
                state.byLease,
                wxWasmPendingEventIndex(
                        provenance.lease.id.value, provenance.targetScope));
    }
}

void wxWasmLatchPendingEventFailure(
        wxWasmPendingEventState& state,
        wx_wasm_execution::PendingEventFailure failure);

// A pending callback can be queued for a real dialog immediately before
// ShowModal() opens that dialog's lease.  Tagging records that fact on the
// exact event; a handler association which merely shares the dialog scope is
// not this capability.  At lease creation, consume the marker only when its
// parent owner and target generation also match.  An event from an older
// lease can therefore never borrow a newer one.
//
// Keep the physical wx list and its FIFO order unchanged.  Publish the new
// lease index before updating any records so allocation failure leaves every
// event in its original, internally consistent state.
bool wxWasmBindPendingEventsToLease(
        const wx_wasm_execution::OwnerToken& parent,
        const wx_wasm_execution::LeaseToken& lease,
        size_t& transferred)
{
    transferred = 0;
    std::lock_guard<std::mutex> lock(wxWasmPendingEventOwnersMutex());
    wxWasmPendingEventState& state = wxWasmPendingEvents();

    if (state.stats.failure
            != wx_wasm_execution::PendingEventFailure::None)
    {
        return false;
    }

    if (state.closed)
    {
        wxWasmLatchPendingEventFailure(
                state,
                wx_wasm_execution::PendingEventFailure::MissingProvenance);
        return false;
    }

    if (!parent || !lease || !lease.targetScope)
    {
        wxWasmLatchPendingEventFailure(
                state,
                wx_wasm_execution::PendingEventFailure::MissingProvenance);
        return false;
    }

    for (const auto& entry : state.events)
    {
        const wxWasmPendingEventProvenance& provenance = entry.second;
        if (provenance.nextModal && provenance.owner == parent
            && !provenance.lease
            && provenance.targetScope == lease.targetScope)
        {
            ++transferred;
        }
    }

    if (transferred == 0)
        return true;

    try
    {
        state.byLease[wxWasmPendingEventIndex(
                lease.id.value, lease.targetScope)] += transferred;
    }
    catch (...)
    {
        transferred = 0;
        wxWasmLatchPendingEventFailure(
                state,
                wx_wasm_execution::PendingEventFailure::Allocation);
        return false;
    }

    for (auto& entry : state.events)
    {
        wxWasmPendingEventProvenance& provenance = entry.second;
        if (provenance.nextModal && provenance.owner == parent
            && !provenance.lease
            && provenance.targetScope == lease.targetScope)
        {
            provenance.lease = lease;
            provenance.nextModal = false;
        }
    }

    return true;
}

void wxWasmLatchPendingEventFailure(
        wxWasmPendingEventState& state,
        wx_wasm_execution::PendingEventFailure failure)
{
    if (state.stats.failure == wx_wasm_execution::PendingEventFailure::None)
        state.stats.failure = failure;
}

const char *wxWasmPendingEventFailureReason(
        wx_wasm_execution::PendingEventFailure failure)
{
    switch (failure)
    {
        case wx_wasm_execution::PendingEventFailure::Overflow:
            return "wx pending-event queue exceeded capacity";
        case wx_wasm_execution::PendingEventFailure::DuplicatePointer:
            return "wx pending-event pointer was queued twice";
        case wx_wasm_execution::PendingEventFailure::Allocation:
            return "wx pending-event provenance allocation failed";
        case wx_wasm_execution::PendingEventFailure::MissingProvenance:
            return "wx pending event lost its execution provenance";
        case wx_wasm_execution::PendingEventFailure::None:
            break;
    }

    return nullptr;
}

// QueueEvent may run on a pthread. The worker may latch a failure under the
// mutex, but only a main-runtime pump may touch the coordinator or JavaScript.
// The first main observer reports the exact one-way reason and fail-stops.
bool wxWasmConsumePendingEventFailure()
{
    if (!emscripten_is_main_runtime_thread())
        return true;

    wx_wasm_execution::PendingEventFailure failure =
            wx_wasm_execution::PendingEventFailure::None;
    bool shouldReport = false;

    {
        std::lock_guard<std::mutex> lock(wxWasmPendingEventOwnersMutex());
        wxWasmPendingEventState& state = wxWasmPendingEvents();
        failure = state.stats.failure;
        shouldReport = failure != wx_wasm_execution::PendingEventFailure::None
                       && !state.failureReported;

        if (shouldReport)
            state.failureReported = true;
    }

    if (failure == wx_wasm_execution::PendingEventFailure::None)
        return true;

    if (shouldReport)
    {
        const wx_wasm_execution::PendingEventQueueStats stats =
                wxWasmExecutionPendingEventQueueStats();
        std::printf("[wx-owner] pending-event terminal failure=%u retained=%zu "
                    "high-water=%zu accepted=%llu rejected=%llu\n",
                    static_cast<unsigned>(failure), stats.retained,
                    stats.highWater,
                    static_cast<unsigned long long>(stats.accepted),
                    static_cast<unsigned long long>(stats.rejected));
        std::fflush(stdout);
    }

    wxWasmExecutionFailStop(wxWasmPendingEventFailureReason(failure));
    return false;
}

wx_wasm_execution::OwnerToken wxWasmMappedOwnerForCurrentStack()
{
    const pcbjam_sched::ContextId context = pcbjam_sched::current();

    // Context 0 is the shared browser/main stack, not a durable execution
    // identity. A later JS entry can reuse it while an in-place park is live,
    // so it must never inherit an owner from a numeric map entry.
    if (context == 0)
        return {};

    // An in-place Asyncify park unwinds out through drain() before its
    // post-swap bookkeeping clears `running`. A fresh browser callback can
    // therefore observe the parked context ID while physically executing on
    // the shared main stack. Numeric equality is not provenance: require this
    // frame to lie in the exact registered context allocation before inheriting
    // its owner.
    if (pcbjam_sched::context_owning_current_stack() != context
        || pcbjam_sched::context_has_inplace_park(context))
        return {};

    const auto it = wxWasmContextOwners().find(context);

    if (it != wxWasmContextOwners().end())
        return it->second;

    return {};
}

wx_wasm_execution::OwnerToken wxWasmInternalOwnerForCurrentStack()
{
    const wx_wasm_execution::OwnerToken mapped =
            wxWasmMappedOwnerForCurrentStack();

    if (mapped)
        return mapped;

    // A KiCad tool fiber can run on top of the admitted wx dispatch context.
    // The scheduler then reports the tool's ContextId, not the dispatch ID.
    // `fiber_current()` alone is not proof: after an in-place Asyncify park it
    // deliberately remembers the parked fiber while a fresh browser callback
    // runs on the main stack. Require the caller's frame to be inside that
    // exact registered fiber allocation before inheriting the active branch.
    const pcbjam_sched::ContextId stackOwner =
            pcbjam_sched::context_owning_current_stack();

    if (stackOwner != 0
        && !pcbjam_sched::context_has_inplace_park(stackOwner)
        && stackOwner == pcbjam_sched::fiber_current())
        return wxWasmExecutionCoordinator().CurrentBranchOwner();

    return {};
}

bool wxWasmReleaseExecutionOwner(
        const wx_wasm_execution::OwnerToken& owner);
void wxWasmCompleteRetiredEmbindOwners();

class wxWasmExecutionScope
{
public:
    explicit wxWasmExecutionScope(const wx_wasm_execution::Admission& admission)
        : wxWasmExecutionScope(admission.owner, true)
    {
    }

    wxWasmExecutionScope(wx_wasm_execution::OwnerToken owner,
                         bool releaseOnExit)
        : m_context(pcbjam_sched::current()), m_owner(owner),
          m_releaseOnExit(releaseOnExit)
    {
        auto& owners = wxWasmContextOwners();
        const auto old = owners.find(m_context);

        if (old != owners.end())
        {
            m_hadPrevious = true;
            m_previous = old->second;
        }

        try
        {
            owners.insert_or_assign(m_context, m_owner);
            m_published = true;
        }
        catch (...)
        {
            // Admit() transferred one semantic reference to this scope. A C++
            // constructor which throws never runs its destructor, so consume
            // that reference here and let the caller discard any already-popped
            // payload before it fail-stops the instance.
            if (m_releaseOnExit)
            {
                wxWasmReleaseExecutionOwner(m_owner);
                m_releaseOnExit = false;
            }
        }
    }

    explicit operator bool() const { return m_published; }

    ~wxWasmExecutionScope()
    {
        if (!m_published)
            return;

        auto& owners = wxWasmContextOwners();

        // Fail-stop/teardown already abandoned every semantic reference and
        // cleared this map. A scope unwinding after that terminal edge must
        // not restore the stale mapping it observed on entry or release the
        // same logical reference a second time.
        if (wxWasmExecutionCoordinator().Failed())
        {
            owners.erase(m_context);
            return;
        }

        if (m_hadPrevious)
            owners[m_context] = m_previous;
        else
            owners.erase(m_context);

        if (m_releaseOnExit && !wxWasmReleaseExecutionOwner(m_owner))
        {
            printf("[wx-owner] refused release owner=%llu generation=%llu\n",
                   static_cast<unsigned long long>(m_owner.id.value),
                   static_cast<unsigned long long>(m_owner.generation));
        }
    }

    wxDECLARE_NO_COPY_CLASS(wxWasmExecutionScope);

private:
    pcbjam_sched::ContextId m_context;
    wx_wasm_execution::OwnerToken m_owner;
    wx_wasm_execution::OwnerToken m_previous;
    bool m_hadPrevious = false;
    bool m_published = false;
    bool m_releaseOnExit = true;
};
} // namespace

EM_JS(void, wxWasmExecutionShutdownJs,
      (const char *reason, int failed), {
    if (failed) globalThis.__wxWasmFailed = true;
    if (globalThis.__wxScheduler &&
        typeof globalThis.__wxScheduler.shutdown === "function") {
        globalThis.__wxScheduler.shutdown(
            reason ? UTF8ToString(reason) :
                     (failed ? "fatal Wasm execution failure" :
                               "Wasm application shutdown"));
    }
});

EM_JS(void, wxWasmEmbindCompleteJs,
      (unsigned jobId, const char *failure), {
    var scheduler = globalThis.__wxScheduler;
    if (scheduler && typeof scheduler.completeMutator === "function") {
        scheduler.completeMutator(
            jobId >>> 0, failure ? UTF8ToString(failure) : "");
    }
});

namespace
{
void wxWasmCompleteRetiredEmbindOwners()
{
    wx_wasm_execution::Coordinator& coordinator =
            wxWasmExecutionCoordinator();
    auto& records = wxWasmEmbindOwners();

    for (auto it = records.begin(); it != records.end();)
    {
        if (coordinator.Owns(it->second.owner))
        {
            ++it;
            continue;
        }

        const wxWasmEmbindOwnerRecord record = it->second;
        it = records.erase(it);
        wxWasmEmbindCompleteJs(
                record.jobId,
                record.failure.empty() ? nullptr : record.failure.c_str());
    }
}
} // namespace

namespace
{
void wxWasmTerminateExecution(const char *reason, bool failed)
{
    wx_wasm_execution::Coordinator& coordinator =
            wxWasmExecutionCoordinator();

    if (coordinator.Failed())
        return;

    // Latch native admission closed before calling JavaScript or invoking any
    // payload destructor. Either path can report a secondary failure; it must
    // observe this one-way transition instead of recursively cleaning the
    // same queue.
    coordinator.Abandon();
    wxWasmExecutionShutdownJs(reason, failed ? 1 : 0);
    wxWasmDiscardQueuedJobs();
    wxWasmEmbindOwners().clear();
    wxWasmContextOwners().clear();
    wxWasmModalLeases().clear();
    {
        std::lock_guard<std::mutex> lock(wxWasmPendingEventOwnersMutex());
        wxWasmPendingEventState& state = wxWasmPendingEvents();
        state.stats.clearedOnShutdown += state.events.size();
        state.events.clear();
        state.byOwner.clear();
        state.byLease.clear();
        state.closed = true;
    }
    wxWasmStartupOwner() = {};
}
} // namespace

void wxWasmExecutionFailStop(const char *reason)
{
    wxWasmTerminateExecution(reason, true);
}

namespace
{
bool wxWasmSchedulerEntryBusy()
{
    return pcbjam_sched::current() != 0
           || pcbjam_sched::transition_in_flight()
           || pcbjam_sched::any_context_has_inplace_park();
}

void wxWasmParkExternalOrAbort(const char *reason)
{
    const pcbjam_sched::ParkResult parked =
            pcbjam_sched::yield_park(reason);

    if (!parked.accepted)
    {
        wxWasmExecutionFailStop(
                "scheduler context refused its required external park");
        std::abort();
    }
}
} // namespace

// Leaf admission probe for browser callbacks which can mutate scheduler
// readiness or start a context. It deliberately tests only physical scheduler
// state. Semantic owner admission remains the execution coordinator's job.
// This function must never suspend, allocate a context, or drain work.
extern "C" int EMSCRIPTEN_KEEPALIVE wxWasmNativeEntryReady()
{
    return !wxWasmExecutionCoordinator().Failed()
           && !wxWasmSchedulerEntryBusy() ? 1 : 0;
}

// ----------------------------------------------------------------------------
// Scheduler mailbox (wx/wasm/private/mailbox.h). The queue itself lives in
// the injected asyncify-scheduler.js
// shim — this side pushes deferred callbacks and pulls due messages from the
// pump's clean stack. The shim is the only runtime; a glue without it is a
// broken build, caught loudly
// by wxWasmSchedulerAssertInstalled() at main-loop entry.
// ----------------------------------------------------------------------------

EM_JS(int, wxWasmMailboxJsEnabled, (), {
    return (typeof globalThis !== "undefined" &&
            globalThis.__wxSchedulerInstalled &&
            globalThis.__wxScheduler &&
            globalThis.__wxScheduler.mailbox) ? 1 : 0;
});

void wxWasmPublishActiveBrowserIngressLease();

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

    // Replace the shim's conservative unavailable sentinel with the exact
    // initial no-lease state before any registered browser callback can rely
    // on receipt provenance.
    wxWasmPublishActiveBrowserIngressLease();
}

EM_JS(unsigned, wxWasmCaptureBrowserIngressReceiptJs, (), {
    var scheduler = globalThis.__wxScheduler;
    if (!scheduler
        || typeof scheduler.captureNativeIngressReceipt !== "function")
        return 0;
    return scheduler.captureNativeIngressReceipt() >>> 0;
});

EM_JS(int, wxWasmTakeBrowserIngressReceiptJs,
      (unsigned token, unsigned *sequenceOut, unsigned *flagsOut,
       std::uintptr_t *targetScopeOut, unsigned *targetGenerationOut,
       unsigned *leaseIdLowOut, unsigned *leaseIdHighOut,
       unsigned *leaseParentLowOut, unsigned *leaseParentHighOut,
       unsigned *leaseGenerationLowOut, unsigned *leaseGenerationHighOut), {
    var scheduler = globalThis.__wxScheduler;
    if (!scheduler || typeof scheduler.takeIngressReceipt !== "function")
        return 0;
    var receipt = scheduler.takeIngressReceipt(token >>> 0);
    if (!receipt) return 0;
    var flags = (receipt.snapshotAvailable ? 1 : 0)
              | (receipt.hasLease ? 2 : 0)
              | (receipt.deferredBehindEarlier ? 4 : 0);
    HEAPU32[sequenceOut >> 2] = receipt.sequence >>> 0;
    HEAPU32[flagsOut >> 2] = flags >>> 0;
    HEAPU32[targetScopeOut >> 2] = receipt.targetScope >>> 0;
    HEAPU32[targetGenerationOut >> 2] = receipt.targetGeneration >>> 0;
    HEAPU32[leaseIdLowOut >> 2] = receipt.leaseIdLow >>> 0;
    HEAPU32[leaseIdHighOut >> 2] = receipt.leaseIdHigh >>> 0;
    HEAPU32[leaseParentLowOut >> 2] = receipt.leaseParentLow >>> 0;
    HEAPU32[leaseParentHighOut >> 2] = receipt.leaseParentHigh >>> 0;
    HEAPU32[leaseGenerationLowOut >> 2] = receipt.leaseGenerationLow >>> 0;
    HEAPU32[leaseGenerationHighOut >> 2] = receipt.leaseGenerationHigh >>> 0;
    return 1;
});

EM_JS(unsigned, wxWasmEarliestUnstagedBrowserIngressReceiptJs, (), {
    var scheduler = globalThis.__wxScheduler;
    // Missing receipt state cannot prove that the external FIFO has no gap.
    // The mandatory-scheduler assertion/fail-stop owns recovery; native queue
    // admission remains conservative in the meantime.
    if (!scheduler || !scheduler.pendingIngressReceipts)
        return 1;
    var first = scheduler.pendingIngressReceipts.values().next();
    return first.done ? 0 : first.value >>> 0;
});

EM_JS(int, wxWasmPublishBrowserIngressLeaseJs,
      (int hasLease, std::uintptr_t targetScope, unsigned targetGeneration,
       unsigned leaseIdLow, unsigned leaseIdHigh,
       unsigned leaseParentLow, unsigned leaseParentHigh,
       unsigned leaseGenerationLow, unsigned leaseGenerationHigh), {
    var scheduler = globalThis.__wxScheduler;
    if (!scheduler
        || typeof scheduler.publishIngressLeaseSnapshot !== "function")
        return 0;
    return scheduler.publishIngressLeaseSnapshot({
      available: true,
      hasLease: !!hasLease,
      targetScope: targetScope >>> 0,
      targetGeneration: targetGeneration >>> 0,
      leaseIdLow: leaseIdLow >>> 0,
      leaseIdHigh: leaseIdHigh >>> 0,
      leaseParentLow: leaseParentLow >>> 0,
      leaseParentHigh: leaseParentHigh >>> 0,
      leaseGenerationLow: leaseGenerationLow >>> 0,
      leaseGenerationHigh: leaseGenerationHigh >>> 0,
    }) ? 1 : 0;
});

void wxWasmPublishActiveBrowserIngressLease()
{
    wx_wasm_execution::Coordinator& coordinator =
            wxWasmExecutionCoordinator();
    // This snapshot is a receipt capability, not permission to execute. When
    // the top lease is closing, preserve the nearest accepting ancestor's
    // generation so its browser input can wait behind the closing child.
    // Native admission still consults the exact ActiveLease().
    const wx_wasm_execution::LeaseToken lease =
            coordinator.IngressReceiptLease();
    const wx_wasm_execution::ScopeToken targetScope =
            lease ? lease.targetScope : wx_wasm_execution::ScopeToken{};

    if (!wxWasmPublishBrowserIngressLeaseJs(
            lease ? 1 : 0, targetScope.value, targetScope.generation,
            static_cast<unsigned>(lease.id.value),
            static_cast<unsigned>(lease.id.value >> 32),
            static_cast<unsigned>(lease.parent.value),
            static_cast<unsigned>(lease.parent.value >> 32),
            static_cast<unsigned>(lease.generation),
            static_cast<unsigned>(lease.generation >> 32)))
    {
        wxWasmExecutionFailStop(
                "browser ingress lease publication was refused");
    }
}

EM_JS(unsigned, wxWasmMailboxJsEnqueue,
      (void *fn, void *arg, int ms, unsigned workClass,
       std::uintptr_t targetScope, unsigned targetGeneration,
       void *discard, unsigned coalesce,
       unsigned leaseIdLow, unsigned leaseIdHigh,
       unsigned leaseParentLow, unsigned leaseParentHigh,
       unsigned leaseGenerationLow, unsigned leaseGenerationHigh), {
    return globalThis.__wxScheduler.enqueueAfter(
        fn, arg, ms, workClass >>> 0, targetScope >>> 0,
        targetGeneration >>> 0, discard, coalesce >>> 0,
        leaseIdLow >>> 0, leaseIdHigh >>> 0,
        leaseParentLow >>> 0, leaseParentHigh >>> 0,
        leaseGenerationLow >>> 0, leaseGenerationHigh >>> 0) >>> 0;
});

EM_JS(int, wxWasmMailboxJsCancel, (unsigned timerId), {
    return globalThis.__wxScheduler.cancelMailbox(timerId >>> 0) ? 1 : 0;
});

EM_JS(int, wxWasmMailboxJsPending, (), {
    return globalThis.__wxScheduler.mailbox.length;
});

// Pop the oldest due message into *fnOut/*argOut; 0 if the queue is empty.
EM_JS(int, wxWasmMailboxJsPop,
      (void **fnOut, void **argOut, unsigned *workClassOut,
       std::uintptr_t *targetScopeOut, unsigned *targetGenerationOut,
       void **discardOut, unsigned *coalesceOut,
       unsigned *leaseIdLowOut, unsigned *leaseIdHighOut,
       unsigned *leaseParentLowOut, unsigned *leaseParentHighOut,
       unsigned *leaseGenerationLowOut, unsigned *leaseGenerationHighOut), {
    var m = globalThis.__wxScheduler.pop();
    if (!m) return 0;
    HEAPU32[fnOut >> 2] = m.fn;
    HEAPU32[argOut >> 2] = m.arg;
    HEAPU32[workClassOut >> 2] = m.workClass >>> 0;
    HEAPU32[targetScopeOut >> 2] = m.targetScope >>> 0;
    HEAPU32[targetGenerationOut >> 2] = m.targetGeneration >>> 0;
    HEAPU32[discardOut >> 2] = m.discard >>> 0;
    HEAPU32[coalesceOut >> 2] = m.coalesce >>> 0;
    HEAPU32[leaseIdLowOut >> 2] = m.leaseIdLow >>> 0;
    HEAPU32[leaseIdHighOut >> 2] = m.leaseIdHigh >>> 0;
    HEAPU32[leaseParentLowOut >> 2] = m.leaseParentLow >>> 0;
    HEAPU32[leaseParentHighOut >> 2] = m.leaseParentHigh >>> 0;
    HEAPU32[leaseGenerationLowOut >> 2] = m.leaseGenerationLow >>> 0;
    HEAPU32[leaseGenerationHighOut >> 2] = m.leaseGenerationHigh >>> 0;
    return 1;
});

extern "C" void wxWasmMailboxEnqueueAfter(void (*fn)(void *), void *arg,
                                          int millisecs)
{
    wxWasmMailboxEnqueueAfterScoped(
            fn, arg, millisecs,
            wx_wasm_execution::WorkClass::Ordinary, {});
}

wxWasmMailboxTimerId wxWasmMailboxEnqueueAfterScoped(
        void (*fn)(void *), void *arg, int millisecs,
        wx_wasm_execution::WorkClass workClass,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::DiscardCallback discard,
        wx_wasm_execution::CoalesceClass coalesce)
{
    if (!fn || wxWasmExecutionCoordinator().Failed())
    {
        if (discard)
            discard(arg);
        return 0;
    }

    const wx_wasm_execution::LeaseToken leaseProvenance =
            wx_wasm_execution::LeaseProvenanceForIngress(
                    workClass, targetScope,
                    wxWasmExecutionCoordinator().ActiveLease());

    const wxWasmMailboxTimerId timerId = wxWasmMailboxJsEnqueue(
            reinterpret_cast<void *>(fn), arg, millisecs,
            static_cast<unsigned>(workClass), targetScope.value,
            targetScope.generation,
            reinterpret_cast<void *>(discard),
            static_cast<unsigned>(coalesce),
            static_cast<unsigned>(leaseProvenance.id.value),
            static_cast<unsigned>(leaseProvenance.id.value >> 32),
            static_cast<unsigned>(leaseProvenance.parent.value),
            static_cast<unsigned>(leaseProvenance.parent.value >> 32),
            static_cast<unsigned>(leaseProvenance.generation),
            static_cast<unsigned>(leaseProvenance.generation >> 32));

    // A zero return means JavaScript never accepted ownership. Invoke the
    // same exact discard callback used by later native queue abandonment.
    if (!timerId && discard)
        discard(arg);

    return timerId;
}

bool wxWasmMailboxCancel(wxWasmMailboxTimerId timerId)
{
    return timerId && wxWasmMailboxJsCancel(timerId) != 0;
}

extern "C" {

    // The mailbox's own dispatch entry. Called by
    // the shim's self-armed delivery tick as a PLAIN export call — never from
    // inside a pump's `await ccall('ProcessEvents')`. Delivering from the
    // pump's awaited export put a fiber swap inside the JS-awaits-a-
    // suspending-export boundary (Emscripten #13302) and trapped `unreachable`
    // in the coroutine-nested battery (fiber_create_run_destroy_inside_modal).
    // A fresh sync entry is exactly the context legacy timer callbacks always
    // ran in — the mailbox changes WHEN a message runs (queued, in order,
    // owner-admissible), not the kind of stack it runs on.
    void EMSCRIPTEN_KEEPALIVE wxWasmMailboxTick()
    {
        if (!wxTheApp || wxWasmExecutionCoordinator().Failed())
            return;

        // The JavaScript arbiter probes immediately before this call. Keep a
        // native precondition too so no future direct caller can pop payloads
        // while another context is physically live.
        if (wxWasmSchedulerEntryBusy())
        {
            wxWasmExecutionFailStop(
                    "mailbox tick entered while scheduler was busy");
            return;
        }

        // Move only the snapshot present on entry. A zero-delay periodic timer
        // scheduled by one staged callback arrives in a later browser task and
        // therefore cannot extend this transport pass without bound.
        int budget = wxWasmMailboxJsPending();
        while (budget-- > 0)
        {
            void *fn = NULL;
            void *arg = NULL;
            unsigned workClassValue = 0;
            std::uintptr_t targetScopeValue = 0;
            unsigned targetGeneration = 0;
            void *discard = NULL;
            unsigned coalesceValue = 0;
            unsigned leaseIdLow = 0;
            unsigned leaseIdHigh = 0;
            unsigned leaseParentLow = 0;
            unsigned leaseParentHigh = 0;
            unsigned leaseGenerationLow = 0;
            unsigned leaseGenerationHigh = 0;

            if (!wxWasmMailboxJsPop(
                    &fn, &arg, &workClassValue, &targetScopeValue,
                    &targetGeneration, &discard, &coalesceValue,
                    &leaseIdLow, &leaseIdHigh,
                    &leaseParentLow, &leaseParentHigh,
                    &leaseGenerationLow, &leaseGenerationHigh))
                break;

            const wx_wasm_execution::WorkClass workClass =
                    static_cast<wx_wasm_execution::WorkClass>(workClassValue);
            if (!wx_wasm_execution::IsValidWorkClass(workClass))
            {
                if (discard)
                {
                    reinterpret_cast<wx_wasm_execution::DiscardCallback>(
                            discard)(arg);
                }
                wxWasmExecutionFailStop("invalid mailbox work classification");
                return;
            }

            wx_wasm_execution::ScopeToken targetScope;
            targetScope.value = targetScopeValue;
            targetScope.generation = targetGeneration;
            wx_wasm_execution::LeaseToken leaseProvenance;
            leaseProvenance.id.value =
                    static_cast<std::uint64_t>(leaseIdLow)
                    | (static_cast<std::uint64_t>(leaseIdHigh) << 32);
            leaseProvenance.parent.value =
                    static_cast<std::uint64_t>(leaseParentLow)
                    | (static_cast<std::uint64_t>(leaseParentHigh) << 32);
            leaseProvenance.generation =
                    static_cast<std::uint64_t>(leaseGenerationLow)
                    | (static_cast<std::uint64_t>(leaseGenerationHigh) << 32);
            if (leaseProvenance)
                leaseProvenance.targetScope = targetScope;

            const bool accepted = wxWasmStageExecutionJob(
                    reinterpret_cast<void (*)(void *)>(fn), arg,
                    workClass, targetScope, leaseProvenance,
                    0,
                    reinterpret_cast<wx_wasm_execution::DiscardCallback>(
                            discard),
                    static_cast<wx_wasm_execution::CoalesceClass>(
                            coalesceValue));

            if (!accepted && discard)
            {
                reinterpret_cast<wx_wasm_execution::DiscardCallback>(
                        discard)(arg);
                return;
            }
        }
    }

}  // extern "C"

// ----------------------------------------------------------------------------
// Scheduler token waits (wx/wasm/private/yieldwait.h).
// The wait registry lives in the injected scheduler shim; these are thin
// bridges. wxWasmYieldUntil is the ONE park primitive the migrated waits
// share — a handleSleep the scheduler manages like any other (deferred wakes,
// registry, recorder).
// ----------------------------------------------------------------------------

EM_JS(int, wxWasmBeginWaitJs, (const char *kind), {
    return globalThis.__wxScheduler.beginWait(UTF8ToString(kind));
});

EM_ASYNC_JS(int, wxWasmYieldUntilJs, (int token), {
    return await globalThis.__wxScheduler.waitPromise(token);
});

EM_JS(int, wxWasmResolveWaitJs, (int token, int result), {
    return globalThis.__wxScheduler.resolveWait(token, result) ? 1 : 0;
});

extern "C" int wxWasmBeginWait(const char *kind)
{
    return wxWasmBeginWaitJs(kind);
}

// Tell the shim that this token's waiter is a scheduler context, so
// resolveWait marks it ready instead of resolving a promise nobody awaits.
EM_JS(int, wxWasmNoteContextWaitJs, (int token), {
    return globalThis.__wxScheduler.noteContextWait(token) ? 1 : 0;
});

// Early-resolve window: a bridge whose request settles before the C++
// frame reaches the park (a provider answering from cache, or a test page with
// no provider at all) resolves the wait first. The shim retains such entries
// with the result attached; peek-and-consume here instead of parking a context
// whose wake has already been spent — that park is unresumable by construction.
EM_JS(int, wxWasmWaitEarlyResolvedJs, (int token), {
    return globalThis.__wxScheduler.waitEarlyResolved(token);
});

// The wait's kind, readable only BEFORE the resolve deletes the entry — so it
// is copied out at park time for the per-kind telemetry below.
EM_JS(void, wxWasmWaitKindJs, (int token, char *out, int cap), {
    const e = globalThis.__wxScheduler.waits.get(token);
    stringToUTF8((e && e.kind) || "?", out, cap);
});

namespace {

// Context-park buffer sizing: deepest observed park per wait
// kind. A new maximum beacons, so each bridge's high-water can be read
// straight out of any suite log instead of being guessed from a synthetic
// reducer baseline. Fixed table: the kind set is small and known ("lib", "fp-lib", "3d",
// "occ", "ngspice", "modal", "nested", ...).
struct WaitKindHighWater
{
    char kind[16];
    size_t bytes;
};

WaitKindHighWater s_waitKindHw[16];

void noteWaitKindParkUse(const char *kind, size_t used)
{
    if (!used || !kind || !*kind)
        return;

    for (auto &slot : s_waitKindHw)
    {
        if (slot.kind[0] == '\0')
        {
            snprintf(slot.kind, sizeof(slot.kind), "%s", kind);
        }
        else if (strcmp(slot.kind, kind) != 0)
        {
            continue;
        }

        if (used > slot.bytes)
        {
            slot.bytes = used;
            EM_ASM({ console.log("[wx-wait] high-water " + UTF8ToString($0) + "=" + $1 + "B"); },
                   slot.kind, (int) used);
        }
        return;
    }
}

}  // namespace

EM_JS(int, wxWasmTakeWaitResultJs, (int token), {
    return globalThis.__wxScheduler.takeWaitResult(token);
});

namespace
{
// Token -> the context parked on it. Small and short-lived:
// one entry per outstanding wait, erased on resolve.
std::map<int, pcbjam_sched::ContextId> &wxWasmContextWaits()
{
    static std::map<int, pcbjam_sched::ContextId> s_waits;
    return s_waits;
}

}  // namespace

extern "C" int wxWasmYieldUntil(int token)
{
    if (token <= 0)
    {
        wxWasmExecutionFailStop("wait has no valid exact token");
        return 0;
    }

    // If this wait is running on a scheduler context, park that
    // context rather than suspending the stack in place. yield_park verifies
    // the caller's frame really lies inside the running context's stack, so a
    // fiber swapped in above it (a tool coroutine) is refused rather than
    // silently saving the wrong stack — and falls back to the Asyncify park,
    // which is the correct in-place Asyncify fallback when no scheduler
    // context owns the current stack.
    const pcbjam_sched::ContextId self = pcbjam_sched::current();

    if (self && pcbjam_sched::can_yield_here())
    {
        // Already resolved before we could park: consume the retained result
        // instead of parking a context
        // nobody will resume. Nothing can interleave between this check and
        // the park below — wasm holds the thread for the whole block.
        if (wxWasmWaitEarlyResolvedJs(token))
            return wxWasmTakeWaitResultJs(token);

        char kind[16];
        wxWasmWaitKindJs(token, kind, sizeof(kind));

        wxWasmContextWaits()[token] = self;

        if (!wxWasmNoteContextWaitJs(token))
        {
            wxWasmContextWaits().erase(token);
            wxWasmExecutionFailStop(
                    "context wait could not acquire its JavaScript lease");
            return 0;
        }

        const pcbjam_sched::ParkResult parked =
                pcbjam_sched::yield_park(
                        "wx-wait",
                        pcbjam_sched::ParkWake::RetainedExact(
                                static_cast<pcbjam_sched::WakeToken>(token)));

        if (!parked.accepted)
        {
            // The Promise/provider which owns this generic wait may still
            // retain native output pointers. It is deliberately External,
            // not cancellable: terminalize without freeing this stack.
            wxWasmExecutionFailStop(
                    "context wait could not park its owning context");
            std::abort();
        }

        wxWasmContextWaits().erase(token);

        // The registry sampled this park's live capture at swap-out; fold it
        // into the per-kind high-water now that we know whose park it was.
        noteWaitKindParkUse(kind, pcbjam_sched::last_park_use_of(self));
        return parked.value;
    }

    return wxWasmYieldUntilJs(token);
}

// A budget continuation is control-plane work, not another model event.  The
// shim coalesces it in the physical native-entry FIFO and runs it from a new
// macrotask after the current Wasm export has returned completely.
EM_JS(int, wxWasmArmSchedulerContinuationJs, (), {
    var scheduler = globalThis.__wxScheduler;
    if (!scheduler || scheduler.dead
        || typeof scheduler._armSchedPump !== "function") return 0;
    scheduler._armSchedPump();
    return !scheduler.dead && scheduler._pumpArmed ? 1 : 0;
});

void wxWasmDrainSchedulerBatch()
{
    const pcbjam_sched::DrainResult result = pcbjam_sched::drain_all();

    if (result.disposition
            == pcbjam_sched::DrainDisposition::ContinueOnFreshTask)
    {
        if (!wxWasmArmSchedulerContinuationJs())
        {
            wxWasmExecutionFailStop(
                    "scheduler drain continuation could not be retained");
        }
    }
    else if (result.disposition == pcbjam_sched::DrainDisposition::Livelock)
    {
        wxWasmExecutionFailStop(
                "scheduler contexts exceeded the transfer livelock bound");
    }
}

extern "C" {

    // The shim's callback for a context-parked wait: mark it ready and let the
    // next pump resume it. Never resumes inline — a wake that rewound inside
    // the resolver's own JS turn is the whole class doc 13 §1.4 forbids.
    // The shim's pump entry: resume whatever the registry says is ready, from
    // a fresh JS task. Called after a context wake and by the top-level tick.
    void EMSCRIPTEN_KEEPALIVE wxWasmSchedPump()
    {
        if (wxWasmSchedulerEntryBusy())
        {
            wxWasmExecutionFailStop(
                    "scheduler pump entered while scheduler was busy");
            return;
        }

        wxWasmDrainSchedulerBatch();
    }

    int EMSCRIPTEN_KEEPALIVE wxWasmSchedResolveContextWait(int token, int result)
    {
        if (wxWasmSchedulerEntryBusy())
        {
            wxWasmExecutionFailStop(
                    "context-wait wake entered while scheduler was busy");
            return 0;
        }

        auto &waits = wxWasmContextWaits();
        auto it = waits.find(token);

        if (it == waits.end())
        {
            // The JavaScript side has already consumed this exact wait token.
            // Returning would make the lost wake an unrecoverable silent hang.
            wxWasmExecutionFailStop(
                    "context-wait resolution has no registered context");
            return 0;
        }

        if (!pcbjam_sched::mark_ready_retained(
                it->second, result,
                static_cast<pcbjam_sched::WakeToken>(token)))
        {
            // mark_ready already beacons the context identity. The exact
            // token was consumed before this call, so retry is impossible.
            wxWasmExecutionFailStop(
                    "context-wait target refused its exact wake");
            return 0;
        }

        waits.erase(it);

        return 1;
    }

}  // extern "C"

extern "C" bool wxWasmResolveWait(int token, int result)
{
    return wxWasmResolveWaitJs(token, result) != 0;
}

// Dispatch body used after the execution-owner coordinator has admitted this
// slice. wxGUIEventLoop::Dispatch()/wxYield deliberately recurse on the same
// owner and therefore call it directly.
static void wxWasmProcessEventsUngated(bool allowPaint = true,
                                       bool allowIdle = true)
{
    static int counter = 0;

    // The physical wx list may contain only parent/worker events while a modal
    // child owns execution. Its indexed provenance can answer that in O(1),
    // avoiding one full ineligible-list traversal on every browser frame.
    if (wxWasmExecutionHasProcessablePendingEvents())
        wxTheApp->ProcessPendingEvents();

    // A missing provenance record can be detected inside the list traversal.
    // Consume that terminal latch after the wx lock has been released.
    if (!wxWasmConsumePendingEventFailure())
        return;

    // A final animation-frame callback can already be admitted when the page
    // begins tearing down its last top-level window.  Pending-event cleanup
    // and idle processing can still be useful in that slice, but there is no
    // paint target.  Do not call wxApp::Paint(): its non-null top-window
    // precondition is deliberately strict and would turn normal same-tab
    // reload/teardown into an assertion.
    if (allowPaint && wxTheApp->GetTopWindow())
        wxTheApp->Paint();
    if (allowIdle && counter++ % 3 == 0)
    {
        wxTheApp->ProcessIdle();
    }
}

extern "C" {

    void EMSCRIPTEN_KEEPALIVE ProcessEvents()
    {
        if (!wxTheApp)
            return;

        // Synchronous wxYield/Dispatch recursion stays in the transaction
        // already assigned to this physical context. A fresh pump slice must
        // acquire a new ordinary owner, or a child transaction from the
        // active modal lease.
        const wx_wasm_execution::OwnerToken mappedOwner =
                wxWasmMappedOwnerForCurrentStack();
        if (mappedOwner)
        {
            const bool isRoot =
                    mappedOwner == wxWasmExecutionCoordinator().RootOwner();
            // A modal child can process only its selected pending events. A
            // global paint or idle traversal can read the parked parent's
            // half-mutated model.
            wxWasmProcessEventsUngated(isRoot, isRoot);
            return;
        }

        // Context 0 cannot distinguish a resumed stack from a fresh browser
        // entry and is therefore not a safe owner key. Preserve the last
        // complete frame on allocation failure; unowned progress is unsafe.
        if (pcbjam_sched::current() == 0)
            return;

        wx_wasm_execution::Coordinator& coordinator =
                wxWasmExecutionCoordinator();
        const wx_wasm_execution::WorkClass cls =
                coordinator.ActiveLease() && coordinator.LeaseAccepting()
                        ? wx_wasm_execution::WorkClass::PendingEvents
                        : wx_wasm_execution::WorkClass::Ordinary;
        const wx_wasm_execution::ScopeToken targetScope =
                cls == wx_wasm_execution::WorkClass::PendingEvents
                        ? coordinator.ActiveLeaseScope()
                        : wx_wasm_execution::ScopeToken{};
        const wx_wasm_execution::Admission admission =
                coordinator.Admit(cls, targetScope);

        if (!admission)
        {
            // Preserve the last stable frame. Paint is a model read and is not
            // safe merely because it does not dispatch a wx event.
            return;
        }

        wxWasmExecutionScope ownerScope(admission);
        if (!ownerScope)
        {
            wxWasmExecutionFailStop(
                    "execution stack-owner mapping allocation failed");
            return;
        }

        const bool isRoot =
                admission.kind == wx_wasm_execution::AdmissionKind::Root;
        wxWasmProcessEventsUngated(isRoot, isRoot);
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
//   * top level (DoRun depth 0): a C++ loop runs on the scheduler-owned main-loop
//     context. Each frame arms one requestAnimationFrame wake and parks that context
//     with yield_park(). The real main stack remains the scheduler stack.
//   * nested (DoRun depth >0): a registered "nested" scheduler wait, not a second
//     pump. The top-level tick keeps dispatching at any depth. ScheduleExit() resolves
//     the innermost wait. The top-level loop instead just sets m_shouldExit.

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

/** Is the caller's frame outside the captured main-stack allocation? */
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

extern "C" {

    // In-place park cross-check: the handleSleep wrapper reports every fresh
    // park to the registry. Begin() answers which context (if any) owns the
    // parking stack and records the park against it; End() clears it when the
    // park's wake completes. While recorded, fiber_enterable()/fiber_transfer
    // refuse entering that context. Returns 0 for main-stack parks
    // (nothing to guard — the scheduler stack has no fiber capture to
    // corrupt). Leaf-safe: called from the import frame before any unwind
    // begins / after the wake fully rewound.
    unsigned EMSCRIPTEN_KEEPALIVE wxWasmSchedInplaceParkBegin()
    {
        const pcbjam_sched::ContextId id =
                pcbjam_sched::context_owning_current_stack();

        if (id)
            pcbjam_sched::note_inplace_park(id, +1);

        return id;
    }

    void EMSCRIPTEN_KEEPALIVE wxWasmSchedInplaceParkEnd(unsigned id)
    {
        if (id)
            pcbjam_sched::note_inplace_park(id, -1);
    }

}

// The nested loop's actual park, extracted so it can run either in place or on
// the main stack. The parent owner stays represented while a scoped child
// lease admits the nested loop's permitted work. The loop is a registered
// WAIT, not a second pump: ScheduleExit()/Exit() resolves the exact innermost
// wait and resumes this stack.
struct wxWasmNestedWaitArguments
{
    const void *scope = NULL;
    wxWindow *target = NULL;
    wx_wasm_execution::OwnerToken owner;
    wx_wasm_execution::ScopeToken targetScope;
    int result = 0;
};

// A child modal slice deliberately cannot run the global wxApp::Paint()
// sweep: the opener can be parked halfway through a model mutation. Paint the
// exact quasi-modal target instead, after a browser-task boundary, just as
// wxDialog::ShowModal() does for a true modal dialog.
void wxWasmPaintNestedModal(void *arg)
{
    wxNonOwnedWindow *target = wxDynamicCast(
            static_cast<wxWindow *>(arg), wxNonOwnedWindow);

    if (target && target->IsShown() && target->NeedsPaint())
        target->HandlePaintRequests();
}

void wxWasmDiscardNestedModalPaint(void *)
{
}

void wxWasmNestedWaitBody(void *arg)
{
    wxWasmNestedWaitArguments *arguments =
            static_cast<wxWasmNestedWaitArguments *>(arg);
    const int token = wxWasmBeginWait("nested");

    if (token <= 0)
        return;

    const wx_wasm_execution::LeaseToken executionLease =
            wxWasmExecutionOpenModalLeaseForOwner(
                    arguments->scope, arguments->owner,
                    arguments->targetScope,
                    wx_wasm_execution::ModalWorkMask, token);

    if (!executionLease)
        return;

    // Keyboard ingress is capability-checked against the focused top-level
    // window. ShowQuasiModal() has already shown the target, but it does not
    // move focus from its parent. Establish the child scope before parking so
    // an immediate Escape belongs to this lease instead of being discarded as
    // parent input.
    arguments->target->SetFocus();

    wxWasmMailboxEnqueueAfterScoped(
            &wxWasmPaintNestedModal, arguments->target, 0,
            wx_wasm_execution::WorkClass::ModalLifecycle,
            arguments->targetScope,
            &wxWasmDiscardNestedModalPaint);

    arguments->result = wxWasmYieldUntil(token); // suspends until resolved

    wxWasmExecutionCloseModalLease(arguments->scope, executionLease);
}

}  // namespace

extern "C" void wxWasmSetMainStackRunner(wxWasmMainStackRunner aRunner)
{
    s_mainStackRunner = aRunner;
}

// Top-level main loop: yield to the browser for ONE animation frame, then return. It is
// called in a plain C++ while-loop in DoRun (below), so ProcessEvents() runs on the real
// main C stack and each Asyncify suspension COMPLETES every frame — unlike a permanent
// handleAsync park, which is "in flight" for the app's whole life and makes a tool-
// coroutine fiber swap abort ("cannot stop an async operation in flight"). With this
// per-frame yield the slot is free whenever ProcessEvents runs (docs/features/async/13).
EM_ASYNC_JS(void, wxWasmYieldToBrowser, (), {
    await new Promise(function (resolve) {
        requestAnimationFrame(function () {
            var scheduler = globalThis.__wxScheduler;
            if (!scheduler || typeof scheduler.canTouchNative !== "function" ||
                !scheduler.canTouchNative()) return;
            resolve();
        });
    });
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
// recorder shows all of them at wake-depth 0). ProcessEvents then asks the
// semantic owner coordinator to admit a root or exact modal child slice.
EM_JS(void, wxWasmScheduleProcessEvents, (), {
    var scheduler = globalThis.__wxScheduler;
    if (globalThis.__wxNativeIntegrityUnknown || !scheduler ||
        typeof scheduler.enqueueNativeEntry !== "function" ||
        !scheduler.canTouchNative()) return;
    scheduler.enqueueNativeEntry("dispatch", "top-level wx dispatch", function () {
        if (scheduler.canTouchNative())
            Module["_wxWasmTopLevelTick"]();
    });
});

// Application commands and affiliated completion work must enter from a new
// browser task. In particular, starting a tool fiber from the Embind stack
// which submitted it recreates the root/fiber capture topology this scheduler
// was introduced to remove. Coalesce wakeups; the C++ queue holds the work.
EM_JS(void, wxWasmArmExecutionTick, (), {
    var scheduler = globalThis.__wxScheduler;
    if (globalThis.__wxNativeIntegrityUnknown || !scheduler ||
        typeof scheduler.enqueueNativeEntry !== "function" ||
        !scheduler.canTouchNative()) return;
    // Top-level and owner ticks drive the same dispatch-context drain. One
    // level signal is sufficient even if both producers fire while blocked.
    scheduler.enqueueNativeEntry("dispatch", "execution-owner tick", function () {
        if (scheduler.canTouchNative())
            Module["_wxWasmExecutionTick"]();
    });
});

// Defined below with the dispatch context it drives.
extern "C" void wxWasmDispatchOnContext();

// Scheduler-context dispatch is the permanent runtime. A measured earlier
// attempt ran dispatch contexts while the main loop still used an in-place
// Asyncify park. Four KiCad canvas-tool tests then failed in doRewind because
// a star transfer targeted a tool body with an in-place park. The current
// invariant moves the main loop, dispatch, and token waits onto scheduler
// contexts together. The constant keeps the supported path explicit at the
// call site.
#define wxWASM_STAR_DISPATCH 1

extern "C" {

    // The top-level loop's scheduled dispatch. A separate entry point from
    // ProcessEvents so the JS side has one obvious name to schedule, and so any
    // top-level-only policy has a separate home from the nested pump's direct
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
        // The supported path dispatches on a context above a scheduler-only
        // main stack. Running context dispatch above an in-place-parked main
        // loop caused the measured overlapped wake.
#if wxWASM_STAR_DISPATCH
        wxWasmDispatchOnContext();
#else
        ProcessEvents();
#endif
    }

}  // extern "C"

// ----------------------------------------------------------------------------
// The dispatch contexts form an idle-reuse set bounded by
// nesting depth, not by tick rate.
//
// Every wx handler chain runs here instead of on the main stack, which is what
// lets a wait inside a handler park its context instead of suspending the stack
// the whole runtime stands on.
//
// WHY NOT ONE (measured 2026-08-07, races nested_quasi_modal_pump_error):
// a nested quasi-modal loop is a WAIT that only some LATER dispatch can
// resolve — the pending event that closes the dialog, or the throwing handler
// whose error path releases it. With a single context, the loop's own park
// consumes the only dispatcher, so nothing ever dispatches that event and the
// wait is unresolvable: the app wedges. "A blocked dispatch no longer blocks
// the runtime" only holds if something else can dispatch.
//
// WHY THIS IS NOT THE OLD PER-TICK POOL (doc 20 §10 — 8 contexts burned in
// 30 ms): that design took a FRESH context per tick while one sat suspended,
// so the count grew with the
// tick rate — a leak wearing a cap. Here a tick REUSES any context parked at
// "dispatch-idle" and only creates one when every existing context is parked
// deeper (i.e. inside a wait). A context finishes its ProcessEvents and
// returns to idle as soon as its wait resolves, so the live count is bounded
// by actual modal-nesting depth (1 in steady state, 2-3 under nested dialogs).
//
// ORDERING, load-bearing: this must exist before libcontext adopts its root,
// so the scheduler owns the main stack alone and the root adopts the RUNNING
// context instead. Two emscripten_fiber_t describing the main stack corrupt
// each other on first entry (doc 22 §5, the one-root constraint).
// ----------------------------------------------------------------------------
namespace
{
// Deeper than the scheduler's 128K default: a wx dispatch chain reaches deep
// into KiCad (commit -> connectivity -> font work) before anything parks.
constexpr size_t DISPATCH_STACK_BYTES = 1024 * 1024;
constexpr size_t DISPATCH_ASYNCIFY_BYTES = 512 * 1024;

// Nesting deeper than this is a bug, not a UI. The arriving tick may be the
// only completion edge for admitted work, so exhausting the fixed set is a
// terminal invariant failure rather than an invitation to drop that edge.
constexpr size_t MAX_DISPATCH_CONTEXTS = 16;

std::vector<pcbjam_sched::ContextId> &wxWasmDispatchContexts()
{
    static std::vector<pcbjam_sched::ContextId> s_contexts;
    return s_contexts;
}

// Work handed to a dispatch context by an entry that arrived on the MAIN
// stack — a DOM event handler, a mailbox timer delivery. See
// the scoped adapter below for why those may not run where they land.
struct wxWasmDispatchJob
{
    void (*fn)(void *);
    void *arg;
    wx_wasm_execution::WorkClass workClass;
    wx_wasm_execution::ScopeToken targetScope;
    // Exact lease generation present when scoped child-capable work entered.
    // A target scope can be reused by a later modal, but this capability
    // cannot: stale work is discarded instead of borrowing the later lease.
    wx_wasm_execution::LeaseToken leaseProvenance;
    // Non-zero for a control continuation which belongs to an owner that the
    // producer already retained. Such a continuation can make progress while
    // unrelated ordinary admission is closed, but it cannot create an owner.
    wx_wasm_execution::OwnerToken affiliatedOwner;
    // Non-zero only for an external browser receipt. This sequence is minted
    // before JavaScript decides whether to defer the staging call, so a later
    // generated canvas callback can be inserted behind the older receipt even
    // when it entered native code first.
    std::uint32_t ingressSequence = 0;
    std::uint64_t sequence = 0;
    wx_wasm_execution::DiscardCallback discard = nullptr;
    wx_wasm_execution::CoalesceClass coalesce =
            wx_wasm_execution::CoalesceClass::None;
    std::uintptr_t coalesceKey = 0;
    // Non-null only while the synchronous submitter's stack is live. Taking,
    // detaching, or discarding this envelope clears the pointer before any
    // callback can re-enter queue cleanup.
    wx_wasm_execution::DispatchSubmissionHandshake *submission = nullptr;
    // Explicit memory lease for copied payload storage owned by arg. This
    // remains charged after the record is taken, until callback return.
    std::size_t retainedBytes = 0;
};

std::vector<wxWasmDispatchJob> &wxWasmDispatchJobs()
{
    static std::vector<wxWasmDispatchJob> s_jobs;
    return s_jobs;
}

std::uint64_t &wxWasmNextDispatchSequence()
{
    static std::uint64_t s_sequence = 0;
    return s_sequence;
}

wx_wasm_execution::QueueCounters& wxWasmDispatchQueueCounters()
{
    static wx_wasm_execution::QueueCounters s_counters;
    return s_counters;
}

wx_wasm_execution::RetainedByteBudget& wxWasmDispatchRetainedByteBudget()
{
    static wx_wasm_execution::RetainedByteBudget s_budget;
    return s_budget;
}

void wxWasmPrintDispatchQueueCounters(const char *event)
{
    const wx_wasm_execution::QueueCounters& counters =
            wxWasmDispatchQueueCounters();
    const auto& jobs = wxWasmDispatchJobs();
    size_t affiliated = 0;

    for (const wxWasmDispatchJob& job : jobs)
    {
        if (job.affiliatedOwner)
            ++affiliated;
    }

    const wx_wasm_execution::RetainedByteBudget& byteBudget =
            wxWasmDispatchRetainedByteBudget();

    printf("[wx-owner] queue %s depth=%zu affiliated=%zu high-water=%zu "
           "coalesced=%llu rejected=%llu limit=%zu retained-bytes=%zu "
           "byte-high-water=%zu byte-rejected=%llu byte-limit=%zu\n",
           event, jobs.size(), affiliated, counters.highWater,
           static_cast<unsigned long long>(counters.coalesced),
           static_cast<unsigned long long>(counters.rejected),
           wx_wasm_execution::MaxQueuedJobs, byteBudget.Retained(),
           byteBudget.HighWater(),
           static_cast<unsigned long long>(byteBudget.Rejections()),
           wx_wasm_execution::MaxQueuedRetainedBytes);
}

bool wxWasmReleaseDispatchJobBytes(wxWasmDispatchJob& job)
{
    const std::size_t bytes = job.retainedBytes;
    job.retainedBytes = 0;

    if (wxWasmDispatchRetainedByteBudget().Release(bytes))
        return true;

    wxWasmExecutionFailStop(
            "execution envelope retained-byte accounting underflowed");
    return false;
}

class wxWasmDispatchJobByteLease
{
public:
    explicit wxWasmDispatchJobByteLease(wxWasmDispatchJob& job) : m_job(job) {}
    ~wxWasmDispatchJobByteLease() { wxWasmReleaseDispatchJobBytes(m_job); }

private:
    wxWasmDispatchJob& m_job;
};

void wxWasmMarkDispatchJobLive(wxWasmDispatchJob& job)
{
    wx_wasm_execution::DispatchSubmissionHandshake *submission =
            job.submission;
    job.submission = nullptr;

    if (submission && !submission->MarkPayloadLive())
        wxWasmExecutionFailStop(
                "execution envelope completed its submit handshake twice");
}

void wxWasmDiscardDispatchJob(wxWasmDispatchJob& job)
{
    // Detach every ownership edge before user cleanup. A destructor can enter
    // fail-stop recursively; neither the stack witness nor the callback may be
    // observed a second time from that path.
    wx_wasm_execution::DispatchSubmissionHandshake *submission =
            job.submission;
    wx_wasm_execution::DiscardCallback discard = job.discard;
    void *arg = job.arg;
    job.submission = nullptr;
    job.discard = nullptr;
    job.arg = nullptr;

    // Release capacity before user cleanup. A destructor can enqueue another
    // payload, and that new admission must observe the detached queue state.
    wxWasmReleaseDispatchJobBytes(job);

    if (submission && !submission->MarkPayloadDiscarded())
        wxWasmExecutionFailStop(
                "execution envelope discarded its submit handshake twice");

    if (discard)
        discard(arg);
}

wx_wasm_execution::DispatchSubmitDisposition
wxWasmDetachDispatchSubmission(
        wx_wasm_execution::DispatchSubmissionHandshake& submission)
{
    if (submission.IsAttached())
    {
        for (wxWasmDispatchJob& job : wxWasmDispatchJobs())
        {
            if (job.submission != &submission)
                continue;

            // The record remains queued after this call returns. Remove its
            // pointer to the submitter's stack before reporting a live payload.
            job.submission = nullptr;
            submission.MarkPayloadLive();
            break;
        }
    }

    if (submission.IsAttached())
    {
        // A retained envelope can leave the queue only through take/discard,
        // and both paths close the witness first. Treat disagreement as
        // terminal and conservatively forbid the caller from touching arg.
        wxWasmExecutionFailStop(
                "execution envelope lost its submit ownership handshake");
        submission.MarkPayloadDiscarded();
    }

    return submission.Disposition();
}

bool wxWasmEnqueueDispatchJob(wxWasmDispatchJob job, bool armTick = true)
{
    auto& jobs = wxWasmDispatchJobs();
    wx_wasm_execution::QueueCounters& counters =
            wxWasmDispatchQueueCounters();

    if (!wx_wasm_execution::IsValidWorkClass(job.workClass))
    {
        counters.NoteRejected();
        wxWasmPrintDispatchQueueCounters("invalid-work-class");
        wxWasmExecutionFailStop("invalid execution envelope work class");
        return false;
    }

    if (job.leaseProvenance
        && (job.workClass == wx_wasm_execution::WorkClass::Ordinary
            || !job.targetScope
            || job.leaseProvenance.targetScope != job.targetScope
            || job.affiliatedOwner
            || !job.discard))
    {
        counters.NoteRejected();
        wxWasmPrintDispatchQueueCounters("invalid-lease-provenance");
        wxWasmExecutionFailStop(
                "invalid execution envelope lease provenance");
        return false;
    }

    switch (job.coalesce)
    {
        case wx_wasm_execution::CoalesceClass::None:
        case wx_wasm_execution::CoalesceClass::PassiveMouseMove:
        case wx_wasm_execution::CoalesceClass::LatestGeometry:
            break;

        default:
            counters.NoteRejected();
            wxWasmPrintDispatchQueueCounters("invalid-coalescer");
            wxWasmExecutionFailStop(
                    "invalid execution envelope coalescing class");
            return false;
    }

    // Replacement would need to atomically transfer two independently sized
    // leases while its discard callback is allowed to re-enter this queue.
    // Large service payloads are exact ordering records, never lossy state,
    // so keep the two contracts disjoint.
    if (job.retainedBytes != 0
        && job.coalesce != wx_wasm_execution::CoalesceClass::None)
    {
        counters.NoteRejected();
        wxWasmPrintDispatchQueueCounters("invalid-byte-coalescer");
        wxWasmExecutionFailStop(
                "retained execution envelope cannot be coalesced");
        return false;
    }

    if (!job.affiliatedOwner
        && job.coalesce != wx_wasm_execution::CoalesceClass::None)
    {
        if (!job.discard)
        {
            counters.NoteRejected();
            wxWasmPrintDispatchQueueCounters("invalid-coalescer");
            wxWasmExecutionFailStop(
                    "coalescible execution envelope has no discard callback");
            return false;
        }

        if (!jobs.empty())
        {
            wxWasmDispatchJob& previous = jobs.back();
            if (previous.leaseProvenance == job.leaseProvenance
                && ((previous.ingressSequence == 0
                     && job.ingressSequence == 0)
                    || (previous.ingressSequence != 0
                        && job.ingressSequence != 0
                        && previous.ingressSequence + 1
                                == job.ingressSequence))
                && wx_wasm_execution::CanCoalesceAdjacent(
                    previous.coalesce, previous.workClass,
                    previous.targetScope,
                    static_cast<bool>(previous.affiliatedOwner),
                    previous.fn == job.fn,
                    previous.coalesceKey == job.coalesceKey,
                    previous.sequence + 1 == job.sequence,
                    job.coalesce, job.workClass, job.targetScope))
            {
                // Transfer the replacement first. If discard re-enters a
                // native fail-stop, the old record is no longer discoverable
                // and therefore cannot be discarded twice.
                wxWasmDispatchJob discarded = previous;
                previous = job;
                counters.NoteCoalesced();
                wxWasmDiscardDispatchJob(discarded);
                if (wxWasmExecutionCoordinator().Failed())
                    return true;
                if ((counters.coalesced & (counters.coalesced - 1)) == 0)
                    wxWasmPrintDispatchQueueCounters("coalesced");
                if (armTick)
                    wxWasmArmExecutionTick();
                return true;
            }
        }
    }

    if (!wx_wasm_execution::QueueHasCapacity(jobs.size()))
    {
        counters.NoteRejected();
        wxWasmPrintDispatchQueueCounters("overflow");
        wxWasmExecutionFailStop(
                job.affiliatedOwner
                    ? "affiliated execution tail exceeded queue capacity"
                    : "execution envelope queue exceeded capacity");
        return false;
    }

    wx_wasm_execution::RetainedByteBudget& byteBudget =
            wxWasmDispatchRetainedByteBudget();
    if (!byteBudget.TryRetain(job.retainedBytes))
    {
        counters.NoteRejected();
        wxWasmPrintDispatchQueueCounters("byte-overflow");
        wxWasmExecutionFailStop(
                "execution envelope retained-byte capacity exceeded");
        return false;
    }

    // Admission above is a private reservation until vector publication
    // succeeds. Every pre-publication exit rolls it back and leaves arg with
    // the caller; after publication, only take/discard owns the release.
    const auto rollbackIncomingBytes = [&job, &byteBudget]() {
        const std::size_t bytes = job.retainedBytes;
        job.retainedBytes = 0;
        if (!byteBudget.Release(bytes))
        {
            wxWasmExecutionFailStop(
                    "execution envelope byte-admission rollback underflowed");
        }
    };

    try
    {
        if (job.ingressSequence != 0)
        {
            auto insertion = jobs.end();
            for (auto it = jobs.begin(); it != jobs.end(); ++it)
            {
                if (it->ingressSequence == job.ingressSequence)
                {
                    counters.NoteRejected();
                    rollbackIncomingBytes();
                    wxWasmExecutionFailStop(
                            "browser ingress sequence was staged twice");
                    return false;
                }

                if (it->ingressSequence != 0
                    && it->ingressSequence > job.ingressSequence)
                {
                    insertion = it;
                    break;
                }
            }

            jobs.insert(insertion, job);
        }
        else
        {
            jobs.push_back(job);
        }
    }
    catch (...)
    {
        // The incoming record was never published, so its synchronous
        // submission witness remains Attached and false leaves the payload
        // with that caller.  Fail-stop owns and discards only records already
        // in jobs; it cannot double-discard this one.
        counters.NoteRejected();
        rollbackIncomingBytes();
        wxWasmPrintDispatchQueueCounters("allocation-failure");
        wxWasmExecutionFailStop(
                "execution envelope queue allocation failed");
        return false;
    }

    const size_t depth = jobs.size();
    const size_t oldHighWater = counters.highWater;
    counters.NoteEnqueued(depth);
    if (counters.highWater != oldHighWater
        && ((depth & (depth - 1)) == 0
            || depth == wx_wasm_execution::MaxQueuedJobs))
    {
        wxWasmPrintDispatchQueueCounters("high-water");
    }
    if (armTick)
        wxWasmArmExecutionTick();
    return true;
}

wxWasmDispatchJob wxWasmTakeDispatchJob(
        std::vector<wxWasmDispatchJob>& jobs, size_t index)
{
    wxWasmDispatchJob job = jobs[index];
    jobs.erase(jobs.begin() + index);
    return job;
}

void wxWasmDiscardQueuedJobs()
{
    // Detach the complete queue before calling user destructors. A discard
    // callback can re-enter fail-stop or attempt to enqueue; the failed latch
    // rejects that work and no record remains discoverable for a second call.
    std::vector<wxWasmDispatchJob> abandoned;
    abandoned.swap(wxWasmDispatchJobs());

    for (size_t i = abandoned.size(); i-- > 0;)
    {
        wxWasmDiscardDispatchJob(abandoned[i]);
    }
}

bool wxWasmReleaseExecutionOwner(
        const wx_wasm_execution::OwnerToken& owner)
{
    wx_wasm_execution::Coordinator& coordinator =
            wxWasmExecutionCoordinator();
    const wx_wasm_execution::OwnerToken before =
            coordinator.CurrentBranchOwner();
    const bool released = coordinator.Release(owner);
    const wx_wasm_execution::OwnerToken after =
            coordinator.CurrentBranchOwner();

    if (released)
    {
        wxWasmCompleteRetiredEmbindOwners();
        wxWasmTryCompleteActiveModalLease();
    }

    // Wake on eligibility transitions, not on a retry timer. A tick can find
    // a child busy (or a root parked) and leave work queued. Releasing that
    // child or the final root must request one fresh admission task.
    if (released && before != after && !wxWasmDispatchJobs().empty())
    {
        wxWasmArmExecutionTick();
    }

    if (!released)
        wxWasmExecutionFailStop("execution owner refused terminal release");

    return released;
}

bool wxWasmStageExecutionJob(
        void (*fn)(void *), void *arg,
        wx_wasm_execution::WorkClass workClass,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::LeaseToken leaseProvenance,
        std::uint32_t ingressSequence,
        wx_wasm_execution::DiscardCallback discard,
        wx_wasm_execution::CoalesceClass coalesce,
        std::uintptr_t coalesceKey,
        std::size_t retainedBytes)
{
    if (!fn || wxWasmExecutionCoordinator().Failed())
        return false;

    return wxWasmEnqueueDispatchJob(
            {fn, arg, workClass, targetScope, leaseProvenance, {},
             ingressSequence,
             ++wxWasmNextDispatchSequence(), discard, coalesce,
             coalesceKey, nullptr, retainedBytes});
}

void wxWasmRunQueuedJobs()
{
    if (wxWasmExecutionCoordinator().Failed())
        return;

    auto &jobs = wxWasmDispatchJobs();
    // A QueueOrdinary/QueueAffiliated call made by a running job promises a
    // new browser-task boundary. Keep jobs appended during this drain behind
    // the sequence snapshot even if the current callback releases the owner
    // and would otherwise make them immediately eligible.
    const std::uint64_t sequenceLimit = wxWasmNextDispatchSequence();

    for (;;)
    {
        bool ran = false;
        const std::uint32_t earliestUnstagedReceipt =
                wxWasmEarliestUnstagedBrowserIngressReceiptJs();

        // An ordinary command at the head stays queued while an owner is
        // parked, but it must not hide later input that an active child lease
        // explicitly permits. Scan for the first eligible item; relative
        // order within each deferred class is preserved.
        for (size_t i = 0; i < jobs.size(); ++i)
        {
            if (jobs[i].sequence > sequenceLimit)
                continue;

            if (!wx_wasm_execution::BrowserIngressCanRun(
                    jobs[i].ingressSequence,
                    earliestUnstagedReceipt))
            {
                continue;
            }

            if (jobs[i].affiliatedOwner)
            {
                const wx_wasm_execution::OwnerToken owner =
                        jobs[i].affiliatedOwner;

                if (!wxWasmExecutionCoordinator().Owns(owner))
                {
                    // Leave the record in the queue. Fail-stop detaches the
                    // whole queue first and then invokes its optional discard
                    // tail exactly once.
                    wxWasmExecutionFailStop(
                            "retained affiliated job lost its execution owner");
                    return;
                }

                if (!wxWasmExecutionCoordinator().CanRunAffiliated(owner))
                    continue;

                wxWasmDispatchJob job =
                        wxWasmTakeDispatchJob(jobs, i);

                // The producer transferred an existing reference to this
                // callback. This scope supplies exact stack provenance only;
                // the callback owns the matching terminal Release().
                wxWasmExecutionScope ownerScope(owner, false);
                if (!ownerScope)
                {
                    wxWasmDiscardDispatchJob(job);
                    wxWasmExecutionFailStop(
                            "affiliated stack-owner mapping allocation failed");
                    return;
                }

                // Callback execution, rather than removal from the vector, is
                // the ownership edge: a stale lease removes and discards.
                wxWasmMarkDispatchJobLive(job);
                wxWasmDispatchJobByteLease byteLease(job);
                job.fn(job.arg);
                ran = true;
                break;
            }

            wx_wasm_execution::Coordinator& coordinator =
                    wxWasmExecutionCoordinator();

            if (coordinator.IngressReceiptIsStale(
                    jobs[i].leaseProvenance))
            {
                wxWasmDispatchJob job =
                        wxWasmTakeDispatchJob(jobs, i);

                // Lease-bound envelopes are required to own a discard tail.
                // Remove the record before invoking it because destruction can
                // enqueue more work or enter the terminal fail-stop path.
                wxWasmDiscardDispatchJob(job);
                ran = true;
                break;
            }

            const wx_wasm_execution::Admission admission =
                    coordinator.Admit(
                            jobs[i].workClass, jobs[i].targetScope);

            if (!admission)
                continue;

            wxWasmDispatchJob job = wxWasmTakeDispatchJob(jobs, i);

            wxWasmExecutionScope ownerScope(admission);
            if (!ownerScope)
            {
                wxWasmDiscardDispatchJob(job);
                wxWasmExecutionFailStop(
                        "execution stack-owner mapping allocation failed");
                return;
            }

            wxWasmMarkDispatchJobLive(job);
            wxWasmDispatchJobByteLease byteLease(job);
            job.fn(job.arg);
            ran = true;
            break;
        }

        if (!ran)
            return;
    }
}

// Continue only input/pending-event envelopes which belong to the transaction
// already mapped to this stack. This is the native nested-pump rule: a root
// action can sleep, receive a mouse-up while parked, and consume that mouse-up
// from its next wxYield() without admitting a second root owner. Ordinary
// service/Embind work remains queued until the transaction really ends.
void wxWasmRunNestedOwnerJobs(
        const wx_wasm_execution::OwnerToken& owner)
{
    wx_wasm_execution::Coordinator& coordinator =
            wxWasmExecutionCoordinator();

    if (coordinator.Failed() || !owner
        || wxWasmInternalOwnerForCurrentStack() != owner
        || !coordinator.Owns(owner))
    {
        return;
    }

    auto& jobs = wxWasmDispatchJobs();
    const std::uint64_t sequenceLimit = wxWasmNextDispatchSequence();
    size_t budget = jobs.size();

    // The entry snapshot is a hard bound even if a handler stages more input.
    // Each taken or discarded record consumes one unit; records appended during
    // this continuation have a later sequence and wait for another boundary.
    while (budget > 0)
    {
        bool progressed = false;
        const std::uint32_t earliestUnstagedReceipt =
                wxWasmEarliestUnstagedBrowserIngressReceiptJs();

        for (size_t i = 0; i < jobs.size(); ++i)
        {
            wxWasmDispatchJob& candidate = jobs[i];

            if (candidate.sequence > sequenceLimit
                || candidate.affiliatedOwner
                || !wx_wasm_execution::BrowserIngressCanRun(
                        candidate.ingressSequence,
                        earliestUnstagedReceipt)
                || (candidate.workClass
                            != wx_wasm_execution::WorkClass::UserInput
                    && candidate.workClass
                            != wx_wasm_execution::WorkClass::PendingEvents))
            {
                continue;
            }

            if (coordinator.IngressReceiptIsStale(
                    candidate.leaseProvenance))
            {
                wxWasmDispatchJob job = wxWasmTakeDispatchJob(jobs, i);
                wxWasmDiscardDispatchJob(job);
                --budget;
                progressed = true;
                break;
            }

            if (!coordinator.CanRunNestedIngress(
                    owner, candidate.workClass, candidate.targetScope,
                    candidate.leaseProvenance))
            {
                continue;
            }

            wxWasmDispatchJob job = wxWasmTakeDispatchJob(jobs, i);
            wxWasmMarkDispatchJobLive(job);
            wxWasmDispatchJobByteLease byteLease(job);
            job.fn(job.arg);
            --budget;
            progressed = true;

            if (coordinator.Failed())
                return;

            break;
        }

        if (!progressed)
            return;
    }
}

void wxWasmDispatchEntry(void *)
{
    // Never returns: an emscripten fiber entry that returns ends the program.
    for (;;)
    {
        // Handed-off entries first: they are the reason this context exists
        // for anyone but the tick, and they are already ordered.
        wxWasmRunQueuedJobs();
        ProcessEvents();

        // One tick done. Park until the next kick rather than spinning; the
        // scheduler resumes us from a fresh JS task. Parking HERE is what
        // returns this context to the reusable set.
        wxWasmParkExternalOrAbort("dispatch-idle");
    }
}

/** Is this context ready to take a tick (never entered, or idle)? */
bool wxWasmDispatchAvailable(pcbjam_sched::ContextId id)
{
    const pcbjam_sched::Status st = pcbjam_sched::status_of(id);

    if (st == pcbjam_sched::Status::Fresh)
        return true;

    return st == pcbjam_sched::Status::Parked
           && strcmp(pcbjam_sched::park_reason_of(id), "dispatch-idle") == 0;
}
}  // namespace

bool wxWasmExecutionStageIngress(
        void (*fn)(void *), void *arg,
        wx_wasm_execution::WorkClass cls,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::DiscardCallback discard,
        wx_wasm_execution::CoalesceClass coalesce,
        std::uintptr_t coalesceKey)
{
    // JavaScript proves Asyncify is Normal and no Emscripten trampoline is in
    // flight, without yielding before this call. Do not use the scheduler's
    // transition flag as a physical-entry guard here: it deliberately stays
    // set for the full logical libcontext transition, including while that
    // context is unwound inside emscripten_sleep() and the browser is free to
    // deliver a new task. This strict leaf only transfers an owned record to
    // the queue; it never admits, switches contexts, or suspends.
    if (!fn || wxWasmExecutionCoordinator().Failed())
        return false;

    const wx_wasm_execution::LeaseToken leaseProvenance =
            wx_wasm_execution::LeaseProvenanceForIngress(
                    cls, targetScope,
                    wxWasmExecutionCoordinator().ActiveLease());

    // wxWasmStageExecutionJob only appends the owned record and calls the
    // non-suspending wxWasmArmExecutionTick() import. Admission and every
    // context transfer happen later, through the existing physical arbiter.
    return wxWasmStageExecutionJob(
            fn, arg, cls, targetScope, leaseProvenance, 0, discard,
            coalesce, coalesceKey);
}

namespace
{
bool wxWasmPrepareBrowserIngress(
        wx_wasm_execution::BrowserIngressReceipt receipt,
        wx_wasm_execution::WorkClass& cls,
        wx_wasm_execution::ScopeToken& targetScope,
        wx_wasm_execution::LeaseToken& leaseProvenance)
{
    leaseProvenance = {};

    // A caller without the receipt registry cannot prove which modal lifetime
    // existed at browser receipt time. Keep the payload, but make it Ordinary
    // root work with no child capability.
    if (!receipt || !receipt.snapshotAvailable)
    {
        cls = wx_wasm_execution::WorkClass::Ordinary;
        targetScope = {};
        return true;
    }

    if (!receipt.lease)
    {
        if (targetScope)
        {
            wxWasmExecutionFailStop(
                    "unleased browser receipt supplied a scoped target");
            return false;
        }

        return true;
    }

    // Some external callbacks (global resize/focus and completed file-drop
    // batches) deliberately choose no modal affiliation. An empty requested
    // scope preserves that conservative policy even when the snapshot names
    // a live lease. A scoped producer must match the published token exactly.
    if (!targetScope)
        return true;

    if (cls == wx_wasm_execution::WorkClass::Ordinary
        || targetScope != receipt.lease.targetScope)
    {
        wxWasmExecutionFailStop(
                "browser ingress scope did not match its receipt lease");
        return false;
    }

    leaseProvenance = receipt.lease;
    return true;
}
} // namespace

bool wxWasmExecutionStageBrowserIngress(
        void (*fn)(void *), void *arg,
        wx_wasm_execution::WorkClass cls,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::BrowserIngressReceipt receipt,
        wx_wasm_execution::DiscardCallback discard,
        wx_wasm_execution::CoalesceClass coalesce,
        std::uintptr_t coalesceKey)
{
    if (!fn || wxWasmExecutionCoordinator().Failed())
        return false;

    wx_wasm_execution::LeaseToken leaseProvenance;
    if (!wxWasmPrepareBrowserIngress(
            receipt, cls, targetScope, leaseProvenance))
    {
        return false;
    }

    return wxWasmStageExecutionJob(
            fn, arg, cls, targetScope, leaseProvenance,
            receipt.sequence, discard, coalesce, coalesceKey);
}

extern "C" void wxWasmDispatchOnContext()
{
    if (!wxTheApp || wxWasmExecutionCoordinator().Failed())
        return;

    // A context can be physically parked inside handleSleep while its
    // registry record still says Running. The JavaScript arbiter and the
    // synchronous DOM path both prove this predicate before they call us.
    // Seeing a busy scheduler here means an admitted signal was consumed
    // through an unaudited path; continuing or silently dropping it would
    // leave no reliable completion edge.
    if (wxWasmSchedulerEntryBusy())
    {
        wxWasmExecutionFailStop(
                "dispatch entered while scheduler was busy");
        return;
    }

    auto &contexts = wxWasmDispatchContexts();

    // Drop contexts poisoned by abandon_transition (a handler died abnormally
    // and left a half-unwound stack): they are Finished and must never be
    // entered again, and keeping them would count against the ceiling.
    for (size_t i = contexts.size(); i-- > 0;)
    {
        if (pcbjam_sched::status_of(contexts[i])
            != pcbjam_sched::Status::Finished)
        {
            continue;
        }

        // The registry owns the Context allocation, Asyncify buffer, and the
        // generated-JS suspension guards. Forgetting the id here would only
        // hide that ownership and leak all of it. Erase our reference only
        // after the scheduler confirms that it reclaimed the Finished fiber.
        if (!pcbjam_sched::fiber_release(contexts[i]))
        {
            wxWasmExecutionFailStop(
                    "finished dispatch context refused release");
            return;
        }

        contexts.erase(contexts.begin() + i);
    }

    // Reuse an idle context if there is one; only allocate when every context
    // we own is parked deeper (inside a wait), which is exactly the nested
    // case that needs an additional dispatcher.
    for (pcbjam_sched::ContextId id : contexts)
    {
        if (!wxWasmDispatchAvailable(id))
            continue;

        const bool activated =
                pcbjam_sched::status_of(id) == pcbjam_sched::Status::Fresh
                    ? pcbjam_sched::fiber_start(id, 0)
                    : pcbjam_sched::mark_ready(id, 0);

        if (!activated)
        {
            wxWasmExecutionFailStop(
                    "dispatch context refused an admitted tick");
            return;
        }

        wxWasmDrainSchedulerBatch();
        return;
    }

    if (contexts.size() >= MAX_DISPATCH_CONTEXTS)
    {
        printf("[wx-dispatch] %zu dispatch contexts all parked in waits - "
               "failing the scheduler (nesting runaway)\n", contexts.size());
        // This tick is the only edge for already-admitted work. Dropping it
        // would leave that work queued forever with no deterministic retry.
        wxWasmExecutionFailStop(
                "dispatch context nesting exceeded the fixed capacity");
        return;
    }

    const pcbjam_sched::ContextId fresh = pcbjam_sched::fiber_create(
        wxWasmDispatchEntry, NULL, NULL, DISPATCH_STACK_BYTES,
        DISPATCH_ASYNCIFY_BYTES, "wx-dispatch");

    if (!fresh)
    {
        // An owner-safe event cannot fall back to the shared main stack. The
        // context budget/allocation failure is terminal for this instance.
        wxWasmExecutionFailStop(
                "could not allocate an owner-safe dispatch context");
        return;
    }

    if (!pcbjam_sched::fiber_start(fresh, 0))
    {
        wxWasmExecutionFailStop(
                "new dispatch context refused its initial tick");
        return;
    }

    contexts.push_back(fresh);
    wxWasmDrainSchedulerBatch();
}

extern "C" bool wxWasmOnDispatchContext()
{
    const pcbjam_sched::ContextId cur = pcbjam_sched::current();

    if (!cur)
        return false;

    for (pcbjam_sched::ContextId id : wxWasmDispatchContexts())
    {
        if (id == cur)
            return true;
    }

    return false;
}

bool wxWasmExecutionOnOwnedDispatchContext()
{
    return wxWasmOnDispatchContext()
           && static_cast<bool>(wxWasmMappedOwnerForCurrentStack());
}

wx_wasm_execution::ScopeToken wxWasmExecutionScopeForWindow(
        const wxWindow *window)
{
    if (!window)
        return {};

    wxWindow *topLevel = wxGetTopLevelParent(
            const_cast<wxWindow *>(window));
    wxTopLevelWindow *topLevelWindow = wxDynamicCast(
            topLevel, wxTopLevelWindow);

    if (!topLevelWindow || topLevelWindow->GetCSSId() == wxID_NONE)
        return {};

    // wx.js allocates CSS IDs monotonically for the lifetime of this module.
    // Pairing it with the address prevents allocator reuse from reviving a
    // queued scope after a top-level window was destroyed and recreated.
    const std::uint32_t generation =
            static_cast<std::uint32_t>(topLevelWindow->GetCSSId()) + 1u;
    return wx_wasm_execution::ScopeFromPointer(topLevelWindow, generation);
}

wx_wasm_execution::ScopeToken wxWasmExecutionActiveLeaseScope()
{
    return wxWasmExecutionCoordinator().ActiveLeaseScope();
}

wx_wasm_execution::BrowserIngressReceipt
wxWasmExecutionTakeBrowserIngressReceipt(unsigned token)
{
    wx_wasm_execution::BrowserIngressReceipt receipt;
    if (token == 0)
        return receipt;

    unsigned sequence = 0;
    unsigned flags = 0;
    std::uintptr_t targetScopeValue = 0;
    unsigned targetGeneration = 0;
    unsigned leaseIdLow = 0;
    unsigned leaseIdHigh = 0;
    unsigned leaseParentLow = 0;
    unsigned leaseParentHigh = 0;
    unsigned leaseGenerationLow = 0;
    unsigned leaseGenerationHigh = 0;

    if (!wxWasmTakeBrowserIngressReceiptJs(
            token, &sequence, &flags, &targetScopeValue,
            &targetGeneration, &leaseIdLow, &leaseIdHigh,
            &leaseParentLow, &leaseParentHigh,
            &leaseGenerationLow, &leaseGenerationHigh))
    {
        wxWasmExecutionFailStop(
                "browser ingress lost its exact receipt token");
        return receipt;
    }

    receipt.sequence = sequence;
    receipt.snapshotAvailable = (flags & 1u) != 0;
    receipt.deferredBehindEarlier = (flags & 4u) != 0;
    const bool hasLease = (flags & 2u) != 0;
    const bool unknownFlags = (flags & ~7u) != 0;

    receipt.lease.id.value =
            static_cast<std::uint64_t>(leaseIdLow)
            | (static_cast<std::uint64_t>(leaseIdHigh) << 32);
    receipt.lease.parent.value =
            static_cast<std::uint64_t>(leaseParentLow)
            | (static_cast<std::uint64_t>(leaseParentHigh) << 32);
    receipt.lease.generation =
            static_cast<std::uint64_t>(leaseGenerationLow)
            | (static_cast<std::uint64_t>(leaseGenerationHigh) << 32);
    receipt.lease.targetScope.value = targetScopeValue;
    receipt.lease.targetScope.generation = targetGeneration;

    const bool anyLeaseField = receipt.lease.id.value != 0
                               || receipt.lease.parent.value != 0
                               || receipt.lease.generation != 0
                               || receipt.lease.targetScope.value != 0
                               || receipt.lease.targetScope.generation != 0;
    const bool structurallyValid = sequence == token && !unknownFlags
            && (receipt.snapshotAvailable
                    ? (hasLease
                            ? static_cast<bool>(receipt.lease)
                              && static_cast<bool>(receipt.lease.parent)
                            : !anyLeaseField)
                    : (!hasLease && !anyLeaseField));

    if (!structurallyValid)
    {
        wxWasmExecutionFailStop(
                "browser ingress receipt failed native validation");
        return {};
    }

    return receipt;
}

wx_wasm_execution::BrowserIngressReceipt
wxWasmExecutionCaptureBrowserIngressReceipt()
{
    const unsigned token = wxWasmCaptureBrowserIngressReceiptJs();
    if (token == 0)
    {
        // Generated Emscripten callbacks have no JavaScript adapter which can
        // reject the event before this point.  Do not let a missing receipt
        // fall through to the legacy ambient-lease path: after fail-stop the
        // scoped submitter rejects the payload without touching wx state.
        wxWasmExecutionFailStop(
                "generated browser callback could not capture a receipt");
        return {};
    }

    return wxWasmExecutionTakeBrowserIngressReceipt(token);
}

bool wxWasmExecutionAssociatePendingEventHandler(
        const void *handler, const wxWindow *ownerWindow)
{
    if (!emscripten_is_main_runtime_thread() || !handler)
        return false;

    const wx_wasm_execution::ScopeToken scope =
            wxWasmExecutionScopeForWindow(ownerWindow);

    if (!scope)
        return false;

    auto& scopes = wxWasmPendingEventHandlerScopes();
    const auto it = scopes.find(handler);

    if (it != scopes.end())
        return it->second == scope;

    try
    {
        return scopes.emplace(handler, scope).second;
    }
    catch (...)
    {
        wxWasmExecutionFailStop(
                "could not retain a pending-event handler scope");
        return false;
    }
}

void wxWasmExecutionForgetPendingEventHandler(const void *handler)
{
    if (!emscripten_is_main_runtime_thread() || !handler)
        return;

    wxWasmPendingEventHandlerScopes().erase(handler);
}

wx_wasm_execution::PendingEventDelegate
wxWasmExecutionCapturePendingEventDelegate(const wxWindow *ownerWindow)
{
    if (!emscripten_is_main_runtime_thread() || !ownerWindow)
        return {};

    const wx_wasm_execution::ScopeToken scope =
            wxWasmExecutionScopeForWindow(ownerWindow);
    const wx_wasm_execution::OwnerToken owner =
            wxWasmInternalOwnerForCurrentStack();
    wx_wasm_execution::Coordinator& coordinator =
            wxWasmExecutionCoordinator();
    const wx_wasm_execution::LeaseToken lease = coordinator.ActiveLease();

    // This is capability capture, not ambient modal discovery.  Require the
    // caller to be the exact live child which could process a pending event
    // for this same top-level family now.
    if (!scope || !owner || !lease
        || !coordinator.CanRunNestedIngress(
                owner, wx_wasm_execution::WorkClass::PendingEvents,
                scope, lease))
    {
        return {};
    }

    return {lease};
}

void wxWasmExecutionQueueDelegatedPendingEvent(
        wxEvtHandler *handler, wxEvent *event,
        wx_wasm_execution::PendingEventDelegate delegate)
{
    // QueueEvent deliberately rejects null, but routing it through that API
    // also enters its assertion path and obscures which delegated producer
    // violated the contract. Refuse before any handler or provenance access.
    if (!event)
        return;

    if (!handler)
    {
        delete event;
        return;
    }

    if (!delegate)
    {
        handler->QueueEvent(event);
        return;
    }

    wxWasmPendingEventDelegateScope scope(handler, event, delegate);
    handler->QueueEvent(event);
}

wx_wasm_execution::LeaseToken wxWasmExecutionOpenModalLease(
        const void *scope, wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::WorkMask allowed, int waitToken,
        wxWasmExecutionLeaseReadyCallback readyCallback)
{
    const wx_wasm_execution::OwnerToken mappedParent =
            wxWasmInternalOwnerForCurrentStack();
    const wx_wasm_execution::OwnerToken parent =
            mappedParent ? mappedParent : wxWasmStartupOwner();

    return wxWasmExecutionOpenModalLeaseForOwner(
            scope, parent, targetScope, allowed, waitToken, readyCallback);
}

wx_wasm_execution::LeaseToken wxWasmExecutionOpenModalLeaseForOwner(
        const void *scope, wx_wasm_execution::OwnerToken parent,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::WorkMask allowed, int waitToken,
        wxWasmExecutionLeaseReadyCallback readyCallback)
{
    const bool hasWait = waitToken > 0;
    const bool hasCallback = readyCallback != nullptr;

    if (!scope || !parent || !targetScope || hasWait == hasCallback
        || wxWasmModalLeases().find(scope) != wxWasmModalLeases().end())
    {
        printf("[wx-owner] modal opened without exact owner, target, or "
               "completion provenance\n");
        wxWasmExecutionFailStop("invalid modal lease provenance");
        return {};
    }

    wx_wasm_execution::LeaseToken lease;

    try
    {
        lease = wxWasmExecutionCoordinator().OpenLease(
                parent, allowed, targetScope);
    }
    catch (...)
    {
        // OpenLease() allocates its retained lease record.  It can advance
        // generation counters before vector growth throws, so continuing with
        // the coordinator would not be a recoverable transaction.
        wxWasmExecutionFailStop("modal coordinator lease allocation failed");
        return {};
    }

    if (!lease)
    {
        printf("[wx-owner] refused modal lease for owner=%llu generation=%llu\n",
               static_cast<unsigned long long>(parent.id.value),
               static_cast<unsigned long long>(parent.generation));
        wxWasmExecutionFailStop("modal lease admission refused");
    }
    else
    {
        wxWasmModalLeaseRecord record;
        record.lease = lease;
        record.waitToken = waitToken;
        record.readyCallback = readyCallback;

        try
        {
            const auto inserted = wxWasmModalLeases().emplace(scope, record);
            if (!inserted.second)
            {
                wxWasmExecutionFailStop(
                        "modal lease record was published twice");
                return {};
            }
        }
        catch (...)
        {
            // OpenLease() already published coordinator state.  Fail-stop is
            // the only exception-safe exit: it abandons that state and closes
            // every native and JavaScript admission queue.
            wxWasmExecutionFailStop("modal lease record allocation failed");
            return {};
        }

        size_t transferredPendingEvents = 0;
        if (!wxWasmBindPendingEventsToLease(
                    parent, lease, transferredPendingEvents))
        {
            // The helper latched the exact pending-event failure under its
            // mutex.  Consume it only after the lock is released; fail-stop
            // then owns the partially opened modal and all retained events.
            wxWasmConsumePendingEventFailure();
            return {};
        }

        wxWasmPublishActiveBrowserIngressLease();

        if (transferredPendingEvents != 0)
            wxWasmArmExecutionTick();
    }

    if (lease && !wxWasmDispatchJobs().empty())
        wxWasmArmExecutionTick();

    return lease;
}

bool wxWasmExecutionRequestModalClose(const void *scope, int result)
{
    wx_wasm_execution::Coordinator& coordinator =
            wxWasmExecutionCoordinator();
    const auto it = wxWasmModalLeases().find(scope);

    if (it == wxWasmModalLeases().end())
    {
        wxWasmExecutionFailStop("modal close requested for unknown scope");
        return false;
    }

    wxWasmModalLeaseRecord& record = it->second;

    if (record.closeRequested)
    {
        // wxEventLoop permits repeated ScheduleExit() calls. Preserve its
        // last-result semantics while the exact wake is still pending. Once
        // delivery has happened, the wait token is immutable and another
        // request is only an idempotent close notification.
        if (!record.completionDelivered)
            record.closeResult = result;

        wxWasmTryCompleteModalLease(scope);
        return true;
    }

    record.closeRequested = true;
    record.closeResult = result;

    if (!coordinator.BeginClose(record.lease))
    {
        printf("[wx-owner] refused begin-close lease=%llu generation=%llu\n",
               static_cast<unsigned long long>(record.lease.id.value),
               static_cast<unsigned long long>(record.lease.generation));
        wxWasmExecutionFailStop("modal lease refused begin-close");
        return false;
    }

    // Browser receipts after BeginClose must not keep borrowing this child,
    // and a delayed L1 receipt must retain the L1 snapshot it already owns.
    wxWasmPublishActiveBrowserIngressLease();

    wxWasmTryCompleteModalLease(scope);
    return true;
}

void wxWasmExecutionCloseModalLease(const void *scope,
                                    wx_wasm_execution::LeaseToken lease)
{
    if (!lease)
        return;

    const auto it = wxWasmModalLeases().find(scope);

    if (it == wxWasmModalLeases().end() || it->second.lease != lease
        || !it->second.completionDelivered
        || !wxWasmExecutionCoordinator().LeaseReady(lease))
    {
        wxWasmExecutionFailStop("modal lease closed before exact completion");
        return;
    }

    if (!wxWasmExecutionCoordinator().CloseLease(lease))
    {
        printf("[wx-owner] refused close lease=%llu generation=%llu\n",
               static_cast<unsigned long long>(lease.id.value),
               static_cast<unsigned long long>(lease.generation));
        wxWasmExecutionFailStop("modal lease refused terminal close");
        return;
    }

    // A nested parent lease can become active again after the pop. Publish
    // that exact generation; never let JavaScript infer it from window scope.
    wxWasmPublishActiveBrowserIngressLease();

    wxWasmCompleteRetiredEmbindOwners();

    wxWasmModalLeases().erase(it);
    // Popping a nested lease can retire its zero-reference parent child. If
    // that parent lease was already closing, this is its child-zero edge.
    wxWasmTryCompleteActiveModalLease();

    if (!wxWasmDispatchJobs().empty())
        wxWasmArmExecutionTick();
}

wx_wasm_execution::OwnerToken wxWasmExecutionCurrentOwner()
{
    // Context 0 is shared by startup code and unrelated browser callbacks.
    // It cannot carry a public capability. The two audited modal adapters use
    // wxWasmStartupOwner() explicitly while initial OnInit is in progress.
    return wxWasmInternalOwnerForCurrentStack();
}

bool wxWasmExecutionRetainOwner(wx_wasm_execution::OwnerToken owner)
{
    return wxWasmExecutionCoordinator().Retain(owner);
}

bool wxWasmExecutionReleaseOwner(wx_wasm_execution::OwnerToken owner)
{
    return wxWasmReleaseExecutionOwner(owner);
}

bool wxWasmExecutionRecordOwnerFailure(
        wx_wasm_execution::OwnerToken owner, const char *reason)
{
    const auto it = wxWasmEmbindOwners().find(owner.id.value);

    if (it == wxWasmEmbindOwners().end() || it->second.owner != owner)
        return false;

    if (it->second.failure.empty())
        it->second.failure = reason ? reason : "affiliated native execution failed";

    return true;
}

wx_wasm_execution::OwnerToken wxWasmExecutionBeginStartup()
{
    if (!emscripten_is_main_runtime_thread() || wxWasmStartupOwner())
    {
        wxWasmExecutionFailStop("invalid initial startup-owner acquisition");
        return {};
    }

    const wx_wasm_execution::Admission admission =
            wxWasmExecutionCoordinator().Admit(
                    wx_wasm_execution::WorkClass::Ordinary);

    if (!admission
        || admission.kind != wx_wasm_execution::AdmissionKind::Root)
    {
        wxWasmExecutionFailStop("could not acquire initial startup owner");
        return {};
    }

    wxWasmStartupOwner() = admission.owner;
    return admission.owner;
}

bool wxWasmExecutionEndStartup(wx_wasm_execution::OwnerToken owner)
{
    if (!emscripten_is_main_runtime_thread()
        || !owner || wxWasmStartupOwner() != owner)
        return false;

    wxWasmStartupOwner() = {};
    return wxWasmReleaseExecutionOwner(owner);
}

bool wxWasmExecutionCanStartFreshEntry()
{
    return !wxWasmExecutionCoordinator().Failed()
           && !wxWasmExecutionCoordinator().RootOwner()
           && wxWasmDispatchJobs().empty()
           && pcbjam_sched::current() == 0
           && !pcbjam_sched::transition_in_flight();
}

wx_wasm_execution::PendingEventTagDisposition
wxWasmExecutionTagPendingEvent(const void *event, const void *handler)
{
    if (!event)
        return wx_wasm_execution::PendingEventTagDisposition::RejectedDeleteEvent;

    wxWasmPendingEventProvenance provenance;

    const wx_wasm_execution::PendingEventDelegate delegate =
            wxWasmPendingEventDelegateFor(event, handler);

    if (delegate)
    {
        // The captured token is immutable and safe to copy from a worker;
        // unlike ambient capture, this does not inspect the main-thread
        // coordinator. Do not also infer an owner from the posting stack. In
        // particular, a stale capability posted by a later modal child must
        // still compare its original lease generation and wait for root.
        provenance.lease = delegate.lease;
        provenance.targetScope = delegate.lease.targetScope;
    }
    // QueueEvent is one of the few wx APIs that a pthread may call. Never
    // inspect the main-thread owner tree from a worker; an ordinary unowned
    // record waits for the next root transaction.
    else if (emscripten_is_main_runtime_thread())
    {
        const wxEvent *queuedEvent = static_cast<const wxEvent *>(event);
        const wxEvtHandler *queueHandler = handler
                ? static_cast<const wxEvtHandler *>(handler) : nullptr;
        wxWindow *target = NULL;
        wxWindow *handlerTarget = NULL;

        if (queuedEvent)
        {
            target = wxDynamicCast(
                    const_cast<wxObject *>(queuedEvent->GetEventObject()),
                    wxWindow);
        }

        if (queueHandler)
        {
            handlerTarget = wxDynamicCast(
                    const_cast<wxEvtHandler *>(queueHandler), wxWindow);
        }

        if (!target)
            target = handlerTarget;

        provenance.targetScope = wxWasmExecutionScopeForWindow(target);

        // CallAfter on a non-window controller names that controller as both
        // QueueEvent handler and event object. Only an explicit association
        // may delegate the controller into a modal window family; targetless
        // pending work must remain blocked behind the parent transaction.
        if (!provenance.targetScope)
        {
            provenance.targetScope =
                    wxWasmPendingEventScopeForHandler(handler);
        }

        if (!provenance.targetScope && queuedEvent)
        {
            provenance.targetScope = wxWasmPendingEventScopeForHandler(
                    queuedEvent->GetEventObject());
        }

        provenance.owner = wxWasmInternalOwnerForCurrentStack();

        if (provenance.owner)
        {
            const wx_wasm_execution::LeaseToken activeLease =
                    wxWasmExecutionCoordinator().ActiveLease();
            provenance.lease =
                    wx_wasm_execution::LeaseProvenanceForIngress(
                            wx_wasm_execution::WorkClass::PendingEvents,
                            provenance.targetScope,
                            activeLease);

            // CallAfter() may be posted to a dialog (or one of its real child
            // windows) immediately before ShowModal().  There is no lease to
            // capture yet, so retain a consume-once capability on this exact
            // event.  A non-window sidecar association is deliberately not
            // enough.  Also exclude the currently active scope: EndModal()
            // clears IsModal() before it closes the lease, and work posted in
            // that interval belongs behind the closing transaction, not to a
            // possible future reuse of the dialog.
            wxDialog * const targetDialog = handlerTarget
                    ? wxDynamicCast(
                            wxGetTopLevelParent(handlerTarget), wxDialog)
                    : nullptr;
            provenance.nextModal =
                    queuedEvent
                    && queuedEvent->GetEventType() == wxEVT_ASYNC_METHOD_CALL
                    && queuedEvent->GetEventObject() == queueHandler
                    && !provenance.lease && provenance.targetScope
                    && targetDialog && !targetDialog->IsModal()
                    && (!activeLease
                        || activeLease.targetScope != provenance.targetScope);
        }
    }

    wx_wasm_execution::PendingEventTagDisposition disposition =
            wx_wasm_execution::PendingEventTagDisposition::Accepted;

    {
        std::lock_guard<std::mutex> lock(wxWasmPendingEventOwnersMutex());
        wxWasmPendingEventState& state = wxWasmPendingEvents();

        if (state.closed
            || state.stats.failure
                    != wx_wasm_execution::PendingEventFailure::None)
        {
            ++state.stats.rejected;
            disposition = wx_wasm_execution::PendingEventTagDisposition::
                    RejectedDeleteEvent;
        }
        else if (state.events.find(event) != state.events.end())
        {
            ++state.stats.rejected;
            ++state.stats.duplicateRejected;
            wxWasmLatchPendingEventFailure(
                    state,
                    wx_wasm_execution::PendingEventFailure::DuplicatePointer);
            // The first QueueEvent already transferred this exact pointer to
            // the physical list. Deleting the second submission would leave
            // that list with a dangling pointer.
            disposition = wx_wasm_execution::PendingEventTagDisposition::
                    RejectedAlreadyOwned;
        }
        else if (state.events.size()
                 >= wx_wasm_execution::MaxPendingEvents)
        {
            ++state.stats.rejected;
            wxWasmLatchPendingEventFailure(
                    state,
                    wx_wasm_execution::PendingEventFailure::Overflow);
            disposition = wx_wasm_execution::PendingEventTagDisposition::
                    RejectedDeleteEvent;
        }
        else
        {
            const bool hasOwnerIndex = provenance.owner
                                       && provenance.targetScope;
            const bool hasLeaseIndex = provenance.lease
                                       && provenance.targetScope;
            const wxWasmPendingEventIndexKey ownerKey =
                    wxWasmPendingEventIndex(
                            provenance.owner.id.value,
                            provenance.targetScope);
            const wxWasmPendingEventIndexKey leaseKey =
                    wxWasmPendingEventIndex(
                            provenance.lease.id.value,
                            provenance.targetScope);
            bool ownerAdded = false;
            bool leaseAdded = false;

            try
            {
                if (hasOwnerIndex)
                {
                    ++state.byOwner[ownerKey];
                    ownerAdded = true;
                }

                if (hasLeaseIndex)
                {
                    ++state.byLease[leaseKey];
                    leaseAdded = true;
                }

                const auto inserted = state.events.emplace(event, provenance);

                if (!inserted.second)
                {
                    if (ownerAdded)
                        wxWasmDecrementPendingEventIndex(
                                state.byOwner, ownerKey);
                    if (leaseAdded)
                        wxWasmDecrementPendingEventIndex(
                                state.byLease, leaseKey);

                    ++state.stats.rejected;
                    ++state.stats.duplicateRejected;
                    wxWasmLatchPendingEventFailure(
                            state,
                            wx_wasm_execution::PendingEventFailure::
                                    DuplicatePointer);
                    disposition =
                            wx_wasm_execution::PendingEventTagDisposition::
                                    RejectedAlreadyOwned;
                }
                else
                {
                    ++state.stats.accepted;
                    if (state.events.size() > state.stats.highWater)
                        state.stats.highWater = state.events.size();
                }
            }
            catch (...)
            {
                if (ownerAdded)
                    wxWasmDecrementPendingEventIndex(state.byOwner, ownerKey);
                if (leaseAdded)
                    wxWasmDecrementPendingEventIndex(state.byLease, leaseKey);

                ++state.stats.rejected;
                wxWasmLatchPendingEventFailure(
                        state,
                        wx_wasm_execution::PendingEventFailure::Allocation);
                disposition = wx_wasm_execution::PendingEventTagDisposition::
                        RejectedDeleteEvent;
            }
        }
    }

    if (disposition
            != wx_wasm_execution::PendingEventTagDisposition::Accepted
        && emscripten_is_main_runtime_thread())
    {
        wxWasmConsumePendingEventFailure();
    }

    return disposition;
}

bool wxWasmExecutionHasProcessablePendingEvents()
{
    if (!wxWasmConsumePendingEventFailure())
        return false;

    const wx_wasm_execution::OwnerToken current =
            wxWasmInternalOwnerForCurrentStack();
    wx_wasm_execution::Coordinator& coordinator =
            wxWasmExecutionCoordinator();
    const wx_wasm_execution::OwnerToken root = coordinator.RootOwner();
    const wx_wasm_execution::LeaseToken active = coordinator.ActiveLease();
    const wx_wasm_execution::ScopeToken activeScope =
            coordinator.ActiveLeaseScope();

    std::lock_guard<std::mutex> lock(wxWasmPendingEventOwnersMutex());
    wxWasmPendingEventState& state = wxWasmPendingEvents();
    ++state.stats.processableQueries;

    bool processable = false;

    if (!state.closed && !state.events.empty())
    {
        if (!current)
        {
            processable = !root;
        }
        else if (!current.parent)
        {
            // The ordinary root is the stable global drain. No other owner can
            // be live once it is running outside a delegated child. During the
            // small interval after a child lease opens but before its opener
            // parks, however, the root must not traverse that child's work.
            processable = current == root && !active;
        }
        else if (active && activeScope
                 && coordinator.CanRunNestedIngress(
                         current,
                         wx_wasm_execution::WorkClass::PendingEvents,
                         activeScope, active))
        {
            const wxWasmPendingEventIndexKey leaseKey =
                    wxWasmPendingEventIndex(active.id.value, activeScope);
            processable = state.byLease.find(leaseKey)
                          != state.byLease.end();
        }
    }

    if (!processable && !state.events.empty())
        ++state.stats.avoidedPhysicalScans;

    return processable;
}

bool wxWasmExecutionMayProcessPendingEvent(const void *event)
{
    if (!event)
        return false;

    const wx_wasm_execution::OwnerToken current =
            wxWasmInternalOwnerForCurrentStack();
    wx_wasm_execution::Coordinator& coordinator =
            wxWasmExecutionCoordinator();

    // No active transaction means the legacy/startup pending loop can drain.
    // If a root exists but this stack has no provenance, it is precisely the
    // fresh-entry collision which admission is meant to defer.
    if (coordinator.Failed())
        return false;

    if (!current)
        return !coordinator.RootOwner();

    wxWasmPendingEventProvenance provenance;

    {
        std::lock_guard<std::mutex> lock(wxWasmPendingEventOwnersMutex());
        wxWasmPendingEventState& state = wxWasmPendingEvents();
        ++state.stats.dispatchChecks;
        const auto it = state.events.find(event);

        if (it == state.events.end())
        {
            wxWasmLatchPendingEventFailure(
                    state,
                    wx_wasm_execution::PendingEventFailure::MissingProvenance);
            return false;
        }

        provenance = it->second;
    }

    if (current == coordinator.RootOwner())
    {
        // The root is the stable global drain, including unowned worker work
        // and stale exact-lease follow-up.  It cannot drain after any child
        // lease has opened: this also covers synchronous InitDialog yields
        // before the opener parks, at every nesting depth.
        return !coordinator.ActiveLease();
    }

    if (current.parent)
    {
        // Use the same exact owner/lease/scope/refcount predicate as nested DOM
        // ingress. It excludes an outer child during a nested modal's pre-park
        // interval and excludes every child after BeginClose() revokes the
        // lease, without inventing a second pending-event policy.
        return coordinator.CanRunNestedIngress(
                current, wx_wasm_execution::WorkClass::PendingEvents,
                provenance.targetScope, provenance.lease);
    }

    return false;
}

void wxWasmExecutionForgetPendingEvent(const void *event)
{
    if (!event)
        return;

    std::lock_guard<std::mutex> lock(wxWasmPendingEventOwnersMutex());
    wxWasmPendingEventState& state = wxWasmPendingEvents();
    const auto it = state.events.find(event);

    if (it == state.events.end())
    {
        ++state.stats.unmatchedForgets;
        if (!state.closed)
        {
            wxWasmLatchPendingEventFailure(
                    state,
                    wx_wasm_execution::PendingEventFailure::MissingProvenance);
        }
        return;
    }

    wxWasmRemovePendingEventIndexes(state, it->second);
    state.events.erase(it);
    ++state.stats.forgotten;
}

void wxWasmExecutionRejectPendingEventStorage(const void *event)
{
    {
        std::lock_guard<std::mutex> lock(wxWasmPendingEventOwnersMutex());
        wxWasmPendingEventState& state = wxWasmPendingEvents();
        const auto it = state.events.find(event);
        const bool hadProvenance = it != state.events.end();

        if (hadProvenance)
        {
            wxWasmRemovePendingEventIndexes(state, it->second);
            state.events.erase(it);
            ++state.stats.forgotten;
        }
        else
        {
            ++state.stats.unmatchedForgets;
        }

        ++state.stats.rejected;
        wxWasmLatchPendingEventFailure(
                state,
                !hadProvenance
                    ? wx_wasm_execution::PendingEventFailure::MissingProvenance
                    : wx_wasm_execution::PendingEventFailure::Allocation);
    }

    // A pthread may own QueueEvent's physical allocation attempt. It latches
    // under the mutex and wakes the ordinary pump; only the main runtime may
    // touch semantic ownership or JavaScript during terminal cleanup.
    if (emscripten_is_main_runtime_thread())
        wxWasmConsumePendingEventFailure();
}

wx_wasm_execution::PendingEventQueueStats
wxWasmExecutionPendingEventQueueStats()
{
    std::lock_guard<std::mutex> lock(wxWasmPendingEventOwnersMutex());
    wx_wasm_execution::PendingEventQueueStats stats =
            wxWasmPendingEvents().stats;
    stats.retained = wxWasmPendingEvents().events.size();
    return stats;
}

// ----------------------------------------------------------------------------
// The scheduler-owned main-loop context.
//
// The top-level loop's per-frame wait used to be an Asyncify park of the MAIN
// stack (wxWasmYieldToBrowser). That was safe only
// while dispatch also ran there. The moment the scheduler swaps contexts from
// a tick, that park and the transitions interleave over one Asyncify slot
// (the measured overlapped-wake). So the loop itself moves onto a context
// whose per-frame wait is yield_park; OnRun and main() RETURN, the runtime
// stays alive (EXIT_RUNTIME=0), and a rAF-armed pump drives the loop from a
// clean main stack that is only ever the scheduler.
// ----------------------------------------------------------------------------
namespace
{
pcbjam_sched::ContextId g_mainLoopContext = 0;
bool g_mainLoopDetached = false;
int g_mainLoopResult = 0;

// The loop context carries DoRun's one-time setup (top-window layout) and the
// whole app teardown at exit, so size it like the dispatch context rather
// than the scheduler default.
constexpr size_t MAIN_LOOP_STACK_BYTES = 1024 * 1024;
constexpr size_t MAIN_LOOP_ASYNCIFY_BYTES = 512 * 1024;

void wxWasmMainLoopEntry(void *arg)
{
    wxApp *app = static_cast<wxApp *>(arg);

    g_mainLoopResult = app->RunMainLoopOnContext();

    // The loop exited: the app really is ending. Run the teardown wxEntry
    // skipped when OnRun detached — OnExit, then the wxUninitialize that
    // releases the pinned init count and performs wxEntryCleanup (which
    // deletes the app; `app` is dangling below this point).
    app->OnExit();
    wxUninitialize();

    // A raw fiber entry must never return (emscripten ends the program), and
    // nothing marks this context ready again, so the park is terminal.
    for (;;)
        wxWasmParkExternalOrAbort("main-loop-exited");
}
}  // namespace

// One frame's wake: rAF is the same cadence the old in-place park awaited.
// The callback is a fresh JS task, which is exactly what drain_all() requires.
EM_JS(void, wxWasmArmFrameWake, (), {
    requestAnimationFrame(function () {
        var scheduler = globalThis.__wxScheduler;
        if (globalThis.__wxNativeIntegrityUnknown || !scheduler ||
            typeof scheduler.enqueueNativeEntry !== "function" ||
            !scheduler.canTouchNative()) return;
        scheduler.enqueueNativeEntry("main-loop", "main-loop frame wake", function () {
            if (scheduler.canTouchNative())
                Module["_wxWasmMainLoopPump"]();
        });
    });
});

// The FIRST entry must also come from a clean JS task, AFTER main() has
// returned: entering from OnRun's own frame would capture main()/wxEntry
// frames into the scheduler fiber's buffer, and main would then "return"
// inside some later pump's rewind.
EM_JS(int, wxWasmArmMainLoopKick, (), {
    try {
        var scheduler = globalThis.__wxScheduler;
        if (globalThis.__wxNativeIntegrityUnknown || !scheduler ||
            typeof scheduler.enqueueNativeEntry !== "function" ||
            typeof scheduler.canTouchNative !== "function" ||
            !scheduler.canTouchNative()) return 0;
        return scheduler.enqueueNativeEntry(
            "main-loop", "main-loop initial kick", function () {
                if (scheduler.canTouchNative())
                    Module["_wxWasmMainLoopPump"]();
            }) ? 1 : 0;
    } catch (error) {
        console.error("[wx-main-loop] initial kick submission threw", error);
        return 0;
    }
});

extern "C" {

    // Enter or resume the main-loop context from a clean stack. Mirrors
    // wxWasmDispatchOnContext's shape: Fresh means first entry, a "frame"
    // park means the per-frame wait — anything else (a wait parked deeper)
    // is left alone and the pump just runs whatever else is ready.
    void EMSCRIPTEN_KEEPALIVE wxWasmMainLoopPump()
    {
        if (!g_mainLoopContext)
            return;

        if (wxWasmSchedulerEntryBusy())
        {
            wxWasmExecutionFailStop(
                    "main-loop pump entered while scheduler was busy");
            return;
        }

        const pcbjam_sched::Status st = pcbjam_sched::status_of(g_mainLoopContext);
        bool activated = true;

        if (st == pcbjam_sched::Status::Fresh)
            activated = pcbjam_sched::fiber_start(g_mainLoopContext, 0);
        else if (st == pcbjam_sched::Status::Parked
                 && strcmp(pcbjam_sched::park_reason_of(g_mainLoopContext),
                           "frame") == 0)
            activated = pcbjam_sched::mark_ready(g_mainLoopContext, 0);

        if (!activated)
        {
            wxWasmExecutionFailStop(
                    "main-loop context refused an admitted frame wake");
            return;
        }

        wxWasmDrainSchedulerBatch();
    }

}  // extern "C"

// Compile-time diagnostic fallback: 0 runs the loop inline on the main stack.
#define wxWASM_D5_DETACH 1

extern "C" bool wxWasmDetachMainLoop(wxApp *app)
{
    if (!wxWASM_D5_DETACH || g_mainLoopContext || !app)
        return false;

    // Capture the MAIN stack's bounds here: OnRun is the last moment we are
    // provably standing on it. DoRun now runs on the loop context, where a
    // live query would record the CONTEXT's bounds and every later stack
    // classification would silently be wrong (doc 22 §7 trap 2).
    s_mainStackBase = emscripten_stack_get_base();
    s_mainStackEnd = emscripten_stack_get_end();

    const pcbjam_sched::ContextId fresh = pcbjam_sched::fiber_create(
        wxWasmMainLoopEntry, app, NULL, MAIN_LOOP_STACK_BYTES,
        MAIN_LOOP_ASYNCIFY_BYTES, "wx-main-loop");

    if (!fresh)
        return false;

    // The queued callback resolves the context through this global, so publish
    // it for submission and roll it back if the JavaScript scheduler does not
    // acknowledge ownership. `detached` is the external lifetime promise: it
    // becomes true only after that exact initial progress edge exists.
    g_mainLoopContext = fresh;

    if (!wxWasmArmMainLoopKick())
    {
        g_mainLoopContext = 0;

        if (!pcbjam_sched::fiber_release(fresh))
        {
            wxWasmExecutionFailStop(
                    "rejected main-loop kick left an unreleasable context");
        }

        return false;
    }

    g_mainLoopDetached = true;
    return true;
}

extern "C" bool wxWasmMainLoopDetached()
{
    return g_mainLoopDetached;
}

// Deterministic reducer seam for the three JavaScript ownership-refusal paths.
// It submits no work when the scheduler is missing, rejecting, or throwing;
// production detach uses this exact helper before publishing detached state.
extern "C" int EMSCRIPTEN_KEEPALIVE wxWasmTestMainLoopKickSubmission()
{
    return wxWasmArmMainLoopKick();
}

bool wxWasmExecutionQueueOrdinary(
        void (*fn)(void *), void *arg,
        wx_wasm_execution::DiscardCallback discard)
{
    return wxWasmStageExecutionJob(
            fn, arg, wx_wasm_execution::WorkClass::Ordinary, {}, {}, 0,
            discard);
}

bool wxWasmExecutionQueueOrdinaryRetained(
        void (*fn)(void *), void *arg, std::size_t retainedBytes,
        wx_wasm_execution::DiscardCallback discard)
{
    return wxWasmStageExecutionJob(
            fn, arg, wx_wasm_execution::WorkClass::Ordinary, {}, {}, 0,
            discard, wx_wasm_execution::CoalesceClass::None, 0,
            retainedBytes);
}

EM_JS(int, wxWasmEmbindDeliverJs, (unsigned jobId), {
    var scheduler = globalThis.__wxScheduler;
    if (scheduler && typeof scheduler.deliverMutator === "function")
        return scheduler.deliverMutator(jobId >>> 0) | 0;
    return 0;
});

namespace
{
void wxWasmEmbindDeliver(void *arg)
{
    const unsigned jobId = static_cast<unsigned>(
            reinterpret_cast<std::uintptr_t>(arg));
    const wx_wasm_execution::OwnerToken owner =
            wxWasmExecutionCurrentOwner();

    if (!owner)
    {
        wxWasmExecutionFailStop("Embind delivery has no execution owner");
        return;
    }

    const auto inserted = wxWasmEmbindOwners().emplace(
            owner.id.value,
            wxWasmEmbindOwnerRecord{owner, jobId, {}});

    if (!inserted.second)
    {
        wxWasmExecutionFailStop("execution owner has multiple Embind tickets");
        return;
    }

    if (!wxWasmEmbindDeliverJs(jobId))
        inserted.first->second.failure = "native delivery ticket was rejected";
}
} // namespace

extern "C" int EMSCRIPTEN_KEEPALIVE wxWasmEmbindSubmit(unsigned jobId)
{
    if (!jobId || !wxTheApp || wxWasmExecutionCoordinator().Failed())
        return 0;

    return wxWasmExecutionQueueOrdinary(
                   wxWasmEmbindDeliver,
                   reinterpret_cast<void *>(
                           static_cast<std::uintptr_t>(jobId)))
                   ? 1 : 0;
}

bool wxWasmExecutionQueueAffiliated(
        wx_wasm_execution::OwnerToken owner,
        void (*fn)(void *), void *arg,
        wx_wasm_execution::DiscardCallback discard)
{
    if (!fn || !wxWasmExecutionCoordinator().Owns(owner))
        return false;

    return wxWasmEnqueueDispatchJob(
            {fn, arg, wx_wasm_execution::WorkClass::Ordinary, {}, {}, owner,
             0, ++wxWasmNextDispatchSequence(), discard,
             wx_wasm_execution::CoalesceClass::None});
}

extern "C" void EMSCRIPTEN_KEEPALIVE wxWasmExecutionTick()
{
    if (wxWasmSchedulerEntryBusy())
    {
        wxWasmExecutionFailStop(
                "execution-owner tick entered while scheduler was busy");
        return;
    }

    wxWasmDispatchOnContext();
}

wx_wasm_execution::DispatchSubmitDisposition
wxWasmRunOnDispatchContextScoped(
        void (*fn)(void *), void *arg,
        wx_wasm_execution::WorkClass cls,
        wx_wasm_execution::ScopeToken targetScope,
        wx_wasm_execution::DiscardCallback discard,
        wx_wasm_execution::CoalesceClass coalesce,
        std::uintptr_t coalesceKey,
        wx_wasm_execution::BrowserIngressReceipt receipt)
{
    // WHY THIS EXISTS (doc 22 §10, measured 2026-08-07). A DOM event handler
    // enters wasm on the MAIN stack, and if it dispatches there it reaches a
    // KiCad tool coroutine through libcontext's DIRECT symmetric swap — while
    // the tick reaches that same coroutine through the dispatch context as a
    // STAR TRANSFER. A capture written by one path cannot be rewound by the
    // other: `index out of bounds` in doRewind, on every canvas tool. Every
    // entry into a coroutine must therefore go through the scheduler.
    //
    // The job runs SYNCHRONOUSLY in the common case: drain_all returns once
    // the context parks again, which for a job that does not itself park is
    // after it completed. Callers that need an answer (a key handler's
    // preventDefault) read it from their own job struct; callers whose job
    // parks (a click that opens a modal) get the same "returns while the work
    // continues" semantics the pre-D DOM handlers already had.
    if (!fn || wxWasmExecutionCoordinator().Failed())
    {
        return wx_wasm_execution::DispatchSubmitDisposition::
                RejectedCallerOwns;
    }

#if wxWASM_STAR_DISPATCH
    if (!wxTheApp)
    {
        fn(arg);
        return wx_wasm_execution::DispatchSubmitDisposition::
                AcceptedPayloadLive;
    }

    wx_wasm_execution::LeaseToken leaseProvenance;
    if (receipt)
    {
        if (!wxWasmPrepareBrowserIngress(
                receipt, cls, targetScope, leaseProvenance))
        {
            return wx_wasm_execution::DispatchSubmitDisposition::
                    RejectedCallerOwns;
        }
    }
    else
    {
        leaseProvenance =
                wx_wasm_execution::LeaseProvenanceForIngress(
                        cls, targetScope,
                        wxWasmExecutionCoordinator().ActiveLease());
    }
    wx_wasm_execution::DispatchSubmissionHandshake submission;

    if (!wxWasmEnqueueDispatchJob(
            {fn, arg, cls, targetScope, leaseProvenance, {},
             receipt.sequence, ++wxWasmNextDispatchSequence(),
             discard, coalesce,
             coalesceKey, &submission}, false))
    {
        return wx_wasm_execution::DispatchSubmitDisposition::
                RejectedCallerOwns;
    }

    // This generated Emscripten callback entered native code after an older
    // custom-DOM receipt was already retained in JavaScript. It may copy and
    // enqueue its payload now, but it must not drain inline. The dispatch
    // signal joins the same physical FIFO behind the older staging closure;
    // receipt-sequence insertion then keeps their native envelopes A,B.
    if (receipt.deferredBehindEarlier)
    {
        const wx_wasm_execution::DispatchSubmitDisposition disposition =
                wxWasmDetachDispatchSubmission(submission);
        wxWasmArmExecutionTick();
        return disposition;
    }

    // A callback can be raised synchronously from JavaScript imported by an
    // already-owned stack. Mapping to an owner is provenance, not blanket
    // permission to run arbitrary work inline: application resize/focus and
    // Ordinary work must remain behind the modal child. Put every callback in
    // the typed queue first, then let the bounded nested-owner policy consume
    // only UserInput/PendingEvents with the exact scope and lease generation.
    const wx_wasm_execution::OwnerToken mappedOwner =
            wxWasmInternalOwnerForCurrentStack();
    if (mappedOwner)
    {
        wxWasmRunNestedOwnerJobs(mappedOwner);
        const wx_wasm_execution::DispatchSubmitDisposition disposition =
                wxWasmDetachDispatchSubmission(submission);
        wxWasmArmExecutionTick();
        return disposition;
    }

    // A scheduler transition already in progress cannot be drained
    // re-entrantly. Keep the job queued; the running dispatch entry or the
    // next fresh pump task will apply the same admission decision.
    if (wxWasmSchedulerEntryBusy())
    {
        const wx_wasm_execution::DispatchSubmitDisposition disposition =
                wxWasmDetachDispatchSubmission(submission);
        wxWasmArmExecutionTick();
        return disposition;
    }

    wxWasmDispatchOnContext();
    return wxWasmDetachDispatchSubmission(submission);
#else
    fn(arg);
    return wx_wasm_execution::DispatchSubmitDisposition::
            AcceptedPayloadLive;
#endif
}

// ----------------------------------------------------------------------------
// wxGUIEventLoop
// ----------------------------------------------------------------------------

void wxGUIEventLoop::ScheduleExit(int rc)
{
    wxCHECK_RET( IsInsideRun(), wxT("can't call ScheduleExit() if not started") );

    m_shouldExit = true;
    m_wasmExitCode = rc;

    // Use this loop's own state, not the process-wide nesting depth. A caller
    // can address the main loop while another loop is active; that must not
    // close (or fail-stop on) the other loop's exact lease.
    if (m_wasmNestedWaitActive)
        wxWasmExecutionRequestModalClose(this, rc);
}

bool wxGUIEventLoop::Pending() const
{
    return wxTheApp && wxTheApp->HasPendingEvents();
}

bool wxGUIEventLoop::Dispatch()
{
    if (!wxTheApp)
        return true;

    // wxYield/Dispatch recursion inherits only a proven current owner. A call
    // outside an owned stack uses normal admission; it must not create an
    // anonymous same-owner capability.
    const wx_wasm_execution::OwnerToken owner =
            wxWasmInternalOwnerForCurrentStack();
    if (!owner)
    {
        ProcessEvents();
        return true;
    }

    wxWasmRunNestedOwnerJobs(owner);
    if (wxWasmExecutionCoordinator().Failed())
        return true;

    const bool isRoot = owner == wxWasmExecutionCoordinator().RootOwner();
    wxWasmProcessEventsUngated(isRoot, isRoot);
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
    const wx_wasm_execution::OwnerToken owner =
            wxWasmInternalOwnerForCurrentStack();
    const wx_wasm_execution::OwnerToken root =
            wxWasmExecutionCoordinator().RootOwner();

    // Staged DOM ingress is separate from wx's pending-event list. Drain the
    // bounded same-owner snapshot even when Pending() is false; that is the
    // positive-sleep path in which the exact timer wake precedes mouse-up
    // consumption. Do not call Dispatch() below for this owner, because doing
    // so would take a second queue snapshot and let callbacks extend one yield.
    if (owner)
        wxWasmRunNestedOwnerJobs(owner);

    // Pending() reports the physical wx queue, not whether this execution
    // owner may consume anything in it. One pass drains every eligible
    // handler; repeating while only another owner's events remain spins.
    if (Pending())
    {
        if (!owner)
        {
            Dispatch();
        }
        else if (!wxWasmExecutionCoordinator().Failed())
        {
            const bool isRoot = owner == root;
            wxWasmProcessEventsUngated(isRoot, isRoot);
        }
    }

    // The base tail also runs global idle processing. Only an ordinary root
    // (or an actually idle owner tree) may do that; a modal child is limited
    // to the scoped pending-event pass performed by Dispatch() above.
    if ((!root && !owner) || owner == root)
        wxEventLoopBase::DoYieldFor(eventsToProcess);
}

int wxGUIEventLoop::DoRun()
{
    wxASSERT_MSG(IsOk(), wxT("invalid event loop"));

    m_wasmExitCode = 0;
    m_wasmNestedWaitActive = false;

    wxWasmSchedulerAssertInstalled();

    // Record the main stack's bounds so nested loops can later tell whether
    // they are standing somewhere else (see wxWasmOnCoroutineStack). The
    // detach already captured them in wxWasmDetachMainLoop — top-level
    // DoRun runs on the loop CONTEXT there, so a live query here would record
    // the context's bounds and misclassify every later stack. Only the
    // non-detached fallback still captures here, where depth-0 DoRun really
    // is on the main stack.
    if (s_wxRunDepth == 0 && !s_mainStackBase)
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
        // layer cannot use it as a symmetric suspension: the consume-once
        // rewind guard refuses that invalid resume, so the dialog can never be
        // closed by a click (docs/features/async/19). Bounce onto the main stack first —
        // that suspends the coroutine the legitimate way, through a fiber swap
        // the layer records, and leaves the park exactly where every
        // non-tool dialog already puts it.
        wxWasmNestedWaitArguments waitArguments;
        waitArguments.scope = this;
        waitArguments.target = m_wasmModalTarget;
        waitArguments.owner = wxWasmExecutionCurrentOwner();
        if (!waitArguments.owner)
            waitArguments.owner = wxWasmStartupOwner();

        // Wasm-specific nested-loop callers pass their exact modal window to
        // wxGUIEventLoop. A plain nested loop has no target authority and must
        // fail closed; mutable focus is not a lease capability.
        waitArguments.targetScope = wxWasmExecutionScopeForWindow(
                m_wasmModalTarget);

        if (!waitArguments.owner || !waitArguments.targetScope)
        {
            printf("[wx-owner] nested GUI loop has no explicit owner/target scope\n");
            wxWasmExecutionFailStop(
                    "nested GUI loop lacks an explicit modal target");
            --s_wxRunDepth;
            return -1;
        }

        wxWeakRef<wxWindow> previousFocus(wxWindow::FindFocus());

        m_wasmNestedWaitActive = true;

        if (!(wxWasmOnCoroutineStack()
              && wxWasmRunOnMainStack(&wxWasmNestedWaitBody, &waitArguments)))
        {
            wxWasmNestedWaitBody(&waitArguments);
        }

        m_wasmNestedWaitActive = false;
        wxWindow * const previousFocusWindow = previousFocus.get();

        if (previousFocusWindow && previousFocusWindow->IsShown()
            && previousFocusWindow->IsEnabled())
        {
            previousFocusWindow->SetFocus();
        }

        --s_wxRunDepth;
        return waitArguments.result;
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

    // One tick per animation frame. No throw (fatal under native wasm-EH), and
    // this loop also does not park the stack it stands on with Asyncify. It
    // runs on the main-loop context, so the per-frame wait is a context park the
    // rAF pump resolves — the main stack stays the scheduler's alone.
    // m_shouldExit, set by ScheduleExit(), ends the loop after the current tick.
    while (!m_shouldExit)
    {
        // Schedule, don't dispatch: see wxWasmScheduleProcessEvents. The tick's
        // events run from a fresh JS task while this loop is parked below, so a
        // tool coroutine resumed by them never swaps main out inside main's own
        // wake continuation.
        //
        // Unconditional at any DoRun depth: nested loops are waits, not pumps,
        // so gating on depth would leave a quasi-modal with no
        // dispatcher at all. The June double-driver hazard (§6e) required TWO
        // pumps re-driving one parked context under awaited semantics; with a
        // single plain-call tick and the scheduler's consume-once/deferred-wake
        // guards it is closed.
        wxWasmScheduleProcessEvents();

        if (pcbjam_sched::can_yield_here())
        {
            // Arm the next frame's wake, then yield this context to the
            // scheduler. The rAF callback (a fresh JS task) marks it ready and
            // drains — indistinguishable, in this frame, from the old await.
            wxWasmArmFrameWake();
            wxWasmParkExternalOrAbort("frame");
        }
        else
        {
            // Non-detached fallback (context creation failed): park the main
            // stack in place.
            wxWasmYieldToBrowser();
        }
    }
    --s_wxRunDepth;

    // The main loop has ended, and wx cleanup follows. Close both admission
    // layers and detach every retained native queue record before application
    // objects are destroyed. JavaScript reports stranded transport work; the
    // native queue invokes each supplied discard tail exactly once.
    wxWasmTerminateExecution("main loop exited", false);

    return m_wasmExitCode;
}
