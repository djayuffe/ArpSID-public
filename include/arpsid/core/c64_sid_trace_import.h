// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/engines/sid_register_engine.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace ArpSID::C64 {

// Offline/host-side SID trace event. Cycles are absolute C64 PHI2 cycles, not
// audio sample offsets. The physical address convention is C64 $D400 register
// space: chip 0/reg $18 means $D418, chip 1/reg $18 means the configured second
// SID base + $18.
struct C64SidTraceEvent final {
    uint64_t phi2Cycle = 0;
    uint8_t chip = 0;
    uint8_t reg = 0;
    uint8_t value = 0;
};

struct C64SidTraceImportStats final {
    size_t eventsSeen = 0;
    size_t eventsImported = 0;
    size_t eventsRejected = 0;
    size_t queueDropped = 0;
    size_t d418EventsImported = 0;
    size_t repeatedD418EventsImported = 0;
    uint64_t firstCycle = 0;
    uint64_t lastCycle = 0;
    bool monotonic = true;
    bool complete = false;
};

inline bool c64SidTraceEventValid(const C64SidTraceEvent& e) noexcept {
    return e.chip < 5u && (e.reg & 0x1Fu) == e.reg && c64SidRegWriteable(e.reg);
}

inline uint16_t c64SidFrequencyRegisterFromHz(double hz, double clockHz) noexcept {
    if (!(hz > 0.0) || !(clockHz > 0.0) || !std::isfinite(hz) || !std::isfinite(clockHz)) return 0;
    const double scaled = hz * 16777216.0 / clockHz;
    const long v = std::lround(std::clamp(scaled, 0.0, 65535.0));
    return static_cast<uint16_t>(v & 0xFFFFu);
}

inline uint8_t c64SidTraceDefaultWaveForMidiChannel(uint8_t midiChannel) noexcept {
    // TS-compatible coarse mapping: ch0 pulse, ch1 saw, ch9 noise drums, other triangle.
    if (midiChannel == 0u) return 0x40u;
    if (midiChannel == 1u) return 0x20u;
    if (midiChannel == 9u) return 0x80u;
    return 0x10u;
}

inline C64SidTraceImportStats importC64SidTraceToBridge(C64SidBridgeState& bridge,
                                                       const C64SidTraceEvent* events,
                                                       size_t count) noexcept {
    C64SidTraceImportStats s{};
    if (!events && count != 0u) return s;
    uint64_t prevCycle = 0;
    uint8_t prevD418Value[5] = {0,0,0,0,0};
    bool prevD418Seen[5] = {false,false,false,false,false};
    for (size_t i = 0; i < count; ++i) {
        const C64SidTraceEvent& e = events[i];
        ++s.eventsSeen;
        if (i == 0u) { s.firstCycle = e.phi2Cycle; prevCycle = e.phi2Cycle; }
        else if (e.phi2Cycle < prevCycle) s.monotonic = false;
        prevCycle = e.phi2Cycle;
        s.lastCycle = e.phi2Cycle;
        if (!c64SidTraceEventValid(e)) { ++s.eventsRejected; continue; }
        const uint8_t flat = static_cast<uint8_t>(e.chip * 32u + (e.reg & 0x1Fu));
        bridge.sidWrite(flat, e.value, e.phi2Cycle);
        ++s.eventsImported;
        if ((e.reg & 0x1Fu) == 0x18u) {
            ++s.d418EventsImported;
            if (prevD418Seen[e.chip] && prevD418Value[e.chip] == e.value) ++s.repeatedD418EventsImported;
            prevD418Seen[e.chip] = true;
            prevD418Value[e.chip] = e.value;
        }
    }
    s.complete = s.eventsSeen == count && s.eventsRejected == 0u && s.monotonic;
    return s;
}

inline C64SidTraceImportStats importC64SidTraceToQueue(ArpSID::SidWriteQueue& queue,
                                                      const C64SidTraceEvent* events,
                                                      size_t count,
                                                      uint64_t blockStartPhi2,
                                                      double c64ClockHz,
                                                      double hostSampleRate,
                                                      uint32_t hostFrames) noexcept {
    C64SidTraceImportStats s{};
    if (!events && count != 0u) return s;
    if (!(c64ClockHz > 0.0) || !(hostSampleRate > 0.0) || hostFrames == 0u) return s;
    uint64_t prevCycle = 0;
    for (size_t i = 0; i < count; ++i) {
        const C64SidTraceEvent& e = events[i];
        ++s.eventsSeen;
        if (i == 0u) { s.firstCycle = e.phi2Cycle; prevCycle = e.phi2Cycle; }
        else if (e.phi2Cycle < prevCycle) s.monotonic = false;
        prevCycle = e.phi2Cycle;
        s.lastCycle = e.phi2Cycle;
        if (!c64SidTraceEventValid(e) || e.phi2Cycle < blockStartPhi2) { ++s.eventsRejected; continue; }
        const uint64_t relCycle = e.phi2Cycle - blockStartPhi2;
        const double sampleD = static_cast<double>(relCycle) * hostSampleRate / c64ClockHz;
        if (!(sampleD >= 0.0) || sampleD >= static_cast<double>(hostFrames)) { ++s.eventsRejected; continue; }
        const uint32_t sampleOffset = static_cast<uint32_t>(std::floor(sampleD));
        const double sampleStartCycle = static_cast<double>(sampleOffset) * c64ClockHz / hostSampleRate;
        const double intra = std::max(0.0, static_cast<double>(relCycle) - sampleStartCycle);
        const uint16_t cycleOffset = static_cast<uint16_t>(std::clamp(std::lround(intra), 0l, 65535l));
        const uint8_t flat = static_cast<uint8_t>(e.chip * 32u + (e.reg & 0x1Fu));
        if (!queue.push(flat, e.value, sampleOffset, cycleOffset)) { ++s.queueDropped; continue; }
        ++s.eventsImported;
        if ((e.reg & 0x1Fu) == 0x18u) ++s.d418EventsImported;
    }
    s.complete = s.eventsSeen == count && s.eventsRejected == 0u && s.queueDropped == 0u && s.monotonic;
    return s;
}

} // namespace ArpSID::C64
