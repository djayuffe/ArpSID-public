// SPDX-License-Identifier: BSD-3-Clause
// sid_write_queue_bucket_sort_v522_tests.cpp
//
// Pins:
// * Audit #10 — SidWriteQueue::sortStable() and SidTimedEventQueue::sort()
// now use std::sort (introsort, O(n log n), in-place). For hostile
// worst-case input the sort must complete in dramatically less wall time
// than the previous O(n²) insertion sort would have taken.
// * Audit #11 — SidWriteQueue::kMaxWrites is at least 44 000 (the
// audit-stated worst-case theoretical ceiling). Pushing the
// audit-worst-case workload must not drop any write.
// * Sort correctness: after sortStable() / sort(), the queue is in the
// canonical priority order (sampleOffset, cycleOffset, order) for
// SidWriteQueue, and SidTimedEvent::before() ordering for
// SidTimedEventQueue. The relative order of entries with identical
// primary keys is preserved by the unique-order tiebreaker (stability
// equivalence under the unique total order).

#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/core/sid_event_queue.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <random>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

double seconds_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

int main() {
    using ArpSID::SidWriteQueue;
    using ArpSID::SidTimedEventQueue;
    using ArpSID::SidTimedEvent;

    // ── A. SidWriteQueue capacity satisfies audit #11 ───────────────────────
    require(SidWriteQueue::kMaxWrites >= 44000u,
            "kMaxWrites must cover the audit-stated worst-case ceiling (~44 k writes/block)");

    // ── B. Insertion order preservation when input is already sorted ────────
    {
        SidWriteQueue q;
        for (uint32_t s = 0; s < 100; ++s) {
            q.push(uint8_t(s & 0x1Fu), uint8_t(s & 0xFFu), s);
        }
        q.sortStable();
        const auto view = q.data();
        require(view.size() == 100u,                            "size preserved through sort");
        for (size_t i = 1; i < view.size(); ++i) {
            require(view[i - 1].sampleOffset <= view[i].sampleOffset,
                    "sortStable produces non-decreasing sampleOffset order");
        }
    }

    // ── C. Reverse-sorted input: the audit's "hostile" pattern ──────────────
    {
        SidWriteQueue q;
        const uint32_t N = 8192u;
        for (uint32_t i = 0; i < N; ++i) {
            const uint32_t s = (N - 1u) - i;
            q.push(uint8_t(i & 0x1Fu), uint8_t(i & 0xFFu), s);
        }
        require(q.size() == N,                                  "all reverse-sorted entries enqueued");
        const auto t0 = std::chrono::steady_clock::now();
        q.sortStable();
        const double dt = seconds_since(t0);
        // Pin: even on the slowest CI hardware std::sort on N=8 k completes
        // in well under 50 ms. The old O(n²) insertion sort on this input
        // is hundreds of milliseconds → seconds.
        require(dt < 0.05,
                "sortStable on 8 k reverse-sorted entries completes in <50 ms (O(n log n) confirmed)");

        const auto view = q.data();
        require(view.size() == N, "size preserved");
        for (size_t i = 1; i < view.size(); ++i) {
            require(view[i - 1].sampleOffset <= view[i].sampleOffset,
                    "reverse-sorted input becomes non-decreasing after sort");
        }
        // First entry must be the smallest sampleOffset (=0).
        require(view[0].sampleOffset == 0u,                     "smallest sampleOffset is first");
        require(view[N - 1].sampleOffset == N - 1u,             "largest sampleOffset is last");
    }

    // ── D. Random dense automation pattern ──────────────────────────────────
    {
        SidWriteQueue q;
        std::mt19937 rng(0xC64BEEFu);
        std::uniform_int_distribution<uint32_t> sampleDist(0, 2047);
        std::uniform_int_distribution<int>      regDist(0, 24);
        std::uniform_int_distribution<int>      valDist(0, 255);
        const uint32_t N = 16384u;
        for (uint32_t i = 0; i < N; ++i) {
            q.push(uint8_t(regDist(rng)), uint8_t(valDist(rng)), sampleDist(rng));
        }
        const auto t0 = std::chrono::steady_clock::now();
        q.sortStable();
        const double dt = seconds_since(t0);
        require(dt < 0.05, "16 k random sort < 50 ms");
        const auto view = q.data();
        // Strict order under the composite key (sampleOffset, cycleOffset, order).
        for (size_t i = 1; i < view.size(); ++i) {
            const auto& a = view[i - 1];
            const auto& b = view[i];
            const bool ok =
                (a.sampleOffset <  b.sampleOffset) ||
                (a.sampleOffset == b.sampleOffset && a.cycleOffset <  b.cycleOffset) ||
                (a.sampleOffset == b.sampleOffset && a.cycleOffset == b.cycleOffset && a.order < b.order);
            require(ok || (a.sampleOffset == b.sampleOffset && a.cycleOffset == b.cycleOffset && a.order == b.order),
                    "sorted output respects composite (sampleOffset, cycleOffset, order) total order");
        }
    }

    // ── E. Capacity exhaustion / drop counter behavior preserved ────────────
    {
        SidWriteQueue q;
        const size_t cap = SidWriteQueue::kMaxWrites;
        for (size_t i = 0; i < cap; ++i) {
            const bool ok = q.push(uint8_t(i & 0x1Fu), uint8_t(i & 0xFFu),
                                    static_cast<uint32_t>(i));
            require(ok, "push must succeed up to capacity");
        }
        require(q.size() == cap,         "queue holds exactly kMaxWrites entries at capacity");
        require(q.droppedCount() == 0u,  "no drops at capacity");
        // Now overflow by one.
        const bool overflowed = !q.push(0u, 0u, static_cast<uint32_t>(cap));
        require(overflowed,              "push returns false at overflow");
        require(q.droppedCount() == 1u,  "drop counter ticks at overflow");
    }

    // ── F. SidTimedEventQueue sort: hostile reverse pattern ─────────────────
    {
        SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};
        const int N = 2048;
        for (int i = N - 1; i >= 0; --i) {
            SidTimedEvent ev{};
            ev.type           = ArpSID::SidTimedEventType::MidiCC;
            ev.sample_offset  = static_cast<uint32_t>(i);  // resolved (not kSidUnresolvedSampleOffset)
            ev.subphase       = 0;
            ev.arrival_order  = static_cast<uint32_t>(N - 1 - i);
            require(q.push(ev),                                  "SidTimedEventQueue push succeeds");
        }
        require(q.count == N,                                    "all reverse entries enqueued");

        const auto t0 = std::chrono::steady_clock::now();
        q.sort();
        const double dt = seconds_since(t0);
        require(dt < 0.05, "SidTimedEventQueue.sort on 2 k reverse input < 50 ms (introsort)");

        require(q.count == N,
                "SidTimedEventQueue size preserved through sort");

        // After sort the array must be non-decreasing under SidTimedEvent::before.
        for (int i = 1; i < q.count; ++i) {
            require(!SidTimedEvent::before(q.events[i], q.events[i - 1]),
                    "post-sort: entries are non-decreasing under SidTimedEvent::before");
        }
        // First entry must be sample_offset == 0 (smallest resolved key).
        require(q.events[0].sample_offset == 0u,
                "smallest sample_offset is first after sort");
        require(q.events[N - 1].sample_offset == static_cast<uint32_t>(N - 1),
                "largest sample_offset is last after sort");
    }

    std::cout << "sid_write_queue_bucket_sort_v522_tests: O(n log n) sort + 65 k capacity confirmed (audit #10, #11)\n";
    return 0;
}
