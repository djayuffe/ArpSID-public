// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "sid_event_queue.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace ArpSID {

static constexpr size_t kSidCriticalRingCapacity = 64; // power-of-2

inline bool sidIngressIsCriticalEvent(uint8_t eventTypeOrdinal) noexcept {
    // Ingress safety law: explicit semantic whitelist, not enum ordinal range.
    switch (static_cast<SidTimedEventType>(eventTypeOrdinal)) {
        case SidTimedEventType::Panic:
        case SidTimedEventType::AllSoundOff:
        case SidTimedEventType::AllNotesOff:
        case SidTimedEventType::MidiNoteOff:
        case SidTimedEventType::ProgramChange:
        case SidTimedEventType::VariantChange:
        case SidTimedEventType::TransportChange:
        case SidTimedEventType::TempoChange:
            return true;
        default:
            return false;
    }
}

template <typename T>
struct SidIngressEntry {
    T        event{};
    uint32_t sourceLocalSequence = 0;
    uint8_t  sourcePriority = 0;
};

template <typename T, size_t Capacity>
class SidIngressLane {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static_assert((kSidCriticalRingCapacity & (kSidCriticalRingCapacity-1)) == 0);
    static constexpr size_t kMask     = Capacity - 1;
    static constexpr size_t kCritMask = kSidCriticalRingCapacity - 1;

    SidIngressLane() noexcept { reset(); }

    bool push(const SidIngressEntry<T>& v) noexcept {
        // Ingress invariant: primary ingress is a bounded MPSC sequence ring. This
        // removes the old producer spin/try-lock and eliminates SPSC semantics
        // under multiple host/UI/MIDI producers.
        if (pushPrimaryMpsc_(v)) return true;
        const uint8_t ord = static_cast<uint8_t>(static_cast<int>(v.event.type));
        if (sidIngressIsCriticalEvent(ord)) return pushCritical_(v);
        overflow_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool pushForced(const SidIngressEntry<T>& v) noexcept { return push(v); }

    bool pop(SidIngressEntry<T>& out) noexcept {
        const uint32_t cr = critReadIndex_.load(std::memory_order_relaxed);
        const size_t critSlot = static_cast<size_t>(cr & kCritMask);
        const uint32_t critSeq = critSequence_[critSlot].load(std::memory_order_acquire);
        if (static_cast<int32_t>(critSeq - (cr + 1u)) == 0) {
            out = critBuffer_[critSlot];
            critSequence_[critSlot].store(cr + static_cast<uint32_t>(kSidCriticalRingCapacity), std::memory_order_release);
            critReadIndex_.store(cr + 1u, std::memory_order_release);
            return true;
        }

        const uint32_t r = readIndex_.load(std::memory_order_relaxed);
        const size_t slot = static_cast<size_t>(r & kMask);
        const uint32_t seq = primarySequence_[slot].load(std::memory_order_acquire);
        if (static_cast<int32_t>(seq - (r + 1u)) != 0) return false;
        out = buffer_[slot];
        primarySequence_[slot].store(r + static_cast<uint32_t>(Capacity), std::memory_order_release);
        readIndex_.store(r + 1u, std::memory_order_release);
        return true;
    }

    void reset() noexcept {
        readIndex_.store(0, std::memory_order_relaxed);
        writeIndex_.store(0, std::memory_order_relaxed);
        critReadIndex_.store(0, std::memory_order_relaxed);
        critWriteIndex_.store(0, std::memory_order_relaxed);
        overflow_.store(0, std::memory_order_relaxed);
        localSeq_.store(1, std::memory_order_relaxed);
        for (uint32_t i = 0; i < static_cast<uint32_t>(Capacity); ++i) {
            primarySequence_[i].store(i, std::memory_order_relaxed);
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(kSidCriticalRingCapacity); ++i) {
            critSequence_[i].store(i, std::memory_order_relaxed);
        }
    }

    // audit #12: 64-bit overflow telemetry so cumulative lane drops never wrap.
    uint64_t overflowCount() const noexcept { return overflow_.load(std::memory_order_relaxed); }
    uint64_t consumeOverflowCount() noexcept { return overflow_.exchange(0u, std::memory_order_relaxed); }
    uint32_t nextLocalSeq() noexcept { return localSeq_.fetch_add(1, std::memory_order_relaxed); }

    bool empty() const noexcept {
        const uint32_t pr = readIndex_.load(std::memory_order_acquire);
        const uint32_t pseq = primarySequence_[pr & kMask].load(std::memory_order_acquire);
        const bool primEmpty = static_cast<int32_t>(pseq - (pr + 1u)) != 0;
        const uint32_t cr = critReadIndex_.load(std::memory_order_acquire);
        const uint32_t seq = critSequence_[cr & kCritMask].load(std::memory_order_acquire);
        const bool critEmpty = static_cast<int32_t>(seq - (cr + 1u)) != 0;
        return primEmpty && critEmpty;
    }

private:
    bool pushPrimaryMpsc_(const SidIngressEntry<T>& v) noexcept {
        uint32_t cw = writeIndex_.load(std::memory_order_relaxed);
        for (;;) {
            const size_t slot = static_cast<size_t>(cw & kMask);
            const uint32_t seq = primarySequence_[slot].load(std::memory_order_acquire);
            const int32_t diff = static_cast<int32_t>(seq - cw);
            if (diff == 0) {
                if (writeIndex_.compare_exchange_weak(cw, cw + 1u,
                                                      std::memory_order_relaxed,
                                                      std::memory_order_relaxed)) {
                    buffer_[slot] = v;
                    primarySequence_[slot].store(cw + 1u, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                cw = writeIndex_.load(std::memory_order_relaxed);
            }
        }
    }

    bool pushCritical_(const SidIngressEntry<T>& v) noexcept {
        uint32_t cw = critWriteIndex_.load(std::memory_order_relaxed);
        for (;;) {
            const size_t slot = static_cast<size_t>(cw & kCritMask);
            const uint32_t seq = critSequence_[slot].load(std::memory_order_acquire);
            const int32_t diff = static_cast<int32_t>(seq - cw);
            if (diff == 0) {
                if (critWriteIndex_.compare_exchange_weak(cw, cw + 1u,
                                                          std::memory_order_relaxed,
                                                          std::memory_order_relaxed)) {
                    critBuffer_[slot] = v;
                    critSequence_[slot].store(cw + 1u, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                overflow_.fetch_add(1, std::memory_order_relaxed);
                return false;
            } else {
                cw = critWriteIndex_.load(std::memory_order_relaxed);
            }
        }
    }

    alignas(64) std::atomic<uint32_t> writeIndex_{0};
    alignas(64) std::atomic<uint32_t> readIndex_{0};
    alignas(64) SidIngressEntry<T> buffer_[Capacity]{};
    alignas(64) std::array<std::atomic<uint32_t>, Capacity> primarySequence_{};

    alignas(64) std::atomic<uint32_t> critWriteIndex_{0};
    alignas(64) std::atomic<uint32_t> critReadIndex_{0};
    alignas(64) SidIngressEntry<T> critBuffer_[kSidCriticalRingCapacity]{};
    alignas(64) std::array<std::atomic<uint32_t>, kSidCriticalRingCapacity> critSequence_{};

    alignas(64) std::atomic<uint64_t> overflow_{0};
    alignas(64) std::atomic<uint32_t> localSeq_{1};
};

enum class SidIngressSourcePriority : uint8_t {
    SystemSafety         = 0,
    TransportTempo       = 1,
    MidiNoteControl      = 2,
    AutomationRefinement = 3,
    DerivedFollowup      = 4,
};

} // namespace ArpSID
