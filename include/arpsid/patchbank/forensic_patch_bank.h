// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ArpSID {

enum class SidChipTarget : uint8_t { AnyPortable, MOS6581, MOS8580 };
enum class ClockTarget : uint8_t { PAL_First, NTSC_First, Dual_Safe };
enum class PatchRole : uint8_t { Init, Bass, Lead, Arp, Chord, PadIllusion, Bell, Metallic, Drum, FX, Utility };
enum class TrackerLiveBias : uint8_t { Tracker, Live, Balanced };
enum class AuthenticityGrade : uint8_t { Utility, Portable, Strong, Forensic, Experimental };
enum class StartPolicyId : uint8_t { Default, HardRestart, StrictHardRestart, SoftLegato, DrumGate };
enum class PitchMotionId : uint8_t { Static, Vibrato, Glide, SteppedArp, TrackerSlide };
enum class PulseMotionId : uint8_t { Static, SlowPWM, AudioPWM, TrackerPWM };
enum class FilterMotionId : uint8_t { Static, EnvPunch, SlowSweep, Acid, TrackerGate };
enum class HistoricalFamilyId : uint8_t {
    Init, CoreBass, CoreLead, CorePad, CoreArp, TrackerSeq, DrumKit, FxBed, Portable, Experimental
};

// ─── Direct SID register snapshot ────────────────────────────────────────────
// All 30 SID registers $D400..$D41D encoded as bytes (0–255).
// Index n = register at address $D400+n.
// Indices 25–28 ($D419–$D41C) are read-only on real hardware; stored here as
// reference/telemetry values. Index 29 ($D41D) is the ArpSID system pseudo-register.
//
// Control byte bits (regs $D404, $D40B, $D412):
// bit7=NOISE bit6=PULSE bit5=SAW bit4=TRI
// bit3=TEST bit2=RING bit1=SYNC bit0=GATE
// AD byte: (Attack[3:0] << 4) | Decay[3:0]
// SR byte: (Sustain[3:0] << 4) | Release[3:0]
// $D415: FC bits[2:0]
// $D416: FC bits[10:3]
// $D417: (Resonance[3:0] << 4) | FILT[3:0] (FILT: bit2=V3 bit1=V2 bit0=V1)
// $D418: (V3OFF<<7)|(HP<<6)|(BP<<5)|(LP<<4)|VOL[3:0]
// $D41D: bit0=NTSC, bit1/model mask 0x02: 0=6581 1=8580, bit2=ADSR-bug, bit7=forensic
struct SidRegisterSnapshot {
    std::array<uint8_t, 30> r{};  // $D400..$D41D
    bool valid = false;
};

struct PatchStaticState {
    bool synthMode = true;
    bool drSidMode = false;
    SidChipTarget chip = SidChipTarget::AnyPortable;
    ClockTarget clock = ClockTarget::PAL_First;
    double masterVolumeNorm = 0.80;
    double vco1WaveNorm = 0.0;
    double vco2WaveNorm = 0.0;
    double vco3WaveNorm = 0.0;

    // Canonical authored mixer levels. Waveform != audible output: secondary
    // oscillators may exist only as ring/sync sources and must not leak into
    // the mixer unless these fields explicitly say so.
    double vco1LevelNorm = 0.80;
    double vco2LevelNorm = 0.0;
    double vco3LevelNorm = 0.0;

    double vco1PulseWidthNorm = 0.50;
    double vco2PulseWidthNorm = 0.50;
    double vco3PulseWidthNorm = 0.50;
    // Real SID control bytes allow hard-sync on all three voices:
    //   $D404 V1 sync (source V3), $D40B V2 sync (source V1), $D412 V3 sync (source V2).
    bool vco1Sync = false;
    bool vco2Sync = false;
    bool vco3Sync = false;
    // Ring-mod cyclic source topology mirrors sync: V1<-V3, V2<-V1, V3<-V2.
    bool vco1Ring = false;
    bool vco2Ring = false;
    bool vco3Ring = false;
    double attackNorm = 0.0;
    double decayNorm = 0.2;
    double sustainNorm = 0.7;
    double releaseNorm = 0.2;
    double filterCutoffNorm = 0.7;
    double filterResNorm = 0.1;
    double filterModeNorm = 0.0;
    double filterEnvAmountNorm = 0.0;
    double filterDriveNorm = 0.0;
    // Explicit $D417 filter route (one of the most important SID registers).
    // bit0=V1, bit1=V2, bit2=V3, bit3=EXT. Authored per patch instead of being
    // inferred from mixer levels. Default routes V1 through the filter.
    uint8_t filterRouteMask = 0x01;

