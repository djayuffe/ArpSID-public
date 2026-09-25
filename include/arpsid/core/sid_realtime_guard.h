#pragma once

// ── RT Isolation Guarantee ────────────────────────────────────────────────────
//
// Each ArpSID AUv2/AUv3 instance is realtime-isolated from all other running
// instances by the following contract:
//
// 1. THREAD-LOCAL GUARD. `gSidRealtimeGuardState` is thread_local, so each
// OS thread has its own depth counter and violation log. DAWs that run
// multiple instances SEQUENTIALLY on one thread are safe: a scope from
// Instance A is fully destroyed before Instance B's render starts. DAWs
// that run instances CONCURRENTLY each use a separate thread.
//
// 2. NO SHARED MUTABLE RENDER STATE. Each plugin instance owns all render
// state inside its `ArpSIDAUv2Instance` / `ArpSIDDSPKernel`. The only
// shared global is the factory-preset cache (`storageByFlavor`), which is
// written once via `dispatch_once` and treated as read-only thereafter.
//
// 3. STRICT MODE DEFAULT. `strictRealtimeNotifyMode` is ON by default and is
// DIAGNOSTIC/ATTRIBUTION ONLY for AUv2 render-notify callbacks: the AUv2
// contract (auval/Logic) requires the host's notify procs to be dispatched
// from the render call, so they execute through the bounded published table
// and any realtime violations they cause are attributed per callback.
// Strict mode does NOT prevent or quarantine the dispatch itself.
//
// 4. ALL LOCKS ARE AUDITED. Every mutex in the instance is an
// `AuditedMutex<NonRealtime>`; acquiring one on the render thread
// increments a violation counter observable in the diagnostic dashboard.
//
// Adding `ARPSID_RT_GUARD_ABORT_ON_VIOLATION=1` to the compile flags converts
// any RT violation from a logged event into an immediate abort — useful for
// development.

#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace ArpSID {

// Non-RT enforcement helpers (called from production code to flag bad patterns).
#define ARPSID_ASSERT_NON_REALTIME(site) \
    ArpSID::sidRealtimeGuardForbidAllocation(site)

#define ARPSID_ASSERT_NOT_RENDER_THREAD(site) \
    do { if (ArpSID::sidRealtimeGuardActive()) \
             ArpSID::sidRealtimeGuardRecordViolation(site); } while (0)

struct SidRealtimeGuardState {
    uint32_t depth = 0;
    uint64_t violations = 0;
    const char* lastViolation = nullptr;
};

inline thread_local SidRealtimeGuardState gSidRealtimeGuardState{};

inline bool sidRealtimeGuardActive() noexcept {
    return gSidRealtimeGuardState.depth != 0u;
}

inline uint32_t sidRealtimeGuardDepth() noexcept {
    return gSidRealtimeGuardState.depth;
}

inline uint64_t sidRealtimeGuardViolationCount() noexcept {
    return gSidRealtimeGuardState.violations;
}

inline const char* sidRealtimeGuardLastViolation() noexcept {
    return gSidRealtimeGuardState.lastViolation;
}

inline void sidRealtimeGuardResetForTest() noexcept {
    gSidRealtimeGuardState.violations = 0u;
    gSidRealtimeGuardState.lastViolation = nullptr;
}

inline void sidRealtimeGuardRecordViolation(const char* reason) noexcept {
    ++gSidRealtimeGuardState.violations;
    gSidRealtimeGuardState.lastViolation = reason;
#if defined(ARPSID_RT_GUARD_ABORT_ON_VIOLATION) && ARPSID_RT_GUARD_ABORT_ON_VIOLATION
    std::abort();
#else
    (void)reason;
#endif
}

inline void sidRealtimeGuardForbidAllocation(const char* site) noexcept {
    if (sidRealtimeGuardActive()) sidRealtimeGuardRecordViolation(site);
}

inline void sidRealtimeGuardForbidLock(const char* site) noexcept {
    if (sidRealtimeGuardActive()) sidRealtimeGuardRecordViolation(site);
}

inline void sidRealtimeGuardForbidHostCallback(const char* site) noexcept {
    if (sidRealtimeGuardActive()) sidRealtimeGuardRecordViolation(site);
}

inline void sidRealtimeGuardForbidLateTableInit(const char* site) noexcept {
    if (sidRealtimeGuardActive()) sidRealtimeGuardRecordViolation(site);
}

inline void sidRealtimeGuardForbidFileIO(const char* site) noexcept {
    if (sidRealtimeGuardActive()) sidRealtimeGuardRecordViolation(site);
}

inline void sidRealtimeGuardForbidStringFormatting(const char* site) noexcept {
    if (sidRealtimeGuardActive()) sidRealtimeGuardRecordViolation(site);
}

class SidRealtimeScope final {
public:
    explicit SidRealtimeScope(const char* label = nullptr) noexcept : label_(label) {
        (void)label_;
        ++gSidRealtimeGuardState.depth;
    }
    ~SidRealtimeScope() noexcept {
        if (gSidRealtimeGuardState.depth != 0u) --gSidRealtimeGuardState.depth;
    }
    SidRealtimeScope(const SidRealtimeScope&) = delete;
    SidRealtimeScope& operator=(const SidRealtimeScope&) = delete;
private:
    const char* label_ = nullptr;
};

} // namespace ArpSID
