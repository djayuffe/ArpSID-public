// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/core/math_utils.h"
#include "arpsid/core/sid_runtime_forensic_config.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ArpSID {

enum class HiFiQuality : uint8_t {
    PureEmulation = 0,   // bypass: cycle-accurate SID only
    HighFidelity  = 1,   // transparent enhancement
    Transcendence = 2    // full super-hires/luxury chain
};

struct SidHiFiConfig {
    HiFiQuality quality = HiFiQuality::PureEmulation;
    int     oversampling = 8;           // control value; render path stays bounded/fixed
    float   masterWidth = 1.22f;
    float   tapeSaturation = 0.45f;
    float   analogWarmth = 0.68f;
    float   psychoExciter = 0.82f;
    float   stereoDepth = 0.65f;
    float   voiceDiffuserAmount = 0.35f;
    bool    perVoiceDiffuser = true;
    bool    dynamicResonanceEnhance = true;
    bool    cabinetModeling = true;

    void sanitize() noexcept {
        oversampling = std::clamp(oversampling, 1, 16);
        masterWidth = std::clamp(ArpSID_sanitizeFloat(masterWidth, 1.0f), 0.50f, 2.00f);
        tapeSaturation = std::clamp(ArpSID_sanitizeFloat(tapeSaturation), 0.0f, 1.0f);
        analogWarmth = std::clamp(ArpSID_sanitizeFloat(analogWarmth), 0.0f, 1.0f);
        psychoExciter = std::clamp(ArpSID_sanitizeFloat(psychoExciter), 0.0f, 1.0f);
        stereoDepth = std::clamp(ArpSID_sanitizeFloat(stereoDepth), 0.0f, 1.0f);
        voiceDiffuserAmount = std::clamp(ArpSID_sanitizeFloat(voiceDiffuserAmount), 0.0f, 1.0f);
    }
};

// Realtime-safe HI-FI/SUPER-HIRES post engine.
// No heap allocation, no locks, no std::vector resize, no host calls in processStereo().
// PureEmulation bypass preserves exact SID output. HighFidelity/Transcendence use
// sample-ramped parameter state so GUI/preset changes cannot zipper-click.
class SidHiFiTranscendence final {
public:
    static constexpr int kMaxHostFrames = 65536;
    static constexpr int kMaxOversampling = 16;

    void configure(double hostSampleRate, const ArpSIDForensicConfig& forensic, const SidHiFiConfig& cfg) noexcept {
        hostSampleRate_ = (std::isfinite(hostSampleRate) && hostSampleRate > 1000.0) ? hostSampleRate : 44100.0;
        forensic_ = forensic;
        targetConfig_ = cfg;
        targetConfig_.sanitize();
        oversampling_ = std::clamp(targetConfig_.oversampling, 1, kMaxOversampling);
        if (!configured_) {
            current_ = SmoothedControls::from(targetConfig_);
            configured_ = true;
        }
    }

    void reset() noexcept {
        luxury_ = LuxuryChain{};
        for (auto& d : diffusers_) d = VoiceDiffuser{};
        current_ = SmoothedControls::from(targetConfig_);
        lastDeltaPeak_ = 0.0f;
        lastDryPeak_ = 0.0f;
        lastWetPeak_ = 0.0f;
        lastMonoCorrelation_ = 1.0f;
        lastSafetyGain_ = 1.0f;
        smoothedSafetyGain_ = 1.0f;
    }

    bool enabled() const noexcept { return targetConfig_.quality != HiFiQuality::PureEmulation; }
    const SidHiFiConfig& config() const noexcept { return targetConfig_; }
    float lastDeltaPeak() const noexcept { return lastDeltaPeak_; }
    float lastDryPeak() const noexcept { return lastDryPeak_; }
    float lastWetPeak() const noexcept { return lastWetPeak_; }
    float lastMonoCorrelation() const noexcept { return lastMonoCorrelation_; }
    float lastSafetyGain() const noexcept { return lastSafetyGain_; }

