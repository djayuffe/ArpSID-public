// SPDX-License-Identifier: BSD-3-Clause
// audited_mutex_v541_tests.cpp
//
// Pins Audit #46 — the AUv2 instance carries multiple `std::mutex`
// members alongside render-thread state. The legacy comments documented
// "never taken by componentRender()" but the class structure made silent
// regression possible. `AuditedMutex<Policy>` provides:
//
// * Compile-time policy tag (NonRealtime vs RealtimeAllowed) pinned via
// `static_assert`.
// * Runtime detection of render-thread acquisition attempts on a
// NonRealtime mutex via `sidRealtimeGuardActive()` check.
// * Per-mutex diagnostic counters (total acquisitions + RT-violations).
// * BasicLockable / Lockable compatibility (std::lock_guard /
// std::unique_lock drop-in).
//
// Tests:
// A. Policy tag pinned at compile time
// B. Basic lock/unlock with non-RT context: no violations
// C. Multiple acquisitions tick total counter
// D. try_lock semantics + counter behavior
// E. RT-context acquisition triggers violation counter
// F. RT-context try_lock_failure path also triggers (audit-correct:
// even non-blocking probes are design smells on render thread)
// G. std::lock_guard / std::unique_lock work with AuditedMutex
// H. Concurrent acquisition stress (no internal corruption)

#include "arpsid/core/audited_mutex.h"
#include "arpsid/core/sid_realtime_guard.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

} // namespace

int main() {
    using namespace ArpSID;

    // ── A. Policy tag is a compile-time constant pinned via static_assert ─
    {
        static_assert(AuditedActivityMutex::policy()
                      == AuditedMutexPolicy::NonRealtime,
                      "activity mutex policy is NonRealtime");
        static_assert(AuditedStateMutex::policy()
                      == AuditedMutexPolicy::NonRealtime,
                      "state mutex policy is NonRealtime");
        // Runtime mirror.
        AuditedStateMutex m;
        require(m.policy() == AuditedMutexPolicy::NonRealtime,
                "runtime policy() accessor matches static");
    }

    // ── B. Basic lock/unlock in non-RT context: no violations ─────────────
    {
        AuditedStateMutex m;
        sidRealtimeGuardResetForTest();
        // No SidRealtimeScope active → guard is NOT active.
        require(!sidRealtimeGuardActive(), "no RT scope active");
        m.lock();
        m.unlock();
        require(m.rtViolationCount() == 0,
                "no RT violation when locked outside RT scope");
        require(m.totalAcquisitionCount() == 1,
                "total acquisition counter ticked");
    }

    // ── C. Multiple acquisitions tick total counter ───────────────────────
    {
        AuditedStateMutex m;
        for (int i = 0; i < 10; ++i) {
            m.lock();
            m.unlock();
        }
        require(m.totalAcquisitionCount() == 10,
                "10 acquisitions ticked the counter to 10");
        require(m.rtViolationCount() == 0, "no violations from non-RT context");
    }

    // ── D. try_lock semantics + counter behavior ──────────────────────────
    {
        AuditedStateMutex m;
        require(m.try_lock(), "try_lock succeeds on free mutex");
        require(m.totalAcquisitionCount() == 1, "successful try_lock ticks counter");
        m.unlock();

        // try_lock on an already-locked mutex returns false; counter unchanged.
        m.lock();
        m.resetCountersForTesting();
        require(!m.try_lock(), "try_lock fails on locked mutex");
        require(m.totalAcquisitionCount() == 0,
                "failed try_lock does NOT tick total counter");
        m.unlock();
    }

    // ── E. RT-context acquisition triggers violation counter ──────────────
    {
        AuditedStateMutex m;
        sidRealtimeGuardResetForTest();
        SidRealtimeScope rtScope("audit #46 — render-thread simulation");
        require(sidRealtimeGuardActive(), "RT scope is now active");
        const uint64_t guardViolationsBefore = sidRealtimeGuardViolationCount();

        m.lock();   // ← this should record an RT violation
        m.unlock();

        const uint64_t guardViolationsAfter = sidRealtimeGuardViolationCount();
        require(m.rtViolationCount() == 1,
                "audit #46 — mutex's local RT violation counter ticked");
        require(guardViolationsAfter == guardViolationsBefore + 1,
                "global SidRealtimeGuard violation counter also ticked");
    }

    // ── F. RT-context try_lock also triggers (even on success) ────────────
    {
        AuditedStateMutex m;
        sidRealtimeGuardResetForTest();
        SidRealtimeScope rtScope("audit #46 — render-thread try_lock");
        require(m.try_lock(), "try_lock succeeds on free mutex even in RT scope");
        m.unlock();
        require(m.rtViolationCount() == 1,
                "audit #46 — RT try_lock recorded a violation");
    }

    // ── G. std::lock_guard works with AuditedMutex ────────────────────────
    {
        AuditedStateMutex m;
        sidRealtimeGuardResetForTest();
        {
            std::lock_guard<AuditedStateMutex> guard(m);
            // Inside scope: mutex is locked.
        }
        require(m.totalAcquisitionCount() == 1,
                "std::lock_guard acquired+released the AuditedMutex");
    }

    // ── G'. std::unique_lock works with AuditedMutex ──────────────────────
    {
        AuditedStateMutex m;
        sidRealtimeGuardResetForTest();
        std::unique_lock<AuditedStateMutex> ul(m);
        require(ul.owns_lock(),
                "std::unique_lock acquired the AuditedMutex");
        ul.unlock();
        require(!ul.owns_lock(),
                "std::unique_lock can release the AuditedMutex");
        ul.lock();
        require(ul.owns_lock(),
                "std::unique_lock can re-acquire the AuditedMutex");
        require(m.totalAcquisitionCount() == 2,
                "two acquisitions counted (initial + re-acquire)");
    }

    // ── H. Concurrent stress — counters increment atomically ──────────────
    {
        AuditedStateMutex m;
        constexpr int kThreadCount = 4;
        constexpr int kAcqPerThread = 1000;
        std::atomic<int> ready{0};
        std::thread threads[kThreadCount];
        for (int t = 0; t < kThreadCount; ++t) {
            threads[t] = std::thread([&]{
                ++ready;
                while (ready.load() < kThreadCount) { /* spin */ }
                for (int i = 0; i < kAcqPerThread; ++i) {
                    m.lock();
                    m.unlock();
                }
            });
        }
        for (auto& th : threads) th.join();
        require(m.totalAcquisitionCount() ==
                static_cast<std::uint64_t>(kThreadCount) * kAcqPerThread,
                "concurrent stress: counter is exactly thread_count × per-thread");
        require(m.rtViolationCount() == 0,
                "no violations under concurrent non-RT stress");
    }

    std::cout << "audited_mutex_v541_tests: audit #46 — AUv2 mutex policy contract pinned (compile + runtime)\n";
    return 0;
}
