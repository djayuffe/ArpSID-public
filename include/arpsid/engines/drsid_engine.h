// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/core/sid_chip.h"
#include "arpsid/core/sid_interval_renderable.h"
#include "arpsid/core/sid_chip_interval_native.h"
#include "arpsid/core/sid_runtime_drsid_gain.h"
#include "arpsid/core/sid_gm_drum_map.h"
#include "arpsid/core/drum_context.h"
#include "arpsid/core/sid_serializer_schema.h"
#include "arpsid/core/sid_voice_allocator.h"
#include "arpsid/core/scope_triple_buffer.h"
#include "arpsid/engines/drsid_wavetable_program_runner.h"
#include "parameter_ids.h"
#include <atomic>
#include <cmath>
#include <algorithm>
#include <array>

namespace ArpSID {

// B10: DR-SID playback mode — shown in GUI HUD.
// Authentic: SID physics + SID-authentic waveform sequences.
// Clean: SID physics without filter modelling irregularities.
// Overlay: SID physics + analog overlay synthesis on top.
// Wavetable: register-microprogram runner (DrSidWavetableProgramRunner).
// Legacy: original fallback path (pre-wavetable runner integration).
enum class DrSidPlaybackMode : std::uint8_t {
    Legacy     = 0,
    Authentic  = 1,
    Clean      = 2,
    Overlay    = 3,
    Wavetable  = 4,
};

/**
 * DrSid Drum Engine - Percussion synthesis using SID chip
 * 8 different drum sounds synthesized from SID waveforms.
 * Implements ISidIntervalRenderable as canonical interval render contract (Pass L).
 */
class DrSidEngine : public ISidIntervalRenderable {
public:
    virtual ~DrSidEngine() = default;

    // ----------------------------------------------------------------------
    // ISidIntervalRenderable — true interval-native physics (Pass L, revised).
    //
    // DrSid renders drum physics through its single SIDChip. The canonical
    // interval advance path is identical to BitPerfect: phase 1 / 2 / 3
    // decomposition with full causal state advance. Accent and performance
    // modulation are applied POST-physics, not inside the chip model.
    //
    // Combined-wave authority, hard-sync, ring-mod and filter all advance
    // through SIDChip's own physics — not approximated here.
    // ----------------------------------------------------------------------
    void renderIntervalAccurate(const SidRenderInterval& iv,
                                float& outL, float& outR) noexcept override {
        if (!iv.valid()) return;
        outL = outR = 0.0f;
        prepareFractionalHostSample_();

        const uint32_t bCyc = iv.beginCycle, eCyc = iv.endCycle;
        const uint16_t bSub = iv.beginSubphase, eSub = iv.endSubphase;

        float accumL = 0.0f, accumR = 0.0f;
        int   accumSteps = 0;

        // Phase 1: leading partial-cycle subphases.
        if (bSub != 0u) {
            const uint16_t subEnd = (bCyc == eCyc) ? eSub : kSidSubcycleBoundary;
            if (subEnd > bSub) {
                float l = 0.f, r = 0.f;
                sidChip.renderSubCyclePhaseContribution(
                    static_cast<uint16_t>(bCyc), bSub, subEnd, l, r);
                // v903: split intervals that complete a SID cycle must advance
                // the DrSID wavetable microprogram on the same cycle boundary
                // as SIDChip physics; whole-cycle-only ticking was a split-brain
                // around mid-cycle writes/events.
                if (subEnd >= kSidSubcycleBoundary) tickWavetableRunnersForCycles_(1u);
                const int spanSteps = static_cast<int>(subEnd - bSub);
                accumL += l * static_cast<float>(spanSteps);
                accumR += r * static_cast<float>(spanSteps);
                accumSteps += spanSteps;
            }
        }

        // Phase 2: whole cycles — true cycle-by-cycle drum SID physics.
        const uint32_t wholeCycleStart = (bSub > 0u) ? bCyc + 1u : bCyc;
        // Respect the interval's exclusive end boundary: [0,0)→[1,0) contains
        // exactly cycle 0. Full cycles therefore stop before eCyc.
        const uint32_t wholeCycleEnd   = eCyc;
        if (wholeCycleEnd > wholeCycleStart) {
            const int span = static_cast<int>(wholeCycleEnd - wholeCycleStart);
            tickWavetableRunnersForCycles_(wholeCycleEnd - wholeCycleStart);
            float l = 0.f, r = 0.f;
            sidChip.renderCycleWindowContribution(
                static_cast<uint16_t>(wholeCycleStart),
                static_cast<uint16_t>(wholeCycleEnd), l, r);
            accumL += l * static_cast<float>(span * kSidSubcycleResolution);
            accumR += r * static_cast<float>(span * kSidSubcycleResolution);
            accumSteps += span * kSidSubcycleResolution;
        }

        // Phase 3: trailing partial-cycle subphases.
        if (eSub > 0u && eCyc >= bCyc && (eCyc != bCyc || bSub == 0u)) {
            const uint16_t subStart = (eCyc == bCyc) ? bSub : 0u;
            if (eSub > subStart) {
                float l = 0.f, r = 0.f;
                sidChip.renderSubCyclePhaseContribution(
                    static_cast<uint16_t>(eCyc), subStart, eSub, l, r);
                const int spanSteps = static_cast<int>(eSub - subStart);
                accumL += l * static_cast<float>(spanSteps);
                accumR += r * static_cast<float>(spanSteps);
                accumSteps += spanSteps;
            }
        }

        if (accumSteps > 0) {
            const float invN = 1.0f / static_cast<float>(accumSteps);
            const float l = accumL * invN;
            const float r = accumR * invN;

            // Post-physics performance modulation (NOT baked into chip state).
            const float gl = preparedFractionalMasterVolume_;
            outL = shapeDrSidSample_(l, 0.0f, gl);
            outR = shapeDrSidSample_(r, 0.0f, gl);
        }

        intervalCursor_ = (eCyc << 8u) + eSub;
    }

    uint16_t estimatedCyclesPerHostSample() const noexcept override {
        const double hz = clockFreq > 0.0 ? clockFreq : PAL_CLOCK_FREQ;
        return boundedSidCyclesPerHostSampleEstimate(
            hz, sampleRate > 1.0 ? sampleRate : 44100.0);
    }

    void resetIntervalCursor() noexcept override { intervalCursor_ = 0; }

    enum class DrumType {
        Kick,
        Snare,
        ClosedHat,
        OpenHat,
        Clap,
        Cowbell,
        Tom,
        Rim
    };

    static constexpr int kDrumTypeCount = 8;
    static constexpr int kGMDrumNoteMin = 35;
    static constexpr int kGMDrumNoteMax = 81;
    static constexpr int kGMDrumNoteCount = kGMDrumNoteMax - kGMDrumNoteMin + 1;

    static constexpr DrumType drumTypeForGMClass(SidGMDrumClass c) noexcept {
        switch (c) {
            case SidGMDrumClass::Kick:      return DrumType::Kick;
            case SidGMDrumClass::Snare:     return DrumType::Snare;
            case SidGMDrumClass::ClosedHat: return DrumType::ClosedHat;
            case SidGMDrumClass::OpenHat:   return DrumType::OpenHat;
            case SidGMDrumClass::Clap:      return DrumType::Clap;
            case SidGMDrumClass::Cowbell:   return DrumType::Cowbell;
            case SidGMDrumClass::Tom:       return DrumType::Tom;
            case SidGMDrumClass::Rim:       return DrumType::Rim;
            default:                        return DrumType::Kick;
        }
    }

    static constexpr DrumType drumTypeForMidiNote(int note) noexcept {
        const uint8_t n = static_cast<uint8_t>(std::clamp(note, 0, 127));
        const SidGMDrumClass c = sidGMDrumClassForNote(n);
        if (c != SidGMDrumClass::Unsupported) return drumTypeForGMClass(c);
        if (note <= 36) return DrumType::Kick;
        if (note <= 40) return DrumType::Snare;
        if (note <= 44) return DrumType::Tom;
        if (note <= 46) return DrumType::ClosedHat;
        if (note <= 50) return DrumType::Tom;
        if (note <= 59) return DrumType::OpenHat;
        if (note <= 66) return DrumType::Tom;
        if (note <= 68) return DrumType::Cowbell;
        if (note <= 74) return DrumType::OpenHat;
        if (note <= 77) return DrumType::Rim;
        if (note <= 79) return DrumType::Tom;
        return DrumType::OpenHat;
    }

    static constexpr int canonicalMidiNoteForDrumType(DrumType type) noexcept {
        switch (type) {
            case DrumType::Kick:      return 36;
            case DrumType::Snare:     return 38;
            case DrumType::ClosedHat: return 42;
            case DrumType::OpenHat:   return 46;
            case DrumType::Clap:      return 39;
            case DrumType::Cowbell:   return 56;
            case DrumType::Tom:       return 47;
            case DrumType::Rim:       return 37;
        }
        return 36;
    }

    static constexpr const char* shortNameForDrumType(DrumType type) noexcept {
        switch (type) {
            case DrumType::Kick:      return "KICK";
            case DrumType::Snare:     return "SNAR";
            case DrumType::ClosedHat: return "CHAT";
            case DrumType::OpenHat:   return "OHAT";
            case DrumType::Clap:      return "CLAP";
            case DrumType::Cowbell:   return "CBEL";
            case DrumType::Tom:       return "TOM";
            case DrumType::Rim:       return "RIM";
        }
        return "DRM";
    }
    
    DrSidEngine() {
        setSampleRate(sampleRate);
        sidChip.setClockFrequency(PAL_CLOCK_FREQ);
        applyAuthenticC64DrumProfile();
        reset();
    }
    
    void setAllowUnsupportedMidiFallback(bool enabled) noexcept { allowUnsupportedMidiFallback_ = enabled; }
    bool allowUnsupportedMidiFallback() const noexcept { return allowUnsupportedMidiFallback_; }
    float gmNoteLevel(int note) const noexcept {
        const int n = std::clamp(note, 0, 127);
        return gmNoteLevels_[static_cast<std::size_t>(n)];
    }
    float drumEnvelopeLevel(DrumType type) const noexcept {
        return drumEnvelopes[static_cast<std::size_t>(type)];
    }

    void setSampleRate(double sr) {
        sampleRate = std::isfinite(sr) ? std::clamp(sr, 1.0, 384000.0) : 44100.0;
        sidChip.setSampleRate(sampleRate);
        updateOverlayDcBlockCoeff_();
        fractionalSamplePrepared_ = false;
        // Sample-rate changes must not erase telemetry/register truth. The
        // current SID program remains authoritative; only fractional prepared
        // samples and scope history are invalidated.
        clearScopeHistory_();
    }

    // FIX Bug#11: Accept the actual clock frequency so NTSC vs PAL pitch is correct.
    // Previously hzToSIDReg() always used PAL_CLOCK_FREQ, making all drum pitches
    // ~3.8% sharp (~65 cents) in NTSC mode.
    //
    // Audit #45 fix: do NOT silently fall back to PAL when an invalid (zero,
    // negative, or non-finite) clock arrives. The legacy behavior could
    // re-pitch ALL drums by ~3.8 % under hostile/buggy upstream code if a
    // clock-update path glitched. The new behavior is "refuse and keep
    // last valid clock", with a diagnostic counter so the misbehaviour is
    // observable instead of audible.
    void setClockFrequency(double hz) {
        if (!sidClockFrequencySupported(hz)) {
            // Reject — keep the last valid clock. If we have no last valid
            // clock yet (engine just constructed), fall back to PAL ONCE
            // so the engine is in a known good state.
            ++invalidClockFrequencyRejectCount_;
            if (clockFreq <= 0.0) {
                clockFreq = PAL_CLOCK_FREQ;
                sidChip.setClockFrequency(clockFreq);
            }
            return;
        }
        clockFreq = hz;
        sidChip.setClockFrequency(clockFreq);
    }
    // Audit #45 diagnostic — how many times setClockFrequency was called
    // with an invalid value (non-finite, zero, or negative).
    std::uint64_t invalidClockFrequencyRejectCount() const noexcept {
        return invalidClockFrequencyRejectCount_;
    }
    void resetInvalidClockFrequencyRejectCount() noexcept {
        invalidClockFrequencyRejectCount_ = 0u;
    }

    // ── Audit #41/#42 — voice-stealing + choke-group wire-helper ────────────
    //
    // DrSidEngine's legacy overlay voice allocator uses "lowest envelope"
    // stealing (the audit's #41 complaint) and a fixed 12-voice pool
    // without per-family reservation (audit #42). The full fix requires
    // splitting the analog overlay into Sid808Engine (declared out of
    // scope for this slice — see audit-progression doc).
    //
    // What we DO provide here: a typed, deterministic, choke-group-aware
    // voice allocator that future call sites can use INSTEAD of the
    // legacy "lowest envelope" path. The allocator is held as a member of
    // DrSidEngine so its state survives across processBlock calls and is
    // already in place when the eventual engine split lands.
    //
    // Usage from host/sequencer code:
    // const uint8_t voice = engine.allocateDrumVoice(midiNote, vel, ChokeGroup::HiHat);
    // if (voice == kNoVoice) { /* refused by policy */ }
    // ...
    // engine.releaseDrumVoice(midiNote);
    //
    // B2 DECISION: SidVoiceAllocator<12> is a DIAGNOSTIC/TRACKING allocator only.
    // It does NOT drive any actual SID voice rendering. Its allocate() return
    // value is not used to select a SID voice; the legacy overlay render path
    // selects voices independently via 'lowest-envelope' heuristics.
    // Keep this allocator for audit/telemetry (voice-steal policy, choke groups)
    // and for the future engine-split migration where it will replace the legacy
    // overlay allocator entirely. Do NOT wire its output into render until the
    // migration is complete and validated.
    using DrumVoiceAllocator = SidVoiceAllocator<12>;

    DrumVoiceAllocator&       drumVoiceAllocator()       noexcept { return drumVoiceAllocator_; }
    const DrumVoiceAllocator& drumVoiceAllocator() const noexcept { return drumVoiceAllocator_; }

    void setDrumVoiceStealingPolicy(VoiceStealingPolicy p) noexcept {
        drumVoiceAllocator_.setPolicy(p);
    }

    std::uint8_t allocateDrumVoice(std::uint8_t midiNote,
                                   std::uint8_t velocity,
                                   Drsid::ChokeGroup choke = Drsid::ChokeGroup::None) noexcept {
        return drumVoiceAllocator_.allocate(midiNote, velocity, choke);
    }
    std::uint8_t releaseDrumVoice(std::uint8_t midiNote) noexcept {
        return drumVoiceAllocator_.release(midiNote);
    }
    void freeReleasedDrumVoice(std::uint8_t voiceIndex) noexcept {
        drumVoiceAllocator_.freeReleased(voiceIndex);
    }
    void drumVoiceAllocatorTick(std::uint32_t samples) noexcept {
        drumVoiceAllocator_.tick(samples);
    }
    void drumVoiceAllocatorReset() noexcept { drumVoiceAllocator_.reset(); }


    void setSIDModel(ArpSID::SIDModel m) noexcept {
        currentSidModel_ = m;
        sidChip.setModel(m);
        configureSidCoreForDrums_();
    }
    void setSIDRevision(uint8_t revision) noexcept { sidChip.setRevision(revision); }
    void setSIDExternalRcEnabled(bool enable) noexcept { sidChip.setExternalRcEnabled(enable); }
    void setSIDOversamplingFactor(uint8_t factor) noexcept { sidChip.setOversamplingFactor(factor); }

    struct AnalogueRuntimeSnapshot {
        SIDChip::Snapshot sidChip{};
        std::array<float, 8> drumEnvelopes{};
        std::array<float, 128> gmNoteLevels{};
        std::array<float, 3> voiceVelocity{};
        std::array<int, 3> voiceReleaseSamples{};
        std::array<int, 3> cleanRestartRampRemaining{};
        std::array<int, 3> cleanRestartRampTotal{};
        std::array<float, 3> cleanRestartRampTarget{};
        bool authenticC64DrumMode = true;
        float legacyDigitalOverlayAmount = 0.0f;
        bool cleanRestartRampEnabled = true;
        uint8_t drumMachineModel = static_cast<uint8_t>(DrSidMachineModel::SidAuthentic);
        float accentAmount = 0.68f;
        float outputDrive = 0.18f;
        float hatMetal = 0.62f;
        float clapSpread = 0.54f;
    };

    AnalogueRuntimeSnapshot serializeAnalogueRuntimeState() const noexcept {
        AnalogueRuntimeSnapshot s{};
        s.sidChip = sidChip.serializeAnalogueRuntimeState();
        s.drumEnvelopes = drumEnvelopes;
        s.gmNoteLevels = gmNoteLevels_;
        s.voiceVelocity = voiceVelocity;
        s.voiceReleaseSamples = voiceReleaseSamples;
        s.cleanRestartRampRemaining = cleanRestartRampRemaining_;
        s.cleanRestartRampTotal = cleanRestartRampTotal_;
        s.cleanRestartRampTarget = cleanRestartRampTarget_;
        s.authenticC64DrumMode = c64AuthenticDrumMode_;
        s.legacyDigitalOverlayAmount = legacyDigitalOverlayAmount_;
        s.cleanRestartRampEnabled = cleanRestartRampEnabled_;
        s.drumMachineModel = static_cast<uint8_t>(drumMachineModel_);
        s.accentAmount = accentAmount_;
        s.outputDrive = outputDrive_;
        s.hatMetal = hatMetal_;
        s.clapSpread = clapSpread_;
        return s;
    }

    void restoreAnalogueRuntimeState(const AnalogueRuntimeSnapshot& s) noexcept {
        sidChip.restoreAnalogueRuntimeState(s.sidChip);
        drumEnvelopes = s.drumEnvelopes;
        gmNoteLevels_ = s.gmNoteLevels;
        voiceVelocity = s.voiceVelocity;
        voiceReleaseSamples = s.voiceReleaseSamples;
        cleanRestartRampRemaining_ = s.cleanRestartRampRemaining;
        cleanRestartRampTotal_ = s.cleanRestartRampTotal;
        cleanRestartRampTarget_ = s.cleanRestartRampTarget;
        c64AuthenticDrumMode_ = s.authenticC64DrumMode;
        legacyDigitalOverlayAmount_ = std::clamp(std::isfinite(s.legacyDigitalOverlayAmount) ? s.legacyDigitalOverlayAmount : 0.0f, 0.0f, 1.0f);
        cleanRestartRampEnabled_ = s.cleanRestartRampEnabled;
        // Audit #40 fix: the serialized `drumMachineModel` IS the ground
        // truth on restore. The legacy code coupled the model decision to
        // `legacyDigitalOverlayAmount_`: any nonzero overlay would force
        // the model to AnalogX0X8, silently flipping a SidAuthentic-saved
        // kit to AnalogX0X8 on load. That's the audit's "host-state
        // surprise".
        //
        // The fix: respect the serialized model. Overlay amount is
        // restored as INDEPENDENT state; it does not coerce the model.
        // (If a kit's model was AnalogX0X8 the serialized value already
        // says so — we don't need to infer it from overlay amount.)
        ++stateRestoreModelDecisionsCount_;
        if (drSidMachineModelIsAnalogX0X8(static_cast<float>(s.drumMachineModel))) {
            drumMachineModel_ = DrSidMachineModel::AnalogX0X8;
            drSidPlaybackMode_ = DrSidPlaybackMode::Overlay;
            if (legacyDigitalOverlayAmount_ != 0.0f) {
                ++stateRestoreOverlayAuthoredAnalogCount_;
            }
        } else {
            // Serialized model says SidAuthentic. Restore that, even if
            // overlay amount > 0 — they are independent control surfaces
            // and the user authored both. The audit-correct invariant:
            // overlay > 0 means "blend in some analog texture on top of
            // SidAuthentic", NOT "flip the engine into AnalogX0X8 mode".
            drumMachineModel_ = DrSidMachineModel::SidAuthentic;
            if (legacyDigitalOverlayAmount_ > 0.0f) {
                ++stateRestoreOverlayAuthoredAuthenticCount_;
            }
        }
        accentAmount_ = sanitizeNormParam_(s.accentAmount);
        outputDrive_ = sanitizeNormParam_(s.outputDrive);
        smoothedAccentAmount_ = accentAmount_;
        smoothedOutputDrive_ = outputDrive_;
        smoothedMasterVolume_ = masterVolume;
        hatMetal_ = sanitizeNormParam_(s.hatMetal);
        clapSpread_ = sanitizeNormParam_(s.clapSpread);
        // Sync the authentic-mode flag with the (now-trusted) restored model.
        c64AuthenticDrumMode_ = (drumMachineModel_ == DrSidMachineModel::SidAuthentic);
        configureSidCoreForDrums_();
    }

