// Copyright (C) 2024-2026 Ulf Bertilsson
// ─── ArpSIDSidRegMapper.h ─────────────────────────────────────────────────────
// Phase 3: Explicit SID register mapping layer.
// Translates high-level synth state into authentic SID register images.
// Separated from telemetry — never mutates saved state.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <array>
#include "arpsid/core/sid_runtime_register_ops.h"

namespace ArpSID {

// ── SID register layout ($D400 base) ─────────────────────────────────────────
// Voice 0: $00-$06 Voice 1: $07-$0D Voice 2: $0E-$14
// Filter: $15-$18 Global: $19-$1C
static constexpr int kSidVoiceRegs   = 7;
static constexpr int kSidMapperRegCount = 29;   // $D400..$D41C (local to mapper, avoids collision)

// Waveform control byte bits
static constexpr uint8_t kSidWavTriangle = 0x10u;
static constexpr uint8_t kSidWavSawtooth = 0x20u;
static constexpr uint8_t kSidWavPulse    = 0x40u;
static constexpr uint8_t kSidWavNoise    = 0x80u;
static constexpr uint8_t kSidBitGate     = 0x01u;
static constexpr uint8_t kSidBitSync     = 0x02u;
static constexpr uint8_t kSidBitRing     = 0x04u;
static constexpr uint8_t kSidBitTest     = 0x08u;

// ── SID voice state ───────────────────────────────────────────────────────────
struct SidVoiceState {
    uint16_t frequency  = 0;        // SID freq reg value (0..65535)
    uint16_t pulseWidth = 0x800u;   // SID PW reg value (0..4095)
    uint8_t  waveform   = kSidWavTriangle;
    bool     gate       = false;
    bool     sync       = false;
    bool     ringMod    = false;
    bool     test       = false;
    uint8_t  attackDecay  = 0x08u;  // A=0 D=8
    uint8_t  sustainRelease = 0xF0u; // S=15 R=0
};

// ── SID filter state ──────────────────────────────────────────────────────────
struct SidFilterState {
    uint16_t cutoff   = 1024;  // 0..2047
    uint8_t  resonance= 8;     // 0..15
    uint8_t  routing  = 0x07u; // bits 0-2: voices through filter
    uint8_t  mode     = 0x10u; // LP=0x10 BP=0x20 HP=0x40
    uint8_t  volume   = 15;    // 0..15
};

// ── SID register image (complete $D400 snapshot) ──────────────────────────────
struct SidRegImage {
    uint8_t regs[kSidMapperRegCount] = {};

    // Write voice registers from state
    void setVoice(int v, const SidVoiceState& s) noexcept {
        if (v < 0 || v > 2) return;
        const int b = v * kSidVoiceRegs;
        regs[b+0] = (uint8_t)(s.frequency & 0xFFu);           // freq lo
        regs[b+1] = (uint8_t)((s.frequency >> 8) & 0xFFu);    // freq hi
        regs[b+2] = (uint8_t)(s.pulseWidth & 0xFFu);          // PW lo
        regs[b+3] = (uint8_t)((s.pulseWidth >> 8) & 0x0Fu);   // PW hi (4 bits)
        uint8_t ctrl = s.waveform;
        if (s.gate)   ctrl |= kSidBitGate;
        if (s.sync)   ctrl |= kSidBitSync;
        if (s.ringMod) ctrl |= kSidBitRing;
        if (s.test)   ctrl |= kSidBitTest;
        regs[b+4] = ctrl;
        regs[b+5] = s.attackDecay;
        regs[b+6] = s.sustainRelease;
    }

    // Write filter registers
    void setFilter(const SidFilterState& f) noexcept {
        regs[0x15] = (uint8_t)((f.cutoff & 0x07u) | (f.routing & 0x70u));
        regs[0x16] = (uint8_t)((f.cutoff >> 3) & 0xFFu);
        regs[0x17] = (uint8_t)((f.resonance << 4) | (f.routing & 0x0Fu));
        regs[0x18] = (uint8_t)(f.mode | (f.volume & 0x0Fu));
    }
};

// ── SID Register Mapper ───────────────────────────────────────────────────────
// Translates normalized (0..1) synth params → SID register values.
// All authenticity boundaries documented inline.
class SidRegMapper {
public:
    // ── Frequency mapping (authentic SID formula) ────────────────────────────
    // SID freq = (Fout * 16777216) / Fclock
    // Returns SID frequency register value.
    static uint16_t midiNoteToFreqReg(float midiNote, double clockHz = 985248.0) noexcept {
        return canonicalSidFrequencyRegisterForMidiNote(static_cast<double>(midiNote), clockHz);
    }

