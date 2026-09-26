// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "sid_runtime_event_materializer.h"
#include "sid_runtime_model.h"
#include "sid_gm_drum_map.h"
#include "parameter_ids.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ArpSID {

struct SidGMDrSidPromotionDecision final {
    bool promote = false;
    uint32_t disableSynthParam = static_cast<uint32_t>(kParamSynthModeEnable);
    float disableSynthValue = 0.0f;
    uint32_t enableDrSidParam = static_cast<uint32_t>(kParamDrSidEnable);
    float enableDrSidValue = 1.0f;
};

inline bool sidCanonicalGMDrumPromotionCandidate(uint8_t channel, uint8_t note) noexcept {
    return sidIsGMDrumMessage(channel, note);
}

// v909 Classic-mode authority closure: a GM drum message may only promote the
// runtime into DrSID when the host wrapper explicitly allows auto-promotion.
// The user-selected Classic/BitPerfect (or SynthMode) render mode is authority;
// channel-10 traffic must never silently hijack it. Wrappers derive
// `autoPromotionAllowed` from the component flavor policy plus the
// kParamAutoGmDrumPromotion opt-in (see componentFlavorAllowsDrSidAutoPromotion).
inline SidGMDrSidPromotionDecision sidCanonicalEvaluateGMDrSidPromotion(uint8_t channel,
                                                                        uint8_t note,
                                                                        bool drSidEngineAvailable,
                                                                        bool alreadyDrSidMode,
                                                                        bool autoPromotionAllowed) noexcept {
    SidGMDrSidPromotionDecision d{};
    d.promote = autoPromotionAllowed && drSidEngineAvailable && !alreadyDrSidMode &&
                sidCanonicalGMDrumPromotionCandidate(channel, note);
    return d;
}

// Shared flavor/opt-in law for GM DrSID auto-promotion. Dedicated drum flavors
// are drum machines by construction and always allow promotion; the Hybrid
// (Classic) flavor requires the explicit kParamAutoGmDrumPromotion opt-in;
// Instrument and C64SidPlayer flavors never promote.
inline constexpr bool sidCanonicalGMDrumAutoPromotionAllowed(bool dedicatedDrumFlavor,
                                                             bool hybridFlavor,
                                                             bool autoGmDrumPromotionParamOn) noexcept {
    if (dedicatedDrumFlavor) return true;
    if (!hybridFlavor) return false;
    return autoGmDrumPromotionParamOn;
}

template <class Target>
inline bool sidCanonicalApplyGMDrSidPromotion(Target& target,
                                              uint8_t channel,
                                              uint8_t note,
                                              bool drSidEngineAvailable,
                                              bool alreadyDrSidMode,
                                              bool autoPromotionAllowed) noexcept {
    const SidGMDrSidPromotionDecision d = sidCanonicalEvaluateGMDrSidPromotion(channel, note,
                                                                              drSidEngineAvailable,
                                                                              alreadyDrSidMode,
                                                                              autoPromotionAllowed);
    if (!d.promote) return false;
    target.runtimeStageNormalizedParameterOnly(d.disableSynthParam, d.disableSynthValue);
    target.runtimeStageNormalizedParameterOnly(d.enableDrSidParam, d.enableDrSidValue);
    target.runtimeApplyNormalizedParameter(d.disableSynthParam, d.disableSynthValue);
    target.runtimeApplyNormalizedParameter(d.enableDrSidParam, d.enableDrSidValue);
    return true;
}

template <class RuntimeModel>
inline void sidCanonicalPushTransportTempoIfChanged(RuntimeModel& model,
                                                    bool playing,
                                                    float bpm,
                                                    int frameCount,
                                                    int sampleOffset = 0) noexcept {
    const float cleanTempo = std::clamp(std::isfinite(bpm) ? bpm : 120.0f, 1.0f, 400.0f);
    const bool transportChanged = model.transportPlayingFlag() != playing;
    const bool tempoChanged = std::fabs(model.hostTempoBpm() - cleanTempo) > 1.0e-4f;
    model.setTransportPlayingFlag(playing);
    model.setHostTempoBpm(cleanTempo);
    if (transportChanged) {
        sidWrapperPushEvent(model, sidMaterializeTransportChange(playing, sampleOffset), frameCount);
    }
    if (tempoChanged) {
        sidWrapperPushEvent(model, sidMaterializeTempoChange(cleanTempo, sampleOffset), frameCount);
    }
}

} // namespace ArpSID
