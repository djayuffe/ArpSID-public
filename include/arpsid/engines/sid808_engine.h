// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// sid808_engine.h — Authentic SID-808 analog x0x projection engine
// (Audit #39, #74 — engine-split architecture).
//
// PROBLEM (audit #39)
// ------------------// The legacy `DrSidEngine` carries TWO identities simultaneously:
// 1. SidAuthentic — pure C64 wavetable-drum register microprograms.
// 2. AnalogX0X8 — analog x0x-style projection (kick = pitched sine,
// snare = tone + noise, hat = filtered noise, etc.).
//
// State, choke logic, voice allocation, and even the machine-model flag
// drift between these two modes inside one class. Audit #74 specifically
// flagged the SID-808 factory slots (120..149 canonical) as being
// "forced through DrSID with drSidMode=true" — the engine-split is the
// architectural answer.
//
// THIS HEADER
// ----------// `Sid808Engine` is a CLEAN, FROM-SCRATCH analog x0x projection engine
// built on `SingleSidThreeVoiceEngine` (audit #29 follow-up). It has:
//
// * Its own `SingleSidThreeVoiceEngine` — one SIDChip + 3-voice
// allocator. NOT shared with DrSidEngine.
// * Per-drum analog voice configuration baked in (kick/snare/hat/clap/
// cowbell/tom). Each drum hit configures the SID voice's waveform,
// ADSR, pulse-width, and filter route for x0x-style sound.
// * Choke groups via `SidVoiceAllocator` (audit #41).
// * Per-drum-family voice reservation (audit #42 — kick reserved on
// voice 0, snare/clap on voice 1, hats/cymbal on voice 2).
// * Own state, own clock, own SR — independent of DrSidEngine.
//
// USAGE
// ----// Host / sequencer code dispatches via `DrumEngineRouter` (separate
// header), which inspects `DrumContext` to pick the engine:
//
// * `DrumContext::SID808_AnalogProjection` → Sid808Engine
// * `DrumContext::DrSID_C64Wavetable` → DrSidEngine (legacy)
//
// MIGRATION
// --------// New factory presets in the canonical 120..149 range should route
// through `DrumEngineRouter` → Sid808Engine. Existing Logic sessions
// that depend on the legacy DrSidEngine.AnalogX0X8 path continue to
// work via the unchanged DrSidEngine — no audio regression. The
// follow-up engine-split pass will migrate legacy presets to the new
// engine and remove the AnalogX0X8 code path from DrSidEngine.

#ifndef ARPSID_ENGINES_SID808_ENGINE_H
#define ARPSID_ENGINES_SID808_ENGINE_H

#include "arpsid/core/drsid_instrument_program.h"
#include "arpsid/core/sid_chip.h"
#include "arpsid/core/sid_runtime_forensic_config.h"
#include "arpsid/core/sid_voice_allocator.h"
#include "arpsid/engines/single_sid_three_voice_engine.h"
#include "arpsid/gui/kit_voice_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ArpSID {

// SID-808 drum-class enumeration. Mirrors the legacy 8 drum classes but
// is owned by this engine — not shared with DrSidEngine.
enum class Sid808Drum : std::uint8_t {
    Kick        = 0,
    Snare       = 1,
    ClosedHat   = 2,
    OpenHat     = 3,
    Clap        = 4,
    Cowbell     = 5,
    Tom         = 6,
    Rim         = 7,
    Count       = 8,
};

// Per-drum-family voice reservation (audit #42): each drum family
// targets a specific SID voice index. Kick → voice 0 (always), snare/
// clap → voice 1, hats/cymbal/cowbell → voice 2. Toms borrow voice 0
// when free. Rim borrows voice 1.
constexpr std::uint8_t sid808VoiceForDrum(Sid808Drum d) noexcept {
    switch (d) {
        case Sid808Drum::Kick:        return 0;
        case Sid808Drum::Tom:         return 0;
        case Sid808Drum::Snare:       return 1;
        case Sid808Drum::Clap:        return 1;
        case Sid808Drum::Rim:         return 1;
        case Sid808Drum::ClosedHat:   return 2;
        case Sid808Drum::OpenHat:     return 2;
        case Sid808Drum::Cowbell:     return 2;
        case Sid808Drum::Count:       return 0;
    }
    return 0;
}

// Choke group per drum family — audit #41 explicit choke law.
// Hat family: ClosedHat chokes OpenHat and vice versa (realistic hat choke).
// Noise family: Clap shares voice 1 with Snare+Rim; they choke each other.
// Cowbell: voice 2 (with the hats); cross-family voice collisions are handled
// by the forced-voice reprogram, so Cowbell keeps its own Cymbal group.
//
// v857 fix: Tom maps to SID voice 0 (with Kick — see sid808VoiceForDrum), NOT
// voice 2. Its previous ChokeGroup::Cymbal made a Tom hit choke a ringing
// Cowbell/Hat on a DIFFERENT physical voice (and vice versa) — an audible
// spurious mute with no hardware justification. Tom now uses TomShared (toms
// choke each other, x0x-correct); the voice-0 collision with Kick is resolved
// by the forced-voice reprogram exactly like every other same-voice pair.
constexpr Drsid::ChokeGroup sid808ChokeForDrum(Sid808Drum d) noexcept {
    switch (d) {
        case Sid808Drum::ClosedHat:   return Drsid::ChokeGroup::HiHat;
        case Sid808Drum::OpenHat:     return Drsid::ChokeGroup::HiHat;
        case Sid808Drum::Clap:        return Drsid::ChokeGroup::NoiseShared;
        case Sid808Drum::Snare:       return Drsid::ChokeGroup::NoiseShared;  // snare+clap share
        case Sid808Drum::Rim:         return Drsid::ChokeGroup::NoiseShared;  // rim+snare share voice 1
        case Sid808Drum::Cowbell:     return Drsid::ChokeGroup::Cymbal;
        case Sid808Drum::Tom:         return Drsid::ChokeGroup::TomShared;    // voice 0; toms choke toms only
        default:                       return Drsid::ChokeGroup::None;
    }
}

// A9: Per-drum voice configuration — unified with KitVoiceConfig flags.
// ring/sync/filter flags added so GUI-side KitVoiceConfig can round-trip.
struct Sid808VoiceConfig {
    std::uint16_t  freq;            ///< 16-bit SID frequency register
    std::uint16_t  pulseWidth;      ///< 12-bit PWM (0..4095)
    std::uint8_t   waveform;        ///< SID $D404 control reg (excluding GATE)
    std::uint8_t   attackDecay;     ///< $D405 ADSR nibbles
    std::uint8_t   sustainRelease;  ///< $D406 ADSR nibbles
    std::uint8_t   flags;           ///< bit0=ringMod, bit1=hardSync, bit2=filterRoute (A9)
    float          voiceLevel;      ///< chip-level mixer 0..1
};
static_assert(sizeof(Sid808VoiceConfig) == 12, "Sid808VoiceConfig size pinned");

enum class Sid808PercProfile : std::uint8_t {
    Default = 0,
    TomLow,
    TomMid,
    TomHigh,
    BongoHigh,
    BongoLow,
    CongaMute,
    CongaOpen,
    CongaLow,
    TimbaleHigh,
    TimbaleLow,
    CuicaMute,
    CuicaOpen,
    OpenHat,
    Crash,
    Ride,
    Splash,
    China,
    Triangle,
    WhistleShort,
    WhistleLong,
    GuiroShort,
    GuiroLong,
    Shaker,
    Tambourine,
    // v873 GM families: ClosedHat-, Cowbell- and Rim-derived percussion that
    // previously collapsed to the generic voice for their engine drum.
    PedalHat,       // ClosedHat family
    RideBell,       // Cowbell family
    AgogoHigh,      // Cowbell family
    AgogoLow,       // Cowbell family
    TriangleMute,   // Cowbell family (muted triangle)
    Vibraslap,      // Rim family
    Claves,         // Rim family
    WoodBlockHigh,  // Rim family
    WoodBlockLow,   // Rim family
};

struct Sid808HitOverride {
    std::uint16_t freq = 0;
    float voiceLevel = 0.0f;
    std::uint8_t waveform = 0;
    std::uint8_t attackDecay = 0;
    std::uint8_t sustainRelease = 0;
    std::uint16_t pulseWidth = 0;
    std::uint8_t flags = 0;
    std::uint16_t selectedFactorySlot = 0;
    Sid808PercProfile profile = Sid808PercProfile::Default;
    bool hasFreq = false;
    bool hasVoiceLevel = false;
    bool hasWaveform = false;
    bool hasAttackDecay = false;
    bool hasSustainRelease = false;
    bool hasPulseWidth = false;
    bool hasFlags = false;
    bool hasSelectedFactorySlot = false;
    bool hasProfile = false;
};
static_assert(std::is_trivially_copyable<Sid808HitOverride>::value,
              "Sid808HitOverride must be trivially copyable");


// A9: Convert Sid808VoiceConfig ↔ KitVoiceConfig (from kit_voice_config.h).
// Forward declarations — include kit_voice_config.h before using these.
namespace Sid808Detail {
    inline constexpr std::uint8_t kFlagRingMod   = 1u << 0;
    inline constexpr std::uint8_t kFlagHardSync  = 1u << 1;
    inline constexpr std::uint8_t kFlagFilter    = 1u << 2;
} // namespace Sid808Detail

// SID808 kit data stores waveform as the real SID control-register waveform
// bits (0x10/0x20/0x40/0x80), while some GUI/test callers naturally hand over
// the internal Waveform nibble (1/2/4/8). Normalize every boundary to the SID
// control-bit form so valid pulse/noise kits cannot be masked down to zero.
inline constexpr std::uint8_t sid808NormalizeWaveformControl(std::uint8_t wave) noexcept {
    const std::uint8_t rawControl = static_cast<std::uint8_t>(wave & 0xF0u);
    if (rawControl != 0u) return rawControl;

    std::uint8_t out = 0u;
    const std::uint8_t internal = static_cast<std::uint8_t>(wave & 0x0Fu);
    if (internal & static_cast<std::uint8_t>(Waveform::Triangle)) out |= 0x10u;
    if (internal & static_cast<std::uint8_t>(Waveform::Sawtooth)) out |= 0x20u;
    if (internal & static_cast<std::uint8_t>(Waveform::Pulse))    out |= 0x40u;
    if (internal & static_cast<std::uint8_t>(Waveform::Noise))    out |= 0x80u;
    return out;
}

inline constexpr std::uint8_t sid808ControlWaveformToInternal(std::uint8_t wave) noexcept {
    const std::uint8_t sidControlWave = sid808NormalizeWaveformControl(wave);
    std::uint8_t wf = 0u;
    if (sidControlWave & 0x10u) wf |= static_cast<std::uint8_t>(Waveform::Triangle);
    if (sidControlWave & 0x20u) wf |= static_cast<std::uint8_t>(Waveform::Sawtooth);
    if (sidControlWave & 0x40u) wf |= static_cast<std::uint8_t>(Waveform::Pulse);
    if (sidControlWave & 0x80u) wf |= static_cast<std::uint8_t>(Waveform::Noise);
    return static_cast<std::uint8_t>(wf & 0x0Fu);
}

inline constexpr bool sid808WaveformHasRenderableBits(std::uint8_t wave) noexcept {
    return sid808NormalizeWaveformControl(wave) != 0u;
}