    // ── Direct SID register snapshot (all 30 registers) ──────────────────
    // When regSnap.valid=true, applyFactorySidRegisterMirrors uses these bytes
    // verbatim instead of deriving them from the high-level params above.
    // This enables 100% hardware-accurate register encoding for authentic patches.
    SidRegisterSnapshot regSnap{};

    // ── Per-patch forensic defaults ───────────────────────────────────────
    // When forensicOverride=true, these values are written directly.
    // When false, applyFactoryForensicDefaults() uses authenticity-grade logic.
    bool forensicOverride = false;
    float forensicEnable = 0.0f;
    float forensicIntensity = 0.0f;
    float clockJitter = 0.0f;      bool clockJitterEnable = false;
    float supplyRipple = 0.0f;     bool supplyRippleEnable = false;
    float thermalDrift = 0.0f;     bool thermalDriftEnable = false;
    float voiceCrosstalk = 0.0f;   bool voiceCrosstalkEnable = false;
    float externalBleed = 0.0f;    bool externalBleedEnable = false;
    float envelopeTDM = 0.0f;
    float d418Asymmetry = 0.0f;
    float filterOhmic = 0.0f;
    float systemNoise = 0.0f;
    float motherboard = 0.0f;
    float adcBleed = 0.0f;
    float busCollision = 0.0f;
    float potInput = 0.0f;
    bool digifix8580 = false;
    bool adsrBug6581 = false;
};

struct PatchStartPolicy {
    StartPolicyId id = StartPolicyId::Default;
    bool gateOffBeforeStart = true;
    bool useHardRestart = true;
    bool strictHardRestart = false;
    bool preloadWaveform = true;
    bool preloadPulseWidth = true;
    bool preloadFilterRoute = true;
    bool preloadFrequency = true;
    bool useTestBitPrecharge = false;
    uint32_t postStartDelaySamples = 0;
};

struct PatchMotionPolicy {
    PitchMotionId pitchMotion = PitchMotionId::Static;
    PulseMotionId pulseMotion = PulseMotionId::Static;
    FilterMotionId filterMotion = FilterMotionId::Static;
    bool trackerStepped = false;
    bool legatoSafe = false;
    bool repeatedNoteOptimized = false;
};

struct PatchUsageMetadata {
    PatchRole role = PatchRole::Lead;
    TrackerLiveBias bias = TrackerLiveBias::Balanced;
    AuthenticityGrade authenticity = AuthenticityGrade::Strong;
    HistoricalFamilyId family = HistoricalFamilyId::Experimental;
    int preferredMidiMin = 48;
    int preferredMidiMax = 84;
    int validMidiMin = 36;
    int validMidiMax = 96;
    bool monoPreferred = true;
    bool polyAllowed = false;
    uint8_t intendedDensity = 1;
};

struct PatchDefinition {
    std::string id;
    std::string displayName;
    std::string description;
    PatchStaticState staticState;
    PatchStartPolicy start;
    PatchMotionPolicy motion;
    PatchUsageMetadata usage;
};

const std::vector<PatchDefinition>& getFactoryPatchDefinitions();
const PatchDefinition* getFactoryPatchDefinition(int slot) noexcept;
std::string factoryPatchNameForSlot(int slot);
std::string factoryPatchIdForSlot(int slot);
std::string factoryPatchDescriptionForSlot(int slot);
const char* toString(SidChipTarget value) noexcept;
const char* toString(ClockTarget value) noexcept;
const char* toString(PatchRole value) noexcept;
const char* toString(TrackerLiveBias value) noexcept;
const char* toString(AuthenticityGrade value) noexcept;
const char* toString(StartPolicyId value) noexcept;
const char* toString(PitchMotionId value) noexcept;
const char* toString(PulseMotionId value) noexcept;
const char* toString(FilterMotionId value) noexcept;
const char* toString(HistoricalFamilyId value) noexcept;
std::string factoryPatchManifestCsv();

} // namespace ArpSID
