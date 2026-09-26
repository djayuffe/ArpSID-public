// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "parameter_ids.h"
#include <cstdint>

namespace ArpSID {

struct SidRealtimeCcMapping final {
    uint8_t cc = 0;
    ParamID param = static_cast<ParamID>(kNumParams);
    const char* label = "";
};

inline ParamID sidMappedRealtimeCcParam(uint8_t cc) noexcept {
    // Canonical realtime CC macro law. Keep AU/VST/standalone adapters thin:
    // they may choose queue vs immediate apply, but not different targets.
    switch (cc) {
        case 2:  return kParamFilterCutoff;       // Breath -> cutoff macro
        case 70: return kParamFilterCutoff;       // AKAI MPK mini K1
        case 71: return kParamFilterResonance;    // AKAI MPK mini K2
        case 72: return kParamFilterDrive;        // AKAI MPK mini K3
        case 73: return kParamFilterEnvAmount;    // AKAI MPK mini K4
        case 74: return kParamFilterLFOAmount;    // AKAI MPK mini K5
        case 75: return kParamMasterVolume;       // AKAI MPK mini K6
        case 76: return kParamReverbMix;          // AKAI MPK mini K7
        case 77: return kParamForensicIntensity;  // AKAI MPK mini K8
        default: return static_cast<ParamID>(kNumParams);
    }
}

inline const char* sidMappedRealtimeCcLabel(uint8_t cc) noexcept {
    switch (cc) {
        case 2:  return "Breath -> Filter Cutoff";
        case 70: return "AKAI MPK mini K1 -> Filter Cutoff";
        case 71: return "AKAI MPK mini K2 -> Resonance";
        case 72: return "AKAI MPK mini K3 -> Filter Drive";
        case 73: return "AKAI MPK mini K4 -> Filter Env Amount";
        case 74: return "AKAI MPK mini K5 -> Filter LFO Amount";
        case 75: return "AKAI MPK mini K6 -> Master Volume";
        case 76: return "AKAI MPK mini K7 -> Reverb Mix";
        case 77: return "AKAI MPK mini K8 -> Forensic Intensity";
        default: return "";
    }
}

inline int sidAkaiMpkMiniDefaultKnobIndex(uint8_t cc) noexcept {
    return (cc >= 70u && cc <= 77u) ? static_cast<int>(cc - 70u) : -1;
}

inline bool sidIsAkaiMpkMiniDefaultKnob(uint8_t cc) noexcept {
    return sidAkaiMpkMiniDefaultKnobIndex(cc) >= 0;
}

inline ParamID sidAkaiMpkMiniDefaultKnobParamByIndex(int index) noexcept {
    switch (index) {
        case 0: return kParamFilterCutoff;
        case 1: return kParamFilterResonance;
        case 2: return kParamFilterDrive;
        case 3: return kParamFilterEnvAmount;
        case 4: return kParamFilterLFOAmount;
        case 5: return kParamMasterVolume;
        case 6: return kParamReverbMix;
        case 7: return kParamForensicIntensity;
        default: return static_cast<ParamID>(kNumParams);
    }
}

inline const char* sidAkaiMpkMiniDefaultKnobLabelByIndex(int index) noexcept {
    switch (index) {
        case 0: return "K1 Cutoff";
        case 1: return "K2 Resonance";
        case 2: return "K3 Drive";
        case 3: return "K4 Env";
        case 4: return "K5 LFO";
        case 5: return "K6 Volume";
        case 6: return "K7 Reverb";
        case 7: return "K8 Forensic";
        default: return "";
    }
}

inline const char* sidAkaiMpkMiniDefaultMapSummary() noexcept {
    return "MPK mini map armed: CC70 K1 Cutoff · 71 K2 Res · 72 K3 Drive · 73 K4 Env · 74 K5 LFO · 75 K6 Vol · 76 K7 Reverb · 77 K8 Forensic";
}

inline bool sidCcRequiresImmediateAudioProjection(uint8_t cc) noexcept {
    if (sidMappedRealtimeCcParam(cc) != kNumParams) return true;
    switch (cc) {
        case 1:   // mod wheel / modulation source
        case 2:   // breath / cutoff macro
        case 4:   // foot/expression alias
        case 7:   // channel volume
        case 11:  // expression
        case 64:  // sustain
        case 65:  // portamento switch
        case 66:  // sostenuto
        case 67:  // soft pedal
        case 120: // all sound off
        case 121: // reset controllers
        case 123: // all notes off
            return true;
        default:
            return false;
    }
}

inline constexpr SidRealtimeCcMapping kAkaiMpkMiniDefaultCcMap[] = {
    {70, kParamFilterCutoff,       "K1 Filter Cutoff"},
    {71, kParamFilterResonance,    "K2 Resonance"},
    {72, kParamFilterDrive,        "K3 Filter Drive"},
    {73, kParamFilterEnvAmount,    "K4 Filter Env"},
    {74, kParamFilterLFOAmount,    "K5 Filter LFO"},
    {75, kParamMasterVolume,       "K6 Master Volume"},
    {76, kParamReverbMix,          "K7 Reverb Mix"},
    {77, kParamForensicIntensity,  "K8 Forensic Intensity"},
};

} // namespace ArpSID
