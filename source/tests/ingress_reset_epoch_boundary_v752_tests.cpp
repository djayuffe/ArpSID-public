// Copyright (C) 2024-2026 Ulf Bertilsson
// ingress_reset_epoch_boundary_v752_tests.cpp
//
// audit P0-6 / fix-order #5: clearEnqueuedBeforeNow() must be a true reset
// boundary. The Vyukov MPSC producer reserves a slot (CAS on enqueuePos_) and
// only later publishes it (sequence store). A producer that RESERVED a slot
// before the reset but PUBLISHES it after must NOT leak across the boundary — the
// consumer must reject that stale straggler.
//
// These are behavioral tests, not source-string checks. The white-box
// BoundedMpscRingTestAccess friend deterministically reproduces the in-flight
// straggler (reserve, then reset, then publish) which is otherwise only reachable
// under a precise concurrent interleaving.

#include "arpsid_bounded_mpsc_ring.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace ArpSID {
// Friend test access: split the producer's atomic push into its two real phases
// (reserve a slot without publishing; publish a previously reserved slot) so the
// "reserved before reset, published after" race is deterministic.
template <typename U, std::size_t C>
struct BoundedMpscRingTestAccess {
    static std::size_t reserveSlot(BoundedMpscRing<U, C>& r) {
        std::size_t pos = r.enqueuePos_.load(std::memory_order_relaxed);
        for (;;) {
            auto& cell = r.cells_[pos & r.kMask];
            const std::size_t seq = cell.sequence.load(std::memory_order_acquire);
            const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (r.enqueuePos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                    return pos;
            } else if (diff < 0) {
                return SIZE_MAX;  // full
            } else {
                pos = r.enqueuePos_.load(std::memory_order_relaxed);
            }
        }
    }
    static void publishSlot(BoundedMpscRing<U, C>& r, std::size_t pos, const U& item) {
        auto& cell = r.cells_[pos & r.kMask];
        cell.data = item;
        cell.sequence.store(pos + 1, std::memory_order_release);
    }
};
}  // namespace ArpSID

namespace {
struct Ev { int id; };
static_assert(std::is_trivially_copyable<Ev>::value, "Ev must be trivially copyable");

void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
using Ring = ArpSID::BoundedMpscRing<Ev, 8>;
using TA = ArpSID::BoundedMpscRingTestAccess<Ev, 8>;
}  // namespace

// Sanity: committed pre-reset items are discarded; post-reset items flow.
static void testBasicResetBoundary() {
    Ring r;
    require(r.push({1}) && r.push({2}) && r.push({3}), "push three committed");
    require(r.clearEnqueuedBeforeNow() == 3u, "clear discards the three committed items");
    Ev out{};
    require(!r.pop(out), "ring empty after clearing all committed items");
    require(r.push({10}), "post-reset push");
    require(r.pop(out) && out.id == 10, "post-reset event is delivered");
    require(!r.pop(out), "no extra events");
}

// THE FIX: a slot reserved before the reset but published after must be rejected.
static void testStragglerRejected() {
    Ring r;
    require(r.push({1}) && r.push({2}), "two committed before reset");
    const std::size_t inflight = TA::reserveSlot(r);  // reserved, NOT yet published
    require(inflight != SIZE_MAX, "reserved an in-flight slot");

    const std::size_t cleared = r.clearEnqueuedBeforeNow();
    require(cleared == 2u, "clear discards the committed items and stops at the in-flight slot");

    // Straggler publishes AFTER the reset boundary.
    TA::publishSlot(r, inflight, Ev{99});

    Ev out{};
    require(!r.pop(out),
            "straggler reserved before the reset is rejected (true epoch boundary)");

    // A genuine post-reset event must still flow.
    require(r.push({7}), "post-reset push after straggler");
    require(r.pop(out) && out.id == 7, "post-reset event survives the boundary");
    require(!r.pop(out), "nothing else queued");
}

// Straggler interleaved with a real post-reset item: skip the stale one, deliver
// only the fresh one (proves pop() filters per-item, not all-or-nothing).
static void testStragglerSkippedFreshDelivered() {
    Ring r;
    require(r.push({1}), "one committed");
    const std::size_t inflight = TA::reserveSlot(r);  // reserved pre-reset
    require(inflight != SIZE_MAX, "reserved in-flight slot");
    require(r.clearEnqueuedBeforeNow() == 1u, "clear discards the one committed item");

    require(r.push({50}), "genuine post-reset event");      // position >= barrier
    TA::publishSlot(r, inflight, Ev{88});                   // straggler publishes late

    Ev out{};
    require(r.pop(out) && out.id == 50,
            "straggler skipped; only the post-reset event is delivered");
    require(!r.pop(out), "exactly one fresh event delivered");
}

// Multiple resets: the barrier is monotonic; older stragglers stay rejected.
static void testMonotonicBarrier() {
    Ring r;
    require(r.push({1}), "seed");
    const std::size_t s1 = TA::reserveSlot(r);
    r.clearEnqueuedBeforeNow();          // barrier B1
    require(r.push({2}), "post-B1 event");
    const std::size_t s2 = TA::reserveSlot(r);
    r.clearEnqueuedBeforeNow();          // barrier B2 > B1 (also drops the {2} not yet popped)
    // Both stragglers (reserved before their respective barriers) publish late.
    TA::publishSlot(r, s1, Ev{111});
    TA::publishSlot(r, s2, Ev{222});
    Ev out{};
    require(!r.pop(out), "both pre-barrier stragglers rejected");
    require(r.push({3}) && r.pop(out) && out.id == 3, "fresh event after two resets flows");
}

int main() {
    testBasicResetBoundary();
    testStragglerRejected();
    testStragglerSkippedFreshDelivered();
    testMonotonicBarrier();
    std::cout << "IngressResetEpochBoundaryV752Tests PASS\n";
    return 0;
}
