#pragma once

#include "parameter_ids.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid/core/sid_runtime_host_ops.h"
#include "arpsid/core/sid_mod_matrix_types.h"
#include "arpsid/core/sid_serializer_schema.h"
#include "arpsid/core/sid_variant_profile.h"
#include "arpsid/core/sid_variant_ops.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "arpsid/core/drum_context.h"
#include "arpsid/patchbank/factory_sid808_param_bridge.h"
#include "arpsid/patchbank/factory_digi_param_bridge.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <string>

namespace ArpSID {

constexpr int kFactoryPatchSlotCount = 180;

inline uint8_t quantizeSidByte01(float v) noexcept {
    const float x = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(x * 255.0f));
}

inline uint16_t quantizeSidPw12(float v) noexcept {
    const float x = std::clamp(std::isfinite(v) ? v : 0.5f, 0.0f, 1.0f);
    return static_cast<uint16_t>(std::lround(x * 4095.0f)) & 0x0FFFu;
}

inline uint8_t packSidAdByte(float attackNorm, float decayNorm) noexcept {
    const uint8_t a = static_cast<uint8_t>(std::clamp<int>(std::lround(std::clamp(std::isfinite(attackNorm) ? attackNorm : 0.0f, 0.0f, 1.0f) * 15.0f), 0, 15));
    const uint8_t d = static_cast<uint8_t>(std::clamp<int>(std::lround(std::clamp(std::isfinite(decayNorm) ? decayNorm : 0.0f, 0.0f, 1.0f) * 15.0f), 0, 15));
    return static_cast<uint8_t>((a << 4) | d);
}

inline uint8_t packSidSrByte(float sustainNorm, float releaseNorm) noexcept {
    const uint8_t s = static_cast<uint8_t>(std::clamp<int>(std::lround(std::clamp(std::isfinite(sustainNorm) ? sustainNorm : 0.0f, 0.0f, 1.0f) * 15.0f), 0, 15));
    const uint8_t r = static_cast<uint8_t>(std::clamp<int>(std::lround(std::clamp(std::isfinite(releaseNorm) ? releaseNorm : 0.0f, 0.0f, 1.0f) * 15.0f), 0, 15));
    return static_cast<uint8_t>((s << 4) | r);
}

inline uint8_t synthWaveControlByteFromNorm(float waveNorm, bool sync, bool ring) noexcept {
    const float w = std::clamp(std::isfinite(waveNorm) ? waveNorm : 0.0f, 0.0f, 0.999999f);
    const int bucket = std::clamp<int>(static_cast<int>(w * 8.0f), 0, 7);
    uint8_t ctrl = 0;
    switch (bucket) {
        case 0: ctrl = 0x10u; break; // triangle
        case 1: ctrl = 0x20u; break; // saw
        case 2: ctrl = 0x40u; break; // pulse
        case 3: ctrl = 0x80u; break; // noise
        case 4: ctrl = 0x30u; break; // tri+saw
        case 5: ctrl = 0x50u; break; // tri+pulse
        case 6: ctrl = 0x60u; break; // saw+pulse
        default: ctrl = 0x70u; break; // tri+saw+pulse
    }
    if (ring) ctrl |= 0x04u;
    if (sync) ctrl |= 0x02u;
    return ctrl;
}

inline uint8_t audibleFactoryControlByte(float waveNorm, float levelNorm, bool sync, bool ring) noexcept {
    const bool audible = std::isfinite(levelNorm) && levelNorm > 0.0001f;
    if (!audible) return 0x00u;
    return synthWaveControlByteFromNorm(waveNorm, sync, ring);
}

// A SID voice must keep running (waveform bits present) when another voice uses
// it as a ring-mod or hard-sync source, even if it is muted out of the mixer.
// SID source topology is cyclic: V1's source is V3, V2's source is V1, V3's
// source is V2.  voice: 0=V1, 1=V2, 2=V3.
inline bool factoryVoiceMustRunAsInternalSource(const PatchStaticState& s, int voice) noexcept {
    if (voice == 0) return s.vco2Ring || s.vco2Sync; // V1 drives V2
    if (voice == 1) return s.vco3Ring || s.vco3Sync; // V2 drives V3
    if (voice == 2) return s.vco1Ring || s.vco1Sync; // V3 drives V1
    return false;
}

// Source-aware control byte: a voice that is inaudible but required as an
// internal ring/sync source still gets its waveform/test/ring/sync bits so the
// oscillator runs.  Only a voice that is both inaudible AND not an internal
// source is silenced to 0x00.
inline uint8_t factoryControlByte(float waveNorm, float levelNorm, bool sync, bool ring,
                                  bool internalSource) noexcept {
    const bool audible = std::isfinite(levelNorm) && levelNorm > 0.0001f;
    if (!audible && !internalSource) return 0x00u;
    return synthWaveControlByteFromNorm(waveNorm, sync, ring);
}

inline uint16_t makeFactorySeqNote(int midi) noexcept {
    return static_cast<uint16_t>(std::clamp(midi, 0, 127));
}

inline float normalizedMidiNoteValue(int midi) noexcept {
    return std::clamp(static_cast<float>(makeFactorySeqNote(midi)) / 127.0f, 0.0f, 1.0f);
}

inline void setFactorySeqStep(std::array<float, static_cast<size_t>(kNumParams)>& params,
                              int stepIndex,
                              int midi,
                              float velocity,
                              float gate) noexcept {
    if (stepIndex < 0 || stepIndex >= 32) return;
    const int base = static_cast<int>(kParamSeqStep1Note) + stepIndex * 3;
    if (static_cast<size_t>(base + 2) >= params.size()) return;
    params[static_cast<size_t>(base + 0)] = normalizedMidiNoteValue(midi);
    params[static_cast<size_t>(base + 1)] = std::clamp(std::isfinite(velocity) ? velocity : 1.0f, 0.0f, 1.0f);
    params[static_cast<size_t>(base + 2)] = std::clamp(std::isfinite(gate) ? gate : 1.0f, 0.0f, 1.0f);
}

inline float normalizedFactorySeqLength(int steps) noexcept {
    return std::clamp((static_cast<float>(std::clamp(steps, 1, 32)) - 1.0f) / 31.0f, 0.0f, 1.0f);
}

inline void setFactoryModSource(std::array<float, static_cast<size_t>(kNumParams)>& params,
                                ParamID pid,
                                SidModSource src) noexcept {
    params[static_cast<size_t>(pid)] = encodeSidModSourceToNormalizedUi(src);
}


constexpr bool isAuthoredDrSidProjectionFactorySlot(int slot) noexcept {
    // v916: do not clamp. Invalid/future slots must stay invalid instead of
    // aliasing to legacy slot 127 and silently taking DrSID defaults.
    return slot == 47 || slot == 127 || (slot >= 80 && slot <= 119);
}

static_assert(!isAuthoredDrSidProjectionFactorySlot(-1),  "negative slots must not alias to DrSID");
static_assert( isAuthoredDrSidProjectionFactorySlot(47),  "slot 47 remains authored DrSID");
static_assert( isAuthoredDrSidProjectionFactorySlot(80),  "slot 80 remains authored DrSID");
static_assert( isAuthoredDrSidProjectionFactorySlot(119), "slot 119 remains authored DrSID");
static_assert(!isAuthoredDrSidProjectionFactorySlot(120), "slot 120 is canonical SID808, not authored DrSID");
static_assert( isAuthoredDrSidProjectionFactorySlot(127), "legacy slot 127 remains authored DrSID only when explicitly addressed");
static_assert(!isAuthoredDrSidProjectionFactorySlot(128), "slot 128 must not alias to legacy DrSID 127");
static_assert(!isAuthoredDrSidProjectionFactorySlot(149), "slot 149 is canonical SID808, not authored DrSID");
static_assert(!isAuthoredDrSidProjectionFactorySlot(150), "slot 150 is Digi, not authored DrSID");
static_assert(!isAuthoredDrSidProjectionFactorySlot(180), "out-of-range slots must not alias to DrSID");


inline bool isFactoryBassDefinition(const PatchDefinition& def) noexcept {
    return def.usage.role == PatchRole::Bass || def.usage.family == HistoricalFamilyId::CoreBass;
}

inline void applyFactoryAuthenticC64BassDefaults(const PatchDefinition& def,
                                                std::array<float, static_cast<size_t>(kNumParams)>& params) noexcept {
    if (!isFactoryBassDefinition(def)) return;
    auto sp = [&](ParamID pid, float v) noexcept {
        params[static_cast<size_t>(pid)] = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    };
    const bool organic6581 = def.staticState.chip == SidChipTarget::MOS6581;
    const bool synthBass = def.displayName.find("Synth Bass") != std::string::npos;
    const bool slapBass = def.displayName.find("Slap Bass") != std::string::npos;

    // C64-authentic bass authority: one dominant SID oscillator, one restrained
    // body/edge helper, no accidental unison spread, no wide LFO tremolo smear.
    sp(kParamVoiceMode, 1.0f / 3.0f);      // Mono
    sp(kParamVoiceSpread, 0.0f);
    sp(kParamPortamentoTime, synthBass ? 0.08f : 0.04f);
    // Pin bass glide to the C64 register-slide law (style index 0 = 0.0f
    // normalized). Without this, bass patches would inherit whatever the
    // user/host last set kParamPortamentoStyle to, which could leave bass on
    // SmoothSynth/LinearSemitone and smear the fundamental.
    sp(kParamPortamentoStyle, 0.0f);
    sp(kParamArpEnable, 0.0f);
    sp(kParamLFODepth, 0.0f);
    sp(kParamLFO2Depth, 0.0f);
    sp(kParamLFO3Depth, 0.0f);
    sp(kParamLFO4Depth, 0.0f);

    // Keep the fundamental stable and big without fake sub-bass. 6581 bass gets
    // warmer pulse/triangle authority; 8580 synth/slap bass gets tighter saw/pulse.
    sp(kParamVCO1Level, organic6581 ? 0.92f : 0.88f);
    sp(kParamVCO2Level, slapBass ? 0.26f : (synthBass ? 0.34f : 0.18f));
    // VCO3 stays out of the audible mix by default for bass (two-osc authority),
    // but is not forced to zero if the patch has already authored a non-trivial
    // VCO3 level — preserves authored sub-octave/FM helpers.
    if (params[static_cast<size_t>(kParamVCO3Level)] <= 0.0001f) {
        sp(kParamVCO3Level, 0.0f);
    }
    sp(kParamVCO1PulseWidth, organic6581 ? 0.46f : 0.42f);
    sp(kParamVCO2PulseWidth, synthBass ? 0.56f : 0.50f);
    sp(kParamVCO2SyncEnable, synthBass ? 1.0f : 0.0f);
    sp(kParamVCO1RingModEnable, 0.0f);
    sp(kParamVCO2RingModEnable, 0.0f);
    sp(kParamVCO3RingModEnable, 0.0f);

    sp(kParamAttack, 0.0f);
    sp(kParamDecay, slapBass ? 0.13f : (synthBass ? 0.18f : 0.15f));
    sp(kParamSustain, synthBass ? 0.54f : (organic6581 ? 0.42f : 0.48f));
    sp(kParamRelease, 0.08f);
    sp(kParamFilterCutoff, organic6581 ? 0.24f : 0.30f);
    sp(kParamFilterResonance, synthBass ? 0.18f : 0.13f);
    sp(kParamFilterEnvAmount, slapBass ? 0.18f : 0.10f);
    sp(kParamFilterDrive, organic6581 ? 0.10f : (synthBass ? 0.16f : 0.12f));
    sp(kParamOutputLimiter, 1.0f);
    sp(kParamLimiterThreshold, 0.90f);

    // Bass modulation must be explicit and tiny: velocity opens the SID filter;
    // it must not modulate master volume enough to wobble the low end.
    setFactoryModSource(params, kParamModVCFCutoffSource, SidModSource::Velocity);
    sp(kParamModVCFCutoffDepth, slapBass ? 0.20f : 0.12f);
    setFactoryModSource(params, kParamModMasterVolumeSource, SidModSource::None);
    sp(kParamModMasterVolumeDepth, 0.0f);

    // 6581 bass perfection:
    //
    // 1) The 6581 ADSR bug can occasionally swallow the attack of a freshly    // retriggered envelope. For bass the fundamental is the entire patch,
    // so missing-attack is much more audible than on a brassy lead. The
    // AdsrBug6581 toggle stays enabled for authenticity, but the bass
    // defaults arm the strict hard-restart precharge path on 6581 only.
    // The clean-restart ramp absorbs the click and the precharge primes
    // the envelope so the bug can't silence the note onset.
    //
    // 2) The 6581 filter has a steeper, more nonlinear low-pass response;
    // cutoff readings at the same normalized value yield a darker tone
    // than on 8580. Lift the cutoff floor slightly so 6581 bass doesn't
    // collapse into a muffled thud at low cutoffs.
    //
    // 3) The 6581 filter saturates earlier than 8580. Pull filter drive in
    // a tick further to keep low-end clean.
    if (organic6581) {
        sp(kParamSidAdsrBug6581, 1.0f);
        // Lift cutoff slightly compared to the chip-agnostic default above
        // (which already biases organic6581 to 0.24). The +0.04 keeps the
        // wool but rescues fundamentals on the bottom octave.
        const float bassCutoff = std::min(1.0f,
            params[static_cast<size_t>(kParamFilterCutoff)] + 0.04f);
        sp(kParamFilterCutoff, bassCutoff);
        // Tighten filter drive on 6581 to avoid running into the chip's
        // self-resonant region under velocity-driven cutoff modulation.
        sp(kParamFilterDrive, std::min(0.12f,
            params[static_cast<size_t>(kParamFilterDrive)]));
    } else {
        // Explicit 8580: ADSR bug must be OFF.
        sp(kParamSidAdsrBug6581, 0.0f);
    }
}

