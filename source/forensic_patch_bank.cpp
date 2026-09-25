// forensic_patch_bank.cpp — 30 iconic C64 patches using all 30 SID registers
// Every patch sets regSnap.valid=true with a complete SID register image
// ($D400–$D41D, indices 0–29). Forensic overrides reproduce chip-accurate
// behaviour for 6581/8580 on PAL/NTSC including ADSR bugs and hardware quirks.
//
// SID register map:
// 0-6: Voice 1 (FLo,FHi,PWLo,PWHi,Ctrl,AD,SR)
// 7-13: Voice 2 (same layout)
// 14-20:Voice 3 (same layout)
// 21: $D415 Filter Cutoff Lo (bits 2:0)
// 22: $D416 Filter Cutoff Hi (bits 10:3)
// 23: $D417 (Res[7:4]|FILT[3:0]) FILT: bit2=V3 bit1=V2 bit0=V1
// 24: $D418 (V3OFF|HP|BP|LP|VOL[3:0])
// 25-28: Read-only ($D419-$D41C)
// 29: $D41D pseudo-reg (bit0=NTSC, bit1/model mask 0x02: 0=6581/1=8580, bit2=ADSR bug)
//
// Control register (regs 4, 11, 18):
// bit7=NOISE bit6=PULSE bit5=SAW bit4=TRI bit3=TEST bit2=RING bit1=SYNC bit0=GATE
//
// PAL reference frequencies (clock=985248 Hz):
// A2=1870(0x74E) A3=3741(0xE9D) A4=7482(0x1D3A) A5=14965(0x3A75)
// C4=4450(0x1162) G3=3335(0xD07) D4=4994(0x1382) E3=2803(0xAF3)
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid/core/drum_context.h"
#include "arpsid/patchbank/factory_drsid_kits.h"
#include "parameter_ids.h"
#include "factory_patch_params.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace ArpSID {
namespace {

constexpr int kFactorySlots = 180;

// ─── Register snapshot builder macro ──────────────────────────────────────────
// R(v1fl,v1fh,v1pwl,v1pwh,v1ctrl,v1ad,v1sr,
// v2fl,v2fh,v2pwl,v2pwh,v2ctrl,v2ad,v2sr,
// v3fl,v3fh,v3pwl,v3pwh,v3ctrl,v3ad,v3sr,
// fclo,fchi,resfilt,volmode,sys)
#define RSNAP(v1fl,v1fh,v1pwl,v1pwh,v1ctrl,v1ad,v1sr,   \
              v2fl,v2fh,v2pwl,v2pwh,v2ctrl,v2ad,v2sr,   \
              v3fl,v3fh,v3pwl,v3pwh,v3ctrl,v3ad,v3sr,   \
              fclo,fchi,resfilt,volmode,sys)              \
    SidRegisterSnapshot{                                  \
        {{ v1fl,v1fh,v1pwl,v1pwh,v1ctrl,v1ad,v1sr,       \
           v2fl,v2fh,v2pwl,v2pwh,v2ctrl,v2ad,v2sr,       \
           v3fl,v3fh,v3pwl,v3pwh,v3ctrl,v3ad,v3sr,       \
           fclo,fchi,resfilt,volmode,                     \
           0x00,0x00,0x00,0x00,sys }},                    \
        true }

// ─── Waveform control byte presets (with GATE bit set) ───────────────────────
// Tri=0x11 Saw=0x21 Pul=0x41 Noi=0x81 TriSaw=0x31 TriPul=0x51 SawPul=0x61
// TriSawPul=0x71 PulSync=0x43 SawSync=0x23 TriRing=0x15 SawRing=0x25
// (without gate: subtract 1)

// ─── AD/SR byte presets ──────────────────────────────────────────────────────
// AD: (Attack[3:0]<<4)|Decay[3:0]
// Attack nib: 0=2ms 1=8ms 2=16ms 3=24ms 4=38ms 5=56ms 6=68ms 7=80ms
// 8=100ms 9=250ms A=500ms B=800ms C=1s D=3s E=5s F=8s
// Decay/Rel nib: 0=6ms 1=24ms 2=48ms 3=72ms 4=114ms 5=168ms 6=204ms 7=240ms
// 8=300ms 9=750ms A=1.5s B=2.4s C=3s D=9s E=15s F=24s

// ─── $D41D system pseudo-register ────────────────────────────────────────────
// 0x02 = 8580/PAL/no ADSR bug (default)
// 0x00 = 6581/PAL
// 0x04 = 6581/PAL/ADSR bug ON
// 0x03 = 8580/NTSC
// 0x01 = 6581/NTSC (rare)

// Dead pre-GM iconic patch table and register-to-high-level backfill helper removed.
// The shipping factory bank is now authored solely by the General MIDI SID mapper below.

// ─── Build PatchDefinition from iconic slot ───────────────────────────────────
// ─── General MIDI mapped C64-auth factory bank ───────────────────────────────
// The legacy GM-compatible subset is 0..127; the canonical factory bank is 180 slots,
// but every slot is still authored as a SID-native patch. We deliberately do
// not pretend to reproduce sampled ROM instruments; instead each GM program is
// revoiced into a plausible 6581/8580-era C64 interpretation using authentic
// waveform, envelope, filter, and start-policy constraints.

static const char* kGeneralMidiProgramNames[kFactorySlots] = {
    "Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano",
    "Electric Piano 1","Electric Piano 2","Harpsichord","Clavinet",
    "Celesta","Glockenspiel","Music Box","Vibraphone","Marimba","Xylophone","Tubular Bells","Dulcimer",
    "Drawbar Organ","Percussive Organ","Rock Organ","Church Organ","Reed Organ","Accordion","Harmonica","Tango Accordion",
    "Acoustic Guitar (nylon)","Acoustic Guitar (steel)","Electric Guitar (jazz)","Electric Guitar (clean)",
    "Electric Guitar (muted)","Overdriven Guitar","Distortion Guitar","Guitar Harmonics",
    "Acoustic Bass","Electric Bass (finger)","Electric Bass (pick)","Fretless Bass","Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
    "Violin","Viola","Cello","Contrabass","Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
    "String Ensemble 1","String Ensemble 2","SynthStrings 1","SynthStrings 2","Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
    "Trumpet","Trombone","Tuba","Muted Trumpet","French Horn","Brass Section","SynthBrass 1","SynthBrass 2",
    "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax","Oboe","English Horn","Bassoon","Clarinet",
    "Piccolo","Flute","Recorder","Pan Flute","Blown Bottle","Shakuhachi","Whistle","Ocarina",
    "Lead 1 (square)","Lead 2 (sawtooth)","Lead 3 (calliope)","Lead 4 (chiff)","Lead 5 (charang)","Lead 6 (voice)","Lead 7 (fifths)","Lead 8 (bass + lead)",
    "Pad 1 (new age)","Pad 2 (warm)","Pad 3 (polysynth)","Pad 4 (choir)","Pad 5 (bowed)","Pad 6 (metallic)","Pad 7 (halo)","Pad 8 (sweep)",
    "FX 1 (rain)","FX 2 (soundtrack)","FX 3 (crystal)","FX 4 (atmosphere)","FX 5 (brightness)","FX 6 (goblins)","FX 7 (echoes)","FX 8 (sci-fi)",
    "Sitar","Banjo","Shamisen","Koto","Kalimba","Bag pipe","Fiddle","Shanai",
    "Tinkle Bell","Agogo","Steel Drums","Woodblock","Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
    "Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet","Telephone Ring","Helicopter","Applause","Gunshot"
};


static inline double clampFinite01(double v, double fallback = 0.0) noexcept {
    if (!std::isfinite(v)) return std::clamp(fallback, 0.0, 1.0);
    return std::clamp(v, 0.0, 1.0);
}

static inline bool isOrganicBassVariant(int variant) noexcept {
    return variant >= 0 && variant <= 3;
}

static inline bool isPercussiveBassVariant(int variant) noexcept {
    return variant == 4 || variant == 5;
}

static inline bool isSynthBassVariant(int variant) noexcept {
    return variant >= 6;
}