    // Audit #40 diagnostics — count how many restores were processed and
    // how many fell into the "overlay > 0 was authored together with each
    // model" category. Non-zero `Authored` counters mean users genuinely
    // do this combination; non-zero `Authentic` with overlay > 0 is the
    // case the LEGACY code would have silently rewritten to AnalogX0X8.
    std::uint64_t stateRestoreModelDecisionsCount() const noexcept {
        return stateRestoreModelDecisionsCount_;
    }
    std::uint64_t stateRestoreOverlayAuthoredAuthenticCount() const noexcept {
        return stateRestoreOverlayAuthoredAuthenticCount_;
    }
    std::uint64_t stateRestoreOverlayAuthoredAnalogCount() const noexcept {
        return stateRestoreOverlayAuthoredAnalogCount_;
    }
    void resetStateRestoreDiagnostics() noexcept {
        stateRestoreModelDecisionsCount_ = 0u;
        stateRestoreOverlayAuthoredAuthenticCount_ = 0u;
        stateRestoreOverlayAuthoredAnalogCount_ = 0u;
    }

    SidSerializedState serializePrimarySidEnvelopeRuntimeState() const noexcept {
        SidSerializedState out{};
        const auto snap = sidChip.serializeAnalogueRuntimeState();
        for (size_t i = 0; i < 3; ++i) {
            out.envelopeRateCounter[i] = snap.voices[i].envelope.rateCounter;
            out.envelopeExponentialCounter[i] = snap.voices[i].envelope.expoCounter;
            out.envelopeAdsrDelayHold[i] = snap.voices[i].envelope.adsrDelayHold;
        }
        return out;
    }

    void restorePrimarySidEnvelopeRuntimeState(const SidSerializedState& in) noexcept {
        auto snap = sidChip.serializeAnalogueRuntimeState();
        for (size_t i = 0; i < 3; ++i) {
            snap.voices[i].envelope.rateCounter = static_cast<uint16_t>(in.envelopeRateCounter[i] & Sid6581Envelope::kRateCounterMask);
            snap.voices[i].envelope.expoCounter = in.envelopeExponentialCounter[i];
            snap.voices[i].envelope.adsrDelayHold = in.envelopeAdsrDelayHold[i];
            snap.envelopeAdsrDelayHold[i] = in.envelopeAdsrDelayHold[i];
        }
        sidChip.restoreAnalogueRuntimeState(snap);
        configureSidCoreForDrums_();
    }

    void setEnableAdsrBug6581(bool enable) { sidChip.setEnableAdsrBug6581(enable); }
    void setForensicConfig(const ArpSIDForensicConfig& cfg) { forensicConfig_ = cfg; sidChip.setForensicConfig(cfg); }
    const ArpSIDForensicConfig& getForensicConfig() const noexcept { return forensicConfig_; }

    // DrSID authentic C64 mode: all audible drum tone is produced by the SIDChip
    // voices, ADSR, combined waveforms and analog filter. The legacy helper
    // overlay is explicit opt-in and defaults to zero for register-authentic kits.
    void setAuthenticC64DrumMode(bool enabled) noexcept {
        c64AuthenticDrumMode_ = enabled;
        drumMachineModel_ = enabled ? DrSidMachineModel::SidAuthentic : DrSidMachineModel::AnalogX0X8;
        drSidPlaybackMode_ = enabled ? DrSidPlaybackMode::Wavetable : DrSidPlaybackMode::Overlay;
        if (enabled) legacyDigitalOverlayAmount_ = 0.0f;
        else if (legacyDigitalOverlayAmount_ <= 0.0f) legacyDigitalOverlayAmount_ = 1.0f;
    }
    bool authenticC64DrumMode() const noexcept { return c64AuthenticDrumMode_; }
    void setLegacyDigitalOverlayAmount(float amount) noexcept {
        legacyDigitalOverlayAmount_ = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
        if (legacyDigitalOverlayAmount_ > 0.0f) {
            c64AuthenticDrumMode_ = false;
            drumMachineModel_ = DrSidMachineModel::AnalogX0X8;
        }
    }
    float legacyDigitalOverlayAmount() const noexcept { return legacyDigitalOverlayAmount_; }

    void setDrumMachineModelNormalized(float value) noexcept {
        drumMachineModel_ = static_cast<DrSidMachineModel>(drSidMachineModelIndexFromNormalized(value));
        if (drumMachineModel_ == DrSidMachineModel::AnalogX0X8) {
            c64AuthenticDrumMode_ = false;
            legacyDigitalOverlayAmount_ = 1.0f;
            drSidPlaybackMode_ = DrSidPlaybackMode::Overlay;
        } else {
            c64AuthenticDrumMode_ = true;
            legacyDigitalOverlayAmount_ = 0.0f;
            drSidPlaybackMode_ = DrSidPlaybackMode::Wavetable;
        }
    }
    void setAccentAmount(float value) noexcept { accentAmount_ = sanitizeNormParam_(value); }
    void setOutputDrive(float value) noexcept { outputDrive_ = sanitizeNormParam_(value); }
    void setHatMetal(float value) noexcept { hatMetal_ = sanitizeNormParam_(value); }
    void setClapSpread(float value) noexcept { clapSpread_ = sanitizeNormParam_(value); }
    float drumMachineModelNormalized() const noexcept { return drSidMachineModelIndexToNormalized(static_cast<int>(drumMachineModel_)); }
    float accentAmountNormalized() const noexcept { return accentAmount_; }
    float outputDriveNormalized() const noexcept { return outputDrive_; }
    float hatMetalNormalized() const noexcept { return hatMetal_; }
    float clapSpreadNormalized() const noexcept { return clapSpread_; }
    DrSidMachineModel drumMachineModel() const noexcept { return drumMachineModel_; }
    const char* drumMachineModelName() const noexcept { return drSidMachineModelDisplayNameFromIndex(static_cast<int>(drumMachineModel_)); }
    float kickOverlayBaseHzForTelemetry() const noexcept { return analogKickBaseHz_(); }
    float kickOverlaySweepDeltaHzForTelemetry() const noexcept { return analogKickSweepHz_(); }

    // Clean DrSID restart ramp is the musical default. The SID hard-restart
    // sequence is still performed, but voice level is ramped up over a tiny
    // sample-rate-scaled window so note-on/test-bit edges do not become full    // scale clicks that later get amplified by filter/reverb/limiter stages.
    // Set this false only for forensic/authentic click reproduction tests.
    void setCleanRestartRampEnabled(bool enabled) noexcept { cleanRestartRampEnabled_ = enabled; }
    bool cleanRestartRampEnabled() const noexcept { return cleanRestartRampEnabled_; }

    void applyAuthenticC64DrumProfile() noexcept {
        drumMachineModel_ = DrSidMachineModel::SidAuthentic;
        c64AuthenticDrumMode_ = true;
        legacyDigitalOverlayAmount_ = 0.0f;
        sidChip.setModel(SIDModel::MOS6581);
        sidChip.setEnableAdsrBug6581(true);
        configureSidCoreForDrums_();
    }
    
    void reset() {
        sidChip.reset();
        drsidRegImage_.fill(0u);
        for (auto& env : drumEnvelopes) {
            env = 0.0f;
        }
        for (auto& ov : overlays) ov = OverlayVoice{};
        voiceVelocity = {0.0f, 0.0f, 0.0f};
        cleanRestartRampRemaining_.fill(0);
        cleanRestartRampTotal_.fill(0);
        cleanRestartRampTarget_.fill(0.0f);
        smoothedAccentAmount_ = std::clamp(ArpSID_sanitizeFloat(accentAmount_), 0.0f, 1.0f);
        smoothedOutputDrive_ = std::clamp(ArpSID_sanitizeFloat(outputDrive_), 0.0f, 1.0f);
        // Clamp the smoothing target so a poisoned masterVolume that bypassed
        // setMasterVolume() cannot reset the smoother to an unbounded value.
        smoothedMasterVolume_ = std::clamp(ArpSID_sanitizeFloat(masterVolume, 0.0f), 0.0f, kDrSidMaxInternalBusGain);
        configureSidCoreForDrums_();
        voiceReleaseSamples = {0,0,0};
        authenticPitchSweepFreqReg_.fill(0.0f);
        authenticPitchSweepTargetReg_.fill(0.0f);
        authenticPitchSweepCoeff_.fill(0.0f);
        authenticPitchSweepActive_.fill(0u);
        authenticFilterSweepCutoff_.fill(0.0f);
        authenticFilterSweepTarget_.fill(0.0f);
        authenticFilterSweepCoeff_.fill(0.0f);
        authenticFilterSweepActive_.fill(0u);
        midiHeldCounts_.fill(0u);
        midiHeldGeneration_.fill(0u);
        familyHeldCounts_.fill(0u);
        familyGeneration_.fill(0u);
        gmNoteLevels_.fill(0.0f);
        activeTomGMNote_ = -1;
        drumVoiceAllocator_.reset();
        intervalCursor_ = 0u;
        preparedOverlaySample_ = 0.0f;
        overlayDcIn_ = 0.0f;
        overlayDcOut_ = 0.0f;
        fractionalSamplePrepared_ = false;
        clearScopeHistory_();
    }

    void copyDrumLevels(float* outLevels, int count) const noexcept {
        if (!outLevels || count <= 0) return;
        const int n = std::min(count, kDrumTypeCount);
        for (int i = 0; i < n; ++i) outLevels[i] = std::clamp(ArpSID_sanitizeFloat(drumEnvelopes[(size_t)i]), 0.0f, 1.0f);
        for (const auto& ov : overlays) {
            if (!ov.active) continue;
            const int idx = std::clamp(static_cast<int>(ov.type), 0, kDrumTypeCount - 1);
            if (idx < n) outLevels[idx] = std::max(outLevels[idx], std::clamp(ArpSID_sanitizeFloat((ov.env + ov.envB * 0.7f) * 6.0f), 0.0f, 1.0f));
        }
        for (int i = n; i < count; ++i) outLevels[i] = 0.0f;
    }

    void copyGMDrumNoteLevels(float* outLevels, int count) const noexcept {
        if (!outLevels || count <= 0) return;
        const int n = std::min(count, kGMDrumNoteCount);
        for (int i = 0; i < n; ++i) outLevels[i] = std::clamp(ArpSID_sanitizeFloat(gmNoteLevels_[(size_t)(kGMDrumNoteMin + i)]), 0.0f, 1.0f);
        for (int i = n; i < count; ++i) outLevels[i] = 0.0f;
    }

    void copyVoiceLevels(float* outLevels, int count) const noexcept {
        if (!outLevels || count <= 0) return;
        const int n = std::min(count, 3);
        for (int i = 0; i < n; ++i)
            outLevels[i] = std::clamp(ArpSID_sanitizeFloat(effectiveVoiceLevelForCoreConfigure_(i)), 0.0f, 1.0f);
        for (int i = n; i < count; ++i) outLevels[i] = 0.0f;
    }

    float gmDrumNoteLevel(int midiNote) const noexcept {
        if (midiNote < kGMDrumNoteMin || midiNote > kGMDrumNoteMax) return 0.0f;
        return std::clamp(ArpSID_sanitizeFloat(gmNoteLevels_[(size_t)midiNote]), 0.0f, 1.0f);
    }

    void clearGMDrumClassLevels(SidGMDrumClass cls) noexcept {
        for (int note = kGMDrumNoteMin; note <= kGMDrumNoteMax; ++note) {
            if (sidGMDrumClassForNote(static_cast<uint8_t>(note)) == cls)
                gmNoteLevels_[(size_t)note] = 0.0f;
        }
    }

    void dampGMDrumClassLevels(SidGMDrumClass cls, float scale) noexcept {
        const float s = std::clamp(std::isfinite(scale) ? scale : 0.0f, 0.0f, 1.0f);
        for (int note = kGMDrumNoteMin; note <= kGMDrumNoteMax; ++note) {
            if (sidGMDrumClassForNote(static_cast<uint8_t>(note)) == cls)
                gmNoteLevels_[(size_t)note] *= s;
        }
    }

    int lastGMDrumNote() const noexcept { return lastGMDrumNote_; }
    int lastGMDrumClass() const noexcept { return static_cast<int>(lastGMDrumClass_); }

    // B8: Filter-sweep owner policy.
    // Returns the voice index that currently owns the filter sweep, or 0xFF if none.
    uint8_t filterSweepOwner() const noexcept { return filterSweepOwnerVoice_; }
    // Claim filter-sweep ownership for `voiceIndex`. Clears sweep activity on
    // all other voices — they must re-arm if they want to sweep later.
    void claimFilterSweepOwnership(uint8_t voiceIndex) noexcept {
        if (voiceIndex >= 3u) return;
        filterSweepOwnerVoice_       = voiceIndex;
        filterSweepOwnerGeneration_  = filterSweepOwnerGeneration_ + 1u;
        for (uint8_t v = 0u; v < 3u; ++v) {
            if (v != voiceIndex) {
                authenticFilterSweepActive_[(size_t)v] = 0u;
                authenticFilterSweepCoeff_[(size_t)v]  = 0.0f;
            }
        }
    }

    // B10: DR-SID playback mode observable by GUI.
    DrSidPlaybackMode drSidPlaybackMode() const noexcept { return drSidPlaybackMode_; }
    void setDrSidPlaybackMode(DrSidPlaybackMode m) noexcept { drSidPlaybackMode_ = m; }
    std::uint64_t wavetableTriggerCount() const noexcept { return wavetableTriggerCount_; }
    std::uint64_t wavetableStepEmitCount() const noexcept {
        std::uint64_t total = 0u;
        for (const auto& r : wavetableRunners_) total += r.stepsEmittedSinceLastReset();
        return total;
    }
    float lastGMDrumVelocity() const noexcept { return std::clamp(ArpSID_sanitizeFloat(lastGMDrumVelocity_), 0.0f, 1.0f); }
    const char* lastGMDrumName() const noexcept { return lastGMDrumNote_ >= 0 ? sidGMDrumFullName(static_cast<uint8_t>(lastGMDrumNote_)) : "None"; }
    const char* lastGMDrumClassName() const noexcept { return sidGMDrumClassName(lastGMDrumClass_); }
    float getVoiceLastSample(int i) const noexcept { return sidChip.getVoiceLastSample(i); }
    float getVoiceEnvelopeLevel(int i) const noexcept { return sidChip.getVoiceEnvelopeLevel(i); }
    float getLastFilterInputSample() const noexcept { return sidChip.getLastFilterInputSample(); }
    float getLastFilterOutputSample() const noexcept { return sidChip.getLastFilterOutputSample(); }
    const std::array<uint8_t, 0x20>& getRegisterImage() const noexcept { return drsidRegImage_; }
    struct DrSidScopeSnapshot {
        float osc[3][256]{};
        float filter[2][256]{};
        uint32_t writePos = 0u;
        uint8_t activeMask = 0u;
    };
    // Race-safe scope snapshot. The GUI only reads a consumer-owned triple
    // buffer slot; it never touches render-owned live rings.
    void getScopeSnapshot(float outOsc[3][256], float outFilter[2][256], uint8_t& activeMask, uint32_t& writePos) const noexcept {
        DrSidScopeSnapshot snap{};
        drsidScopeTriple_.peekLatest(snap);
        std::memcpy(outOsc, snap.osc, sizeof(snap.osc));
        std::memcpy(outFilter, snap.filter, sizeof(snap.filter));
        activeMask = snap.activeMask;
        writePos = snap.writePos;
    }
    uint8_t getScopeActiveMask() const noexcept {
        uint8_t m = 0;
        for (int i = 0; i < 3; ++i) {
            if (sidChip.isVoiceActive(i) || std::fabs(sidChip.getVoiceLastSample(i)) > 1.0e-5f)
                m |= static_cast<uint8_t>(1u << i);
        }
        return m;
    }
    
    // Trigger drum sound
    void trigger(DrumType type, float velocity = 1.0f) {
        switch (type) {
            case DrumType::Kick:
                triggerKick(velocity);
                break;
            case DrumType::Snare:
                triggerSnare(velocity);
                break;
            case DrumType::ClosedHat:
                triggerClosedHat(velocity);
                break;
            case DrumType::OpenHat:
                triggerOpenHat(velocity);
                break;
            case DrumType::Clap:
                triggerClap(velocity);
                break;
            case DrumType::Cowbell:
                triggerCowbell(velocity);
                break;
            case DrumType::Tom:
                // v587: use canonical GM note (47) so SID core and overlay
                // tomBaseHzForGMNote_ lookups resolve to the same pitch table entry.
                triggerTom(velocity, 0.0f, 1.0f, canonicalMidiNoteForDrumType(DrumType::Tom));
                break;
            case DrumType::Rim:
                triggerRim(velocity);
                break;
        }
        registerDrumActivity_(type, velocity, canonicalMidiNoteForDrumType(type));
    }

    void triggerMidiNote(int midiNote, float velocity = 1.0f) {
        velocity = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        // B7: velocity 0 is NoteOff per MIDI spec.
        if (velocity < 1.0f / 127.0f) { noteOffMidi(midiNote); return; }
        const uint8_t note = static_cast<uint8_t>(std::clamp(midiNote, 0, 127));
        const SidGMDrumNoteSpec spec = sidGMDrumSpecForNote(note);
        lastGMDrumNote_ = static_cast<int>(note);
        lastGMDrumClass_ = spec.drumClass;
        lastGMDrumVelocity_ = velocity;
        // Exact per-pad level is published after GM velocity scaling in triggerGMDrumSpec_.
        holdMidiNote_(note);
        triggerGMDrumSpec_(spec, velocity);
    }

    void triggerKitMidiNote(int midiNote,
                            float velocity,
                            std::uint16_t selectedFactorySlot,
                            bool hasSelectedFactorySlot,
                            std::uint8_t voiceWaveform,
                            std::uint8_t voiceAttackDecay,
                            std::uint8_t voiceSustainRelease,
                            std::uint16_t voicePulseWidth,
                            std::uint8_t voiceFlags,
                            std::uint8_t voiceOverrideMask) {
        lastKitSelectedFactorySlot_ = selectedFactorySlot;
        lastKitHasSelectedFactorySlot_ = hasSelectedFactorySlot;
        lastKitVoiceWaveform_ = voiceWaveform;
        lastKitVoiceAttackDecay_ = voiceAttackDecay;
        lastKitVoiceSustainRelease_ = voiceSustainRelease;
        lastKitVoicePulseWidth_ = static_cast<std::uint16_t>(voicePulseWidth & 0x0FFFu);
        lastKitVoiceFlags_ = voiceFlags;
        lastKitVoiceOverrideMask_ = voiceOverrideMask;
        ++kitTriggerCount_;

        const KitSlotRuntimeShape_ shape = kitSlotRuntimeShape_(selectedFactorySlot, hasSelectedFactorySlot);
        const ScopedKitRuntimeShape_ scopedShape(*this, shape);
        const std::uint8_t note = static_cast<std::uint8_t>(std::clamp(midiNote, 0, 127));
        const SidGMDrumNoteSpec spec = sidGMDrumSpecForNote(note);
        const float vel = std::clamp(velocity * shape.velocityScale, 0.0f, 1.0f);
        if (vel < 1.0f / 127.0f) { noteOffMidi(note); return; }

        lastGMDrumNote_ = static_cast<int>(note);
        lastGMDrumClass_ = spec.drumClass;
        lastGMDrumVelocity_ = vel;
        holdMidiNote_(note);

        const KitAuthoredDrSidVoiceProgram_ program =
            makeKitAuthoredVoiceProgram_(spec, vel, selectedFactorySlot, hasSelectedFactorySlot,
                                          voiceWaveform, voiceAttackDecay, voiceSustainRelease,
                                          voicePulseWidth, voiceFlags, voiceOverrideMask);
        applyKitAuthoredVoiceProgram_(program);
        registerGMDrumActivity_(spec, vel);
    }