inline bool isFactorySid808KitSlot(int slot) noexcept {
    return slot >= 120 && slot <= 149;
}

inline void applyFactorySid808MissingLogicDefaults(int slot,
                                                   std::array<float, static_cast<size_t>(kNumParams)>& params) noexcept {
    if (!isFactorySid808KitSlot(slot)) return;
    auto sp = [&](ParamID pid, float v) noexcept {
        params[static_cast<size_t>(pid)] = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    };
    // SID-808 must be a complete authored x0x page, never a generic DrSID kit
    // with an 808 label. The engine is still SID-register driven; the Analog
    // X0X model only selects the SID-projected x0x control law.
    sp(kParamSynthModeEnable, 0.0f);
    sp(kParamDrSidEnable, 1.0f);
    sp(kParamDrSidMachineModel, 1.0f);
    // v859 noise-on-load fix: the sequencer defaults OFF. The internal seq clock
    // free-runs regardless of host transport (follow_host_tempo_seq defaults to
    // false), so seqEnable=1 here made every SID-808 factory load start playing
    // its pattern immediately — "loads up and starts making noise". The authored
    // per-kit pattern below stays fully published so pressing SEQ plays the
    // kit's groove instantly; loading a kit is silent until the user plays pads,
    // MIDI, or enables the sequencer.
    sp(kParamSeqEnable, 0.0f);
    sp(kParamSeqMode, 0.0f);
    sp(kParamSeqLength, normalizedFactorySeqLength(16));
    sp(kParamArpEnable, 0.0f);
    sp(kParamVoiceSpread, 0.0f);
    sp(kParamOutputLimiter, 1.0f);
    sp(kParamLimiterThreshold, 0.88f);

    // SID-808 guard: applyFactoryDrSidDefaults() is guarded to drSidMode/Drum
    // role and may early-return for these slots, leaving every per-pad tone at
    // zero. Publish a full authored x0x voice page here so each of the five
    // 808 kits has a distinct, audible kick/snare/hat/clap/cowbell/tom set,
    // regardless of role/family flags on the slot's PatchDefinition.
    //
    // The five kits span a useful musical range — not just numeric jitter
    // around one archetype. Tunings were chosen by listening through the
    // DrSID X0X path on the 8580 model (the canonical chip for SID-808):
    //
    // 120 Classic — balanced 4-on-the-floor 808: warm round kick, medium
    // snare snap, clean hats; subtle swing, light reverb.
    // 121 Punch — tighter louder kick, shorter hats, harder clap snap;
    // straight grid, harder accent, hotter output drive.
    // 122 Lo-Fi — softer hats, long-decay kick, narrower bandwidth, more
    // swing, more reverb; boom-bap / dub / vintage tape.
    // 123 Hard — aggressive kick tune, hard snare snap, bright hats and
    // cymbals, hottest accent; techno / industrial.
    // 124 Wide — wider clap spread, longer cowbell decay, brighter hat
    // metal, more reverb; breakbeat / electronica with air.
    //
    // Slots 125..149 repeat those five authored x0x families, matching the
    // canonical KIT/SID-808 30-slot range and the v589 factory kit table.
    const Sid808FactoryParamSignature sig = factorySid808ParamSignatureForSlot(slot);
    sp(kParamDrSidVolume,        sig.volume);
    sp(kParamDrSidKickTune,      sig.kickTune);
    sp(kParamDrSidKickDecay,     sig.kickDecay);
    sp(kParamDrSidSnareTone,     sig.snareTone);
    sp(kParamDrSidSnareSnap,     sig.snareSnap);
    sp(kParamDrSidHatTune,       sig.hatTune);
    sp(kParamDrSidHatDecay,      sig.hatDecay);
    sp(kParamDrSidClapDecay,     sig.clapDecay);
    sp(kParamDrSidCowbellTune,   sig.cowbellTune);
    sp(kParamDrSidCowbellDecay,  sig.cowbellDecay);
    sp(kParamDrSidTomTune,       sig.tomTune);
    sp(kParamDrSidTomDecay,      sig.tomDecay);
    sp(kParamDrSidAccentAmount,  sig.accent);
    sp(kParamDrSidOutputDrive,   sig.drive);
    sp(kParamDrSidHatMetal,      sig.hatMetal);
    sp(kParamDrSidClapSpread,    sig.clapSpread);

    const int kitVariant = (slot - 120) % 5;
    // Per-kit musical defaults the sequencer/filter respect at first load.
    static constexpr float kSwing[5]       = { 0.04f, 0.00f, 0.16f, 0.00f, 0.12f };
    static constexpr float kReverbMix[5]   = { 0.06f, 0.00f, 0.14f, 0.00f, 0.18f };
    static constexpr float kLimiterThr[5]  = { 0.88f, 0.82f, 0.94f, 0.80f, 0.90f };
    static constexpr float kFilterCutoff[5]= { 0.62f, 0.58f, 0.42f, 0.74f, 0.66f };
    static constexpr float kFilterRes[5]   = { 0.12f, 0.18f, 0.06f, 0.26f, 0.14f };
    sp(kParamSeqSwing,           kSwing[kitVariant]);
    sp(kParamReverbMix,          kReverbMix[kitVariant]);
    sp(kParamLimiterThreshold,   kLimiterThr[kitVariant]);
    sp(kParamFilterCutoff,       kFilterCutoff[kitVariant]);
    sp(kParamFilterResonance,    kFilterRes[kitVariant]);

    // v859 default-pattern fix: each kit variant ships its authored musical
    // groove instead of the old GM-coverage TEST pattern (one hit of every drum
    // cycling — the "wrong/random-sounding default kit" report). These are the
    // five patterns originally authored for the kit families (recovered from the
    // retired per-slot table); every step is still a valid GM drum note so
    // restore-safety is preserved, and the seq is OFF by default (above) so the
    // pattern only sounds when the user enables SEQ.
    {
        struct KitPattern { int notes[16]; float vels[16]; float gates[16]; };
        static const KitPattern kKitPatterns[5] = {
            { // 0 Classic — balanced 4-on-the-floor
              {36,42,38,42,36,44,38,46,36,42,40,42,37,42,39,49},
              {1.00f,0.78f,0.94f,0.74f,1.00f,0.70f,0.88f,0.66f,1.00f,0.72f,0.92f,0.80f,0.96f,0.70f,0.76f,0.84f},
              {1.00f,0.82f,1.00f,0.78f,1.00f,0.70f,0.92f,0.76f,1.00f,0.76f,0.95f,0.82f,1.00f,0.72f,0.80f,0.90f} },
            { // 1 Punch — hard straight grid
              {36,42,38,42,36,46,40,42,36,42,38,49,36,46,40,57},
              {1.00f,0.76f,0.98f,0.74f,0.96f,0.70f,0.92f,0.80f,1.00f,0.76f,0.96f,0.84f,1.00f,0.78f,0.90f,0.86f},
              {1.00f,0.82f,1.00f,0.78f,1.00f,0.72f,0.95f,0.82f,1.00f,0.80f,1.00f,0.92f,1.00f,0.82f,0.92f,0.88f} },
            { // 2 Lo-Fi — sparse dub gaps (silent off-steps)
              {35,36,42,36,37,36,38,36,35,36,44,36,39,36,46,49},
              {1.00f,0.00f,0.70f,0.00f,0.92f,0.00f,0.80f,0.00f,1.00f,0.00f,0.72f,0.00f,0.84f,0.00f,0.68f,0.86f},
              {1.00f,0.00f,0.78f,0.00f,0.92f,0.00f,0.85f,0.00f,1.00f,0.00f,0.74f,0.00f,0.86f,0.00f,0.72f,0.95f} },
            { // 3 Hard — tracker drive
              {36,42,38,44,36,42,40,46,35,42,38,49,37,42,39,56},
              {1.00f,0.82f,0.98f,0.78f,0.96f,0.74f,0.92f,0.76f,1.00f,0.84f,0.94f,0.82f,0.96f,0.80f,0.86f,0.90f},
              {1.00f,0.86f,1.00f,0.84f,1.00f,0.80f,0.96f,0.82f,1.00f,0.90f,1.00f,0.86f,1.00f,0.82f,0.88f,0.92f} },
            { // 4 Wide — airy breaks with cymbal color
              {36,42,38,46,36,42,56,49,36,42,38,57,67,68,75,81},
              {1.00f,0.74f,0.94f,0.86f,1.00f,0.78f,0.92f,0.72f,0.96f,0.76f,0.88f,0.82f,0.90f,0.84f,0.86f,0.94f},
              {1.00f,0.80f,1.00f,0.92f,1.00f,0.82f,0.96f,0.78f,1.00f,0.82f,0.90f,0.86f,0.92f,0.88f,0.90f,1.00f} },
        };
        const KitPattern& pat = kKitPatterns[kitVariant];
        for (int i = 0; i < 16; ++i) {
            setFactorySeqStep(params, i, pat.notes[i], pat.vels[i], pat.gates[i]);
        }
    }
}