static void sanitizePatchDefinition(PatchDefinition& d, int slot) noexcept {
    auto& s = d.staticState;
    auto& use = d.usage;
    auto& start = d.start;
    auto& mot = d.motion;

    if (d.displayName.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Patch %03d", std::clamp(slot, 0, kFactorySlots - 1) + 1);
        d.displayName = buf;
    }
    if (d.id.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "GM_%03d", std::clamp(slot, 0, kFactorySlots - 1) + 1);
        d.id = buf;
    }
    if (d.description.empty()) d.description = "SID-native factory patch.";

    s.masterVolumeNorm = clampFinite01(s.masterVolumeNorm, 0.8);
    s.vco1WaveNorm = clampFinite01(s.vco1WaveNorm, 0.10);
    s.vco2WaveNorm = clampFinite01(s.vco2WaveNorm, 0.0);
    s.vco3WaveNorm = clampFinite01(s.vco3WaveNorm, 0.0);
    s.vco1LevelNorm = clampFinite01(s.vco1LevelNorm, 0.80);
    s.vco2LevelNorm = clampFinite01(s.vco2LevelNorm, 0.0);
    s.vco3LevelNorm = clampFinite01(s.vco3LevelNorm, 0.0);
    s.vco1PulseWidthNorm = clampFinite01(s.vco1PulseWidthNorm, 0.5);
    s.vco2PulseWidthNorm = clampFinite01(s.vco2PulseWidthNorm, 0.5);
    s.vco3PulseWidthNorm = clampFinite01(s.vco3PulseWidthNorm, 0.5);
    s.attackNorm = clampFinite01(s.attackNorm, 0.0);
    s.decayNorm = clampFinite01(s.decayNorm, 0.2);
    s.sustainNorm = clampFinite01(s.sustainNorm, 0.7);
    s.releaseNorm = clampFinite01(s.releaseNorm, 0.2);
    s.filterCutoffNorm = clampFinite01(s.filterCutoffNorm, 0.7);
    s.filterResNorm = clampFinite01(s.filterResNorm, 0.1);
    s.filterModeNorm = clampFinite01(s.filterModeNorm, 0.0);
    s.filterEnvAmountNorm = clampFinite01(s.filterEnvAmountNorm, 0.0);
    s.filterDriveNorm = clampFinite01(s.filterDriveNorm, 0.0);
    s.forensicEnable = (float)clampFinite01(s.forensicEnable, 0.0);
    s.forensicIntensity = (float)clampFinite01(s.forensicIntensity, 0.0);
    s.clockJitter = (float)clampFinite01(s.clockJitter, 0.0);
    s.supplyRipple = (float)clampFinite01(s.supplyRipple, 0.0);
    s.thermalDrift = (float)clampFinite01(s.thermalDrift, 0.0);
    s.voiceCrosstalk = (float)clampFinite01(s.voiceCrosstalk, 0.0);
    s.externalBleed = (float)clampFinite01(s.externalBleed, 0.0);
    s.envelopeTDM = (float)clampFinite01(s.envelopeTDM, 0.0);
    s.d418Asymmetry = (float)clampFinite01(s.d418Asymmetry, 0.0);
    s.filterOhmic = (float)clampFinite01(s.filterOhmic, 0.0);
    s.systemNoise = (float)clampFinite01(s.systemNoise, 0.0);
    s.motherboard = (float)clampFinite01(s.motherboard, 0.0);
    s.adcBleed = (float)clampFinite01(s.adcBleed, 0.0);
    s.busCollision = (float)clampFinite01(s.busCollision, 0.0);
    s.potInput = (float)clampFinite01(s.potInput, 0.0);

    if (s.vco1WaveNorm <= 0.0 && s.vco2WaveNorm <= 0.0 && s.vco3WaveNorm <= 0.0) {
        s.vco1WaveNorm = 0.10;
    }
    // SID ring/sync source topology is cyclic: V1 is modulated/synced by V3,
    // V2 by V1, V3 by V2. Keep the actual source oscillator authored — for both
    // ring-mod and hard-sync, since both read the source oscillator.
    if ((s.vco1Ring || s.vco1Sync) && s.vco3WaveNorm <= 0.0) s.vco3WaveNorm = 0.10; // V1 <- V3
    if ((s.vco2Ring || s.vco2Sync) && s.vco1WaveNorm <= 0.0) s.vco1WaveNorm = 0.10; // V2 <- V1
    if ((s.vco3Ring || s.vco3Sync) && s.vco2WaveNorm <= 0.0) s.vco2WaveNorm = 0.10; // V3 <- V2

    use.validMidiMin = std::clamp(use.validMidiMin, 0, 127);
    use.validMidiMax = std::clamp(use.validMidiMax, use.validMidiMin, 127);
    use.preferredMidiMin = std::clamp(use.preferredMidiMin, use.validMidiMin, use.validMidiMax);
    use.preferredMidiMax = std::clamp(use.preferredMidiMax, use.preferredMidiMin, use.validMidiMax);
    if (use.intendedDensity == 0) use.intendedDensity = 1;

    if (use.role == PatchRole::Drum) {
        const DrumContext ctx = factorySlotContext(slot);
        if (ctx == DrumContext::Digi4Bit) {
            s.drSidMode = false;
            s.synthMode = false;
        } else if (ctx == DrumContext::DrSID_C64Wavetable ||
                   ctx == DrumContext::SID808_AnalogProjection ||
                   factorySlotContextLegacy(slot) == DrumContext::DrSID_C64Wavetable) {
            s.drSidMode = true;
            s.synthMode = false;
        }
        use.validMidiMin = 35;
        use.validMidiMax = 81;
        use.preferredMidiMin = 35;
        use.preferredMidiMax = 81;
        use.intendedDensity = std::max<uint8_t>(use.intendedDensity, 4);
    }

    if (use.role == PatchRole::Drum || use.role == PatchRole::FX || start.id == StartPolicyId::DrumGate) {
        use.polyAllowed = false;
        use.monoPreferred = true;
        mot.legatoSafe = false;
        start.id = StartPolicyId::DrumGate;
        start.strictHardRestart = false;
    }

    if (use.role == PatchRole::Bass) {
        // Bass must be authoritative mono low-end, not accidental poly chord bass.
        // Strict/test-bit restarts make low notes click hard and can excite reverb/drive;
        // keep a preloaded HardRestart for punch, and let legato/glide stay clean.
        const bool bassOn8580 = (s.chip == SidChipTarget::MOS8580);
        use.polyAllowed = false;
        use.monoPreferred = true;
        use.validMidiMin = std::min(use.validMidiMin, 21);
        use.validMidiMax = std::max(use.validMidiMax, 72);
        use.preferredMidiMin = std::clamp(use.preferredMidiMin, use.validMidiMin, use.validMidiMax);
        use.preferredMidiMax = std::clamp(use.preferredMidiMax, use.preferredMidiMin, use.validMidiMax);
        use.intendedDensity = 1;
        mot.legatoSafe = true;
        mot.repeatedNoteOptimized = true;
        start.id = StartPolicyId::HardRestart;
        start.strictHardRestart = false;
        start.useTestBitPrecharge = false;
        start.preloadFrequency = true;
        start.preloadWaveform = true;
        start.preloadPulseWidth = true;
        start.preloadFilterRoute = true;
        // A bass hard restart with zero post-start delay can still expose the TEST/gate edge
        // as a low-frequency click. This is not a cap; it is the canonical one-sample
        // preload settle delay for bass programs and bass+lead hybrids promoted to Bass.
        start.postStartDelaySamples = 1u;
        // finalizeFactoryPatch() owns PatchDefinition::staticState as `s` in this scope.
        // Keep the bass closeout here because it is role-derived policy, but never
        // reference the `st` alias that only exists in finalizeFactoryVoiceMix().
        s.masterVolumeNorm = std::min(s.masterVolumeNorm, bassOn8580 ? 0.72 : 0.76);
        s.filterResNorm = std::min(s.filterResNorm, bassOn8580 ? 0.22 : 0.18);
        s.filterEnvAmountNorm = std::min(s.filterEnvAmountNorm, bassOn8580 ? 0.20 : 0.16);
        s.filterDriveNorm = std::min(s.filterDriveNorm, bassOn8580 ? 0.18 : 0.14);
        s.vco3LevelNorm = 0.0;
        s.vco3WaveNorm = 0.0;
    }

    if (use.family == HistoricalFamilyId::CorePad &&
        (use.intendedDensity >= 2 || use.role == PatchRole::Chord || use.role == PatchRole::PadIllusion)) {
        // Dense sustain families (organs, strings, ensembles, choirs) sum several
        // SID oscillators per played note and are often played polyphonically. Keep
        // their factory loudness below lead/bass/drum patches at source instead
        // of relying on the final limiter. This intentionally catches string pads
        // even if an authored case forgot to raise intendedDensity above its default.
        use.intendedDensity = std::max<uint8_t>(use.intendedDensity, 3u);
        s.masterVolumeNorm = std::min(s.masterVolumeNorm, 0.52);
        s.sustainNorm = std::min(s.sustainNorm, 0.54);
        s.filterResNorm = std::min(s.filterResNorm, 0.08);
        s.filterDriveNorm = std::min(s.filterDriveNorm, 0.06);
        s.vco1LevelNorm = std::min(s.vco1LevelNorm, 0.64);
        s.vco2LevelNorm = std::min(s.vco2LevelNorm, 0.38);
        s.vco3LevelNorm = std::min(s.vco3LevelNorm, 0.20);
    }

    start.postStartDelaySamples = std::min<uint32_t>(start.postStartDelaySamples, 4096u);
    if (start.id == StartPolicyId::DrumGate) {
        start.preloadFrequency = true;
        start.preloadWaveform = true;
    }

    if ((use.role == PatchRole::Bell || use.role == PatchRole::Metallic) &&
        s.filterCutoffNorm < 0.15) {
        s.filterCutoffNorm = 0.15;
    }

    // NOTE: V3 ring-mod ($D412 RING, source V2) is a valid SID control-register
    // possibility and is intentionally NOT cleared here. Earlier code force-deleted
    // it, blocking authentic V3 ring patches. The source-aware control-byte path
    // keeps V2 running as the ring source, so V3 ring is safe.
}

