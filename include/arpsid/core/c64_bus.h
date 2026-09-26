// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ArpSID::C64 {

static constexpr uint32_t kPalPhi2Hz = 985248u;
static constexpr uint32_t kNtscPhi2Hz = 1022727u;

struct BusCycle {
    uint16_t address = 0;
    uint8_t data = 0;
    bool rw = true; // true = read, false = write
    uint64_t phi2Cycle = 0;
    uint32_t order = 0; // stable CPU/VIC/CIA bus ordering inside the same PHI2 slot
    // Cosmetic projection-mirror events are block-local: they may be discarded
    // when the GUI C64 mirror is not advanced. Ordinary CPU/PSID/CIA events must
    // survive that discard. Keep the tag on the bus event itself so clearing is
    // selective instead of queue-wide.
    bool projectionMirror = false;
};

struct SidBusEvent {
    BusCycle cycle{};
    bool valid = false;
};

inline bool c64SidRegWriteable(uint8_t reg) noexcept {
    reg = static_cast<uint8_t>(reg & 0x1Fu);
    // Physical 6581/8580 exposes writable registers only at $D400-$D418.
    // $D419-$D41C are readable POTX/POTY/OSC3/ENV3, while $D41D-$D41F are
    // unmapped SID holes and must not reach the live register-write path.
    return reg <= 0x18u;
}

class SidBusQueue {
public:
    static constexpr size_t kCapacity = 1024;

    bool push(BusCycle cycle) noexcept {
        if (count_ >= kCapacity) compactConsumed_();
        if (count_ >= kCapacity) return false;
        cycle.order = orderCounter_++;
        events_[count_++] = SidBusEvent{cycle, true};
        sorted_ = false;
        return true;
    }

    void clear() noexcept { count_ = 0; sorted_ = true; cursor_ = 0; orderCounter_ = 0; }

    // v899 mirror-correctness fix: FLUSH pending projection-mirror events into
    // the caller-provided apply function (in stable bus order) instead of
    // silently dropping them. The v885 block-local law stands — projection
    // events never replay in a future block — but their VALUES describe writes
    // the audio engine already applied this block, so the cosmetic mirror's
    // register image must land on them. Dropping them (the pre-v899 behavior)
    // combined with the deliberate per-block mirror cycle cap meant most
    // same-block projection writes never reached the mirror at all and the
    // SID projection display froze. Timing beyond the advanced window is
    // sacrificed (already reported via the mirrorFidelity ratio); values are
    // not.
    template <class ApplyFn>
    void flushPendingProjectionMirrorEvents(ApplyFn&& apply) noexcept {
        if (count_ == 0) return;
        compactConsumed_();
        if (count_ == 0) return;
        sortStable();
        size_t write = 0;
        for (size_t read = 0; read < count_; ++read) {
            const bool projection = events_[read].valid && events_[read].cycle.projectionMirror;
            if (projection) {
                apply(events_[read].cycle);
                continue;
            }
            if (write != read) events_[write] = events_[read];
            ++write;
        }
        count_ = write;
        cursor_ = 0;
        rebasePendingOrder_();
        sorted_ = true;
    }

    void clearPendingProjectionMirrorEvents() noexcept {
        if (count_ == 0) return;
        // v879 closure: never sort a mixed consumed+pending range while using
        // cursor_ as the consumed boundary. Compact first so the remaining range
        // is purely pending; then selective discard cannot accidentally preserve
        // or drop the wrong event if future callers ever enqueue a same/past-PHI2
        // projection event after partial consumption. This also keeps the queue
        // bounded during long cockpit sessions instead of retaining consumed
        // projection entries until capacity pressure triggers compaction.
        compactConsumed_();
        if (count_ == 0) return;
        sortStable();
        size_t write = 0;
        for (size_t read = 0; read < count_; ++read) {
            const bool drop = events_[read].valid && events_[read].cycle.projectionMirror;
            if (drop) continue;
            if (write != read) events_[write] = events_[read];
            ++write;
        }
        count_ = write;
        cursor_ = 0;
        // v881 closure: pending ordinary events may have inherited large stable
        // order numbers from many previous discarded cockpit blocks. Rebase the
        // surviving pending range after selective clear so same-PHI2 ordering is
        // closed by construction for arbitrarily long sessions and future events
        // always sort after already-pending events. The range is already sorted,
        // so rewriting order in index order preserves all relative bus ordering.
        rebasePendingOrder_();
        sorted_ = true;
    }