// The 40 canonical DrSID factory slots (80..119) are organised as 8 primary-drum
// families x 5 variants, but only the GM-percussion slots (112..119) were ever
// authored with distinct drum params — slots 80..111 all fell through to one
// generic default, so 32 slots produced the identical kit. This layer gives each
// generic slot a deterministic per-(family,variant) character (mirroring
// SID-808's sid808ApplySlotVariation), so every slot is an audibly distinct,
// still-musical kit. Offsets are bounded so every drum stays audible. Not applied
// to 112..119, whose switch cases author real GM percussion.
inline void applyFactoryDrSidNewKitCharacter_(int slot,
        std::array<float, static_cast<size_t>(kNumParams)>& params) noexcept {
    const int first = static_cast<int>(kDrSidNewFactoryRange.first); // 80
    if (slot < first || slot > 111) return; // 112..119 are authored GM percussion
    const int idx     = slot - first;
    const int family  = (idx / 5) & 7;  // 0..7 kit character (Kick..Rim)
    const int variant = idx % 5;        // 0..4 A..E micro-variation
    auto sp = [&](ParamID pid, float v) noexcept {
        params[static_cast<size_t>(pid)] = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    };
    auto g = [&](ParamID pid) noexcept { return params[static_cast<size_t>(pid)]; };

    // Per-family character: pitch (T) and length (D) biases per drum, plus
    // drive/metal, so each family reads as a different kit.
    struct KitBias { float kickT, kickD, snT, snD, hatT, hatD, cowT, tomT, tomD, drive, metal, clapD; };
    static const KitBias kBias[8] = {
        /*Kick */ {-0.12f, 0.12f,-0.05f, 0.00f,-0.06f, 0.02f,-0.06f,-0.10f, 0.08f, 0.12f,-0.06f, 0.02f},
        /*Snare*/ { 0.05f,-0.05f, 0.11f, 0.11f, 0.04f,-0.02f, 0.02f, 0.04f,-0.04f, 0.06f, 0.05f,-0.04f},
        /*CHat */ { 0.02f,-0.06f, 0.05f,-0.05f, 0.13f,-0.09f, 0.08f, 0.02f,-0.06f, 0.02f, 0.15f,-0.06f},
        /*OHat */ { 0.00f,-0.02f, 0.00f, 0.06f, 0.09f, 0.15f, 0.06f, 0.00f, 0.02f, 0.00f, 0.11f, 0.05f},
        /*Clap */ { 0.02f, 0.00f, 0.06f,-0.11f,-0.02f, 0.04f,-0.02f, 0.00f, 0.00f, 0.09f,-0.05f, 0.13f},
        /*Cowb */ { 0.05f,-0.02f, 0.02f, 0.02f, 0.06f, 0.02f, 0.15f, 0.06f,-0.02f, 0.04f, 0.11f,-0.02f},
        /*Tom  */ {-0.07f, 0.10f,-0.02f,-0.02f,-0.04f, 0.02f,-0.02f,-0.13f, 0.15f, 0.06f,-0.02f, 0.00f},
        /*Rim  */ { 0.07f,-0.09f, 0.08f,-0.07f, 0.06f,-0.06f, 0.04f, 0.06f,-0.07f, 0.02f, 0.06f,-0.06f},
    };
    const KitBias& b = kBias[family];
    const float vt = (static_cast<float>(variant) - 2.0f) * 0.045f; // tune spread A..E
    const float vd = (static_cast<float>(variant) - 2.0f) * 0.035f; // decay spread A..E

    // The default SidAuthentic drum model plays fixed canonical C64 drum
    // microprograms and does NOT respond to the per-drum tune/decay/tone knobs
    // (only accent). Only the AnalogX0X8 model expresses those params, so the
    // authored per-slot character below is audible only under AnalogX0X8. Select
    // it for these generic factory kits so each slot is a genuinely distinct,
    // tweakable kit instead of 32 aliases of the one fixed authentic set.
    sp(kParamDrSidMachineModel, 1.0f);

    sp(kParamDrSidKickTune,     g(kParamDrSidKickTune)     + b.kickT + vt);
    sp(kParamDrSidKickDecay,    g(kParamDrSidKickDecay)    + b.kickD - vd);
    sp(kParamDrSidSnareTone,    g(kParamDrSidSnareTone)    + b.snT   + vt);
    sp(kParamDrSidSnareSnap,    g(kParamDrSidSnareSnap)    + b.snD   + vd);
    sp(kParamDrSidHatTune,      g(kParamDrSidHatTune)      + b.hatT  + vt);
    sp(kParamDrSidHatDecay,     g(kParamDrSidHatDecay)     + b.hatD  + vd);
    sp(kParamDrSidCowbellTune,  g(kParamDrSidCowbellTune)  + b.cowT  + vt);
    sp(kParamDrSidTomTune,      g(kParamDrSidTomTune)      + b.tomT  + vt);
    sp(kParamDrSidTomDecay,     g(kParamDrSidTomDecay)     + b.tomD  + vd);
    sp(kParamDrSidClapDecay,    g(kParamDrSidClapDecay)    + b.clapD + vd);
    sp(kParamDrSidOutputDrive,  g(kParamDrSidOutputDrive)  + b.drive);
    sp(kParamDrSidHatMetal,     g(kParamDrSidHatMetal)     + b.metal);
}

