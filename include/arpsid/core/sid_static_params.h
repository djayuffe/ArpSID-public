#pragma once
#include "sid_variant_profile.h"
#include "sid_measured_posterior.h"
#include "arpsid/core/sid_analogue_calibration.h"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace ArpSID {

struct SidStaticParams {
    SidVariantProfile variant_profile{};
    float filter_cutoff_min_hz = 18.0f;
    float filter_cutoff_max_hz = 18600.0f;
    float waveform_coupling = 0.24f;
    float waveform_leak = 0.04f;
    float resonance_bias = 0.12f;
    float output_drive = 1.0f;
    float output_trim = 1.0f;
    float board_crosstalk = 0.0f;
    float supply_sag_sensitivity = 0.0f;
    float thermal_tracking = 0.0f;
    float dc_bias_millivolts = 0.0f;
    float waveform_memory = 0.0f;
    float output_lowpass_hz = 22000.0f;
    float external_rc_lowpass_hz = 18500.0f;
    float external_rc_highpass_hz = 16.0f;
    bool external_rc_enabled = true;
    float video_clock_scale = 1.0f;
    float posterior_confidence = 0.0f;
    uint32_t stable_chip_identity = 0;

    void sanitize() noexcept {
        variant_profile.sanitize();
        const bool is6581Family = variant_profile.family == SidFamily::MOS6581;
        const float defaultMin = is6581Family ? 26.0f : 18.0f;
        const float defaultMax = is6581Family ? 11800.0f : 18600.0f;
        if (!std::isfinite(filter_cutoff_min_hz) || filter_cutoff_min_hz < 1.0f) filter_cutoff_min_hz = defaultMin;
        if (!std::isfinite(filter_cutoff_max_hz) || filter_cutoff_max_hz <= filter_cutoff_min_hz) filter_cutoff_max_hz = defaultMax;
        if (!std::isfinite(waveform_coupling)) waveform_coupling = is6581Family ? 0.38f : 0.24f;
        if (!std::isfinite(waveform_leak)) waveform_leak = is6581Family ? 0.09f : 0.04f;
        if (!std::isfinite(resonance_bias)) resonance_bias = is6581Family ? 0.22f : 0.12f;
        if (!std::isfinite(output_drive) || output_drive <= 0.0f) output_drive = is6581Family ? 1.22f : 1.04f;
        if (!std::isfinite(output_trim) || output_trim <= 0.0f) output_trim = is6581Family ? 0.92f : 1.0f;
        if (!std::isfinite(board_crosstalk)) board_crosstalk = 0.0f;
        if (!std::isfinite(supply_sag_sensitivity)) supply_sag_sensitivity = 0.0f;
        if (!std::isfinite(thermal_tracking)) thermal_tracking = 0.0f;
        if (!std::isfinite(dc_bias_millivolts)) dc_bias_millivolts = 0.0f;
        if (!std::isfinite(waveform_memory)) waveform_memory = 0.0f;
        if (!std::isfinite(output_lowpass_hz) || output_lowpass_hz < 1000.0f) output_lowpass_hz = 22000.0f;
        if (!std::isfinite(external_rc_lowpass_hz) || external_rc_lowpass_hz < 1000.0f) external_rc_lowpass_hz = is6581Family ? 14500.0f : 18500.0f;
        if (!std::isfinite(external_rc_highpass_hz) || external_rc_highpass_hz < 1.0f) external_rc_highpass_hz = 16.0f;
        if (!std::isfinite(video_clock_scale) || video_clock_scale <= 0.0f) video_clock_scale = 1.0f;
        posterior_confidence = std::clamp(std::isfinite(posterior_confidence) ? posterior_confidence : 0.0f, 0.0f, 1.0f);
    }
};

