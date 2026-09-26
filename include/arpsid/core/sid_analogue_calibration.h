// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include "arpsid/core/sid_variant_profile.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <cstdlib>

namespace ArpSID {

struct SidAnalogueAnchor {
    uint16_t reg = 0;
    float hz = 20.0f;
};

struct SidAnalogueCalibration {
    SidFamily family = SidFamily::MOS8580;
    uint8_t revision = 5u;
    std::array<SidAnalogueAnchor, 9> cutoffAnchors{};
    std::array<float, 16> resonanceQ{};
    std::array<float, 16> combinedWavePullDown{};
    float dcOffsetMillivolts = 0.0f;
    float outputGain = 1.0f;
    float waveformCoupling = 0.24f;
    float waveformLeak = 0.04f;
    float externalRcLowpassHz = 18500.0f;
    float externalRcHighpassHz = 16.0f;
    bool externalRcDefaultEnabled = true;

    void sanitize() noexcept {
        const bool is6581 = family == SidFamily::MOS6581;
        if (revision == 0u) revision = is6581 ? 3u : 5u;
        for (auto& a : cutoffAnchors) {
            if (!std::isfinite(a.hz) || a.hz < 1.0f) a.hz = is6581 ? 30.0f : 18.0f;
            a.reg = std::min<uint16_t>(a.reg, 2047u);
        }
        // ARPSID_RT_SORT_CLASSIFICATION: setup/control-only calibration data;
        // never reached from render and allocation is permitted here.
        std::sort(cutoffAnchors.begin(), cutoffAnchors.end(), [](const auto& a, const auto& b){ return a.reg < b.reg; });
        cutoffAnchors.front().reg = 0u;
        cutoffAnchors.back().reg = 2047u;
        for (auto& q : resonanceQ) {
            if (!std::isfinite(q) || q <= 0.0f) q = 0.74f;
            q = std::clamp(q, 0.20f, 12.0f);
        }
        for (auto& p : combinedWavePullDown) {
            if (!std::isfinite(p)) p = 0.0f;
            p = std::clamp(p, 0.0f, 1.0f);
        }
        if (!std::isfinite(dcOffsetMillivolts)) dcOffsetMillivolts = 0.0f;
        if (!std::isfinite(outputGain) || outputGain <= 0.0f) outputGain = 1.0f;
        if (!std::isfinite(waveformCoupling)) waveformCoupling = is6581 ? 0.38f : 0.24f;
        if (!std::isfinite(waveformLeak)) waveformLeak = is6581 ? 0.09f : 0.04f;
        if (!std::isfinite(externalRcLowpassHz) || externalRcLowpassHz < 1000.0f) externalRcLowpassHz = is6581 ? 14500.0f : 18500.0f;
        if (!std::isfinite(externalRcHighpassHz) || externalRcHighpassHz < 1.0f) externalRcHighpassHz = 16.0f;
        outputGain = std::clamp(outputGain, 0.10f, 4.0f);
        waveformCoupling = std::clamp(waveformCoupling, 0.0f, 1.0f);
        waveformLeak = std::clamp(waveformLeak, 0.0f, 0.5f);
        externalRcLowpassHz = std::clamp(externalRcLowpassHz, 1000.0f, 48000.0f);
        externalRcHighpassHz = std::clamp(externalRcHighpassHz, 1.0f, 200.0f);
    }
};

inline constexpr std::array<SidAnalogueAnchor, 9> sidCutoff6581R2Anchors() noexcept {
    return {{{0, 34.0f}, {64, 52.0f}, {192, 102.0f}, {384, 250.0f}, {768, 760.0f}, {1152, 1850.0f}, {1536, 3900.0f}, {1856, 6900.0f}, {2047, 9300.0f}}};
}
inline constexpr std::array<SidAnalogueAnchor, 9> sidCutoff6581R3Anchors() noexcept {
    return {{{0, 30.0f}, {64, 55.0f}, {192, 120.0f}, {384, 320.0f}, {768, 1100.0f}, {1152, 2800.0f}, {1536, 5400.0f}, {1856, 8800.0f}, {2047, 11800.0f}}};
}
inline constexpr std::array<SidAnalogueAnchor, 9> sidCutoff6581R4Anchors() noexcept {
    return {{{0, 28.0f}, {64, 62.0f}, {192, 150.0f}, {384, 420.0f}, {768, 1450.0f}, {1152, 3500.0f}, {1536, 6500.0f}, {1856, 10100.0f}, {2047, 13000.0f}}};
}
inline constexpr std::array<SidAnalogueAnchor, 9> sidCutoff8580R5Anchors() noexcept {
    return {{{0, 18.0f}, {64, 28.0f}, {192, 55.0f}, {384, 180.0f}, {768, 900.0f}, {1152, 3100.0f}, {1536, 7600.0f}, {1856, 13200.0f}, {2047, 19800.0f}}};
}

inline SidAnalogueCalibration sidDefaultAnalogueCalibration(SidFamily family, uint8_t revision) noexcept {
    SidAnalogueCalibration c{};
    c.family = family;
    c.revision = revision;
    const bool is6581 = family == SidFamily::MOS6581;
    if (is6581) {
        if (revision <= 2u) c.cutoffAnchors = sidCutoff6581R2Anchors();
        else if (revision >= 4u) c.cutoffAnchors = sidCutoff6581R4Anchors();
        else c.cutoffAnchors = sidCutoff6581R3Anchors();
        for (size_t i = 0; i < c.resonanceQ.size(); ++i) {
            const float n = static_cast<float>(i) / 15.0f;
            const float revBias = revision <= 2u ? 1.18f : (revision >= 4u ? 0.90f : 1.0f);
            // 6581 revisions are intentionally ordered by measured resonance aggressiveness:
            // early R2 parts tend to ring more, R3 is the middle reference, and later
            // R4 parts are comparatively tamer. Keep this monotonic because the
            // release regression tests use it as a calibration sanity check.
            c.resonanceQ[i] = (0.70f + 0.56f * n + 4.6f * n * n + 2.4f * n * n * n) * revBias;
            c.combinedWavePullDown[i] = std::clamp(0.28f + 0.032f * static_cast<float>(i) + (revision <= 2u ? 0.08f : 0.0f), 0.0f, 0.92f);
        }
        c.dcOffsetMillivolts = revision <= 2u ? 28.0f : (revision >= 4u ? 14.0f : 20.0f);
        c.outputGain = revision <= 2u ? 1.16f : (revision >= 4u ? 1.05f : 1.10f);
        c.waveformCoupling = revision <= 2u ? 0.46f : (revision >= 4u ? 0.34f : 0.39f);
        c.waveformLeak = revision <= 2u ? 0.13f : (revision >= 4u ? 0.08f : 0.10f);
        c.externalRcLowpassHz = revision <= 2u ? 13200.0f : (revision >= 4u ? 15200.0f : 14200.0f);
        c.externalRcHighpassHz = 16.0f;
    } else {
        c.cutoffAnchors = sidCutoff8580R5Anchors();
        for (size_t i = 0; i < c.resonanceQ.size(); ++i) {
            const float n = static_cast<float>(i) / 15.0f;
            c.resonanceQ[i] = 0.74f + 0.85f * n + 8.2f * n * n + 6.8f * n * n * n;
            c.combinedWavePullDown[i] = std::clamp(0.10f + 0.010f * static_cast<float>(i), 0.0f, 0.38f);
        }
        c.dcOffsetMillivolts = 3.0f;
        c.outputGain = 1.0f;
        c.waveformCoupling = 0.22f;
        c.waveformLeak = 0.035f;
        c.externalRcLowpassHz = 18500.0f;
        c.externalRcHighpassHz = 16.0f;
    }
    c.externalRcDefaultEnabled = true;
    c.sanitize();
    return c;
}

inline SidAnalogueCalibration sidCalibrationForVariant(const SidVariantProfile& profile) noexcept {
    const uint8_t rev = static_cast<uint8_t>(profile.chip_revision_code & 0x0Fu);
    const uint8_t canonicalRev = rev ? rev : (profile.family == SidFamily::MOS6581 ? 3u : 5u);
    return sidDefaultAnalogueCalibration(profile.family, canonicalRev);
}

inline bool sidParseCalibrationAssignment(std::string_view line, SidAnalogueCalibration& out) noexcept {
    const auto trim = [](std::string_view v) noexcept {
        while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '\r' || v.front() == '\n')) v.remove_prefix(1);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n')) v.remove_suffix(1);
        return v;
    };
    line = trim(line);
    if (line.empty() || line.front() == '#') return true;
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) return false;
    const std::string_view key = trim(line.substr(0, eq));
    const std::string_view val = trim(line.substr(eq + 1));
    char tmp[64] = {};
    const size_t n = std::min<size_t>(val.size(), sizeof(tmp) - 1u);
    for (size_t i = 0; i < n; ++i) tmp[i] = val[i];
    char* end = nullptr;
    const float f = std::strtof(tmp, &end);
    if (end == tmp) return false;
    if (key == "dc_mv") out.dcOffsetMillivolts = f;
    else if (key == "output_gain") out.outputGain = f;
    else if (key == "waveform_coupling") out.waveformCoupling = f;
    else if (key == "waveform_leak") out.waveformLeak = f;
    else if (key == "external_rc_lowpass_hz") out.externalRcLowpassHz = f;
    else if (key == "external_rc_highpass_hz") out.externalRcHighpassHz = f;
    else return false;
    out.sanitize();
    return true;
}

} // namespace ArpSID