// Canonical x0x-style voice configs per drum family. Tuned for "Classic"
// SID-808 character. Future kits ("Punch", "Lo-Fi", etc.) override these
// via setDrumVoiceConfig().
// A10: Authentic 808 synthesis per drum class.
//
// Kick: triangle (0x10) at low freq with an explicit timed pitch program.
// The first register write starts above the body pitch, then scheduled
// sub-stages drop the oscillator through punch/body/tail regions.
//
// Snare: pulse+noise (0x40 | 0x80 = 0xC0) for tonal body+noise blend.
// The SID can set both bits simultaneously — voice produces a noisy pulse
// giving the snare's "crack+noise" character. PW narrows the pulse.
//
// ClosedHat/OpenHat: pure noise (0x80), metallic filter shaping via the
// SID noise register sequence. OpenHat uses scheduled level/filter stages
// so the ring is a tail, not a single static tick.
//
// Clap: noise (0x80) with rapid internal gate re-triggers for the classic
// multi-burst handclap onset, followed by a lower noise tail.
//
// Cowbell: pulse (0x40) with narrow PW and timed partial alternation on
// voice 2. The SID has one oscillator per voice, so the engine time-multiplexes
// two close pulse partials before settling into the ringing tail.
//
// Tom: triangle (0x10) with a timed downward pitch drop.
//
// Rim: short pulse burst, very fast A/D, dry click character.
// struct layout: {freq, pulseWidth, waveform, attackDecay, sustainRelease, flags, voiceLevel}
constexpr Sid808VoiceConfig sid808DefaultConfig(Sid808Drum d) noexcept {
    switch (d) {
        // Kick: triangle body; note-on programs the high-to-low pitch drop.
        case Sid808Drum::Kick:        return { 0x0900u, 0u,    0x10u, 0x04u, 0x18u, 0u, 0.98f };
        // Snare: pulse+noise blend, medium freq, tight snap
        case Sid808Drum::Snare:       return { 0x3000u, 800u,  0xC0u, 0x01u, 0x06u, Sid808Detail::kFlagFilter, 0.88f };
        // Closed hat: pure noise, very short decay
        case Sid808Drum::ClosedHat:   return { 0x7FFFu, 0u,    0x80u, 0x00u, 0x11u, 0u, 0.68f };
        // Open hat: pure noise, long ring with high-pass shaping.
        case Sid808Drum::OpenHat:     return { 0x7FFFu, 0u,    0x80u, 0x00u, 0x78u, Sid808Detail::kFlagFilter, 0.72f };
        // Clap: noise burst; note-on schedules the burst train.
        case Sid808Drum::Clap:        return { 0x5200u, 0u,    0x80u, 0x00u, 0x35u, 0u, 0.85f };
        // Cowbell: pulse partial A; note-on schedules partial B and tail.
        case Sid808Drum::Cowbell:     return { 0x4A00u, 0x0450u, 0x40u, 0x00u, 0x87u, Sid808Detail::kFlagFilter, 0.78f };
        // Tom: triangle body; note-on starts high and drops into this base.
        // v909: longer default decay/release + more level so the default tom
        // sings like the 808 reference instead of thudding out early.
        case Sid808Drum::Tom:         return { 0x1600u, 0u,    0x10u, 0x04u, 0x79u, 0u, 0.95f };
        // Rim: short pulse click, very fast A/D/S/R
        case Sid808Drum::Rim:         return { 0x5200u, 1200u, 0x40u, 0x00u, 0x13u, 0u, 0.72f };
        case Sid808Drum::Count:       return { 0u,      0u,    0x00u, 0x00u, 0x00u, 0u, 0.0f };
    }
    return { 0u, 0u, 0x00u, 0x00u, 0x00u, 0u, 0.0f };
}

constexpr std::uint8_t sid808CanonicalMidiNoteForDrum(Sid808Drum d) noexcept {
    switch (d) {
        case Sid808Drum::Kick:        return 36;
        case Sid808Drum::Snare:       return 38;
        case Sid808Drum::ClosedHat:   return 42;
        case Sid808Drum::OpenHat:     return 46;
        case Sid808Drum::Clap:        return 39;
        case Sid808Drum::Cowbell:     return 56;
        case Sid808Drum::Tom:         return 47;
        case Sid808Drum::Rim:         return 37;
        case Sid808Drum::Count:       return 0;
    }
    return 0;
}

// ─── Sid808Engine ───────────────────────────────────────────────────────────
//
// Architectural sibling to DrSidEngine. Owns:
// * ONE `SingleSidThreeVoiceEngine` (one SIDChip + 3-voice allocator)
// * Per-drum-family voice config table
// * Sample-rate, clock, model state (independent of DrSidEngine)
//
// Render path: noteOn(drumClass, velocity) → configure SID voice from
// Sid808VoiceConfig → engine.noteOn(midiNote, velocity, chokeGroup).
class Sid808Engine {
public:
    Sid808Engine() noexcept {
        // Initialize per-drum config table from canonical defaults.
        for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(Sid808Drum::Count); ++i) {
            drumConfigs_[i] = sid808DefaultConfig(static_cast<Sid808Drum>(i));
        }
        sidEngine_.setSidModel(SIDModel::MOS8580); // x0x kits live cleanest on 8580
        sidEngine_.setVoiceStealingPolicy(VoiceStealingPolicy::StealOldest);
    }

    // ── Setup (non-RT) ──────────────────────────────────────────────────────
    void prepare(double sampleRate) noexcept {
        // v872: clear any active drum voices / auto-release timers / microstages
        // before re-preparing. Otherwise a device or sample-rate change leaves
        // scheduled stage/gate state that was timed for the OLD sample rate,
        // producing stuck or wrongly-timed hits until the host issues an
        // all-notes-off. prepare() is non-RT, so this is safe here.
        allNotesOff();
        sampleRate_ = std::isfinite(sampleRate) ? std::max(1.0, sampleRate) : 44100.0;
        refreshTelemetryDecay_();
        sidEngine_.prepare(sampleRate_);
    }
    void setClockFrequency(double hz) noexcept {
        // Audit #45 contract — reject invalid clock instead of silent fallback.
        if (!sidClockFrequencySupported(hz)) {
            ++invalidClockRejectCount_;
            return;
        }
        clockFreq_ = hz;
        sidEngine_.setClockFrequency(hz);
    }
    void setSidModel(SIDModel model) noexcept {
        sidModel_ = model;
        sidEngine_.setSidModel(model);
        sidEngine_.chip().setForensicConfig(forensicConfig_);
    }
    void setVoiceStealingPolicy(VoiceStealingPolicy p) noexcept {
        sidEngine_.setVoiceStealingPolicy(p);
    }
    void setForensicConfig(const ArpSIDForensicConfig& cfg) noexcept {
        forensicConfig_ = cfg;
        sidEngine_.chip().setForensicConfig(forensicConfig_);
    }
    const ArpSIDForensicConfig& getForensicConfig() const noexcept {
        return forensicConfig_;
    }

    // A6: Accent support — velocity above threshold gets boosted level + tighter decay.
    void setAccentVelocityThreshold(std::uint8_t t) noexcept { accentThreshold_ = t; }
    std::uint8_t accentVelocityThreshold() const noexcept { return accentThreshold_; }

    // A7: Zero-velocity NoteOn is NoteOff (MIDI spec — B7 fix).
    static constexpr bool isNoteOff(std::uint8_t velocity) noexcept { return velocity == 0u; }

    // Override per-kit config for a drum family (called by kit-loader).
    void setDrumVoiceConfig(Sid808Drum drum, const Sid808VoiceConfig& cfg) noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        if (i < static_cast<std::uint8_t>(Sid808Drum::Count)) {
            Sid808VoiceConfig normalized = cfg;
            normalized.waveform = sid808NormalizeWaveformControl(normalized.waveform);
            drumConfigs_[i] = normalized;
        }
    }
    Sid808VoiceConfig drumVoiceConfig(Sid808Drum drum) const noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        if (i < static_cast<std::uint8_t>(Sid808Drum::Count)) {
            return drumConfigs_[i];
        }
        return sid808DefaultConfig(Sid808Drum::Count);
    }
    Sid808VoiceConfig compensatedDrumVoiceConfig(Sid808Drum drum) const noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        if (i < static_cast<std::uint8_t>(Sid808Drum::Count)) {
            return compensateConfigForSidModel_(drum, drumConfigs_[i]);
        }
        return sid808DefaultConfig(Sid808Drum::Count);
    }

    // ── Note dispatch (host / sequencer thread) ─────────────────────────────
    // A4: Voice index enforced from sid808VoiceForDrum() — not allocator choice.
    // A5: midiNoteHint modulates frequency (semitone offset from canonical note).
    // A6: Accent — velocity > accentThreshold_ boosts level and tightens decay.
    // B7: velocity==0 treated as noteOff per MIDI spec.
    std::uint8_t noteOn(Sid808Drum drum,
                        std::uint8_t velocity,
                        std::uint8_t midiNoteHint = 0) noexcept {
        return noteOnWithConfig_(drum, velocity, midiNoteHint, compensatedDrumVoiceConfig(drum));
    }
    std::uint8_t noteOnWithOverride(Sid808Drum drum,
                                    std::uint8_t velocity,
                                    std::uint8_t midiNoteHint,
                                    const Sid808HitOverride& ov) noexcept {
        Sid808VoiceConfig cfg = compensatedDrumVoiceConfig(drum);
        if (ov.hasFreq) cfg.freq = ov.freq;
        if (ov.hasVoiceLevel) cfg.voiceLevel = std::clamp(ov.voiceLevel, 0.0f, 1.0f);
        if (ov.hasWaveform) cfg.waveform = sid808NormalizeWaveformControl(ov.waveform);
        if (ov.hasAttackDecay) cfg.attackDecay = ov.attackDecay;
        if (ov.hasSustainRelease) cfg.sustainRelease = ov.sustainRelease;
        if (ov.hasPulseWidth) cfg.pulseWidth = static_cast<std::uint16_t>(ov.pulseWidth & 0x0FFFu);
        if (ov.hasFlags) cfg.flags = ov.flags;
        const Sid808PercProfile profile = ov.hasProfile ? ov.profile : Sid808PercProfile::Default;
        return noteOnWithConfig_(drum, velocity, midiNoteHint, cfg, profile);
    }