static void finalizeFactoryVoiceMix(PatchDefinition& d) noexcept {
    auto& st = d.staticState;
    const auto& use = d.usage;

    const bool v1Wave = st.vco1WaveNorm > 0.0001;
    const bool v2Wave = st.vco2WaveNorm > 0.0001;
    const bool v3Wave = st.vco3WaveNorm > 0.0001;

    // Cyclic SID source topology, for both ring-mod and hard-sync:
    // V1's source is V3, V2's source is V1, V3's source is V2.
    const bool v3IsInternalSource = st.vco1Ring || st.vco1Sync; // V1 reads V3
    const bool v1IsInternalSource = st.vco2Ring || st.vco2Sync; // V2 reads V1
    const bool v2IsInternalSource = st.vco3Ring || st.vco3Sync; // V3 reads V2

    st.vco1LevelNorm = v1Wave ? 0.82 : 0.0;

    switch (use.role) {
        case PatchRole::PadIllusion:
        case PatchRole::Chord:
            st.vco2LevelNorm = v2Wave ? 0.46 : 0.0;
            st.vco3LevelNorm = v3Wave ? 0.34 : 0.0;
            break;
        case PatchRole::Bell:
            st.vco2LevelNorm = (v2Wave && !v1IsInternalSource) ? 0.10 : 0.0;
            st.vco3LevelNorm = (v3Wave && !v3IsInternalSource) ? 0.08 : 0.0;
            break;
        case PatchRole::Metallic:
            st.vco2LevelNorm = (v2Wave && !v2IsInternalSource) ? 0.18 : 0.0;
            st.vco3LevelNorm = (v3Wave && !v3IsInternalSource) ? 0.12 : 0.0;
            break;
        case PatchRole::Bass:
            // Bass gets one dominant oscillator and one restrained body/edge helper.
            // 6581 bass keeps more of the woolly body; 8580 bass keeps the helper lower
            // so slap/synth variants do not bloom into a smeared dual-oscillator sub cloud.
            {
                const bool bassOn8580 = (st.chip == SidChipTarget::MOS8580);
                st.vco1LevelNorm = v1Wave ? (bassOn8580 ? 0.84 : 0.86) : 0.0;
                st.vco2LevelNorm = v2Wave ? (bassOn8580 ? 0.12 : 0.14) : 0.0;
            }
            st.vco3LevelNorm = 0.0;
            break;
        case PatchRole::Lead:
            st.vco2LevelNorm = v2Wave ? 0.20 : 0.0;
            st.vco3LevelNorm = (v3Wave && !v3IsInternalSource) ? 0.10 : 0.0;
            break;
        case PatchRole::FX:
            st.vco2LevelNorm = v2Wave ? 0.16 : 0.0;
            st.vco3LevelNorm = (v3Wave && !v3IsInternalSource) ? 0.08 : 0.0;
            break;
        case PatchRole::Drum:
            st.vco2LevelNorm = v2Wave ? 0.10 : 0.0;
            st.vco3LevelNorm = 0.0;
            break;
        case PatchRole::Arp:
        case PatchRole::Utility:
        case PatchRole::Init:
        default:
            st.vco2LevelNorm = v2Wave ? 0.18 : 0.0;
            st.vco3LevelNorm = (v3Wave && !v3IsInternalSource) ? 0.08 : 0.0;
            break;
    }

    if (v1IsInternalSource) st.vco1LevelNorm = std::max(st.vco1LevelNorm, 0.82);
    if (v2IsInternalSource) st.vco2LevelNorm = 0.0;
    if (v3IsInternalSource) st.vco3LevelNorm = 0.0;

    if (d.displayName == "Music Box") {
        st.vco1WaveNorm = 0.10;
        st.vco2WaveNorm = 0.0;
        st.vco3WaveNorm = 0.0;
        st.vco1Ring = false;
        st.vco2Ring = false;
        st.vco3Ring = false;
        st.vco1LevelNorm = 0.58;
        st.vco2LevelNorm = 0.0;
        st.vco3LevelNorm = 0.0;
        st.filterResNorm = std::min(st.filterResNorm, 0.24);
        st.decayNorm = std::min(std::max(st.decayNorm, 0.16), 0.28);
        st.releaseNorm = std::min(std::max(st.releaseNorm, 0.18), 0.30);
    } else if (d.displayName == "Vibraphone") {
        st.vco1WaveNorm = 0.10;
        st.vco3WaveNorm = 0.10;
        st.vco2WaveNorm = 0.0;
        st.vco1Ring = true;
        st.vco2Ring = false;
        st.vco3Ring = false;
        st.vco1LevelNorm = 0.70;
        st.vco2LevelNorm = 0.0;
        st.vco3LevelNorm = 0.0;
        st.filterResNorm = std::min(st.filterResNorm, 0.28);
        d.motion.pitchMotion = PitchMotionId::Vibrato;
    }

    st.vco1LevelNorm = clampFinite01(st.vco1LevelNorm, 0.80);
    st.vco2LevelNorm = clampFinite01(st.vco2LevelNorm, 0.0);
    st.vco3LevelNorm = clampFinite01(st.vco3LevelNorm, 0.0);
}

// Filter route + mode authorship (audit items 6, 7).
//
// CRITICAL correctness rule discovered after the first v845 pass:
//   * synthMode patches render in SidRuntimeRenderMode::SidRegister, where the
//     $D418 filter-mode bits are LIVE AUDIO AUTHORITY (the engine plays the
//     register image directly). Those patches were tuned around their authored
//     filter mode (LP); blindly reassigning BP/HP/multi here thinned/hollowed
//     the tuned sound and made loaded patches sound wrong ("not applied right").
//     => For synth patches we must NOT rewrite the authored filter mode/motion.
//   * DrSID/SID808 (DrSid mode) and Digi (Digi mode) slots synthesise from their
//     own kParamDrSid*/digi params; their $D418 register is only a telemetry
//     mirror, so varying its mode there is cosmetic and cannot degrade the sound.
//     => Filter-vocabulary variety (BP/HP/multi) is expressed on those families.
//
// The explicit $D417 route is set for every slot but kept equal to the voices
// actually in the mix, reproducing the prior audible-route behaviour exactly so
// it introduces no audible change while still being stored as explicit authorship.
static void finalizeFactoryFilterAuthorship(PatchDefinition& d) noexcept {
    auto& s = d.staticState;
    const auto& use = d.usage;
    const std::string& n = d.displayName;
    auto has = [&](const char* sub) noexcept { return n.find(sub) != std::string::npos; };

    // Explicit $D417 route = V1 plus whatever helper voices are audible.
    uint8_t route = 0x01u;
    if (s.vco2LevelNorm > 0.0001) route |= 0x02u;
    if (s.vco3LevelNorm > 0.0001) route |= 0x04u;
    s.filterRouteMask = static_cast<uint8_t>(route & 0x0Fu);

    // Synth patches: leave authored filter mode + motion untouched (audio authority).
    if (s.synthMode && !s.drSidMode) return;

    // Drum / SID808 / Digi register-mirror filter-mode variety (cosmetic for sound).
    double mode = 0.05; // LP default
    if (use.role == PatchRole::Drum) {
        if (has("Digi"))                                        mode = 0.90; // 4-bit DAC sample kit = multi
        else if (has("Cymbal") || has("Cym") || has("Reverse")) mode = 0.90; // cymbals = multi
        else if (has("Snare") || has("Clap") || has("Rim") ||
                 has("Cowbell") || has("Cow"))                  mode = 0.36; // BP noise/metal band
        else if (has("Hat"))                                    mode = 0.64; // HP bright noise
        else if (has("Kit"))                                    mode = 0.05; // full-kit master = LP
        else                                                    mode = 0.05; // kick/tom/tonal = LP
    } else {
        mode = 0.90; // non-drum digi/FX mirror = multi
    }

    // Software-written filter motion for the drum/digi mirror families only.
    if (d.motion.filterMotion == FilterMotionId::Static) {
        if (has("Hat") || has("Cymbal") || has("Cym")) d.motion.filterMotion = FilterMotionId::EnvPunch;
        else if (has("Digi"))                          d.motion.filterMotion = FilterMotionId::SlowSweep;
    }

    s.filterModeNorm = clampFinite01(mode, 0.05);
}

// Author a real SID register snapshot for synth factory slots (audit items 1,6).
// The snapshot is derived through the canonical param pipeline so it cannot drift
// from the live register encoding; once stored, applyFactorySidRegisterMirrors
// consumes the raw image directly. DrSID/SID808/Digi slots keep their own engine
// payload authority and intentionally leave regSnap invalid.
static void authorSynthRegisterSnapshot(PatchDefinition& d, int slot) noexcept {
    auto& s = d.staticState;
    if (!s.synthMode || s.drSidMode || s.regSnap.valid) return;
    std::array<float, static_cast<size_t>(kNumParams)> params{};
    for (size_t i = 0; i < params.size(); ++i) params[i] = kParamInfos[i].defaultNorm;
    applyFactoryPatchDefinitionToNormalizedParams(d, slot, params);
    SidRegisterSnapshot snap{};
    for (int i = 0; i < 30; ++i) {
        const float v = params[static_cast<size_t>(kParamSidRegD400) + static_cast<size_t>(i)];
        const int b = std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255);
        snap.r[static_cast<size_t>(i)] = static_cast<uint8_t>(b);
    }
    snap.valid = true;
    s.regSnap = snap;
}


