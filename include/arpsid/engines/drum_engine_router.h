// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// drum_engine_router.h — Routes drum kit-loads + note dispatch to the
// right engine based on `DrumContext` (audit #39, #74 follow-up).
//
// PROBLEM
// ------// After audit #5/#39/#74 we have:
// * `DrumContext` enum that classifies factory slots by intent
// (`DrSID_C64Wavetable` vs `SID808_AnalogProjection`).
// * `DrSidEngine` (legacy — owns both authentic + AnalogX0X8 paths)
// * `Sid808Engine` (new — owns pure analog x0x projection)
//
// Router role: one place where host / sequencer / factory-loader code says
// "I want to play drum X with velocity V on identity I" and the right engine
// receives the call. This keeps context switching out of scattered call sites.
//
// CONTRACT
// -------// `DrumEngineRouter` references the canonical engines. Its public
// surface mirrors the union of both engines' note dispatch + render APIs.
// Internally it dispatches to the engine the active `DrumKitIdentity`
// nominates:
//
// * identity.context == DrSID_C64Wavetable → DrSidEngine
// * identity.context == SID808_AnalogProjection → Sid808Engine
// * identity.context == None or other → no-op / silence
//
// The router DOES NOT take ownership of the engines — it holds references
// — so host code can keep an existing DrSidEngine instance and wrap it in
// a router without invalidating live state. This lets factory-loader code
// migrate to the router incrementally without forcing a session rebuild.

#ifndef ARPSID_ENGINES_DRUM_ENGINE_ROUTER_H
#define ARPSID_ENGINES_DRUM_ENGINE_ROUTER_H

#include "arpsid/core/drum_context.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/sid808_engine.h"
#include "arpsid/engines/sid808_gm_projection.h"
#include "arpsid/patchbank/factory_sid808_kits.h"

#include <cstdint>

namespace ArpSID {

// Map a `Sid808Drum` back to a `SidGMDrumClass` for cross-engine
// routing — the two enums have parallel meanings.
constexpr SidGMDrumClass gmDrumClassForSid808(Sid808Drum d) noexcept {
    switch (d) {
        case Sid808Drum::Kick:      return SidGMDrumClass::Kick;
        case Sid808Drum::Snare:     return SidGMDrumClass::Snare;
        case Sid808Drum::ClosedHat: return SidGMDrumClass::ClosedHat;
        case Sid808Drum::OpenHat:   return SidGMDrumClass::OpenHat;
        case Sid808Drum::Clap:      return SidGMDrumClass::Clap;
        case Sid808Drum::Cowbell:   return SidGMDrumClass::Cowbell;
        case Sid808Drum::Tom:       return SidGMDrumClass::Tom;
        case Sid808Drum::Rim:       return SidGMDrumClass::Rim;
        case Sid808Drum::Count:     return SidGMDrumClass::Unsupported;
    }
    return SidGMDrumClass::Unsupported;
}

constexpr Sid808Drum sid808DrumFromGmClass(SidGMDrumClass c) noexcept {
    switch (c) {
        case SidGMDrumClass::Kick:      return Sid808Drum::Kick;
        case SidGMDrumClass::Snare:     return Sid808Drum::Snare;
        case SidGMDrumClass::ClosedHat: return Sid808Drum::ClosedHat;
        case SidGMDrumClass::OpenHat:   return Sid808Drum::OpenHat;
        case SidGMDrumClass::Clap:      return Sid808Drum::Clap;
        case SidGMDrumClass::Cowbell:   return Sid808Drum::Cowbell;
        case SidGMDrumClass::Tom:       return Sid808Drum::Tom;
        case SidGMDrumClass::Rim:       return Sid808Drum::Rim;
        case SidGMDrumClass::Unsupported: return Sid808Drum::Count;
    }
    return Sid808Drum::Count;
}

// Dispatch counters — surface which engine received how many calls.
// Diagnostic only; never used for control flow.
struct DrumEngineRouterDiagnostics {
    std::uint64_t drsidNoteOnCount    = 0;
    std::uint64_t drsidNoteOffCount   = 0;
    std::uint64_t sid808NoteOnCount   = 0;
    std::uint64_t sid808NoteOffCount  = 0;
    std::uint64_t unroutedNoteOnCount = 0;  // context == None / unsupported
};

class DrumEngineRouter {
public:
    DrumEngineRouter(DrSidEngine& drsidEngine, Sid808Engine& sid808Engine) noexcept
      : drsid_(&drsidEngine), sid808_(sid808Engine) {}
    explicit DrumEngineRouter(Sid808Engine& sid808Engine) noexcept
      : drsid_(nullptr), sid808_(sid808Engine) {}

    void bindDrSidEngine(DrSidEngine& engine) noexcept { drsid_ = &engine; }
    void unbindDrSidEngine() noexcept { drsid_ = nullptr; }
    bool hasDrSidEngine() const noexcept { return drsid_ != nullptr; }