inline void applyFactoryDrSidDefaults(const PatchDefinition& def,
                                      int slot,
                                      std::array<float, static_cast<size_t>(kNumParams)>& params) noexcept {
    // Audit #5/#39/#74 follow-up: gate the DrSID-defaults application
    // through the unified DrumContext classifier. The legacy predicate
    // (drSidMode || role==Drum || slot in legacy projection list) permitted
    // slots 120..127 to apply DrSID defaults — but the canonical factory
    // range now reserves 120..149 for SID-808 AnalogProjection. The
    // combined classifier resolves that conflict in favor of SID-808, so
    // any slot whose `factorySlotContext()` is SID808_AnalogProjection or
    // Digi4Bit must refuse to take DrSID-shaped defaults regardless of
    // what the legacy predicate says.
    const DrumContext slotCtx = factorySlotContext(slot);
    if (slotCtx == DrumContext::SID808_AnalogProjection ||
        slotCtx == DrumContext::Digi4Bit) {
        return;
    }
    if (!(def.staticState.drSidMode || def.usage.role == PatchRole::Drum || isAuthoredDrSidProjectionFactorySlot(slot))) return;

    auto sp = [&](ParamID pid, float v) noexcept {
        params[static_cast<size_t>(pid)] = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    };
    const int normalizedSlot = std::clamp(slot, 0, kFactoryPatchSlotCount - 1);

    sp(kParamSynthModeEnable, 0.0f);
    sp(kParamDrSidEnable, 1.0f);
    sp(kParamDrSidVolume, 0.84f);
    sp(kParamDrSidKickTune, 0.44f);
    sp(kParamDrSidKickDecay, 0.30f);
    sp(kParamDrSidSnareTone, 0.55f);
    sp(kParamDrSidSnareSnap, 0.70f);
    sp(kParamDrSidHatTune, 0.72f);
    sp(kParamDrSidHatDecay, 0.20f);
    sp(kParamDrSidClapDecay, 0.26f);
    sp(kParamDrSidCowbellTune, 0.74f);
    sp(kParamDrSidCowbellDecay, 0.38f);
    sp(kParamDrSidTomTune, 0.55f);
    sp(kParamDrSidTomDecay, 0.44f); // v909: longer singing 808 tom in the generic factory kit
    sp(kParamDrSidMachineModel, 0.0f);
    sp(kParamDrSidAccentAmount, 0.68f);
    sp(kParamDrSidOutputDrive, 0.18f);
    sp(kParamDrSidHatMetal, 0.62f);
    sp(kParamDrSidClapSpread, 0.54f);
    // v950: DrSID/SID808 factory roots are first-class DrSID authority.
    // Keep authored sequencer pattern data. Factory defaults still load
    // SeqEnable off unless a pattern/root explicitly owns drum transport.
    sp(kParamArpEnable, 0.0f);
    sp(kParamSeqEnable, 0.0f);
    sp(kParamSeqMode, 0.0f);
    sp(kParamSeqSwing, 0.04f);
    sp(kParamSeqTempo, 0.46f);
    sp(kParamSeqLength, normalizedFactorySeqLength(16));
    sp(kParamLFOSync, 1.0f);
    sp(kParamLFORate, 0.18f);
    sp(kParamLFODepth, 0.24f);
    sp(kParamLFOShape, 0.0f);
    sp(kParamLFO2Sync, 0.0f);
    sp(kParamLFO2Rate, 0.12f);
    sp(kParamLFO2Depth, 0.08f);

    sp(kParamModVCFCutoffSource, 0.0f);
    sp(kParamModVCFCutoffDepth, 0.0f);
    sp(kParamModVCFResonanceSource, 0.0f);
    sp(kParamModVCFResonanceDepth, 0.0f);
    sp(kParamModVCO1FreqSource, 0.0f);
    sp(kParamModVCO1FreqDepth, 0.0f);
    sp(kParamModVCO1PWSource, 0.0f);
    sp(kParamModVCO1PWDepth, 0.0f);
    sp(kParamModVCO2FreqSource, 0.0f);
    sp(kParamModVCO2FreqDepth, 0.0f);
    sp(kParamModVCO2PWSource, 0.0f);
    sp(kParamModVCO2PWDepth, 0.0f);
    sp(kParamModVCO3FreqSource, 0.0f);
    sp(kParamModVCO3FreqDepth, 0.0f);
    sp(kParamModVCO3PWSource, 0.0f);
    sp(kParamModVCO3PWDepth, 0.0f);
    sp(kParamModMasterVolumeSource, 0.0f);
    sp(kParamModMasterVolumeDepth, 0.0f);

    setFactoryModSource(params, kParamModVCFCutoffSource, SidModSource::Velocity);
    sp(kParamModVCFCutoffDepth, 0.10f);
    setFactoryModSource(params, kParamModVCO2PWSource, SidModSource::LFO1);
    sp(kParamModVCO2PWDepth, 0.12f);
    setFactoryModSource(params, kParamModMasterVolumeSource, SidModSource::LFO1);
    sp(kParamModMasterVolumeDepth, 0.08f);

    for (int i = 0; i < 32; ++i) setFactorySeqStep(params, i, 36, 0.0f, 0.0f);

    auto putStep = [&](int step, int midi, float velocity, float gate) noexcept {
        setFactorySeqStep(params, step, midi, velocity, gate);
    };

    static const int kGenericNotes[16] = {36,42,38,42,36,42,38,46,36,42,38,42,36,42,38,49};
    static const float kGenericVels[16] = {1.00f,0.70f,0.92f,0.66f,0.96f,0.72f,0.90f,0.78f,
                                           0.98f,0.70f,0.92f,0.68f,1.00f,0.74f,0.94f,0.82f};
    for (int i = 0; i < 16; ++i) putStep(i, kGenericNotes[i], kGenericVels[i], 1.0f);

    // Give the generic DrSID factory kits (80..111) distinct per-(family,variant)
    // character so the 32 slots are not aliases of one kit. Authored GM-percussion
    // slots (112..119) are excluded and keep their switch-case params below.
    applyFactoryDrSidNewKitCharacter_(normalizedSlot, params);

    switch (normalizedSlot) {
        case 47: { // Timpani
            sp(kParamDrSidVolume, 0.86f);
            sp(kParamDrSidKickTune, 0.36f);
            sp(kParamDrSidKickDecay, 0.48f);
            sp(kParamDrSidSnareTone, 0.30f);
            sp(kParamDrSidSnareSnap, 0.22f);
            sp(kParamDrSidHatTune, 0.45f);
            sp(kParamDrSidHatDecay, 0.08f);
            sp(kParamDrSidClapDecay, 0.12f);
            sp(kParamDrSidCowbellTune, 0.40f);
            sp(kParamDrSidCowbellDecay, 0.22f);
            sp(kParamDrSidTomTune, 0.24f);
            sp(kParamDrSidTomDecay, 0.62f);
            setFactoryModSource(params, kParamModVCO3PWSource, SidModSource::NoteNumber);
            sp(kParamModVCO3PWDepth, 0.18f);
            static const int notes[16] = {41,0,43,0,45,0,43,0,41,0,47,0,45,0,43,0};
            static const float vels[16] = {0.96f,0,0.88f,0,0.92f,0,0.86f,0,0.98f,0,1.00f,0,0.90f,0,0.84f,0};
            for (int i = 0; i < 16; ++i) putStep(i, notes[i] == 0 ? 41 : notes[i], vels[i], notes[i] == 0 ? 0.0f : 1.0f);
            break;
        }
        case 112: { // Tinkle Bell
            sp(kParamDrSidVolume, 0.76f);
            sp(kParamDrSidKickTune, 0.58f);
            sp(kParamDrSidKickDecay, 0.10f);
            sp(kParamDrSidSnareTone, 0.78f);
            sp(kParamDrSidSnareSnap, 0.84f);
            sp(kParamDrSidHatTune, 0.94f);
            sp(kParamDrSidHatDecay, 0.08f);
            sp(kParamDrSidClapDecay, 0.10f);
            sp(kParamDrSidCowbellTune, 0.96f);
            sp(kParamDrSidCowbellDecay, 0.16f);
            sp(kParamDrSidTomTune, 0.72f);
            sp(kParamDrSidTomDecay, 0.14f);
            setFactoryModSource(params, kParamModVCO3FreqSource, SidModSource::LFO1);
            sp(kParamModVCO3FreqDepth, 0.10f);
            static const int notes[16] = {56,0,80,0,56,0,68,0,56,0,80,0,68,0,56,0};
            static const float vels[16] = {0.92f,0,0.78f,0,0.96f,0,0.72f,0,0.90f,0,0.82f,0,0.74f,0,1.00f,0};
            for (int i = 0; i < 16; ++i) putStep(i, notes[i] == 0 ? 56 : notes[i], vels[i], notes[i] == 0 ? 0.0f : 1.0f);
            break;
        }
        case 113: { // Agogo
            sp(kParamDrSidVolume, 0.80f);
            sp(kParamDrSidKickTune, 0.54f);
            sp(kParamDrSidKickDecay, 0.14f);
            sp(kParamDrSidSnareTone, 0.66f);
            sp(kParamDrSidSnareSnap, 0.58f);
            sp(kParamDrSidHatTune, 0.82f);
            sp(kParamDrSidHatDecay, 0.12f);
            sp(kParamDrSidClapDecay, 0.10f);
            sp(kParamDrSidCowbellTune, 0.86f);
            sp(kParamDrSidCowbellDecay, 0.22f);
            sp(kParamDrSidTomTune, 0.64f);
            sp(kParamDrSidTomDecay, 0.20f);
            setFactoryModSource(params, kParamModVCFCutoffSource, SidModSource::Velocity);
            sp(kParamModVCFCutoffDepth, 0.12f);
            static const int notes[16] = {67,0,68,0,67,0,68,0,56,0,68,0,67,0,56,0};
            static const float vels[16] = {0.92f,0,0.80f,0,0.98f,0,0.84f,0,0.72f,0,0.88f,0,0.90f,0,0.76f,0};
            for (int i = 0; i < 16; ++i) putStep(i, notes[i] == 0 ? 67 : notes[i], vels[i], notes[i] == 0 ? 0.0f : 1.0f);
            break;
        }
        case 114: { // Steel Drums
            sp(kParamDrSidVolume, 0.84f);
            sp(kParamDrSidKickTune, 0.50f);
            sp(kParamDrSidKickDecay, 0.16f);
            sp(kParamDrSidSnareTone, 0.60f);
            sp(kParamDrSidSnareSnap, 0.42f);
            sp(kParamDrSidHatTune, 0.74f);
            sp(kParamDrSidHatDecay, 0.10f);
            sp(kParamDrSidClapDecay, 0.12f);
            sp(kParamDrSidCowbellTune, 0.72f);
            sp(kParamDrSidCowbellDecay, 0.26f);
            sp(kParamDrSidTomTune, 0.84f);
            sp(kParamDrSidTomDecay, 0.38f);
            setFactoryModSource(params, kParamModVCO3PWSource, SidModSource::NoteNumber);
            sp(kParamModVCO3PWDepth, 0.18f);
            static const int notes[16] = {60,62,64,67,69,67,64,62,60,62,64,67,71,69,67,64};
            static const float vels[16] = {0.88f,0.80f,0.86f,0.94f,1.00f,0.92f,0.84f,0.78f,
                                           0.90f,0.82f,0.88f,0.96f,0.98f,0.90f,0.86f,0.80f};
            for (int i = 0; i < 16; ++i) putStep(i, notes[i], vels[i], 1.0f);
            break;
        }
        case 115: { // Woodblock
            sp(kParamDrSidVolume, 0.78f);
            sp(kParamDrSidKickTune, 0.62f);
            sp(kParamDrSidKickDecay, 0.08f);
            sp(kParamDrSidSnareTone, 0.74f);
            sp(kParamDrSidSnareSnap, 0.52f);
            sp(kParamDrSidHatTune, 0.86f);
            sp(kParamDrSidHatDecay, 0.06f);
            sp(kParamDrSidClapDecay, 0.08f);
            sp(kParamDrSidCowbellTune, 0.68f);
            sp(kParamDrSidCowbellDecay, 0.14f);
            sp(kParamDrSidTomTune, 0.58f);
            sp(kParamDrSidTomDecay, 0.12f);
            setFactoryModSource(params, kParamModVCO1PWSource, SidModSource::Velocity);
            sp(kParamModVCO1PWDepth, 0.10f);
            static const int notes[16] = {37,0,76,0,77,0,76,0,37,0,76,0,77,0,58,0};
            static const float vels[16] = {0.94f,0,0.78f,0,0.84f,0,0.80f,0,0.98f,0,0.82f,0,0.86f,0,0.76f,0};
            for (int i = 0; i < 16; ++i) putStep(i, notes[i] == 0 ? 37 : notes[i], vels[i], notes[i] == 0 ? 0.0f : 1.0f);
            break;
        }
        case 116: { // Taiko Drum
            sp(kParamDrSidKickTune, 0.32f);
            sp(kParamDrSidKickDecay, 0.60f);
            sp(kParamDrSidSnareTone, 0.28f);
            sp(kParamDrSidSnareSnap, 0.30f);
            sp(kParamDrSidHatTune, 0.40f);
            sp(kParamDrSidHatDecay, 0.10f);
            sp(kParamDrSidClapDecay, 0.14f);
            sp(kParamDrSidCowbellTune, 0.35f);
            sp(kParamDrSidCowbellDecay, 0.20f);
            sp(kParamDrSidTomTune, 0.22f);
            sp(kParamDrSidTomDecay, 0.58f);
            setFactoryModSource(params, kParamModVCFResonanceSource, SidModSource::Velocity);
            sp(kParamModVCFResonanceDepth, 0.14f);
            sp(kParamModMasterVolumeDepth, 0.05f);
            static const int notes[16] = {36,0,36,0,47,0,36,0,36,0,43,0,47,0,36,49};
            static const float vels[16] = {1.00f,0,0.92f,0,0.86f,0,0.96f,0,1.00f,0,0.82f,0,0.88f,0,0.98f,0.72f};
            for (int i = 0; i < 16; ++i) putStep(i, notes[i] == 0 ? 36 : notes[i], vels[i], notes[i] == 0 ? 0.0f : 1.0f);
            break;
        }
        case 117: { // Melodic Tom
            sp(kParamDrSidKickTune, 0.48f);
            sp(kParamDrSidKickDecay, 0.22f);
            sp(kParamDrSidSnareTone, 0.44f);
            sp(kParamDrSidSnareSnap, 0.34f);
            sp(kParamDrSidHatTune, 0.58f);
            sp(kParamDrSidHatDecay, 0.12f);
            sp(kParamDrSidClapDecay, 0.18f);
            sp(kParamDrSidCowbellTune, 0.46f);
            sp(kParamDrSidCowbellDecay, 0.28f);
            sp(kParamDrSidTomTune, 0.70f);
            sp(kParamDrSidTomDecay, 0.48f);
            setFactoryModSource(params, kParamModVCO3PWSource, SidModSource::NoteNumber);
            sp(kParamModVCO3PWDepth, 0.22f);
            static const int notes[16] = {41,43,45,47,48,50,48,47,45,43,41,43,45,47,48,50};
            static const float vels[16] = {0.92f,0.84f,0.88f,0.92f,0.96f,1.00f,0.90f,0.88f,
                                           0.86f,0.82f,0.90f,0.84f,0.88f,0.92f,0.96f,1.00f};
            for (int i = 0; i < 16; ++i) putStep(i, notes[i], vels[i], 1.0f);
            break;
        }
        case 118: { // Synth Drum
            sp(kParamDrSidKickTune, 0.62f);
            sp(kParamDrSidKickDecay, 0.32f);
            sp(kParamDrSidSnareTone, 0.70f);
            sp(kParamDrSidSnareSnap, 0.78f);
            sp(kParamDrSidHatTune, 0.84f);
            sp(kParamDrSidHatDecay, 0.22f);
            sp(kParamDrSidClapDecay, 0.20f);
            sp(kParamDrSidCowbellTune, 0.82f);
            sp(kParamDrSidCowbellDecay, 0.34f);
            sp(kParamDrSidTomTune, 0.78f);
            sp(kParamDrSidTomDecay, 0.34f);
            sp(kParamLFO2Sync, 1.0f);
            sp(kParamLFO2Rate, 0.28f);
            sp(kParamLFO2Depth, 0.18f);
            setFactoryModSource(params, kParamModVCO3PWSource, SidModSource::LFO2);
            sp(kParamModVCO3PWDepth, 0.18f);
            setFactoryModSource(params, kParamModVCO3FreqSource, SidModSource::LFO1);
            sp(kParamModVCO3FreqDepth, 0.14f);
            static const int notes[16] = {36,42,38,50,36,44,38,49,36,42,38,50,36,46,53,57};
            static const float vels[16] = {1.00f,0.72f,0.94f,0.84f,0.96f,0.68f,0.90f,0.80f,
                                           1.00f,0.74f,0.92f,0.86f,0.98f,0.82f,0.78f,0.88f};
            for (int i = 0; i < 16; ++i) putStep(i, notes[i], vels[i], 1.0f);
            break;
        }
        case 119: { // Reverse Cymbal
            sp(kParamDrSidVolume, 0.80f);
            sp(kParamDrSidKickTune, 0.50f);
            sp(kParamDrSidKickDecay, 0.16f);
            sp(kParamDrSidSnareTone, 0.34f);
            sp(kParamDrSidSnareSnap, 0.26f);
            sp(kParamDrSidHatTune, 0.92f);
            sp(kParamDrSidHatDecay, 0.72f);
            sp(kParamDrSidClapDecay, 0.16f);
            sp(kParamDrSidCowbellTune, 0.78f);
            sp(kParamDrSidCowbellDecay, 0.24f);
            sp(kParamDrSidTomTune, 0.56f);
            sp(kParamDrSidTomDecay, 0.20f);
            sp(kParamLFORate, 0.08f);
            setFactoryModSource(params, kParamModVCO2PWSource, SidModSource::LFO1);
            sp(kParamModVCO2PWDepth, 0.24f);
            setFactoryModSource(params, kParamModMasterVolumeSource, SidModSource::LFO1);
            sp(kParamModMasterVolumeDepth, 0.12f);
            static const int notes[16] = {57,0,49,0,55,0,57,0,49,0,57,0,55,0,49,0};
            static const float vels[16] = {0.86f,0,0.92f,0,0.80f,0,0.96f,0,0.88f,0,1.00f,0,0.84f,0,0.92f,0};
            for (int i = 0; i < 16; ++i) putStep(i, notes[i] == 0 ? 57 : notes[i], vels[i], notes[i] == 0 ? 0.0f : 1.0f);
            break;
        }
        // v858 split-brain removal: per-slot cases 120..124 and 127 used to
        // live here, carrying kit tunings and sequencer pages that DIVERGED from
        // the live authority. They were DEAD CODE: factorySlotContext(120..149)
        // is SID808_AnalogProjection (static_assert-pinned in drum_context.h),
        // so this function early-returns for every one of those slots and the
        // cases could never execute — while still inviting edits to the wrong
        // table. The single live authority for SID-808 slots is
        // applyFactorySid808MissingLogicDefaults() +
        // factorySid808ParamSignatureForSlot() (derived from the kit voice-
        // config table in factory_sid808_kits.h).
        default:
            break;
    }
}

