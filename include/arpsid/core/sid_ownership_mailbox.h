// sid_ownership_mailbox.h
// ArpSID — RT-safe latest-value ownership mailbox.
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>

namespace ArpSID {

// Single-producer / single-consumer "latest value wins" mailbox that hands a
// heap-owning value object from a (serialized) non-realtime PRODUCER to a
// realtime CONSUMER without ever sharing a buffer between the two threads.
//
// WHY THIS EXISTS (audit P0-1):
// The previous state-root handoff was a version-counter seqlock whose RT reader
// *swapped* shared double-buffer slots. That makes the reader a writer of shared
// memory: if the producer "laps" a stalled reader (two publishes while the reader
// is preempted between selecting a slot and swapping it), the producer's slot
// assignment and the reader's swap touch the SAME SidStateRootV1 object
// concurrently — a data race / torn buffer (ABA). The post-swap version re-read
// detected the lap *after* the racing access had already happened (UB).
//
// THE FIX — ownership transfer, not slot swapping:
// Three buffers, three roles held at ALL times: one owned by the producer, one
// owned by the consumer, one parked in the mailbox. Ownership moves through a
// single atomic word; a buffer is NEVER accessed by both threads at once.
//   * The producer only ever writes its own private buffer, then publishes by
//     atomically exchanging it into the mailbox and reclaiming whatever was
//     parked there. A producer that laps a stalled consumer simply ping-pongs
//     between its own buffer and the parked one — it can never touch the buffer
//     the consumer currently owns.
//   * The consumer only ever reads/owns its private buffer. tryConsume() parks
//     the consumer's buffer and takes ownership of the freshly published one.
//
// THREAD CONTRACT:
//   producerSlot()/publish() — PRODUCER thread only. May allocate (it copies a
//     heap-owning T). Multiple producer threads must be serialized externally.
//   tryConsume()/hasPending() — CONSUMER (realtime) thread only. Wait-free, no
//     allocation, no spin, no shared-buffer writes.
//
// The atomic state word packs the parked buffer index (bits >=1) and a "fresh"
// flag (bit 0, set by the producer, cleared by the consumer on claim).
template <typename T, unsigned NSlots = 3u>
class OwnershipMailbox {
    static_assert(NSlots >= 3u,
                  "lock-free latest-value handoff needs >= 3 buffers");

public:
    OwnershipMailbox() noexcept : producerIndex_(0u), consumerIndex_(1u) {
        // Slots 0 and 1 are owned by producer/consumer respectively; park slot 2
        // (no fresh data yet).
        state_.store(packState_(2u, /*fresh=*/false), std::memory_order_relaxed);
    }

    OwnershipMailbox(const OwnershipMailbox&) = delete;
    OwnershipMailbox& operator=(const OwnershipMailbox&) = delete;

    // PRODUCER: the buffer to fill before calling publish().
    T& producerSlot() noexcept { return slots_[producerIndex_]; }

    // PRODUCER: publish the producer-owned buffer as the new latest value and
    // reclaim the previously parked buffer for the next fill.
    void publish() noexcept {
        const uint32_t parked = state_.exchange(
            packState_(producerIndex_, /*fresh=*/true), std::memory_order_acq_rel);
        producerIndex_ = indexOf_(parked);
    }

    // CONSUMER: if a fresh value is available, take ownership of it and return a
    // pointer to the consumer-owned buffer holding that latest value; otherwise
    // return nullptr. The returned buffer stays consumer-owned until the next
    // tryConsume() — the producer can never touch it.
    T* tryConsume() noexcept {
        // Cheap early-out; avoids an exchange when nothing is pending.
        if ((state_.load(std::memory_order_acquire) & kFreshBit_) == 0u)
            return nullptr;
        const uint32_t parked = state_.exchange(
            packState_(consumerIndex_, /*fresh=*/false), std::memory_order_acq_rel);
        const bool wasFresh = (parked & kFreshBit_) != 0u;
        // Always reclaim the parked buffer so producer/consumer indices stay
        // consistent with the three-buffer invariant, even on the (single-
        // consumer-impossible) lost-race path.
        consumerIndex_ = indexOf_(parked);
        return wasFresh ? &slots_[consumerIndex_] : nullptr;
    }

    // CONSUMER: cheap check (one atomic load) for a pending value.
    bool hasPending() const noexcept {
        return (state_.load(std::memory_order_acquire) & kFreshBit_) != 0u;
    }

private:
    static constexpr uint32_t kFreshBit_ = 1u;
    static constexpr uint32_t packState_(uint32_t idx, bool fresh) noexcept {
        return (idx << 1u) | (fresh ? kFreshBit_ : 0u);
    }
    static constexpr uint32_t indexOf_(uint32_t state) noexcept {
        return state >> 1u;
    }

    T slots_[NSlots]{};
    std::atomic<uint32_t> state_{};
    uint32_t producerIndex_;
    uint32_t consumerIndex_;
};

}  // namespace ArpSID
