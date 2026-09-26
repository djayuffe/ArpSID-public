// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// digi_sampler_engine.h - RT-safe DIGI/$D418 factory/user sampler layer.

#ifndef ARPSID_ENGINES_DIGI_SAMPLER_ENGINE_H
#define ARPSID_ENGINES_DIGI_SAMPLER_ENGINE_H

#include "arpsid/gui/gui_realtime_projection_v588.h"
#include "arpsid/gui/digi_sample_bank_v596.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ArpSID {

// DIGI layer composition model (Fix #7).
//
// NOTE: This engine implements the legacy float‑mixing approach for the
// so‑called “$D418” DIGI trick. In the original C64, DIGI samples were
// written to the SID volume register ($D418) on the host bus; the low
// 4‑bit nibble carried the sample and the high nibble preserved the
// existing volume. The legacy sampler here does **not** model the C64
// bus or SID register at all – it simply mixes a floating‑point sample
// directly into the audio buffers. It remains in the codebase for
// backwards‑compatibility but is **not** an authentic $D418 implementation.
//
// Additive — DIGI mixes additively into the SID/DrSID output buffers.
// This mirrors the traditional behaviour where the sample
// appears on top of any running SID audio. Suitable for
// percussion layers alongside melodic SID voices.
//
// Exclusive — DIGI zeros the output buffers BEFORE writing its mix.
// Use when you want sample‑only playback with no SID bleed.
// Equivalent to silencing all SID voices and playing samples only.
//
// Default: Additive (preserves all existing sessions).
enum class DigiLayerMode : uint8_t {
    Additive  = 0,  ///< Layer DIGI on top of SID output (default, traditional)
    Exclusive = 1,  ///< Zero SID output, DIGI is the only audio source
};

struct DigiSamplerTelemetry {
    std::uint8_t  activeSlotCount = 0;
    std::uint8_t  configuredFactorySlots = 0;
    std::uint8_t  configuredUserImportSlots = 0;
    std::uint8_t  playingVoiceCount = 0;   ///< current active voices this block
    std::uint8_t  peakVoiceCount = 0;      ///< high-water with slow decay (~2 s)
    std::uint8_t  peakVoiceDecayTick_ = 0; ///< internal decay counter (not for display)
    std::uint8_t  stepIndex = 0;
    std::uint8_t  lastTriggeredSlot = 255;
    std::uint16_t lastTriggeredFactorySlot = 0;
    std::uint32_t triggerCount = 0;
    std::uint32_t unavailableUserImportCount = 0;
    float outputPeak = 0.0f;
};

class DigiSamplerEngine {
public:
    static constexpr int kMaxVoices = ArpSID::GUI::kDigiActiveSlotCount;
    static constexpr int kScopeLen = 128;