    // ── Pulse width (authentic: 0..4095, 2048=50%) ───────────────────────────
    static uint16_t normToPulseWidth(float norm) noexcept {
        if (!std::isfinite(norm)) return 0x800u;
        return (uint16_t)std::clamp((int)(norm * 4095.f), 0, 4095);
    }

    // ── Waveform norm to SID waveform bits ────────────────────────────────────
    // Authentic mapping: TRI/SAW/PULSE/NOISE + combinations
    static uint8_t normToWaveform(float norm) noexcept {
        static const uint8_t table[8] = {
            kSidWavTriangle,
            kSidWavSawtooth,
            kSidWavPulse,
            kSidWavNoise,
            kSidWavTriangle | kSidWavSawtooth,
            kSidWavTriangle | kSidWavPulse,
            kSidWavSawtooth | kSidWavPulse,
            kSidWavTriangle | kSidWavSawtooth | kSidWavPulse,
        };
        const int idx = std::clamp((int)(std::clamp(norm, 0.f, 0.9999f) * 8.f), 0, 7);
        return table[idx];
    }

    // ── ADSR nibble encoding (authentic 0..15 → SID table lookup implied) ────
    static uint8_t normToAttackDecay(float aNorm, float dNorm) noexcept {
        const uint8_t a = (uint8_t)std::clamp((int)(aNorm * 15.f), 0, 15);
        const uint8_t d = (uint8_t)std::clamp((int)(dNorm * 15.f), 0, 15);
        return (uint8_t)((a << 4) | d);
    }
    static uint8_t normToSustainRelease(float sNorm, float rNorm) noexcept {
        const uint8_t s = (uint8_t)std::clamp((int)(sNorm * 15.f), 0, 15);
        const uint8_t r = (uint8_t)std::clamp((int)(rNorm * 15.f), 0, 15);
        return (uint8_t)((s << 4) | r);
    }

    // ── Filter cutoff (authentic 11-bit) ──────────────────────────────────────
    static uint16_t normToFilterCutoff(float norm) noexcept {
        if (!std::isfinite(norm)) return 0;
        return (uint16_t)std::clamp((int)(norm * 2047.f), 0, 2047);
    }

    // ── Filter resonance (authentic 0..15) ────────────────────────────────────
    static uint8_t normToFilterRes(float norm) noexcept {
        return (uint8_t)std::clamp((int)(norm * 15.f), 0, 15);
    }

    // ── Filter mode ────────────────────────────────────────────────────────────
    // Authentic: LP=0x10, BP=0x20, HP=0x40 (can combine, but only one typically used)
    static uint8_t normToFilterMode(float norm) noexcept {
        const int idx = std::clamp((int)(norm * 3.f), 0, 2);
        static const uint8_t modes[3] = { 0x10u, 0x20u, 0x40u };
        return modes[idx];
    }

    // ── Filter routing: which voices go through filter ─────────────────────────
    // Authentic: bits 0-2 = voice 0/1/2; bit 3 = external input
    // Volume/V3OFF in upper nibble of $D418
    static uint8_t buildFilterRouting(bool v0, bool v1, bool v2) noexcept {
        return (uint8_t)((v0 ? 1u : 0u) | (v1 ? 2u : 0u) | (v2 ? 4u : 0u));
    }

    // ── Volume/mode register ($D418) ──────────────────────────────────────────
    static uint8_t buildVolumeMode(uint8_t filterMode, uint8_t volume,
                                    bool voice3Off) noexcept {
        return (uint8_t)(filterMode | (volume & 0x0Fu) | (voice3Off ? 0x80u : 0u));
    }

    // ── Build complete voice SidVoiceState from normalized params ─────────────
    static SidVoiceState buildVoiceState(
            float midiNote,      // actual note (can include detune/bend)
            float pulseWidthNorm,
            float waveformNorm,
            bool  gate,
            bool  sync,
            bool  ringMod,
            float attackNorm, float decayNorm,
            float sustainNorm, float releaseNorm,
            double clockHz = 985248.0) noexcept {
        SidVoiceState s{};
        s.frequency    = midiNoteToFreqReg(midiNote, clockHz);
        s.pulseWidth   = normToPulseWidth(pulseWidthNorm);
        s.waveform     = normToWaveform(waveformNorm);
        s.gate         = gate;
        s.sync         = sync;
        s.ringMod      = ringMod;
        s.test         = false;  // test bit reserved for hard restart
        s.attackDecay  = normToAttackDecay(attackNorm, decayNorm);
        s.sustainRelease = normToSustainRelease(sustainNorm, releaseNorm);
        return s;
    }
};

} // namespace ArpSID