static PatchDefinition makeDrSidKitDefinition(int slot) noexcept {
    PatchDefinition d{};
    const int rel = std::clamp(slot - 80, 0, 39);
    static const char* const kFamilyNames[8] = {
        "DrSID Kick Grid", "DrSID Snare Grid", "DrSID Hat Grid", "DrSID Open Hat Grid",
        "DrSID Clap/Rim Grid", "DrSID Tom Grid", "DrSID Cowbell Grid", "DrSID Cymbal Grid"
    };
    static const char* const kFamilyIds[8] = {
        "DRSID_KICK", "DRSID_SNARE", "DRSID_HAT", "DRSID_OHAT",
        "DRSID_CLAP_RIM", "DRSID_TOM", "DRSID_COW", "DRSID_CYM"
    };
    const int family = rel % 8;
    const int variant = rel / 8;
    char nameBuf[96];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s %02d", kFamilyNames[family], variant + 1);
    char idBuf[96];
    std::snprintf(idBuf, sizeof(idBuf), "%s_%03d", kFamilyIds[family], slot);
    d.displayName = nameBuf;
    d.id = idBuf;
    d.description = "Canonical DrSID/C64 wavetable drum kit — authored factory payload for slots 80..119.";

    auto& st = d.staticState;
    auto& use = d.usage;
    auto& mot = d.motion;
    auto& start = d.start;

    st.synthMode = false;
    st.drSidMode = true;
    st.regSnap.valid = false;
    st.chip = (variant & 1) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
    st.clock = ClockTarget::PAL_First;
    st.adsrBug6581 = (st.chip == SidChipTarget::MOS6581);
    st.masterVolumeNorm = 0.78 + 0.03 * (variant % 3);
    st.vco1WaveNorm = 0.85; // noise-heavy drum source
    st.vco1LevelNorm = 0.78;
    st.vco2LevelNorm = 0.0;
    st.vco3LevelNorm = 0.0;
    st.attackNorm = 0.0;
    st.decayNorm = 0.10 + 0.05 * (family % 4);
    st.sustainNorm = 0.0;
    st.releaseNorm = 0.08 + 0.04 * (variant % 4);
    st.filterCutoffNorm = 0.35 + 0.05 * (family % 5);
    st.filterResNorm = 0.10 + 0.03 * (variant % 3);
    st.filterModeNorm = 0.0;

    use.role = PatchRole::Drum;
    use.bias = TrackerLiveBias::Balanced;
    use.authenticity = AuthenticityGrade::Strong;
    use.family = HistoricalFamilyId::DrumKit;
    use.preferredMidiMin = 35;
    use.preferredMidiMax = 81;
    use.validMidiMin = 35;
    use.validMidiMax = 81;
    use.monoPreferred = true;
    use.polyAllowed = false;
    use.intendedDensity = static_cast<uint8_t>(4 + (variant % 3));

    start.id = StartPolicyId::DrumGate;
    start.gateOffBeforeStart = true;
    start.useHardRestart = true;
    start.strictHardRestart = false;
    start.preloadFrequency = true;
    start.preloadWaveform = true;
    start.preloadPulseWidth = true;

    mot.pitchMotion = PitchMotionId::Static;
    mot.pulseMotion = (family == 5) ? PulseMotionId::TrackerPWM : PulseMotionId::Static;
    mot.filterMotion = (family >= 6) ? FilterMotionId::EnvPunch : FilterMotionId::Static;
    mot.trackerStepped = true;
    mot.legatoSafe = false;
    mot.repeatedNoteOptimized = true;

    sanitizePatchDefinition(d, slot);
    finalizeFactoryVoiceMix(d);
    sanitizePatchDefinition(d, slot);
    return d;
}

// SID-808 kit definitions occupy the canonical factory slots 120..149 (30 slots):
// five authored kit families (Classic/Punch/Lo-Fi/Hard/Wide) repeated across the
// range. These slots used to fall through the GM SFX family
// (Breath/Seashore/Bird/Telephone/Helicopter) and were silently relabelled as
// drums by the parameter overlay only. The
// PatchDefinition itself stayed marked as FX, which left role-keyed code paths
// (applyFactoryDrSidDefaults guard, GUI flavor detection, persistence) seeing
// the wrong identity. This function provides authored, complete drum-machine
// PatchDefinitions for those five slots so every consumer agrees on the role.
static PatchDefinition makeSid808KitDefinition(int slot) noexcept {
    PatchDefinition d{};
    const int kitIndex = std::clamp((slot - 120) % 5, 0, 4);
    static const char* const kKitNames[5] = {
        "SID-808 Classic Kit",
        "SID-808 Punch Kit",
        "SID-808 Lo-Fi Kit",
        "SID-808 Hard Kit",
        "SID-808 Wide Kit",
    };
    static const char* const kKitIds[5] = {
        "SID_808_CLASSIC_KIT",
        "SID_808_PUNCH_KIT",
        "SID_808_LOFI_KIT",
        "SID_808_HARD_KIT",
        "SID_808_WIDE_KIT",
    };
    static const char* const kKitDescriptions[5] = {
        "SID-808 Classic — register-driven x0x kick/snare/hat/clap/cow/tom set running through the analog X0X control law on the 8580 SID; balanced punch and decay for general drum-machine patterns.",
        "SID-808 Punch — tighter, louder kick with shorter hats and harder clap snap; tuned for percussive backbeats and four-on-the-floor patterns.",
        "SID-808 Lo-Fi — narrower bandwidth, longer-decayed kick, softer hats; voices the SID-808 grid as a dirtier vintage drum machine for boom-bap and dub patterns.",
        "SID-808 Hard — aggressive kick tune, harder snare snap, brighter hats and cymbals; tuned for techno/industrial drum lines.",
        "SID-808 Wide — wider clap spread, longer cowbell decay, brighter hat metal; more open-sounding kit suitable for breakbeat/electronica.",
    };

    // v910 honest naming: family + variant bank letter (A..F), matching
    // factorySid808KitName() — 5 authored families × 6 deterministic variants,
    // not 30 independently authored kits.
    const int variantIndex = std::clamp((slot - 120) / 5, 0, 5);
    char nameBuf[96];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s %c", kKitNames[kitIndex],
                  static_cast<char>('A' + variantIndex));
    char idBuf[96];
    std::snprintf(idBuf, sizeof(idBuf), "%s_%03d", kKitIds[kitIndex], slot);
    d.displayName = nameBuf;
    d.id = idBuf;
    d.description = kKitDescriptions[kitIndex];

    auto& st = d.staticState;
    auto& use = d.usage;
    auto& mot = d.motion;
    auto& start = d.start;

    // Engine identity: SID-808 runs through DrSID (drSidMode=true) with the
    // Analog X0X machine model selected. The voice is still SID-register
    // driven; the X0X model selects the SID-projected x0x control law.
    st.synthMode = false;
    st.drSidMode = true;
    st.regSnap.valid = false;
    st.chip = SidChipTarget::MOS8580;       // x0x kits live cleanest on the 8580.
    st.clock = ClockTarget::PAL_First;
    st.adsrBug6581 = false;
    st.masterVolumeNorm = 0.84;
    st.filterModeNorm = 0.0;
    st.filterEnvAmountNorm = 0.0;

    // Per-kit static-state tuning (mirrors the param overlay in
    // applyFactorySid808MissingLogicDefaults so the static state and runtime
    // params agree at first load).
    static const double kKickTune[5]  = { 0.40, 0.46, 0.36, 0.50, 0.42 };
    static const double kKickDecay[5] = { 0.32, 0.38, 0.28, 0.44, 0.34 };
    st.attackNorm    = 0.0;
    st.decayNorm     = kKickDecay[kitIndex];
    st.sustainNorm   = 0.0;
    st.releaseNorm   = 0.20;
    st.filterCutoffNorm = 0.42 + 0.04 * kitIndex;
    st.filterResNorm    = 0.20;
    st.vco1WaveNorm     = 0.18;            // pulse/saw blend for analog kick body
    st.vco2WaveNorm     = 0.10;            // triangle for sub
    st.vco3WaveNorm     = 0.10;            // triangle for noise routing helper
    st.vco1PulseWidthNorm = 0.50;
    st.vco2PulseWidthNorm = 0.50;
    st.vco3PulseWidthNorm = 0.50;
    st.vco1Ring = false;
    st.vco2Ring = false;
    st.vco2Sync = false;
    st.forensicOverride = false;
    (void)kKickTune; // applied in the param overlay; kept for documentation.

    use.role = PatchRole::Drum;
    use.bias = TrackerLiveBias::Balanced;
    use.authenticity = AuthenticityGrade::Forensic;
    use.family = HistoricalFamilyId::FxBed;
    use.preferredMidiMin = 35;
    use.preferredMidiMax = 81;
    use.validMidiMin = 35;
    use.validMidiMax = 81;
    use.monoPreferred = true;
    use.polyAllowed = false;
    use.intendedDensity = 4;

    start.id = StartPolicyId::DrumGate;
    start.strictHardRestart = false;

    mot.pitchMotion = PitchMotionId::Static;
    mot.pulseMotion = PulseMotionId::Static;
    mot.filterMotion = FilterMotionId::Static;
    mot.legatoSafe = false;
    mot.trackerStepped = false;
    mot.repeatedNoteOptimized = true;

    sanitizePatchDefinition(d, slot);
    finalizeFactoryVoiceMix(d);
    sanitizePatchDefinition(d, slot);
    return d;
}


static PatchDefinition makeDigiKitDefinition(int slot) noexcept {
    PatchDefinition d{};
    const int rel = std::clamp(slot - 150, 0, 29);
    char nameBuf[96];
    std::snprintf(nameBuf, sizeof(nameBuf), "Digi 4-bit Kit %02d", rel + 1);
    char idBuf[96];
    std::snprintf(idBuf, sizeof(idBuf), "DIGI_4BIT_KIT_%03d", slot);
    d.displayName = nameBuf;
    d.id = idBuf;
    d.description = "Digi 4-bit $D418 DAC kit slot — canonical v500+ sample drum factory identity.";

    auto& st = d.staticState;
    auto& use = d.usage;
    auto& mot = d.motion;
    auto& start = d.start;

    st.synthMode = false;
    st.drSidMode = false;
    st.regSnap.valid = false;
    st.chip = SidChipTarget::MOS8580;
    st.clock = ClockTarget::PAL_First;
    st.adsrBug6581 = false;
    st.masterVolumeNorm = 0.82;

    use.role = PatchRole::Drum;
    use.bias = TrackerLiveBias::Balanced;
    use.authenticity = AuthenticityGrade::Strong;
    use.family = HistoricalFamilyId::FxBed;
    use.preferredMidiMin = 35;
    use.preferredMidiMax = 81;
    use.validMidiMin = 35;
    use.validMidiMax = 81;
    use.monoPreferred = true;
    use.polyAllowed = false;
    use.intendedDensity = 4;

    start.id = StartPolicyId::DrumGate;
    start.strictHardRestart = false;

    mot.pitchMotion = PitchMotionId::Static;
    mot.pulseMotion = PulseMotionId::Static;
    mot.filterMotion = FilterMotionId::Static;
    mot.legatoSafe = false;
    mot.trackerStepped = true;
    mot.repeatedNoteOptimized = true;

    sanitizePatchDefinition(d, slot);
    finalizeFactoryVoiceMix(d);
    sanitizePatchDefinition(d, slot);
    return d;
}

