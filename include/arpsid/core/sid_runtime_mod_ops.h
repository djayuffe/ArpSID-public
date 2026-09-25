#pragma once
#include "sid_runtime_model.h"
#include "sid_runtime_drsid_gain.h"
#include "arpsid/engines/bitperfect_engine.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/modulation/lfo.h"
#include "parameter_ids.h"
#include "sid_mod_matrix_types.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace ArpSID {

struct SidParamMatrixRouteDef {
    int   sourcePid = 0;
    int   depthPid = 0;
    int   targetPid = 0;
    float depthScale = 0.0f;
};

inline constexpr std::array<SidParamMatrixRouteDef, 9> kSynthParamMatrixRouteDefs{{
    { kParamModVCFCutoffSource,   kParamModVCFCutoffDepth,   kParamFilterCutoff,      0.35f },
    { kParamModVCFResonanceSource,kParamModVCFResonanceDepth,kParamFilterResonance,   0.30f },
    { kParamModVCO1FreqSource,    kParamModVCO1FreqDepth,    kParamVCO1Detune,        0.20f },
    { kParamModVCO1PWSource,      kParamModVCO1PWDepth,      kParamVCO1PulseWidth,    0.35f },
    { kParamModVCO2FreqSource,    kParamModVCO2FreqDepth,    kParamVCO2Detune,        0.20f },
    { kParamModVCO2PWSource,      kParamModVCO2PWDepth,      kParamVCO2PulseWidth,    0.35f },
    { kParamModVCO3FreqSource,    kParamModVCO3FreqDepth,    kParamVCO3Detune,        0.20f },
    { kParamModVCO3PWSource,      kParamModVCO3PWDepth,      kParamVCO3PulseWidth,    0.35f },
    { kParamModMasterVolumeSource,kParamModMasterVolumeDepth,kParamMasterVolume,      0.30f },
}};

inline constexpr float kSynthFreqMatrixNormalizedDeltaToSemis = 2.0f;

inline constexpr std::array<SidParamMatrixRouteDef, 9> kDrSidParamMatrixRouteDefs{{
    { kParamModVCFCutoffSource,   kParamModVCFCutoffDepth,   kParamDrSidKickTune,     0.28f },
    { kParamModVCFResonanceSource,kParamModVCFResonanceDepth,kParamDrSidKickDecay,    0.30f },
    { kParamModVCO1FreqSource,    kParamModVCO1FreqDepth,    kParamDrSidSnareTone,    0.26f },
    { kParamModVCO1PWSource,      kParamModVCO1PWDepth,      kParamDrSidSnareSnap,    0.30f },
    { kParamModVCO2FreqSource,    kParamModVCO2FreqDepth,    kParamDrSidHatTune,      0.24f },
    { kParamModVCO2PWSource,      kParamModVCO2PWDepth,      kParamDrSidHatDecay,     0.28f },
    { kParamModVCO3FreqSource,    kParamModVCO3FreqDepth,    kParamDrSidCowbellTune,  0.24f },
    { kParamModVCO3PWSource,      kParamModVCO3PWDepth,      kParamDrSidTomTune,      0.24f },
    { kParamModMasterVolumeSource,kParamModMasterVolumeDepth,kParamDrSidVolume,       0.28f },
}};

inline const std::array<SidParamMatrixRouteDef, 9>& sidParamMatrixRouteDefs(bool drSidMode) noexcept {
    return drSidMode ? kDrSidParamMatrixRouteDefs : kSynthParamMatrixRouteDefs;
}

inline const SidParamMatrixRouteDef* sidParamMatrixRouteDefForControlPid(bool drSidMode, int controlPid) noexcept {
    const auto& defs = sidParamMatrixRouteDefs(drSidMode);
    for (const auto& def : defs) {
        if (def.sourcePid == controlPid || def.depthPid == controlPid) return &def;
    }
    return nullptr;
}