    std::uint16_t lastKitSelectedFactorySlot() const noexcept { return lastKitSelectedFactorySlot_; }
    bool lastKitHasSelectedFactorySlot() const noexcept { return lastKitHasSelectedFactorySlot_; }
    std::uint8_t lastKitVoiceWaveform() const noexcept { return lastKitVoiceWaveform_; }
    std::uint8_t lastKitVoiceAttackDecay() const noexcept { return lastKitVoiceAttackDecay_; }
    std::uint8_t lastKitVoiceSustainRelease() const noexcept { return lastKitVoiceSustainRelease_; }
    std::uint16_t lastKitVoicePulseWidth() const noexcept { return lastKitVoicePulseWidth_; }
    std::uint8_t lastKitVoiceFlags() const noexcept { return lastKitVoiceFlags_; }
    std::uint8_t lastKitVoiceOverrideMask() const noexcept { return lastKitVoiceOverrideMask_; }
    std::uint64_t kitTriggerCount() const noexcept { return kitTriggerCount_; }
    std::uint64_t kitVoiceOverrideAppliedCount() const noexcept { return kitVoiceOverrideAppliedCount_; }
    std::uint8_t lastKitAppliedVoiceIndex() const noexcept { return lastKitAppliedVoiceIndex_; }
    std::uint8_t lastKitAppliedWaveformControl() const noexcept { return lastKitAppliedWaveformControl_; }
    std::uint8_t lastKitAppliedAttackDecay() const noexcept { return lastKitAppliedAttackDecay_; }
    std::uint8_t lastKitAppliedSustainRelease() const noexcept { return lastKitAppliedSustainRelease_; }
    std::uint16_t lastKitAppliedPulseWidth() const noexcept { return lastKitAppliedPulseWidth_; }
    std::uint8_t lastKitAppliedFlags() const noexcept { return lastKitAppliedFlags_; }

    void noteOffMidi(int midiNote) noexcept {
        if (!shouldReleaseMidiNote_(midiNote)) {
            fractionalSamplePrepared_ = false;
            return;
        }
        // DrSID is a one-shot drum engine. A MIDI NoteOff is ledger/accounting only;
        // it must not force the audible SID lane toward silence. DAWs commonly emit
        // very short drum notes or delayed note-offs, and using NoteOff as a choke
        // makes kicks/hats/snare intermittently disappear even though each trigger
        // has already scheduled its own SID-authentic release. Only explicit
        // all-notes-off/panic may hard-clear voices.
        fractionalSamplePrepared_ = false;
    }

    void allNotesOff() noexcept {
        chokeFamily0Drums_();
        chokeFamily1Drums_();
        chokeTomDrum_();
        chokeFamily2Drums_();
        forceVoiceIdle_(0);
        forceVoiceIdle_(1);
        forceVoiceIdle_(2);
        for (auto& r : wavetableRunners_) r.abort();
        cleanRestartRampRemaining_.fill(0);
        cleanRestartRampTotal_.fill(0);
        cleanRestartRampStart_.fill(0.0f);
        cleanRestartRampTarget_.fill(0.0f);
        midiHeldCounts_.fill(0u);
        midiHeldGeneration_.fill(0u);
        familyHeldCounts_.fill(0u);
        familyGeneration_.fill(0u);
        gmNoteLevels_.fill(0.0f);
        activeTomGMNote_ = -1;
        drumVoiceAllocator_.reset();
        intervalCursor_ = 0u;
        lastGMDrumNote_ = -1;
        lastGMDrumClass_ = SidGMDrumClass::Unsupported;
        lastGMDrumVelocity_ = 0.0f;
        lastKitHasSelectedFactorySlot_ = false;
        lastKitSelectedFactorySlot_ = 0u;
        lastKitVoiceWaveform_ = 0u;
        lastKitVoiceAttackDecay_ = 0u;
        lastKitVoiceSustainRelease_ = 0u;
        lastKitVoicePulseWidth_ = 0u;
        lastKitVoiceFlags_ = 0u;
        lastKitVoiceOverrideMask_ = 0u;
        drsidRegImage_[0x04] &= static_cast<uint8_t>(~0x01u);
        drsidRegImage_[0x0B] &= static_cast<uint8_t>(~0x01u);
        drsidRegImage_[0x12] &= static_cast<uint8_t>(~0x01u);
        fractionalSamplePrepared_ = false;
    }

    void allNotesOffChannel(int /*channel*/) noexcept {
        allNotesOff();
    }
    
    void renderFractionalCycleSpanContribution(float& outL, float& outR, uint16_t cycleStart, uint16_t cycleEnd) noexcept {
        outL = outR = 0.0f;
        prepareFractionalHostSample_();
        float l = 0.0f, r = 0.0f;
        sidChip.renderCycleWindowContribution(cycleStart, cycleEnd, l, r);
        tickWavetableRunnersForCycles_(cycleEnd > cycleStart ? static_cast<std::uint32_t>(cycleEnd - cycleStart) : 0u);
        const float gl = preparedFractionalMasterVolume_;
        outL += shapeDrSidSample_(l, 0.0f, gl);
        outR += shapeDrSidSample_(r, 0.0f, gl);
    }

    void renderSubCyclePhaseContribution(float& outL, float& outR, uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd) noexcept {
        outL = outR = 0.0f;
        prepareFractionalHostSample_();
        float l = 0.0f, r = 0.0f;
        sidChip.renderSubCyclePhaseContribution(cycleIndex, subphaseStart, subphaseEnd, l, r);
        if (subphaseEnd >= kSidSubcycleBoundary && subphaseStart < kSidSubcycleBoundary)
            tickWavetableRunnersForCycles_(1u);
        const float gl = preparedFractionalMasterVolume_;
        outL += shapeDrSidSample_(l, 0.0f, gl);
        outR += shapeDrSidSample_(r, 0.0f, gl);
    }

    void finalizeFractionalHostSample(float& outL, float& outR) noexcept {
        outL = outR = 0.0f;
        prepareFractionalHostSample_();
        float l = 0.0f, r = 0.0f;
        sidChip.finalizePlannedSample(l, r);
        const float gl = preparedFractionalMasterVolume_;
        outL += shapeDrSidSample_(l, preparedOverlaySample_, gl);
        outR += shapeDrSidSample_(r, preparedOverlaySample_, gl);
        preparedOverlaySample_ = 0.0f;
        fractionalSamplePrepared_ = false;
        updateEnvelopes(1);
    }

    // Process audio

    bool wavetableModeActive_() const noexcept {
        return drSidPlaybackMode_ == DrSidPlaybackMode::Wavetable;
    }

    static std::uint8_t wavetableVoiceForClass_(SidGMDrumClass cls) noexcept {
        switch (Drsid::makeCanonicalDrSidProgram(cls).voicePolicy) {
            case Drsid::VoicePolicy::FixedVoice0: return 0u;
            case Drsid::VoicePolicy::FixedVoice1: return 1u;
            case Drsid::VoicePolicy::FixedVoice2: return 2u;
            default:
                switch (cls) {
                    case SidGMDrumClass::Kick:
                    case SidGMDrumClass::Cowbell: return 0u;
                    case SidGMDrumClass::Snare:
                    case SidGMDrumClass::Clap:
                    case SidGMDrumClass::Tom: return 1u;
                    case SidGMDrumClass::ClosedHat:
                    case SidGMDrumClass::OpenHat:
                    case SidGMDrumClass::Rim: return 2u;
                    default: return 0u;
                }
        }
    }

    const Drsid::DrSidInstrumentProgram* wavetableProgramForClass_(SidGMDrumClass cls) const noexcept {
        for (const auto& p : canonicalWavetableKit_) {
            if (p.drumClass == cls && Drsid::programIsWellFormed(p)) return &p;
        }
        return nullptr;
    }

    // Maps the per-drum Tune/Decay/Tone knobs — which the fixed SidAuthentic
    // microprograms would otherwise ignore — onto a modulated copy of the
    // canonical program: Tune scales every step's oscillator frequency, Decay
    // scales the step schedule (cycleOffset/durationCycles) and the release
    // tail. Modulation is centered on each knob's default position so a default
    // patch reproduces the authored canonical hit bit-for-bit (exp2(0)==1), and
    // only deviations from center reshape the sound. Called at note-on only.
    Drsid::DrSidInstrumentProgram makeKnobModulatedWavetableProgram_(
            const Drsid::DrSidInstrumentProgram& base) const noexcept {
        Drsid::DrSidInstrumentProgram p = base;

        // Normalized knob value + its default center for this drum class.
        float tune = 0.5f, tuneCenter = 0.5f;  // → frequency scale
        float len  = 0.5f, lenCenter  = 0.5f;  // → decay / length scale
        bool  invertLen = false;               // "snap" tightens (shortens) as it rises
        switch (base.drumClass) {
            case SidGMDrumClass::Kick:
                tune = kickTuneNormalized();     tuneCenter = 0.5f;
                len  = kickDecayNormalized();    lenCenter  = 0.2f;  break;
            case SidGMDrumClass::Snare:
                tune = snareToneNormalized();    tuneCenter = 0.5f;
                len  = snareSnapNormalized();    lenCenter  = 0.6f;  invertLen = true; break;
            case SidGMDrumClass::ClosedHat:
            case SidGMDrumClass::OpenHat:
                tune = hatTuneNormalized();      tuneCenter = 0.7f;
                len  = hatDecayNormalized();     lenCenter  = 0.2f;  break;
            case SidGMDrumClass::Clap:
                len  = clapDecayNormalized();    lenCenter  = 0.25f; break;  // no dedicated tune
            case SidGMDrumClass::Cowbell:
                tune = cowbellTuneNormalized();  tuneCenter = 0.8f;
                len  = cowbellDecayNormalized(); lenCenter  = 0.42f; break;
            case SidGMDrumClass::Tom:
                tune = tomTuneNormalized();      tuneCenter = 0.55f;
                len  = tomDecayNormalized();     lenCenter  = 0.48f; break;
            default: break;  // Rim / Unsupported: no dedicated knob → play canonical
        }

        const float tuneDelta = tune - tuneCenter;
        float       lenDelta  = len  - lenCenter;
        if (invertLen) lenDelta = -lenDelta;

        const float freqFactor = std::exp2f(tuneDelta * 1.8f);  // ≈ ±0.9 octave at knob extremes
        const float lenFactor  = std::exp2f(lenDelta  * 1.6f);  // ≈ ×1.7 / ×0.57 length

        // Fast path: knobs at their default center → bit-identical canonical.
        const bool freqIdentity = (freqFactor == 1.0f);
        const bool lenIdentity  = (lenFactor  == 1.0f);
        if (freqIdentity && lenIdentity) return p;

        for (std::uint8_t i = 0u; i < p.stepCount; ++i) {
            Drsid::DrSidRegisterStep& s = p.steps[(size_t)i];
            if (!freqIdentity && s.freq != 0u) {
                const long f = std::lround(static_cast<float>(s.freq) * freqFactor);
                s.freq = static_cast<std::uint16_t>(std::clamp<long>(f, 1, 65535));
            }
            if (!lenIdentity) {
                const long co = std::lround(static_cast<float>(s.cycleOffset) * lenFactor);
                s.cycleOffset = static_cast<std::uint16_t>(std::clamp<long>(co, 0, 65535));
                const long dc = std::lround(static_cast<float>(s.durationCycles) * lenFactor);
                s.durationCycles = static_cast<std::uint16_t>(std::clamp<long>(dc, 0, 65535));
                const std::uint8_t rel = static_cast<std::uint8_t>(s.sustainRelease & 0x0Fu);
                if (rel != 0u) {
                    const long nr = std::lround(static_cast<float>(rel) * lenFactor);
                    const std::uint8_t rn = static_cast<std::uint8_t>(std::clamp<long>(nr, 0, 15));
                    s.sustainRelease = static_cast<std::uint8_t>((s.sustainRelease & 0xF0u) | rn);
                }
            }
        }
        return p;
    }

    bool triggerWavetableProgram_(const SidGMDrumNoteSpec& spec, float velocity) noexcept {
        const auto* program = wavetableProgramForClass_(spec.drumClass);
        if (!program) return false;
        const std::uint8_t voice = wavetableVoiceForClass_(spec.drumClass);
        if (voice >= 3u) return false;

        wavetableRunners_[voice].abort();
        authenticPitchSweepActive_[(size_t)voice] = 0u;
        authenticFilterSweepActive_[(size_t)voice] = 0u;
        voiceReleaseSamples[(size_t)voice] = 0;

        const float vel = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        setVoiceVelocity(voice, 0.12f + 0.88f * vel);
        // Apply the per-drum Tune/Decay/Tone knobs to the fixed program. The
        // runner stores a pointer, so the modulated copy lives in a persistent
        // per-voice buffer that outlives this trigger.
        wavetableModulatedProgram_[(size_t)voice] = makeKnobModulatedWavetableProgram_(*program);
        const Drsid::DrSidInstrumentProgram* playProgram = &wavetableModulatedProgram_[(size_t)voice];
        wavetableRunners_[voice].trigger(playProgram, sidChip, voice,
                                         static_cast<std::uint8_t>(std::clamp(std::lround(vel * 127.0f), 1l, 127l)));
        drumEnvelopes[(size_t)drumTypeForGMClass(spec.drumClass)] = vel;
        ++wavetableTriggerCount_;
        return true;
    }

    void tickWavetableRunnersForCycles_(std::uint32_t cycles) noexcept {
        if (!wavetableModeActive_() || cycles == 0u) return;
        for (std::uint8_t v = 0u; v < 3u; ++v) {
            (void)wavetableRunners_[v].tickSidCycles(sidChip, v, cycles);
        }
    }

    void tickWavetableRunners_() noexcept {
        const std::uint32_t cycles = static_cast<std::uint32_t>(
            std::max<std::uint16_t>(1u, estimatedCyclesPerHostSample()));
        tickWavetableRunnersForCycles_(cycles);
    }

    void processReplacingBlock(float** outputs, int numSamples) {
        if (!outputs || !outputs[0] || numSamples <= 0) return;
        float* outL = outputs[0];
        float* outR = outputs[1] ? outputs[1] : outputs[0];
        if (outR == outL) {
            for (int i = 0; i < numSamples; ++i) outL[i] = 0.0f;
        } else {
            for (int i = 0; i < numSamples; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }
        }
        processBlock(outputs, numSamples);
    }

    void processBlock(float** outputs, int numSamples) {
        if (!outputs || !outputs[0])
            return;
        if (numSamples <= 0)
            return;
        if (!isActive()) {
            fractionalSamplePrepared_ = false;
            return;
        }
        
        float* outL = outputs[0];
        float* outR = outputs[1] ? outputs[1] : outputs[0];
        const bool sharedOutputBus = (outL == outR);
        
        const float accentStart = std::clamp(ArpSID_sanitizeFloat(smoothedAccentAmount_, accentAmount_), 0.0f, 1.0f);
        const float driveStart = std::clamp(ArpSID_sanitizeFloat(smoothedOutputDrive_, outputDrive_), 0.0f, 1.0f);
        const float masterStart = std::clamp(ArpSID_sanitizeFloat(smoothedMasterVolume_, masterVolume), 0.0f, kDrSidMaxInternalBusGain);
        const float accentEnd = std::clamp(ArpSID_sanitizeFloat(accentAmount_), 0.0f, 1.0f);
        const float driveEnd = std::clamp(ArpSID_sanitizeFloat(outputDrive_), 0.0f, 1.0f);
        const float masterEnd = std::clamp(ArpSID_sanitizeFloat(masterVolume), 0.0f, kDrSidMaxInternalBusGain);
        const float invN = 1.0f / static_cast<float>(std::max(1, numSamples));
        for (int i = 0; i < numSamples; ++i) {
            const float t = static_cast<float>(i + 1) * invN;
            smoothedAccentAmount_ = accentStart + (accentEnd - accentStart) * t;
            smoothedOutputDrive_ = driveStart + (driveEnd - driveStart) * t;
            smoothedMasterVolume_ = masterStart + (masterEnd - masterStart) * t;
            tickVoiceReleases();
            tickCleanRestartRamps_();
            tickWavetableRunners_();
            // Process SID chip before decaying activity envelopes. This preserves
            // the exact note-on attack level on sample 0 and keeps block render
            // aligned with the fractional/cycle render path.
            float left, right;
            sidChip.processSample(left, right);
            if (!std::isfinite(left)) left = 0.0f;
            if (!std::isfinite(right)) right = 0.0f;

            const float gl = smoothedMasterVolume_;
            const float overlay = renderOverlaySample();
            const float shapedL = shapeDrSidSample_(left, overlay, gl);
            const float shapedR = shapeDrSidSample_(right, overlay, gl);
            const float mixedL = std::fabs(shapedL) < 1.5e-7f ? 0.0f : shapedL;
            const float mixedR = std::fabs(shapedR) < 1.5e-7f ? 0.0f : shapedR;
            if (sharedOutputBus) {
                // Some legacy wrappers/tests can hand the same writable buffer
                // for L/R. Do not double-add DrSID energy into that channel.
                outL[i] += 0.5f * (mixedL + mixedR);
            } else {
                outL[i] += mixedL;
                outR[i] += mixedR;
            }
            publishDrSidScopeSample_();
            updateEnvelopes(1);
        }
        smoothedAccentAmount_ = accentEnd;
        smoothedOutputDrive_ = driveEnd;
        smoothedMasterVolume_ = masterEnd;
        publishDrSidScopeSnapshot_();
    }
    
    // Parameters
    void setKickTune(float value) { kickTune = sanitizeNormParam_(value); }
    void setKickDecay(float value) { kickDecay = 0.01f + sanitizeNormParam_(value) * 0.5f; }
    
    void setSnareTone(float value) { snareTone = sanitizeNormParam_(value); }
    void setSnareSnap(float value) { snareSnap = sanitizeNormParam_(value); }
    
    void setHatTune(float value) { hatTune = sanitizeNormParam_(value); }
    void setHatDecay(float value) { hatDecay = 0.01f + sanitizeNormParam_(value) * 0.3f; }
    
    void setClapDecay(float value) { clapDecay = 0.05f + sanitizeNormParam_(value) * 0.4f; }
    void setCowbellTune(float value) { cowbellTune = sanitizeNormParam_(value); }
    // FIX Bug#10: cowbell release was hardcoded 0.060+0.180=0.240s; now uses cowbellDecay param.
    void setCowbellDecay(float value) { cowbellDecay = 0.04f + sanitizeNormParam_(value) * 0.56f; }
    void setTomTune(float value) { tomTune = sanitizeNormParam_(value); }
    void setTomDecay(float value) { tomDecay = 0.04f + sanitizeNormParam_(value) * 0.56f; }
    
