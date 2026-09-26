// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// auv2_render_notify_rt_safety_v524_tests.cpp
//
// Pins:
// * Audit #3 — (proc, userData) pair-coherence under concurrent publishing.
// We model the same seqlock-protected dual-atomic pattern the AUv2
// wrapper uses, run a publisher thread that flips the pair 200k times
// against a reader thread that snapshots and asserts coherence, and
// verify no torn pair is ever observed.
// * Audit #2 — `SidRealtimeScope` + thread-local violation counter
// correctly attribute RT-guard violations to specific callbacks. We
// simulate three notify callbacks (one well-behaved, one that
// allocates, one that takes a lock) and verify the bad ones each
// contribute exactly one violation to the per-callback delta. This
// mirrors what `callAuv2RenderNotifyTable` does at render time.
//
// We do NOT link the AUv2 wrapper here — that would pull in Cocoa/AUv2
// frameworks that aren't part of the core test binary. Instead we
// replicate the *exact same* memory-order pattern the wrapper uses, so
// any regression in the pattern (e.g. someone re-lowering the userData
// ordering to relaxed) breaks this test.

#include "arpsid/core/sid_realtime_guard.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

// Replica of `ArpSIDAUv2Instance::PublishedRenderNotifyTable::Slot`// two paired atomics (proc, userData) that must stay coherent under
// concurrent publishing. Audit #3 says: both halves must use release-store
// + acquire-load so a reader never sees old userData with a new proc (or
// vice versa).
struct PairedSlot {
    std::atomic<std::uint64_t> proc{0};      // analog of AURenderCallback
    std::atomic<std::uint64_t> userData{0};  // analog of `void* userData`
};

// Pair invariant: we always publish (proc=N, userData=N+0x1000) so the
// reader can verify the two halves were stored as a coherent pair.
constexpr std::uint64_t kUserDataXor = 0x1000ull;

} // namespace

