// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — value labels for stepped parameters in the cross-platform editor.
//
// Each table follows the engine's own decoder for that parameter (same index
// order, and the binning of stepIndexForParam below), so a menu entry always
// names what the engine plays:
//   waveform  bitperfect_engine.h valueToWaveform (floor(v*8))
//   filter    bitperfect_engine.h setFilterMode   (index == SID LP|BP|HP bits)
//   voice     bitperfect_engine.h setVoiceMode    (round(v*3))
//   glide     sid_runtime_synth_register_scheduler.h portamento style
//   LFO shape modulation/lfo.h Shape               (round(v*6))
//   arp       engines/arpeggiator.h                (mode round(v*6), octaves 1+floor(v*3),
//                                                   transpose round(v*48)-24, pattern 1+round(v*31))
//   seq mode  ArpSIDSequencerEngine.h traversal     (round(v*3))
//   hi-fi     core/sid_hifi_transcendence.h quality
#pragma once

#include "parameter_ids.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ArpSID::Editor {

// Index the engine plays for a stepped parameter's normalized value. Most
// stepped parameters round (index = round(v * steps)); a few decode with
// equal-width floor bins instead, and the editor must use the same law or a
// host-automated value between grid points shows a different entry than the
// one the engine plays:
//   VCO waveform, filter mode  floor(v * 8)   (steps 7, 8 equal bins)
//   arpeggiator octaves        floor(v * 3)   (steps 3, v = 1 -> index 3)
inline int stepIndexForParam(int id, float norm) {
    const int steps = static_cast<int>(normalizedParamStepCount(id));
    if (steps <= 0) return 0;
    const float v = std::clamp(std::isfinite(norm) ? norm : 0.f, 0.f, 1.f);
    int idx;
    switch (id) {
        case kParamVCO1Waveform: case kParamVCO2Waveform: case kParamVCO3Waveform:
        case kParamFilterMode:
            idx = static_cast<int>(std::floor(std::min(v, 0.999999f) * static_cast<float>(steps + 1)));
            break;
        case kParamArpOctaves:
            idx = static_cast<int>(v * static_cast<float>(steps));
            break;
        default:
            idx = static_cast<int>(std::lround(v * static_cast<float>(steps)));
            break;
    }
    return std::clamp(idx, 0, steps);
}

// Normalized value the editor writes for a stepped index (always on the grid,
// so it decodes back to the same index under every law above).
inline float stepNormForIndex(int id, int index) {
    const int steps = static_cast<int>(normalizedParamStepCount(id));
    if (steps <= 0) return 0.f;
    return static_cast<float>(std::clamp(index, 0, steps)) / static_cast<float>(steps);
}

// Label for a stepped parameter's index, or nullptr to use the host text.
inline const char* choiceLabel(int id, int index) {
    static const char* kWave[] = {"TRI", "SAW", "PULSE", "NOISE", "TRI+SAW", "TRI+PUL", "SAW+PUL", "TRI+SAW+PUL"};
    static const char* kFilter[] = {"OFF", "LOW-PASS", "BAND-PASS", "LP+BP", "HIGH-PASS", "NOTCH", "BP+HP", "ALL"};
    static const char* kVoice[] = {"POLY", "MONO", "LEGATO", "UNISON"};
    static const char* kGlide[] = {"C64 SLIDE", "C64 FIXED", "LINEAR", "SMOOTH"};
    static const char* kLfo[] = {"SINE", "TRIANGLE", "SAW", "RAMP DOWN", "SQUARE", "S&H", "RANDOM"};
    static const char* kArpMode[] = {"UP", "DOWN", "UP/DOWN", "DOWN/UP", "RANDOM", "PATTERN", "CHORD"};
    static const char* kOct[] = {"1 OCT", "2 OCT", "3 OCT", "4 OCT"};
    static const char* kSeq[] = {"FORWARD", "REVERSE", "PING-PONG", "RANDOM"};
    static const char* kHiFi[] = {"PURE", "HI-FI", "TRANSCENDENCE"};
    auto pick = [index](const char* const* table, int n) -> const char* {
        return (index >= 0 && index < n) ? table[index] : nullptr;
    };
    switch (id) {
        case kParamVCO1Waveform: case kParamVCO2Waveform: case kParamVCO3Waveform: return pick(kWave, 8);
        case kParamFilterMode: return pick(kFilter, 8);
        case kParamVoiceMode: return pick(kVoice, 4);
        case kParamPortamentoStyle: return pick(kGlide, 4);
        case kParamLFOShape: case kParamLFO2Shape: case kParamLFO3Shape: case kParamLFO4Shape: return pick(kLfo, 7);
        case kParamArpMode: return pick(kArpMode, 7);
        case kParamArpOctaves: return pick(kOct, 4);
        case kParamSeqMode: return pick(kSeq, 4);
        case kParamHiFiQuality: return pick(kHiFi, 3);
        default: return nullptr;
    }
}

// Editor text for a parameter value, or empty to use the host text.
inline std::string editorValueText(int id, float norm) {
    if (normalizedParamStepCount(id) > 0)
        if (const char* l = choiceLabel(id, stepIndexForParam(id, norm))) return l;
    char buf[32];
    if (id >= static_cast<int>(kParamSidRegD400) && id <= static_cast<int>(kParamSidRegD41D)) {
        std::snprintf(buf, sizeof buf, "$%02X", static_cast<unsigned>(std::lround(std::clamp(norm, 0.f, 1.f) * 255.f)));
        return buf;
    }
    switch (id) {
        case kParamArpTranspose:
            std::snprintf(buf, sizeof buf, "%+d st", static_cast<int>(std::lround(norm * 48.f)) - 24);
            return buf;
        case kParamArpPatternLength:
            std::snprintf(buf, sizeof buf, "%d steps", 1 + stepIndexForParam(id, norm));
            return buf;
        default:
            return {};
    }
}

} // namespace ArpSID::Editor