inline void applyFactoryArpSeqDefaults(const PatchDefinition& def,
                                       std::array<float, static_cast<size_t>(kNumParams)>& params) noexcept {
    const PatchUsageMetadata& usage = def.usage;
    const PatchMotionPolicy& motion = def.motion;
    const bool arpLike = usage.role == PatchRole::Arp || motion.pitchMotion == PitchMotionId::SteppedArp;
    const bool seqLike = usage.role == PatchRole::Drum || usage.role == PatchRole::FX || motion.trackerStepped;

    params[static_cast<size_t>(kParamArpEnable)] = arpLike ? 1.0f : 0.0f;
    params[static_cast<size_t>(kParamArpMode)] = usage.role == PatchRole::Arp ? 0.25f : params[static_cast<size_t>(kParamArpMode)];
    params[static_cast<size_t>(kParamArpSwing)] = usage.bias == TrackerLiveBias::Live ? 0.08f : 0.0f;
    params[static_cast<size_t>(kParamArpHold)] = 0.0f;
    params[static_cast<size_t>(kParamArpLatch)] = usage.role == PatchRole::Arp ? 1.0f : 0.0f;
    params[static_cast<size_t>(kParamArpTranspose)] = 0.5f;
    params[static_cast<size_t>(kParamArpPatternLength)] = usage.intendedDensity >= 3 ? 0.22f : 0.10f;

    params[static_cast<size_t>(kParamSeqEnable)] = seqLike ? 1.0f : 0.0f;
    params[static_cast<size_t>(kParamSeqTempo)] = usage.bias == TrackerLiveBias::Tracker ? 0.33f : (usage.role == PatchRole::Drum ? 0.48f : 0.40f);
    params[static_cast<size_t>(kParamSeqSwing)] = usage.role == PatchRole::Drum ? 0.06f : 0.0f;
    params[static_cast<size_t>(kParamSeqMode)] = usage.role == PatchRole::Drum ? 0.25f : 0.0f;
    params[static_cast<size_t>(kParamSeqLength)] = seqLike ? 0.22f : 0.50f; // ~8 steps when quantized UI-side

    for (int i = 0; i < 32; ++i) setFactorySeqStep(params, i, 60, 1.0f, 0.0f);

    if (usage.role == PatchRole::Drum) {
        const int notes[8] = {36, 38, 42, 38, 36, 42, 43, 42};
        const float vels[8] = {1.0f, 0.82f, 0.72f, 0.78f, 0.96f, 0.70f, 0.66f, 0.74f};
        for (int i = 0; i < 8; ++i) setFactorySeqStep(params, i, notes[i], vels[i], 1.0f);
    } else if (arpLike) {
        const int notes[8] = {60, 64, 67, 72, 67, 64, 72, 79};
        for (int i = 0; i < 8; ++i) setFactorySeqStep(params, i, notes[i], 0.90f, 1.0f);
    } else if (seqLike) {
        const int notes[8] = {48, 55, 60, 67, 48, 55, 62, 67};
        for (int i = 0; i < 8; ++i) setFactorySeqStep(params, i, notes[i], 0.88f, (i == 3 || i == 7) ? 0.7f : 1.0f);
    }
}

inline void applyFactoryForensicDefaults(const PatchDefinition& def,
                                         std::array<float, static_cast<size_t>(kNumParams)>& params) noexcept {
    const auto auth = def.usage.authenticity;
    const bool forensic = auth >= AuthenticityGrade::Forensic;
    const bool strong = auth >= AuthenticityGrade::Strong;
    const bool bass6581 = def.usage.role == PatchRole::Bass && def.staticState.chip == SidChipTarget::MOS6581;
    const bool bass8580 = def.usage.role == PatchRole::Bass && def.staticState.chip == SidChipTarget::MOS8580;
    auto sp=[&](ParamID pid,float v) noexcept { params[static_cast<size_t>(pid)] = std::clamp(std::isfinite(v)?v:0.0f,0.0f,1.0f); };

    // ── Per-patch forensic override (authentic patches) ────────────────────
    if (def.staticState.forensicOverride) {
        const auto& s = def.staticState;
        sp(kParamForensicEnable, s.forensicEnable);
        sp(kParamForensicIntensity, s.forensicIntensity);
        sp(kParamForensicClockJitterEnable, s.clockJitterEnable ? 1.0f : 0.0f);
        sp(kParamForensicClockJitter, s.clockJitter);
        sp(kParamForensicSupplyRippleEnable, s.supplyRippleEnable ? 1.0f : 0.0f);
        sp(kParamForensicSupplyRipple, s.supplyRipple);
        sp(kParamForensicThermalDriftEnable, s.thermalDriftEnable ? 1.0f : 0.0f);
        sp(kParamForensicThermalDrift, s.thermalDrift);
        sp(kParamForensicVoiceCrosstalkEnable, s.voiceCrosstalkEnable ? 1.0f : 0.0f);
        sp(kParamForensicVoiceCrosstalk, s.voiceCrosstalk);
        sp(kParamForensicExternalBleedEnable, s.externalBleedEnable ? 1.0f : 0.0f);
        sp(kParamForensicExternalBleed, s.externalBleed);
        sp(kParamForensicDigifix8580, s.digifix8580 ? 1.0f : 0.0f);
        sp(kParamForensicEnvelopeTDM, s.envelopeTDM);
        sp(kParamForensicD418Asymmetry, s.d418Asymmetry);
        sp(kParamForensicFilterOhmic, s.filterOhmic);
        sp(kParamForensicSystemNoise, s.systemNoise);
        sp(kParamForensicMotherboard, s.motherboard);
        sp(kParamForensicADCBleed, s.adcBleed);
        sp(kParamForensicBusCollision, s.busCollision);
        sp(kParamForensicPOTInput, s.potInput);
        sp(kParamForensicStartupRandom, forensic || strong ? 1.0f : 0.0f);
        sp(kParamSidAdsrBug6581, s.adsrBug6581 ? 1.0f : 0.0f);
        return;
    }

    // ── Grade-based defaults ───────────────────────────────────────────────
    sp(kParamForensicEnable, forensic ? 1.0f : (strong ? 1.0f : 0.0f));
    sp(kParamForensicStartupRandom, strong ? 1.0f : 0.0f);
    sp(kParamForensicClockJitterEnable, strong ? 1.0f : 0.0f);
    sp(kParamForensicClockJitter, forensic ? 0.28f : (strong ? 0.12f : 0.0f));
    sp(kParamForensicSupplyRippleEnable, strong ? 1.0f : 0.0f);
    sp(kParamForensicSupplyRipple, def.usage.role == PatchRole::Bass ? (bass8580 ? 0.06f : 0.08f) : (forensic ? 0.14f : 0.0f));
    sp(kParamForensicThermalDriftEnable, forensic ? 1.0f : 0.0f);
    sp(kParamForensicThermalDrift, forensic ? 0.18f : 0.0f);
    sp(kParamForensicVoiceCrosstalkEnable, strong ? 1.0f : 0.0f);
    sp(kParamForensicVoiceCrosstalk, def.usage.role == PatchRole::Bass ? (forensic ? (bass8580 ? 0.03f : 0.04f) : 0.0f)
                                      : (def.usage.polyAllowed ? 0.20f : (forensic ? 0.12f : 0.0f)));
    sp(kParamForensicExternalBleedEnable, strong ? 1.0f : 0.0f);
    sp(kParamForensicExternalBleed, def.usage.role == PatchRole::Bass ? (forensic ? (bass8580 ? 0.02f : 0.03f) : 0.0f)
                                      : (def.usage.role == PatchRole::FX ? 0.24f : (forensic ? 0.08f : 0.0f)));
    sp(kParamForensicDigifix8580, def.staticState.chip == SidChipTarget::MOS8580 ? 1.0f : 0.0f);
    sp(kParamForensicIntensity, forensic ? 0.88f : (strong ? 0.55f : 0.20f));
    sp(kParamForensicEnvelopeTDM, def.usage.role == PatchRole::Arp ? 0.16f : (forensic ? 0.10f : 0.0f));
    sp(kParamForensicD418Asymmetry, def.usage.role == PatchRole::Bass
                                      ? (bass6581 ? 0.10f : 0.03f)
                                      : (def.staticState.chip == SidChipTarget::MOS6581 ? 0.20f : 0.06f));
    sp(kParamForensicFilterOhmic, def.usage.role == PatchRole::Bass ? (bass8580 ? 0.06f : 0.08f) : (forensic ? 0.12f : 0.0f));
    sp(kParamForensicSystemNoise, def.usage.role == PatchRole::Bass ? (strong ? (bass8580 ? 0.04f : 0.05f) : 0.02f)
                                  : (strong ? 0.14f : 0.04f));
    sp(kParamForensicMotherboard, def.usage.role == PatchRole::Bass ? (forensic ? (bass8580 ? 0.04f : 0.05f) : (strong ? 0.02f : 0.0f))
                                  : (forensic ? 0.18f : (strong ? 0.08f : 0.0f)));
    sp(kParamForensicADCBleed, def.usage.role == PatchRole::FX ? 0.18f : (forensic ? 0.10f : 0.0f));
    sp(kParamForensicBusCollision, def.usage.role == PatchRole::Arp ? 0.18f : (forensic ? 0.08f : 0.0f));
    sp(kParamForensicPOTInput, def.usage.role == PatchRole::Bell ? 0.10f : (forensic ? 0.06f : 0.0f));
}