static PatchDefinition makeGeneralMidiDefinition(int slot) noexcept {
    // Canonical v500+ drum factory ranges are authored beyond the legacy 128-slot bank.
    if (slot >= 80 && slot <= 119) {
        return makeDrSidKitDefinition(slot);
    }
    if (slot >= 120 && slot <= 149) {
        return makeSid808KitDefinition(slot);
    }
    if (slot >= 150 && slot <= 179) {
        return makeDigiKitDefinition(slot);
    }

    PatchDefinition d{};
    const int s = std::clamp(slot, 0, kFactorySlots - 1);
    const int family = s / 8;
    const int variant = s % 8;
    const char* gmName = kGeneralMidiProgramNames[std::clamp(s, 0, 127)];

    d.displayName = gmName;
    char idBuf[160];
    std::snprintf(idBuf, sizeof(idBuf), "GM_%03d_%s", s + 1, gmName);
    std::string id = idBuf;
    for (char& c : id) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) continue;
        c = '_';
    }
    d.id = id;

    auto& st = d.staticState;
    auto& use = d.usage;
    auto& mot = d.motion;
    auto& start = d.start;

    st.synthMode = true;
    st.drSidMode = false;
    st.regSnap.valid = false;
    st.masterVolumeNorm = 0.78;
    st.filterModeNorm = 0.0;
    st.filterEnvAmountNorm = 0.0;
    st.vco1WaveNorm = 0.10;
    st.vco2WaveNorm = 0.10;
    st.vco3WaveNorm = 0.10;
    st.vco1PulseWidthNorm = 0.50;
    st.vco2PulseWidthNorm = 0.50;
    st.vco3PulseWidthNorm = 0.50;

    use.bias = TrackerLiveBias::Balanced;
    use.authenticity = AuthenticityGrade::Strong;
    use.family = HistoricalFamilyId::Experimental;
    use.role = PatchRole::Lead;
    use.preferredMidiMin = 48;
    use.preferredMidiMax = 84;
    use.validMidiMin = 21;
    use.validMidiMax = 108;
    use.monoPreferred = true;
    use.polyAllowed = false;
    use.intendedDensity = 2;

    start.id = StartPolicyId::HardRestart;
    start.strictHardRestart = false;

    mot.pitchMotion = PitchMotionId::Static;
    mot.pulseMotion = PulseMotionId::Static;
    mot.filterMotion = FilterMotionId::Static;
    mot.legatoSafe = true;
    mot.trackerStepped = false;
    mot.repeatedNoteOptimized = true;

    switch (family) {
        case 0: // Pianos
            st.chip = (variant < 4) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
            st.clock = ClockTarget::Dual_Safe;
            st.vco1WaveNorm = (variant <= 2) ? 0.22 : 0.28;
            st.vco2WaveNorm = 0.10;
            st.vco3WaveNorm = 0.0;
            st.attackNorm = 0.0; st.decayNorm = 0.18f + 0.02f * variant;
            st.sustainNorm = 0.08f + 0.03f * (variant & 1);
            st.releaseNorm = 0.16f + 0.02f * (variant % 3);
            st.filterCutoffNorm = 0.55f + 0.03f * variant;
            st.filterResNorm = 0.16f;
            st.filterEnvAmountNorm = 0.22f;
            use.role = (variant >= 6) ? PatchRole::Bell : PatchRole::Chord;
            use.family = HistoricalFamilyId::CoreLead;
            use.polyAllowed = true;
            use.monoPreferred = false;
            d.description = "GM piano program revoiced as a SID-native struck key patch with authentic short-decay register behaviour rather than sampled piano playback.";
            break;
        case 1: // Chromatic percussion
            st.chip = (variant < 4) ? SidChipTarget::MOS8580 : SidChipTarget::MOS6581;
            st.clock = ClockTarget::PAL_First;
            st.vco1WaveNorm = (variant < 2) ? 0.10 : ((variant < 6) ? 0.50 : 0.36);
            st.vco2WaveNorm = (variant == 1 || variant == 3 || variant == 6) ? 0.10 : 0.0;
            st.vco3WaveNorm = 0.0;
            st.vco1Ring = (variant == 1 || variant == 3 || variant == 6);
            st.attackNorm = 0.0; st.decayNorm = 0.10f + 0.05f * (variant % 4);
            st.sustainNorm = 0.0f; st.releaseNorm = 0.14f + 0.03f * (variant % 3);
            st.filterCutoffNorm = 0.72f; st.filterResNorm = 0.36f + 0.05f * (variant % 3);
            use.role = (variant < 5) ? PatchRole::Bell : PatchRole::Metallic;
            use.family = HistoricalFamilyId::Experimental;
            d.description = "GM mallet/chime program translated to SID bell and metallic techniques: triangle, ring modulation, pulse tint, and resonant filter emphasis.";
            break;
        case 2: // Organs / free reeds
            st.chip = (variant < 4) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
            st.clock = ClockTarget::Dual_Safe;
            st.vco1WaveNorm = 0.18; st.vco2WaveNorm = 0.08; st.vco3WaveNorm = 0.14;
            st.attackNorm = 0.0; st.decayNorm = 0.06; st.sustainNorm = 0.62; st.releaseNorm = 0.10;
            st.masterVolumeNorm = 0.50f;
            st.filterCutoffNorm = 0.68; st.filterResNorm = 0.04;
            use.role = PatchRole::Chord; use.family = HistoricalFamilyId::CorePad;
            use.polyAllowed = true; use.monoPreferred = false; use.intendedDensity = 3;
            d.description = "GM organ/reed program rendered as a gain-normalized three-oscillator C64 sustain voice: immediate gate, bounded sustain headroom, modest filter coloration, no fake sample drawbars.";
            break;
        case 3: // Guitars
            st.chip = (variant < 5) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
            st.clock = ClockTarget::Dual_Safe;
            st.vco1WaveNorm = (variant < 2) ? 0.10 : 0.22;
            st.vco2WaveNorm = (variant >= 5) ? 0.28 : 0.10;
            st.vco2Sync = (variant >= 5);
            st.attackNorm = 0.0; st.decayNorm = 0.16f + 0.03f * (variant % 4);
            st.sustainNorm = (variant >= 4) ? 0.20f : 0.08f;
            st.releaseNorm = 0.14f + 0.02f * (variant % 3);
            st.filterCutoffNorm = 0.58f; st.filterResNorm = 0.24f;
            st.filterEnvAmountNorm = 0.28f;
            st.vco3WaveNorm = 0.0;
            if (variant == 7) {
                st.vco2Sync = false;
                st.sustainNorm = 0.0f;
                st.releaseNorm = 0.30f;
            }
            if (variant >= 5) {
                st.chip = SidChipTarget::MOS6581;
                st.clock = ClockTarget::PAL_First;
                st.vco1WaveNorm = 0.34f;
                st.vco2WaveNorm = 0.22f;
                // Performance closeout: avoid carrying an inaudible V3 helper through
                // combined-wave/DAC/filter work on dense guitar parts. Guitar Harmonics
                // keeps V3; overdrive/distortion stay two-oscillator.
                st.vco3WaveNorm = (variant == 7) ? 0.34f : 0.0f;
                st.vco1PulseWidthNorm = 0.34f;
                st.vco2PulseWidthNorm = 0.62f;
                st.vco2Sync = (variant == 5); // only overdrive keeps sync bite
                st.attackNorm = 0.0f;
                st.decayNorm = 0.24f;
                st.sustainNorm = (variant == 7) ? 0.0f : 0.26f;
                st.releaseNorm = (variant == 7) ? 0.26f : 0.16f;
                // Controlled SID guitar bite: avoid stacking sync+saw pick transients
                // into the global output-drive path as a second clipping stage.
                st.masterVolumeNorm = (variant >= 6) ? 0.68f : 0.72f;
                st.filterCutoffNorm = (variant >= 6) ? 0.40f : 0.46f;
                st.filterResNorm = (variant >= 6) ? 0.24f : 0.22f;
                st.filterEnvAmountNorm = (variant >= 6) ? 0.22f : 0.20f;
                st.filterDriveNorm = (variant >= 6) ? 0.34f : 0.30f;
                // Lightweight explicit analogue profile. Keep 6581 flavour but do not
                // enable every forensic perturbation path for performance-sensitive guitars.
                st.forensicOverride = true;
                st.forensicEnable = 1.0f;
                st.forensicIntensity = 0.30f;
                st.clockJitterEnable = false; st.clockJitter = 0.0f;
                st.supplyRippleEnable = false; st.supplyRipple = 0.0f;
                st.thermalDriftEnable = false; st.thermalDrift = 0.0f;
                st.voiceCrosstalkEnable = false; st.voiceCrosstalk = 0.0f;
                st.externalBleedEnable = false; st.externalBleed = 0.0f;
                st.envelopeTDM = 0.0f;
                st.d418Asymmetry = 0.06f;
                st.filterOhmic = 0.04f;
                st.systemNoise = 0.02f;
                st.motherboard = 0.02f;
                st.adcBleed = 0.0f;
                st.busCollision = 0.0f;
                st.potInput = 0.0f;
                start.id = StartPolicyId::HardRestart;
                start.strictHardRestart = false;
                start.useTestBitPrecharge = false;
                start.postStartDelaySamples = 1;
                mot.filterMotion = FilterMotionId::Static;
                mot.repeatedNoteOptimized = true;
            }
            // Musical guitar roles (audit item 10): nylon/steel/jazz/clean/muted
            // are played plucks (Lead), harmonics is a Bell, overdrive/distortion
            // are Leads. Never tag musical instruments as Utility.
            use.role = (variant == 7) ? PatchRole::Bell : PatchRole::Lead;
            use.family = HistoricalFamilyId::CoreLead;
            use.authenticity = AuthenticityGrade::Strong;
            if (variant >= 5) {
                use.polyAllowed = false;
                use.monoPreferred = true;
                use.intendedDensity = 1;
            }
            d.description = (variant >= 5)
                ? "GM overdrive/distortion guitar rebuilt as a controlled 6581 SID guitar: clean-ramped hard restart, pulse+saw bite, bounded resonant filter drive, and no sample playback."
                : "GM guitar program recast as authentic SID pluck: pulse/triangle body, sync edge, compact ADSR, and filter-pick transient.";
            break;
        case 4: // Basses
            st.chip = isOrganicBassVariant(variant) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
            st.clock = ClockTarget::PAL_First;
            // Chip split: acoustic/finger/pick/fretless stay on the woollier 6581,
            // while slap and synth bass move to the tighter 8580 for cleaner sub control.
            // V1 is always the body. V2 is a restrained helper only when the GM program
            // actually wants edge/snap; V3 stays off to prevent low-end beating.
            {
                const bool organicBass = isOrganicBassVariant(variant);
                const bool percussiveBass = isPercussiveBassVariant(variant);
                const bool synthBass = isSynthBassVariant(variant);
                st.vco1WaveNorm = (variant == 0 || variant == 3) ? 0.10f : (synthBass ? 0.20f : 0.24f);
                st.vco2WaveNorm = synthBass ? 0.18f : (percussiveBass ? 0.16f : ((variant == 1 || variant == 2) ? 0.08f : 0.0f));
                st.vco1PulseWidthNorm = synthBass ? 0.42f : (percussiveBass ? 0.44f : 0.48f);
                st.vco2PulseWidthNorm = synthBass ? 0.56f : (percussiveBass ? 0.52f : 0.50f);
                st.decayNorm = organicBass ? (0.14f + 0.02f * static_cast<float>(variant % 4))
                                           : (synthBass ? (0.16f + 0.02f * static_cast<float>(variant - 6))
                                                        : (0.12f + 0.02f * static_cast<float>(variant - 4)));
                st.sustainNorm = organicBass ? (0.34f + 0.04f * static_cast<float>(variant))
                                             : (synthBass ? (0.42f + 0.04f * static_cast<float>(variant - 6))
                                                          : (0.28f + 0.04f * static_cast<float>(variant - 4)));
                st.releaseNorm = organicBass ? (0.08f + 0.02f * static_cast<float>(variant % 3))
                                             : (synthBass ? (0.10f + 0.02f * static_cast<float>(variant - 6))
                                                          : (0.08f + 0.02f * static_cast<float>(variant - 4)));
                st.masterVolumeNorm = organicBass ? 0.72f : (synthBass ? 0.68f : 0.70f);
                st.filterCutoffNorm = organicBass ? (0.22f + 0.02f * static_cast<float>(variant % 4))
                                                  : (synthBass ? (0.28f + 0.02f * static_cast<float>(variant - 6))
                                                               : (0.30f + 0.02f * static_cast<float>(variant - 4)));
                st.filterResNorm = synthBass ? 0.20f : (percussiveBass ? 0.18f : 0.16f);
                st.filterEnvAmountNorm = synthBass ? 0.18f : (percussiveBass ? 0.14f : 0.12f);
                st.filterDriveNorm = synthBass ? 0.18f : (percussiveBass ? 0.12f : 0.08f);
                use.authenticity = synthBass ? AuthenticityGrade::Forensic : AuthenticityGrade::Strong;
                mot.pitchMotion = (variant == 3 || synthBass) ? PitchMotionId::Glide : PitchMotionId::Static;
            }
            st.vco3WaveNorm = 0.0;
            st.attackNorm = 0.0f;
            use.role = PatchRole::Bass; use.family = HistoricalFamilyId::CoreBass;
            use.preferredMidiMin = 24; use.preferredMidiMax = 60; use.validMidiMin = 21; use.validMidiMax = 72;
            use.monoPreferred = true; use.polyAllowed = false; use.intendedDensity = 1;
            start.id = StartPolicyId::HardRestart;
            start.strictHardRestart = false;
            start.useTestBitPrecharge = false;
            start.postStartDelaySamples = 1;
            mot.filterMotion = FilterMotionId::Static;
            mot.repeatedNoteOptimized = true;
            mot.legatoSafe = true;
            d.description = "GM bass program translated to chip-authentic SID low-end: 6581 body for organic/electric bass, cleaner 8580 snap for slap/synth variants, mono authoritative fundamental, short C64 register-slide glide, restrained helper oscillator, bounded filter drive, reduced forensic low-end wobble, and clean hard restart without clicky strict TEST precharge.";
            break;
        case 5: // Strings / harp / timpani
            st.chip = (variant < 6) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
            st.clock = ClockTarget::Dual_Safe;
            st.vco1WaveNorm = (variant == 5) ? 0.24 : 0.08;
            st.vco2WaveNorm = (variant == 6 || variant == 7) ? 0.38 : 0.16;
            st.vco3WaveNorm = (variant < 5) ? 0.06 : 0.04;
            st.attackNorm = (variant == 5 || variant == 7) ? 0.0f : 0.18f + 0.03f * (variant % 3);
            st.decayNorm = 0.20f + 0.025f * (variant % 4);
            st.sustainNorm = (variant == 5 || variant == 7) ? 0.0f : 0.54f;
            st.releaseNorm = (variant == 5 || variant == 7) ? 0.18f : 0.24f;
            st.masterVolumeNorm = (variant < 5) ? 0.50f : 0.58f;
            st.filterCutoffNorm = (variant == 7) ? 0.24f : 0.38f;
            st.filterResNorm = (variant == 7) ? 0.24f : 0.08f;
            use.role = (variant == 5) ? PatchRole::Bell : ((variant == 7) ? PatchRole::Drum : PatchRole::PadIllusion);
            use.family = (variant == 5 || variant == 7) ? HistoricalFamilyId::DrumKit : HistoricalFamilyId::CorePad;
            use.polyAllowed = variant < 5; use.monoPreferred = !(variant < 5);
            use.intendedDensity = (variant < 5) ? 3 : 1;
            mot.filterMotion = (variant < 5) ? FilterMotionId::SlowSweep : FilterMotionId::EnvPunch;
            d.description = "GM strings/harp/timpani mapped into C64 sustain pad, pizzicato pluck, or drum-thump territory using SID-native envelope law.";
            break;
        case 6: // Ensemble / choir / orchestra hit
            st.chip = (variant < 4) ? SidChipTarget::MOS8580 : SidChipTarget::MOS6581;
            st.clock = ClockTarget::Dual_Safe;
            st.vco1WaveNorm = 0.08; st.vco2WaveNorm = 0.16; st.vco3WaveNorm = 0.06;
            st.attackNorm = (variant == 7) ? 0.0f : 0.14f + 0.03f * (variant % 4);
            st.decayNorm = (variant == 7) ? 0.18f : 0.18f;
            st.sustainNorm = (variant == 7) ? 0.0f : 0.54f;
            st.releaseNorm = (variant == 7) ? 0.14f : 0.22f;
            st.masterVolumeNorm = (variant == 7) ? 0.64f : 0.50f;
            st.filterCutoffNorm = 0.42f; st.filterResNorm = 0.08f;
            st.filterEnvAmountNorm = (variant == 7) ? 0.38f : st.filterEnvAmountNorm;
            if (variant == 7) {
                start.id = StartPolicyId::StrictHardRestart;
                start.strictHardRestart = true;
            }
            use.role = (variant == 7) ? PatchRole::Lead : PatchRole::PadIllusion;
            use.family = HistoricalFamilyId::CorePad; use.polyAllowed = (variant == 7) ? false : true; use.monoPreferred = (variant == 7) ? true : false; use.intendedDensity = 3;
            mot.filterMotion = (variant >= 2) ? FilterMotionId::SlowSweep : FilterMotionId::Static;
            d.description = "GM ensemble and choir programs expressed as dense SID chorusing and filtered multi-voice illusion patches, ending with a classic orchestra-hit stab.";
            break;
        case 7: // Brass
            st.chip = SidChipTarget::MOS6581;
            st.clock = ClockTarget::PAL_First;
            st.vco1WaveNorm = 0.22; st.vco2WaveNorm = 0.22; st.vco3WaveNorm = (variant >= 4) ? 0.10 : 0.0;
            st.attackNorm = (variant == 1 || variant == 3 || variant == 4) ? 0.05f : 0.0f;
            st.decayNorm = 0.14f + 0.03f * (variant % 4);
            st.sustainNorm = 0.36f + 0.05f * (variant >= 4);
            st.releaseNorm = 0.12f + 0.02f * (variant % 3);
            st.filterCutoffNorm = 0.44f + 0.04f * (variant % 3); st.filterResNorm = 0.22f;
            st.filterEnvAmountNorm = 0.24f;
            use.role = (variant >= 5) ? PatchRole::Chord : PatchRole::Lead; use.family = HistoricalFamilyId::CoreLead;
            use.polyAllowed = (variant >= 5); use.monoPreferred = !(variant >= 5);
            start.id = StartPolicyId::StrictHardRestart; start.strictHardRestart = true;
            mot.filterMotion = FilterMotionId::Static;
            d.description = "GM brass section reimagined as authentic SID saw/triangle brass stabs with hard restart and filter edge.";
            break;
        case 8: // Reeds
            st.chip = (variant < 4) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
            st.clock = ClockTarget::PAL_First;
            st.vco1WaveNorm = 0.22; st.vco2WaveNorm = (variant < 4) ? 0.10 : 0.28;
            st.vco3WaveNorm = 0.0;
            st.attackNorm = 0.02f; st.decayNorm = 0.18f; st.sustainNorm = (variant >= 6) ? 0.44f : 0.58f; st.releaseNorm = 0.12f;
            st.filterCutoffNorm = (variant >= 6) ? 0.50f : 0.62f; st.filterResNorm = 0.18f + 0.04f * (variant % 2);
            use.role = PatchRole::Lead; use.family = HistoricalFamilyId::CoreLead;
            mot.pitchMotion = (variant < 4) ? PitchMotionId::Vibrato : PitchMotionId::Static;
            d.description = "GM reed instruments voiced as SID solo leads with narrow vibrato-safe sustain and reed-like filter emphasis.";
            break;
        case 9: // Pipes
            st.chip = (variant < 5) ? SidChipTarget::MOS8580 : SidChipTarget::MOS6581;
            st.clock = ClockTarget::Dual_Safe;
            st.vco1WaveNorm = 0.10; st.vco2WaveNorm = (variant >= 4) ? 0.10 : 0.0;
            st.vco3WaveNorm = 0.0;
            st.attackNorm = 0.04f; st.decayNorm = 0.12f; st.sustainNorm = 0.74f; st.releaseNorm = 0.18f;
            st.filterCutoffNorm = 0.74f; st.filterResNorm = 0.10f;
            use.role = PatchRole::Lead; use.family = HistoricalFamilyId::CoreLead;
            use.polyAllowed = false;
            mot.pitchMotion = (variant == 3 || variant == 5 || variant == 6) ? PitchMotionId::Vibrato : PitchMotionId::Static;
            d.description = "GM flute and whistle-family programs rendered as bright SID pipes: triangle-based tone, optional airy doubling, and restrained filter sheen.";
            break;
        case 10: // Synth leads
            st.chip = (variant < 4) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
            st.clock = ClockTarget::PAL_First;
            st.vco1WaveNorm = (variant == 0) ? 0.28 : (variant == 1 ? 0.22 : (variant == 3 ? 0.50 : (variant == 7 ? 0.22 : 0.10)));
            st.vco2WaveNorm = (variant >= 4) ? 0.22 : 0.10;
            st.vco3WaveNorm = 0.0;
            st.vco2Sync = (variant == 1 || variant == 4);
            st.vco1Ring = (variant == 4 || variant == 6);
            st.attackNorm = 0.0f; st.decayNorm = 0.12f + 0.02f * (variant % 3);
            st.sustainNorm = 0.48f + 0.05f * (variant >= 4); st.releaseNorm = 0.10f;
            st.filterCutoffNorm = 0.66f; st.filterResNorm = 0.18f + 0.03f * (variant % 3);
            use.role = (variant == 7) ? PatchRole::Bass : PatchRole::Lead; use.family = (variant == 7) ? HistoricalFamilyId::CoreBass : HistoricalFamilyId::CoreLead;
            use.preferredMidiMin = (variant == 7) ? 30 : 48; use.preferredMidiMax = (variant == 7) ? 72 : 96;
            use.authenticity = AuthenticityGrade::Forensic;
            start.id = (variant == 7) ? StartPolicyId::HardRestart : StartPolicyId::StrictHardRestart;
            start.strictHardRestart = (variant != 7);
            start.useTestBitPrecharge = (variant != 7);
            if (variant == 7) {
                st.masterVolumeNorm = 0.74f;
                st.filterResNorm = std::min(st.filterResNorm, 0.28);
                st.filterEnvAmountNorm = std::min(st.filterEnvAmountNorm, 0.26);
                st.filterDriveNorm = 0.24f;
                start.postStartDelaySamples = 1;
            }
            mot.pitchMotion = (variant == 2) ? PitchMotionId::SteppedArp : ((variant == 7) ? PitchMotionId::Glide : PitchMotionId::Static);
            mot.pulseMotion = (variant == 0 || variant == 4) ? PulseMotionId::SlowPWM : PulseMotionId::Static;
            d.description = "GM synth-lead program mapped to pure SID lead law: square, saw, chiff, ring, sync, and bass+lead hybrids using chip-authentic start policy.";
            break;
        case 11: // Synth pads
            st.chip = (variant < 4) ? SidChipTarget::MOS8580 : SidChipTarget::MOS6581;
            st.clock = ClockTarget::Dual_Safe;
            st.vco1WaveNorm = 0.10; st.vco2WaveNorm = 0.22; st.vco3WaveNorm = (variant == 5) ? 0.28 : 0.10;
            st.vco1PulseWidthNorm = 0.42f; st.vco2PulseWidthNorm = 0.58f;
            st.attackNorm = 0.20f + 0.03f * (variant % 4); st.decayNorm = 0.18f;
            st.sustainNorm = 0.86f; st.releaseNorm = 0.34f;
            st.filterCutoffNorm = 0.42f + 0.04f * (variant % 4); st.filterResNorm = 0.12f + 0.05f * (variant == 5);
            use.role = (variant == 5) ? PatchRole::Metallic : PatchRole::PadIllusion;
            use.family = HistoricalFamilyId::CorePad; use.polyAllowed = true; use.monoPreferred = false; use.intendedDensity = 3;
            use.authenticity = (variant == 5 || variant == 7) ? AuthenticityGrade::Forensic : AuthenticityGrade::Strong;
            start.id = StartPolicyId::SoftLegato;
            mot.filterMotion = (variant == 7) ? FilterMotionId::SlowSweep : FilterMotionId::Static;
            mot.pulseMotion = (variant == 2 || variant == 7) ? PulseMotionId::SlowPWM : PulseMotionId::Static;
            d.description = "GM synth-pad program interpreted as an actually-buildable SID pad illusion: chorus by detune, PWM, and filtered sustain rather than impossible sample wash.";
            break;
        case 12: // FX
            st.chip = (variant < 4) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
            st.clock = ClockTarget::PAL_First;
            st.vco1WaveNorm = (variant == 0 || variant == 5) ? 0.50 : ((variant == 2 || variant == 4) ? 0.10 : 0.22);
            st.vco2WaveNorm = (variant >= 4) ? 0.50 : 0.10;
            st.vco3WaveNorm = 0.0;
            st.vco1Ring = (variant == 2 || variant == 5);
            st.attackNorm = (variant == 0) ? 0.44f : 0.0f;
            st.decayNorm = 0.26f;
            st.sustainNorm = (variant == 0) ? 0.44f : 0.18f;
            st.releaseNorm = (variant == 0) ? 0.60f : 0.28f;
            st.filterCutoffNorm = 0.50f + 0.06f * (variant % 4); st.filterResNorm = 0.28f + 0.06f * (variant % 3);
            if (variant == 5) { // FX 6 (goblins)
                // VCO1 ring modulation uses VCO3 as source on the SID. Author
                // VCO3 and tame the resonant/forensic edge so the patch does not
                // collapse into pathological output when selected or triggered.
                st.vco3WaveNorm = 0.10;
                st.filterResNorm = 0.26f;
                st.masterVolumeNorm = 0.70;
                st.externalBleed = 0.04f;
                st.adcBleed = 0.04f;
                mot.filterMotion = FilterMotionId::Static;
            }
            use.role = PatchRole::FX; use.family = HistoricalFamilyId::FxBed; use.monoPreferred = true; use.polyAllowed = false;
            use.authenticity = AuthenticityGrade::Forensic;
            mot.pitchMotion = (variant == 6) ? PitchMotionId::SteppedArp : PitchMotionId::Static;
            mot.filterMotion = (variant == 5) ? FilterMotionId::Static : FilterMotionId::SlowSweep;
            d.description = "GM FX program redone as authentic C64 sound-design: noise, ring-mod, resonant sweeps, and SID control-register theatrics.";
            break;
        case 13: // Ethnic
            st.chip = (variant < 4) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
            st.clock = ClockTarget::Dual_Safe;
            st.vco1WaveNorm = (variant == 4 || variant == 7) ? 0.10 : 0.22;
            st.vco2WaveNorm = (variant == 1 || variant == 3 || variant == 6) ? 0.10 : 0.0;
            st.vco3WaveNorm = 0.0;
            st.vco1Ring = (variant == 0 || variant == 2);
            st.attackNorm = (variant == 5) ? 0.08f : 0.0f; st.decayNorm = 0.20f + 0.03f * (variant % 4);
            st.sustainNorm = (variant == 5) ? 0.88f : (0.24f + 0.08f * (variant >= 4)); st.releaseNorm = 0.18f;
            st.filterCutoffNorm = 0.60f; st.filterResNorm = 0.20f;
            use.role = (variant == 4) ? PatchRole::Bell : PatchRole::Lead; use.family = HistoricalFamilyId::Experimental;
            mot.pitchMotion = (variant == 0 || variant == 5) ? PitchMotionId::Vibrato : PitchMotionId::Static;
            d.description = "GM ethnic/world instrument program translated into SID pluck, drone, and modal lead gestures within genuine C64 synthesis limits.";
            break;
        case 14: // Percussive
            st.chip = SidChipTarget::MOS6581;
            st.clock = ClockTarget::PAL_First;
            st.vco1WaveNorm = (variant < 4) ? 0.50 : 0.10;
            st.vco2WaveNorm = (variant == 2 || variant == 5 || variant == 6) ? 0.50 : 0.0;
            st.vco3WaveNorm = 0.0;
            st.attackNorm = (variant == 7) ? 0.50f : 0.0f;
            st.decayNorm = 0.08f + 0.04f * (variant % 4);
            st.sustainNorm = (variant == 7) ? 0.60f : 0.0f;
            st.releaseNorm = (variant == 7) ? 0.08f : 0.10f;
            st.filterCutoffNorm = 0.48f; st.filterResNorm = 0.26f;
            use.role = PatchRole::Drum;
            use.family = HistoricalFamilyId::DrumKit; use.preferredMidiMin = 36; use.preferredMidiMax = 72; use.intendedDensity = 3;
            use.authenticity = AuthenticityGrade::Forensic;
            start.id = StartPolicyId::DrumGate; start.strictHardRestart = false;
            mot.filterMotion = FilterMotionId::Static;
            d.description = "GM tuned percussion program forced through SID drum/bell reality: noise bursts, short gates, metallic ring, and no impossible multisamples.";
            break;
        case 15: // SFX
        default:
            st.chip = (variant < 4) ? SidChipTarget::MOS6581 : SidChipTarget::MOS8580;
            st.clock = ClockTarget::PAL_First;
            st.vco1WaveNorm = (variant == 0 || variant == 1 || variant == 2 || variant == 5) ? 0.50 : ((variant == 7) ? 0.75 : 0.22);
            st.vco2WaveNorm = (variant == 4 || variant == 5 || variant == 7) ? 0.10 : 0.0;
            st.vco3WaveNorm = 0.0;
            st.vco1Ring = (variant == 6);
            st.attackNorm = ((variant == 2) || (variant == 5)) ? 0.55f : 0.0f;
            st.decayNorm = 0.22f;
            st.sustainNorm = (variant == 2 || variant == 5) ? 0.78f : 0.04f;
            st.releaseNorm = (variant == 2 || variant == 5) ? 0.55f : ((variant == 6) ? 0.42f : 0.18f);
            st.filterCutoffNorm = 0.52f; st.filterResNorm = 0.34f;
            use.role = (variant == 5) ? PatchRole::FX : ((variant == 6 || variant == 7) ? PatchRole::Drum : PatchRole::FX);
            use.family = HistoricalFamilyId::FxBed; use.authenticity = AuthenticityGrade::Forensic;
            start.id = (variant == 1 || variant == 6 || variant == 7) ? StartPolicyId::DrumGate : StartPolicyId::Default;
            if (variant == 6) {
                use.role = PatchRole::FX;
                start.id = StartPolicyId::Default;
                st.releaseNorm = 0.42f;
            }
            mot.filterMotion = FilterMotionId::SlowSweep;
            d.description = "GM sound-effect slot rewritten as period-correct SID effects vocabulary: breath, surf, bird, ring, rotor, applause-noise, and impact bursts.";
            break;
    }

    if (st.chip == SidChipTarget::MOS6581) {
        // Do not clear an explicit per-patch forensicOverride here.
        // Performance-sensitive guitars install a lightweight analogue override
        // (jitter/ripple/drift/crosstalk/bleed disabled) before this common
        // 6581 finalizer. Clearing the flag here silently fell back to the
        // grade-based forensic defaults, re-enabling expensive perturbation
        // paths and causing guitar CPU spikes.
        st.adsrBug6581 = (family == 4 || family == 7 || family == 10);
    } else {
        st.adsrBug6581 = false;
    }

    sanitizePatchDefinition(d, s);
    finalizeFactoryVoiceMix(d);
    sanitizePatchDefinition(d, s);
    return d;
}
} // namespace