inline const char* sidParamMatrixTargetShortName(bool drSidMode, int targetPid) noexcept {
    if (!drSidMode) {
        switch (targetPid) {
            case kParamFilterCutoff: return "VCF Cut";
            case kParamFilterResonance: return "VCF Res";
            case kParamVCO1Detune: return "VCO1 Fr";
            case kParamVCO1PulseWidth: return "VCO1 PW";
            case kParamVCO2Detune: return "VCO2 Fr";
            case kParamVCO2PulseWidth: return "VCO2 PW";
            case kParamVCO3Detune: return "VCO3 Fr";
            case kParamVCO3PulseWidth: return "VCO3 PW";
            case kParamMasterVolume: return "Volume";
            default: return "Matrix";
        }
    }
    switch (targetPid) {
        case kParamDrSidKickTune: return "KickTun";
        case kParamDrSidKickDecay: return "KickDec";
        case kParamDrSidSnareTone: return "SnrTone";
        case kParamDrSidSnareSnap: return "SnrSnap";
        case kParamDrSidHatTune: return "HatTune";
        case kParamDrSidHatDecay: return "HatDec";
        case kParamDrSidCowbellTune: return "CowTun";
        case kParamDrSidTomTune: return "TomTune";
        case kParamDrSidVolume: return "DrmVol";
        default: return "DrSID";
    }
}

inline float canonicalModSourceBipolar(const SidRuntimeModel& runtime, SidModSource src) noexcept;

template <typename ParamsArray>
inline float canonicalModSourceBipolar(const SidRuntimeModel& runtime,
                                       const ParamsArray& liveParams,
                                       SidModSource src) noexcept;

template <typename ParamsArray>
inline float sidParamMatrixDepthDeltaForRoute(const SidRuntimeModel& runtime,
                                              const ParamsArray& params,
                                              const SidParamMatrixRouteDef& route) noexcept {
    const float depthNorm = std::clamp(std::isfinite(params[(size_t)route.depthPid]) ? (float)params[(size_t)route.depthPid] : 0.0f,
                                       0.0f, 1.0f);
    if (depthNorm <= 1.0e-4f) return 0.0f;
    const SidModSource src = decodeSidModSourceFromNormalizedUi((float)params[(size_t)route.sourcePid]);
    if (src == SidModSource::None) return 0.0f;
    const float srcVal = canonicalModSourceBipolar(runtime, params, src);
    return std::clamp(srcVal * depthNorm * route.depthScale, -1.0f, 1.0f);
}

inline const SidTokenVoiceEntry* canonicalNewestActiveToken(const SidDynamicState& dyn) noexcept {
    return dyn.newestActiveTokenEntry();
}

inline const SidTokenVoiceEntry* canonicalFocusedToken(const SidDynamicState& dyn) noexcept {
    if (dyn.focused_note.valid()) {
        const uint64_t exactTok = dyn.resolveVoiceTokenForEventIdentity(
            static_cast<int16_t>(dyn.focused_note.channel),
            static_cast<int16_t>(dyn.focused_note.midi_note),
            dyn.focused_note.note_id);
        if (exactTok != 0) {
            if (const auto* byTok = dyn.findActiveVoiceByToken(exactTok)) return byTok;
        }
    }
    if (const auto* tok = dyn.focusedTokenEntry()) return tok;
    return dyn.newestActiveTokenEntry();
}

inline float canonicalStrongestAbs16(const float (&values)[16]) noexcept {
    float best = 0.0f;
    float bestAbs = -1.0f;
    for (int ch = 0; ch < 16; ++ch) {
        const float v = std::isfinite(values[ch]) ? values[ch] : 0.0f;
        const float av = std::fabs(v);
        if (av > bestAbs) {
            best = v;
            bestAbs = av;
        }
    }
    return best;
}

inline float canonicalStrongestPressureBipolar16(const float (&values)[16]) noexcept {
    float best = 0.0f;
    for (int ch = 0; ch < 16; ++ch) {
        const float uni = std::isfinite(values[ch]) ? std::clamp(values[ch], 0.0f, 1.0f) : 0.0f;
        if (uni > best) best = uni;
    }
    return best * 2.0f - 1.0f;
}

inline int canonicalFocusedChannelIndex(const SidDynamicState& dyn) noexcept {
    if (const auto* tok = canonicalFocusedToken(dyn))
        return std::clamp<int>(tok->token.channel, 0, 15);
    return std::clamp<int>(dyn.last_note_channel, 0, 15);
}

inline float canonicalFocusedChannelPressureBipolar(const SidDynamicState& dyn) noexcept {
    const int ch = canonicalFocusedChannelIndex(dyn);
    const float uni = std::isfinite(dyn.channel_pressure[ch]) ? std::clamp(dyn.channel_pressure[ch], 0.0f, 1.0f) : 0.0f;
    return std::clamp(uni * 2.0f - 1.0f, -1.0f, 1.0f);
}

