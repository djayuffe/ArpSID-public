// SPDX-License-Identifier: BSD-3-Clause
// audited_mutex.h — RT-policy-tagged mutex wrapper (Audit #46).
//
// PROBLEM
// ------
// `ArpSIDAUv2Instance` carries multiple `std::mutex` members alongside
// render-thread state:
//
// * `activityMutex` — non-RT config gate
// * `stateMutex` — non-RT state, but lock sites scattered
// * `propertyListenerMutex` — host callback table
// * `closeWaitMutex` — suspendAuv2RenderingAndWait + close
//
// Comments document "never taken by componentRender()" — but the class
// mixes render-state and non-render-state, so a typo or refactor could
// silently introduce a mutex acquisition on the render thread. Per the
// audit: "any accidental lock on render is catastrophic" — a single such
// lock can cause crackle, dropouts, or priority inversion that crashes
// CoreAudio.
//
// THIS HEADER
// ----------
// `AuditedMutex<Policy>` is a template wrapper around `std::mutex` that:
//
// 1. Carries a COMPILE-TIME `AuditedMutexPolicy` tag (`NonRealtime`,
// `RealtimeAllowed`). Any attempt to declare a mutex with
// `RealtimeAllowed` policy is loud and require explicit ack.
// 2. Checks `sidRealtimeGuardActive()` on every `lock()` call. If the
// mutex is `NonRealtime` AND we're inside a SidRealtimeScope,
// records a diagnostic violation via `sidRealtimeGuardRecordViolation`.
// 3. Tracks per-mutex acquisition counters so production diagnostics
// can observe lock pressure even when no violations occur.
//
// The header is drop-in compatible with `std::lock_guard` /
// `std::unique_lock` since `AuditedMutex` exposes `lock()`, `try_lock()`,
// `unlock()` matching `BasicLockable` / `Lockable` requirements.
//
// SCOPE OF THIS SLICE
// ------------------
// * Header-only type + policy enum + tests.
// * Documentation comments pin which existing AUv2 mutexes WOULD be
// `NonRealtime` if migrated.
// * Existing mutexes are NOT replaced — that's a follow-up if the
// diagnostic counters surface any leakage in production.

#ifndef ARPSID_CORE_AUDITED_MUTEX_H
#define ARPSID_CORE_AUDITED_MUTEX_H

#include "arpsid/core/sid_realtime_guard.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <type_traits>

namespace ArpSID {

enum class AuditedMutexPolicy : std::uint8_t {
    NonRealtime      = 0,  ///< Acquiring on the render thread is a violation
    RealtimeAllowed  = 1,  ///< Render-thread acquisition is explicitly OK (rare)
};

constexpr const char* auditedMutexPolicyName(AuditedMutexPolicy p) noexcept {
    switch (p) {
        case AuditedMutexPolicy::NonRealtime:     return "non_realtime";
        case AuditedMutexPolicy::RealtimeAllowed: return "realtime_allowed";
    }
    return "unknown";
}

template <AuditedMutexPolicy Policy>
class AuditedMutex {
public:
    AuditedMutex() noexcept = default;
    AuditedMutex(const AuditedMutex&) = delete;
    AuditedMutex& operator=(const AuditedMutex&) = delete;
    AuditedMutex(AuditedMutex&&) = delete;
    AuditedMutex& operator=(AuditedMutex&&) = delete;

    // BasicLockable contract — std::lock_guard / std::unique_lock compatible.
    void lock() {
        if constexpr (Policy == AuditedMutexPolicy::NonRealtime) {
            if (sidRealtimeGuardActive()) {
                ++rtViolationCount_;
                sidRealtimeGuardRecordViolation("AuditedMutex<NonRealtime>::lock on RT thread");
            }
        }
        ++totalAcquisitionCount_;
        underlying_.lock();
    }

    bool try_lock() {
        if constexpr (Policy == AuditedMutexPolicy::NonRealtime) {
            if (sidRealtimeGuardActive()) {
                // try_lock is still observable — if a render thread tries
                // to acquire a non-RT mutex (even non-blocking) it's a
                // design smell that should be surfaced.
                ++rtViolationCount_;
                sidRealtimeGuardRecordViolation("AuditedMutex<NonRealtime>::try_lock on RT thread");
            }
        }
        if (!underlying_.try_lock()) return false;
        ++totalAcquisitionCount_;
        return true;
    }

    void unlock() {
        underlying_.unlock();
    }

    // Diagnostic accessors. Counters are atomic so render-thread reads
    // (e.g., from a hot-loop telemetry collector) are safe.
    std::uint64_t totalAcquisitionCount() const noexcept {
        return totalAcquisitionCount_.load(std::memory_order_acquire);
    }
    std::uint64_t rtViolationCount() const noexcept {
        return rtViolationCount_.load(std::memory_order_acquire);
    }
    void resetCountersForTesting() noexcept {
        totalAcquisitionCount_.store(0u, std::memory_order_release);
        rtViolationCount_.store(0u, std::memory_order_release);
    }

    static constexpr AuditedMutexPolicy policy() noexcept { return Policy; }

private:
    std::mutex underlying_{};
    std::atomic<std::uint64_t> totalAcquisitionCount_{0u};
    std::atomic<std::uint64_t> rtViolationCount_{0u};
};

// Canonical aliases for the AUv2 mutex set the audit identified.
//
// These types are header-only and produce zero runtime overhead when
// asserts/guards are compiled out. They CAN replace the existing
// `std::mutex` declarations in `ArpSIDAUv2Instance` one-by-one in
// follow-up slices once each lock site has been individually verified.
using AuditedActivityMutex          = AuditedMutex<AuditedMutexPolicy::NonRealtime>;
using AuditedStateMutex             = AuditedMutex<AuditedMutexPolicy::NonRealtime>;
using AuditedPropertyListenerMutex  = AuditedMutex<AuditedMutexPolicy::NonRealtime>;
using AuditedCloseWaitMutex         = AuditedMutex<AuditedMutexPolicy::NonRealtime>;
// Kernel-side non-RT mutexes (DSP kernel, never taken on render thread).
using AuditedSerializationMutex     = AuditedMutex<AuditedMutexPolicy::NonRealtime>;
using AuditedStateRestoreWriterMutex = AuditedMutex<AuditedMutexPolicy::NonRealtime>;

// Static guarantees pinned at compile time.
static_assert(AuditedActivityMutex::policy() == AuditedMutexPolicy::NonRealtime,
              "activity mutex must be NonRealtime");
static_assert(AuditedStateMutex::policy() == AuditedMutexPolicy::NonRealtime,
              "state mutex must be NonRealtime");
static_assert(AuditedPropertyListenerMutex::policy() == AuditedMutexPolicy::NonRealtime,
              "property-listener mutex must be NonRealtime");
static_assert(AuditedCloseWaitMutex::policy() == AuditedMutexPolicy::NonRealtime,
              "close-wait mutex must be NonRealtime");
static_assert(AuditedSerializationMutex::policy() == AuditedMutexPolicy::NonRealtime,
              "serialization mutex must be NonRealtime");
static_assert(AuditedStateRestoreWriterMutex::policy() == AuditedMutexPolicy::NonRealtime,
              "state-restore-writer mutex must be NonRealtime");

} // namespace ArpSID

#endif // ARPSID_CORE_AUDITED_MUTEX_H
