// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// sidcore_panel_model.h — SIDCORE tab data model (v545).
//
// PURPOSE
// ------// The SIDCORE tab (docs/TAB_ARCHITECTURE.md §6) is a forensic debug surface
// that visualizes:
// * Last N SID register writes ($D400..$D41C) as a timeline
// * Per-voice frequency / pulse-width / waveform / gate scope
// * Filter cutoff / resonance / route-mode live scope
// * Hard-restart event markers
//
// This header is the **data-model layer** the NSView builder consumes.
// It is RT-safe by construction:
// * Fixed-size ring buffer for register writes (no allocation)
// * Atomic head/tail cursors (single producer = render thread, single
// consumer = GUI thread)
// * Wait-free read on GUI side via the seqlock-snapshot pattern
// mirroring the scope_triple_buffer.h approach (audit #14)
//
// The actual rendering is the NSView builder follow-up; this header
// pins the data contract so the GUI builder, the kernel-side feeder,
// and tests all agree on the wire format.

#ifndef ARPSID_GUI_SIDCORE_PANEL_MODEL_H
#define ARPSID_GUI_SIDCORE_PANEL_MODEL_H

#include "arpsid/core/scope_triple_buffer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace ArpSID {
namespace GUI {

// ─── Register-write log entry (16 bytes) ────────────────────────────────────
// One SID register write, captured at the moment the kernel emitted it.
// Producer (render thread) writes into the ring; consumer (GUI thread)
// reads via the seqlock-snapshot pattern.
struct SidCoreRegisterWriteEvent {
    std::uint64_t sidCycleStamp;  ///< Monotonic SID cycle at emission
    std::uint16_t registerIndex;  ///< 0..0x1C ($D400..$D41C); 0xFFFF = invalid
    std::uint8_t  value;          ///< Byte written
    std::uint8_t  voiceHint;      ///< 0..2 if the write belongs to a voice; 0xFF for filter/master
    std::uint8_t  flags;          ///< Event flag bits (see SidCoreEventFlag)
    std::uint8_t  reserved[3];    ///< Pad to 16 bytes
};
static_assert(std::is_trivially_copyable<SidCoreRegisterWriteEvent>::value,
              "SidCoreRegisterWriteEvent must be trivially copyable");
static_assert(sizeof(SidCoreRegisterWriteEvent) == 16,
              "SidCoreRegisterWriteEvent layout pinned at 16 bytes");

namespace SidCoreEventFlag {
inline constexpr std::uint8_t kGateOn         = 0x01; ///< $D404 bit 0 transitioned high
inline constexpr std::uint8_t kGateOff        = 0x02; ///< $D404 bit 0 transitioned low
inline constexpr std::uint8_t kHardRestart    = 0x04; ///< Test-bit triggered hard-restart
inline constexpr std::uint8_t kFilterMode     = 0x08; ///< $D418 filter-mode bits changed
inline constexpr std::uint8_t kWaveformChange = 0x10; ///< Waveform bits in $D404 changed
} // namespace SidCoreEventFlag

// ─── Live snapshot of per-voice + filter state (32 bytes) ──────────────────
// Updated every render block. The GUI consumes this via the triple-buffer
// pattern so the scope visualizer always sees a coherent snapshot even
// when the audio thread is mid-block.
struct SidCoreLiveSnapshot {
    std::uint16_t voiceFrequency[3];   ///< $D400/$D407/$D40E ($D401/$D408/$D40F MSB combined) — 6 bytes
    std::uint16_t voicePulseWidth[3];  ///< $D402..3 / $D409..A / $D410..1 (12-bit) — 6 bytes
    std::uint8_t  voiceWaveform[3];    ///< $D404 / $D40B / $D412 (gate + waveform bits) — 3 bytes
    std::uint8_t  voiceAD[3];          ///< $D405 / $D40C / $D413 — 3 bytes
    std::uint8_t  voiceSR[3];          ///< $D406 / $D40D / $D414 — 3 bytes
    std::uint8_t  pad0;                ///< explicit pad to make filterCutoff 2-byte aligned — 1 byte (22)
    std::uint16_t filterCutoff;        ///< $D415/$D416 11-bit cutoff — 2 bytes (24)
    std::uint8_t  filterResRoute;      ///< $D417 (res hi nibble + route lo nibble) — 1 byte (25)
    std::uint8_t  filterModeVolume;    ///< $D418 (mode hi nibble + vol lo nibble) — 1 byte (26)
    std::uint8_t  reserved[6];         ///< trailing pad to 32 bytes — 6 bytes
};
static_assert(std::is_trivially_copyable<SidCoreLiveSnapshot>::value,
              "SidCoreLiveSnapshot must be trivially copyable");
static_assert(sizeof(SidCoreLiveSnapshot) == 32,
              "SidCoreLiveSnapshot layout pinned at 32 bytes");

// ─── Register-write ring (RT-safe, fixed capacity) ──────────────────────────
// Bounded ring buffer for the last N register writes. Capacity = 256;
// older writes age out. Producer (render) increments `head`, consumer
// (GUI) reads from `tail` upward. No allocation, no locks.
//
// On overflow (producer outpaces consumer by > capacity), the consumer's
// oldest visible entries are silently lost — diagnostic counter ticks.
// This is acceptable because SIDCORE is a debug surface; tons of register
// traffic at the GUI is impossible to read live anyway.
template <std::size_t Capacity>
class SidCoreRegisterWriteRing {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "SidCoreRegisterWriteRing capacity must be a power of two");
    static_assert(Capacity >= 16 && Capacity <= 4096,
                  "Reasonable bounds: 16..4096 entries");
public:
    static constexpr std::size_t kCapacity = Capacity;