inline float canonicalFocusedPitchBend(const SidDynamicState& dyn) noexcept {
    const int ch = canonicalFocusedChannelIndex(dyn);
    return std::clamp(std::isfinite(dyn.pitch_bend_norm[ch]) ? dyn.pitch_bend_norm[ch] : 0.0f, -1.0f, 1.0f);
}

inline float canonicalFocusedPolyPressureBipolar(const SidDynamicState& dyn) noexcept {
    if (const auto* tok = canonicalFocusedToken(dyn))
        return std::clamp(std::clamp(tok->polyPressure, 0.0f, 1.0f) * 2.0f - 1.0f, -1.0f, 1.0f);
    return 0.0f;
}

inline int canonicalFocusedChannelIndex(const SidRuntimeModel& runtime) noexcept {
    return runtime.focusedChannelIndex();
}

inline float canonicalFocusedChannelPressureBipolar(const SidRuntimeModel& runtime) noexcept {
    return runtime.focusedChannelPressureBipolar();
}

inline float canonicalFocusedPitchBend(const SidRuntimeModel& runtime) noexcept {
    return runtime.focusedPitchBendNorm();
}

inline float canonicalFocusedPolyPressureBipolar(const SidRuntimeModel& runtime) noexcept {
    return runtime.focusedPolyPressureBipolar();
}

inline float canonicalModSourceBipolar(const SidRuntimeModel& runtime, SidModSource src) noexcept {
    const auto& root = runtime.stateRoot();
    const auto gp = [&](size_t idx) noexcept -> float {
        return sidStateRootParamValue(root, static_cast<int>(idx));
    };
    const int lastNoteMidi = std::clamp<int>(runtime.lastNote(), 0, 127);
    const float noteNorm = static_cast<float>(lastNoteMidi) / 127.0f;
    const float keyFollow = std::clamp((static_cast<float>(lastNoteMidi) - 60.0f) / 48.0f, -1.0f, 1.0f);
    switch (src) {
        case SidModSource::None:         return 0.0f;
        case SidModSource::Velocity:     return std::clamp(runtime.lastNoteVelocity() * 2.0f - 1.0f, -1.0f, 1.0f);
        case SidModSource::NoteNumber:   return noteNorm * 2.0f - 1.0f;
        case SidModSource::KeyFollow:    return keyFollow;
        case SidModSource::ModWheel:     return std::clamp(runtime.modWheelNorm() * 2.0f - 1.0f, -1.0f, 1.0f);
        case SidModSource::LFO1:         return std::clamp(runtime.lfoValue(0), -1.0f, 1.0f);
        case SidModSource::LFO2:         return std::clamp(runtime.lfoValue(1), -1.0f, 1.0f);
        case SidModSource::LFO3:         return std::clamp(runtime.lfoValue(2), -1.0f, 1.0f);
        case SidModSource::LFO4:         return std::clamp(runtime.lfoValue(3), -1.0f, 1.0f);
        case SidModSource::Env1:         return std::clamp(runtime.env1Level() * 2.0f - 1.0f, -1.0f, 1.0f);
        case SidModSource::Random:       return std::clamp(runtime.randomBipolar(), -1.0f, 1.0f);
        case SidModSource::PitchBend:    return canonicalFocusedPitchBend(runtime);
        case SidModSource::AfterTouch:   return canonicalFocusedChannelPressureBipolar(runtime);
        case SidModSource::PolyPressure: return canonicalFocusedPolyPressureBipolar(runtime);
        case SidModSource::Macro1:       return gp(static_cast<size_t>(kParamMacro1)) * 2.0f - 1.0f;
        case SidModSource::Macro2:       return gp(static_cast<size_t>(kParamMacro2)) * 2.0f - 1.0f;
        case SidModSource::Macro3:       return gp(static_cast<size_t>(kParamMacro3)) * 2.0f - 1.0f;
        case SidModSource::Macro4:       return gp(static_cast<size_t>(kParamMacro4)) * 2.0f - 1.0f;
        case SidModSource::Macro5:       return gp(static_cast<size_t>(kParamMacro5)) * 2.0f - 1.0f;
        case SidModSource::Macro6:       return gp(static_cast<size_t>(kParamMacro6)) * 2.0f - 1.0f;
        case SidModSource::Macro7:       return gp(static_cast<size_t>(kParamMacro7)) * 2.0f - 1.0f;
        case SidModSource::Macro8:       return gp(static_cast<size_t>(kParamMacro8)) * 2.0f - 1.0f;
        default:                         return 0.0f;
    }
}