private:
    static constexpr std::size_t kMicroStageProgramCapacity_ = 4u;

    struct Sid808MicroStage {
        int samples = -1;
        Sid808Drum drum = Sid808Drum::Count;
        Sid808VoiceConfig config{};
        float level = 0.0f;
        bool raiseGate = true;
    };

    std::uint8_t noteOnWithConfig_(Sid808Drum drum,
                                   std::uint8_t velocity,
                                   std::uint8_t midiNoteHint,
                                   Sid808VoiceConfig cfg,
                                   Sid808PercProfile profile = Sid808PercProfile::Default) noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        if (i >= static_cast<std::uint8_t>(Sid808Drum::Count)) return kNoVoice;
        // B7: velocity 0 = NoteOff per MIDI spec.
        if (velocity == 0u) { noteOff(drum); return kNoVoice; }

        // A4: Fixed voice from lookup — not free allocator choice. Force the
        // allocator to the same physical voice so note-off/choke/active-count
        // bookkeeping matches the audible SID voice we program below.
        const std::uint8_t fixedVoice = sid808VoiceForDrum(drum);
        const std::uint8_t token      = static_cast<std::uint8_t>(0x40u + i);
        const Drsid::ChokeGroup choke = sid808ChokeForDrum(drum);
        const std::uint8_t ownedVoice =
            sidEngine_.forceNoteOnVoiceBookkeepingOnly(fixedVoice, token, velocity, choke);
        if (ownedVoice == kNoVoice) return kNoVoice;

        // A5: midiNoteHint → semitone-offset the base frequency.
        if (midiNoteHint > 0u && midiNoteHint <= 127u &&
            sid808ProfileAllowsRawMidiPitch_(profile)) {
            const std::uint8_t canonNote = sid808CanonicalMidiNoteForDrum(drum);
            const int semis = static_cast<int>(midiNoteHint) - static_cast<int>(canonNote);
            if (semis != 0) {
                // Shift 16-bit SID freq by 2^(semis/12).
                const float ratio = std::pow(2.0f, static_cast<float>(semis) / 12.0f);
                const float shifted = static_cast<float>(cfg.freq) * ratio;
                cfg.freq = static_cast<std::uint16_t>(
                    std::clamp(static_cast<int>(shifted), 0, 65535));
            }
        }

        // A6: Accent — velocity above threshold boosts level and sharpens decay.
        const bool accent = (velocity >= accentThreshold_);
        float velocityScale = static_cast<float>(velocity) * (1.0f / 127.0f);
        if (accent) {
            velocityScale = std::min(1.0f, velocityScale * 1.35f);  // +2.6 dB boost
            const std::uint8_t decayNibble = cfg.attackDecay & 0x0Fu;
            const std::uint8_t shorterDecay = decayNibble > 0u ? decayNibble - 1u : 0u;
            cfg.attackDecay = static_cast<std::uint8_t>((cfg.attackDecay & 0xF0u) | shorterDecay);
        }

        const float appliedLevel = std::clamp(cfg.voiceLevel * velocityScale, 0.0f, 1.0f);
        cfg = sanitizeRealtimeDrumConfig_(drum, cfg);
        clearMicroStage_(fixedVoice);
        if (drum == Sid808Drum::Snare) {
            resetSnareStageTelemetry_();
            const Sid808VoiceConfig bodyCfg = cfg;
            const Sid808VoiceConfig snapCfg = sid808SnareSnapConfig_(bodyCfg);
            recordInitialProgram_(drum, snapCfg);
            applySid808VoiceConfigToFixedVoice_(fixedVoice,
                                                drum,
                                                snapCfg,
                                                std::clamp(appliedLevel * 1.12f, 0.0f, 1.0f),
                                                true);
            armSnareBodyStage_(fixedVoice,
                                bodyCfg,
                                std::clamp(appliedLevel * 0.82f, 0.0f, 1.0f));
        } else if (drum == Sid808Drum::Kick) {
            const Sid808VoiceConfig attackCfg = sid808KickAttackConfig_(cfg);
            recordInitialProgram_(drum, attackCfg);
            applySid808VoiceConfigToFixedVoice_(fixedVoice,
                                                drum,
                                                attackCfg,
                                                std::clamp(appliedLevel * 1.08f, 0.0f, 1.0f),
                                                true);
            armKickPitchSweep_(fixedVoice, cfg, appliedLevel);
        } else if (drum == Sid808Drum::OpenHat) {
            const Sid808VoiceConfig attackCfg = sid808OpenHatAttackConfig_(cfg, profile);
            recordInitialProgram_(drum, attackCfg);
            applySid808VoiceConfigToFixedVoice_(fixedVoice, drum, attackCfg, appliedLevel, true);
            armOpenHatRingProgram_(fixedVoice, cfg, appliedLevel, profile);
        } else if (drum == Sid808Drum::Clap) {
            const Sid808VoiceConfig attackCfg = sid808ClapBurstConfig_(cfg, 0u);
            recordInitialProgram_(drum, attackCfg);
            applySid808VoiceConfigToFixedVoice_(fixedVoice, drum, attackCfg, appliedLevel, true);
            armClapBurstProgram_(fixedVoice, cfg, appliedLevel);
        } else if (drum == Sid808Drum::Cowbell) {
            if (sid808IsCowbellFamilyProfile_(profile)) {
                // RideBell / Agogo / MuteTriangle: distinct bell/pulse voice instead
                // of the generic two-partial cowbell (v873 GM Cowbell family).
                const Sid808VoiceConfig attackCfg = sid808CowbellProfileConfig_(cfg, profile);
                recordInitialProgram_(drum, attackCfg);
                applySid808VoiceConfigToFixedVoice_(fixedVoice, drum, attackCfg, appliedLevel, true);
            } else {
                const Sid808VoiceConfig attackCfg = sid808CowbellPartialConfig_(cfg, 0u);
                recordInitialProgram_(drum, attackCfg);
                applySid808VoiceConfigToFixedVoice_(fixedVoice, drum, attackCfg, appliedLevel, true);
                armCowbellPartialProgram_(fixedVoice, cfg, appliedLevel);
            }
        } else if (drum == Sid808Drum::ClosedHat && sid808IsClosedHatFamilyProfile_(profile)) {
            // Tambourine / Shaker(Cabasa/Maracas) / GuiroShort / PedalHat: textured
            // closed-hat voice instead of the generic tick (v873 GM ClosedHat family).
            const Sid808VoiceConfig attackCfg = sid808ClosedHatAttackConfig_(cfg, profile);
            recordInitialProgram_(drum, attackCfg);
            applySid808VoiceConfigToFixedVoice_(fixedVoice, drum, attackCfg, appliedLevel, true);
        } else if (drum == Sid808Drum::Rim && sid808IsRimFamilyProfile_(profile)) {
            // Vibraslap / Claves / WoodBlock: woody/rattle voice at the projected
            // pitch instead of a max-frequency rim click (v873 GM Rim family).
            const Sid808VoiceConfig attackCfg = sid808RimProfileConfig_(cfg, profile);
            recordInitialProgram_(drum, attackCfg);
            applySid808VoiceConfigToFixedVoice_(fixedVoice, drum, attackCfg, appliedLevel, true);
        } else if (drum == Sid808Drum::Tom) {
            const Sid808VoiceConfig attackCfg = sid808TomAttackConfig_(cfg, profile);
            recordInitialProgram_(drum, attackCfg);
            applySid808VoiceConfigToFixedVoice_(fixedVoice,
                                                drum,
                                                attackCfg,
                                                std::clamp(appliedLevel * 1.04f, 0.0f, 1.0f),
                                                true);
            armTomPitchDrop_(fixedVoice, cfg, appliedLevel, profile);
        } else {
            recordInitialProgram_(drum, cfg);
            applySid808VoiceConfigToFixedVoice_(fixedVoice, drum, cfg, appliedLevel, true);
        }
        armAutoRelease_(fixedVoice, drum);

        voiceTelemetryLevels_[(size_t)fixedVoice] = std::max(voiceTelemetryLevels_[(size_t)fixedVoice], appliedLevel);
        drumTelemetryLevels_[(size_t)i]           = std::max(drumTelemetryLevels_[(size_t)i], appliedLevel);
        lastAppliedConfig_ = cfg;
        lastDrum_     = drum;
        lastMidiNote_ = (midiNoteHint > 0u && midiNoteHint <= 127u)
                        ? midiNoteHint : static_cast<int>(sid808CanonicalMidiNoteForDrum(drum));
        lastVelocity_ = velocityScale;
        lastVoice_    = fixedVoice;
        lastPercProfile_ = profile;
        ++noteOnCount_;
        return fixedVoice;
    }

    static Sid808VoiceConfig sanitizeRealtimeDrumConfig_(Sid808Drum drum,
                                                         Sid808VoiceConfig cfg) noexcept {
        cfg.waveform = sid808NormalizeWaveformControl(cfg.waveform);
        if (!sid808WaveformHasRenderableBits(cfg.waveform) && cfg.voiceLevel > 0.0f) {
            cfg.waveform = sid808DefaultConfig(drum).waveform;
        }
        if (drum == Sid808Drum::Snare) {
            cfg.waveform = static_cast<std::uint8_t>(cfg.waveform | 0x80u);
            cfg.flags = static_cast<std::uint8_t>(cfg.flags | Sid808Detail::kFlagFilter);
            cfg.sustainRelease = static_cast<std::uint8_t>(cfg.sustainRelease & 0x0Fu);
        }
        return cfg;
    }

    static Sid808VoiceConfig sid808SnareSnapConfig_(Sid808VoiceConfig body) noexcept {
        Sid808VoiceConfig snap = body;
        snap.freq = static_cast<std::uint16_t>(std::max<int>(static_cast<int>(body.freq), 0x5800));
        snap.pulseWidth = 0u;
        snap.waveform = 0x80u;
        snap.attackDecay = 0x00u;
        snap.sustainRelease = 0x02u;
        snap.flags = static_cast<std::uint8_t>(body.flags | Sid808Detail::kFlagFilter);
        snap.voiceLevel = std::clamp(body.voiceLevel * 1.12f, 0.0f, 1.0f);
        return snap;
    }

    static std::uint16_t sid808ScaledFrequency_(std::uint16_t base, float ratio) noexcept {
        const float cleanRatio = std::clamp(std::isfinite(ratio) ? ratio : 1.0f, 0.125f, 8.0f);
        const int scaled = static_cast<int>(std::lround(static_cast<float>(base) * cleanRatio));
        return clampSidFreqReg_(std::clamp(scaled, 1, 65535));
    }

    static std::uint16_t sid808ScaledPulseWidth_(std::uint16_t base,
                                                 float ratio,
                                                 std::uint16_t fallback) noexcept {
        std::uint16_t cleanBase = static_cast<std::uint16_t>(base & 0x0FFFu);
        if (cleanBase == 0u) cleanBase = static_cast<std::uint16_t>(fallback & 0x0FFFu);
        const float cleanRatio = std::clamp(std::isfinite(ratio) ? ratio : 1.0f, 0.125f, 8.0f);
        const int scaled = static_cast<int>(std::lround(static_cast<float>(cleanBase) * cleanRatio));
        return static_cast<std::uint16_t>(std::clamp(scaled, 0, 0x0FFF));
    }

    static std::uint8_t sid808ScaledAttackDecay_(std::uint8_t base,
                                                 float decayScale,
                                                 int attackDelta = 0) noexcept {
        const int attack = std::clamp<int>(((base >> 4) & 0x0F) + attackDelta, 0, 15);
        const int decay = std::clamp<int>(static_cast<int>(std::lround(
            static_cast<float>(base & 0x0F) * std::clamp(decayScale, 0.0f, 4.0f))), 0, 15);
        return static_cast<std::uint8_t>((attack << 4) | decay);
    }

    static std::uint8_t sid808ScaledAttackDecayWithFloor_(std::uint8_t base,
                                                          float decayScale,
                                                          int decayFloor,
                                                          int attackDelta = 0) noexcept {
        const std::uint8_t scaled = sid808ScaledAttackDecay_(base, decayScale, attackDelta);
        const int attack = (scaled >> 4) & 0x0F;
        const int decay = std::max<int>(scaled & 0x0F, std::clamp(decayFloor, 0, 15));
        return static_cast<std::uint8_t>((attack << 4) | decay);
    }

    static std::uint8_t sid808ScaledSustainRelease_(std::uint8_t base,
                                                    float releaseScale,
                                                    int sustainDelta = 0) noexcept {
        const int sustain = std::clamp<int>(((base >> 4) & 0x0F) + sustainDelta, 0, 15);
        const int release = std::clamp<int>(static_cast<int>(std::lround(
            static_cast<float>(base & 0x0F) * std::clamp(releaseScale, 0.0f, 4.0f))), 0, 15);
        return static_cast<std::uint8_t>((sustain << 4) | release);
    }

    static bool sid808ProfileAllowsRawMidiPitch_(Sid808PercProfile profile) noexcept {
        switch (profile) {
            case Sid808PercProfile::Default:
            case Sid808PercProfile::TomLow:
            case Sid808PercProfile::TomMid:
            case Sid808PercProfile::TomHigh:
                return true;
            default:
                return false;
        }
    }

    static std::uint8_t sid808TomProfileWaveform_(Sid808PercProfile profile) noexcept {
        switch (profile) {
            case Sid808PercProfile::CongaMute:   return 0x40u;
            case Sid808PercProfile::CongaOpen:
            case Sid808PercProfile::CongaLow:    return 0x50u;
            case Sid808PercProfile::TimbaleHigh:
            case Sid808PercProfile::TimbaleLow:  return 0x40u;
            case Sid808PercProfile::CuicaMute:
            case Sid808PercProfile::CuicaOpen:   return 0x20u;
            default:                             return 0x10u;
        }
    }

    static std::uint16_t sid808TomProfilePulseWidth_(Sid808VoiceConfig base,
                                                     Sid808PercProfile profile) noexcept {
        switch (profile) {
            case Sid808PercProfile::CongaMute:   return std::max<std::uint16_t>(base.pulseWidth, 0x0350u);
            case Sid808PercProfile::CongaOpen:
            case Sid808PercProfile::CongaLow:    return std::max<std::uint16_t>(base.pulseWidth, 0x0550u);
            case Sid808PercProfile::TimbaleHigh:
            case Sid808PercProfile::TimbaleLow:  return std::max<std::uint16_t>(base.pulseWidth, 0x0280u);
            default:                             return 0u;
        }
    }

    static std::uint8_t sid808OpenHatProfileWaveform_(Sid808PercProfile profile) noexcept {
        switch (profile) {
            case Sid808PercProfile::Triangle:    return 0x40u;
            case Sid808PercProfile::WhistleShort:
            case Sid808PercProfile::WhistleLong: return 0x20u;
            default:                             return 0x80u;
        }
    }

    static Sid808VoiceConfig sid808KickAttackConfig_(Sid808VoiceConfig base) noexcept {
        base.freq = sid808ScaledFrequency_(base.freq, 2.85f);
        base.waveform = 0x10u;
        base.attackDecay = sid808ScaledAttackDecay_(base.attackDecay, 0.70f);
        base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, 1.60f, 2);
        return base;
    }

    static Sid808VoiceConfig sid808TomAttackConfig_(Sid808VoiceConfig base,
                                                    Sid808PercProfile profile) noexcept {
        float ratio = 1.90f;
        float releaseScale = 1.25f;
        switch (profile) {
            case Sid808PercProfile::TomLow:      ratio = 1.72f; releaseScale = 1.35f; break;
            case Sid808PercProfile::TomHigh:     ratio = 2.08f; releaseScale = 1.00f; break;
            case Sid808PercProfile::BongoHigh:   ratio = 1.18f; releaseScale = 0.62f; break;
            case Sid808PercProfile::BongoLow:    ratio = 1.08f; releaseScale = 0.72f; break;
            case Sid808PercProfile::CongaMute:   ratio = 1.04f; releaseScale = 0.52f; break;
            case Sid808PercProfile::CongaOpen:   ratio = 1.00f; releaseScale = 0.95f; break;
            case Sid808PercProfile::CongaLow:    ratio = 0.92f; releaseScale = 1.08f; break;
            case Sid808PercProfile::TimbaleHigh: ratio = 1.26f; releaseScale = 0.58f; break;
            case Sid808PercProfile::TimbaleLow:  ratio = 1.16f; releaseScale = 0.66f; break;
            case Sid808PercProfile::CuicaMute:   ratio = 0.78f; releaseScale = 0.55f; break;
            case Sid808PercProfile::CuicaOpen:   ratio = 0.84f; releaseScale = 0.70f; break;
            default: break;
        }
        base.freq = sid808ScaledFrequency_(base.freq, ratio);
        base.waveform = sid808TomProfileWaveform_(profile);
        base.pulseWidth = sid808TomProfilePulseWidth_(base, profile);
        base.attackDecay = sid808ScaledAttackDecay_(base.attackDecay, 0.95f);
        base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, releaseScale);
        return base;
    }

    static Sid808VoiceConfig sid808OpenHatAttackConfig_(Sid808VoiceConfig base,
                                                        Sid808PercProfile profile) noexcept {
        float ratio = 1.23f;
        float releaseScale = 1.60f;
        switch (profile) {
            case Sid808PercProfile::Crash:       ratio = 1.12f; releaseScale = 2.25f; break;
            case Sid808PercProfile::Ride:        ratio = 0.82f; releaseScale = 1.75f; break;
            case Sid808PercProfile::Splash:      ratio = 1.30f; releaseScale = 1.15f; break;
            case Sid808PercProfile::China:       ratio = 1.44f; releaseScale = 1.85f; break;
            case Sid808PercProfile::Triangle:    ratio = 0.92f; releaseScale = 2.00f; break;
            case Sid808PercProfile::WhistleShort: ratio = 0.72f; releaseScale = 0.80f; break;
            case Sid808PercProfile::WhistleLong: ratio = 0.82f; releaseScale = 1.25f; break;
            case Sid808PercProfile::GuiroLong:   ratio = 0.76f; releaseScale = 1.05f; break;
            default: break;
        }
        base.freq = sid808ScaledFrequency_(base.freq, ratio);
        base.waveform = sid808OpenHatProfileWaveform_(profile);
        base.attackDecay = sid808ScaledAttackDecay_(base.attackDecay, 0.35f);
        base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, releaseScale, 3);
        if (base.waveform == 0x80u)
            base.flags = static_cast<std::uint8_t>(base.flags | Sid808Detail::kFlagFilter);
        return base;
    }

    static Sid808VoiceConfig sid808ClapBurstConfig_(Sid808VoiceConfig base,
                                                    std::uint8_t stage) noexcept {
        base.waveform = 0x80u;
        base.pulseWidth = 0u;
        base.flags = static_cast<std::uint8_t>(base.flags & ~Sid808Detail::kFlagFilter);
        switch (stage) {
            case 0u:
                base.freq = sid808ScaledFrequency_(base.freq, 1.53f);
                base.attackDecay = sid808ScaledAttackDecayWithFloor_(base.attackDecay, 0.45f, 0);
                base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, 0.70f, 1);
                break;
            case 1u:
                base.freq = sid808ScaledFrequency_(base.freq, 1.41f);
                base.attackDecay = sid808ScaledAttackDecayWithFloor_(base.attackDecay, 0.50f, 0);
                base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, 0.70f, 1);
                break;
            case 2u:
                base.freq = sid808ScaledFrequency_(base.freq, 1.23f);
                base.attackDecay = sid808ScaledAttackDecayWithFloor_(base.attackDecay, 0.65f, 0);
                base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, 1.00f, 1);
                break;
            default:
                base.freq = sid808ScaledFrequency_(base.freq, 1.03f);
                base.attackDecay = sid808ScaledAttackDecayWithFloor_(base.attackDecay, 0.85f, 1);
                base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, 2.00f, 4);
                break;
        }
        return base;
    }

    static Sid808VoiceConfig sid808CowbellPartialConfig_(Sid808VoiceConfig base,
                                                         std::uint8_t stage) noexcept {
        base.waveform = 0x40u;
        base.flags = static_cast<std::uint8_t>(base.flags | Sid808Detail::kFlagFilter);
        switch (stage) {
            case 0u:
                base.freq = sid808ScaledFrequency_(base.freq, 1.00f);
                base.pulseWidth = sid808ScaledPulseWidth_(base.pulseWidth, 1.00f, 0x0450u);
                base.attackDecay = sid808ScaledAttackDecayWithFloor_(base.attackDecay, 0.50f, 0);
                base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, 1.75f, 8);
                break;
            case 1u:
                base.freq = sid808ScaledFrequency_(base.freq, 1.48f);
                base.pulseWidth = sid808ScaledPulseWidth_(base.pulseWidth, 1.98f, 0x08A0u);
                base.attackDecay = sid808ScaledAttackDecayWithFloor_(base.attackDecay, 0.50f, 0);
                base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, 1.75f, 8);
                break;
            case 2u:
                base.freq = sid808ScaledFrequency_(base.freq, 0.99f);
                base.pulseWidth = sid808ScaledPulseWidth_(base.pulseWidth, 1.16f, 0x0520u);
                base.attackDecay = sid808ScaledAttackDecayWithFloor_(base.attackDecay, 0.70f, 1);
                base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, 1.50f, 9);
                break;
            default:
                base.freq = sid808ScaledFrequency_(base.freq, 1.51f);
                base.pulseWidth = sid808ScaledPulseWidth_(base.pulseWidth, 1.35f, 0x0880u);
                base.attackDecay = sid808ScaledAttackDecayWithFloor_(base.attackDecay, 0.85f, 2);
                base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, 1.15f, 4);
                break;
        }
        return base;
    }

    // ── v873 GM family voices ───────────────────────────────────────────────
    // ClosedHat-, Cowbell- and Rim-derived GM percussion previously fell through
    // to the generic voice for their engine drum (a single tick / cowbell partial
    // / max-freq rim click). These helpers give each family profile a distinct
    // waveform + envelope so the GM map's Tambourine/Shaker/Guiro/PedalHat,
    // RideBell/Agogo/MuteTriangle and Vibraslap/Claves/WoodBlock actually sound
    // like themselves. Frequency here is a modest per-profile trim; the primary
    // pitch differentiation already came from the projection layer's freq ratio.
    static bool sid808IsClosedHatFamilyProfile_(Sid808PercProfile p) noexcept {
        return p == Sid808PercProfile::Tambourine || p == Sid808PercProfile::Shaker ||
               p == Sid808PercProfile::GuiroShort || p == Sid808PercProfile::PedalHat;
    }
    static bool sid808IsCowbellFamilyProfile_(Sid808PercProfile p) noexcept {
        return p == Sid808PercProfile::RideBell || p == Sid808PercProfile::AgogoHigh ||
               p == Sid808PercProfile::AgogoLow || p == Sid808PercProfile::TriangleMute;
    }
    static bool sid808IsRimFamilyProfile_(Sid808PercProfile p) noexcept {
        return p == Sid808PercProfile::Vibraslap || p == Sid808PercProfile::Claves ||
               p == Sid808PercProfile::WoodBlockHigh || p == Sid808PercProfile::WoodBlockLow;
    }

    static Sid808VoiceConfig sid808ClosedHatAttackConfig_(Sid808VoiceConfig base,
                                                          Sid808PercProfile profile) noexcept {
        float ratio = 1.0f, decayScale = 0.55f, releaseScale = 0.50f;
        switch (profile) {
            case Sid808PercProfile::Tambourine: ratio = 1.06f; decayScale = 1.05f; releaseScale = 1.35f; break; // jingling medium tail
            case Sid808PercProfile::Shaker:     ratio = 0.94f; decayScale = 0.70f; releaseScale = 0.62f; break; // soft scrape/shake
            case Sid808PercProfile::GuiroShort: ratio = 0.80f; decayScale = 0.42f; releaseScale = 0.38f; break; // short scrape transient
            case Sid808PercProfile::PedalHat:   ratio = 0.88f; decayScale = 0.38f; releaseScale = 0.34f; break; // shorter/duller than closed hat
            default: break;
        }
        base.freq = sid808ScaledFrequency_(base.freq, ratio);
        base.waveform = 0x80u; // noise texture
        base.attackDecay = sid808ScaledAttackDecay_(base.attackDecay, decayScale);
        base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, releaseScale, 1);
        base.flags = static_cast<std::uint8_t>(base.flags | Sid808Detail::kFlagFilter);
        return base;
    }

    static Sid808VoiceConfig sid808CowbellProfileConfig_(Sid808VoiceConfig base,
                                                         Sid808PercProfile profile) noexcept {
        float ratio = 1.0f, decayScale = 0.60f, releaseScale = 1.20f;
        std::uint8_t waveform = 0x40u; // pulse
        switch (profile) {
            case Sid808PercProfile::RideBell:     ratio = 1.50f; decayScale = 0.85f; releaseScale = 1.90f; break; // bright, long bell
            case Sid808PercProfile::AgogoHigh:    ratio = 1.88f; decayScale = 0.55f; releaseScale = 0.85f; break; // high pitched pulse
            case Sid808PercProfile::AgogoLow:     ratio = 1.56f; decayScale = 0.55f; releaseScale = 0.92f; break;
            case Sid808PercProfile::TriangleMute: ratio = 0.70f; decayScale = 0.50f; releaseScale = 0.72f; waveform = 0x10u; break; // damped triangle
            default: break;
        }
        base.freq = sid808ScaledFrequency_(base.freq, ratio);
        base.waveform = waveform;
        base.pulseWidth = sid808ScaledPulseWidth_(base.pulseWidth, 1.0f, 0x0450u);
        base.attackDecay = sid808ScaledAttackDecay_(base.attackDecay, decayScale);
        base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, releaseScale, 6);
        base.flags = static_cast<std::uint8_t>(base.flags | Sid808Detail::kFlagFilter);
        return base;
    }

    static Sid808VoiceConfig sid808RimProfileConfig_(Sid808VoiceConfig base,
                                                     Sid808PercProfile profile) noexcept {
        float ratio = 1.0f, decayScale = 0.45f, releaseScale = 0.40f;
        std::uint8_t waveform = 0x40u; // woody pulse
        std::uint16_t pw = 0x0800u;
        switch (profile) {
            case Sid808PercProfile::Claves:        ratio = 1.34f; pw = 0x0700u; decayScale = 0.35f; releaseScale = 0.30f; break; // tight woody click
            case Sid808PercProfile::WoodBlockHigh: ratio = 1.44f; pw = 0x0680u; decayScale = 0.40f; releaseScale = 0.35f; break;
            case Sid808PercProfile::WoodBlockLow:  ratio = 1.14f; pw = 0x0780u; decayScale = 0.45f; releaseScale = 0.40f; break;
            case Sid808PercProfile::Vibraslap:     ratio = 1.10f; waveform = 0x80u; decayScale = 1.25f; releaseScale = 1.45f; break; // rattly noise sustain
            default: break;
        }
        base.freq = sid808ScaledFrequency_(base.freq, ratio);
        base.waveform = waveform;
        if (waveform == 0x40u) base.pulseWidth = sid808ScaledPulseWidth_(pw, 1.0f, 0x0800u);
        base.attackDecay = sid808ScaledAttackDecay_(base.attackDecay, decayScale);
        base.sustainRelease = sid808ScaledSustainRelease_(base.sustainRelease, releaseScale);
        return base;
    }

    static int sid808SamplesForMilliseconds_(double sampleRate, double ms) noexcept {
        const double sr = std::isfinite(sampleRate) ? std::max(1.0, sampleRate) : 44100.0;
        return ms > 0.0 ? std::max(1, static_cast<int>(std::lround(sr * (ms / 1000.0)))) : -1;
    }

    void applySid808VoiceConfigToFixedVoice_(std::uint8_t fixedVoice,
                                             Sid808Drum drum,
                                             Sid808VoiceConfig cfg,
                                             float appliedLevel,
                                             bool raiseGate) noexcept {
        if (fixedVoice >= 3u) return;
        cfg = sanitizeRealtimeDrumConfig_(drum, cfg);
        SIDVoice& v = sidEngine_.chip().getVoice(fixedVoice);
        if (raiseGate) v.setGate(false);
        // A10: Kick pitch sweep — reset to high freq; decay will sweep it down naturally
        // via SID's own envelope acting on the filter (authentic x0x behaviour).
        v.setFrequency(cfg.freq);
        v.setPulseWidth(cfg.pulseWidth);
        // Convert real SID control waveform bits (0x10/0x20/0x40/0x80) into the
        // internal Waveform nibble (1/2/4/8).
        v.setWaveform(sidControlWaveformToInternal_(cfg.waveform));
        sidEngine_.chip().setVoiceRingModEnable(fixedVoice, (cfg.flags & Sid808Detail::kFlagRingMod) != 0u);
        sidEngine_.chip().setVoiceSyncEnable(fixedVoice, (cfg.flags & Sid808Detail::kFlagHardSync) != 0u);
        applyFilterRoutingAndShape_(fixedVoice, drum, cfg);
        v.setAttack((cfg.attackDecay >> 4) & 0x0Fu);
        v.setDecay( cfg.attackDecay        & 0x0Fu);
        v.setSustain((cfg.sustainRelease >> 4) & 0x0Fu);
        v.setRelease( cfg.sustainRelease   & 0x0Fu);
        sidEngine_.chip().setVoiceLevel(fixedVoice,
                                        std::clamp(std::isfinite(appliedLevel) ? appliedLevel : 0.0f,
                                                   0.0f,
                                                   1.0f));
        if (raiseGate) v.setGate(true);
    }

    void applyFilterRoutingAndShape_(std::uint8_t fixedVoice,
                                     Sid808Drum drum,
                                     const Sid808VoiceConfig& cfg) noexcept {
        const FilterMode currentMode = sid808FilterModeForDrum_(drum, cfg);
        activeDrumByVoice_[(size_t)fixedVoice] = drum;
        activeFlagsByVoice_[(size_t)fixedVoice] = cfg.flags;
        activeFilterModeByVoice_[(size_t)fixedVoice] = currentMode;

        bool route[3] = {false, false, false};
        for (std::uint8_t voice = 0u; voice < 3u; ++voice) {
            route[voice] =
                activeDrumByVoice_[(size_t)voice] != Sid808Drum::Count &&
                activeFilterModeByVoice_[(size_t)voice] != FilterMode::None &&
                (activeFlagsByVoice_[(size_t)voice] & Sid808Detail::kFlagFilter) != 0u;
        }
        sidEngine_.chip().setFilterVoiceRouting(route[0], route[1], route[2]);

        FilterMode modeToApply = currentMode;
        if (modeToApply == FilterMode::None) {
            for (FilterMode activeMode : activeFilterModeByVoice_) {
                if (activeMode != FilterMode::None) {
                    modeToApply = activeMode;
                    break;
                }
            }
        }

        Sid808Drum shapeDrum = drum;
        if (currentMode == FilterMode::None) {
            for (std::size_t v = 0; v < activeFilterModeByVoice_.size(); ++v) {
                if (activeFilterModeByVoice_[v] != FilterMode::None) {
                    shapeDrum = activeDrumByVoice_[v];
                    break;
                }
            }
        }

        if (modeToApply == FilterMode::BpHp) {
            sidEngine_.setFilterCutoff(0x0680u);
            sidEngine_.setFilterResonance(0x0Cu);
            sidEngine_.setFilterMode(FilterMode::BpHp);
        } else if (modeToApply == FilterMode::HighPass) {
            sidEngine_.setFilterCutoff(0x0980u);
            sidEngine_.setFilterResonance(0x06u);
            sidEngine_.setFilterMode(FilterMode::HighPass);
        } else if (modeToApply == FilterMode::BandPass) {
            if (shapeDrum == Sid808Drum::Cowbell) {
                sidEngine_.setFilterCutoff(0x0780u);
                sidEngine_.setFilterResonance(0x08u);
            } else {
                sidEngine_.setFilterCutoff(0x0880u);
                sidEngine_.setFilterResonance(0x05u);
            }
            sidEngine_.setFilterMode(FilterMode::BandPass);
        } else {
            sidEngine_.setFilterMode(modeToApply);
        }
    }

    static FilterMode sid808FilterModeForDrum_(Sid808Drum drum,
                                               const Sid808VoiceConfig& cfg) noexcept {
        if (drum == Sid808Drum::Snare) return FilterMode::BpHp;
        if ((cfg.flags & Sid808Detail::kFlagFilter) == 0u) return FilterMode::None;
        if (drum == Sid808Drum::ClosedHat || drum == Sid808Drum::OpenHat) return FilterMode::HighPass;
        if (drum == Sid808Drum::Cowbell) return FilterMode::BandPass;
        return (cfg.flags & Sid808Detail::kFlagFilter) != 0u
            ? FilterMode::BandPass
            : FilterMode::None;
    }

    void clearActiveVoiceState_(std::uint8_t voice, Sid808Drum expected = Sid808Drum::Count) noexcept {
        if (voice >= 3u) return;
        if (expected != Sid808Drum::Count && activeDrumByVoice_[(size_t)voice] != expected) return;
        activeDrumByVoice_[(size_t)voice] = Sid808Drum::Count;
        activeFlagsByVoice_[(size_t)voice] = 0u;
        activeFilterModeByVoice_[(size_t)voice] = FilterMode::None;
        bool route[3] = {false, false, false};
        FilterMode modeToApply = FilterMode::None;
        for (std::uint8_t v = 0u; v < 3u; ++v) {
            route[v] =
                activeDrumByVoice_[(size_t)v] != Sid808Drum::Count &&
                activeFilterModeByVoice_[(size_t)v] != FilterMode::None &&
                (activeFlagsByVoice_[(size_t)v] & Sid808Detail::kFlagFilter) != 0u;
            if (modeToApply == FilterMode::None && activeFilterModeByVoice_[(size_t)v] != FilterMode::None) {
                modeToApply = activeFilterModeByVoice_[(size_t)v];
            }
        }
        sidEngine_.chip().setFilterVoiceRouting(route[0], route[1], route[2]);
        sidEngine_.setFilterMode(modeToApply);
    }

    void clearMicroStage_(std::uint8_t voice) noexcept {
        if (voice >= microStages_.size()) return;
        for (auto& stage : microStages_[voice]) stage = Sid808MicroStage{};
    }

    void clearMicroStageSlot_(std::uint8_t voice, std::size_t slot) noexcept {
        if (voice >= microStages_.size() || slot >= kMicroStageProgramCapacity_) return;
        microStages_[voice][slot] = Sid808MicroStage{};
    }

    void armMicroStage_(std::uint8_t voice,
                        std::size_t slot,
                        Sid808Drum drum,
                        double delayMs,
                        Sid808VoiceConfig cfg,
                        float appliedLevel,
                        bool raiseGate) noexcept {
        if (voice >= microStages_.size() || slot >= kMicroStageProgramCapacity_) return;
        cfg = sanitizeRealtimeDrumConfig_(drum, cfg);
        Sid808MicroStage stage{};
        stage.samples = sid808SamplesForMilliseconds_(sampleRate_, delayMs);
        stage.drum = drum;
        stage.config = cfg;
        stage.level = std::clamp(std::isfinite(appliedLevel) ? appliedLevel : 0.0f, 0.0f, 1.0f);
        stage.raiseGate = raiseGate;
        microStages_[voice][slot] = stage;
    }

    void recordInitialProgram_(Sid808Drum drum, Sid808VoiceConfig cfg) noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        if (i >= static_cast<std::uint8_t>(Sid808Drum::Count)) return;
        cfg = sanitizeRealtimeDrumConfig_(drum, cfg);
        lastInitialFreqByDrum_[(size_t)i] = cfg.freq;
        lastMicroStageFreqByDrum_[(size_t)i] = 0u;
        lastMicroStageConfigByDrum_[(size_t)i] = Sid808VoiceConfig{};
        lastMicroStageWaveformByDrum_[(size_t)i] = 0u;
        lastMicroStageGateByDrum_[(size_t)i] = false;
    }

    void recordAppliedMicroStage_(std::size_t voice,
                                  Sid808Drum drum,
                                  const Sid808VoiceConfig& cfg,
                                  float level,
                                  bool raiseGate) noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        if (i >= static_cast<std::uint8_t>(Sid808Drum::Count)) return;
        ++microStageAppliedCountByDrum_[(size_t)i];
        lastMicroStageFreqByDrum_[(size_t)i] = cfg.freq;
        lastMicroStageConfigByDrum_[(size_t)i] = cfg;
        lastMicroStageWaveformByDrum_[(size_t)i] = cfg.waveform;
        lastMicroStageGateByDrum_[(size_t)i] = raiseGate;
        if (voice < voiceTelemetryLevels_.size()) {
            voiceTelemetryLevels_[voice] = std::max(voiceTelemetryLevels_[voice], level);
        }
        drumTelemetryLevels_[(size_t)i] = std::max(drumTelemetryLevels_[(size_t)i], level);
    }

    void armKickPitchSweep_(std::uint8_t voice,
                            Sid808VoiceConfig baseCfg,
                            float appliedLevel) noexcept {
        Sid808VoiceConfig punch = baseCfg;
        punch.freq = sid808ScaledFrequency_(baseCfg.freq, 1.52f);
        punch.waveform = 0x10u;
        punch.pulseWidth = 0u;
        punch.attackDecay = sid808ScaledAttackDecay_(baseCfg.attackDecay, 0.85f);
        punch.sustainRelease = sid808ScaledSustainRelease_(baseCfg.sustainRelease, 1.55f, 2);
        armMicroStage_(voice, 0u, Sid808Drum::Kick, 4.5, punch, appliedLevel * 0.92f, false);

        Sid808VoiceConfig body = baseCfg;
        body.freq = sid808ScaledFrequency_(baseCfg.freq, 0.88f);
        body.waveform = 0x10u;
        body.pulseWidth = 0u;
        body.attackDecay = sid808ScaledAttackDecay_(baseCfg.attackDecay, 1.00f);
        body.sustainRelease = sid808ScaledSustainRelease_(baseCfg.sustainRelease, 1.20f, 1);
        armMicroStage_(voice, 1u, Sid808Drum::Kick, 16.0, body, appliedLevel * 0.72f, false);

        Sid808VoiceConfig tail = baseCfg;
        tail.freq = sid808ScaledFrequency_(baseCfg.freq, 0.66f);
        tail.waveform = 0x10u;
        tail.pulseWidth = 0u;
        tail.attackDecay = sid808ScaledAttackDecay_(baseCfg.attackDecay, 1.15f);
        tail.sustainRelease = sid808ScaledSustainRelease_(baseCfg.sustainRelease, 0.85f);
        armMicroStage_(voice, 2u, Sid808Drum::Kick, 46.0, tail, appliedLevel * 0.44f, false);
    }

    void armOpenHatRingProgram_(std::uint8_t voice,
                                Sid808VoiceConfig baseCfg,
                                float appliedLevel,
                                Sid808PercProfile profile) noexcept {
        float r0 = 1.23f, r1 = 1.04f, r2 = 0.86f;
        double t0 = 24.0, t1 = 118.0, t2 = 250.0;
        float sr0 = 1.55f, sr1 = 1.15f, sr2 = 0.78f;
        switch (profile) {
            case Sid808PercProfile::Crash:   r0 = 1.18f; r1 = 0.88f; r2 = 0.62f; t0 = 30.0; t1 = 170.0; t2 = 360.0; sr0 = 2.30f; sr1 = 1.95f; sr2 = 1.40f; break;
            case Sid808PercProfile::Ride:    r0 = 0.96f; r1 = 0.76f; r2 = 0.58f; t0 = 42.0; t1 = 220.0; t2 = 420.0; sr0 = 1.90f; sr1 = 1.55f; sr2 = 1.10f; break;
            case Sid808PercProfile::Splash:  r0 = 1.36f; r1 = 1.04f; r2 = 0.74f; t0 = 18.0; t1 = 82.0;  t2 = 165.0; sr0 = 1.20f; sr1 = 0.92f; sr2 = 0.65f; break;
            case Sid808PercProfile::China:   r0 = 1.44f; r1 = 0.82f; r2 = 0.54f; t0 = 26.0; t1 = 130.0; t2 = 300.0; sr0 = 1.80f; sr1 = 1.45f; sr2 = 0.95f; break;
            case Sid808PercProfile::Triangle: r0 = 1.00f; r1 = 0.92f; r2 = 0.84f; t0 = 36.0; t1 = 180.0; t2 = 390.0; sr0 = 2.10f; sr1 = 1.80f; sr2 = 1.35f; break;
            case Sid808PercProfile::WhistleShort: r0 = 1.00f; r1 = 1.12f; r2 = 1.24f; t0 = 28.0; t1 = 90.0; t2 = 150.0; sr0 = 0.75f; sr1 = 0.70f; sr2 = 0.55f; break;
            case Sid808PercProfile::WhistleLong: r0 = 1.00f; r1 = 1.10f; r2 = 1.18f; t0 = 42.0; t1 = 150.0; t2 = 290.0; sr0 = 1.15f; sr1 = 1.00f; sr2 = 0.80f; break;
            case Sid808PercProfile::GuiroLong: r0 = 1.08f; r1 = 0.86f; r2 = 0.72f; t0 = 22.0; t1 = 105.0; t2 = 220.0; sr0 = 1.05f; sr1 = 0.88f; sr2 = 0.62f; break;
            default: break;
        }
        const std::uint8_t wave = sid808OpenHatProfileWaveform_(profile);
        Sid808VoiceConfig shimmer = baseCfg;
        shimmer.freq = sid808ScaledFrequency_(baseCfg.freq, r0);
        shimmer.waveform = wave;
        shimmer.attackDecay = sid808ScaledAttackDecay_(baseCfg.attackDecay, 0.45f);
        shimmer.sustainRelease = sid808ScaledSustainRelease_(baseCfg.sustainRelease, sr0, 3);
        if (wave == 0x80u) shimmer.flags = static_cast<std::uint8_t>(shimmer.flags | Sid808Detail::kFlagFilter);
        armMicroStage_(voice, 0u, Sid808Drum::OpenHat, t0, shimmer, appliedLevel * 0.74f, false);

        Sid808VoiceConfig ring = shimmer;
        ring.freq = sid808ScaledFrequency_(baseCfg.freq, r1);
        ring.sustainRelease = sid808ScaledSustainRelease_(baseCfg.sustainRelease, sr1, 2);
        armMicroStage_(voice, 1u, Sid808Drum::OpenHat, t1, ring, appliedLevel * 0.46f, false);

        Sid808VoiceConfig air = shimmer;
        air.freq = sid808ScaledFrequency_(baseCfg.freq, r2);
        air.sustainRelease = sid808ScaledSustainRelease_(baseCfg.sustainRelease, sr2, 1);
        armMicroStage_(voice, 2u, Sid808Drum::OpenHat, t2, air, appliedLevel * 0.24f, false);
    }

    void armClapBurstProgram_(std::uint8_t voice,
                              Sid808VoiceConfig baseCfg,
                              float appliedLevel) noexcept {
        armMicroStage_(voice,
                       0u,
                       Sid808Drum::Clap,
                       3.2,
                       sid808ClapBurstConfig_(baseCfg, 1u),
                       appliedLevel * 0.96f,
                       true);
        armMicroStage_(voice,
                       1u,
                       Sid808Drum::Clap,
                       6.6,
                       sid808ClapBurstConfig_(baseCfg, 2u),
                       appliedLevel * 0.86f,
                       true);
        armMicroStage_(voice,
                       2u,
                       Sid808Drum::Clap,
                       10.5,
                       sid808ClapBurstConfig_(baseCfg, 3u),
                       appliedLevel * 0.62f,
                       true);
    }

    void armCowbellPartialProgram_(std::uint8_t voice,
                                   Sid808VoiceConfig baseCfg,
                                   float appliedLevel) noexcept {
        armMicroStage_(voice,
                       0u,
                       Sid808Drum::Cowbell,
                       2.4,
                       sid808CowbellPartialConfig_(baseCfg, 1u),
                       appliedLevel * 0.96f,
                       true);
        armMicroStage_(voice,
                       1u,
                       Sid808Drum::Cowbell,
                       5.2,
                       sid808CowbellPartialConfig_(baseCfg, 2u),
                       appliedLevel * 0.78f,
                       true);
        armMicroStage_(voice,
                       2u,
                       Sid808Drum::Cowbell,
                       18.0,
                       sid808CowbellPartialConfig_(baseCfg, 3u),
                       appliedLevel * 0.22f,
                       false);
    }

    void armTomPitchDrop_(std::uint8_t voice,
                          Sid808VoiceConfig baseCfg,
                          float appliedLevel,
                          Sid808PercProfile profile) noexcept {
        // v909 tom improvement: deeper three-stage pitch drop with longer stage
        // spacing and a stronger sustained tail, matching the 808's singing
        // "doo" instead of a fast pitch thud. Mid tom is the default profile.
        float r0 = 1.22f, r1 = 0.72f, r2 = 0.54f;
        double t0 = 18.0, t1 = 70.0, t2 = 145.0;
        float sr0 = 1.18f, sr1 = 0.94f, sr2 = 0.66f;
        switch (profile) {
            case Sid808PercProfile::TomLow:      r0 = 1.16f; r1 = 0.68f; r2 = 0.48f; t0 = 22.0; t1 = 80.0; t2 = 170.0; sr0 = 1.28f; sr1 = 1.04f; sr2 = 0.78f; break;
            case Sid808PercProfile::TomHigh:     r0 = 1.28f; r1 = 0.82f; r2 = 0.64f; t0 = 14.0; t1 = 52.0; t2 = 104.0; sr0 = 1.00f; sr1 = 0.78f; sr2 = 0.54f; break;
            case Sid808PercProfile::BongoHigh:   r0 = 1.08f; r1 = 0.96f; r2 = 0.84f; t0 = 7.5;  t1 = 22.0; t2 = 44.0;  sr0 = 0.55f; sr1 = 0.46f; sr2 = 0.34f; break;
            case Sid808PercProfile::BongoLow:    r0 = 1.04f; r1 = 0.92f; r2 = 0.78f; t0 = 9.0;  t1 = 28.0; t2 = 56.0;  sr0 = 0.64f; sr1 = 0.52f; sr2 = 0.40f; break;
            case Sid808PercProfile::CongaMute:   r0 = 1.02f; r1 = 0.90f; r2 = 0.80f; t0 = 8.0;  t1 = 24.0; t2 = 46.0;  sr0 = 0.48f; sr1 = 0.40f; sr2 = 0.28f; break;
            case Sid808PercProfile::CongaOpen:   r0 = 1.00f; r1 = 0.86f; r2 = 0.72f; t0 = 13.0; t1 = 52.0; t2 = 116.0; sr0 = 0.92f; sr1 = 0.82f; sr2 = 0.62f; break;
            case Sid808PercProfile::CongaLow:    r0 = 0.96f; r1 = 0.78f; r2 = 0.62f; t0 = 15.0; t1 = 58.0; t2 = 130.0; sr0 = 1.05f; sr1 = 0.90f; sr2 = 0.72f; break;
            case Sid808PercProfile::TimbaleHigh: r0 = 1.18f; r1 = 1.06f; r2 = 0.92f; t0 = 6.0;  t1 = 18.0; t2 = 38.0;  sr0 = 0.44f; sr1 = 0.35f; sr2 = 0.24f; break;
            case Sid808PercProfile::TimbaleLow:  r0 = 1.10f; r1 = 0.98f; r2 = 0.84f; t0 = 7.0;  t1 = 22.0; t2 = 48.0;  sr0 = 0.50f; sr1 = 0.40f; sr2 = 0.30f; break;
            case Sid808PercProfile::CuicaMute:   r0 = 0.72f; r1 = 1.35f; r2 = 2.10f; t0 = 12.0; t1 = 34.0; t2 = 74.0;  sr0 = 0.48f; sr1 = 0.62f; sr2 = 0.80f; break;
            case Sid808PercProfile::CuicaOpen:   r0 = 0.78f; r1 = 1.28f; r2 = 1.90f; t0 = 14.0; t1 = 42.0; t2 = 96.0;  sr0 = 0.56f; sr1 = 0.72f; sr2 = 0.92f; break;
            default: break;
        }
        const std::uint8_t wave = sid808TomProfileWaveform_(profile);
        const std::uint16_t pw = sid808TomProfilePulseWidth_(baseCfg, profile);
        Sid808VoiceConfig body = baseCfg;
        body.freq = sid808ScaledFrequency_(baseCfg.freq, r0);
        body.waveform = wave;
        body.pulseWidth = pw;
        body.attackDecay = sid808ScaledAttackDecay_(baseCfg.attackDecay, 1.05f);
        body.sustainRelease = sid808ScaledSustainRelease_(baseCfg.sustainRelease, sr0);
        armMicroStage_(voice, 0u, Sid808Drum::Tom, t0, body, appliedLevel * 0.84f, false);

        Sid808VoiceConfig drop = baseCfg;
        drop.freq = sid808ScaledFrequency_(baseCfg.freq, r1);
        drop.waveform = wave;
        drop.pulseWidth = pw;
        drop.attackDecay = sid808ScaledAttackDecay_(baseCfg.attackDecay, 1.15f);
        drop.sustainRelease = sid808ScaledSustainRelease_(baseCfg.sustainRelease, sr1);
        armMicroStage_(voice, 1u, Sid808Drum::Tom, t1, drop, appliedLevel * 0.58f, false);

        Sid808VoiceConfig tail = baseCfg;
        tail.freq = sid808ScaledFrequency_(baseCfg.freq, r2);
        tail.waveform = wave;
        tail.pulseWidth = pw;
        tail.attackDecay = sid808ScaledAttackDecay_(baseCfg.attackDecay, 1.25f);
        tail.sustainRelease = sid808ScaledSustainRelease_(baseCfg.sustainRelease, sr2);
        armMicroStage_(voice, 2u, Sid808Drum::Tom, t2, tail, appliedLevel * 0.34f, false);
    }

    void armSnareBodyStage_(std::uint8_t voice,
                            Sid808VoiceConfig bodyCfg,
                            float appliedLevel) noexcept {
        armMicroStage_(voice,
                       0u,
                       Sid808Drum::Snare,
                       7.5,
                       bodyCfg,
                       appliedLevel,
                       true);
    }

    void applyMicroStage_(std::size_t voice, std::size_t slot) noexcept {
        if (voice >= microStages_.size() || slot >= kMicroStageProgramCapacity_) return;
        const Sid808MicroStage stage = microStages_[voice][slot];
        const Sid808Drum drum = stage.drum;
        if (drum == Sid808Drum::Count) {
            clearMicroStageSlot_(static_cast<std::uint8_t>(voice), slot);
            return;
        }
        const Sid808VoiceConfig cfg = stage.config;
        const float level = stage.level;
        const bool raiseGate = stage.raiseGate;
        clearMicroStageSlot_(static_cast<std::uint8_t>(voice), slot);
        if (drum == Sid808Drum::Snare) {
            ++snareMicroStageAppliedCount_;
            recordAppliedMicroStage_(voice, drum, cfg, level, true);
            applySid808VoiceConfigToFixedVoice_(static_cast<std::uint8_t>(voice), drum, cfg, level, true);
            return;
        }
        recordAppliedMicroStage_(voice, drum, cfg, level, raiseGate);
        applySid808VoiceConfigToFixedVoice_(static_cast<std::uint8_t>(voice), drum, cfg, level, raiseGate);
    }

    static int sid808OneShotHoldSamples_(Sid808Drum drum, double sampleRate) noexcept {
        const double sr = std::isfinite(sampleRate) ? std::max(1.0, sampleRate) : 44100.0;
        double ms = 40.0;
        switch (drum) {
            case Sid808Drum::Kick:      ms = 130.0; break;
            case Sid808Drum::Snare:     ms = 35.0;  break;
            case Sid808Drum::ClosedHat: ms = 12.0;  break;
            case Sid808Drum::OpenHat:   ms = 420.0; break;
            case Sid808Drum::Clap:      ms = 95.0;  break;
            case Sid808Drum::Cowbell:   ms = 360.0; break;
            case Sid808Drum::Tom:       ms = 300.0; break; // v909: room for the longer singing tail
            case Sid808Drum::Rim:       ms = 15.0;  break;
            case Sid808Drum::Count:     ms = 0.0;   break;
        }
        return ms > 0.0 ? std::max(1, static_cast<int>(std::lround(sr * (ms / 1000.0)))) : -1;
    }

    static std::uint8_t tokenForDrum_(Sid808Drum drum) noexcept {
        return static_cast<std::uint8_t>(0x40u + static_cast<std::uint8_t>(drum));
    }

    void armAutoRelease_(std::uint8_t voice, Sid808Drum drum) noexcept {
        if (voice >= autoReleaseSamples_.size()) return;
        autoReleaseSamples_[voice] = sid808OneShotHoldSamples_(drum, sampleRate_);
        autoReleaseDrum_[voice] = drum;
    }

    void advanceAutoReleaseOneSample_() noexcept {
        advanceScheduledEventsSamples_(1);
    }

    int nextScheduledEventChunk_(int maxSamples) const noexcept {
        int chunk = std::max(1, maxSamples);
        for (int remaining : autoReleaseSamples_) {
            if (remaining > 0) chunk = std::min(chunk, remaining);
        }
        for (const auto& voiceStages : microStages_) {
            for (const auto& stage : voiceStages) {
                if (stage.samples > 0) chunk = std::min(chunk, stage.samples);
            }
        }
        return std::clamp(chunk, 1, std::max(1, maxSamples));
    }

    void advanceScheduledEventsSamples_(int samples) noexcept {
        const int n = std::max(1, samples);
        for (std::size_t voice = 0; voice < microStages_.size(); ++voice) {
            for (std::size_t slot = 0; slot < kMicroStageProgramCapacity_; ++slot) {
                Sid808MicroStage& stage = microStages_[voice][slot];
                if (stage.samples < 0) continue;
                stage.samples -= n;
                if (stage.samples <= 0) {
                    if (stage.samples < 0 && stage.drum == Sid808Drum::Snare)
                        ++snareMicroStageLateCount_;
                    applyMicroStage_(voice, slot);
                }
            }
        }
        for (std::size_t voice = 0; voice < autoReleaseSamples_.size(); ++voice) {
            int& remaining = autoReleaseSamples_[voice];
            if (remaining < 0) continue;
            remaining -= n;
            if (remaining <= 0) {
                const Sid808Drum drum = autoReleaseDrum_[voice];
                remaining = -1;
                autoReleaseDrum_[voice] = Sid808Drum::Count;
                if (drum != Sid808Drum::Count) {
                    clearActiveVoiceState_(static_cast<std::uint8_t>(voice), drum);
                    (void)sidEngine_.noteOff(tokenForDrum_(drum));
                } else {
                    clearActiveVoiceState_(static_cast<std::uint8_t>(voice));
                    sidEngine_.chip().getVoice(static_cast<int>(voice)).setGate(false);
                }
            }
        }
    }