// ─── Public API ───────────────────────────────────────────────────────────────
const std::vector<PatchDefinition>& getFactoryPatchDefinitions() {
    static const std::vector<PatchDefinition> defs = [] {
        std::vector<PatchDefinition> v;
        v.reserve(kFactorySlots);
        for (int i = 0; i < kFactorySlots; ++i) {
            PatchDefinition d = makeGeneralMidiDefinition(i);
            // Filter authorship first (sets mode/route/motion), then derive the
            // synth register snapshot from the canonical param pipeline so the
            // stored image matches the live encoding.
            finalizeFactoryFilterAuthorship(d);
            authorSynthRegisterSnapshot(d, i);
            v.push_back(std::move(d));
        }
        return v;
    }();
    return defs;
}

const PatchDefinition* getFactoryPatchDefinition(int slot) noexcept {
    const auto& defs = getFactoryPatchDefinitions();
    if (slot < 0 || static_cast<size_t>(slot) >= defs.size()) return nullptr;
    return &defs[static_cast<size_t>(slot)];
}

std::string factoryPatchNameForSlot(int slot) {
    if (const auto* def = getFactoryPatchDefinition(slot)) return def->displayName;
    return {};
}

std::string factoryPatchIdForSlot(int slot) {
    if (const auto* def = getFactoryPatchDefinition(slot)) return def->id;
    return {};
}