template <typename ParamsArray>
inline float canonicalModSourceBipolar(const SidRuntimeModel& runtime,
                                       const ParamsArray& liveParams,
                                       SidModSource src) noexcept {
    const auto gp = [&](size_t idx) noexcept -> float {
        if (idx >= liveParams.size()) return 0.0f;
        const float v = static_cast<float>(liveParams[idx]);
        return std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    };
    const int lastNoteMidi = std::clamp<int>(runtime.lastNote(), 0, 127);
    const float noteNorm = static_cast<float>(lastNoteMidi) / 127.0f;
    const float keyFollow = std::clamp((static_cast<float>(lastNoteMidi) - 60.0f) / 48.0f, -1.0f, 1.0f);
    switch (src) {
        case SidModSource::None:         return 0.0f;
        case SidModSource::Velocity:     return std::clamp(runtime.lastNoteVelocity() * 2.0f - 1.0f, -1.0f, 1.0f);
        case SidModSource::NoteNumber:   return noteNorm * 2.0f - 1.0f;
        case SidModSource::KeyFollow:    return keyFollow;
        case SidModSource::ModWheel:     return std::clamp(runtime.modWheelNorm() * 2.0f - 1.0f, -1.0f, 1.0f);
        case SidModSource::LFO1:         return std::clamp(runtime.lfoValue(0), -1.0f, 1.0f);
        case SidModSource::LFO2:         return std::clamp(runtime.lfoValue(1), -1.0f, 1.0f);
        case SidModSource::LFO3:         return std::clamp(runtime.lfoValue(2), -1.0f, 1.0f);
        case SidModSource::LFO4:         return std::clamp(runtime.lfoValue(3), -1.0f, 1.0f);
        case SidModSource::Env1:         return std::clamp(runtime.env1Level() * 2.0f - 1.0f, -1.0f, 1.0f);
        case SidModSource::Random:       return std::clamp(runtime.randomBipolar(), -1.0f, 1.0f);
        case SidModSource::PitchBend:    return canonicalFocusedPitchBend(runtime);
        case SidModSource::AfterTouch:   return canonicalFocusedChannelPressureBipolar(runtime);
        case SidModSource::PolyPressure: return canonicalFocusedPolyPressureBipolar(runtime);
        case SidModSource::Macro1:       return gp(static_cast<size_t>(kParamMacro1)) * 2.0f - 1.0f;
        case SidModSource::Macro2:       return gp(static_cast<size_t>(kParamMacro2)) * 2.0f - 1.0f;
        case SidModSource::Macro3:       return gp(static_cast<size_t>(kParamMacro3)) * 2.0f - 1.0f;
        case SidModSource::Macro4:       return gp(static_cast<size_t>(kParamMacro4)) * 2.0f - 1.0f;
        case SidModSource::Macro5:       return gp(static_cast<size_t>(kParamMacro5)) * 2.0f - 1.0f;
        case SidModSource::Macro6:       return gp(static_cast<size_t>(kParamMacro6)) * 2.0f - 1.0f;
        case SidModSource::Macro7:       return gp(static_cast<size_t>(kParamMacro7)) * 2.0f - 1.0f;
        case SidModSource::Macro8:       return gp(static_cast<size_t>(kParamMacro8)) * 2.0f - 1.0f;
        default:                         return 0.0f;
    }
}

struct SidTypedModProjectionValues {
    float cutoff = 0.0f;
    float resonance = 0.0f;
    float vco1Detune = 0.0f;
    float vco1PW = 0.0f;
    float vco2Detune = 0.0f;
    float vco2PW = 0.0f;
    float vco3Detune = 0.0f;
    float vco3PW = 0.0f;
    float masterVolume = 0.0f;
};