inline SidStaticParams makeSidStaticParams(const SidVariantProfile& profile) noexcept {
    SidStaticParams sp{};
    sp.variant_profile = profile;
    sp.variant_profile.sanitize();
    const bool is6581Family = sp.variant_profile.family == SidFamily::MOS6581;

    const SidAnalogueCalibration cal = sidCalibrationForVariant(sp.variant_profile);
    sp.filter_cutoff_min_hz = cal.cutoffAnchors.front().hz;
    sp.filter_cutoff_max_hz = cal.cutoffAnchors.back().hz;
    sp.waveform_coupling = cal.waveformCoupling;
    sp.waveform_leak = cal.waveformLeak;
    sp.resonance_bias = is6581Family ? 0.22f : 0.12f;
    sp.output_drive = cal.outputGain * (is6581Family ? 1.02f : 1.00f);
    sp.waveform_memory = is6581Family ? 0.18f : 0.08f;
    sp.supply_sag_sensitivity = is6581Family ? 0.18f : 0.08f;
    sp.thermal_tracking = is6581Family ? 0.18f : 0.10f;
    sp.board_crosstalk = is6581Family ? 0.05f : 0.02f;
    sp.output_trim = is6581Family ? 0.92f : 1.00f;
    sp.output_lowpass_hz = cal.externalRcLowpassHz;
    sp.external_rc_lowpass_hz = cal.externalRcLowpassHz;
    sp.external_rc_highpass_hz = cal.externalRcHighpassHz;
    sp.external_rc_enabled = cal.externalRcDefaultEnabled;
    sp.dc_bias_millivolts = cal.dcOffsetMillivolts;
    sp.video_clock_scale = sidDefaultClockHz(sp.variant_profile.video_standard) /
                           sidDefaultClockHz(SidVideoStandard::PAL);

    switch (sp.variant_profile.board_revision) {
        case SidBoardRevision::C64_Assy_250407:
            sp.board_crosstalk += 0.030f;
            sp.supply_sag_sensitivity += 0.030f;
            sp.filter_cutoff_max_hz *= 0.94f;
            break;
        case SidBoardRevision::C64_Assy_250425:
            sp.board_crosstalk += 0.020f;
            sp.filter_cutoff_max_hz *= 0.97f;
            break;
        case SidBoardRevision::C64_Assy_250466:
            sp.board_crosstalk += 0.010f;
            sp.output_lowpass_hz *= 1.03f;
            break;
        case SidBoardRevision::C64C_Assy_250469:
            sp.board_crosstalk *= 0.70f;
            sp.supply_sag_sensitivity *= 0.70f;
            sp.output_lowpass_hz *= 1.06f;
            break;
        case SidBoardRevision::C128_Generic:
            sp.board_crosstalk *= 0.85f;
            sp.output_trim *= 0.99f;
            break;
        case SidBoardRevision::CleanLabBoard:
            sp.board_crosstalk = 0.0f;
            sp.supply_sag_sensitivity *= 0.5f;
            sp.output_lowpass_hz = 22000.0f;
            break;
        case SidBoardRevision::Custom:
        case SidBoardRevision::Unknown:
        default:
            break;
    }

    switch (sp.variant_profile.output_stage) {
        case SidOutputStageProfile::StockC64_6581:
            sp.output_trim *= 0.92f;
            sp.dc_bias_millivolts += 18.0f;
            sp.output_lowpass_hz = std::min(sp.output_lowpass_hz, 14000.0f);
            break;
        case SidOutputStageProfile::StockC64_8580:
            sp.output_trim *= 1.00f;
            sp.output_lowpass_hz = std::min(sp.output_lowpass_hz, 18500.0f);
            break;
        case SidOutputStageProfile::C64C_Modified:
            sp.output_trim *= 0.98f;
            sp.output_lowpass_hz = std::max(sp.output_lowpass_hz, 20000.0f);
            break;
        case SidOutputStageProfile::DirectLineOut:
            sp.output_trim *= 1.02f;
            sp.board_crosstalk *= 0.4f;
            sp.dc_bias_millivolts *= 0.5f;
            sp.output_lowpass_hz = 22000.0f;
            break;
        case SidOutputStageProfile::StudioCapture:
            sp.output_trim *= 1.00f;
            sp.board_crosstalk *= 0.25f;
            sp.output_lowpass_hz = 22000.0f;
            break;
        case SidOutputStageProfile::Custom:
        case SidOutputStageProfile::Unknown:
        default:
            break;
    }

    const uint32_t revNibble = (sp.variant_profile.chip_revision_code & 0x0Fu);
    if (revNibble != 0u) {
        const float revT = static_cast<float>(revNibble) * (1.0f / 15.0f);
        sp.filter_cutoff_max_hz *= (is6581Family ? (0.92f + 0.14f * revT) : (0.97f + 0.06f * revT));
        sp.resonance_bias += (is6581Family ? 0.05f : 0.03f) * (revT - 0.5f);
        sp.waveform_memory += (is6581Family ? 0.08f : 0.03f) * (revT - 0.5f);
    }

    sp.sanitize();
    return sp;
}

inline SidStaticParams resolveEffectiveSidStaticParams(const SidVariantProfile& profile,
                                                       const SidMeasuredPosterior& posterior) noexcept {
    SidStaticParams sp = makeSidStaticParams(profile);
    SidMeasuredPosterior post = posterior;
    post.sanitize();
    const float k = post.confidence;
    sp.posterior_confidence = k;
    sp.stable_chip_identity = post.stable_chip_identity;
    sp.filter_cutoff_min_hz *= (1.0f + 0.08f * k * post.cutoff_bias);
    sp.filter_cutoff_max_hz *= (1.0f + 0.16f * k * post.cutoff_bias);
    sp.resonance_bias += 0.18f * k * post.resonance_bias;
    sp.output_drive *= std::exp(std::log(10.0f) * ((post.output_gain_bias * k) / 20.0f));
    sp.waveform_memory += 0.20f * k * post.waveform_memory_bias;
    sp.waveform_leak += 0.05f * k * post.leakage_bias;
    sp.dc_bias_millivolts += k * post.dc_bias_millivolts;
    sp.supply_sag_sensitivity += 0.20f * k * post.supply_sag_bias;
    sp.thermal_tracking += 0.20f * k * post.thermal_tracking_bias;
    sp.sanitize();
    return sp;
}

} // namespace ArpSID