    void setMasterVolume(float value) { masterVolume = std::clamp(ArpSID_sanitizeFloat(value, 0.0f), 0.0f, kDrSidMaxInternalBusGain); }
    void setPerformancePitchBendSemis(float semis) { performancePitchBendSemis = std::clamp(std::isfinite(semis) ? semis : 0.0f, -24.0f, 24.0f); }
    void setPerformanceModWheel(float value) { performanceModWheel = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f); }
    void setPerformancePressure(float value) { performancePressure = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f); }

    float kickTuneNormalized() const noexcept { return kickTune; }
    float kickDecayNormalized() const noexcept { return std::clamp((kickDecay - 0.01f) / 0.5f, 0.0f, 1.0f); }
    float snareToneNormalized() const noexcept { return snareTone; }
    float snareSnapNormalized() const noexcept { return snareSnap; }
    float hatTuneNormalized() const noexcept { return hatTune; }
    float hatDecayNormalized() const noexcept { return std::clamp((hatDecay - 0.01f) / 0.3f, 0.0f, 1.0f); }
    float clapDecayNormalized() const noexcept { return std::clamp((clapDecay - 0.05f) / 0.4f, 0.0f, 1.0f); }
    float cowbellTuneNormalized() const noexcept { return cowbellTune; }
    float cowbellDecayNormalized() const noexcept { return std::clamp((cowbellDecay - 0.04f) / 0.56f, 0.0f, 1.0f); }
    float tomTuneNormalized() const noexcept { return tomTune; }
    float tomDecayNormalized() const noexcept { return std::clamp((tomDecay - 0.04f) / 0.56f, 0.0f, 1.0f); }
    float masterVolumeNormalized() const noexcept { return masterVolume; }

    // True if any drum voice is currently producing audio (envelope above silence floor)
    int getActiveVoiceCount() const noexcept {
        int n = 0;
        for (const auto& e : drumEnvelopes) if (e > 1e-4f) ++n;
        for (const auto& ov : overlays) if (ov.active) ++n;
        for (int i = 0; i < 3; ++i) if (sidChip.isVoiceActive(i)) ++n;
        return std::clamp(n, 0, 64);
    }

    bool isActive() const {
        for (const auto& e : drumEnvelopes)
            if (e > 1e-4f) return true;
        for (const auto& ov : overlays)
            if (ov.active) return true;
        for (int remain : voiceReleaseSamples)
            if (remain > 0) return true;
        for (int remain : cleanRestartRampRemaining_)
            if (remain > 0) return true;
        for (int i = 0; i < 3; ++i)
            if (sidChip.isVoiceActive(i)) return true;
        return false;
    }