    // ── Active identity (which engine receives subsequent calls) ────────────
    void setActiveIdentity(const DrumKitIdentity& id) noexcept { activeIdentity_ = id; }
    const DrumKitIdentity& activeIdentity() const noexcept { return activeIdentity_; }

    // Convenience: set identity from a factory slot number. Looks up the
    // slot's combined-classifier context.
    void setActiveIdentityFromFactorySlot(int slot) noexcept {
        activeIdentity_ = drumKitIdentityForFactorySlot(slot);
    }

    // ── Note dispatch (dispatches to the engine the active identity names) ─
    // `gmDrumClass` is the canonical cross-engine drum identifier. The
    // router converts to whichever engine API the active context demands.
    void noteOn(SidGMDrumClass gmDrumClass, std::uint8_t velocity, std::uint8_t midiNoteHint = 0) noexcept {
        noteOnWithOverride(gmDrumClass, velocity, midiNoteHint, nullptr);
    }
    void noteOnWithOverride(SidGMDrumClass gmDrumClass,
                            std::uint8_t velocity,
                            std::uint8_t midiNoteHint,
                            const Sid808HitOverride* overrideOrNull) noexcept {
        switch (activeIdentity_.context) {
            case DrumContext::SID808_AnalogProjection: {
                const Sid808Drum drum = sid808DrumFromGmClass(gmDrumClass);
                if (drum == Sid808Drum::Count) { ++diag_.unroutedNoteOnCount; return; }
                Sid808HitOverride user = overrideOrNull ? *overrideOrNull : Sid808HitOverride{};
                Sid808VoiceConfig base = sid808_.compensatedDrumVoiceConfig(drum);
                bool hasSelectedFactoryBase = false;
                if (user.hasSelectedFactorySlot &&
                    isSid808FactorySlot(static_cast<int>(user.selectedFactorySlot))) {
                    const Sid808KitConfigTable kit =
                        factorySid808ResolvedKitForSlot(static_cast<int>(user.selectedFactorySlot));
                    base = kit[static_cast<std::size_t>(drum)];
                    hasSelectedFactoryBase = true;
                }
                const SidGMDrumNoteSpec gmSpec =
                    sid808GMProjectionSpecForNote(midiNoteHint, gmDrumClass);
                Sid808HitOverride merged = sid808GMNoteOverride(gmSpec, base, user);
                if (hasSelectedFactoryBase) {
                    if (!merged.hasFreq) { merged.freq = base.freq; merged.hasFreq = true; }
                    if (!merged.hasVoiceLevel) { merged.voiceLevel = base.voiceLevel; merged.hasVoiceLevel = true; }
                    if (!merged.hasWaveform) { merged.waveform = base.waveform; merged.hasWaveform = true; }
                    if (!merged.hasAttackDecay) { merged.attackDecay = base.attackDecay; merged.hasAttackDecay = true; }
                    if (!merged.hasSustainRelease) { merged.sustainRelease = base.sustainRelease; merged.hasSustainRelease = true; }
                    if (!merged.hasPulseWidth) { merged.pulseWidth = base.pulseWidth; merged.hasPulseWidth = true; }
                    if (!merged.hasFlags) { merged.flags = base.flags; merged.hasFlags = true; }
                }
                const std::uint8_t sidVelocity =
                    sid808GMScaledVelocity(velocity, gmSpec.velocityScale);
                sid808_.noteOnWithOverride(drum, sidVelocity, midiNoteHint, merged);
                ++diag_.sid808NoteOnCount;
                return;
            }
            case DrumContext::DrSID_C64Wavetable: {
                if (!drsid_) { ++diag_.unroutedNoteOnCount; return; }
                // B1 Fix: use triggerMidiNote() so DrSidEngine's full synthesis
                // path fires (wavetable runner, GM spec lookup, level tracking).
                // allocateDrumVoice() only increments the diagnostic allocator
                // and does NOT trigger any actual SID voice synthesis.
                const DrSidEngine::DrumType dt = DrSidEngine::drumTypeForGMClass(gmDrumClass);
                const std::uint8_t canonNote = static_cast<std::uint8_t>(
                    DrSidEngine::canonicalMidiNoteForDrumType(dt));
                const std::uint8_t note = (midiNoteHint > 0u && midiNoteHint <= 127u)
                    ? midiNoteHint : canonNote;
                const float velF = static_cast<float>(velocity) * (1.0f / 127.0f);
                drsid_->triggerMidiNote(static_cast<int>(note), velF);
                ++diag_.drsidNoteOnCount;
                return;
            }
            case DrumContext::None:
            case DrumContext::Digi4Bit:
            default:
                ++diag_.unroutedNoteOnCount;
                return;
        }
    }