inline void applyFactorySidRegisterMirrors(const PatchDefinition& def,
                                           std::array<float, static_cast<size_t>(kNumParams)>& params) noexcept {
    auto sp=[&](ParamID pid,float v) noexcept { params[static_cast<size_t>(pid)] = std::clamp(std::isfinite(v)?v:0.0f,0.0f,1.0f); };

    // ── Direct register snapshot path (authentic patches) ──────────────────
    // When regSnap.valid=true, use the raw bytes from the patch definition
    // directly. This enables 100% hardware-accurate SID register encoding.
    if (def.staticState.regSnap.valid) {
        std::array<uint8_t, 30> r = def.staticState.regSnap.r;
        // even authentic raw snapshots must not reintroduce output leakage
        // after factory voice-mix finalization. Keep oscillator/frequency state
        // for internal ring/sync sources, but force audible mixer/filter mirrors
        // from the canonical authored VCO levels.
        const bool v1Audible = params[static_cast<size_t>(kParamVCO1Level)] > 0.0001f;
        const bool v2Audible = params[static_cast<size_t>(kParamVCO2Level)] > 0.0001f;
        const bool v3Audible = params[static_cast<size_t>(kParamVCO3Level)] > 0.0001f;
        // A voice that is inaudible but required as an internal ring/sync source
        // must keep its control byte (oscillator runs); only fully unused voices
        // are zeroed.  V3OFF (D418 bit7) still mutes V3's output when inaudible.
        const bool v1MustRun = v1Audible || factoryVoiceMustRunAsInternalSource(def.staticState, 0);
        const bool v2MustRun = v2Audible || factoryVoiceMustRunAsInternalSource(def.staticState, 1);
        const bool v3MustRun = v3Audible || factoryVoiceMustRunAsInternalSource(def.staticState, 2);
        if (!v1MustRun) r[4]  = 0x00u;
        if (!v2MustRun) r[11] = 0x00u;
        if (!v3MustRun) r[18] = 0x00u;
        // $D417 FILT route is explicit patch authorship, not a level inference.
        r[23] = static_cast<uint8_t>((r[23] & 0xF8u) | (def.staticState.filterRouteMask & 0x07u));
        if (!v3Audible) r[24] = static_cast<uint8_t>(r[24] | 0x80u);
        else            r[24] = static_cast<uint8_t>(r[24] & 0x7Fu);
        const ParamID base = kParamSidRegD400;
        for (int i = 0; i < 30; ++i)
            sp(static_cast<ParamID>(static_cast<int>(base) + i), static_cast<float>(r[(size_t)i]) / 255.0f);
        return;
    }

    // ── Derived path (high-level params → SID registers) ───────────────────
    const uint16_t pw1 = quantizeSidPw12(params[static_cast<size_t>(kParamVCO1PulseWidth)]);
    const uint16_t pw2 = quantizeSidPw12(params[static_cast<size_t>(kParamVCO2PulseWidth)]);
    const uint16_t pw3 = quantizeSidPw12(params[static_cast<size_t>(kParamVCO3PulseWidth)]);
    const uint8_t ad = packSidAdByte(params[static_cast<size_t>(kParamAttack)], params[static_cast<size_t>(kParamDecay)]);
    const uint8_t sr = packSidSrByte(params[static_cast<size_t>(kParamSustain)], params[static_cast<size_t>(kParamRelease)]);
    const bool v1Src = factoryVoiceMustRunAsInternalSource(def.staticState, 0);
    const bool v2Src = factoryVoiceMustRunAsInternalSource(def.staticState, 1);
    const bool v3Src = factoryVoiceMustRunAsInternalSource(def.staticState, 2);
    const uint8_t c1 = factoryControlByte(params[static_cast<size_t>(kParamVCO1Waveform)], params[static_cast<size_t>(kParamVCO1Level)], def.staticState.vco1Sync, def.staticState.vco1Ring, v1Src);
    const uint8_t c2 = factoryControlByte(params[static_cast<size_t>(kParamVCO2Waveform)], params[static_cast<size_t>(kParamVCO2Level)], def.staticState.vco2Sync, def.staticState.vco2Ring, v2Src);
    const uint8_t c3 = factoryControlByte(params[static_cast<size_t>(kParamVCO3Waveform)], params[static_cast<size_t>(kParamVCO3Level)], def.staticState.vco3Sync, def.staticState.vco3Ring, v3Src);
    // seed neutral non-zero frequencies so mirrors are materialized and visible on load
    const uint16_t f1 = 0x1148u, f2 = 0x1180u, f3 = 0x11C0u;
    sp(kParamSidRegD400, static_cast<float>(f1 & 0xFFu) / 255.0f);
    sp(kParamSidRegD401, static_cast<float>((f1 >> 8) & 0xFFu) / 255.0f);
    sp(kParamSidRegD402, static_cast<float>(pw1 & 0xFFu) / 255.0f);
    sp(kParamSidRegD403, static_cast<float>((pw1 >> 8) & 0x0Fu) / 255.0f);
    sp(kParamSidRegD404, static_cast<float>(c1) / 255.0f);
    sp(kParamSidRegD405, static_cast<float>(ad) / 255.0f);
    sp(kParamSidRegD406, static_cast<float>(sr) / 255.0f);
    sp(kParamSidRegD407, static_cast<float>(f2 & 0xFFu) / 255.0f);
    sp(kParamSidRegD408, static_cast<float>((f2 >> 8) & 0xFFu) / 255.0f);
    sp(kParamSidRegD409, static_cast<float>(pw2 & 0xFFu) / 255.0f);
    sp(kParamSidRegD40A, static_cast<float>((pw2 >> 8) & 0x0Fu) / 255.0f);
    sp(kParamSidRegD40B, static_cast<float>(c2) / 255.0f);
    sp(kParamSidRegD40C, static_cast<float>(ad) / 255.0f);
    sp(kParamSidRegD40D, static_cast<float>(sr) / 255.0f);
    sp(kParamSidRegD40E, static_cast<float>(f3 & 0xFFu) / 255.0f);
    sp(kParamSidRegD40F, static_cast<float>((f3 >> 8) & 0xFFu) / 255.0f);
    sp(kParamSidRegD410, static_cast<float>(pw3 & 0xFFu) / 255.0f);
    sp(kParamSidRegD411, static_cast<float>((pw3 >> 8) & 0x0Fu) / 255.0f);
    sp(kParamSidRegD412, static_cast<float>(c3) / 255.0f);
    sp(kParamSidRegD413, static_cast<float>(ad) / 255.0f);
    sp(kParamSidRegD414, static_cast<float>(sr) / 255.0f);
    const int fc = std::clamp<int>(std::lround(params[static_cast<size_t>(kParamFilterCutoff)] * 2047.0f), 0, 2047);
    sp(kParamSidRegD415, static_cast<float>(fc & 0x07u) / 255.0f);
    sp(kParamSidRegD416, static_cast<float>((fc >> 3) & 0xFFu) / 255.0f);
    // $D417 filter route is explicit patch authorship (filterRouteMask), not an
    // inference from which oscillators happen to be audible.
    const uint8_t explicitFilterRoute = static_cast<uint8_t>(def.staticState.filterRouteMask & 0x07u);
    const uint8_t resFilt = static_cast<uint8_t>(((std::clamp<int>(std::lround(params[static_cast<size_t>(kParamFilterResonance)] * 15.0f),0,15) & 0x0F) << 4) | explicitFilterRoute);
    sp(kParamSidRegD417, static_cast<float>(resFilt) / 255.0f);
    const int volNib = std::clamp<int>(std::lround(params[static_cast<size_t>(kParamMasterVolume)] * 15.0f),0,15);
    uint8_t modeVol = static_cast<uint8_t>(volNib & 0x0Fu);
    const float mode = params[static_cast<size_t>(kParamFilterMode)];
    if (mode < 0.25f) modeVol |= 0x10u;
    else if (mode < 0.50f) modeVol |= 0x20u;
    else if (mode < 0.75f) modeVol |= 0x40u;
    else modeVol |= 0x70u;
    if (params[static_cast<size_t>(kParamVCO3Level)] <= 0.0001f) modeVol |= 0x80u;
    sp(kParamSidRegD418, static_cast<float>(modeVol) / 255.0f);
    sp(kParamSidRegD419, 0.0f); sp(kParamSidRegD41A, 0.0f); sp(kParamSidRegD41B, 0.0f); sp(kParamSidRegD41C, 0.0f);
    SidVariantProfile pseudoProfile{};
    switch (def.staticState.chip) {
        case SidChipTarget::MOS8580: pseudoProfile.family = SidFamily::MOS8580; break;
        case SidChipTarget::MOS6581: pseudoProfile.family = SidFamily::MOS6581; break;
        case SidChipTarget::AnyPortable: default: pseudoProfile.family = SidFamily::MOS8580; break;
    }
    pseudoProfile.video_standard = (def.staticState.clock == ClockTarget::NTSC_First)
                                 ? SidVideoStandard::NTSC
                                 : SidVideoStandard::PAL;
    uint8_t sysByte = sidSystemByteFromVariantProfile(
        pseudoProfile,
        params[static_cast<size_t>(kParamSidAdsrBug6581)] > 0.5f);
    if (params[static_cast<size_t>(kParamForensicEnable)] > 0.5f || def.staticState.forensicOverride)
        sysByte |= 0x80u;
    sp(kParamSidRegD41D, static_cast<float>(sysByte) / 255.0f);
}




inline int normalizeFactoryPatchSlot(int slot) noexcept {
    return std::clamp(slot, 0, kFactoryPatchSlotCount - 1);
}

inline bool isCanonicalDrumFactorySlotForRoot(int slot) noexcept {
    return ArpSID::isDrSidFactorySlot(slot) ||
           ArpSID::isSid808FactorySlot(slot) ||
           ArpSID::isDigiFactorySlot(slot);
}

inline int canonicalFactorySlotForRoot(int slot) noexcept {
    if (isCanonicalDrumFactorySlotForRoot(slot)) return slot;
    return normalizeFactoryPatchSlot(slot);
}

inline float normalizedFactoryVoiceMode(const PatchUsageMetadata& usage, const PatchMotionPolicy& motion) noexcept {
    // 0=Poly, 1=Mono, 2=Legato, 3=Unison -> normalized by /3
    const int mode = usage.polyAllowed ? 0
                    : (motion.legatoSafe && !usage.monoPreferred) ? 2
                    : usage.monoPreferred ? 1
                    : 0;
    return static_cast<float>(mode) / 3.0f;
}

inline float normalizedFactoryClockSystem(ClockTarget clock) noexcept {
    return (clock == ClockTarget::NTSC_First) ? 1.0f : 0.0f;
}

inline bool isFactoryPerformanceSensitiveGuitar(const PatchDefinition& def) noexcept {
    if (def.usage.family != HistoricalFamilyId::CoreLead || def.usage.role != PatchRole::Lead) return false;
    return def.displayName == "Overdriven Guitar" || def.displayName == "Distortion Guitar";
}

