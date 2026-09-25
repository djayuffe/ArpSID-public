#pragma once

#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/core/c64_phi2_types.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ArpSID::C64 {

struct D418CaptureSample final {
    uint64_t phi2 = 0;
    uint32_t hostFrame = 0;
    uint8_t value = 0;
    uint8_t nibble = 0;
    float unit = 0.0f; // [-1, +1] from the low $D418 volume nibble.
};

inline bool isD418Register(uint8_t reg) noexcept {
    return static_cast<uint8_t>(reg & 0x1Fu) == 0x18u;
}

// TS-reference bus contract for SID write tracing. Normal SID registers may be
// changed-filtered by high-level trace compilers, but $D418 volume-DAC PCM must
// bypass that filter: repeated identical low nibbles are real 4-bit samples and
// must survive with their absolute PHI2 timestamps. The physical C64 bridge
// records every CPU bus write; this helper is the named policy for offline
// trace import / MIDI→SID compilers that start from changed-register deltas.
inline bool c64SidWriteShouldEnterTrace(uint8_t reg, bool valueChanged) noexcept {
    return valueChanged || isD418Register(reg);
}

inline float d418NibbleToBipolar(uint8_t value) noexcept {
    const float n = static_cast<float>(value & 0x0Fu) / 15.0f;
    return n * 2.0f - 1.0f;
}

inline uint32_t phi2ToHostFrame(uint64_t phi2,
                                uint32_t cpuHz,
                                uint32_t sampleRate) noexcept {
    const uint64_t safeCpu = cpuHz ? cpuHz : kPalPhi2Hz;
    const uint64_t safeRate = sampleRate ? sampleRate : 48000u;
    return static_cast<uint32_t>((phi2 * safeRate) / safeCpu);
}

// Extract every $D418 bus write, including repeated identical values. This is
// intentionally not a changed-register filter: volume-register PCM depends on
// repeated samples being preserved with their absolute PHI2 timestamps.
inline size_t extractD418Samples(const C64SidBridgeState& bridge,
                                 D418CaptureSample* out,
                                 size_t outCap,
                                 uint32_t cpuHz = kPalPhi2Hz,
                                 uint32_t sampleRate = 48000u) noexcept {
    if (!out || outCap == 0u) return 0u;
    size_t n = 0;
    const uint32_t count = std::min<uint32_t>(bridge.timedWriteCount,
                                             static_cast<uint32_t>(C64SidBridgeState::kMaxTimedWrites));
    for (uint32_t i = 0; i < count && n < outCap; ++i) {
        const auto& w = bridge.timedWrites[i];
        if (!isD418Register(w.reg)) continue;
        out[n++] = D418CaptureSample{w.phi2Cycle,
                                     phi2ToHostFrame(w.phi2Cycle, cpuHz, sampleRate),
                                     w.value,
                                     static_cast<uint8_t>(w.value & 0x0Fu),
                                     d418NibbleToBipolar(w.value)};
    }
    return n;
}

// Zero-order-hold reconstruction for the internal DIGI REC/MON Pure SID source.
// The caller supplies a mono output buffer at the requested sample rate; this
// applies every $D418 write at its timestamp and holds the volume-DAC level until
// the next write. Existing contents are overwritten.
inline size_t reconstructD418Zoh(const C64SidBridgeState& bridge,
                                 float* dst,
                                 size_t frames,
                                 uint32_t cpuHz = kPalPhi2Hz,
                                 uint32_t sampleRate = 48000u) noexcept {
    if (!dst || frames == 0u) return 0u;
    std::fill(dst, dst + frames, 0.0f);
    float held = 0.0f;
    size_t cursor = 0;
    size_t applied = 0;
    const uint32_t count = std::min<uint32_t>(bridge.timedWriteCount,
                                             static_cast<uint32_t>(C64SidBridgeState::kMaxTimedWrites));
    for (uint32_t i = 0; i < count; ++i) {
        const auto& w = bridge.timedWrites[i];
        if (!isD418Register(w.reg)) continue;
        const size_t frame = std::min<size_t>(frames, phi2ToHostFrame(w.phi2Cycle, cpuHz, sampleRate));
        while (cursor < frame) dst[cursor++] = held;
        held = d418NibbleToBipolar(w.value);
        ++applied;
    }
    while (cursor < frames) dst[cursor++] = held;
    return applied;
}

} // namespace ArpSID::C64
