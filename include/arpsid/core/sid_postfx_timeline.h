// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "parameter_ids.h"
#include "sid_event_queue.h"
#include "sid_hifi_transcendence.h"
#include "math_utils.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace ArpSID {

struct SidPostFxAutomationState final {
    float reverbMix = 0.0f;
    bool limiterEnabled = true;
    float limiterThreshold = 0.94f;
    float limiterAttackMs = 1.6f;
    float limiterReleaseMs = 356.5f;
    std::array<float, 9> hiFiNormalized{};
};

template <class ParamContainer>
inline SidPostFxAutomationState sidCapturePostFxAutomationState(const ParamContainer& p) noexcept {
    SidPostFxAutomationState s{};
    const auto unit = [](float v, float fallback = 0.0f) noexcept {
        return std::clamp(std::isfinite(v) ? v : fallback, 0.0f, 1.0f);
    };
    s.reverbMix = unit(p[(size_t)kParamReverbMix]);
    s.limiterEnabled = unit(p[(size_t)kParamOutputLimiter], 1.0f) > 0.5f;
    s.limiterThreshold = std::clamp(std::isfinite(p[(size_t)kParamLimiterThreshold])
                                        ? p[(size_t)kParamLimiterThreshold]
                                        : 0.94f,
                                    0.5f, 1.0f);
    s.limiterAttackMs = ArpSID_normToLimiterAttackMs(unit(p[(size_t)kParamLimiterAttack], 0.08f));
    s.limiterReleaseMs = ArpSID_normToLimiterReleaseMs(unit(p[(size_t)kParamLimiterRelease], 0.35f));
    constexpr std::array<int, 9> ids = {
        kParamHiFiEnable, kParamHiFiQuality, kParamHiFiOversampling,
        kParamHiFiMasterWidth, kParamHiFiTapeSaturation, kParamHiFiAnalogWarmth,
        kParamHiFiPsychoExciter, kParamHiFiStereoDepth, kParamHiFiVoiceDiffuser
    };
    for (size_t i = 0; i < ids.size(); ++i) s.hiFiNormalized[i] = unit(p[(size_t)ids[i]]);
    return s;
}

inline bool sidApplyPostFxAutomationEvent(SidPostFxAutomationState& s,
                                          const SidTimedEvent& ev) noexcept {
    if (ev.type != SidTimedEventType::AutomationPoint) return false;
    const float v = std::clamp(std::isfinite(ev.value) ? ev.value : 0.0f, 0.0f, 1.0f);
    switch (static_cast<int>(ev.target)) {
        case kParamReverbMix: s.reverbMix = v; return true;
        case kParamOutputLimiter: s.limiterEnabled = v > 0.5f; return true;
        case kParamLimiterThreshold: s.limiterThreshold = std::clamp(v, 0.5f, 1.0f); return true;
        case kParamLimiterAttack: s.limiterAttackMs = ArpSID_normToLimiterAttackMs(v); return true;
        case kParamLimiterRelease: s.limiterReleaseMs = ArpSID_normToLimiterReleaseMs(v); return true;
        default: break;
    }
    const int first = static_cast<int>(kParamHiFiEnable);
    const int last = static_cast<int>(kParamHiFiVoiceDiffuser);
    const int target = static_cast<int>(ev.target);
    if (target >= first && target <= last) {
        s.hiFiNormalized[static_cast<size_t>(target - first)] = v;
        return true;
    }
    return false;
}

inline bool sidPostFxEventTargetsHiFi(const SidTimedEvent& ev) noexcept {
    if (ev.type != SidTimedEventType::AutomationPoint) return false;
    const int target = static_cast<int>(ev.target);
    return target >= static_cast<int>(kParamHiFiEnable) &&
           target <= static_cast<int>(kParamHiFiVoiceDiffuser);
}

inline SidHiFiConfig sidHiFiConfigFromPostFxState(const SidPostFxAutomationState& s) noexcept {
    return sidHiFiConfigFromNormalized(s.hiFiNormalized[0], s.hiFiNormalized[1],
                                       s.hiFiNormalized[2], s.hiFiNormalized[3],
                                       s.hiFiNormalized[4], s.hiFiNormalized[5],
                                       s.hiFiNormalized[6], s.hiFiNormalized[7],
                                       s.hiFiNormalized[8]);
}

} // namespace ArpSID