inline float normalizedFactoryPortamentoTime(const PatchUsageMetadata& usage,
                                            const PatchMotionPolicy& motion) noexcept {
    // Bass must use a short, low-end-safe trajectory so note changes don't smear
    // the fundamental. Portamento style (C64 register-slide vs. linear-semitone
    // vs. smooth-synth) is selected by kParamPortamentoStyle — a separate
    // discrete control — so the per-role time below only governs how long the
    // glide takes, not which law applies. Bass stays shorter than other roles.
    if (motion.pitchMotion == PitchMotionId::TrackerSlide) {
        return usage.role == PatchRole::Bass ? 0.07f : 0.10f;
    }
    if (motion.pitchMotion == PitchMotionId::Glide) {
        return usage.role == PatchRole::Bass ? 0.08f : 0.18f;
    }
    return 0.0f;
}

inline bool factoryOscillatorNeedsPitchMotion(const PatchStaticState& s, int osc) noexcept {
    // An oscillator needs pitch motion if it is audible, self-modulated (its own
    // ring/sync), or required to run as an internal ring/sync source for another
    // voice (V1<-V3, V2<-V1, V3<-V2).
    switch (osc) {
        case 0:
            return (std::isfinite(s.vco1LevelNorm) && s.vco1LevelNorm > 0.0001)
                   || s.vco1Sync || s.vco1Ring || factoryVoiceMustRunAsInternalSource(s, 0);
        case 1:
            return (std::isfinite(s.vco2LevelNorm) && s.vco2LevelNorm > 0.0001)
                   || s.vco2Sync || s.vco2Ring || factoryVoiceMustRunAsInternalSource(s, 1);
        case 2:
            return (std::isfinite(s.vco3LevelNorm) && s.vco3LevelNorm > 0.0001)
                   || s.vco3Sync || s.vco3Ring || factoryVoiceMustRunAsInternalSource(s, 2);
        default:
            return false;
    }
}

inline float normalizedFactoryVibratoRate(const PatchDefinition& def) noexcept {
    if (def.displayName == "Vibraphone") return 0.20f;
    switch (def.usage.role) {
        case PatchRole::Bell:      return 0.18f;
        case PatchRole::Lead:      return 0.22f;
        case PatchRole::Metallic:  return 0.16f;
        default:                   return 0.19f;
    }
}

inline float normalizedFactoryVibratoMatrixDepth(const PatchDefinition& def) noexcept {
    if (def.displayName == "Vibraphone") return 0.42f;
    switch (def.usage.role) {
        case PatchRole::Bell:      return 0.34f;
        case PatchRole::Lead:      return 0.40f;
        case PatchRole::Metallic:  return 0.30f;
        default:                   return 0.36f;
    }
}

template <typename ParamsArray>
inline void applyFactoryPitchMotionDefaults(const PatchDefinition& def,
                                            ParamsArray& params) noexcept {
    if (def.motion.pitchMotion != PitchMotionId::Vibrato) return;

    auto sp = [&](ParamID pid, float v) noexcept {
        params[static_cast<size_t>(pid)] = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    };

    // Materialize authored vibrato as actual per-oscillator pitch motion.
    // Earlier factory loads only tagged the patch metadata, leaving the live
    // sound path on static detune/tremolo defaults.
    sp(kParamLFO2Sync, 0.0f);
    sp(kParamLFO2Shape, 0.0f);
    sp(kParamLFO2Rate, normalizedFactoryVibratoRate(def));
    sp(kParamLFO2Depth, def.displayName == "Vibraphone" ? 0.96f : 0.88f);

    const float baseDepth = normalizedFactoryVibratoMatrixDepth(def);
    if (factoryOscillatorNeedsPitchMotion(def.staticState, 0)) {
        setFactoryModSource(params, kParamModVCO1FreqSource, SidModSource::LFO2);
        sp(kParamModVCO1FreqDepth, baseDepth);
    }
    if (factoryOscillatorNeedsPitchMotion(def.staticState, 1)) {
        setFactoryModSource(params, kParamModVCO2FreqSource, SidModSource::LFO2);
        sp(kParamModVCO2FreqDepth, std::max(0.0f, baseDepth - 0.04f));
    }
    if (factoryOscillatorNeedsPitchMotion(def.staticState, 2)) {
        setFactoryModSource(params, kParamModVCO3FreqSource, SidModSource::LFO2);
        sp(kParamModVCO3FreqDepth, std::max(0.0f, baseDepth - 0.02f));
    }

    if (def.motion.pulseMotion == PulseMotionId::Static) {
        sp(kParamModVCO2PWDepth, 0.0f);
    }

    // Pitch-vibrato patches do not want the generic tremolo bias unless they
    // are explicitly bell/motor-style voices.
    sp(kParamModMasterVolumeDepth,
       (def.usage.role == PatchRole::Bell || def.usage.role == PatchRole::Metallic) ? 0.03f : 0.0f);
}

inline void applyFactoryPatchDefinitionToNormalizedParams(const PatchDefinition& def,
                                                          int slot,
                                                          std::array<float, static_cast<size_t>(kNumParams)>& params) noexcept {
    const PatchStaticState& s = def.staticState;
    const PatchMotionPolicy& motion = def.motion;
    const PatchUsageMetadata& usage = def.usage;

    auto sp = [&](ParamID pid, float v) noexcept {
        const size_t idx = static_cast<size_t>(pid);
        if (idx < params.size()) params[idx] = std::clamp(v, 0.0f, 1.0f);
    };

    sp(kParamMasterVolume,      static_cast<float>(s.masterVolumeNorm));
    sp(kParamVCO1Waveform,      static_cast<float>(s.vco1WaveNorm));
    sp(kParamVCO2Waveform,      static_cast<float>(s.vco2WaveNorm));
    sp(kParamVCO3Waveform,      static_cast<float>(s.vco3WaveNorm));
    sp(kParamVCO1Level,         static_cast<float>(s.vco1LevelNorm));
    sp(kParamVCO2Level,         static_cast<float>(s.vco2LevelNorm));
    sp(kParamVCO3Level,         static_cast<float>(s.vco3LevelNorm));
    sp(kParamVCO1PulseWidth,    static_cast<float>(s.vco1PulseWidthNorm));
    sp(kParamVCO2PulseWidth,    static_cast<float>(s.vco2PulseWidthNorm));
    sp(kParamVCO3PulseWidth,    static_cast<float>(s.vco3PulseWidthNorm));
    sp(kParamVCO1SyncEnable,    s.vco1Sync ? 1.0f : 0.0f);
    sp(kParamVCO2SyncEnable,    s.vco2Sync ? 1.0f : 0.0f);
    sp(kParamVCO3SyncEnable,    s.vco3Sync ? 1.0f : 0.0f);
    sp(kParamVCO1RingModEnable, s.vco1Ring ? 1.0f : 0.0f);
    sp(kParamVCO2RingModEnable, s.vco2Ring ? 1.0f : 0.0f);
    sp(kParamVCO3RingModEnable, s.vco3Ring ? 1.0f : 0.0f);
    sp(kParamAttack,            static_cast<float>(s.attackNorm));
    sp(kParamDecay,             static_cast<float>(s.decayNorm));
    sp(kParamSustain,           static_cast<float>(s.sustainNorm));
    sp(kParamRelease,           static_cast<float>(s.releaseNorm));
    sp(kParamFilterCutoff,      static_cast<float>(s.filterCutoffNorm));
    sp(kParamFilterResonance,   static_cast<float>(s.filterResNorm));
    sp(kParamFilterMode,        static_cast<float>(s.filterModeNorm));
    sp(kParamFilterEnvAmount,   static_cast<float>(s.filterEnvAmountNorm));
    sp(kParamFilterDrive,       static_cast<float>(s.filterDriveNorm));
    // Legacy model/clock mirror params intentionally not populated here.
    // Canonical factory truth is the typed variant profile, not public mirror params.
    sp(kParamSynthModeEnable,   s.synthMode ? 1.0f : 0.0f);
    sp(kParamDrSidEnable,       s.drSidMode ? 1.0f : 0.0f);

    // Snapshot metadata must track the actual loaded factory slot.
    sp(kParamBankSlot,          canonicalNormalizedBankSlotValue(canonicalFactorySlotForRoot(slot)));
    sp(kParamProgram,           canonicalNormalizedFactoryProgramValue(canonicalFactorySlotForRoot(slot)));

    // Usage/motion-derived audible defaults so factory slots do not load with stale generic values.
    sp(kParamVoiceMode,         normalizedFactoryVoiceMode(usage, motion));
    sp(kParamPortamentoTime,    normalizedFactoryPortamentoTime(usage, motion));

    if (motion.pulseMotion != PulseMotionId::Static) {
        const float pwmDepth = (motion.pulseMotion == PulseMotionId::AudioPWM) ? 0.85f
                               : (motion.pulseMotion == PulseMotionId::TrackerPWM) ? 0.60f
                                                                                   : 0.35f;
        sp(kParamVCO1PWMDepth, pwmDepth);
        sp(kParamVCO2PWMDepth, pwmDepth * 0.8f);
        sp(kParamVCO3PWMDepth, pwmDepth * 0.6f);
    }

    const bool arpLike = usage.role == PatchRole::Arp || motion.pitchMotion == PitchMotionId::SteppedArp;
    sp(kParamArpEnable, arpLike ? 1.0f : 0.0f);
    if (arpLike) {
        sp(kParamArpRate, motion.trackerStepped ? 0.42f : 0.28f);
        sp(kParamArpGate, usage.role == PatchRole::Drum ? 0.55f : 0.82f);
        sp(kParamArpOctaves, usage.intendedDensity >= 3 ? (1.0f / 3.0f) : 0.0f);
        sp(kParamArpRandom, usage.bias == TrackerLiveBias::Live ? 0.10f : 0.0f);
    }

    if (usage.role == PatchRole::PadIllusion || usage.role == PatchRole::Chord) {
        sp(kParamVoiceSpread, 0.35f);
    } else if (isFactoryPerformanceSensitiveGuitar(def)) {
        sp(kParamVoiceSpread, 0.0f);
    } else if (usage.role == PatchRole::Lead && usage.authenticity >= AuthenticityGrade::Forensic) {
        sp(kParamVoiceSpread, 0.12f);
    }

    const bool perfGuitar = isFactoryPerformanceSensitiveGuitar(def);
    if (perfGuitar) {
        // Guitar performance closeout: these patches are transient-rich and already
        // use sync/filter bite. Do not add host-visible unison/spread, LFO2 motion,
        // PWM depth or oversampling by default; all remain user-selectable from GUI.
        sp(kParamSidOversamplingFactor, 0.0f); // 1x default for guitar patches
        sp(kParamVCO1PWMDepth, 0.04f);
        sp(kParamVCO2PWMDepth, 0.03f);
        sp(kParamVCO3PWMDepth, 0.0f);
    }

    // Broader factory defaults so bank loads materialize a complete, mode-agnostic authored state.
    sp(kParamLFORate, usage.role == PatchRole::PadIllusion ? 0.08f : (usage.role == PatchRole::Arp ? 0.18f : 0.06f));
    sp(kParamLFODepth, perfGuitar ? 0.03f : (motion.pulseMotion != PulseMotionId::Static ? 0.24f : 0.08f));
    sp(kParamLFO2Rate, usage.role == PatchRole::Lead ? (perfGuitar ? 0.10f : 0.22f) : 0.10f);
    sp(kParamLFO2Depth, usage.role == PatchRole::Lead ? (perfGuitar ? 0.0f : 0.10f) : 0.0f);
    sp(kParamOutputLimiter, 1.0f);
    sp(kParamLimiterThreshold, 0.94f);
    sp(kParamLimiterAttack, 0.08f);
    sp(kParamLimiterRelease, usage.role == PatchRole::Drum ? 0.18f : 0.35f);
    sp(kParamReverbMix, usage.role == PatchRole::PadIllusion ? 0.10f : (usage.role == PatchRole::Bell ? 0.06f : 0.0f));

    applyFactoryPitchMotionDefaults(def, params);

    applyFactoryArpSeqDefaults(def, params);
    applyFactoryAuthenticC64BassDefaults(def, params);
    applyFactoryDrSidDefaults(def, slot, params);
    applyFactorySid808MissingLogicDefaults(slot, params);
    applyFactoryDigiDefaults(slot, params);
    applyFactoryForensicDefaults(def, params);
    applyFactorySidRegisterMirrors(def, params);
}