template <typename ParamsArray>
inline SidTypedModProjectionValues computeTypedModProjectionValues(
        const SidRuntimeModel& runtime,
        const ParamsArray& liveParams) noexcept {
    const auto& root = runtime.stateRoot();
    const auto gp = [&](size_t idx) noexcept -> float {
        if (idx >= liveParams.size()) return 0.0f;
        const float v = static_cast<float>(liveParams[idx]);
        return std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    };
    SidTypedModProjectionValues out{};
    out.cutoff = gp(static_cast<size_t>(kParamFilterCutoff));
    out.resonance = gp(static_cast<size_t>(kParamFilterResonance));
    out.vco1Detune = gp(static_cast<size_t>(kParamVCO1Detune));
    out.vco1PW = gp(static_cast<size_t>(kParamVCO1PulseWidth));
    out.vco2Detune = gp(static_cast<size_t>(kParamVCO2Detune));
    out.vco2PW = gp(static_cast<size_t>(kParamVCO2PulseWidth));
    out.vco3Detune = gp(static_cast<size_t>(kParamVCO3Detune));
    out.vco3PW = gp(static_cast<size_t>(kParamVCO3PulseWidth));
    out.masterVolume = gp(static_cast<size_t>(kParamMasterVolume));
    for (const SidModRoute& r : root.patch.mod_routes) {
        if (!r.active()) continue;
        float src = canonicalModSourceBipolar(runtime, liveParams, r.source);
        switch (r.transform) {
            case SidModTransform::Squared: src = (src >= 0.0f) ? src*src : -(src*src); break;
            case SidModTransform::Abs: src = std::fabs(src); break;
            case SidModTransform::Invert: src = -src; break;
            default: break;
        }
        if (!r.bipolar) src = std::clamp(src * 0.5f + 0.5f, 0.0f, 1.0f);
        const float delta = std::clamp(src * r.depth, -1.0f, 1.0f);
        switch (r.target) {
            case SidModTarget::FilterCutoff: out.cutoff = std::clamp(out.cutoff + delta, 0.0f, 1.0f); break;
            case SidModTarget::FilterResonance: out.resonance = std::clamp(out.resonance + delta, 0.0f, 1.0f); break;
            case SidModTarget::VCO1Detune: out.vco1Detune = std::clamp(out.vco1Detune + delta, 0.0f, 1.0f); break;
            case SidModTarget::VCO1PulseWidth: out.vco1PW = std::clamp(out.vco1PW + delta, 0.0f, 1.0f); break;
            case SidModTarget::VCO2Detune: out.vco2Detune = std::clamp(out.vco2Detune + delta, 0.0f, 1.0f); break;
            case SidModTarget::VCO2PulseWidth: out.vco2PW = std::clamp(out.vco2PW + delta, 0.0f, 1.0f); break;
            case SidModTarget::VCO3Detune: out.vco3Detune = std::clamp(out.vco3Detune + delta, 0.0f, 1.0f); break;
            case SidModTarget::VCO3PulseWidth: out.vco3PW = std::clamp(out.vco3PW + delta, 0.0f, 1.0f); break;
            case SidModTarget::MasterVolume: out.masterVolume = std::clamp(out.masterVolume + delta, 0.0f, 1.0f); break;
            default: break;
        }
    }
    return out;
}

template <typename ParamsArray>
inline void applyTypedModRoutesToBitPerfect(const SidRuntimeModel& runtime,
                                            BitPerfectEngine* bitPerfect,
                                            const ParamsArray& liveParams,
                                            const LFOBank* lfoBank = nullptr) noexcept {
    (void)lfoBank;
    if (!bitPerfect) return;
    const SidTypedModProjectionValues v =
        computeTypedModProjectionValues(runtime, liveParams);
    bitPerfect->setFilterCutoff(v.cutoff);
    bitPerfect->setFilterResonance(v.resonance);
    bitPerfect->setVCO1Detune(v.vco1Detune);
    bitPerfect->setVCO1PulseWidth(v.vco1PW);
    bitPerfect->setVCO2Detune(v.vco2Detune);
    bitPerfect->setVCO2PulseWidth(v.vco2PW);
    bitPerfect->setVCO3Detune(v.vco3Detune);
    bitPerfect->setVCO3PulseWidth(v.vco3PW);
    bitPerfect->setMasterVolume(v.masterVolume);
}

