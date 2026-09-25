// arpsid_telemetry_iface.h
// ArpSID — Telemetry interface shared between processor and controller
//
// MeterSnapshot: POD projection of the canonical telemetry snapshot.
// CanonicalTelemetryRing: lock-free handoff used only as the VST adapter
// transport; it is not a second telemetry authority.
// IArpSIDTelemetryProvider: abstract base the processor implements so the
// controller retrieves canonical snapshots without
// exposing the ring internals.
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#pragma once

// Pull in Steinberg portable calling-convention macros
#if defined(__cplusplus) && !defined(PLUGIN_API)
#  if defined(_WIN32) || defined(_WIN64)
#    define PLUGIN_API __stdcall
#  else
#    define PLUGIN_API
#  endif
#endif

// Pull in Steinberg portable types when available.
#ifdef __has_include
#  if __has_include("pluginterfaces/base/ftypes.h")
#    include "pluginterfaces/base/ftypes.h"
#    define ARPSID_HAVE_STEINBERG_FTYPES 1
#  endif
#endif

#if !defined(ARPSID_HAVE_STEINBERG_FTYPES) && !defined(ARPSID_HAVE_STEINBERG_TBOOL)
namespace Steinberg { using TBool = bool; }
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include "arpsid/core/scope_triple_buffer.h"
#include "common/arpsid_telemetry_snapshot.h"

#ifdef __cplusplus

namespace ArpSID {

// ─── MeterSnapshot ────────────────────────────────────────────────────────────
/// POD adapter projection of the canonical processor telemetry snapshot.
/// All fields are safe to read from any thread after atomic handoff.
struct MeterSnapshot {
    // Active-voice counts
    int  activeVoices  = 0;
    int  activeVoicesMax = 0;

    // VU meter (0..1)
    float peakL = 0.0f;
    float peakR = 0.0f;
    float rmsL  = 0.0f;
    float rmsR  = 0.0f;

    // MIDI activity
    bool  midiActivity = false;
    uint8_t lastMidiNote = 0;

    // Processor info
    float  sampleRate  = 44100.0f;
    int    bufferSize  = 512;
    uint64_t blockCounter = 0;

    // Arpeggiator state
    bool   arpPlaying  = false;
    int    arpStep     = 0;
    int    arpNoteCount= 0;

    // SID register shadow (for oscilloscope display)
    std::array<uint8_t, 0x1E> sidRegs{};

    void clear() noexcept { *this = MeterSnapshot{}; }
};

// ─── CanonicalTelemetryRing ──────────────────────────────────────────────────
/// Lock-free triple-buffer adapter for MeterSnapshot. This is the single VST
/// transport for canonical telemetry, not an independent telemetry model.
///
/// • Audio thread writes to the "write" slot (index = writeIdx).
/// • UI thread reads from the "read" slot (index = readIdx).
/// • The third slot is the "spare" used during atomic handoff.
///
/// No mutex, no blocking. Audio thread latency = single atomic exchange.
class CanonicalTelemetryRing {
public:
    void publish(const MeterSnapshot& snap) noexcept {
        triple_.writeSlot() = snap;
        triple_.publish();
        published_.store(true, std::memory_order_release);
    }

    bool consume(MeterSnapshot& out) noexcept {
        if (!published_.load(std::memory_order_acquire)) return false;
        triple_.peekLatest(out);
        return true;
    }

    bool loadLatest(MeterSnapshot& out) const noexcept {
        if (!published_.load(std::memory_order_acquire)) return false;
        triple_.peekLatest(out);
        return true;
    }

    bool loadLastConsumed(MeterSnapshot& out) const noexcept {
        // Shadow reads are stable-latest reads. They no longer intentionally
        // lag one GUI consume behind the render-published frame.
        return loadLatest(out);
    }

private:
    mutable ScopeTripleBuffer<MeterSnapshot> triple_{};
    std::atomic<bool> published_{false};
};

class FullTelemetryRing {
public:
    void publish(const ArpSIDTelemetry& snap) noexcept {
        triple_.writeSlot() = snap;
        triple_.publish();
        published_.store(true, std::memory_order_release);
    }

    bool loadLatest(ArpSIDTelemetry& out) const noexcept {
        if (!published_.load(std::memory_order_acquire)) return false;
        triple_.peekLatest(out);
        return true;
    }

private:
    mutable ScopeTripleBuffer<ArpSIDTelemetry> triple_{};
    std::atomic<bool> published_{false};
};

// ─── IArpSIDTelemetryProvider ─────────────────────────────────────────────────
/// Interface implemented by ArpSIDProcessorPhase2 and queried by the controller
/// via a VST3 queryInterface call. The ring is intentionally private; consumers
/// can only request coherent canonical snapshots.
struct IArpSIDTelemetryProvider {
    virtual ~IArpSIDTelemetryProvider() = default;

    /// Consume the latest snapshot. Returns kResultTrue if data was fresh.
    virtual Steinberg::TBool PLUGIN_API arpGetLatestSnapshot(MeterSnapshot& out) noexcept = 0;

    /// Get the controller-side shadow snapshot (last successfully consumed).
    virtual Steinberg::TBool PLUGIN_API arpGetTelemetryShadowSnapshot(MeterSnapshot& out) noexcept = 0;

    /// Read the same full presentation snapshot used by AU/standalone.
    virtual Steinberg::TBool PLUGIN_API
    arpGetLatestFullTelemetry(ArpSIDTelemetry& out,
                              Steinberg::TBool includeScopes) noexcept = 0;
};

} // namespace ArpSID

#endif // __cplusplus