    void noteOff(SidGMDrumClass gmDrumClass) noexcept {
        switch (activeIdentity_.context) {
            case DrumContext::SID808_AnalogProjection: {
                const Sid808Drum drum = sid808DrumFromGmClass(gmDrumClass);
                if (drum == Sid808Drum::Count) return;
                sid808_.noteOff(drum);
                ++diag_.sid808NoteOffCount;
                return;
            }
            case DrumContext::DrSID_C64Wavetable: {
                if (!drsid_) return;
                const DrSidEngine::DrumType dt = DrSidEngine::drumTypeForGMClass(gmDrumClass);
                const std::uint8_t note = static_cast<std::uint8_t>(
                    DrSidEngine::canonicalMidiNoteForDrumType(dt));
                drsid_->noteOffMidi(static_cast<int>(note));
                ++diag_.drsidNoteOffCount;
                return;
            }
            default:
                return;
        }
    }

    void allNotesOff() noexcept {
        sid808_.allNotesOff();
        if (drsid_) drsid_->allNotesOff();
    }

    // ── Realtime forensic projection ──────────────────────────────────────
    // The GUI exposes forensic controls as live sound-shaping effects. Keep
    // both owned drum engines current so context switches never resurrect a
    // stale chip model/forensic state.
    void setForensicConfig(const ArpSIDForensicConfig& cfg) noexcept {
        if (drsid_) drsid_->setForensicConfig(cfg);
        sid808_.setForensicConfig(cfg);
    }

    const ArpSIDForensicConfig& activeForensicConfig() const noexcept {
        if (activeIdentity_.context == DrumContext::SID808_AnalogProjection) {
            return sid808_.getForensicConfig();
        }
        return drsid_ ? drsid_->getForensicConfig() : sid808_.getForensicConfig();
    }

    // ── Render (dispatches to active engine) ───────────────────────────────
    // The router writes into the host's stereo buffers. Only the engine
    // matching the active context emits audio — the other engine is
    // dormant. If host code wants BOTH engines layered, it should call
    // them directly (router is single-context by design — audit-correct
    // separation).
    void processBlock(float* outL, float* outR, int numSamples) noexcept {
        if (!outL || numSamples <= 0) return;
        if (!outR) outR = outL;
        if (activeIdentity_.context == DrumContext::SID808_AnalogProjection) {
            if (outR == outL) {
                for (int i = 0; i < numSamples; ++i) outL[i] = 0.0f;
            } else {
                for (int i = 0; i < numSamples; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }
            }
            sid808_.processBlock(outL, outR, numSamples);
            return;
        }
        if (activeIdentity_.context == DrumContext::DrSID_C64Wavetable) {
            if (!drsid_) {
                for (int i = 0; i < numSamples; ++i) outL[i] = 0.0f;
                if (outR != outL) for (int i = 0; i < numSamples; ++i) outR[i] = 0.0f;
                return;
            }
            if (outR == outL) {
                for (int i = 0; i < numSamples; ++i) outL[i] = 0.0f;
            } else {
                for (int i = 0; i < numSamples; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }
            }
            float* outputs[2] = { outL, outR };
            drsid_->processReplacingBlock(outputs, numSamples);
            return;
        }
        for (int i = 0; i < numSamples; ++i) outL[i] = 0.0f;
        if (outR != outL) for (int i = 0; i < numSamples; ++i) outR[i] = 0.0f;
    }

    // ── Setup propagation (host SR / clock change) ─────────────────────────
    void prepare(double sampleRate) noexcept {
        sid808_.prepare(sampleRate);
        if (drsid_) drsid_->setSampleRate(sampleRate);
    }
    void setClockFrequency(double hz) noexcept {
        sid808_.setClockFrequency(hz);
        if (drsid_) drsid_->setClockFrequency(hz);
    }
    void setSidModel(SIDModel model) noexcept {
        if (drsid_) drsid_->setSIDModel(model);
        sid808_.setSidModel(model);
    }
    bool applySid808FactorySlot(int slot) noexcept {
        activeIdentity_ = drumKitIdentityForFactorySlot(slot);
        if (activeIdentity_.context != DrumContext::SID808_AnalogProjection) return false;
        return applyFactorySid808Kit(slot, sid808_);
    }

    // ── Diagnostic ─────────────────────────────────────────────────────────
    const DrumEngineRouterDiagnostics& diagnostics() const noexcept { return diag_; }
    void resetDiagnostics() noexcept { diag_ = DrumEngineRouterDiagnostics{}; }

    DrSidEngine*       drsidEngine()      noexcept { return drsid_; }
    const DrSidEngine* drsidEngine() const noexcept { return drsid_; }
    Sid808Engine&       sid808Engine()      noexcept { return sid808_; }
    const Sid808Engine& sid808Engine() const noexcept { return sid808_; }

private:
    DrSidEngine*   drsid_;
    Sid808Engine&  sid808_;
    DrumKitIdentity activeIdentity_{};
    DrumEngineRouterDiagnostics diag_{};
};

} // namespace ArpSID

#endif // ARPSID_ENGINES_DRUM_ENGINE_ROUTER_H
