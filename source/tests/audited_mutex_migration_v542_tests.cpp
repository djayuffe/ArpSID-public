// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// audited_mutex_migration_v542_tests.cpp
//
// Pins the v542 migration: AUv2's 4 instance mutexes
// (`activityMutex`, `stateMutex`, `propertyListenerMutex`, `closeWaitMutex`)
// are now `AuditedXxxMutex` instead of plain `std::mutex`. The migration
// is **drop-in API-compatible** — every existing `std::lock_guard` /
// `std::unique_lock` call site continues to work — but each lock attempt
// now passes through the audit's RT-violation detector.
//
// We can't link the AUv2 component from this test (Cocoa). The migration
// is verified by:
// 1. The AUv2 build target compiling (already proves drop-in compat).
// 2. This test exercising the same lock_guard / unique_lock patterns
// the wrapper uses, against the canonical AUv2 mutex aliases.
// 3. Confirming the migration's RT-detector survives concurrent
// condition-variable usage (closeWaitMutex + condition_variable_any).

#include "arpsid/core/audited_mutex.h"
#include "arpsid/core/sid_realtime_guard.h"

#include <atomic>
#include <condition_variable>
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

    // ── A. The 4 canonical AUv2 mutex types are all `NonRealtime` ─────────
    {
        static_assert(AuditedActivityMutex::policy()         == AuditedMutexPolicy::NonRealtime, "");
        static_assert(AuditedStateMutex::policy()            == AuditedMutexPolicy::NonRealtime, "");
        static_assert(AuditedPropertyListenerMutex::policy() == AuditedMutexPolicy::NonRealtime, "");
        static_assert(AuditedCloseWaitMutex::policy()        == AuditedMutexPolicy::NonRealtime, "");
        AuditedActivityMutex a; AuditedStateMutex b;
        AuditedPropertyListenerMutex c; AuditedCloseWaitMutex d;
        require(a.policy() == AuditedMutexPolicy::NonRealtime, "activity policy NonRT");
        require(b.policy() == AuditedMutexPolicy::NonRealtime, "state policy NonRT");
        require(c.policy() == AuditedMutexPolicy::NonRealtime, "listener policy NonRT");
        require(d.policy() == AuditedMutexPolicy::NonRealtime, "close-wait policy NonRT");
    }

    // ── B. std::lock_guard drop-in compat for each AUv2 mutex alias ───────
    {
        AuditedActivityMutex activityMutex;
        AuditedStateMutex stateMutex;
        AuditedPropertyListenerMutex propertyListenerMutex;
        AuditedCloseWaitMutex closeWaitMutex;

        // Mirror the wrapper's exact patterns.
        {
            std::lock_guard<AuditedActivityMutex> lock(activityMutex);
            (void)lock;
        }
        {
            std::lock_guard<AuditedStateMutex> lock(stateMutex);
            (void)lock;
        }
        {
            std::lock_guard<AuditedPropertyListenerMutex> lock(propertyListenerMutex);
            (void)lock;
        }
        {
            std::unique_lock<AuditedCloseWaitMutex> lock(closeWaitMutex);
            (void)lock;
        }
        // Each was acquired+released exactly once.
        require(activityMutex.totalAcquisitionCount() == 1,         "activity counter == 1");
        require(stateMutex.totalAcquisitionCount() == 1,            "state counter == 1");
        require(propertyListenerMutex.totalAcquisitionCount() == 1, "listener counter == 1");
        require(closeWaitMutex.totalAcquisitionCount() == 1,        "closeWait counter == 1");
        // No violations from non-RT context.
        require(activityMutex.rtViolationCount() == 0,         "activity: no RT violations");
        require(stateMutex.rtViolationCount() == 0,            "state: no RT violations");
        require(propertyListenerMutex.rtViolationCount() == 0, "listener: no RT violations");
        require(closeWaitMutex.rtViolationCount() == 0,        "closeWait: no RT violations");
    }

    // ── C. std::unique_lock + std::condition_variable_any pattern ─────────
    //
    // The wrapper changed `closeWaitCv` from `std::condition_variable` to
    // `std::condition_variable_any` so it could pair with AuditedCloseWaitMutex.
    // Exercise the same wait/notify pattern the wrapper uses.
    {
        AuditedCloseWaitMutex closeWaitMutex;
        std::condition_variable_any closeWaitCv;
        std::atomic<int> sharedState{0};

        std::thread waiter([&]{
            std::unique_lock<AuditedCloseWaitMutex> waitLock(closeWaitMutex);
            closeWaitCv.wait(waitLock, [&] { return sharedState.load() == 1; });
            // wait() releases + re-acquires the lock; counter sees both.
        });
        // Let the waiter establish the wait.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        sharedState.store(1);
        {
            std::lock_guard<AuditedCloseWaitMutex> lock(closeWaitMutex);
            // notify_all without explicit lock requirement; but pattern
            // matches wrapper usage.
        }
        closeWaitCv.notify_all();
        waiter.join();
        require(closeWaitMutex.totalAcquisitionCount() >= 2,
                "closeWait acquired at least twice (wait + notify pattern)");
        require(closeWaitMutex.rtViolationCount() == 0,
                "no RT violations during cv-wait pattern");
    }

    // ── D. AUv2-style mixed-mutex acquire ordering compiles + runs ────────
    //
    // Mirror the wrapper's pattern at line 2526-2527:
    // std::unique_lock<...> activityLock(impl->activityMutex);
    // std::lock_guard<...> lock(impl->stateMutex);
    //
    // (state acquired while activity already held — this nested pattern
    // is what the wrapper uses for compound configuration changes.)
    {
        AuditedActivityMutex activityMutex;
        AuditedStateMutex stateMutex;
        {
            std::unique_lock<AuditedActivityMutex> activityLock(activityMutex);
            {
                std::lock_guard<AuditedStateMutex> lock(stateMutex);
                (void)lock;
            }
        }
        require(activityMutex.totalAcquisitionCount() == 1, "activity acquired once");
        require(stateMutex.totalAcquisitionCount() == 1,    "state acquired once");
    }

    // ── E. The migration preserves audit-#46 RT-violation detection ───────
    //
    // This is the critical end-to-end pin: a render thread (simulated via
    // SidRealtimeScope) attempting to take any of the 4 AUv2 mutexes
    // increments the violation counter. Each mutex is tested independently.
    {
        sidRealtimeGuardResetForTest();
        AuditedActivityMutex am;
        AuditedStateMutex sm;
        AuditedPropertyListenerMutex pm;
        AuditedCloseWaitMutex cm;
        {
            SidRealtimeScope rtScope("audit #46 — render thread mistakenly locking AUv2 mutex");
            std::lock_guard<AuditedActivityMutex> a(am);
            std::lock_guard<AuditedStateMutex> b(sm);
            std::lock_guard<AuditedPropertyListenerMutex> c(pm);
            std::lock_guard<AuditedCloseWaitMutex> d(cm);
        }
        require(am.rtViolationCount() == 1, "activity: RT violation detected");
        require(sm.rtViolationCount() == 1, "state: RT violation detected");
        require(pm.rtViolationCount() == 1, "listener: RT violation detected");
        require(cm.rtViolationCount() == 1, "closeWait: RT violation detected");
        require(sidRealtimeGuardViolationCount() >= 4,
                "global SidRealtimeGuard counter received 4+ violations");
    }

    std::cout << "audited_mutex_migration_v542_tests: AUv2 4-mutex migration to AuditedMutex pinned (audit #46 closed end-to-end)\n";
    return 0;
}