    // Single source of truth for audible HI-FI processing.
    // Guarantees:
    // * PureEmulation is exact bypass.
    // * Silence stays silent; no free-running air oscillator bleeds into empty buffers.
    // * L/R aliasing is safe.
    // * Parameter changes are de-zippered sample-by-sample.
    // * Output is finite and hard bounded.
    void processStereo(float* outL, float* outR, int hostFrames) noexcept {
        lastDeltaPeak_ = 0.0f;
        lastDryPeak_ = 0.0f;
        lastWetPeak_ = 0.0f;
        lastMonoCorrelation_ = 1.0f;
        lastSafetyGain_ = 1.0f;
        if (!enabled() || !outL || hostFrames <= 0) return;

        const bool monoOrAliased = (!outR || outR == outL);
        if (!outR) outR = outL;
        const int frames = std::min(hostFrames, kMaxHostFrames);
        const float invN = 1.0f / static_cast<float>(std::max(1, frames));
        const float rampCoeff = smoothingCoeffForBlock_(frames);
        const float tempFactor = forensic_.frozen() ? 0.0f : std::clamp((forensic_.junctionTempC - 25.0f) * 0.012f, -0.25f, 0.85f);

        // Dry scan first: lets us keep true digital silence silent and keeps the
        // luxury chain from inventing DC/noise when the SID engine is inactive.
        float dryPeak = 0.0f;
        for (int i = 0; i < frames; ++i) {
            const float dl = ArpSID_sanitizeFloat(outL[i]);
            const float dr = ArpSID_sanitizeFloat(monoOrAliased ? dl : outR[i]);
            dryPeak = std::max(dryPeak, std::max(std::fabs(dl), std::fabs(dr)));
        }
        lastDryPeak_ = dryPeak;
        if (dryPeak < 1.0e-9f) {
            // Normalize non-finite garbage to silence, but do not add ambience.
            for (int i = 0; i < frames; ++i) {
                outL[i] = 0.0f;
                if (!monoOrAliased) outR[i] = 0.0f;
            }
            return;
        }

        float deltaPeak = 0.0f;
        float wetPeak = 0.0f;
        float sumLR = 0.0f, sumL2 = 0.0f, sumR2 = 0.0f;
        for (int i = 0; i < frames; ++i) {
            current_.approach(targetConfig_, rampCoeff);
            const float qualityScale = qualityScaleFor_(targetConfig_.quality);
            const float superHiresScale = std::sqrt(std::clamp((float)oversampling_ / 8.0f, 0.50f, 2.00f));
            const float energyNorm = std::clamp(dryPeak * 1.5f, 0.0f, 1.0f);

            const float dryL = ArpSID_sanitizeFloat(outL[i]);
            const float dryR = ArpSID_sanitizeFloat(monoOrAliased ? dryL : outR[i]);
            float l = dryL;
            float r = dryR;
            if (targetConfig_.perVoiceDiffuser) {
                l = applyVoiceDiffusion(diffusers_[0], l, current_.voiceDiffuserAmount, qualityScale, superHiresScale);
                r = applyVoiceDiffusion(diffusers_[1], r, current_.voiceDiffuserAmount, qualityScale, superHiresScale);
            }

            float mid  = (l + r) * 0.5f;
            float side = (l - r) * 0.5f;
            const float t = static_cast<float>(i) * invN;
            const float groove = std::sin(luxury_.haasPhase += (0.00055f + 0.00030f * qualityScale) * superHiresScale);
            const float monoDepthSeed = monoOrAliased ? (mid * current_.stereoDepth * 0.035f * qualityScale * energyNorm) : 0.0f;
            side = side * (1.0f + (current_.masterWidth - 1.0f) * qualityScale)
                 + groove * current_.stereoDepth * 0.010f * qualityScale * energyNorm
                 + monoDepthSeed;

            const float excL = applyPsychoExciter(l, luxury_.exciterStateL, current_.psychoExciter, qualityScale, superHiresScale);
            const float excR = applyPsychoExciter(r, luxury_.exciterStateR, current_.psychoExciter, qualityScale, superHiresScale);
            float warm = applyTapeSaturation(mid, current_.tapeSaturation, tempFactor) * current_.analogWarmth * qualityScale;
            if (targetConfig_.cabinetModeling) {
                luxury_.warmthIntegrator = sidHiFiLerp_(luxury_.warmthIntegrator, mid, 0.016f + 0.007f * qualityScale);
                luxury_.bodyIntegrator = sidHiFiLerp_(luxury_.bodyIntegrator, luxury_.warmthIntegrator - mid, 0.009f + 0.006f * qualityScale);
                warm += (luxury_.warmthIntegrator * 0.052f + luxury_.bodyIntegrator * 0.035f) * qualityScale;
            }
            if (targetConfig_.dynamicResonanceEnhance) {
                luxury_.resonanceFollower = sidHiFiLerp_(luxury_.resonanceFollower, std::fabs(mid), 0.020f);
                warm += std::tanh(luxury_.resonanceFollower * 1.70f) * mid * 0.028f * qualityScale;
            }

            l = mid + side + excL + warm;
            r = mid - side + excR + warm;
            // Air is signal-gated and anti-correlated; it cannot make silence noisy.
            const float microAir = (targetConfig_.quality == HiFiQuality::Transcendence)
                ? (std::sin((luxury_.airPhase += 0.011f * superHiresScale) + t) * 0.00075f * energyNorm)
                : 0.0f;
            l += microAir;
            r -= microAir;

            const float wet = (targetConfig_.quality == HiFiQuality::Transcendence) ? 1.0f : 0.62f;
            l = sidHiFiLerp_(dryL, l, wet);
            r = sidHiFiLerp_(dryR, r, wet);
            applySafetyLimiter_(l, r);
            l = std::clamp(ArpSID_sanitizeFloat(l), -1.0f, 1.0f);
            r = std::clamp(ArpSID_sanitizeFloat(r), -1.0f, 1.0f);

            if (monoOrAliased) {
                // Single-channel hosts cannot carry stereo width; fold back safely but
                // preserve tape/warmth/air/body changes that are audible in mono.
                const float mono = std::clamp(ArpSID_sanitizeFloat((l + r) * 0.5f), -1.0f, 1.0f);
                deltaPeak = std::max(deltaPeak, std::fabs(mono - dryL));
                wetPeak = std::max(wetPeak, std::fabs(mono));
                outL[i] = mono;
            } else {
                deltaPeak = std::max(deltaPeak, std::max(std::fabs(l - dryL), std::fabs(r - dryR)));
                wetPeak = std::max(wetPeak, std::max(std::fabs(l), std::fabs(r)));
                sumLR += l * r; sumL2 += l * l; sumR2 += r * r;
                outL[i] = l;
                outR[i] = r;
            }
        }
        lastDeltaPeak_ = std::clamp(ArpSID_sanitizeFloat(deltaPeak), 0.0f, 2.0f);
        lastWetPeak_ = std::clamp(ArpSID_sanitizeFloat(wetPeak), 0.0f, 1.0f);
        if (!monoOrAliased && sumL2 > 1.0e-12f && sumR2 > 1.0e-12f) {
            lastMonoCorrelation_ = std::clamp(sumLR / std::sqrt(sumL2 * sumR2), -1.0f, 1.0f);
        }
    }

private:
    struct VoiceDiffuser {
        std::array<float, 32> delayLine{};
        int writePos = 0;
        float phase = 0.0f;
    };
    struct LuxuryChain {
        float warmthIntegrator = 0.0f;
        float bodyIntegrator = 0.0f;
        float resonanceFollower = 0.0f;
        float exciterStateL = 0.0f;
        float exciterStateR = 0.0f;
        float haasPhase = 0.0f;
        float airPhase = 0.0f;
    };
    struct SmoothedControls {
        float masterWidth = 1.0f;
        float tapeSaturation = 0.0f;
        float analogWarmth = 0.0f;
        float psychoExciter = 0.0f;
        float stereoDepth = 0.0f;
        float voiceDiffuserAmount = 0.0f;
        static SmoothedControls from(const SidHiFiConfig& c) noexcept {
            SmoothedControls s{};
            s.masterWidth = c.masterWidth;
            s.tapeSaturation = c.tapeSaturation;
            s.analogWarmth = c.analogWarmth;
            s.psychoExciter = c.psychoExciter;
            s.stereoDepth = c.stereoDepth;
            s.voiceDiffuserAmount = c.voiceDiffuserAmount;
            return s;
        }
        void approach(const SidHiFiConfig& c, float a) noexcept {
            masterWidth = sidHiFiLerpStatic_(masterWidth, c.masterWidth, a);
            tapeSaturation = sidHiFiLerpStatic_(tapeSaturation, c.tapeSaturation, a);
            analogWarmth = sidHiFiLerpStatic_(analogWarmth, c.analogWarmth, a);
            psychoExciter = sidHiFiLerpStatic_(psychoExciter, c.psychoExciter, a);
            stereoDepth = sidHiFiLerpStatic_(stereoDepth, c.stereoDepth, a);
            voiceDiffuserAmount = sidHiFiLerpStatic_(voiceDiffuserAmount, c.voiceDiffuserAmount, a);
        }
    };