    void prepare(double sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 384000.0)
            ? sampleRate
            : 44100.0;
        reset();
    }

    void reset() noexcept {
        for (auto& v : voices_) v = Voice{};
        telemetry_ = DigiSamplerTelemetry{};
        telemetry_.lastTriggeredSlot = 255;
        scope_.fill(0.0f);
        scopeWritePos_ = 0u;
        haveStep_ = false;
        lastStep_ = 255u;
        ageCounter_ = 0u;
    }

    void allNotesOff() noexcept {
        for (auto& v : voices_) v.active = false;
        telemetry_.playingVoiceCount = 0u;
        telemetry_.activeSlotCount = 0u;
        telemetry_.outputPeak = 0.0f;
    }

    bool isActive() const noexcept {
        for (const auto& v : voices_) {
            if (v.active) return true;
        }
        return false;
    }

    const DigiSamplerTelemetry& telemetry() const noexcept { return telemetry_; }

    DigiLayerMode layerMode() const noexcept { return layerMode_; }
    void setLayerMode(DigiLayerMode m) noexcept { layerMode_ = m; }

    // ── Programmatic / MIDI trigger ──────────────────────────────────────────
    // Trigger a specific slot immediately (not gated by the step sequencer).
    // Use for MIDI-note-triggered sample playback or programmatic one-shots.
    // `slotIndex` is 0-based into the active slot list.
    // `velocity` is 1–127; 0 is a no-op.
    // `sampleBank` must remain valid until the voice finishes playing.
    void triggerSlotImmediate(std::uint8_t slotIndex,
                              std::uint8_t velocity,
                              const ArpSID::GUI::DigiSampleBankBlob& sampleBank,
                              const ArpSID::GUI::GuiRealtimeDigiProjection& projection) noexcept {
        triggerSlotAt(slotIndex, velocity, sampleBank, projection, 0);
    }
    void triggerSlotAt(std::uint8_t slotIndex,
                       std::uint8_t velocity,
                       const ArpSID::GUI::DigiSampleBankBlob& sampleBank,
                       const ArpSID::GUI::GuiRealtimeDigiProjection& projection,
                       int sampleOffset) noexcept {
        if (velocity == 0u || slotIndex >= ArpSID::GUI::kDigiActiveSlotCount) return;
        const auto& slot = projection.slots[(std::size_t)slotIndex];
        if (slot.volume == 0u) return;
        ArpSID::GUI::GuiRealtimeDigiProjection singleSlotProj{};
        singleSlotProj.stepIndex = projection.stepIndex;
        singleSlotProj.activeSlot = slotIndex;
        singleSlotProj.activeSlotCount = static_cast<std::uint8_t>(std::min<int>(
            static_cast<int>(slotIndex) + 1, ArpSID::GUI::kDigiActiveSlotCount));
        singleSlotProj.slots[(std::size_t)slotIndex] = slot;
        singleSlotProj.slots[(std::size_t)slotIndex].activeAtStep = true;
        singleSlotProj.slots[(std::size_t)slotIndex].stepVelocity = velocity;
        pendingTriggerOffset_ = std::max(0, sampleOffset);
        triggerStep_(singleSlotProj, sampleBank);
        pendingTriggerOffset_ = 0;
    }
    std::uint32_t scopeWritePos() const noexcept { return scopeWritePos_ & static_cast<std::uint32_t>(kScopeLen - 1); }

    void copyScope(float* dst, int maxSamples) const noexcept {
        if (!dst || maxSamples <= 0) return;
        const int n = std::min(maxSamples, kScopeLen);
        const std::uint32_t wp = scopeWritePos_ & static_cast<std::uint32_t>(kScopeLen - 1);
        for (int i = 0; i < n; ++i) {
            const std::uint32_t rp = (wp + static_cast<std::uint32_t>(i)) & static_cast<std::uint32_t>(kScopeLen - 1);
            dst[i] = scope_[(std::size_t)rp];
        }
    }

    // triggerSampleOffset: the sample index WITHIN this block at which the step
    // boundary falls. Pass 0 if unknown (legacy / block-edge behaviour).
    // When > 0, voices triggered this block start rendering from that sample,
    // producing correct sub-block timing instead of always firing at i=0.
    void process(const ArpSID::GUI::GuiRealtimeDigiProjection& projection,
                 const ArpSID::GUI::DigiSampleBankBlob& sampleBank,
                 float* outL,
                 float* outR,
                 int frames,
                 bool sequencerEnabled,
                 bool allowTriggers,
                 int triggerSampleOffset = 0) noexcept {
        if (frames <= 0) return;

        telemetry_.stepIndex = projection.stepIndex;
        telemetry_.activeSlotCount = projection.activeSlotCount;
        telemetry_.configuredFactorySlots = 0u;
        telemetry_.configuredUserImportSlots = 0u;
        telemetry_.unavailableUserImportCount = 0u;

        for (const auto& slot : projection.slots) {
            if (slot.volume == 0u) continue;
            if (slot.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::FactorySlot)) {
                ++telemetry_.configuredFactorySlots;
            } else if (slot.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::UserImport)) {
                ++telemetry_.configuredUserImportSlots;
            }
        }

        // Clamp offset to valid range; negative or out-of-bounds → block edge (0).
        const int trigOffset = (triggerSampleOffset > 0 && triggerSampleOffset < frames)
                                ? triggerSampleOffset : 0;
        bool triggeredThisBlock = false;
        if (!sequencerEnabled) {
            haveStep_ = false;
        } else {
            const std::uint8_t step = static_cast<std::uint8_t>(projection.stepIndex % ArpSID::GUI::kDigiStepCount);
            const bool stepEdge = (!haveStep_ || step != lastStep_);
            if (stepEdge && allowTriggers) {
                // Mark new voices with the sample offset so their render loop
                // skips the pre-trigger samples.
                pendingTriggerOffset_ = trigOffset;
                triggerStep_(projection, sampleBank);
                pendingTriggerOffset_ = 0;
                triggeredThisBlock = true;
            }
            lastStep_ = step;
            haveStep_ = true;
        }
        (void)triggeredThisBlock;

        // Fix #5: resolve user-sample clip pointers ONCE per block, not per sample.
        // digiFindUserSampleClip is O(1) but avoids the repeated handle-check inside
        // the hot per-sample loop. A null result deactivates the voice immediately.
        std::array<const ArpSID::GUI::DigiUserSampleClip*, kMaxVoices> resolvedClips{};
        for (std::size_t vi = 0; vi < voices_.size(); ++vi) {
            auto& v = voices_[vi];
            if (!v.active) { resolvedClips[vi] = nullptr; continue; }
            if (v.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::UserImport)) {
                resolvedClips[vi] = ArpSID::GUI::digiFindUserSampleClipRealtime(
                    sampleBank, v.userSampleIndex, v.userSampleHandle);
                if (!resolvedClips[vi]) { v.active = false; }
            } else {
                resolvedClips[vi] = nullptr;  // factory: unused
            }
        }

        float blockPeak = 0.0f;
        std::uint8_t activeVoices = 0u;
        for (int i = 0; i < frames; ++i) {
            float mix = 0.0f;
            std::uint8_t voicesThisSample = 0u;
            for (std::size_t vi = 0; vi < voices_.size(); ++vi) {
                auto& v = voices_[vi];
                if (!v.active) continue;
                // Sub-block trigger offset: skip samples before the trigger point.
                if (i < v.startSampleOffset) continue;
                ++voicesThisSample;
                float raw = 0.0f;
                if (v.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::UserImport)) {
                    const auto* clip = resolvedClips[vi];
                    if (!clip) { v.active = false; continue; }
                    raw = userSampleAt_(*clip, v.pos);
                } else {
                    raw = factoryD418SampleAt_(v.factorySlotIndex, v.pos, v.sampleLength);
                }
                const float sample = quantizeD418_(raw);
                mix += sanitizeUnit_(sample * sanitizeUnit_(v.gain));
                advanceVoice_(v);
            }
            activeVoices = std::max(activeVoices, voicesThisSample);
            mix = sanitizeUnit_(std::clamp(mix, -0.95f, 0.95f));
            blockPeak = std::max(blockPeak, std::fabs(mix));
            scope_[(std::size_t)(scopeWritePos_ & static_cast<std::uint32_t>(kScopeLen - 1))] = mix;
            scopeWritePos_ = (scopeWritePos_ + 1u) & static_cast<std::uint32_t>(kScopeLen - 1);
            if (layerMode_ == DigiLayerMode::Exclusive) {
                // Exclusive: replace SID output entirely with DIGI mix.
                if (outL) outL[i] = sanitizeUnit_(mix);
                if (outR && outR != outL) outR[i] = sanitizeUnit_(mix);
            } else {
                // Additive (default): layer DIGI on top of SID output.
                const float cleanMix = sanitizeBus_(mix);
                if (outL) outL[i] = sanitizeBus_(sanitizeBus_(outL[i]) + cleanMix);
                if (outR && outR != outL) outR[i] = sanitizeBus_(sanitizeBus_(outR[i]) + cleanMix);
            }
        }
        std::uint8_t activeEndVoices = 0u;
        for (const auto& v : voices_) {
            if (v.active) ++activeEndVoices;
        }
        // Current voice count is the number of voices still alive at block end.
        // `activeVoices` remains the per-block peak used by peakVoiceCount below.
        telemetry_.playingVoiceCount = activeEndVoices;
        telemetry_.outputPeak = std::max(blockPeak, telemetry_.outputPeak * 0.86f);
        // Fix #6: peak voice count with slow decay (~2 s at 60 Hz poll).
        if (activeVoices > telemetry_.peakVoiceCount) {
            telemetry_.peakVoiceCount = activeVoices;
        } else if (telemetry_.peakVoiceCount > 0u) {
            if (++telemetry_.peakVoiceDecayTick_ >= 120u) {
                --telemetry_.peakVoiceCount;
                telemetry_.peakVoiceDecayTick_ = 0u;
            }
        }
    }