    // Producer (render thread).
    void push(const SidCoreRegisterWriteEvent& ev) noexcept {
        const std::uint64_t h = head_.load(std::memory_order_relaxed);
        slots_[h & (kCapacity - 1)] = ev;
        head_.store(h + 1u, std::memory_order_release);
    }

    // Consumer (GUI thread). Returns the number of new events drained,
    // up to `outCapacity`. Sets `*outOverflowed` to true if the consumer
    // fell more than `kCapacity` events behind (some events were lost).
    std::size_t drain(SidCoreRegisterWriteEvent* out,
                      std::size_t outCapacity,
                      bool* outOverflowed = nullptr) noexcept {
        const std::uint64_t h = head_.load(std::memory_order_acquire);
        std::uint64_t t = tail_;
        if (h <= t) { if (outOverflowed) *outOverflowed = false; return 0; }
        if ((h - t) > kCapacity) {
            // Consumer fell too far behind — skip to the oldest visible.
            ++lostEventCount_;
            t = h - kCapacity;
            if (outOverflowed) *outOverflowed = true;
        } else if (outOverflowed) {
            *outOverflowed = false;
        }
        std::size_t drained = 0;
        while (t < h && drained < outCapacity) {
            out[drained++] = slots_[t & (kCapacity - 1)];
            ++t;
        }
        tail_ = t;
        return drained;
    }

    void reset() noexcept {
        head_.store(0u, std::memory_order_relaxed);
        tail_ = 0u;
        lostEventCount_ = 0u;
    }

    std::uint64_t headForTesting() const noexcept { return head_.load(std::memory_order_acquire); }
    std::uint64_t tailForTesting() const noexcept { return tail_; }
    std::uint64_t lostEventCount() const noexcept { return lostEventCount_; }

private:
    std::array<SidCoreRegisterWriteEvent, kCapacity> slots_{};
    std::atomic<std::uint64_t> head_{0u};
    std::uint64_t              tail_{0u};
    std::uint64_t              lostEventCount_{0u};
};

using SidCoreRegisterWriteRing256 = SidCoreRegisterWriteRing<256>;

// ─── Top-level SIDCORE panel model ──────────────────────────────────────────
// Owns:
// * Register-write ring (RT-safe MPSC, drained by GUI)
// * Live snapshot triple-buffer (audit #14 pattern; producer = render,
// consumer = GUI, never blocks)
//
// The NSView builder (follow-up slice) consumes both:
// - `registerWriteRing.drain(out, N)` to render the timeline
// - `liveSnapshot.peekLatest(out)` to render per-voice scope
class SidCorePanelModel {
public:
    // Producer-side API (called from the kernel after each block).
    void publishRegisterWrite(const SidCoreRegisterWriteEvent& ev) noexcept {
        registerWriteRing_.push(ev);
    }
    void publishLiveSnapshot(const SidCoreLiveSnapshot& snap) noexcept {
        snapshotBuffer_.writeSlot() = snap;
        snapshotBuffer_.publish();
    }

    // Consumer-side API (called from the GUI thread).
    std::size_t drainRegisterWrites(SidCoreRegisterWriteEvent* out,
                                    std::size_t outCapacity,
                                    bool* outOverflowed = nullptr) noexcept {
        return registerWriteRing_.drain(out, outCapacity, outOverflowed);
    }
    void peekLiveSnapshot(SidCoreLiveSnapshot& out) noexcept {
        snapshotBuffer_.peekLatest(out);
    }

    void reset() noexcept {
        registerWriteRing_.reset();
        // The triple-buffer's reset would require re-initializing state;
        // for simplicity we treat reset as "clear ring + write a zeroed
        // snapshot so consumers see a clean baseline".
        SidCoreLiveSnapshot zero{};
        snapshotBuffer_.writeSlot() = zero;
        snapshotBuffer_.publish();
    }

    // Diagnostic accessors.
    std::uint64_t lostRegisterWriteCount() const noexcept {
        return registerWriteRing_.lostEventCount();
    }
    const SidCoreRegisterWriteRing256& registerWriteRingForTesting() const noexcept {
        return registerWriteRing_;
    }

private:
    SidCoreRegisterWriteRing256                 registerWriteRing_{};
    ScopeTripleBuffer<SidCoreLiveSnapshot>      snapshotBuffer_{};
};

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_SIDCORE_PANEL_MODEL_H