std::string factoryPatchDescriptionForSlot(int slot) {
    if (const auto* def = getFactoryPatchDefinition(slot)) return def->description;
    return {};
}

// ─── toString helpers ────────────────────────────────────────────────────────
const char* toString(SidChipTarget v) noexcept {
    switch(v){case SidChipTarget::MOS6581:return"MOS6581";case SidChipTarget::MOS8580:return"MOS8580";default:return"AnyPortable";}
}
const char* toString(ClockTarget v) noexcept {
    switch(v){case ClockTarget::PAL_First:return"PAL_First";case ClockTarget::NTSC_First:return"NTSC_First";default:return"Dual_Safe";}
}
const char* toString(PatchRole v) noexcept {
    switch(v){case PatchRole::Bass:return"Bass";case PatchRole::Lead:return"Lead";
              case PatchRole::Arp:return"Arp";case PatchRole::Chord:return"Chord";
              case PatchRole::PadIllusion:return"PadIllusion";case PatchRole::Bell:return"Bell";
              case PatchRole::Metallic:return"Metallic";case PatchRole::Drum:return"Drum";
              case PatchRole::FX:return"FX";case PatchRole::Init:return"Init";default:return"Utility";}
}
const char* toString(TrackerLiveBias v) noexcept {
    switch(v){case TrackerLiveBias::Tracker:return"Tracker";case TrackerLiveBias::Live:return"Live";default:return"Balanced";}
}
const char* toString(AuthenticityGrade v) noexcept {
    switch(v){case AuthenticityGrade::Utility:return"Utility";case AuthenticityGrade::Portable:return"Portable";
              case AuthenticityGrade::Strong:return"Strong";case AuthenticityGrade::Forensic:return"Forensic";default:return"Experimental";}
}
const char* toString(StartPolicyId v) noexcept {
    switch(v){case StartPolicyId::HardRestart:return"HardRestart";
              case StartPolicyId::StrictHardRestart:return"StrictHardRestart";
              case StartPolicyId::SoftLegato:return"SoftLegato";
              case StartPolicyId::DrumGate:return"DrumGate";default:return"Default";}
}
const char* toString(PitchMotionId v) noexcept {
    switch(v){case PitchMotionId::Vibrato:return"Vibrato";case PitchMotionId::Glide:return"Glide";
              case PitchMotionId::SteppedArp:return"SteppedArp";case PitchMotionId::TrackerSlide:return"TrackerSlide";default:return"Static";}
}
const char* toString(PulseMotionId v) noexcept {
    switch(v){case PulseMotionId::SlowPWM:return"SlowPWM";case PulseMotionId::AudioPWM:return"AudioPWM";
              case PulseMotionId::TrackerPWM:return"TrackerPWM";default:return"Static";}
}
const char* toString(FilterMotionId v) noexcept {
    switch(v){case FilterMotionId::EnvPunch:return"EnvPunch";case FilterMotionId::SlowSweep:return"SlowSweep";
              case FilterMotionId::Acid:return"Acid";case FilterMotionId::TrackerGate:return"TrackerGate";default:return"Static";}
}
const char* toString(HistoricalFamilyId v) noexcept {
    switch(v){case HistoricalFamilyId::CoreBass:return"CoreBass";case HistoricalFamilyId::CoreLead:return"CoreLead";
              case HistoricalFamilyId::CorePad:return"CorePad";case HistoricalFamilyId::CoreArp:return"CoreArp";
              case HistoricalFamilyId::TrackerSeq:return"TrackerSeq";case HistoricalFamilyId::DrumKit:return"DrumKit";
              case HistoricalFamilyId::FxBed:return"FxBed";case HistoricalFamilyId::Portable:return"Portable";
              case HistoricalFamilyId::Experimental:return"Experimental";default:return"Init";}
}

static std::string csvEscape(const std::string& in){
    const bool q=in.find_first_of(",\"\n")!=std::string::npos;
    if(!q)return in;
    std::string o;o.reserve(in.size()+4);o.push_back('"');
    for(char c:in){if(c=='"')o+="\"\"";else o.push_back(c);}
    o.push_back('"');return o;
}

std::string factoryPatchManifestCsv() {
    const auto& defs=getFactoryPatchDefinitions();
    std::string out="slot,id,name,description,chip,clock,role,bias,authenticity,family,start\n";
    for(size_t i=0;i<defs.size();++i){
        const auto& d=defs[i];
        out+=std::to_string(i)+','+csvEscape(d.id)+','+csvEscape(d.displayName)+','+
             csvEscape(d.description)+','+toString(d.staticState.chip)+','+
             toString(d.staticState.clock)+','+toString(d.usage.role)+','+
             toString(d.usage.bias)+','+toString(d.usage.authenticity)+','+
             toString(d.usage.family)+','+toString(d.start.id)+'\n';
    }
    return out;
}

} // namespace ArpSID
