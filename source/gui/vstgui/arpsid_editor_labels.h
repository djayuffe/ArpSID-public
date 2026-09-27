// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — value labels for stepped parameters in the cross-platform editor.
//
// Each table follows the engine's own decoder for that parameter (same index
// order and binning), so a menu entry always names what the engine plays:
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
    const int steps = static_cast<int>(normalizedParamStepCount(id));
    if (steps > 0) {
        // Waveform and filter mode use floor bins; the others round.
        const bool floorBins = id == kParamVCO1Waveform || id == kParamVCO2Waveform || id == kParamVCO3Waveform ||
                               id == kParamFilterMode || id == kParamArpOctaves;
        const float scaled = norm * static_cast<float>(steps);
        const int idx = std::clamp(floorBins ? static_cast<int>(std::floor(scaled + 1e-4f))
                                             : static_cast<int>(std::lround(scaled)), 0, steps);
        if (const char* l = choiceLabel(id, idx)) return l;
    }
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
            std::snprintf(buf, sizeof buf, "%d steps", 1 + static_cast<int>(std::lround(norm * 31.f)));
            return buf;
        default:
            return {};
    }
}

} // namespace ArpSID::Editor