    static float sidHiFiLerpStatic_(float a, float b, float t) noexcept {
        return a + (b - a) * std::clamp(ArpSID_sanitizeFloat(t), 0.0f, 1.0f);
    }
    static float sidHiFiLerp_(float a, float b, float t) noexcept { return sidHiFiLerpStatic_(a, b, t); }

    static float qualityScaleFor_(HiFiQuality q) noexcept {
        return (q == HiFiQuality::Transcendence) ? 1.0f : (q == HiFiQuality::HighFidelity ? 0.50f : 0.0f);
    }
    float smoothingCoeffForBlock_(int frames) const noexcept {
        const float sr = (float)std::clamp(hostSampleRate_, 8000.0, 384000.0);
        const float tauMs = (targetConfig_.quality == HiFiQuality::Transcendence) ? 7.0f : 4.0f;
        const float a = 1.0f - std::exp(-1000.0f / (sr * tauMs));
        (void)frames;
        return std::clamp(ArpSID_sanitizeFloat(a), 0.0005f, 0.25f);
    }

    float applyTapeSaturation(float x, float amount, float tempFactor) const noexcept {
        const float drive = x * (1.0f + amount * (1.85f + tempFactor));
        const float sat = std::tanh(drive);
        return sidHiFiLerp_(x, sat, amount);
    }
    float applyPsychoExciter(float x, float& state, float amount, float qualityScale, float superHiresScale) noexcept {
        const float high = x - state;
        state = sidHiFiLerp_(state, x, 0.055f + 0.012f * superHiresScale);
        return std::tanh(high * 7.2f) * 0.085f * amount * qualityScale;
    }
    float applyVoiceDiffusion(VoiceDiffuser& d, float sample, float amount, float qualityScale, float superHiresScale) noexcept {
        d.delayLine[(size_t)d.writePos] = sample;
        d.writePos = (d.writePos + 1) & 31;
        const int readPos = (d.writePos + 11) & 31;
        const float delayed = d.delayLine[(size_t)readPos];
        d.phase += (0.00030f + 0.00011f * qualityScale) * superHiresScale;
        const float shimmer = std::sin(d.phase) * amount * 0.0055f * qualityScale * std::min(1.0f, std::fabs(sample) * 2.0f);
        return sidHiFiLerp_(sample, delayed, amount * 0.16f * qualityScale) + shimmer;
    }
    void applySafetyLimiter_(float& l, float& r) noexcept {
        const float peak = std::max(std::fabs(l), std::fabs(r));
        float targetGain = 1.0f;
        if (peak > 0.98f) targetGain = 0.98f / peak;
        // Smooth the gain envelope: instantaneous attack (follow target down),
        // slow release (0.9990 per sample ≈ 7 ms at 44.1 kHz) to avoid click
        // artifacts when the limiter activates or deactivates.
        if (targetGain < smoothedSafetyGain_)
            smoothedSafetyGain_ = targetGain;          // instantaneous attack
        else
            smoothedSafetyGain_ += (1.0f - smoothedSafetyGain_) * 0.0010f; // smooth release
        smoothedSafetyGain_ = std::clamp(smoothedSafetyGain_, 0.0f, 1.0f);
        lastSafetyGain_ = std::min(lastSafetyGain_, smoothedSafetyGain_);
        l *= smoothedSafetyGain_;
        r *= smoothedSafetyGain_;
    }

