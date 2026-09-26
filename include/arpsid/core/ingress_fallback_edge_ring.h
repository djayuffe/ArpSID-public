// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// ingress_fallback_edge_ring.h — MPSC bounded ring for ordering-critical
// fallback events (Audit #24).
//
// PROBLEM
// ------
// The legacy ingress fallback in `SidRuntimeModel` collapsed MANY MIDI
// controllers into "latest value wins" atomic latches. That is correct for
// level-style controllers (CC1 mod-wheel, CC7 channel volume, CC11
// expression, channel pressure, pitch bend, poly pressure) but WRONG for
// ordering-critical events:
//
// * Sustain (CC64): a pedal stomp pattern down→up→down→up loses the
// intermediate transitions if only the latest value is kept.
// * Sostenuto (CC66): same.
// * RPN / NRPN sequences (CC101/100, CC99/98, CC6/38): the address-then// data-entry handshake collapses if any of the four CCs is reordered.
// * Transport edges (play → stop → play): collapses to just "currently
// playing y/n", losing the stop-rearm semantics.
// * Tempo discrete updates: tempo automation can pulse, and the audible
// micro-timing depends on the order events arrive.
//
// FIX
// --// A small bounded MPSC ring next to the existing latches. Producers that
// touch ordering-critical event types push a typed `IngressFallbackEdge`
// payload here. The consumer drains the ring FIRST in publish order, then
// continues with the existing per-channel level latches (which remain
// correct for level controllers).
//
// MPSC PROTOCOL (textbook per-slot sequence number)
// ------------------------------------------------
// Each slot owns its own `seq` atomic. Initially `slot[i].seq == i`.
//
// Producer (any thread):
// 1. pos = head.fetch_add(1)
// 2. spin while slot[pos % N].seq.load(acquire) != pos
// (if the spin would block the producer, the ring is full → record
// overflow and return false; we use a single non-blocking check
// since producers must not yield/wait under any condition)
// 3. write payload into slot
// 4. store slot[pos % N].seq = pos + 1, release
//
// Consumer (single thread, drain-time):
// 1. pos = tail (private, non-atomic)
// 2. if slot[pos % N].seq.load(acquire) != pos + 1: nothing ready, return
// 3. read payload
// 4. store slot[pos % N].seq = pos + N, release // re-arm slot for next epoch
// 5. tail = pos + 1, retry from step 1
//
// This is wait-free for the consumer and (modulo full-queue check) for
// the producer. No allocation, no locking.

#ifndef ARPSID_CORE_INGRESS_FALLBACK_EDGE_RING_H
#define ARPSID_CORE_INGRESS_FALLBACK_EDGE_RING_H

#include "arpsid/core/sid_event_queue.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace ArpSID {

// 16-byte payload — one ordering-critical fallback event.
// Mirrors enough of `SidTimedEvent` to losslessly reconstruct the source
// event on the consumer side without reaching into the runtime model.
struct IngressFallbackEdge {
    SidTimedEventType type        = SidTimedEventType::MidiCC;
    std::uint8_t      channel     = 0;
    std::uint8_t      ccNum       = 0;
    std::uint8_t      value7      = 0;      ///< 0..127 (sustain, sostenuto, RPN/NRPN, etc.)
    std::uint16_t     data14      = 0;      ///< for pitch bend / 14-bit data
    std::uint16_t     reserved    = 0;
    float             valueFloat  = 0.0f;   ///< tempo BPM / transport state
    std::uint32_t     producerSeq = 0;      ///< monotonic publisher tag (diagnostic)
};

static_assert(std::is_trivially_copyable<IngressFallbackEdge>::value,
              "IngressFallbackEdge must be trivially copyable");
static_assert(sizeof(IngressFallbackEdge) == 16,
              "IngressFallbackEdge layout pinned at 16 bytes");

template <std::size_t Capacity>
class IngressFallbackEdgeRing {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(Capacity >= 2 && Capacity <= 65536,
                  "Capacity must lie in [2, 65536]");
public:
    static constexpr std::size_t kCapacity = Capacity;