public:
    std::uint8_t noteOff(Sid808Drum drum) noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        if (i >= static_cast<std::uint8_t>(Sid808Drum::Count)) return kNoVoice;
        const std::uint8_t voice = sid808VoiceForDrum(drum);
        if (voice < autoReleaseSamples_.size()) {
            autoReleaseSamples_[voice] = -1;
            autoReleaseDrum_[voice] = Sid808Drum::Count;
            clearMicroStage_(voice);
            clearActiveVoiceState_(voice, drum);
        }
        const std::uint8_t token = tokenForDrum_(drum);
        return sidEngine_.noteOff(token);
    }
    void allNotesOff() noexcept {
        sidEngine_.allNotesOff();
        for (int voice = 0; voice < 3; ++voice) {
            sidEngine_.chip().getVoice(voice).forceIdle();
        }
        autoReleaseSamples_.fill(-1);
        autoReleaseDrum_.fill(Sid808Drum::Count);
        for (std::uint8_t voice = 0u; voice < 3u; ++voice) clearMicroStage_(voice);
        activeDrumByVoice_.fill(Sid808Drum::Count);
        activeFlagsByVoice_.fill(0u);
        activeFilterModeByVoice_.fill(FilterMode::None);
        sidEngine_.setFilterVoiceRouting(false, false, false);
        sidEngine_.setFilterMode(FilterMode::None);
        drumTelemetryLevels_.fill(0.0f);
        voiceTelemetryLevels_.fill(0.0f);
    }

    // ── Render ──────────────────────────────────────────────────────────────
    void processSample(float& outL, float& outR) noexcept {
        sidEngine_.processSample(outL, outR);
        accumulateSnareStageTelemetry_(&outL, &outR, 1);
        advanceAutoReleaseOneSample_();
        decayTelemetryLevels_(1);
    }
    void processBlock(float* outL, float* outR, int numSamples) noexcept {
        if (!outL || !outR || numSamples <= 0) return;
        int rendered = 0;
        while (rendered < numSamples) {
            const int chunk = nextScheduledEventChunk_(numSamples - rendered);
            sidEngine_.processBlock(outL + rendered, outR + rendered, chunk);
            accumulateSnareStageTelemetry_(outL + rendered, outR + rendered, chunk);
            advanceScheduledEventsSamples_(chunk);
            rendered += chunk;
        }
        decayTelemetryLevels_(numSamples);
    }

    // ── Diagnostic / test surface ──────────────────────────────────────────
    double sampleRate()         const noexcept { return sampleRate_; }
    double clockFrequency()     const noexcept { return clockFreq_; }
    SIDModel sidModel()         const noexcept { return sidModel_; }
    std::uint8_t activeVoiceCount() const noexcept { return sidEngine_.activeVoiceCount(); }
    std::uint64_t invalidClockFrequencyRejectCount() const noexcept { return invalidClockRejectCount_; }
    void resetInvalidClockFrequencyRejectCount() noexcept { invalidClockRejectCount_ = 0u; }
    std::uint64_t noteOnCount() const noexcept { return noteOnCount_; }
    Sid808Drum lastDrum() const noexcept { return lastDrum_; }
    int lastMidiNote() const noexcept { return lastMidiNote_; }
    int lastDrumClass() const noexcept { return static_cast<int>(lastDrum_); }
    float lastVelocity() const noexcept { return std::clamp(lastVelocity_, 0.0f, 1.0f); }
    std::uint8_t lastVoice() const noexcept { return lastVoice_; }
    Sid808VoiceConfig lastAppliedConfig() const noexcept { return lastAppliedConfig_; }
    Sid808PercProfile lastPercProfile() const noexcept { return lastPercProfile_; }
    Sid808Drum activeDrumForVoice(int voice) const noexcept {
        return (voice >= 0 && voice < 3) ? activeDrumByVoice_[(size_t)voice] : Sid808Drum::Count;
    }
    std::uint8_t activeFlagsForVoice(int voice) const noexcept {
        return (voice >= 0 && voice < 3) ? activeFlagsByVoice_[(size_t)voice] : 0u;
    }
    FilterMode activeFilterModeForVoice(int voice) const noexcept {
        return (voice >= 0 && voice < 3) ? activeFilterModeByVoice_[(size_t)voice] : FilterMode::None;
    }
    bool activeFilterRoutedForVoice(int voice) const noexcept {
        return voice >= 0 && voice < 3 &&
               activeDrumByVoice_[(size_t)voice] != Sid808Drum::Count &&
               activeFilterModeByVoice_[(size_t)voice] != FilterMode::None &&
               (activeFlagsByVoice_[(size_t)voice] & Sid808Detail::kFlagFilter) != 0u;
    }
    float lastSnareSnapPeak() const noexcept { return std::clamp(snareSnapPeak_, 0.0f, 1.0f); }
    float lastSnareBodyPeak() const noexcept { return std::clamp(snareBodyPeak_, 0.0f, 1.0f); }
    float lastSnareSnapRms() const noexcept {
        return snareSnapCount_ > 0
            ? std::clamp(static_cast<float>(std::sqrt(snareSnapSq_ / static_cast<double>(snareSnapCount_))), 0.0f, 1.0f)
            : 0.0f;
    }
    float lastSnareBodyRms() const noexcept {
        return snareBodyCount_ > 0
            ? std::clamp(static_cast<float>(std::sqrt(snareBodySq_ / static_cast<double>(snareBodyCount_))), 0.0f, 1.0f)
            : 0.0f;
    }
    std::uint64_t snareMicroStageAppliedCount() const noexcept { return snareMicroStageAppliedCount_; }
    std::uint64_t snareMicroStageLateCount() const noexcept { return snareMicroStageLateCount_; }
    std::uint64_t microStageAppliedCount(Sid808Drum drum) const noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        return i < static_cast<std::uint8_t>(Sid808Drum::Count)
            ? microStageAppliedCountByDrum_[(size_t)i]
            : 0u;
    }
    std::uint16_t lastInitialFreqForDrum(Sid808Drum drum) const noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        return i < static_cast<std::uint8_t>(Sid808Drum::Count)
            ? lastInitialFreqByDrum_[(size_t)i]
            : 0u;
    }
    std::uint16_t lastMicroStageFreqForDrum(Sid808Drum drum) const noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        return i < static_cast<std::uint8_t>(Sid808Drum::Count)
            ? lastMicroStageFreqByDrum_[(size_t)i]
            : 0u;
    }
    Sid808VoiceConfig lastMicroStageConfigForDrum(Sid808Drum drum) const noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        return i < static_cast<std::uint8_t>(Sid808Drum::Count)
            ? lastMicroStageConfigByDrum_[(size_t)i]
            : Sid808VoiceConfig{};
    }
    std::uint8_t lastMicroStageWaveformForDrum(Sid808Drum drum) const noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        return i < static_cast<std::uint8_t>(Sid808Drum::Count)
            ? lastMicroStageWaveformByDrum_[(size_t)i]
            : 0u;
    }
    bool lastMicroStageRaisedGateForDrum(Sid808Drum drum) const noexcept {
        const auto i = static_cast<std::uint8_t>(drum);
        return i < static_cast<std::uint8_t>(Sid808Drum::Count)
            ? lastMicroStageGateByDrum_[(size_t)i]
            : false;
    }

    void copyDrumLevels(float* outLevels, int count) const noexcept {
        if (!outLevels || count <= 0) return;
        const int n = std::min(count, static_cast<int>(drumTelemetryLevels_.size()));
        for (int i = 0; i < n; ++i) {
            outLevels[i] = std::clamp(drumTelemetryLevels_[(size_t)i], 0.0f, 1.0f);
        }
        for (int i = n; i < count; ++i) outLevels[i] = 0.0f;
    }
    void copyVoiceLevels(float* outLevels, int count) const noexcept {
        if (!outLevels || count <= 0) return;
        const int n = std::min(count, static_cast<int>(voiceTelemetryLevels_.size()));
        for (int i = 0; i < n; ++i) {
            const float chipLevel = sidEngine_.chip().getVoiceLevel(i);
            outLevels[i] = std::clamp(std::max(voiceTelemetryLevels_[(size_t)i], chipLevel), 0.0f, 1.0f);
        }
        for (int i = n; i < count; ++i) outLevels[i] = 0.0f;
    }
    float getVoiceEnvelopeLevel(int i) const noexcept { return sidEngine_.chip().getVoiceEnvelopeLevel(i); }
    float getVoiceLastSample(int i) const noexcept { return sidEngine_.chip().getVoiceLastSample(i); }
    bool isActive() const noexcept { return activeVoiceCount() > 0; }

    SIDChip&       chip()       noexcept { return sidEngine_.chip(); }
    const SIDChip& chip() const noexcept { return sidEngine_.chip(); }
    SingleSidThreeVoiceEngine&       innerEngine()       noexcept { return sidEngine_; }
    const SingleSidThreeVoiceEngine& innerEngine() const noexcept { return sidEngine_; }