template <typename ParamsArray>
inline void applyParamMatrixToBitPerfect(const SidRuntimeModel& runtime,
                                         BitPerfectEngine* bitPerfect,
                                         const ParamsArray& params) noexcept {
    if (!bitPerfect) return;
    float cutoff = std::clamp(std::isfinite(params[(size_t)kParamFilterCutoff]) ? (float)params[(size_t)kParamFilterCutoff] : 0.0f, 0.0f, 1.0f);
    float resonance = std::clamp(std::isfinite(params[(size_t)kParamFilterResonance]) ? (float)params[(size_t)kParamFilterResonance] : 0.0f, 0.0f, 1.0f);
    float vco1Detune = std::clamp(std::isfinite(params[(size_t)kParamVCO1Detune]) ? (float)params[(size_t)kParamVCO1Detune] : 0.0f, 0.0f, 1.0f);
    float vco1PW = std::clamp(std::isfinite(params[(size_t)kParamVCO1PulseWidth]) ? (float)params[(size_t)kParamVCO1PulseWidth] : 0.0f, 0.0f, 1.0f);
    float vco2Detune = std::clamp(std::isfinite(params[(size_t)kParamVCO2Detune]) ? (float)params[(size_t)kParamVCO2Detune] : 0.0f, 0.0f, 1.0f);
    float vco2PW = std::clamp(std::isfinite(params[(size_t)kParamVCO2PulseWidth]) ? (float)params[(size_t)kParamVCO2PulseWidth] : 0.0f, 0.0f, 1.0f);
    float vco3Detune = std::clamp(std::isfinite(params[(size_t)kParamVCO3Detune]) ? (float)params[(size_t)kParamVCO3Detune] : 0.0f, 0.0f, 1.0f);
    float vco3PW = std::clamp(std::isfinite(params[(size_t)kParamVCO3PulseWidth]) ? (float)params[(size_t)kParamVCO3PulseWidth] : 0.0f, 0.0f, 1.0f);
    float masterVolume = std::clamp(std::isfinite(params[(size_t)kParamMasterVolume]) ? (float)params[(size_t)kParamMasterVolume] : 0.0f, 0.0f, 1.0f);
    float vcoPitchModSemis[3]{};
    for (const auto& route : kSynthParamMatrixRouteDefs) {
        const float delta = sidParamMatrixDepthDeltaForRoute(runtime, params, route);
        if (std::fabs(delta) <= 1.0e-6f) continue;
        switch (route.targetPid) {
            case kParamFilterCutoff: cutoff = std::clamp(cutoff + delta, 0.0f, 1.0f); break;
            case kParamFilterResonance: resonance = std::clamp(resonance + delta, 0.0f, 1.0f); break;
            case kParamVCO1Detune: vcoPitchModSemis[0] = std::clamp(vcoPitchModSemis[0] + delta * kSynthFreqMatrixNormalizedDeltaToSemis, -2.0f, 2.0f); break;
            case kParamVCO1PulseWidth: vco1PW = std::clamp(vco1PW + delta, 0.0f, 1.0f); break;
            case kParamVCO2Detune: vcoPitchModSemis[1] = std::clamp(vcoPitchModSemis[1] + delta * kSynthFreqMatrixNormalizedDeltaToSemis, -2.0f, 2.0f); break;
            case kParamVCO2PulseWidth: vco2PW = std::clamp(vco2PW + delta, 0.0f, 1.0f); break;
            case kParamVCO3Detune: vcoPitchModSemis[2] = std::clamp(vcoPitchModSemis[2] + delta * kSynthFreqMatrixNormalizedDeltaToSemis, -2.0f, 2.0f); break;
            case kParamVCO3PulseWidth: vco3PW = std::clamp(vco3PW + delta, 0.0f, 1.0f); break;
            case kParamMasterVolume: masterVolume = std::clamp(masterVolume + delta, 0.0f, 1.0f); break;
            default: break;
        }
    }
    bitPerfect->setFilterCutoff(cutoff);
    bitPerfect->setFilterResonance(resonance);
    bitPerfect->setVCO1Detune(vco1Detune);
    bitPerfect->setVCO1PulseWidth(vco1PW);
    bitPerfect->setVCO2Detune(vco2Detune);
    bitPerfect->setVCO2PulseWidth(vco2PW);
    bitPerfect->setVCO3Detune(vco3Detune);
    bitPerfect->setVCO3PulseWidth(vco3PW);
    bitPerfect->setVCOPitchModSemis(0, vcoPitchModSemis[0]);
    bitPerfect->setVCOPitchModSemis(1, vcoPitchModSemis[1]);
    bitPerfect->setVCOPitchModSemis(2, vcoPitchModSemis[2]);
    bitPerfect->setMasterVolume(masterVolume);
}

