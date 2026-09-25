#pragma once

#include "arpsid/core/c64_platform.h"
#include "arpsid/core/math_utils.h"
#include "arpsid/core/sid_event_queue.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

namespace ArpSID::C64 {

// Deterministic bridge used by SID projection engines (Synth/SIDReg/DrSID-808
// register projection) when they materialize an intended SID register write.
//
// IMPORTANT: C64Platform::sidRegisterImage() is the only mirror/state authority.
// This bridge never keeps or mutates a second shadow array. Immediate writes use
// C64Platform::cpuWrite($D400+reg). Timed writes are queued in C64Platform and
// become visible only when the PHI2 mirror reaches that cycle. GUI/HUD/telemetry
// therefore see the same $D400 bus semantics as PSID/RSID runtime and do not
// observe an eager side mirror that can disagree with the bus.
inline bool projectSidTimedWriteThroughC64Bus(C64Platform& platform,
                                              uint8_t reg,
                                              uint8_t value,
    uint64_t phi2Offset) noexcept {
    reg = static_cast<uint8_t>(reg & 0x1Fu);
    if (!c64SidRegWriteable(reg)) return false;
    const uint16_t address = static_cast<uint16_t>(0xD400u + reg);
    if (phi2Offset == 0u) {
        platform.cpuWrite(address, value);
        // Accepted by the C64 bus. PLA visibility decides whether this reaches
        // SID, character ROM, or RAM-under-IO; the bridge must not bypass that
        // decision by checking/mutating a second SID mirror.
        return true;
    }
    return platform.scheduleCpuWrite(address, value, phi2Offset);
}


inline uint64_t c64HostSampleOffsetToPhi2(uint16_t sampleOffset,
                                          uint16_t cycleOffset,
                                          double sampleRate,
                                          double phi2ClockHz) noexcept {
    // v899 split-brain fix: sample-only events carry the UNRESOLVED cycle
    // sentinel ($FFFF, kSidUnresolvedCycleOffset — the v890-v892 sample-only
    // ordering policy). The audio engine normalizes that sentinel to cycle 0
    // (sample boundary); this bridge previously added it RAW, scheduling the
    // C64 mirror write +65535 PHI2 cycles into the future — far beyond the
    // capped per-block mirror advance — where the v885 block-local cleanup
    // then deleted it. Net effect: every sample-only projection write vanished
    // and the SID projection mirror froze. Normalize exactly like the audio
    // path so both authorities agree the sentinel means "sample boundary".
    const uint16_t physicalCycleOffset =
        (cycleOffset == ArpSID::kSidUnresolvedCycleOffset) ? 0u : cycleOffset;
    if (!(std::isfinite(sampleRate) && sampleRate > 1.0) ||
        !(std::isfinite(phi2ClockHz) && phi2ClockHz > 1.0)) {
        return static_cast<uint64_t>(physicalCycleOffset);
    }
    const uint64_t q32 = ArpSID_cyclesPerSampleQ32(sampleRate, phi2ClockHz);
    const uint64_t whole = ArpSID_absoluteCycleAtSampleQ32(static_cast<uint64_t>(sampleOffset), q32);
    return whole + static_cast<uint64_t>(physicalCycleOffset);
}

inline bool projectSidHostTimedWriteThroughC64Bus(C64Platform& platform,
                                                  uint8_t reg,
                                                  uint8_t value,
                                                  uint16_t sampleOffset,
                                                  uint16_t cycleOffset,
                                                  double sampleRate,
                                                  double phi2ClockHz) noexcept {
    reg = static_cast<uint8_t>(reg & 0x1Fu);
    if (!c64SidRegWriteable(reg)) return false;
    const uint64_t phi2Offset = c64HostSampleOffsetToPhi2(sampleOffset, cycleOffset, sampleRate, phi2ClockHz);
    const uint16_t address = static_cast<uint16_t>(0xD400u + reg);
    return platform.scheduleProjectionMirrorCpuWrite(address, value, phi2Offset);
}

inline bool projectSidWriteThroughC64Bus(C64Platform& platform,
                                         uint8_t reg,
                                         uint8_t value) noexcept {
    reg = static_cast<uint8_t>(reg & 0x1Fu);
    if (!c64SidRegWriteable(reg)) return false;
    return projectSidTimedWriteThroughC64Bus(platform, reg, value, 0u);
}

// Reconciliation path used only to align the C64 bus mirror with an already
// rendered SID register image (for example after backend reset/restore). This is
// not a second audio or preset authority: it diffs against C64Platform's own SID
// register image and applies mismatches through cpuWrite($D400+reg).
inline bool reconcileSidRegisterImageThroughC64Bus(C64Platform& platform,
                                                   const uint8_t* regs,
                                                   int count,
                                                   uint8_t& lastReg,
                                                   uint8_t& lastValue) noexcept {
    if (!regs || count <= 0) return false;
    bool changed = false;
    const int n = count < 32 ? count : 32;
    const auto& image = platform.sidRegisterImage();
    for (int r = 0; r < n; ++r) {
        const uint8_t reg = static_cast<uint8_t>(r);
        if (!c64SidRegWriteable(reg)) continue;
        const uint8_t v = regs[r];
        if (image[(size_t)reg] == v) continue;
        if (projectSidWriteThroughC64Bus(platform, reg, v)) {
            lastReg = reg;
            lastValue = v;
            changed = true;
        }
    }
    return changed;
}

// Backward-compatible name retained for older source-shape tests. It now means
// reconciliation against C64Platform's own image, not mutation of a parallel
// mirror array.
inline bool projectSidRegisterImageThroughC64Bus(C64Platform& platform,
                                                 const uint8_t* regs,
                                                 int count,
                                                 uint8_t& lastReg,
                                                 uint8_t& lastValue) noexcept {
    return reconcileSidRegisterImageThroughC64Bus(platform, regs, count, lastReg, lastValue);
}

} // namespace ArpSID::C64