private:
    struct Voice {
        bool active = false;
        std::uint8_t sourceType = 0;
        std::uint8_t slotIndex = 0;
        std::uint8_t factorySlotIndex = 0;
        std::uint8_t userSampleIndex = 0;
        std::uint32_t userSampleHandle = 0;
        std::uint16_t sampleLength = 1;
        float pos = 0.0f;
        float inc = 1.0f;
        float gain = 0.0f;
        std::uint16_t start = 0;
        std::uint16_t end = 1;
        std::uint8_t flags = 0u;
        std::uint32_t age = 0u;
        // Sample index within the block at which this voice starts producing output.
        // 0 = block edge (legacy); >0 = sub-block trigger offset (Fix #4).
        int startSampleOffset = 0;
    };

    double sampleRate_ = 44100.0;
    std::array<Voice, kMaxVoices> voices_{};
    DigiSamplerTelemetry telemetry_{};
    std::array<float, kScopeLen> scope_{};
    std::uint32_t scopeWritePos_ = 0u;
    std::uint32_t ageCounter_ = 0u;
    std::uint8_t lastStep_ = 255u;
    bool haveStep_ = false;
    int pendingTriggerOffset_ = 0;  // set before triggerStep_(), cleared after
    DigiLayerMode layerMode_ = DigiLayerMode::Additive;

    void triggerStep_(const ArpSID::GUI::GuiRealtimeDigiProjection& projection,
                      const ArpSID::GUI::DigiSampleBankBlob& sampleBank) noexcept {
        for (std::uint8_t slotIndex = 0; slotIndex < ArpSID::GUI::kDigiActiveSlotCount; ++slotIndex) {
            const auto& slot = projection.slots[slotIndex];
            if (!slot.activeAtStep || slot.stepVelocity == 0u || slot.volume == 0u) continue;
            const bool isFactory =
                slot.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::FactorySlot);
            const bool isUser =
                slot.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::UserImport);
            if (!isFactory && !isUser) continue;

            const ArpSID::GUI::DigiUserSampleClip* userClip = nullptr;
            std::uint8_t factoryIndex = 0u;
            std::uint16_t length = 1u;
            float sourceRate = static_cast<float>(ArpSID::GUI::kDigiUserSampleCanonicalRateHz);
            if (isUser) {
                userClip = ArpSID::GUI::digiFindUserSampleClipRealtime(
                    sampleBank, slot.userSampleIndex, slot.userSampleHandle);
                if (!userClip) {
                    ++telemetry_.unavailableUserImportCount;
                    continue;
                }
                length = userClip->frameCount;
                sourceRate = static_cast<float>(userClip->sourceSampleRateHz);
            } else {
                factoryIndex =
                    static_cast<std::uint8_t>(std::min<int>(slot.factorySlotIndex, ArpSID::GUI::kKitDigiSlotCount - 1));
                length = factoryLength_(factoryIndex);
                sourceRate = static_cast<float>(ArpSID::GUI::kDigiUserSampleCanonicalRateHz);
            }

            Voice* v = allocateVoice_();
            if (!v) return;
            const std::uint16_t start = static_cast<std::uint16_t>(
                std::min<int>(length - 1u, (static_cast<int>(length) * static_cast<int>(slot.startOffset)) / 256));
            const std::uint16_t remaining = static_cast<std::uint16_t>(std::max<int>(1, length - start));
            const std::uint16_t scaled = (slot.lengthScale == 0u)
                ? remaining
                : static_cast<std::uint16_t>(std::max<int>(32, (static_cast<int>(remaining) * static_cast<int>(slot.lengthScale)) / 255));
            const std::uint16_t end = static_cast<std::uint16_t>(
                std::min<int>(length, static_cast<int>(start) + static_cast<int>(scaled)));
            const bool reverse = (slot.flags & ArpSID::GUI::kDigiFlagReverse) != 0u;
            const float pitch = std::pow(2.0f, static_cast<float>(slot.tuneShift) / 12.0f);
            const float inc = std::max(0.035f, (sourceRate / static_cast<float>(sampleRate_)) * pitch);
            v->active = true;
            v->sourceType = slot.sourceType;
            v->slotIndex = slotIndex;
            v->factorySlotIndex = factoryIndex;
            v->userSampleIndex = slot.userSampleIndex;
            v->userSampleHandle = slot.userSampleHandle;
            v->sampleLength = length;
            v->start = start;
            v->end = std::max<std::uint16_t>(static_cast<std::uint16_t>(start + 1u), end);
            v->pos = reverse ? static_cast<float>(v->end - 1u) : static_cast<float>(v->start);
            v->inc = reverse ? -inc : inc;
            v->gain = 0.24f * (static_cast<float>(slot.stepVelocity) / 127.0f) * (static_cast<float>(slot.volume) / 255.0f);
            v->flags = slot.flags;
            v->age = ++ageCounter_;
            v->startSampleOffset = pendingTriggerOffset_;  // Fix #4: sub-block trigger offset
            telemetry_.lastTriggeredSlot = slotIndex;
            telemetry_.lastTriggeredFactorySlot = isFactory ? slot.absoluteFactorySlot : 0u;
            ++telemetry_.triggerCount;
        }
    }

    Voice* allocateVoice_() noexcept {
        for (auto& v : voices_) {
            if (!v.active) return &v;
        }
        // Prefer a voice that is quiet and close to its end; fall back to age.
        // The old oldest-only policy could cut a loud fresh sample while a
        // nearly-finished low-energy voice remained active.
        Voice* best = &voices_[0];
        float bestScore = 1.0e30f;
        for (auto& v : voices_) {
            const float remaining = (v.inc >= 0.0f)
                ? std::max(0.0f, static_cast<float>(v.end) - v.pos)
                : std::max(0.0f, v.pos - static_cast<float>(v.start));
            const float score = remaining * std::max(0.05f, v.gain)
                              - static_cast<float>(v.age) * 1.0e-6f;
            if (score < bestScore) {
                bestScore = score;
                best = &v;
            }
        }
        return best;
    }

    static std::uint16_t factoryLength_(std::uint8_t idx) noexcept {
        static constexpr std::uint16_t lengths[10] = {
            4600u, 5200u, 7000u, 2800u, 3900u,
            2200u, 3100u, 8200u, 3600u, 5800u
        };
        const std::uint16_t base = lengths[(std::size_t)(idx % 10u)];
        return static_cast<std::uint16_t>(base + static_cast<std::uint16_t>((idx / 10u) * 420u));
    }

    static float fastNoise_(std::uint32_t x) noexcept {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return (static_cast<float>(x & 0xFFFFu) / 32767.5f) - 1.0f;
    }

    static float square_(float x) noexcept {
        const float f = x - std::floor(x);
        return f < 0.5f ? 1.0f : -1.0f;
    }

    static float factorySampleAt_(std::uint8_t idx, float pos, std::uint16_t length) noexcept {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float safePos = std::isfinite(pos) ? pos : 0.0f;
        const float len = static_cast<float>(std::max<std::uint16_t>(length, 1u));
        const float t = std::clamp(safePos / len, 0.0f, 1.0f);
        const float inv = 1.0f - t;
        const float envFast = inv * inv * inv;
        const float envMed = inv * inv;
        const std::uint8_t family = static_cast<std::uint8_t>(idx % 10u);
        const float variant = static_cast<float>(idx / 10u);
        const float noise = fastNoise_(static_cast<std::uint32_t>(safePos) + 0x9E3779B9u * (idx + 1u));
        switch (family) {
            case 0: {
                const float phase = (30.0f + 4.0f * variant) * t - (18.0f + 2.0f * variant) * t * t;
                return std::sin(kTwoPi * phase) * envFast;
            }
            case 1: {
                const float body = std::sin(kTwoPi * (18.0f + variant * 2.0f) * t) * envMed * 0.50f;
                return body + noise * envFast * 0.72f;
            }
            case 2: {
                const bool burst = (t < 0.10f) || (t > 0.18f && t < 0.28f) || (t > 0.36f && t < 0.47f);
                return burst ? noise * (0.35f + 0.65f * envMed) : noise * envFast * 0.18f;
            }
            case 3:
                return (noise - fastNoise_(static_cast<std::uint32_t>(safePos) + 19u)) * 0.45f * inv;
            case 4: {
                const float phase = (22.0f + 3.0f * variant) * t - (9.0f + variant) * t * t;
                return std::sin(kTwoPi * phase) * envMed;
            }
            case 5:
                return (square_(t * (58.0f + variant * 5.0f)) * 0.45f +
                        std::sin(kTwoPi * (78.0f + variant * 7.0f) * t) * 0.55f) * envFast;
            case 6:
                return (square_(t * (23.0f + variant * 2.0f)) +
                        square_(t * (31.0f + variant * 2.0f))) * 0.40f * envMed;
            case 7:
                return noise * (0.9f - 0.35f * t) * inv;
            case 8:
                return (std::sin(kTwoPi * (48.0f + variant * 4.0f) * t) * 0.5f + noise * 0.5f) * envMed;
            default:
                return (square_(t * (12.0f + variant * 3.0f)) * 0.35f + noise * 0.65f) * envMed;
        }
    }

    // factory DIGI playback is also a held 4-bit $D418 byte stream.
    // The waveform recipe is used only to choose the nearest nibble for the
    // current 8 kHz DIGI tick; the realtime layer never exposes interpolated
    // factory PCM as an audible sampler source.
    static float factoryD418SampleAt_(std::uint8_t idx, float pos, std::uint16_t length) noexcept {
        const std::uint16_t safeLength = std::max<std::uint16_t>(1u, length);
        const float safePos = std::isfinite(pos) ? pos : 0.0f;
        const std::uint16_t i = static_cast<std::uint16_t>(std::clamp(safePos, 0.0f, static_cast<float>(safeLength - 1u)));
        const float raw = factorySampleAt_(idx, static_cast<float>(i), safeLength);
        const std::uint8_t nibble = ArpSID::GUI::digiFloatToD418Nibble(raw);
        return (static_cast<float>(nibble) / 7.5f) - 1.0f;
    }

    static float userSampleAt_(const ArpSID::GUI::DigiUserSampleClip& clip, float pos) noexcept {
        const std::uint16_t count = clip.frameCount;
        if (count == 0u) return 0.0f;
        const float safePos = std::isfinite(pos) ? pos : 0.0f;
        const float clampedPos = std::clamp(safePos, 0.0f, static_cast<float>(count - 1u));
        const std::uint16_t i0 = static_cast<std::uint16_t>(clampedPos);

        // Even the legacy float layer must not resurrect a hidden hi-fi sampler
        // for user imports. User clips are persisted as legal $D418 low-nibble
        // representatives; playback holds the selected 4-bit code until the
        // next DIGI tick instead of linearly interpolating between 8-bit PCM
        // bytes. This keeps REC/IMPORT semantics identical to the authentic
        // D418 layer while preserving the legacy layer only as a compatibility
        // audio-mix path.
        const std::uint8_t nibble = ArpSID::GUI::digiPcm8ToD418Nibble(clip.pcm[i0]);
        return (static_cast<float>(nibble) / 7.5f) - 1.0f;
    }

    static float sanitizeUnit_(float v) noexcept {
        if (!std::isfinite(v)) return 0.0f;
        return std::clamp(v, -1.0f, 1.0f);
    }

    static float sanitizeBus_(float v) noexcept {
        if (!std::isfinite(v)) return 0.0f;
        return std::clamp(v, -1.25f, 1.25f);
    }

    static float quantizeD418_(float v) noexcept {
        const float clamped = sanitizeUnit_(v);
        const float code = std::round((clamped + 1.0f) * 7.5f);
        if (!std::isfinite(code)) return 0.0f;
        return sanitizeUnit_((code / 7.5f) - 1.0f);
    }

    void advanceVoice_(Voice& v) noexcept {
        const bool loop = (v.flags & ArpSID::GUI::kDigiFlagLoop) != 0u;
        if (!std::isfinite(v.pos)) v.pos = static_cast<float>(v.start);
        if (!std::isfinite(v.inc) || v.inc == 0.0f) { v.active = false; return; }
        v.pos += v.inc;
        if (!std::isfinite(v.pos)) { v.active = false; return; }
        if (v.inc >= 0.0f) {
            if (v.pos < static_cast<float>(v.end)) return;
            if (loop && v.end > v.start + 1u) {
                const float len = static_cast<float>(v.end - v.start);
                v.pos = static_cast<float>(v.start) + std::fmod(v.pos - static_cast<float>(v.start), len);
            } else {
                v.active = false;
            }
        } else {
            if (v.pos >= static_cast<float>(v.start)) return;
            if (loop && v.end > v.start + 1u) {
                const float len = static_cast<float>(v.end - v.start);
                while (v.pos < static_cast<float>(v.start)) v.pos += len;
            } else {
                v.active = false;
            }
        }
    }
};

} // namespace ArpSID

#endif // ARPSID_ENGINES_DIGI_SAMPLER_ENGINE_H