private:

    struct KitAuthoredDrSidVoiceProgram_ {
        SidGMDrumClass drumClass = SidGMDrumClass::Unsupported;
        int voiceIndex = 0;
        std::uint16_t freq = 0;
        Waveform waveform = Waveform::Noise;
        std::uint16_t pulseWidth = 0x0800u;
        std::uint8_t attackDecay = 0x00u;
        std::uint8_t sustainRelease = 0x00u;
        std::uint8_t flags = 0u;
        float velocity = 1.0f;
    };

    static Waveform kitWaveformToDrSid_(std::uint8_t waveBits) noexcept {
        const bool tri = (waveBits & 0x10u) != 0u;
        const bool saw = (waveBits & 0x20u) != 0u;
        const bool pul = (waveBits & 0x40u) != 0u;
        const bool noi = (waveBits & 0x80u) != 0u;
        if (noi) return Waveform::Noise;
        if (tri && saw && pul) return Waveform::TriSawPulse;
        if (tri && saw) return Waveform::TriSaw;
        if (tri && pul) return Waveform::TriPulse;
        if (saw && pul) return Waveform::SawPulse;
        if (pul) return Waveform::Pulse;
        if (saw) return Waveform::Sawtooth;
        if (tri) return Waveform::Triangle;
        return Waveform::Noise;
    }

    static int kitVoiceIndexForDrumClass_(SidGMDrumClass cls) noexcept {
        switch (cls) {
            case SidGMDrumClass::Kick: return 0;
            case SidGMDrumClass::Snare:
            case SidGMDrumClass::Clap:
            case SidGMDrumClass::Rim: return 1;
            case SidGMDrumClass::ClosedHat:
            case SidGMDrumClass::OpenHat:
            case SidGMDrumClass::Cowbell:
            case SidGMDrumClass::Tom: return 2;
            default: return 0;
        }
    }

    KitAuthoredDrSidVoiceProgram_ makeKitAuthoredVoiceProgram_(const SidGMDrumNoteSpec& spec,
                                                               float velocity,
                                                               std::uint16_t selectedFactorySlot,
                                                               bool hasSelectedFactorySlot,
                                                               std::uint8_t voiceWaveform,
                                                               std::uint8_t voiceAttackDecay,
                                                               std::uint8_t voiceSustainRelease,
                                                               std::uint16_t voicePulseWidth,
                                                               std::uint8_t voiceFlags,
                                                               std::uint8_t voiceOverrideMask) noexcept {
        KitAuthoredDrSidVoiceProgram_ p{};
        p.drumClass = spec.drumClass;
        p.voiceIndex = kitVoiceIndexForDrumClass_(spec.drumClass);
        p.velocity = velocity;

        const float slotA = hasSelectedFactorySlot && ::ArpSID::isDrSidFactorySlot(static_cast<int>(selectedFactorySlot))
            ? unitFromSlot_(selectedFactorySlot, 101u + static_cast<std::uint32_t>(p.voiceIndex)) : 0.5f;
        const float slotB = hasSelectedFactorySlot && ::ArpSID::isDrSidFactorySlot(static_cast<int>(selectedFactorySlot))
            ? unitFromSlot_(selectedFactorySlot, 113u + static_cast<std::uint32_t>(p.voiceIndex)) : 0.5f;

        switch (spec.drumClass) {
            case SidGMDrumClass::Kick: p.freq = 0x0A00u; break;
            case SidGMDrumClass::Snare: p.freq = 0x3000u; break;
            case SidGMDrumClass::ClosedHat:
            case SidGMDrumClass::OpenHat: p.freq = 0x6000u; break;
            case SidGMDrumClass::Clap: p.freq = 0x4000u; break;
            case SidGMDrumClass::Cowbell: p.freq = 0x4000u; break;
            case SidGMDrumClass::Tom: p.freq = 0x1A00u; break;
            case SidGMDrumClass::Rim: p.freq = 0x5000u; break;
            default: p.freq = 0x1000u; break;
        }
        p.waveform = (p.voiceIndex == 0) ? Waveform::Triangle : (p.voiceIndex == 1 ? Waveform::Noise : Waveform::Pulse);
        if (slotA > 0.80f) p.waveform = Waveform::TriSawPulse;
        else if (slotA > 0.60f) p.waveform = Waveform::SawPulse;
        else if (slotA > 0.40f) p.waveform = (p.voiceIndex == 1 ? Waveform::Noise : Waveform::Pulse);
        else if (slotA > 0.20f) p.waveform = Waveform::TriPulse;

        p.attackDecay = static_cast<std::uint8_t>((static_cast<std::uint8_t>(slotA * 4.0f) << 4u) |
                                                  static_cast<std::uint8_t>(4u + static_cast<std::uint8_t>(slotB * 10.0f)));
        p.sustainRelease = static_cast<std::uint8_t>((static_cast<std::uint8_t>(slotB * 10.0f) << 4u) |
                                                     static_cast<std::uint8_t>(3u + static_cast<std::uint8_t>(slotA * 8.0f)));
        p.pulseWidth = static_cast<std::uint16_t>(0x0400u + static_cast<std::uint16_t>(std::clamp(slotB * 2047.0f, 0.0f, 2047.0f)));
        p.flags = static_cast<std::uint8_t>(((slotA > 0.72f) ? 0x01u : 0u) |
                                            ((slotB > 0.78f) ? 0x02u : 0u) |
                                            (((slotA + slotB) > 0.85f) ? 0x04u : 0u));

        if ((voiceOverrideMask & 0x01u) != 0u) p.waveform = kitWaveformToDrSid_(voiceWaveform);
        if ((voiceOverrideMask & 0x02u) != 0u) p.attackDecay = voiceAttackDecay;
        if ((voiceOverrideMask & 0x04u) != 0u) p.sustainRelease = voiceSustainRelease;
        if ((voiceOverrideMask & 0x08u) != 0u) p.pulseWidth = static_cast<std::uint16_t>(voicePulseWidth & 0x0FFFu);
        if ((voiceOverrideMask & 0x10u) != 0u) p.flags = static_cast<std::uint8_t>(voiceFlags & 0x07u);
        return p;
    }

    void applyKitAuthoredVoiceProgram_(const KitAuthoredDrSidVoiceProgram_& p) noexcept {
        const bool kitRing = (p.flags & 0x01u) != 0u;
        const bool kitSync = (p.flags & 0x02u) != 0u;
        sidChip.setVoiceRingModEnable(p.voiceIndex, kitRing);
        sidChip.setVoiceSyncEnable(p.voiceIndex, kitSync);
        applyDrSidVoice_(p.voiceIndex, p.freq, p.waveform, p.pulseWidth,
                         static_cast<std::uint8_t>((p.attackDecay >> 4u) & 0x0Fu),
                         static_cast<std::uint8_t>(p.attackDecay & 0x0Fu),
                         static_cast<std::uint8_t>((p.sustainRelease >> 4u) & 0x0Fu),
                         static_cast<std::uint8_t>(p.sustainRelease & 0x0Fu),
                         kitSync,
                         kitRing);
        sidChip.setVoiceRingModEnable(p.voiceIndex, kitRing);
        sidChip.setVoiceSyncEnable(p.voiceIndex, kitSync);
        if ((p.flags & 0x04u) != 0u) {
            applyDrSidFilter_(0x700u, 10u, FilterMode::LowPass,
                              p.voiceIndex == 0, p.voiceIndex == 1, p.voiceIndex == 2);
        }

        const std::size_t reg = static_cast<std::size_t>(p.voiceIndex * 7);
        lastKitAppliedVoiceIndex_ = static_cast<std::uint8_t>(p.voiceIndex);
        lastKitAppliedWaveformControl_ = drsidRegImage_[reg + 4];
        lastKitAppliedAttackDecay_ = drsidRegImage_[reg + 5];
        lastKitAppliedSustainRelease_ = drsidRegImage_[reg + 6];
        lastKitAppliedPulseWidth_ = static_cast<std::uint16_t>(drsidRegImage_[reg + 2] |
            (static_cast<std::uint16_t>(drsidRegImage_[reg + 3] & 0x0Fu) << 8u));
        lastKitAppliedFlags_ = p.flags;
        ++kitVoiceOverrideAppliedCount_;
    }

    void registerGMDrumActivity_(const SidGMDrumNoteSpec& spec, float velocity) noexcept {
        switch (spec.drumClass) {
            case SidGMDrumClass::Kick: registerDrumActivity_(DrumType::Kick, velocity, spec.note); break;
            case SidGMDrumClass::Snare: registerDrumActivity_(DrumType::Snare, velocity, spec.note); break;
            case SidGMDrumClass::ClosedHat: registerDrumActivity_(DrumType::ClosedHat, velocity, spec.note); break;
            case SidGMDrumClass::OpenHat: registerDrumActivity_(DrumType::OpenHat, velocity, spec.note); break;
            case SidGMDrumClass::Clap: registerDrumActivity_(DrumType::Clap, velocity, spec.note); break;
            case SidGMDrumClass::Cowbell: registerDrumActivity_(DrumType::Cowbell, velocity, spec.note); break;
            case SidGMDrumClass::Tom: registerDrumActivity_(DrumType::Tom, velocity, spec.note); break;
            case SidGMDrumClass::Rim: registerDrumActivity_(DrumType::Rim, velocity, spec.note); break;
            default: break;
        }
    }

    struct KitSlotRuntimeShape_ {
        float tuneBias = 0.0f;
        float decayBias = 0.0f;
        float velocityScale = 1.0f;
        float accent = 0.68f;
        float drive = 0.18f;
        float hatMetal = 0.62f;
        float clapSpread = 0.54f;
    };

    struct DrSidKitVoiceConfig_ {
        Waveform waveform = Waveform::Noise;
        std::uint8_t attackDecay = 0x08u;
        std::uint8_t sustainRelease = 0x04u;
        std::uint16_t pulseWidth = 0x0800u;
        std::uint8_t flags = 0u;
        KitSlotRuntimeShape_ shape{};
    };

        static std::uint8_t attackFromAD_(std::uint8_t ad) noexcept { return static_cast<std::uint8_t>((ad >> 4u) & 0x0Fu); }
    static std::uint8_t decayFromAD_(std::uint8_t ad) noexcept { return static_cast<std::uint8_t>(ad & 0x0Fu); }
    static std::uint8_t sustainFromSR_(std::uint8_t sr) noexcept { return static_cast<std::uint8_t>((sr >> 4u) & 0x0Fu); }
    static std::uint8_t releaseFromSR_(std::uint8_t sr) noexcept { return static_cast<std::uint8_t>(sr & 0x0Fu); }

    static int voiceIndexForGMClass_(SidGMDrumClass cls) noexcept {
        switch (cls) {
            case SidGMDrumClass::Kick: return 0;
            case SidGMDrumClass::Snare:
            case SidGMDrumClass::Clap:
            case SidGMDrumClass::Rim: return 1;
            case SidGMDrumClass::ClosedHat:
            case SidGMDrumClass::OpenHat:
            case SidGMDrumClass::Cowbell:
            case SidGMDrumClass::Tom: return 2;
            default: return 0;
        }
    }


    static float unitFromSlot_(std::uint16_t slot, std::uint32_t salt) noexcept {
        std::uint32_t x = static_cast<std::uint32_t>(slot) * 1103515245u + 12345u + salt * 2654435761u;
        x ^= (x >> 16);
        return static_cast<float>(x & 0xFFu) * (1.0f / 255.0f);
    }

    KitSlotRuntimeShape_ kitSlotRuntimeShape_(std::uint16_t slot, bool hasSlot) const noexcept {
        KitSlotRuntimeShape_ s{};
        s.accent = accentAmount_;
        s.drive = outputDrive_;
        s.hatMetal = hatMetal_;
        s.clapSpread = clapSpread_;
        if (!hasSlot || !::ArpSID::isDrSidFactorySlot(static_cast<int>(slot))) return s;
        s.tuneBias = unitFromSlot_(slot, 1u) * 0.34f - 0.17f;
        s.decayBias = unitFromSlot_(slot, 2u) * 0.30f - 0.15f;
        s.velocityScale = 0.82f + unitFromSlot_(slot, 3u) * 0.34f;
        s.accent = std::clamp(0.35f + unitFromSlot_(slot, 4u) * 0.60f, 0.0f, 1.0f);
        s.drive = std::clamp(0.05f + unitFromSlot_(slot, 5u) * 0.42f, 0.0f, 1.0f);
        s.hatMetal = std::clamp(0.25f + unitFromSlot_(slot, 6u) * 0.72f, 0.0f, 1.0f);
        s.clapSpread = std::clamp(0.20f + unitFromSlot_(slot, 7u) * 0.74f, 0.0f, 1.0f);
        return s;
    }

    struct ScopedKitRuntimeShape_ {
        DrSidEngine& e;
        float kickTune, kickDecay, snareTone, snareSnap, hatTune, hatDecay;
        float clapDecay, cowbellTune, cowbellDecay, tomTune, tomDecay;
        float accent, drive, hatMetal, clapSpread;
        explicit ScopedKitRuntimeShape_(DrSidEngine& engine, const KitSlotRuntimeShape_& shape) noexcept
            : e(engine),
              kickTune(engine.kickTune), kickDecay(engine.kickDecay),
              snareTone(engine.snareTone), snareSnap(engine.snareSnap),
              hatTune(engine.hatTune), hatDecay(engine.hatDecay),
              clapDecay(engine.clapDecay), cowbellTune(engine.cowbellTune),
              cowbellDecay(engine.cowbellDecay), tomTune(engine.tomTune),
              tomDecay(engine.tomDecay),
              accent(engine.accentAmount_), drive(engine.outputDrive_),
              hatMetal(engine.hatMetal_), clapSpread(engine.clapSpread_) {
            e.kickTune = std::clamp(e.kickTune + shape.tuneBias, 0.0f, 1.0f);
            e.snareTone = std::clamp(e.snareTone + shape.tuneBias, 0.0f, 1.0f);
            e.hatTune = std::clamp(e.hatTune + shape.tuneBias, 0.0f, 1.0f);
            e.cowbellTune = std::clamp(e.cowbellTune + shape.tuneBias, 0.0f, 1.0f);
            e.tomTune = std::clamp(e.tomTune + shape.tuneBias, 0.0f, 1.0f);
            e.kickDecay = std::clamp(e.kickDecay + shape.decayBias * 0.50f, 0.01f, 0.51f);
            e.snareSnap = std::clamp(e.snareSnap + shape.decayBias, 0.0f, 1.0f);
            e.hatDecay = std::clamp(e.hatDecay + shape.decayBias * 0.30f, 0.01f, 0.31f);
            e.clapDecay = std::clamp(e.clapDecay + shape.decayBias * 0.40f, 0.05f, 0.45f);
            e.cowbellDecay = std::clamp(e.cowbellDecay + shape.decayBias * 0.40f, 0.04f, 0.44f);
            e.tomDecay = std::clamp(e.tomDecay + shape.decayBias, 0.0f, 1.0f);
            e.accentAmount_ = shape.accent;
            e.outputDrive_ = shape.drive;
            e.hatMetal_ = shape.hatMetal;
            e.clapSpread_ = shape.clapSpread;
        }
        ~ScopedKitRuntimeShape_() noexcept {
            e.kickTune = kickTune; e.kickDecay = kickDecay;
            e.snareTone = snareTone; e.snareSnap = snareSnap;
            e.hatTune = hatTune; e.hatDecay = hatDecay;
            e.clapDecay = clapDecay; e.cowbellTune = cowbellTune;
            e.cowbellDecay = cowbellDecay; e.tomTune = tomTune;
            e.tomDecay = tomDecay;
            e.accentAmount_ = accent; e.outputDrive_ = drive;
            e.hatMetal_ = hatMetal; e.clapSpread_ = clapSpread;
        }
    };

    static float sanitizeNormParam_(float value) noexcept {
        return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    }

    float currentAccentGain_() const noexcept {
        float env = 0.0f;
        for (float e : drumEnvelopes) env = std::max(env, e);
        const float dynamicAccent = sidDrSidAccentGain(env, performanceModWheel, performancePressure);
        const float accentBlend = 0.20f + 0.55f * smoothedAccentAmount_;
        return std::clamp((1.0f - accentBlend) + dynamicAccent * accentBlend, 0.25f, kDrSidMaxAccentGain);
    }

    float shapeDrSidSample_(float sidSample, float overlaySample, float gain) const noexcept {
        const float mixed = (sidSample * currentAccentGain_() + overlaySample) * gain * kDrSidPostSidHeadroom;
        if (drumMachineModel_ != DrSidMachineModel::AnalogX0X8)
            return sidDrSidOutputShape(mixed);
        const float drive = 1.0f + smoothedOutputDrive_ * 3.4f;
        const float driven = std::tanh(mixed * drive);
        const float wet = 0.16f + smoothedOutputDrive_ * 0.34f;
        return sidDrSidOutputShape(mixed * (1.0f - wet) + driven * wet);
    }

    void clearScopeHistory_() noexcept {
        for (auto& s : drsidOscScope_) s.fill(0.0f);
        for (auto& s : drsidFilterScope_) s.fill(0.0f);
        drsidScopeWritePos_ = 0u;
        drsidScopeActiveMask_ = 0u;
        drsidScopeTriple_.clearWriteSlot();
        drsidScopeTriple_.publish();
    }

    float effectiveVoiceLevelForCoreConfigure_(int voiceIndex) const noexcept {
        if (voiceIndex < 0 || voiceIndex > 2) return 0.0f;
        const size_t i = static_cast<size_t>(voiceIndex);
        // Do not let profile/sample-rate/reset-style reconfiguration punch
        // through the clean restart ramp. Re-applying the SID core while a
        // ramp is armed must preserve the de-click invariant, not restore the
        // target velocity as an instantaneous step.
        if (cleanRestartRampRemaining_[i] > 0 && cleanRestartRampTotal_[i] > 0) {
            const int done = std::max(0, cleanRestartRampTotal_[i] - cleanRestartRampRemaining_[i]);
            const float t = std::clamp(static_cast<float>(done) / static_cast<float>(cleanRestartRampTotal_[i]), 0.0f, 1.0f);
            const float start = std::clamp(cleanRestartRampStart_[i], 0.0f, 1.0f);
            const float target = std::clamp(cleanRestartRampTarget_[i], 0.0f, 1.0f);
            return start + (target - start) * t;
        }
        return std::clamp(voiceVelocity[i], 0.0f, 1.0f);
    }

    void configureSidCoreForDrums_() noexcept {
        sidChip.setMasterVolume(15);
        sidChip.setVoice3Off(false);
        sidChip.setVoiceLevel(0, effectiveVoiceLevelForCoreConfigure_(0));
        sidChip.setVoiceLevel(1, effectiveVoiceLevelForCoreConfigure_(1));
        sidChip.setVoiceLevel(2, effectiveVoiceLevelForCoreConfigure_(2));
        sidChip.setVoiceSyncEnable(0, false);
        sidChip.setVoiceSyncEnable(1, false);
        sidChip.setVoiceSyncEnable(2, false);
        sidChip.setVoiceRingModEnable(0, false);
        sidChip.setVoiceRingModEnable(1, false);
        sidChip.setVoiceRingModEnable(2, false);
        applyDrSidFilter_(0x680u, 6u, FilterMode::LowPass, true, true, true);
        // Collapse the filter cutoff smoother onto this freshly-configured cutoff.
        // Without this the smoother sits at the cold 20 Hz reset placeholder, so a
        // filter-routed one-shot (cowbell routes voice 0, tom routes voice 1
        // through the lowpass) landing on the very first render block is muted
        // during its short transient — it only sounded after an unrelated warm-up
        // block had ramped the smoother up. Snapping here makes the cold first
        // block behave identically to a warmed one.
        sidChip.snapFilterSmoothing();
    }

    static uint8_t sidWaveControl_(Waveform wf, bool gate, bool sync = false, bool ring = false, bool test = false) noexcept {
        return static_cast<uint8_t>((sidResolveWaveformControlMask(static_cast<uint8_t>(wf)) & 0xF0u) |
                                    (test ? 0x08u : 0x00u) |
                                    (ring ? 0x04u : 0x00u) |
                                    (sync ? 0x02u : 0x00u) |
                                    (gate ? 0x01u : 0x00u));
    }

    void mirrorVoiceRegs_(int voiceIndex,
                          uint16_t freq,
                          uint16_t pulseWidth,
                          Waveform wf,
                          bool gate,
                          uint8_t attack,
                          uint8_t decay,
                          uint8_t sustain,
                          uint8_t release,
                          bool sync = false,
                          bool ring = false,
                          bool test = false) noexcept {
        if (voiceIndex < 0 || voiceIndex > 2) return;
        const size_t base = static_cast<size_t>(voiceIndex * 7);
        drsidRegImage_[base + 0] = static_cast<uint8_t>(freq & 0xFFu);
        drsidRegImage_[base + 1] = static_cast<uint8_t>((freq >> 8u) & 0xFFu);
        drsidRegImage_[base + 2] = static_cast<uint8_t>(pulseWidth & 0xFFu);
        drsidRegImage_[base + 3] = static_cast<uint8_t>((pulseWidth >> 8u) & 0x0Fu);
        drsidRegImage_[base + 4] = sidWaveControl_(wf, gate, sync, ring, test);
        drsidRegImage_[base + 5] = static_cast<uint8_t>(((attack & 0x0Fu) << 4u) | (decay & 0x0Fu));
        drsidRegImage_[base + 6] = static_cast<uint8_t>(((sustain & 0x0Fu) << 4u) | (release & 0x0Fu));
    }

    void applyDrSidVoice_(int voiceIndex,
                          uint16_t freq,
                          Waveform wf,
                          uint16_t pulseWidth,
                          uint8_t attack,
                          uint8_t decay,
                          uint8_t sustain,
                          uint8_t release,
                          bool sync = false,
                          bool ring = false,
                          bool test = false) noexcept {
        if (voiceIndex < 0 || voiceIndex > 2) return;
        auto& voice = sidChip.getVoice(voiceIndex);
        voice.setFrequency(freq);
        voice.setPulseWidth(pulseWidth);
        voice.setWaveform(static_cast<uint8_t>(wf));
        voice.setAttack(attack);
        voice.setDecay(decay);
        voice.setSustain(sustain);
        voice.setRelease(release);
        mirrorVoiceRegs_(voiceIndex, freq, pulseWidth, wf, true, attack, decay, sustain, release, sync, ring, test);
    }

    void applyDrSidFilter_(uint16_t cutoff,
                           uint8_t resonance,
                           FilterMode mode,
                           bool routeV1,
                           bool routeV2,
                           bool routeV3) noexcept {
        cutoff = static_cast<uint16_t>(cutoff & 0x07FFu);
        resonance &= 0x0Fu;
        sidChip.setFilterCutoff(cutoff);
        sidChip.setFilterResonance(resonance);
        sidChip.setFilterMode(mode);
        sidChip.setFilterVoiceRouting(routeV1, routeV2, routeV3);
        drsidRegImage_[0x15] = static_cast<uint8_t>(cutoff & 0x07u);
        drsidRegImage_[0x16] = static_cast<uint8_t>((cutoff >> 3u) & 0xFFu);
        drsidRegImage_[0x17] = static_cast<uint8_t>((resonance << 4u) | (routeV1 ? 0x01u : 0u) | (routeV2 ? 0x02u : 0u) | (routeV3 ? 0x04u : 0u));
        drsidRegImage_[0x18] = static_cast<uint8_t>((static_cast<uint8_t>(mode) << 4u) | 0x0Fu);
    }

    int cleanRestartRampSamples_() const noexcept {
        const double sr = sampleRate > 1.0 ? sampleRate : kReferenceSampleRate;
        // Short enough to preserve SID punch, long enough to remove full-scale
        // test-bit/gate discontinuities before reverb/limiter sees them.
        return std::clamp(static_cast<int>(std::lround(sr * 0.0015)), 1, 256);
    }

    void armCleanRestartRamp_(int voiceIndex) noexcept {
        if (voiceIndex < 0 || voiceIndex > 2) return;
        const size_t i = static_cast<size_t>(voiceIndex);
        const int n = cleanRestartRampSamples_();
        cleanRestartRampRemaining_[i] = n;
        cleanRestartRampTotal_[i] = n;
        cleanRestartRampStart_[i] = std::clamp(ArpSID_sanitizeFloat(sidChip.getVoiceLevel(voiceIndex), voiceVelocity[i]), 0.0f, 1.0f);
        cleanRestartRampTarget_[i] = std::clamp(voiceVelocity[i], 0.0f, 1.0f);
    }

    void tickCleanRestartRamps_() noexcept {
        for (int voiceIndex = 0; voiceIndex < 3; ++voiceIndex) {
            const size_t i = static_cast<size_t>(voiceIndex);
            int& remaining = cleanRestartRampRemaining_[i];
            const int total = cleanRestartRampTotal_[i];
            if (remaining <= 0 || total <= 0) continue;
            const int done = total - remaining + 1;
            const float t = std::clamp(static_cast<float>(done) / static_cast<float>(total), 0.0f, 1.0f);
            const float start = std::clamp(cleanRestartRampStart_[i], 0.0f, 1.0f);
            const float target = std::clamp(cleanRestartRampTarget_[i], 0.0f, 1.0f);
            sidChip.setVoiceLevel(voiceIndex, start + (target - start) * t);
            --remaining;
            if (remaining <= 0) {
                cleanRestartRampTotal_[i] = 0;
                cleanRestartRampStart_[i] = 0.0f;
                cleanRestartRampTarget_[i] = 0.0f;
                sidChip.setVoiceLevel(voiceIndex, target);
            }
        }
    }

    void hardRetriggerVoice_(int voiceIndex) noexcept {
        if (voiceIndex < 0 || voiceIndex > 2) return;
        auto& voice = sidChip.getVoice(voiceIndex);
        voice.setGate(false);
        voice.setTestBit(true);
        voice.setTestBit(false);
        voice.setGate(true);
        const size_t base = static_cast<size_t>(voiceIndex * 7);
        drsidRegImage_[base + 4] |= 0x01u;
        if (cleanRestartRampEnabled_) armCleanRestartRamp_(voiceIndex);
        else sidChip.setVoiceLevel(voiceIndex, sanitizeNormParam_(voiceVelocity[static_cast<size_t>(voiceIndex)]));
    }

    void publishDrSidScopeSample_() noexcept {
        const uint32_t wp = drsidScopeWritePos_ & 255u;
        for (int v = 0; v < 3; ++v) drsidOscScope_[(size_t)v][wp] = std::clamp(ArpSID_sanitizeFloat(sidChip.getVoiceLastSample(v)), -1.0f, 1.0f);
        drsidFilterScope_[0][wp] = std::clamp(ArpSID_sanitizeFloat(sidChip.getLastFilterInputSample()), -1.0f, 1.0f);
        drsidFilterScope_[1][wp] = std::clamp(ArpSID_sanitizeFloat(sidChip.getLastFilterOutputSample()), -1.0f, 1.0f);
        drsidScopeActiveMask_ = getScopeActiveMask();
        drsidScopeWritePos_ = (wp + 1u) & 255u;
        if ((drsidScopeWritePos_ & 31u) == 0u) publishDrSidScopeSnapshot_();
    }

    void publishDrSidScopeSnapshot_() noexcept {
        auto& snap = drsidScopeTriple_.writeSlot();
        std::memcpy(snap.osc, drsidOscScope_.data(), sizeof(snap.osc));
        std::memcpy(snap.filter, drsidFilterScope_.data(), sizeof(snap.filter));
        snap.activeMask = drsidScopeActiveMask_;
        snap.writePos = drsidScopeWritePos_;
        drsidScopeTriple_.publish();
    }

    void updateOverlayDcBlockCoeff_() noexcept {
        const double sr = sampleRate > 1.0 ? sampleRate : kReferenceSampleRate;
        const double cutoffHz = 18.0;
        overlayDcBlockR_ = std::clamp(static_cast<float>(std::exp(-2.0 * ArpSID_pi() * cutoffHz / sr)), 0.0f, 0.9999f);
    }

    void prepareFractionalHostSample_() noexcept {
        if (fractionalSamplePrepared_) return;
        tickVoiceReleases();
        tickCleanRestartRamps_();
        // Do not decay drumEnvelopes before the fractional sample is actually
        // rendered; finalizeFractionalHostSample() advances the one-sample
        // activity envelope after consuming the attack-level sample.
        preparedOverlaySample_ = renderOverlaySample();
        preparedFractionalMasterVolume_ = advanceSmoothedMasterVolumeForOneSample_();
        fractionalSamplePrepared_ = true;
    }

    struct OverlayVoice {
        bool active = false;
        DrumType type = DrumType::Kick;
        int midiNote = 36;
        float velocity = 0.0f;
        float accent = 1.0f;
        float env = 0.0f;
        float envB = 0.0f;
        float decay = 0.999f;
        float decayB = 0.999f;
        float freq = 0.0f;
        float freqB = 0.0f;
        float phase = 0.0f;
        float phaseB = 0.0f;
        float phaseInc = 0.0f;
        float phaseIncB = 0.0f;
        float stateA = 0.0f;
        float stateB = 0.0f;
        int burstStage = 0;
        int burstCountdown = 0;
        int burstIntervalSamples = 0;
        uint32_t noise = 0x12345678u;
    };

    SIDChip sidChip;
    static constexpr double kReferenceSampleRate = 44100.0;
    double sampleRate = 44100.0;
    // FIX Bug#11: clock frequency member; set via setClockFrequency() to match host's PAL/NTSC setting.
    double clockFreq = PAL_CLOCK_FREQ;
    // Audit #45 diagnostic — count of invalid clock-frequency arrivals
    // that were rejected (kept last valid clock instead of silent PAL fallback).
    std::uint64_t invalidClockFrequencyRejectCount_ = 0u;
    // Audit #40 diagnostics — state-restore tracking. `Decisions` counts
    // every restore call; `Authored` counters count the two specific
    // edge-case mixes (overlay>0 with each model) that the legacy code
    // would have silently rewritten.
    std::uint64_t stateRestoreModelDecisionsCount_ = 0u;
    std::uint64_t stateRestoreOverlayAuthoredAuthenticCount_ = 0u;
    std::uint64_t stateRestoreOverlayAuthoredAnalogCount_ = 0u;
    // Audit #41/#42 — typed voice allocator. 12-slot pool with explicit
    // stealing policy + choke-group support. Sits next to (not yet
    // replacing) the legacy "lowest envelope" overlay allocator.
    DrumVoiceAllocator drumVoiceAllocator_{};
    uint32_t intervalCursor_ = 0;  // sub-phase cursor for renderIntervalAccurate
    
    std::array<float, 8> drumEnvelopes{};
    std::array<float, 128> gmNoteLevels_{};
    std::array<float, 3> voiceVelocity{};
    std::array<int, 3> voiceReleaseSamples{};
    std::array<float, 3> authenticPitchSweepFreqReg_{};
    std::array<float, 3> authenticPitchSweepTargetReg_{};
    std::array<float, 3> authenticPitchSweepCoeff_{};
    std::array<uint8_t, 3> authenticPitchSweepActive_{};
    std::array<float, 3> authenticFilterSweepCutoff_{};
    std::array<float, 3> authenticFilterSweepTarget_{};
    std::array<float, 3> authenticFilterSweepCoeff_{};
    std::array<uint8_t, 3> authenticFilterSweepActive_{};
    FilterMode authenticFilterSweepMode_ = FilterMode::LowPass;
    uint8_t authenticFilterSweepResonance_ = 0u;
    uint8_t authenticFilterSweepRouteMask_ = 0u;
    // B8: Filter-sweep owner policy — which voice currently "owns" the
    // shared SID filter for sweeping. When a new voice triggers a filter
    // sweep, it claims ownership and stale sweeps on other voices are cleared.
    // 0xFF = no owner; 0..2 = voice index that last claimed ownership.
    uint8_t filterSweepOwnerVoice_ = 0xFFu;
    uint32_t filterSweepOwnerGeneration_ = 0u;
    // B10: Current DR-SID playback mode (read by GUI HUD).
    DrSidPlaybackMode drSidPlaybackMode_ = DrSidPlaybackMode::Wavetable;
    std::array<Drsid::DrSidInstrumentProgram, Drsid::kCanonicalDrSidProgramCount> canonicalWavetableKit_{
        Drsid::makeCanonicalDrSidKitPrograms()
    };
    std::array<DrSidWavetableProgramRunner, 3> wavetableRunners_{};
    // Per-voice scratch holding the Tune/Decay/Tone-modulated copy of the
    // canonical SidAuthentic microprogram. The runner keeps a *pointer* to the
    // program it plays, so the modulated copy must outlive the trigger — one
    // persistent buffer per voice satisfies that (a voice plays one program at
    // a time; re-trigger overwrites the buffer and re-arms the runner together).
    std::array<Drsid::DrSidInstrumentProgram, 3> wavetableModulatedProgram_{};
    std::uint64_t wavetableTriggerCount_ = 0u;
    std::array<OverlayVoice, 12> overlays{};
    std::array<uint8_t, 128> midiHeldCounts_{};
    std::array<uint32_t, 128> midiHeldGeneration_{};
    std::array<uint8_t, kDrumTypeCount> familyHeldCounts_{};
    std::array<uint32_t, kDrumTypeCount> familyGeneration_{};

    // Parameters
    float kickTune = 0.5f;
    float kickDecay = 0.1f;
    float snareTone = 0.5f;
    float snareSnap = 0.6f;
    float hatTune = 0.7f;
    float hatDecay = 0.05f;
    float clapDecay = 0.15f;
    float cowbellTune = 0.8f;
    float cowbellDecay = 0.24f; // FIX Bug#10: was hardcoded 0.060+0.180 in triggerCowbell
    float tomTune = 0.55f;
    float tomDecay = 0.31f; // v909: matches new kParamDrSidTomDecay default (0.48 norm) — longer 808 tom
    float masterVolume = 0.8f;
    float performancePitchBendSemis = 0.0f;
    float performanceModWheel = 0.0f;
    float performancePressure = 0.0f;
    ArpSIDForensicConfig forensicConfig_{};
    std::array<uint8_t, 0x20> drsidRegImage_{};
    std::array<std::array<float, 256>, 3> drsidOscScope_{};
    std::array<std::array<float, 256>, 2> drsidFilterScope_{};
    uint32_t drsidScopeWritePos_ = 0;
    uint8_t drsidScopeActiveMask_ = 0;
    mutable ScopeTripleBuffer<DrSidScopeSnapshot> drsidScopeTriple_{};
    float preparedOverlaySample_ = 0.0f;
    float overlayDcBlockR_ = 0.0f;
    float overlayDcIn_ = 0.0f;
    float overlayDcOut_ = 0.0f;
    bool c64AuthenticDrumMode_ = true;
    float legacyDigitalOverlayAmount_ = 0.0f;
    DrSidMachineModel drumMachineModel_ = DrSidMachineModel::SidAuthentic;
    bool allowUnsupportedMidiFallback_ = false;
    ArpSID::SIDModel currentSidModel_ = ArpSID::SIDModel::MOS6581;
    float accentAmount_ = 0.68f;
    float outputDrive_ = 0.18f;
    float smoothedAccentAmount_ = 0.68f;
    float smoothedOutputDrive_ = 0.18f;
    float smoothedMasterVolume_ = 0.8f;
    float hatMetal_ = 0.62f;
    float clapSpread_ = 0.54f;
    bool cleanRestartRampEnabled_ = true;
    std::array<int, 3> cleanRestartRampRemaining_{{0,0,0}};
    std::array<int, 3> cleanRestartRampTotal_{{0,0,0}};
    std::array<float, 3> cleanRestartRampStart_{{0.0f,0.0f,0.0f}};
    std::array<float, 3> cleanRestartRampTarget_{{0.0f,0.0f,0.0f}};
    bool fractionalSamplePrepared_ = false;
    float preparedFractionalMasterVolume_ = 0.8f;
    int lastGMDrumNote_ = -1;
    SidGMDrumClass lastGMDrumClass_ = SidGMDrumClass::Unsupported;
    float lastGMDrumVelocity_ = 0.0f;
    std::uint16_t lastKitSelectedFactorySlot_ = 0u;
    bool lastKitHasSelectedFactorySlot_ = false;
    std::uint8_t lastKitVoiceWaveform_ = 0u;
    std::uint8_t lastKitVoiceAttackDecay_ = 0u;
    std::uint8_t lastKitVoiceSustainRelease_ = 0u;
    std::uint16_t lastKitVoicePulseWidth_ = 0u;
    std::uint8_t lastKitVoiceFlags_ = 0u;
    std::uint8_t lastKitVoiceOverrideMask_ = 0u;
    std::uint64_t kitTriggerCount_ = 0u;
    std::uint64_t kitVoiceOverrideAppliedCount_ = 0u;
    std::uint8_t lastKitAppliedVoiceIndex_ = 0u;
    std::uint8_t lastKitAppliedWaveformControl_ = 0u;
    std::uint8_t lastKitAppliedAttackDecay_ = 0u;
    std::uint8_t lastKitAppliedSustainRelease_ = 0u;
    std::uint16_t lastKitAppliedPulseWidth_ = 0u;
    std::uint8_t lastKitAppliedFlags_ = 0u;
    int activeTomGMNote_ = -1;


    float advanceSmoothedMasterVolumeForOneSample_() noexcept {
        const float target = std::clamp(ArpSID_sanitizeFloat(masterVolume), 0.0f, kDrSidMaxInternalBusGain);
        float current = std::clamp(ArpSID_sanitizeFloat(smoothedMasterVolume_, target), 0.0f, kDrSidMaxInternalBusGain);
        constexpr float kDrSidMasterVolumeFractionalSlew = 1.0f / 64.0f;
        const float delta = target - current;
        if (std::fabs(delta) <= kDrSidMasterVolumeFractionalSlew) current = target;
        else current += std::copysign(kDrSidMasterVolumeFractionalSlew, delta);
        smoothedMasterVolume_ = current;
        return current;
    }

    float performancePitchScale() const {
        return std::exp2f(std::clamp(performancePitchBendSemis, -24.0f, 24.0f) / 12.0f);
    }

    float performanceToneScale() const {
        const float mw = std::clamp(performanceModWheel, 0.0f, 1.0f);
        const float pr = std::clamp(performancePressure, 0.0f, 1.0f);
        return std::clamp(0.92f + 0.28f * mw + 0.12f * pr, 0.75f, 1.45f);
    }

    size_t midiIndex_(int midiNote) const noexcept {
        return static_cast<size_t>(std::clamp(midiNote, 0, 127));
    }

    size_t familyIndex_(DrumType type) const noexcept {
        // DrSID has three physical SID voice lanes, not one independent hold
        // ledger per visible GM drum label. Late DAW note-off events must not
        // choke a newer drum that already owns the same physical lane.
        switch (type) {
            case DrumType::Kick:
            case DrumType::Cowbell:
                return 0u;
            case DrumType::Snare:
            case DrumType::Clap:
            case DrumType::Tom:
                return 1u;
            case DrumType::ClosedHat:
            case DrumType::OpenHat:
            case DrumType::Rim:
                return 2u;
        }
        return 0u;
    }

    float compensateReferenceDecay_(float refPerSampleDecay) const noexcept {
        const double clamped = std::clamp(static_cast<double>(refPerSampleDecay), 1.0e-6, 0.9999999);
        const double safeSampleRate = sampleRate > 1.0 ? sampleRate : kReferenceSampleRate;
        const double exponent = kReferenceSampleRate / safeSampleRate;
        return static_cast<float>(std::exp(std::log(clamped) * exponent));
    }

    void softChokeOverlay_(OverlayVoice& ov) noexcept {
        if (!ov.active) return;
        const float fastFade = compensateReferenceDecay_(0.90f);
        ov.env = std::max(ov.env * 0.90f, 0.0f);
        ov.envB = std::max(ov.envB * 0.90f, 0.0f);
        ov.decay = std::min(std::clamp(ov.decay, 0.0f, 0.9999999f), fastFade);
        ov.decayB = std::min(std::clamp(ov.decayB, 0.0f, 0.9999999f), fastFade);
        if (ov.env < 1.0e-6f && ov.envB < 1.0e-6f) ov.active = false;
    }

    void holdMidiNote_(int midiNote) noexcept {
        const DrumType family = drumTypeForMidiNote(midiNote);
        const size_t noteIdx = midiIndex_(midiNote);
        const size_t familyIdx = familyIndex_(family);
        if (midiHeldCounts_[noteIdx] < 0xFFu) ++midiHeldCounts_[noteIdx];
        if (familyHeldCounts_[familyIdx] < 0xFFu) ++familyHeldCounts_[familyIdx];
        uint32_t nextGeneration = familyGeneration_[familyIdx] + 1u;
        if (nextGeneration == 0u) nextGeneration = 1u;
        familyGeneration_[familyIdx] = nextGeneration;
        midiHeldGeneration_[noteIdx] = nextGeneration;
    }

    bool shouldReleaseMidiNote_(int midiNote) noexcept {
        const DrumType family = drumTypeForMidiNote(midiNote);
        const size_t noteIdx = midiIndex_(midiNote);
        const size_t familyIdx = familyIndex_(family);
        uint8_t& noteHeld = midiHeldCounts_[noteIdx];
        if (noteHeld == 0u) return false;
        --noteHeld;
        uint8_t& familyHeld = familyHeldCounts_[familyIdx];
        if (familyHeld > 0u) --familyHeld;
        if (noteHeld > 0u) return false;
        const bool familyEmpty = familyHeld == 0u;
        // A visible GM drum label is not an independent SID voice. The physical
        // lane may already have been re-owned by another pad in the same lane
        // (closed hat -> open hat, snare -> clap/tom, kick -> cowbell). A late
        // note-off must not choke that lane while any same-lane note is held;
        // the drum envelope still decays naturally.
        midiHeldGeneration_[noteIdx] = 0u;
        return familyEmpty;
    }

    // FIX Bug#11: Use member clockFreq (PAL or NTSC) rather than always PAL_CLOCK_FREQ.
    uint16_t hzToSIDReg(float hz) const {
        const double f = std::max(0.0, (double)hz) * (double)performancePitchScale() * (double)performanceToneScale();
        const double reg = (f * 16777216.0) / clockFreq;
        return (uint16_t)std::clamp(reg, 0.0, 65535.0);
    }


    void retriggerVoice(int voiceIndex) {
        auto& voice = sidChip.getVoice(voiceIndex);
        voice.setGate(false);
        voice.setGate(true);
    }

    void setVoiceVelocity(int voiceIndex, float velocity) {
        if (voiceIndex < 0 || voiceIndex >= 3) return;
        const size_t i = static_cast<size_t>(voiceIndex);
        const float v = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        voiceVelocity[i] = v;
        if (cleanRestartRampRemaining_[i] > 0) cleanRestartRampTarget_[i] = v;
        else sidChip.setVoiceLevel(voiceIndex, v);
    }

    void scheduleRelease(int voiceIndex, float seconds) {
        if (voiceIndex < 0 || voiceIndex >= (int)voiceReleaseSamples.size()) return;
        const int samples = (int)std::lround(std::max(0.001f, seconds) * (float)sampleRate);
        voiceReleaseSamples[(size_t)voiceIndex] = std::max(1, samples);
    }

    void chokeVoiceNow(int voiceIndex) {
        if (voiceIndex < 0 || voiceIndex >= (int)voiceReleaseSamples.size()) return;
        voiceReleaseSamples[(size_t)voiceIndex] = 0;
        cleanRestartRampRemaining_[(size_t)voiceIndex] = 0;
        cleanRestartRampTotal_[(size_t)voiceIndex] = 0;
        cleanRestartRampStart_[(size_t)voiceIndex] = 0.0f;
        cleanRestartRampTarget_[(size_t)voiceIndex] = 0.0f;
        authenticPitchSweepActive_[(size_t)voiceIndex] = 0u;
        authenticPitchSweepCoeff_[(size_t)voiceIndex] = 0.0f;
        authenticFilterSweepActive_[(size_t)voiceIndex] = 0u;
        authenticFilterSweepCoeff_[(size_t)voiceIndex] = 0.0f;
        sidChip.getVoice(voiceIndex).setGate(false);
        drsidRegImage_[static_cast<size_t>(voiceIndex * 7 + 4)] &= static_cast<uint8_t>(~0x01u);
        voiceVelocity[(size_t)voiceIndex] *= 0.25f;
    }

    inline void chokeVoice0Family() { chokeVoiceNow(0); }
    inline void chokeVoice1Family() { chokeVoiceNow(1); }
    inline void chokeVoice2Family() { chokeVoiceNow(2); }

    bool analogNoteIsCymbalLike_(int midiNote) const noexcept {
        switch (midiNote) {
            case 49: case 51: case 52: case 55: case 57: case 59:
            case 71: case 72: case 74: case 81:
                return true;
            default:
                return false;
        }
    }

    bool analogNoteIsShakerLike_(int midiNote) const noexcept {
        switch (midiNote) {
            case 54: case 69: case 70: case 73:
                return true;
            default:
                return false;
        }
    }

    bool analogNoteIsBellLike_(int midiNote) const noexcept {
        switch (midiNote) {
            case 53: case 56: case 67: case 68: case 80:
                return true;
            default:
                return false;
        }
    }

    bool analogNoteIsWoodLike_(int midiNote) const noexcept {
        switch (midiNote) {
            case 58: case 75: case 76: case 77:
                return true;
            default:
                return false;
        }
    }

    float analogOverlayAccent_(float velocity) const noexcept {
        const float vel = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        const float accent = std::clamp(ArpSID_sanitizeFloat(smoothedAccentAmount_, accentAmount_), 0.0f, 1.0f);
        return std::clamp(0.80f + vel * (0.28f + 0.44f * accent), 0.60f, 1.48f);
    }

    float analogKickBaseHz_() const noexcept {
        // v587: base (target/final pitch) must track the SID-core freqReg formula in
        // triggerKick() so the overlay sine and SID core pitch are coherent at all
        // tune values. Old values (34+14 / 40+16) were 4× narrower than the SID
        // core range (35+58 / 42+74), creating up to a full-octave divergence at
        // high tune in AnalogX0X8 mode.
        if (drumMachineModel_ == DrSidMachineModel::AnalogX0X8) return 35.0f + kickTune * 58.0f;
        return 42.0f + kickTune * 74.0f;
    }

    float analogKickSweepHz_() const noexcept {
        // v587: sweepDelta = SID startReg Hz - SID freqReg Hz so the overlay's
        // initial pitch (base + sweepDelta) matches the SID core sweep-start at
        // every tune value.
        // AnalogX0X8: (156+tune*148) - (35+tune*58) = 121 + tune*90
        // SidAuthentic: (150+tune*162) - (42+tune*74) = 108 + tune*88
        if (drumMachineModel_ == DrSidMachineModel::AnalogX0X8) return 121.0f + kickTune * 90.0f;
        return 108.0f + kickTune * 88.0f;
    }

    float analogSnareToneHzA_(int midiNote) const noexcept {
        const bool electric = midiNote == 40;
        return (electric ? 206.0f : 174.0f) + snareTone * (electric ? 126.0f : 86.0f);
    }

    float analogSnareToneHzB_(int midiNote) const noexcept {
        const bool electric = midiNote == 40;
        return (electric ? 344.0f : 318.0f) + snareTone * (electric ? 172.0f : 124.0f);
    }

    float analogHatBaseHzForNote_(int midiNote) const noexcept {
        if (analogNoteIsCymbalLike_(midiNote)) return 4500.0f + hatTune * 1700.0f;
        if (analogNoteIsShakerLike_(midiNote)) return 2800.0f + hatTune * 1100.0f;
        return 3800.0f + hatTune * 1500.0f;
    }

    float analogCowbellFreqA_(int midiNote) const noexcept {
        switch (midiNote) {
            case 53: return 660.0f + cowbellTune * 210.0f;
            case 67: return 760.0f + cowbellTune * 240.0f;
            case 68: return 520.0f + cowbellTune * 180.0f;
            case 80: return 980.0f + cowbellTune * 260.0f;
            default: return 540.0f + cowbellTune * 200.0f;
        }
    }

    float analogCowbellFreqB_(int midiNote) const noexcept {
        const float a = analogCowbellFreqA_(midiNote);
        if (midiNote == 80) return a * 1.63f;
        if (midiNote == 67) return a * 1.45f;
        if (midiNote == 68) return a * 1.38f;
        return a * 1.48f;
    }

    float analogRimBaseHz_(int midiNote) const noexcept {
        switch (midiNote) {
            case 58: return 1080.0f;
            case 75: return 2240.0f;
            case 76: return 2580.0f;
            case 77: return 1760.0f;
            default: return 1900.0f;
        }
    }

    void registerDrumActivity_(DrumType type, float velocity, int midiNote) {
        const int drumIndex = static_cast<int>(type);
        const float safeVelocity = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        const int analogNote = sidIsGMDrumNote(static_cast<uint8_t>(std::clamp(midiNote, 0, 127)))
            ? midiNote
            : canonicalMidiNoteForDrumType(type);
        if (!c64AuthenticDrumMode_ && legacyDigitalOverlayAmount_ > 0.0f)
            startOverlay(type, safeVelocity * legacyDigitalOverlayAmount_, analogNote);
        drumEnvelopes[(size_t)drumIndex] = safeVelocity;
    }


    void triggerGMDrumSpec_(const SidGMDrumNoteSpec& spec, float rawVelocity) {
        const float vel = std::clamp((std::isfinite(rawVelocity) ? rawVelocity : 1.0f) * spec.velocityScale, 0.0f, 1.0f);
        if (spec.drumClass == SidGMDrumClass::Unsupported && !allowUnsupportedMidiFallback_) {
            return;
        }
        const DrumType type = drumTypeForGMClass(spec.drumClass);
        if (wavetableModeActive_() && triggerWavetableProgram_(spec, vel)) {
            if (sidIsGMDrumNote(spec.note))
                gmNoteLevels_[(size_t)spec.note] = std::max(gmNoteLevels_[(size_t)spec.note], vel);
            registerDrumActivity_(type, vel, spec.note);
            return;
        }
        switch (spec.drumClass) {
            case SidGMDrumClass::Kick:      triggerKick(vel); break;
            case SidGMDrumClass::Snare:     triggerSnare(vel); break;
            case SidGMDrumClass::ClosedHat: triggerClosedHat(vel, spec.tuneOffsetNorm, spec.decayScale); break;
            case SidGMDrumClass::OpenHat:   triggerOpenHat(vel, spec.tuneOffsetNorm, spec.decayScale); break;
            case SidGMDrumClass::Clap:      triggerClap(vel); break;
            case SidGMDrumClass::Cowbell:   triggerCowbell(vel, spec.tuneOffsetNorm, spec.decayScale); break;
            case SidGMDrumClass::Tom:       triggerTom(vel, spec.tuneOffsetNorm, spec.decayScale, spec.note); break;
            case SidGMDrumClass::Rim:       triggerRim(vel); break;
            default:
                if (allowUnsupportedMidiFallback_) {
                    const DrumType fallbackType = drumTypeForMidiNote(spec.note);
                    trigger(fallbackType, vel);
                    registerDrumActivity_(fallbackType, vel, canonicalMidiNoteForDrumType(fallbackType));
                }
                return;
        }
        if (sidIsGMDrumNote(spec.note))
            gmNoteLevels_[(size_t)spec.note] = std::max(gmNoteLevels_[(size_t)spec.note], vel);
        registerDrumActivity_(type, vel, spec.note);
    }

    void forceVoiceIdle_(int voiceIndex) {
        if (voiceIndex < 0 || voiceIndex >= (int)voiceReleaseSamples.size()) return;
        voiceReleaseSamples[(size_t)voiceIndex] = 0;
        authenticPitchSweepActive_[(size_t)voiceIndex] = 0u;
        authenticPitchSweepCoeff_[(size_t)voiceIndex] = 0.0f;
        authenticFilterSweepActive_[(size_t)voiceIndex] = 0u;
        authenticFilterSweepCoeff_[(size_t)voiceIndex] = 0.0f;
        voiceVelocity[(size_t)voiceIndex] = 0.0f;
        sidChip.getVoice(voiceIndex).forceIdle();
        const float start = std::clamp(ArpSID_sanitizeFloat(sidChip.getVoiceLevel(voiceIndex), 0.0f), 0.0f, 1.0f);
        if (start > 1.0e-5f) {
            const int rampSamples = cleanRestartRampSamples_();
            cleanRestartRampRemaining_[(size_t)voiceIndex] = rampSamples;
            cleanRestartRampTotal_[(size_t)voiceIndex] = rampSamples;
            cleanRestartRampStart_[(size_t)voiceIndex] = start;
            cleanRestartRampTarget_[(size_t)voiceIndex] = 0.0f;
        } else {
            sidChip.setVoiceLevel(voiceIndex, 0.0f);
        }
    }

    void dampVoiceRelease_(int voiceIndex, float seconds) {
        if (voiceIndex < 0 || voiceIndex >= (int)voiceReleaseSamples.size()) return;
        if (voiceVelocity[(size_t)voiceIndex] > 0.0f) {
            voiceVelocity[(size_t)voiceIndex] *= 0.25f;
            sidChip.setVoiceLevel(voiceIndex, voiceVelocity[(size_t)voiceIndex]);
        }
        scheduleRelease(voiceIndex, seconds);
    }

    void chokeFamily0Drums_() {
        chokeVoice0Family();
        drumEnvelopes[(size_t)DrumType::Kick] = 0.0f;
        drumEnvelopes[(size_t)DrumType::Cowbell] = 0.0f;
        clearGMDrumClassLevels(SidGMDrumClass::Kick);
        clearGMDrumClassLevels(SidGMDrumClass::Cowbell);
        for (auto& ov : overlays) {
            if (!ov.active) continue;
            if (ov.type == DrumType::Kick || ov.type == DrumType::Cowbell)
                softChokeOverlay_(ov);
        }
    }

    void dampFamily0Drums_(float envScale, float releaseSeconds) {
        dampVoiceRelease_(0, releaseSeconds);
        drumEnvelopes[(size_t)DrumType::Kick] *= envScale;
        drumEnvelopes[(size_t)DrumType::Cowbell] *= envScale;
        dampGMDrumClassLevels(SidGMDrumClass::Kick, envScale);
        dampGMDrumClassLevels(SidGMDrumClass::Cowbell, envScale);
        for (auto& ov : overlays) {
            if (!ov.active) continue;
            if (ov.type == DrumType::Kick || ov.type == DrumType::Cowbell) {
                ov.env *= envScale;
                if (ov.env < 1.0e-4f) ov.active = false;
            }
        }
    }

    void chokeTomDrum_() {
        forceVoiceIdle_(1);
        drumEnvelopes[(size_t)DrumType::Tom] = 0.0f;
        clearGMDrumClassLevels(SidGMDrumClass::Tom);
        activeTomGMNote_ = -1;
        for (auto& ov : overlays) {
            if (ov.active && ov.type == DrumType::Tom) softChokeOverlay_(ov);
        }
    }

    void dampTomDrum_(float envScale, float releaseSeconds) {
        dampVoiceRelease_(1, releaseSeconds);
        drumEnvelopes[(size_t)DrumType::Tom] *= envScale;
        dampGMDrumClassLevels(SidGMDrumClass::Tom, envScale);
        for (auto& ov : overlays) {
            if (ov.active && ov.type == DrumType::Tom) {
                ov.env *= envScale;
                if (ov.env < 1.0e-4f) ov.active = false;
            }
        }
    }

    void chokeFamily1Drums_() {
        // Voice 1 is the shared tonal/noise SID lane for Snare, Clap and Tom.
        // A new Snare/Clap physically steals the same SID oscillator from Tom,
        // so the telemetry/overlay mirror must be stolen too. This keeps Tom
        // independent from Kick/Cowbell voice 0 while avoiding a stale Tom LED
        // after the audible voice has already become snare/clap.
        chokeVoice1Family();
        drumEnvelopes[(size_t)DrumType::Snare] = 0.0f;
        drumEnvelopes[(size_t)DrumType::Clap] = 0.0f;
        drumEnvelopes[(size_t)DrumType::Tom] = 0.0f;
        clearGMDrumClassLevels(SidGMDrumClass::Snare);
        clearGMDrumClassLevels(SidGMDrumClass::Clap);
        clearGMDrumClassLevels(SidGMDrumClass::Tom);
        activeTomGMNote_ = -1;
        for (auto& ov : overlays) {
            if (!ov.active) continue;
            if (ov.type == DrumType::Snare || ov.type == DrumType::Clap || ov.type == DrumType::Tom)
                softChokeOverlay_(ov);
        }
    }

    void dampFamily1Drums_(float envScale, float releaseSeconds) {
        // Damping the voice-1 family must damp Tom too; the physical SID voice
        // cannot sustain an old Tom while Snare/Clap owns the same oscillator.
        dampVoiceRelease_(1, releaseSeconds);
        drumEnvelopes[(size_t)DrumType::Snare] *= envScale;
        drumEnvelopes[(size_t)DrumType::Clap] *= envScale;
        drumEnvelopes[(size_t)DrumType::Tom] *= envScale;
        dampGMDrumClassLevels(SidGMDrumClass::Snare, envScale);
        dampGMDrumClassLevels(SidGMDrumClass::Clap, envScale);
        dampGMDrumClassLevels(SidGMDrumClass::Tom, envScale);
        for (auto& ov : overlays) {
            if (!ov.active) continue;
            if (ov.type == DrumType::Snare || ov.type == DrumType::Clap || ov.type == DrumType::Tom) {
                ov.env *= envScale;
                if (ov.env < 1.0e-4f) ov.active = false;
            }
        }
    }

    void chokeHatDrums_() {
        drumEnvelopes[(size_t)DrumType::ClosedHat] = 0.0f;
        drumEnvelopes[(size_t)DrumType::OpenHat] = 0.0f;
        clearGMDrumClassLevels(SidGMDrumClass::ClosedHat);
        clearGMDrumClassLevels(SidGMDrumClass::OpenHat);
        for (auto& ov : overlays) {
            if (!ov.active) continue;
            if (ov.type == DrumType::ClosedHat || ov.type == DrumType::OpenHat)
                softChokeOverlay_(ov);
        }
    }

    void chokeFamily2Drums_() {
        chokeVoice2Family();
        drumEnvelopes[(size_t)DrumType::ClosedHat] = 0.0f;
        drumEnvelopes[(size_t)DrumType::OpenHat] = 0.0f;
        drumEnvelopes[(size_t)DrumType::Rim] = 0.0f;
        clearGMDrumClassLevels(SidGMDrumClass::ClosedHat);
        clearGMDrumClassLevels(SidGMDrumClass::OpenHat);
        clearGMDrumClassLevels(SidGMDrumClass::Rim);
        for (auto& ov : overlays) {
            if (!ov.active) continue;
            if (ov.type == DrumType::ClosedHat || ov.type == DrumType::OpenHat || ov.type == DrumType::Rim)
                softChokeOverlay_(ov);
        }
    }

    void startAuthenticPitchSweep_(int voiceIndex, uint16_t startReg, uint16_t targetReg, float halfLifeMs) noexcept {
        if (voiceIndex < 0 || voiceIndex >= (int)voiceReleaseSamples.size()) return;
        const double sr = sampleRate > 1.0 ? sampleRate : kReferenceSampleRate;
        const double safeHalfLifeSamples = std::max(1.0, static_cast<double>(halfLifeMs) * 0.001 * sr);
        const float coeff = static_cast<float>(1.0 - std::exp(std::log(0.5) / safeHalfLifeSamples));
        const size_t i = static_cast<size_t>(voiceIndex);
        authenticPitchSweepFreqReg_[i] = static_cast<float>(startReg);
        authenticPitchSweepTargetReg_[i] = static_cast<float>(targetReg);
        authenticPitchSweepCoeff_[i] = std::clamp(coeff, 0.00001f, 1.0f);
        authenticPitchSweepActive_[i] = (startReg != targetReg) ? 1u : 0u;
        sidChip.getVoice(voiceIndex).setFrequency(startReg);
        const size_t base = i * 7u;
        drsidRegImage_[base + 0u] = static_cast<uint8_t>(startReg & 0xFFu);
        drsidRegImage_[base + 1u] = static_cast<uint8_t>((startReg >> 8u) & 0xFFu);
    }

    void startAuthenticFilterSweep_(int lane,
                                    uint16_t startCutoff,
                                    uint16_t targetCutoff,
                                    float halfLifeMs,
                                    uint8_t resonance,
                                    FilterMode mode,
                                    bool routeV1,
                                    bool routeV2,
                                    bool routeV3) noexcept {
        if (lane < 0 || lane >= 3) return;
        const double sr = sampleRate > 1.0 ? sampleRate : kReferenceSampleRate;
        const double safeHalfLifeSamples = std::max(1.0, static_cast<double>(halfLifeMs) * 0.001 * sr);
        const float coeff = static_cast<float>(1.0 - std::exp(std::log(0.5) / safeHalfLifeSamples));
        const size_t i = static_cast<size_t>(lane);
        authenticFilterSweepCutoff_[i] = static_cast<float>(startCutoff & 0x07FFu);
        authenticFilterSweepTarget_[i] = static_cast<float>(targetCutoff & 0x07FFu);
        authenticFilterSweepCoeff_[i] = std::clamp(coeff, 0.00001f, 1.0f);
        authenticFilterSweepActive_[i] = ((startCutoff & 0x07FFu) != (targetCutoff & 0x07FFu)) ? 1u : 0u;
        authenticFilterSweepMode_ = mode;
        authenticFilterSweepResonance_ = static_cast<uint8_t>(resonance & 0x0Fu);
        authenticFilterSweepRouteMask_ = static_cast<uint8_t>((routeV1 ? 0x01u : 0u) | (routeV2 ? 0x02u : 0u) | (routeV3 ? 0x04u : 0u));
        applyDrSidFilter_(startCutoff, resonance, mode, routeV1, routeV2, routeV3);
    }

    void tickAuthenticFilterSweeps_() noexcept {
        int selected = -1;
        for (int lane = 0; lane < 3; ++lane) {
            if (authenticFilterSweepActive_[static_cast<size_t>(lane)]) selected = lane;
        }
        if (selected < 0) return;
        const size_t i = static_cast<size_t>(selected);
        float cur = authenticFilterSweepCutoff_[i];
        const float target = authenticFilterSweepTarget_[i];
        cur += (target - cur) * authenticFilterSweepCoeff_[i];
        if (!std::isfinite(cur)) cur = target;
        if (std::fabs(cur - target) < 1.0f) {
            cur = target;
            authenticFilterSweepActive_[i] = 0u;
        }
        authenticFilterSweepCutoff_[i] = cur;
        const uint16_t cutoff = static_cast<uint16_t>(std::clamp(std::lround(cur), 0l, 2047l));
        applyDrSidFilter_(cutoff,
                          authenticFilterSweepResonance_,
                          authenticFilterSweepMode_,
                          (authenticFilterSweepRouteMask_ & 0x01u) != 0u,
                          (authenticFilterSweepRouteMask_ & 0x02u) != 0u,
                          (authenticFilterSweepRouteMask_ & 0x04u) != 0u);
    }

    void tickAuthenticPitchSweeps_() noexcept {
        for (int voiceIndex = 0; voiceIndex < 3; ++voiceIndex) {
            const size_t i = static_cast<size_t>(voiceIndex);
            if (!authenticPitchSweepActive_[i]) continue;
            float cur = authenticPitchSweepFreqReg_[i];
            const float target = authenticPitchSweepTargetReg_[i];
            cur += (target - cur) * authenticPitchSweepCoeff_[i];
            if (!std::isfinite(cur)) cur = target;
            if (std::fabs(cur - target) < 1.0f) {
                cur = target;
                authenticPitchSweepActive_[i] = 0u;
            }
            authenticPitchSweepFreqReg_[i] = cur;
            const uint16_t reg = static_cast<uint16_t>(std::clamp(std::lround(cur), 0l, 65535l));
            sidChip.getVoice(voiceIndex).setFrequency(reg);
            const size_t base = i * 7u;
            drsidRegImage_[base + 0u] = static_cast<uint8_t>(reg & 0xFFu);
            drsidRegImage_[base + 1u] = static_cast<uint8_t>((reg >> 8u) & 0xFFu);
        }
    }

    void tickVoiceReleases() {
        tickAuthenticPitchSweeps_();
        tickAuthenticFilterSweeps_();
        for (int i = 0; i < (int)voiceReleaseSamples.size(); ++i) {
            int& remain = voiceReleaseSamples[(size_t)i];
            if (remain <= 0) continue;
            --remain;
            if (remain == 0) {
                authenticPitchSweepActive_[(size_t)i] = 0u;
                authenticPitchSweepCoeff_[(size_t)i] = 0.0f;
                authenticFilterSweepActive_[(size_t)i] = 0u;
                authenticFilterSweepCoeff_[(size_t)i] = 0.0f;
                sidChip.getVoice(i).setGate(false);
                drsidRegImage_[static_cast<size_t>(i * 7 + 4)] &= static_cast<uint8_t>(~0x01u);
            }
        }
    }

    static inline float overlayNoise(uint32_t& s) {
        s ^= (s << 13);
        s ^= (s >> 17);
        s ^= (s << 5);
        return ((s & 0x00FFFFFFu) * (1.0f / 8388608.0f)) - 1.0f;
    }

    void startOverlay(DrumType type, float velocity, int midiNote) {
        OverlayVoice* best = nullptr;
        for (auto& ov : overlays) {
            if (!ov.active) { best = &ov; break; }
            if (!best || ov.env < best->env) best = &ov;
        }
        if (!best) return;
        *best = OverlayVoice{};
        best->active = true;
        best->type = type;
        best->midiNote = std::clamp(midiNote, 0, 127);
        best->velocity = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        best->accent = analogOverlayAccent_(best->velocity);
        const float overlayLevel = overlayLevelScaleForType_(type);
        switch (type) {
            case DrumType::Kick:
                best->env = (drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.27f : 0.24f) * best->velocity * overlayLevel;
                best->envB = 1.0f;
                best->decay = compensateReferenceDecay_((drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.99930f : 0.9992f) - 0.00014f * kickDecay);
                best->decayB = compensateReferenceDecay_((drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.99610f : 0.9958f) - 0.00022f * kickDecay);
                best->freq = analogKickBaseHz_();
                best->freqB = analogKickSweepHz_();
                break;
            case DrumType::Snare:
                best->env = 0.11f * best->velocity * overlayLevel;
                best->envB = 0.20f + best->velocity * 0.12f;
                best->decay = compensateReferenceDecay_(0.9988f - 0.00010f * snareSnap);
                best->decayB = compensateReferenceDecay_(0.9980f - 0.00012f * snareSnap);
                best->freq = analogSnareToneHzA_(best->midiNote);
                best->freqB = analogSnareToneHzB_(best->midiNote);
                break;
            case DrumType::ClosedHat:
                best->env = 0.085f * best->velocity * overlayLevel;
                best->decay = compensateReferenceDecay_(0.9962f - 0.00008f * hatDecay);
                best->freq = analogHatBaseHzForNote_(best->midiNote);
                best->freqB = best->freq * (1.38f + 0.22f * hatMetal_);
                break;
            case DrumType::OpenHat:
                best->env = 0.082f * best->velocity * overlayLevel;
                best->decay = compensateReferenceDecay_(0.9989f - 0.00005f * hatDecay);
                best->freq = analogHatBaseHzForNote_(best->midiNote);
                best->freqB = best->freq * (1.42f + 0.24f * hatMetal_);
                break;
            case DrumType::Clap:
                best->env = 0.11f * best->velocity * overlayLevel;
                best->envB = 0.14f + best->velocity * 0.10f;
                best->decay = compensateReferenceDecay_(0.9960f - 0.00012f * clapDecay);
                best->decayB = compensateReferenceDecay_(0.9986f - 0.00008f * clapDecay);
                best->freq = 1450.0f + clapSpread_ * 850.0f;
                best->burstStage = 0;
                best->burstCountdown = 0;
                best->burstIntervalSamples = std::max(2, static_cast<int>(std::lround((0.006 + clapSpread_ * 0.010) * std::max(1.0, sampleRate))));
                break;
            case DrumType::Cowbell:
                best->env = 0.082f * best->velocity * overlayLevel;
                best->decay = compensateReferenceDecay_(0.9989f - 0.00010f * std::clamp((cowbellDecay - 0.04f) / 0.56f, 0.0f, 1.0f));
                best->freq = analogCowbellFreqA_(best->midiNote);
                best->freqB = analogCowbellFreqB_(best->midiNote);
                break;
            case DrumType::Tom:
                best->env = 0.13f * best->velocity * overlayLevel;
                best->envB = 1.0f;
                best->decay = compensateReferenceDecay_(0.9992f - 0.00012f * std::clamp((tomDecay - 0.04f) / 0.56f, 0.0f, 1.0f));
                best->decayB = compensateReferenceDecay_(0.9964f - 0.00020f * std::clamp((tomDecay - 0.04f) / 0.56f, 0.0f, 1.0f));
                best->freq = tomBaseHzForGMNote_(best->midiNote, tomTune);
                best->freqB = best->freq * (1.28f + 0.24f * (1.0f - tomTune));
                break;
            case DrumType::Rim:
                best->env = 0.07f * best->velocity * overlayLevel;
                best->envB = analogNoteIsWoodLike_(best->midiNote) ? 0.0f : 0.05f;
                best->decay = compensateReferenceDecay_(analogNoteIsWoodLike_(best->midiNote) ? 0.9972f : 0.9982f);
                best->decayB = compensateReferenceDecay_(0.9962f);
                best->freq = analogRimBaseHz_(best->midiNote);
                best->freqB = best->freq * (best->midiNote == 75 ? 1.34f : 1.18f);
                break;
        }
        const float sr = static_cast<float>(std::max(1.0, sampleRate));
        constexpr float kTwoPi = 2.0f * 3.14159265358979323846f;
        best->phaseInc = kTwoPi * std::clamp(best->freq, 0.0f, sr * 0.49f) / sr;
        best->phaseIncB = kTwoPi * std::clamp(best->freqB, 0.0f, sr * 0.49f) / sr;
    }

    float renderOverlaySample() {
        if (c64AuthenticDrumMode_ || legacyDigitalOverlayAmount_ <= 0.0f) return 0.0f;
        float mix = 0.0f;
        constexpr float kTwoPi = 2.0f * 3.14159265358979323846f;
        const float safeSampleRate = std::max(1.0, sampleRate);
        const auto metallicBank = [&](float phaseA, float phaseB, float rootHz) noexcept {
            const float ratio = phaseB / std::max(1.0e-6f, phaseA);
            const float sq1 = std::sin(phaseA) >= 0.0f ? 1.0f : -1.0f;
            const float sq2 = std::sin(phaseB) >= 0.0f ? 1.0f : -1.0f;
            const float sq3 = std::sin(phaseA * 1.997f) >= 0.0f ? 1.0f : -1.0f;
            const float sq4 = std::sin(phaseB * 1.417f) >= 0.0f ? 1.0f : -1.0f;
            const float sq5 = std::sin(phaseA * 2.619f) >= 0.0f ? 1.0f : -1.0f;
            const float sq6 = std::sin(phaseA * (1.0f + 0.12f * ratio) + rootHz * 1.0e-4f) >= 0.0f ? 1.0f : -1.0f;
            return (sq1 + sq2 + 0.8f * sq3 + 0.7f * sq4 + 0.55f * sq5 + 0.45f * sq6) * (1.0f / 4.5f);
        };
        for (auto& ov : overlays) {
            if (!ov.active) continue;
            if (!std::isfinite(ov.env) || !std::isfinite(ov.decay) ||
                !std::isfinite(ov.freq) || !std::isfinite(ov.freqB) ||
                !std::isfinite(ov.phase) || !std::isfinite(ov.phaseB) ||
                !std::isfinite(ov.envB) || !std::isfinite(ov.decayB) ||
                !std::isfinite(ov.stateA) || !std::isfinite(ov.stateB)) {
                ov = OverlayVoice{};
                continue;
            }
            ov.env = std::clamp(ov.env, 0.0f, 1.0f);
            ov.envB = std::clamp(ov.envB, 0.0f, 1.0f);
            ov.decay = std::clamp(ov.decay, 0.0f, 0.9999999f);
            ov.decayB = std::clamp(ov.decayB, 0.0f, 0.9999999f);
            ov.freq = std::clamp(ov.freq, 0.0f, safeSampleRate * 0.49f);
            ov.freqB = std::clamp(ov.freqB, 0.0f, safeSampleRate * 0.49f);
            if (ov.phase >= kTwoPi || ov.phase <= -kTwoPi) {
                ov.phase = std::fmod(ov.phase, kTwoPi);
            }
            if (!std::isfinite(ov.phase)) ov.phase = 0.0f;
            if (ov.phase < 0.0f) ov.phase += kTwoPi;
            if (ov.phaseB >= kTwoPi || ov.phaseB <= -kTwoPi) {
                ov.phaseB = std::fmod(ov.phaseB, kTwoPi);
            }
            if (!std::isfinite(ov.phaseB)) ov.phaseB = 0.0f;
            if (ov.phaseB < 0.0f) ov.phaseB += kTwoPi;
            const float n = overlayNoise(ov.noise);
            float s = 0.0f;
            switch (ov.type) {
                case DrumType::Kick: {
                    const float pitchEnv = std::clamp(ov.envB, 0.0f, 1.0f);
                    const float freq = std::clamp(ov.freq + pitchEnv * ov.freqB, 20.0f, safeSampleRate * 0.45f);
                    const float inc = kTwoPi * freq / safeSampleRate;
                    ov.phase += inc;
                    if (ov.phase >= kTwoPi) ov.phase -= kTwoPi;
                    const float click = pitchEnv > 0.70f ? (pitchEnv - 0.70f) * (0.24f + 0.22f * smoothedOutputDrive_) * ov.accent : 0.0f;
                    s = std::sin(ov.phase) * (0.76f + 0.24f * ov.accent) + click;
                    ov.envB *= ov.decayB;
                    break;
                }
                case DrumType::Snare: {
                    ov.phase += ov.phaseInc;
                    ov.phaseB += ov.phaseIncB;
                    if (ov.phase >= kTwoPi) ov.phase -= kTwoPi;
                    if (ov.phaseB >= kTwoPi) ov.phaseB -= kTwoPi;
                    ov.stateA += (0.18f + 0.18f * snareTone) * ((0.76f * n + 0.22f * ov.stateA) - ov.stateA);
                    const float tone = 0.58f * std::sin(ov.phase) + 0.42f * std::sin(ov.phaseB);
                    s = tone * ov.env + ov.stateA * (0.55f + snareSnap * 0.45f) * ov.envB;
                    ov.envB *= ov.decayB;
                    break;
                }
                case DrumType::ClosedHat:
                case DrumType::OpenHat: {
                    ov.phase += ov.phaseInc;
                    ov.phaseB += ov.phaseIncB;
                    if (ov.phase >= kTwoPi) ov.phase -= kTwoPi;
                    if (ov.phaseB >= kTwoPi) ov.phaseB -= kTwoPi;
                    const bool cymbalLike = analogNoteIsCymbalLike_(ov.midiNote);
                    const bool shakerLike = analogNoteIsShakerLike_(ov.midiNote);
                    const float metalMix = shakerLike ? (0.08f + 0.28f * hatMetal_) : (0.34f + 0.46f * hatMetal_);
                    const float metal = metallicBank(ov.phase, ov.phaseB, ov.freq);
                    const float src = metal * metalMix + n * (1.10f - metalMix * (cymbalLike ? 0.72f : 0.54f));
                    ov.stateA += (0.18f + 0.14f * hatMetal_) * (src - ov.stateA);
                    const float hp = src - ov.stateA;
                    ov.stateB += (0.12f + 0.10f * (cymbalLike ? 1.0f : 0.0f)) * (hp - ov.stateB);
                    s = (hp * 0.68f + ov.stateB * 0.52f) * (0.92f + 0.10f * ov.accent);
                    break;
                }
                case DrumType::Clap: {
                    const float inc = kTwoPi * ov.freq / safeSampleRate;
                    ov.phase += inc;
                    if (ov.phase >= kTwoPi) ov.phase -= kTwoPi;
                    if (ov.burstCountdown <= 0 && ov.burstStage < 3) {
                        ov.burstCountdown = ov.burstIntervalSamples;
                        ++ov.burstStage;
                    }
                    const float burstGate = ov.burstCountdown > (ov.burstIntervalSamples / 2) ? 1.0f : 0.0f;
                    if (ov.burstCountdown > 0) --ov.burstCountdown;
                    ov.stateA += (0.22f + 0.10f * clapSpread_) * (n - ov.stateA);
                    const float bp = ov.stateA - ov.stateB;
                    ov.stateB += (0.10f + 0.12f * clapSpread_) * bp;
                    s = bp * (burstGate * ov.env + ov.envB * 0.65f);
                    ov.envB *= ov.decayB;
                    break;
                }
                case DrumType::Cowbell: {
                    ov.phase += ov.phaseInc;
                    ov.phaseB += ov.phaseIncB;
                    if (ov.phase >= kTwoPi) ov.phase -= kTwoPi;
                    if (ov.phaseB >= kTwoPi) ov.phaseB -= kTwoPi;
                    const float a = std::sin(ov.phase) > 0.0f ? 1.0f : -1.0f;
                    const float b = std::sin(ov.phaseB) > 0.0f ? 1.0f : -1.0f;
                    ov.stateA += 0.18f * ((a + b) * 0.5f - ov.stateA);
                    const float bellLift = analogNoteIsBellLike_(ov.midiNote) ? 0.24f : 0.0f;
                    s = (ov.stateA + bellLift * std::sin(ov.phase * 0.5f)) * (0.84f + 0.14f * ov.accent);
                    break;
                }
                case DrumType::Tom: {
                    const float pitchEnv = std::clamp(ov.envB, 0.0f, 1.0f);
                    const float freq = std::clamp(ov.freq + pitchEnv * (ov.freqB - ov.freq), 30.0f, safeSampleRate * 0.40f);
                    const float inc = kTwoPi * freq / safeSampleRate;
                    ov.phase += inc;
                    if (ov.phase >= kTwoPi) ov.phase -= kTwoPi;
                    s = std::sin(ov.phase) * (0.84f + 0.12f * ov.accent);
                    ov.envB *= ov.decayB;
                    break;
                }
                case DrumType::Rim: {
                    ov.phase += ov.phaseInc;
                    ov.phaseB += ov.phaseIncB;
                    if (ov.phase >= kTwoPi) ov.phase -= kTwoPi;
                    if (ov.phaseB >= kTwoPi) ov.phaseB -= kTwoPi;
                    const bool woodLike = analogNoteIsWoodLike_(ov.midiNote);
                    ov.stateA += (woodLike ? 0.28f : 0.18f) * ((0.70f * std::sin(ov.phase) + 0.30f * std::sin(ov.phaseB)) - ov.stateA);
                    s = woodLike ? ov.stateA : (0.68f * ov.stateA + 0.22f * n * (0.4f + ov.envB));
                    ov.envB *= ov.decayB;
                    break;
                }
            }
            const int drumIndex = std::clamp(static_cast<int>(ov.type), 0, kDrumTypeCount - 1);
            const float coreActivity = std::clamp(drumEnvelopes[(size_t)drumIndex], 0.0f, 1.0f);
            const float overlayAttenuation = std::clamp(1.0f - sidCorePriorityForType_(ov.type) * coreActivity, 0.12f, 1.0f);
            mix += ArpSID_sanitizeFloat(s * ov.env * ov.accent * overlayAttenuation);
            ov.env *= ov.decay;
            if ((!std::isfinite(ov.env) && !std::isfinite(ov.envB)) ||
                ((ov.env < 1.0e-4f) && (ov.envB < 1.0e-4f)))
                ov.active = false;
        }
        float dcBlocked = mix - overlayDcIn_ + overlayDcBlockR_ * overlayDcOut_;
        overlayDcIn_ = ArpSID_sanitizeFloat(mix);
        overlayDcOut_ = ArpSID_sanitizeFloat(dcBlocked);
        if (std::fabs(overlayDcIn_) < 1.0e-12f) overlayDcIn_ = 0.0f;
        if (std::fabs(overlayDcOut_) < 1.0e-12f) overlayDcOut_ = 0.0f;
        return std::clamp(ArpSID_sanitizeFloat(overlayDcOut_), -0.42f, 0.42f);
    }

    float overlayLevelScaleForType_(DrumType type) const noexcept {
        switch (type) {
            case DrumType::Kick:      return drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.66f : 0.24f;
            case DrumType::Snare:     return drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.52f : 0.34f;
            case DrumType::Tom:       return drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.38f : 0.22f;
            case DrumType::Cowbell:   return drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.46f : 0.36f;
            case DrumType::ClosedHat: return drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.68f : 0.82f;
            case DrumType::OpenHat:   return drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.72f : 0.88f;
            case DrumType::Clap:      return drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.68f : 0.74f;
            case DrumType::Rim:       return drumMachineModel_ == DrSidMachineModel::AnalogX0X8 ? 0.50f : 0.68f;
        }
        return 0.5f;
    }

    float sidCorePriorityForType_(DrumType type) const noexcept {
        switch (type) {
            case DrumType::Kick:      return 0.74f;
            case DrumType::Snare:     return 0.66f;
            case DrumType::Tom:       return 0.78f;
            case DrumType::Cowbell:   return 0.58f;
            case DrumType::ClosedHat: return 0.18f;
            case DrumType::OpenHat:   return 0.14f;
            case DrumType::Clap:      return 0.28f;
            case DrumType::Rim:       return 0.24f;
        }
        return 0.4f;
    }