    IngressFallbackEdgeRing() noexcept { reset(); }

    void reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_ = 0;
        overflowCount_.store(0, std::memory_order_relaxed);
        for (std::size_t i = 0; i < kCapacity; ++i) {
            slots_[i].seq.store(static_cast<std::uint64_t>(i), std::memory_order_relaxed);
        }
    }

    // Multi-producer push (Vyukov bounded MPSC formulation). The producer
    // does NOT blindly fetch_add `head_` — instead it CAS-claims the slot
    // only when the per-slot sequence number proves the slot is free. This
    // is the only formulation that's both wait-free (per producer) and
    // bounded without ever corrupting a slot the consumer still owns.
    //
    // Returns false if the ring is full; callers are expected to record
    // the overflow so the level latches remain the audit-correct floor.
    bool push(const IngressFallbackEdge& payload) noexcept {
        std::uint64_t pos = head_.load(std::memory_order_acquire);
        for (;;) {
            Slot& slot = slots_[pos & (kCapacity - 1)];
            const std::uint64_t seq = slot.seq.load(std::memory_order_acquire);
            const std::int64_t diff = static_cast<std::int64_t>(seq) - static_cast<std::int64_t>(pos);
            if (diff == 0) {
                // Slot is ready for this producer to claim.
                if (head_.compare_exchange_weak(pos, pos + 1,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire)) {
                    slot.payload = payload;
                    slot.payload.producerSeq = static_cast<std::uint32_t>(pos);
                    slot.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed: another producer claimed this slot; re-read pos.
            } else if (diff < 0) {
                // Slot still belongs to the consumer's previous epoch → full.
                overflowCount_.fetch_add(1, std::memory_order_relaxed);
                return false;
            } else {
                // Another producer claimed this slot but hasn't committed
                // yet, or we lost a CAS race; reload head.
                pos = head_.load(std::memory_order_acquire);
            }
        }
    }

    // Single-consumer pop (Vyukov bounded MPSC formulation). Returns true
    // and writes `out` if a fresh slot is ready; returns false if the next
    // slot has not been committed yet (no producer has finished writing it).
    bool pop(IngressFallbackEdge& out) noexcept {
        Slot& slot = slots_[tail_ & (kCapacity - 1)];
        const std::uint64_t seq = slot.seq.load(std::memory_order_acquire);
        const std::int64_t diff = static_cast<std::int64_t>(seq) - static_cast<std::int64_t>(tail_ + 1);
        if (diff != 0) {
            return false; // not committed yet
        }
        out = slot.payload;
        // Re-arm this slot for the next epoch (capacity-rounds later).
        slot.seq.store(tail_ + kCapacity, std::memory_order_release);
        ++tail_;
        return true;
    }

    bool empty() const noexcept {
        const std::uint64_t headSnap = head_.load(std::memory_order_acquire);
        return tail_ == headSnap;
    }

    std::uint64_t overflowCount() const noexcept {
        return overflowCount_.load(std::memory_order_acquire);
    }

    // Diagnostic — number of committed slots not yet popped. Approximate.
    std::uint64_t approximateSize() const noexcept {
        const std::uint64_t h = head_.load(std::memory_order_acquire);
        return (h > tail_) ? (h - tail_) : 0u;
    }

private:
    struct Slot {
        std::atomic<std::uint64_t> seq{0};
        IngressFallbackEdge        payload{};
    };
    static_assert(std::is_trivially_copyable<IngressFallbackEdge>::value,
                  "payload must remain trivially copyable");

    std::array<Slot, Capacity> slots_{};
    std::atomic<std::uint64_t> head_{0};       // producer cursor
    std::uint64_t              tail_{0};        // consumer cursor (single-thread)
    std::atomic<std::uint64_t> overflowCount_{0};
};

} // namespace ArpSID

#endif // ARPSID_CORE_INGRESS_FALLBACK_EDGE_RING_H