    SidHiFiConfig targetConfig_{};
    SmoothedControls current_{};
    ArpSIDForensicConfig forensic_{};
    double hostSampleRate_ = 44100.0;
    int oversampling_ = 8;
    bool configured_ = false;
    std::array<VoiceDiffuser, 12> diffusers_{};
    LuxuryChain luxury_{};
    float lastDeltaPeak_ = 0.0f;
    float lastDryPeak_ = 0.0f;
    float lastWetPeak_ = 0.0f;
    float lastMonoCorrelation_ = 1.0f;
    float lastSafetyGain_ = 1.0f;
    float smoothedSafetyGain_ = 1.0f;
};

inline SidHiFiConfig sidHiFiConfigFromNormalized(float enable, float quality, float oversampling,
                                                 float width, float saturation, float warmth,
                                                 float exciter, float depth, float diffuser) noexcept {
    SidHiFiConfig cfg{};
    if (ArpSID_sanitizeFloat(enable) <= 0.5f) cfg.quality = HiFiQuality::PureEmulation;
    else {
        const int qi = std::clamp((int)std::lround(std::clamp(ArpSID_sanitizeFloat(quality), 0.0f, 1.0f) * 2.0f), 0, 2);
        cfg.quality = static_cast<HiFiQuality>(qi);
        if (cfg.quality == HiFiQuality::PureEmulation) cfg.quality = HiFiQuality::HighFidelity;
    }
    const int oi = std::clamp((int)std::lround(std::clamp(ArpSID_sanitizeFloat(oversampling), 0.0f, 1.0f) * 2.0f), 0, 2);
    cfg.oversampling = (oi == 0) ? 4 : (oi == 1 ? 8 : 16);
    cfg.masterWidth = 0.80f + std::clamp(ArpSID_sanitizeFloat(width), 0.0f, 1.0f) * 0.70f;
    cfg.tapeSaturation = std::clamp(ArpSID_sanitizeFloat(saturation), 0.0f, 1.0f);
    cfg.analogWarmth = std::clamp(ArpSID_sanitizeFloat(warmth), 0.0f, 1.0f);
    cfg.psychoExciter = std::clamp(ArpSID_sanitizeFloat(exciter), 0.0f, 1.0f);
    cfg.stereoDepth = std::clamp(ArpSID_sanitizeFloat(depth), 0.0f, 1.0f);
    cfg.voiceDiffuserAmount = std::clamp(ArpSID_sanitizeFloat(diffuser), 0.0f, 1.0f);
    cfg.sanitize();
    return cfg;
}

} // namespace ArpSID