    size_t size() const noexcept { return count_; }
    size_t cursor() const noexcept { return cursor_; }
    bool empty() const noexcept { return cursor_ >= count_; }

    void sortStable() noexcept {
        if (sorted_ || count_ <= 1) return;
        // ARPSID_RT_SORT_CLASSIFICATION: render-owned C64 bus queue, fixed
        // kCapacity storage, total-order noexcept comparator, no allocation.
        std::sort(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(count_),
                  [](const SidBusEvent& a, const SidBusEvent& b) noexcept { return less(a, b); });
        sorted_ = true;
    }

    const SidBusEvent* nextBeforeOrAt(uint64_t phi2Cycle) noexcept {
        sortStable();
        if (cursor_ >= count_) return nullptr;
        if (!events_[cursor_].valid || events_[cursor_].cycle.phi2Cycle > phi2Cycle) return nullptr;
        return &events_[cursor_++];
    }

    const SidBusEvent& at(size_t i) const noexcept { return events_[i]; }

private:
    static bool less(const SidBusEvent& a, const SidBusEvent& b) noexcept {
        if (a.cycle.phi2Cycle != b.cycle.phi2Cycle) return a.cycle.phi2Cycle < b.cycle.phi2Cycle;
        if (a.cycle.rw != b.cycle.rw) return !a.cycle.rw && b.cycle.rw; // write wins before read at same PHI2 edge
        return a.cycle.order < b.cycle.order;
    }

    void compactConsumed_() noexcept {
        if (cursor_ == 0) return;
        if (cursor_ >= count_) {
            count_ = 0;
            cursor_ = 0;
            sorted_ = true;
            // Do not let orderCounter_ grow forever after the queue has become
            // empty through consumption. Long-running cosmetic C64 sessions can
            // otherwise wrap the 32-bit same-PHI2 tie-breaker even though no
            // pending events remain.
            orderCounter_ = 0;
            return;
        }
        const size_t remaining = count_ - cursor_;
        for (size_t i = 0; i < remaining; ++i) events_[i] = events_[cursor_ + i];
        count_ = remaining;
        cursor_ = 0;
        // v883 closure: compaction is a general queue-maintenance operation,
        // not only a projection-clear precursor. Re-establish a compact stable
        // order space for all surviving pending events here as well, so ordinary
        // long-running CPU/CIA/PSID queues cannot inherit an unbounded 32-bit
        // tie-breaker after capacity-pressure compaction. If the pending range
        // was made unsorted by appends after partial consumption, sort once with
        // the old stable order first, then rebase the now-canonical pending
        // sequence to 0..N-1. Future same-PHI2 events will sort after survivors.
        sortStable();
        rebasePendingOrder_();
    }

    void rebasePendingOrder_() noexcept {
        for (size_t i = 0; i < count_; ++i) {
            events_[i].cycle.order = static_cast<uint32_t>(i);
        }
        orderCounter_ = static_cast<uint32_t>(count_);
    }

    std::array<SidBusEvent, kCapacity> events_{};
    size_t count_ = 0;
    size_t cursor_ = 0;
    uint32_t orderCounter_ = 0;
    bool sorted_ = true;
};

class C64MemoryMap {
public:
    uint8_t read(uint16_t addr) const noexcept { return ram_[addr]; }
    void write(uint16_t addr, uint8_t value) noexcept { ram_[addr] = value; }
    uint8_t& operator[](uint16_t addr) noexcept { return ram_[addr]; }
    const uint8_t& operator[](uint16_t addr) const noexcept { return ram_[addr]; }
private:
    std::array<uint8_t, 65536> ram_{};
};

} // namespace ArpSID::C64