private:

    static std::uint8_t sidControlWaveformToInternal_(std::uint8_t sidControlWave) noexcept {
        return sid808ControlWaveformToInternal(sidControlWave);
    }

    static std::uint16_t clampSidFreqReg_(int value) noexcept {
        return static_cast<std::uint16_t>(std::clamp(value, 0, 65535));
    }

    static std::uint16_t clampSidPulseWidth_(int value) noexcept {
        return static_cast<std::uint16_t>(std::clamp(value, 0, 0x0FFF));
    }

    Sid808VoiceConfig compensateConfigForSidModel_(Sid808Drum drum,
                                                   Sid808VoiceConfig cfg) const noexcept {
        if (sidModel_ != SIDModel::MOS6581) return cfg;

        // 6581 bass-family trim: the original SID-808 kit tables were tuned
        // on the cleaner 8580 path. Keep raw kit data immutable, but lift the
        // runtime register/level enough that 6581 kick/tom bodies do not sag
        // under the darker, leakier analog model.
        switch (drum) {
            case Sid808Drum::Kick: {
                const int lift = std::max(0x0080, static_cast<int>(cfg.freq) / 32);
                cfg.freq = clampSidFreqReg_(static_cast<int>(cfg.freq) + lift);
                if (cfg.pulseWidth != 0u) {
                    cfg.pulseWidth = clampSidPulseWidth_(static_cast<int>(cfg.pulseWidth) + 0x0100);
                }
                cfg.voiceLevel = std::clamp(cfg.voiceLevel * 1.08f, 0.0f, 1.0f);
                break;
            }
            case Sid808Drum::Tom: {
                const int lift = std::max(0x0060, static_cast<int>(cfg.freq) / 40);
                cfg.freq = clampSidFreqReg_(static_cast<int>(cfg.freq) + lift);
                if (cfg.pulseWidth != 0u) {
                    cfg.pulseWidth = clampSidPulseWidth_(static_cast<int>(cfg.pulseWidth) + 0x0080);
                }
                cfg.voiceLevel = std::clamp(cfg.voiceLevel * 1.05f, 0.0f, 1.0f);
                break;
            }
            case Sid808Drum::Snare:
            case Sid808Drum::Clap:
            case Sid808Drum::Rim:
                cfg.voiceLevel = std::clamp(cfg.voiceLevel * 1.03f, 0.0f, 1.0f);
                break;
            case Sid808Drum::ClosedHat:
            case Sid808Drum::OpenHat:
            case Sid808Drum::Cowbell:
                cfg.voiceLevel = std::clamp(cfg.voiceLevel * 1.04f, 0.0f, 1.0f);
                break;
            case Sid808Drum::Count:
                break;
        }
        return cfg;
    }

    static float realtimePowUnit_(float base, int exponent) noexcept {
        float result = 1.0f;
        float factor = std::clamp(std::isfinite(base) ? base : 1.0f, 0.0f, 1.0f);
        int n = std::max(0, exponent);
        while (n > 0) {
            if (n & 1) result *= factor;
            factor *= factor;
            n >>= 1;
        }
        return std::clamp(std::isfinite(result) ? result : 1.0f, 0.0f, 1.0f);
    }

    void refreshTelemetryDecay_() noexcept {
        const double sr = std::max(1.0, sampleRate_);
        telemetryDecayPerSample_ = std::clamp(
            static_cast<float>(std::exp(std::log(0.001) / (sr * 0.42))),
            0.0f,
            1.0f);
    }

    void decayTelemetryLevels_(int numSamples) noexcept {
        const float decay = realtimePowUnit_(telemetryDecayPerSample_, std::max(1, numSamples));
        for (float& v : drumTelemetryLevels_) v = std::clamp(v * decay, 0.0f, 1.0f);
        for (float& v : voiceTelemetryLevels_) v = std::clamp(v * decay, 0.0f, 1.0f);
    }

    void resetSnareStageTelemetry_() noexcept {
        snareTelemetryCursor_ = 0;
        snareSnapEndSamples_ = sid808SamplesForMilliseconds_(sampleRate_, 7.5);
        snareBodyEndSamples_ = sid808SamplesForMilliseconds_(sampleRate_, 25.0);
        snareSnapSq_ = 0.0;
        snareBodySq_ = 0.0;
        snareSnapCount_ = 0;
        snareBodyCount_ = 0;
        snareSnapPeak_ = 0.0f;
        snareBodyPeak_ = 0.0f;
    }

    void accumulateSnareStageTelemetry_(const float* outL, const float* outR, int numSamples) noexcept {
        if (!outL || !outR || numSamples <= 0 || snareTelemetryCursor_ < 0) return;
        const int bodyEnd = std::max(snareSnapEndSamples_, snareBodyEndSamples_);
        for (int i = 0; i < numSamples && snareTelemetryCursor_ < bodyEnd; ++i, ++snareTelemetryCursor_) {
            const float l = std::isfinite(outL[i]) ? outL[i] : 0.0f;
            const float r = std::isfinite(outR[i]) ? outR[i] : 0.0f;
            const float peak = std::max(std::fabs(l), std::fabs(r));
            const float mono = 0.5f * (l + r);
            const double sq = static_cast<double>(mono) * static_cast<double>(mono);
            if (snareTelemetryCursor_ < snareSnapEndSamples_) {
                snareSnapPeak_ = std::max(snareSnapPeak_, peak);
                snareSnapSq_ += sq;
                ++snareSnapCount_;
            } else {
                snareBodyPeak_ = std::max(snareBodyPeak_, peak);
                snareBodySq_ += sq;
                ++snareBodyCount_;
            }
        }
        if (snareTelemetryCursor_ >= bodyEnd) snareTelemetryCursor_ = -1;
    }

    SingleSidThreeVoiceEngine sidEngine_{};
    std::array<Sid808VoiceConfig, static_cast<std::size_t>(Sid808Drum::Count)> drumConfigs_{};
    std::array<float, static_cast<std::size_t>(Sid808Drum::Count)> drumTelemetryLevels_{};
    std::array<float, 3> voiceTelemetryLevels_{};
    std::array<int, 3> autoReleaseSamples_{{-1, -1, -1}};
    std::array<Sid808Drum, 3> autoReleaseDrum_{{
        Sid808Drum::Count,
        Sid808Drum::Count,
        Sid808Drum::Count,
    }};
    std::array<std::array<Sid808MicroStage, kMicroStageProgramCapacity_>, 3> microStages_{};
    std::array<std::uint64_t, static_cast<std::size_t>(Sid808Drum::Count)> microStageAppliedCountByDrum_{};
    std::array<std::uint16_t, static_cast<std::size_t>(Sid808Drum::Count)> lastInitialFreqByDrum_{};
    std::array<std::uint16_t, static_cast<std::size_t>(Sid808Drum::Count)> lastMicroStageFreqByDrum_{};
    std::array<Sid808VoiceConfig, static_cast<std::size_t>(Sid808Drum::Count)> lastMicroStageConfigByDrum_{};
    std::array<std::uint8_t, static_cast<std::size_t>(Sid808Drum::Count)> lastMicroStageWaveformByDrum_{};
    std::array<bool, static_cast<std::size_t>(Sid808Drum::Count)> lastMicroStageGateByDrum_{};
    std::array<Sid808Drum, 3> activeDrumByVoice_{{
        Sid808Drum::Count,
        Sid808Drum::Count,
        Sid808Drum::Count,
    }};
    std::array<std::uint8_t, 3> activeFlagsByVoice_{{0u, 0u, 0u}};
    std::array<FilterMode, 3> activeFilterModeByVoice_{{
        FilterMode::None,
        FilterMode::None,
        FilterMode::None,
    }};
    int snareTelemetryCursor_ = -1;
    int snareSnapEndSamples_ = 0;
    int snareBodyEndSamples_ = 0;
    double snareSnapSq_ = 0.0;
    double snareBodySq_ = 0.0;
    int snareSnapCount_ = 0;
    int snareBodyCount_ = 0;
    float snareSnapPeak_ = 0.0f;
    float snareBodyPeak_ = 0.0f;
    std::uint64_t snareMicroStageAppliedCount_ = 0u;
    std::uint64_t snareMicroStageLateCount_ = 0u;
    ArpSIDForensicConfig forensicConfig_{};
    SIDModel sidModel_ = SIDModel::MOS8580;
    double sampleRate_ = 44100.0;
    double clockFreq_  = PAL_CLOCK_FREQ;
    float telemetryDecayPerSample_ = 0.99962723f; // 60 dB over 0.42s at 44.1 kHz; refreshed in prepare().
    std::uint64_t invalidClockRejectCount_ = 0u;
    std::uint64_t noteOnCount_ = 0u;
    Sid808Drum lastDrum_ = Sid808Drum::Count;
    int lastMidiNote_ = -1;
    float lastVelocity_ = 0.0f;
    std::uint8_t lastVoice_ = kNoVoice;
    Sid808VoiceConfig lastAppliedConfig_{};
    Sid808PercProfile lastPercProfile_ = Sid808PercProfile::Default;
    // A6: Accent threshold — velocity >= this value triggers accent behaviour.
    std::uint8_t accentThreshold_ = 100u;
};

