#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace ArpSID {

struct SidMeasuredPosterior {
    float confidence = 0.0f;
    float cutoff_bias = 0.0f;
    float resonance_bias = 0.0f;
    float output_gain_bias = 0.0f;
    float noise_bias = 0.0f;
    float waveform_memory_bias = 0.0f;
    float leakage_bias = 0.0f;
    float dc_bias_millivolts = 0.0f;
    float supply_sag_bias = 0.0f;
    float thermal_tracking_bias = 0.0f;
    uint32_t source_signature = 0;
    uint32_t stable_chip_identity = 0;

    void sanitize() noexcept {
        auto fix = [](float& v, float lo, float hi) noexcept {
            if (!std::isfinite(v)) v = 0.0f;
            v = std::clamp(v, lo, hi);
        };
        fix(confidence, 0.0f, 1.0f);
        fix(cutoff_bias, -1.0f, 1.0f);
        fix(resonance_bias, -1.0f, 1.0f);
        fix(output_gain_bias, -24.0f, 24.0f);
        fix(noise_bias, -1.0f, 1.0f);
        fix(waveform_memory_bias, -1.0f, 1.0f);
        fix(leakage_bias, -1.0f, 1.0f);
        fix(dc_bias_millivolts, -250.0f, 250.0f);
        fix(supply_sag_bias, -1.0f, 1.0f);
        fix(thermal_tracking_bias, -1.0f, 1.0f);
    }
};

} // namespace ArpSID