inline bool loadFactoryPatchNormalizedParamsForSlot(int slot,
                                                    std::array<float, static_cast<size_t>(kNumParams)>& paramsOut) noexcept;
inline bool loadFactoryPatchNormalizedParamsForSlotForSchema(int slot,
                                                            FactorySlotSchema schema,
                                                            std::array<float, static_cast<size_t>(kNumParams)>& paramsOut) noexcept;
inline SidStateRootV1 makeFactoryPatchStateRootForSlotForSchema(int slot,
                                                                FactorySlotSchema schema) noexcept;
inline bool isFactoryStateRootIdentity(const SidStateRootV1& root) noexcept {
    const std::string& ref = root.document.current_program_ref;
    if (ref.rfind("GM_", 0) == 0) return true;
    for (int i = 0; i < kFactoryPatchSlotCount; ++i) {
        const PatchDefinition* pd = getFactoryPatchDefinition(i);
        if (!pd) continue;
        if (!pd->id.empty() && ref == pd->id) return true;
        if (!pd->displayName.empty() && root.document.program_name == pd->displayName) return true;
    }
    return false;
}

inline bool isFactoryPatchAudioAuthorityParamId(int id) noexcept {
    return isFactoryPatchAudioAuthorityParam(id);
}



inline SidVariantProfile makeFactoryVariantProfileForDefinition(const PatchDefinition& def) noexcept {
    SidVariantProfile profile = sidDefaultVariantProfile();
    switch (def.staticState.chip) {
        case SidChipTarget::MOS8580: profile.family = SidFamily::MOS8580; break;
        case SidChipTarget::MOS6581: profile.family = SidFamily::MOS6581; break;
        case SidChipTarget::AnyPortable: default: profile.family = SidFamily::MOS8580; break;
    }
    profile.video_standard = (def.staticState.clock == ClockTarget::NTSC_First)
        ? SidVideoStandard::NTSC : SidVideoStandard::PAL;
    profile.sanitize();
    return profile;
}


inline bool validateFactoryPatchStateRoot(SidStateRootV1& root, int normalizedSlot) noexcept {
    if (!root.valid()) return false;
    if (root.patch.parameters.semantic_entries.empty()) return false;
    if (root.patch.parameters.semantic_entries.size() < static_cast<size_t>(kNumParams)) return false;
    sidEnsureSemanticParameterEntries(root);
    sidHydrateParameterValuesFromSemanticEntries(root);
    bool sawProgram = false;
    bool sawBank = false;
    for (auto& e : root.patch.parameters.semantic_entries) {
        if (e.param_id == static_cast<uint32_t>(kParamProgram)) {
            e.value = canonicalNormalizedFactoryProgramValue(normalizedSlot);
            sawProgram = true;
        } else if (e.param_id == static_cast<uint32_t>(kParamBankSlot)) {
            e.value = canonicalNormalizedBankSlotValue(normalizedSlot);
            sawBank = true;
        } else if (!std::isfinite(e.value)) {
            e.value = kParamInfos[static_cast<size_t>(e.param_id)].defaultNorm;
        }
    }
    if (!sawProgram || !sawBank) return false;
    sidHydrateParameterValuesFromSemanticEntries(root);
    root.document.editor_metadata_present = false;
    return root.valid();
}


inline void applyFactorySlotSchemaContextToParams(int slot,
                                                  FactorySlotSchema schema,
                                                  std::array<float, static_cast<size_t>(kNumParams)>& params) noexcept {
    const DrumContext schemaCtx = factorySlotContextForSchema(slot, schema);
    auto sp = [&](ParamID pid, float v) noexcept {
        params[static_cast<size_t>(pid)] = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    };
    if (schemaCtx == DrumContext::DrSID_C64Wavetable) {
        sp(kParamSynthModeEnable, 0.0f);
        sp(kParamDrSidEnable, 1.0f);
        sp(kParamArpEnable, 0.0f);
        sp(kParamSeqEnable, 0.0f);
        // Legacy saved banks that used slots 120..124 or 127 must restore as
        // DrSID, not as the canonical SID-808 page that now owns those slots.
        sp(kParamDrSidMachineModel, 0.0f);
    } else if (schemaCtx == DrumContext::SID808_AnalogProjection) {
        sp(kParamSynthModeEnable, 0.0f);
        sp(kParamDrSidEnable, 1.0f);
        sp(kParamArpEnable, 0.0f);
        sp(kParamSeqEnable, 0.0f);
        sp(kParamDrSidMachineModel, 1.0f);
    } else if (schemaCtx == DrumContext::Digi4Bit) {
        sp(kParamSynthModeEnable, 0.0f);
        sp(kParamDrSidEnable, 0.0f);
        sp(kParamArpEnable, 0.0f);
        sp(kParamSeqEnable, 0.0f);
    }
}

inline SidStateRootV1 makeFactoryPatchStateRootForSlot(int slot) noexcept {
    SidStateRootV1 root{};
    const int requestedSlot = slot;
    const int normalizedSlot = canonicalFactorySlotForRoot(requestedSlot);
    const PatchDefinition* pd = getFactoryPatchDefinition(normalizedSlot);
    if (!pd) return root;
    if (pd->displayName.empty() || pd->id.empty()) return root;

    std::array<float, static_cast<size_t>(kNumParams)> params{};
    if (!loadFactoryPatchNormalizedParamsForSlot(normalizedSlot, params)) return root;

    root.patch.parameters.values.clear();
    root.patch.parameters.semantic_entries.clear();
    root.patch.parameters.semantic_entries.reserve(static_cast<size_t>(kNumParams));
    for (int i = 0; i < kNumParams; ++i) {
        float v = params[static_cast<size_t>(i)];
        if (!std::isfinite(v)) v = kParamInfos[static_cast<size_t>(i)].defaultNorm;
        if (isRuntimeOnlyOrTransientParam(i)) v = kParamInfos[static_cast<size_t>(i)].defaultNorm;
        root.patch.parameters.semantic_entries.push_back(SidSemanticParamEntry{static_cast<uint32_t>(i), std::clamp(v, 0.0f, 1.0f)});
    }
    for (auto& e : root.patch.parameters.semantic_entries) {
        if (e.param_id == static_cast<uint32_t>(kParamProgram))
            e.value = canonicalNormalizedFactoryProgramValue(normalizedSlot);
        else if (e.param_id == static_cast<uint32_t>(kParamBankSlot))
            e.value = canonicalNormalizedBankSlotValue(normalizedSlot);
        else if (e.param_id == static_cast<uint32_t>(kParamBankCommand))
            e.value = kParamInfos[static_cast<size_t>(kParamBankCommand)].defaultNorm;
    }
    ArpSID::sidHydrateParameterValuesFromSemanticEntries(root);
    ArpSID::sidEnsureSemanticParameterEntries(root);

    root.patch.variant_profile = makeFactoryVariantProfileForDefinition(*pd);
    root.patch.start_policy = pd->start;
    root.document.program_name = pd->displayName.empty() ? std::string("Factory Patch") : pd->displayName;
    root.document.current_program_ref = pd->id.empty() ? std::string("FactoryPatch") : pd->id;
    root.document.editor_metadata_present = false;
    root.document.editor_layout_blob.clear();
    if (!validateFactoryPatchStateRoot(root, normalizedSlot) && normalizedSlot != 0) {
        root = makeFactoryPatchStateRootForSlot(0);
    }
    return root;
}

inline bool loadFactoryPatchNormalizedParamsForSlot(int slot,
                                                    std::array<float, static_cast<size_t>(kNumParams)>& paramsOut) noexcept {
    const int normalizedSlot = normalizeFactoryPatchSlot(slot);
    const PatchDefinition* pd = getFactoryPatchDefinition(normalizedSlot);
    if (!pd) return false;
    for (size_t i = 0; i < paramsOut.size(); ++i) paramsOut[i] = kParamInfos[i].defaultNorm;
    applyFactoryPatchDefinitionToNormalizedParams(*pd, normalizedSlot, paramsOut);
    if (slot != normalizedSlot) {
        applyFactorySid808MissingLogicDefaults(slot, paramsOut);
        applyFactoryDigiDefaults(slot, paramsOut);
    }
    return true;
}

inline bool loadFactoryPatchNormalizedParamsForSlotForSchema(int slot,
                                                            FactorySlotSchema schema,
                                                            std::array<float, static_cast<size_t>(kNumParams)>& paramsOut) noexcept {
    if (!loadFactoryPatchNormalizedParamsForSlot(slot, paramsOut)) return false;
    applyFactorySlotSchemaContextToParams(slot, schema, paramsOut);
    return true;
}

inline SidStateRootV1 makeFactoryPatchStateRootForSlotForSchema(int slot,
                                                                FactorySlotSchema schema) noexcept {
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
    if (!root.valid()) return root;
    sidEnsureSemanticParameterEntries(root);
    sidHydrateParameterValuesFromSemanticEntries(root);
    const DrumContext ctx = factorySlotContextForSchema(slot, schema);
    for (auto& e : root.patch.parameters.semantic_entries) {
        if (e.param_id == static_cast<uint32_t>(kParamSynthModeEnable)) {
            e.value = 0.0f;
        } else if (e.param_id == static_cast<uint32_t>(kParamDrSidEnable)) {
            e.value = (ctx == DrumContext::DrSID_C64Wavetable || ctx == DrumContext::SID808_AnalogProjection) ? 1.0f : 0.0f;
        } else if (e.param_id == static_cast<uint32_t>(kParamArpEnable)) {
            e.value = 0.0f;
        } else if (e.param_id == static_cast<uint32_t>(kParamSeqEnable)) {
            e.value = 0.0f;
        } else if (e.param_id == static_cast<uint32_t>(kParamDrSidMachineModel)) {
            e.value = (ctx == DrumContext::SID808_AnalogProjection) ? 1.0f : 0.0f;
        }
    }
    sidHydrateParameterValuesFromSemanticEntries(root);
    root.document.current_program_ref += (schema == FactorySlotSchema::Legacy128) ? ":legacy128" : ":canonical";
    return root;
}

} // namespace ArpSID