// A9: Conversion helpers between KitVoiceConfig (GUI) and Sid808VoiceConfig (engine).
// These live here (not in kit_voice_config.h) to avoid a GUI→engine include dependency.

/// Convert a KitVoiceConfig + freq/level hints → Sid808VoiceConfig.
inline constexpr Sid808VoiceConfig kitVoiceConfigToSid808(
        const ArpSID::GUI::KitVoiceConfig& kvc,
        std::uint16_t freqHint,
        float levelHint) noexcept {
    const std::uint16_t pw = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(kvc.pulseWidthHi & 0x0Fu) << 8u) |
        static_cast<std::uint16_t>(kvc.pulseWidthLo));
    Sid808VoiceConfig s{};
    s.freq           = freqHint;
    s.pulseWidth     = pw;
    s.waveform       = sid808NormalizeWaveformControl(kvc.waveform);
    s.attackDecay    = kvc.attackDecay;
    s.sustainRelease = kvc.sustainRelease;
    s.flags          = kvc.flags;
    s.voiceLevel     = levelHint;
    return s;
}

/// Convert Sid808VoiceConfig → KitVoiceConfig (reverse direction).
inline constexpr ArpSID::GUI::KitVoiceConfig kitVoiceConfigFromSid808(
        const Sid808VoiceConfig& svc) noexcept {
    ArpSID::GUI::KitVoiceConfig k{};
    k.waveform       = sid808NormalizeWaveformControl(svc.waveform);
    k.attackDecay    = svc.attackDecay;
    k.sustainRelease = svc.sustainRelease;
    k.pulseWidthLo   = static_cast<std::uint8_t>(svc.pulseWidth & 0xFFu);
    k.pulseWidthHi   = static_cast<std::uint8_t>((svc.pulseWidth >> 8u) & 0x0Fu);
    k.flags          = svc.flags;
    k.pad_[0] = k.pad_[1] = 0u;
    return k;
}

} // namespace ArpSID

#endif // ARPSID_ENGINES_SID808_ENGINE_H
