// arpsid_bounded_mpsc_ring.h
// ArpSID — correct bounded lock-free MPSC ring (Dmitry Vyukov's bounded queue).
//
// Multiple producers (any thread), single consumer (the render thread). Unlike
// the older try-lock LockFreeRing, a push only fails when the ring is genuinely
// FULL — there is no "lost the producer lock" contention loss. Each cell carries
// a sequence number so producers reserve a slot with a single CAS and publish
// with a release store; the consumer reads with acquire. This is the canonical
// correct MPSC algorithm.
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ArpSID {

template<typename T, size_t Capacity>
class BoundedMpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "BoundedMpscRing: Capacity must be a power of two");
    static_assert(std::is_trivially_copyable<T>::value,
                  "BoundedMpscRing: T must be trivially copyable");
    static constexpr size_t kMask = Capacity - 1;

    struct Cell {
        std::atomic<size_t> sequence;
        T                   data;
    };

public:
    BoundedMpscRing() noexcept {
        for (size_t i = 0; i < Capacity; ++i)
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        enqueuePos_.store(0, std::memory_order_relaxed);
        dequeuePos_.store(0, std::memory_order_relaxed);
    }

    // Producer side — callable from ANY thread, concurrently. Returns false only
    // when the ring is full (never on contention).
    bool push(const T& item) noexcept {
        Cell* cell;
        size_t pos = enqueuePos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & kMask];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (enqueuePos_.compare_exchange_weak(pos, pos + 1,
                                                      std::memory_order_relaxed))
                    break;
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = enqueuePos_.load(std::memory_order_relaxed);
            }
        }
        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // Consumer side — SINGLE consumer only (the render thread).
    //
    // audit P0-6 (epoch boundary): rejects "straggler" items that were RESERVED
    // before the last clearEnqueuedBeforeNow() reset but PUBLISHED after it. Such an
    // item occupies a position before resetBarrier_; the monotonic enqueue position
    // IS the epoch tag, so no producer-side epoch read (which would race the reset)
    // is needed. Stale stragglers are silently recycled and skipped here so the
    // consumer never sees a pre-reset event leak across the reset boundary.
    bool pop(T& out) noexcept {
        size_t pos;
        while (popInternal_(out, pos)) {
            // Fresh iff the slot was reserved at/after the last reset barrier.
            if (static_cast<intptr_t>(pos - resetBarrier_) >= 0) return true;
            // Stale pre-reset straggler: popInternal_ already recycled the cell;
            // discard it and keep scanning.
        }
        return false;
    }

    bool empty() const noexcept {
        // Approximate (consumer-side hint only).
        const size_t deq = dequeuePos_.load(std::memory_order_acquire);
        const Cell& cell = cells_[deq & kMask];
        const size_t seq = cell.sequence.load(std::memory_order_acquire);
        return static_cast<intptr_t>(seq) - static_cast<intptr_t>(deq + 1) < 0;
    }

    // Consumer-side scrub: drain everything currently queued. Safe to call from
    // the single consumer even while producers push concurrently — it simply pops
    // what is present; concurrently-pushed items remain for the next drain. This
    // is the MPSC-correct replacement for a raw tail mutation.
    void drainAll() noexcept {
        T tmp;
        while (pop(tmp)) { /* discard */ }
    }

    // audit #1: SEMANTICALLY-ATOMIC clear for factory/root resets. drainAll() races
    // open-endedly with producers (it keeps popping items pushed *during* the scrub,
    // so its boundary is undefined). This instead snapshots the enqueue barrier once
    // and discards exactly the items that producers had committed by the call —
    // giving a clean before/after boundary: every event enqueued BEFORE the reset is
    // dropped, every event pushed AFTER it survives (so live notes during a patch
    // load are not lost). It never spins on an in-flight (reserved-but-uncommitted)
    // producer: hitting such a slot simply stops the scrub. Returns the number of
    // discarded items. Single-consumer only, like pop()/drainAll().
    size_t clearEnqueuedBeforeNow() noexcept {
        const size_t barrier = enqueuePos_.load(std::memory_order_acquire);
        size_t discarded = 0;
        T tmp;
        size_t poppedPos = 0;
        while (true) {
            const size_t deq = dequeuePos_.load(std::memory_order_relaxed);
            if (static_cast<intptr_t>(deq) - static_cast<intptr_t>(barrier) >= 0) break; // reached barrier
            // Use the raw pop here (not the public stale-skipping pop) so this scan
            // discards exactly the committed items it counts and stops cleanly at the
            // first reserved-but-uncommitted (in-flight producer) slot.
            if (!popInternal_(tmp, poppedPos)) break;
            ++discarded;
        }
        // audit P0-6: publish the epoch boundary. Any item still RESERVED at a
        // position < barrier (an in-flight producer that publishes after this point)
        // is now stale and will be rejected by pop(). Positions are monotonic, so
        // barrier never moves backward across resets.
        resetBarrier_ = barrier;
        return discarded;
    }

private:
    // Raw single-consumer dequeue: claims the next committed slot, copies it out,
    // recycles the cell, and reports the claimed position. Does NOT apply the
    // epoch-boundary (resetBarrier_) filter — that lives in the public pop().
    bool popInternal_(T& out, size_t& claimedPos) noexcept {
        Cell* cell;
        size_t pos = dequeuePos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &cells_[pos & kMask];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (diff == 0) {
                if (dequeuePos_.compare_exchange_weak(pos, pos + 1,
                                                      std::memory_order_relaxed))
                    break;
            } else if (diff < 0) {
                return false; // empty
            } else {
                pos = dequeuePos_.load(std::memory_order_relaxed);
            }
        }
        out = cell->data;
        cell->sequence.store(pos + Capacity, std::memory_order_release);
        claimedPos = pos;
        return true;
    }

    alignas(64) std::array<Cell, Capacity> cells_{};
    alignas(64) std::atomic<size_t>        enqueuePos_;
    alignas(64) std::atomic<size_t>        dequeuePos_;
    // Epoch boundary for clearEnqueuedBeforeNow(). Consumer-owned (only touched by
    // pop()/clearEnqueuedBeforeNow(), both single-consumer), so it needs no atomic.
    size_t resetBarrier_{0};

    // White-box test access (deterministic in-flight-straggler construction).
    template <typename U, std::size_t C> friend struct BoundedMpscRingTestAccess;
};

} // namespace ArpSID