// Kick drum — pure SID triangle/pulse transient with low-pass body.

    void triggerKick(float velocity) {
        const float vel = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        const bool analog808 = drumMachineModel_ == DrSidMachineModel::AnalogX0X8;
        chokeFamily0Drums_();
        setVoiceVelocity(0, (analog808 ? 0.30f : 0.26f) + (analog808 ? 0.70f : 0.74f) * vel);
        const float tune = std::clamp(kickTune, 0.0f, 1.0f);
        const uint16_t freqReg = hzToSIDReg((analog808 ? 35.0f : 42.0f) + tune * (analog808 ? 58.0f : 74.0f));
        const uint16_t startReg = hzToSIDReg((analog808 ? 156.0f : 150.0f) + tune * (analog808 ? 148.0f : 162.0f));
        const uint8_t decay = static_cast<uint8_t>(std::clamp(5.0f + kickDecay * 9.0f, 1.0f, 15.0f));
        const bool analog808On6581 = analog808 && currentSidModel_ == ArpSID::SIDModel::MOS6581;
        const uint8_t kickResonance = analog808On6581 ? 13u : (analog808 ? 11u : 10u);
        const Waveform kickWaveform = analog808On6581 ? Waveform::Triangle
                                                      : (analog808 ? Waveform::TriPulse : Waveform::Triangle);
        const uint16_t kickPulseWidth = analog808On6581 ? 0x0800u : (analog808 ? 0x0700u : 0x0800u);
        startAuthenticFilterSweep_(0,
                                   analog808 ? 0x620u : 0x700u,
                                   analog808 ? 0x260u : 0x320u,
                                   (analog808 ? 28.0f : 20.0f) + kickDecay * (analog808 ? 30.0f : 20.0f),
                                   kickResonance,
                                   FilterMode::LowPass,
                                   true,
                                   false,
                                   false);
        applyDrSidVoice_(0, freqReg, kickWaveform, kickPulseWidth, 0u, decay, 0u, analog808 ? 4u : 3u);
        startAuthenticPitchSweep_(0, startReg, freqReg, (analog808 ? 18.0f : 12.0f) + kickDecay * (analog808 ? 36.0f : 28.0f));
        hardRetriggerVoice_(0);
        scheduleRelease(0, ((analog808 ? 0.074f : 0.052f) + kickDecay * (analog808 ? 0.430f : 0.380f)) * (0.86f + 0.29f * vel));
    }

    // Snare — SID noise through BP/HP with short 6581 ADSR transient.
    // 6581 vs 8580: the 6581 filter rolls off harder and has more even-harmonic
    // colour, so the snare needs a slightly brighter centre frequency and a
    // higher resonance to keep the rim/snare-wire snap audible. 8580 already
    // has the bright, clean bandwidth required and uses the lighter setting.
    void triggerSnare(float velocity) {
        const float vel = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        const bool on6581 = currentSidModel_ == ArpSID::SIDModel::MOS6581;
        chokeFamily1Drums_();
        setVoiceVelocity(1, 0.18f + 0.82f * vel);
        const float toneBase = on6581 ? 1600.0f : 1450.0f;
        const float toneRange = on6581 ? 2900.0f : 2600.0f;
        const uint16_t freqReg = hzToSIDReg(toneBase + snareTone * toneRange);
        const uint8_t decay = static_cast<uint8_t>(std::clamp(4.0f + snareSnap * 10.0f, 1.0f, 15.0f));
        const uint8_t snareResonance = on6581 ? 14u : 12u;
        startAuthenticFilterSweep_(1, on6581 ? 0x7A0u : 0x760u, on6581 ? 0x560u : 0x520u,
                                   16.0f + snareSnap * 18.0f, snareResonance,
                                   FilterMode::BpHp, false, true, false);
        applyDrSidVoice_(1, freqReg, Waveform::Noise, 0x0800u, 0u, decay, 0u, 4u);
        hardRetriggerVoice_(1);
        scheduleRelease(1, (0.030f + snareSnap * 0.220f) * (0.85f + 0.30f * vel));
    }

    // Closed hi-hat — SID noise, high-pass, very short release.
    // 6581 vs 8580: 6581 highs are softer, so the closed hat uses a higher
    // cutoff frequency and a slightly higher resonance to recover the metallic
    // chick. 8580 keeps the lighter, brighter base path it has always had.
    void triggerClosedHat(float velocity, float tuneOffsetNorm = 0.0f, float decayScale = 1.0f) {
        const float vel = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        const float tune = std::clamp(hatTune + tuneOffsetNorm, 0.0f, 1.0f);
        const float scale = std::clamp(std::isfinite(decayScale) ? decayScale : 1.0f, 0.45f, 2.25f);
        const bool on6581 = currentSidModel_ == ArpSID::SIDModel::MOS6581;
        chokeHatDrums_();
        setVoiceVelocity(2, 0.12f + 0.88f * vel);
        const float hatBase = on6581 ? 5700.0f : 5300.0f;
        const float hatRange = on6581 ? 4200.0f : 3900.0f;
        const uint16_t freqReg = hzToSIDReg(hatBase + tune * hatRange);
        const uint8_t hatResonance = on6581 ? 10u : 8u;
        startAuthenticFilterSweep_(2,
                                   on6581 ? 0x7E0u : 0x7C0u,
                                   on6581 ? 0x6C0u : 0x680u,
                                   8.0f, hatResonance, FilterMode::HighPass,
                                   false, false, true);
        applyDrSidVoice_(2, freqReg, Waveform::Noise, 0x0800u, 0u, 2u, 0u, 1u);
        hardRetriggerVoice_(2);
        scheduleRelease(2, (0.008f + hatDecay * 0.070f) * scale * (0.80f + 0.40f * vel));
    }

    // Open hi-hat/cymbal — SID noise HP/BP with longer decay.
    void triggerOpenHat(float velocity, float tuneOffsetNorm = 0.0f, float decayScale = 1.0f) {
        const float vel = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        const float tune = std::clamp(hatTune + tuneOffsetNorm, 0.0f, 1.0f);
        const float scale = std::clamp(std::isfinite(decayScale) ? decayScale : 1.0f, 0.45f, 2.50f);
        chokeHatDrums_();
        setVoiceVelocity(2, 0.12f + 0.88f * vel);
        // v587: apply per-chip base/range compensation, matching triggerClosedHat().
        const bool on6581 = currentSidModel_ == ArpSID::SIDModel::MOS6581;
        const float hatBaseOpen  = on6581 ? 5000.0f : 4700.0f;
        const float hatRangeOpen = on6581 ? 4700.0f : 4400.0f;
        const uint16_t freqReg = hzToSIDReg(hatBaseOpen + tune * hatRangeOpen);
        startAuthenticFilterSweep_(2, 0x7E0u, 0x700u, 34.0f, 9u, FilterMode::BpHp, false, false, true);
        applyDrSidVoice_(2, freqReg, Waveform::Noise, 0x0800u, 0u, 9u, 2u, 6u);
        hardRetriggerVoice_(2);
        scheduleRelease(2, (0.090f + hatDecay * 0.420f) * scale * (0.85f + 0.30f * vel));
    }

    // Clap — SID noise burst profile, no non-SID delay-line overlay.
    void triggerClap(float velocity) {
        const float vel = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        chokeFamily1Drums_();
        setVoiceVelocity(1, 0.16f + 0.84f * vel);
        startAuthenticFilterSweep_(1, 0x720u, 0x5A0u, 22.0f + clapDecay * 16.0f, 10u, FilterMode::BpHp, false, true, false);
        applyDrSidVoice_(1, hzToSIDReg(2100.0f), Waveform::Noise, 0x0800u, 0u,
                         static_cast<uint8_t>(std::clamp(5.0f + clapDecay * 10.0f, 1.0f, 15.0f)), 0u, 5u);
        hardRetriggerVoice_(1);
        scheduleRelease(1, (0.045f + clapDecay * 0.280f) * (0.85f + 0.30f * vel));
    }

    // Cowbell / agogo / bell — SID pulse/combo waveform into band-pass.
    void triggerCowbell(float velocity, float tuneOffsetNorm = 0.0f, float decayScale = 1.0f) {
        const float vel = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        const float scale = std::clamp(std::isfinite(decayScale) ? decayScale : 1.0f, 0.45f, 2.50f);
        chokeFamily0Drums_();
        setVoiceVelocity(0, 0.16f + 0.84f * vel);
        const float tune = std::clamp(cowbellTune + tuneOffsetNorm, 0.0f, 1.0f);
        const uint16_t pw = static_cast<uint16_t>(std::clamp(0x0180 + static_cast<int>(tune * 0x0500), 0x0080, 0x0F80));
        const uint16_t targetReg = hzToSIDReg(620.0f + tune * 1450.0f);
        const uint16_t startReg = hzToSIDReg(760.0f + tune * 1700.0f);
        startAuthenticFilterSweep_(0, 0x620u, 0x520u, 28.0f, 13u, FilterMode::BandPass, true, false, false);
        applyDrSidVoice_(0, targetReg, Waveform::SawPulse, pw, 0u, 7u, 3u, 5u);
        startAuthenticPitchSweep_(0, startReg, targetReg, 24.0f);
        hardRetriggerVoice_(0);
        scheduleRelease(0, cowbellDecay * scale * (0.85f + 0.30f * vel));
    }

    static float tomBaseHzForGMNote_(int gmNote, float tune) noexcept {
        switch (gmNote) {
            case 41: return 82.0f  + tune * 42.0f;
            case 43: return 98.0f  + tune * 48.0f;
            case 45: return 116.0f + tune * 58.0f;
            case 47: return 138.0f + tune * 70.0f;
            case 48: return 164.0f + tune * 84.0f;
            case 50: return 196.0f + tune * 96.0f;
            case 60: return 285.0f + tune * 170.0f;
            case 61: return 210.0f + tune * 130.0f;
            case 62: return 250.0f + tune * 155.0f;
            case 63: return 220.0f + tune * 145.0f;
            case 64: return 165.0f + tune * 120.0f;
            case 65: return 360.0f + tune * 220.0f;
            case 66: return 270.0f + tune * 180.0f;
            case 78: return 430.0f + tune * 260.0f;
            case 79: return 330.0f + tune * 220.0f;
            default: return 118.0f + tune * 210.0f;
        }
    }

    static uint16_t tomCutoffStartForGMNote_(int gmNote, float tune) noexcept {
        const bool latin = gmNote >= 60;
        const float base = latin ? 1824.0f : 1472.0f;
        return static_cast<uint16_t>(std::clamp(std::lround(base + tune * (latin ? 272.0f : 384.0f)), 0l, 2047l));
    }

    static uint16_t tomCutoffEndForGMNote_(int gmNote, float tune) noexcept {
        const bool latin = gmNote >= 60;
        const float base = latin ? 1216.0f : 768.0f;
        return static_cast<uint16_t>(std::clamp(std::lround(base + tune * (latin ? 256.0f : 320.0f)), 0l, 2047l));
    }

    // Toms / conga / bongo — per-GM-note SID body. Earlier builds mapped every
    // tom to the same voice-0 kick lane with a broad low-pass sweep, so floor
    // toms, kick and low congas collapsed into one bass-drum colour. Keep the
    // 3-voice SID constraint, but make tom pitch/filter/pulse-width note-specific
    // and keep Tom activity independent from Kick/Cowbell telemetry.
    void triggerTom(float velocity, float tuneOffsetNorm = 0.0f, float decayScale = 1.0f, int gmNote = -1) {
        const float vel = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        const float scale = std::clamp(std::isfinite(decayScale) ? decayScale : 1.0f, 0.45f, 2.25f);
        chokeTomDrum_();
        activeTomGMNote_ = sidIsGMDrumNote(static_cast<uint8_t>(std::clamp(gmNote, 0, 127))) ? gmNote : 45;
        if (activeTomGMNote_ >= kGMDrumNoteMin && activeTomGMNote_ <= kGMDrumNoteMax)
            gmNoteLevels_[(size_t)activeTomGMNote_] = std::max(gmNoteLevels_[(size_t)activeTomGMNote_], vel);
        setVoiceVelocity(1, 0.15f + 0.85f * vel);
        const float tune = std::clamp(tomTune + tuneOffsetNorm, 0.0f, 1.0f);
        const float baseHz = tomBaseHzForGMNote_(gmNote, tune);
        const uint16_t freqReg = hzToSIDReg(baseHz);
        const uint16_t startReg = hzToSIDReg(baseHz * (1.52f + 0.52f * (1.0f - std::min(tune, 0.95f))));
        const uint8_t decay = static_cast<uint8_t>(std::clamp(3.0f + tomDecay * 12.0f, 1.0f, 15.0f));
        const uint16_t cutoffStart = tomCutoffStartForGMNote_(gmNote, tune);
        const uint16_t cutoffEnd   = tomCutoffEndForGMNote_(gmNote, tune);
        const uint8_t resonance = static_cast<uint8_t>(std::clamp(5.0f + 5.0f * (1.0f - tune), 4.0f, 11.0f));
        const uint16_t pw = static_cast<uint16_t>(std::clamp(0x0500 + static_cast<int>(tune * 0x0700), 0x0200, 0x0E80));
        startAuthenticFilterSweep_(1, cutoffStart, cutoffEnd, 16.0f + tomDecay * 28.0f, resonance, FilterMode::LpBp, false, true, false);
        applyDrSidVoice_(1, freqReg, Waveform::TriPulse, pw, 0u, decay, 0u,
                         static_cast<uint8_t>(std::clamp(2.0f + tomDecay * 8.0f, 1.0f, 15.0f)));
        startAuthenticPitchSweep_(1, startReg, freqReg, 14.0f + tomDecay * 30.0f);
        hardRetriggerVoice_(1);
        scheduleRelease(1, (0.034f + tomDecay * 0.384f + tune * 0.050f) * scale * (0.85f + 0.29f * vel));
    }

    // Rim / claves / woodblock — short SID pulse+noise-ish high-pass hit.
    void triggerRim(float velocity) {
        const float vel = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
        // Rim shares SID voice 2 physically, but it is not part of the hat choke
        // semantic family. Do not clear OpenHat/ClosedHat GM ledgers here.
        drumEnvelopes[(size_t)DrumType::Rim] = 0.0f;
        clearGMDrumClassLevels(SidGMDrumClass::Rim);
        setVoiceVelocity(2, 0.12f + 0.88f * vel);
        startAuthenticFilterSweep_(2, 0x760u, 0x660u, 7.0f, 10u, FilterMode::HighPass, false, false, true);
        applyDrSidVoice_(2, hzToSIDReg(2200.0f), Waveform::Pulse, 0x0200u, 0u, 2u, 0u, 1u);
        hardRetriggerVoice_(2);
        scheduleRelease(2, (0.010f + 0.030f) * (0.80f + 0.40f * vel));
    }


    void updateEnvelopes(int numSamples) {
        const double safeSampleRate = sampleRate > 1.0 ? sampleRate : kReferenceSampleRate;
        const double refSamples = static_cast<double>(std::max(1, numSamples)) * (kReferenceSampleRate / safeSampleRate);
        const float a = static_cast<float>(std::exp(std::log(0.999) * refSamples));
        for (auto& env : drumEnvelopes) {
            if (env > 0.0f) {
                env *= a;
                if (env < 1e-4f) env = 0.0f; // tighter threshold to avoid prolonged tail
            }
        }
        for (int note = kGMDrumNoteMin; note <= kGMDrumNoteMax; ++note) {
            float& level = gmNoteLevels_[(size_t)note];
            if (level > 0.0f) {
                level *= a;
                if (level < 1e-4f) level = 0.0f;
            }
        }
    }
};

} // namespace ArpSID