template <typename ParamsArray>
inline void applyParamMatrixToDrSid(const SidRuntimeModel& runtime,
                                    DrSidEngine* drSid,
                                    const ParamsArray& params) noexcept {
    if (!drSid) return;
    float kickTune = std::clamp(std::isfinite(params[(size_t)kParamDrSidKickTune]) ? (float)params[(size_t)kParamDrSidKickTune] : 0.0f, 0.0f, 1.0f);
    float kickDecay = std::clamp(std::isfinite(params[(size_t)kParamDrSidKickDecay]) ? (float)params[(size_t)kParamDrSidKickDecay] : 0.0f, 0.0f, 1.0f);
    float snareTone = std::clamp(std::isfinite(params[(size_t)kParamDrSidSnareTone]) ? (float)params[(size_t)kParamDrSidSnareTone] : 0.0f, 0.0f, 1.0f);
    float snareSnap = std::clamp(std::isfinite(params[(size_t)kParamDrSidSnareSnap]) ? (float)params[(size_t)kParamDrSidSnareSnap] : 0.0f, 0.0f, 1.0f);
    float hatTune = std::clamp(std::isfinite(params[(size_t)kParamDrSidHatTune]) ? (float)params[(size_t)kParamDrSidHatTune] : 0.0f, 0.0f, 1.0f);
    float hatDecay = std::clamp(std::isfinite(params[(size_t)kParamDrSidHatDecay]) ? (float)params[(size_t)kParamDrSidHatDecay] : 0.0f, 0.0f, 1.0f);
    float cowbellTune = std::clamp(std::isfinite(params[(size_t)kParamDrSidCowbellTune]) ? (float)params[(size_t)kParamDrSidCowbellTune] : 0.0f, 0.0f, 1.0f);
    float tomTune = std::clamp(std::isfinite(params[(size_t)kParamDrSidTomTune]) ? (float)params[(size_t)kParamDrSidTomTune] : 0.0f, 0.0f, 1.0f);
    float drumVolume = std::clamp(std::isfinite(params[(size_t)kParamDrSidVolume]) ? (float)params[(size_t)kParamDrSidVolume] : 0.0f, 0.0f, 1.0f);
    for (const auto& route : kDrSidParamMatrixRouteDefs) {
        const float delta = sidParamMatrixDepthDeltaForRoute(runtime, params, route);
        if (std::fabs(delta) <= 1.0e-6f) continue;
        switch (route.targetPid) {
            case kParamDrSidKickTune: kickTune = std::clamp(kickTune + delta, 0.0f, 1.0f); break;
            case kParamDrSidKickDecay: kickDecay = std::clamp(kickDecay + delta, 0.0f, 1.0f); break;
            case kParamDrSidSnareTone: snareTone = std::clamp(snareTone + delta, 0.0f, 1.0f); break;
            case kParamDrSidSnareSnap: snareSnap = std::clamp(snareSnap + delta, 0.0f, 1.0f); break;
            case kParamDrSidHatTune: hatTune = std::clamp(hatTune + delta, 0.0f, 1.0f); break;
            case kParamDrSidHatDecay: hatDecay = std::clamp(hatDecay + delta, 0.0f, 1.0f); break;
            case kParamDrSidCowbellTune: cowbellTune = std::clamp(cowbellTune + delta, 0.0f, 1.0f); break;
            case kParamDrSidTomTune: tomTune = std::clamp(tomTune + delta, 0.0f, 1.0f); break;
            case kParamDrSidVolume: drumVolume = std::clamp(drumVolume + delta, 0.0f, 1.0f); break;
            default: break;
        }
    }
    drSid->setKickTune(kickTune);
    drSid->setKickDecay(kickDecay);
    drSid->setSnareTone(snareTone);
    drSid->setSnareSnap(snareSnap);
    drSid->setHatTune(hatTune);
    drSid->setHatDecay(hatDecay);
    drSid->setCowbellTune(cowbellTune);
    drSid->setTomTune(tomTune);
    const float masterVolume = std::isfinite(params[(size_t)kParamMasterVolume]) ? (float)params[(size_t)kParamMasterVolume] : 0.0f;
    drSid->setMasterVolume(sidCanonicalDrSidBusGain(masterVolume, drumVolume));
}

} // namespace ArpSID