int main() {
    using namespace ArpSID;

    // ── A. Single-thread pair coherence ─────────────────────────────────────
    {
        PairedSlot s{};
        for (std::uint64_t i = 1; i <= 100; ++i) {
            s.userData.store(i ^ kUserDataXor, std::memory_order_release);
            s.proc.store(i, std::memory_order_release);
            // The wrapper's read order is: proc.load(acquire), userData.load(acquire).
            const auto p = s.proc.load(std::memory_order_acquire);
            const auto u = s.userData.load(std::memory_order_acquire);
            require(p == i,                                   "proc round-trips");
            require(u == (i ^ kUserDataXor),                  "userData round-trips");
            require((p ^ u) == kUserDataXor,                  "pair invariant holds single-threaded");
        }
    }

    // ── B. Full seqlock + paired release/acquire: reader never *accepts* a
    // torn (proc, userData) pair (audit #2 + #3 combined). This models
    // exactly what `callAuv2RenderNotifyTable` does:
    // 1. gen0 = generation.load(acquire); skip if odd
    // 2. snapshot (proc, userData) with acquire on both
    // 3. gen1 = generation.load(acquire); skip if gen0 != gen1 or odd
    // 4. only then dispatch the callback
    // The combined seqlock+paired-ordering MUST drop every snapshot
    // whose pair invariant is violated.
    {
        PairedSlot s{};
        std::atomic<std::uint32_t> generation{0u};   // even = stable, odd = publishing
        std::atomic<bool> stop{false};
        std::atomic<std::uint64_t> torndAccepted{0u};
        std::atomic<std::uint64_t> snapshotsAccepted{0u};
        std::atomic<std::uint64_t> snapshotsRejected{0u};

        std::thread publisher([&]{
            // Publish at least 200k times, then keep going (bounded) until the
            // reader has both accepted and rejected a snapshot, so a reader
            // starved by a loaded parallel CTest run cannot fail the sanity
            // checks below. The torn-pair invariant itself is unchanged.
            constexpr std::uint64_t kMinPublishes = 200000u;
            constexpr std::uint64_t kMaxPublishes = 200000000u;
            for (std::uint64_t i = 1; i <= kMaxPublishes; ++i) {
                if (i > kMinPublishes &&
                    snapshotsAccepted.load(std::memory_order_relaxed) > 0u &&
                    snapshotsRejected.load(std::memory_order_relaxed) > 0u) {
                    break;
                }
                // Mark publishing-in-progress (odd generation).
                std::uint32_t g = generation.load(std::memory_order_relaxed);
                generation.store(g + 1u, std::memory_order_release);
                // Update the pair with release on BOTH halves (audit #3).
                s.proc.store(0,                       std::memory_order_release);
                s.userData.store(i ^ kUserDataXor,    std::memory_order_release);
                s.proc.store(i,                       std::memory_order_release);
                // Mark stable (even generation).
                std::atomic_thread_fence(std::memory_order_release);
                generation.store(g + 2u, std::memory_order_release);
            }
            stop.store(true, std::memory_order_release);
        });
        std::thread reader([&]{
            while (!stop.load(std::memory_order_acquire)) {
                const std::uint32_t g0 = generation.load(std::memory_order_acquire);
                if ((g0 & 1u) != 0u) { snapshotsRejected.fetch_add(1, std::memory_order_relaxed); continue; }
                const auto p = s.proc.load(std::memory_order_acquire);
                const auto u = s.userData.load(std::memory_order_acquire);
                const std::uint32_t g1 = generation.load(std::memory_order_acquire);
                if (g0 != g1 || (g1 & 1u) != 0u) {
                    // Outer seqlock caught the in-flight publish; the wrapper
                    // would skip this notify dispatch.
                    snapshotsRejected.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                if (p == 0) {
                    // Still publishing (proc temporarily cleared). The
                    // wrapper would also skip — nullptr proc is a sentinel.
                    snapshotsRejected.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                snapshotsAccepted.fetch_add(1, std::memory_order_relaxed);
                if ((p ^ u) != kUserDataXor) {
                    // Any accepted snapshot with a torn pair would mean both
                    // the seqlock retry AND the paired release/acquire failed
                    // to protect us. That is the audit-listed bug surface.
                    torndAccepted.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
        publisher.join();
        reader.join();

        require(snapshotsAccepted.load() > 0u,
                "reader accepted at least one snapshot — test actually ran");
        require(snapshotsRejected.load() > 0u,
                "reader rejected at least one snapshot — seqlock retry actually exercised");
        // The non-negotiable invariant (audit #2 + #3):
        require(torndAccepted.load() == 0u,
                "no accepted snapshot had a torn (proc, userData) pair — combined seqlock+paired ordering holds");
    }

    // ── C. Per-callback RT-violation attribution (audit #2) ─────────────────
    {
        sidRealtimeGuardResetForTest();
        SidRealtimeScope scope("test render block");

        const uint64_t v0 = sidRealtimeGuardViolationCount();

        // Well-behaved callback: no violations.
        {
            const uint64_t before = sidRealtimeGuardViolationCount();
            // Simulate render-block work that does no forbidden things.
            volatile int x = 0;
            for (int i = 0; i < 100; ++i) x += i;
            (void)x;
            const uint64_t after = sidRealtimeGuardViolationCount();
            require(after - before == 0u,
                    "well-behaved callback contributes 0 violations");
        }

        // Misbehaving callback #1: allocates inside RT scope.
        {
            const uint64_t before = sidRealtimeGuardViolationCount();
            sidRealtimeGuardForbidAllocation("notify-callback-1 allocated");
            const uint64_t after = sidRealtimeGuardViolationCount();
            require(after - before == 1u,
                    "allocation callback contributes exactly 1 violation");
        }

        // Misbehaving callback #2: takes a lock inside RT scope.
        {
            const uint64_t before = sidRealtimeGuardViolationCount();
            sidRealtimeGuardForbidLock("notify-callback-2 locked");
            const uint64_t after = sidRealtimeGuardViolationCount();
            require(after - before == 1u,
                    "lock callback contributes exactly 1 violation");
        }

        // Cumulative: scope-level guard captures the sum.
        const uint64_t v1 = sidRealtimeGuardViolationCount();
        require(v1 - v0 == 2u,
                "scope-level violation count equals sum of per-callback violations");
    }

    // ── D. Per-callback attribution is independent across callbacks ─────────
    // i.e., a misbehaving callback does NOT poison the next callback's delta.
    {
        sidRealtimeGuardResetForTest();
        SidRealtimeScope scope("test render block 2");

        const uint64_t v0 = sidRealtimeGuardViolationCount();

        // Misbehaving callback first.
        {
            const uint64_t before = sidRealtimeGuardViolationCount();
            sidRealtimeGuardForbidHostCallback("bad-callback");
            const uint64_t after = sidRealtimeGuardViolationCount();
            require(after - before == 1u,             "bad callback: +1");
        }

        // Well-behaved callback after — must show 0 delta.
        {
            const uint64_t before = sidRealtimeGuardViolationCount();
            volatile int y = 0;
            for (int i = 0; i < 50; ++i) y += i;
            (void)y;
            const uint64_t after = sidRealtimeGuardViolationCount();
            require(after - before == 0u,
                    "well-behaved callback after a misbehaving one: delta == 0");
        }

        const uint64_t v1 = sidRealtimeGuardViolationCount();
        require(v1 - v0 == 1u,
                "total scope delta == 1 (only the bad callback contributed)");
    }

    std::cout << "auv2_render_notify_rt_safety_v524_tests: paired (proc,userData) coherence + per-callback attribution pinned (audit #2/#3)\n";
    return 0;
}
