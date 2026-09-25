#pragma once

// ── VST3 SDK include (conditional) ──────────────────────────────────────────
// When building AUv3 / standalone targets that do NOT have the Steinberg SDK
// on their include path, we provide local type shims so this header compiles
// without modification. The VST3 plugin target DOES have the SDK paths and
// therefore picks up the real header automatically via __has_include.
#ifdef __has_include
#  if __has_include("pluginterfaces/vst/ivstparameterchanges.h")
#    include "pluginterfaces/vst/ivstparameterchanges.h"
#  else
#    include <cstdint>
     // Local Steinberg namespace shims — only what parameter_ids.h uses.
     namespace Steinberg {
         using TBool   = int16_t;
         using int16   = int16_t;
         using int32   = int32_t;
         using uint32  = uint32_t;
         namespace Vst {
             using ParamID    = uint32_t;
             using ParamValue = double;
         }
     }
#    define ARPSID_HAVE_STEINBERG_TBOOL 1
#  endif
#else
// Fallback for compilers without __has_include (rare)
#  include "pluginterfaces/vst/ivstparameterchanges.h"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

using std::size_t;

namespace ArpSID {

// Total parameters for Phase 4 (audio engine + arp + drums + sequencer + macros + modulation matrix)
// NOTE: We expose the modulation routing as true VST3 parameters so it is:
// - fully automatable
// - saved/restored with presets and host state
// - editable from the GUI cable canvas
// Added Synth-Mode SID register file exposure (30 registers + enable)
// + GUI/HUD telemetry + virtual keyboard support
constexpr int kNumParams = 512;

// Parameter IDs (0..kNumParams-1)
enum ParamID : Steinberg::Vst::ParamID {
    // Master
    kParamMasterVolume = 0,
    kParamMasterTune,
    kParamPortamentoTime,
    kParamVoiceMode,
    kParamVoiceSpread,

    // VCO 1
    kParamVCO1Waveform,
    kParamVCO1PulseWidth,
    kParamVCO1Detune,
    kParamVCO1Level,
    kParamVCO1LowFreqMode,
    kParamVCO1PWMDepth,
    kParamVCO1SyncEnable,
    kParamVCO1RingModEnable,

    // VCO 2
    kParamVCO2Waveform,
    kParamVCO2PulseWidth,
    kParamVCO2Detune,
    kParamVCO2Level,
    kParamVCO2LowFreqMode,
    kParamVCO2PWMDepth,
    kParamVCO2SyncEnable,
    kParamVCO2RingModEnable,

    // VCO 3
    kParamVCO3Waveform,
    kParamVCO3PulseWidth,
    kParamVCO3Detune,
    kParamVCO3Level,
    kParamVCO3LowFreqMode,
    kParamVCO3PWMDepth,
    kParamVCO3SyncEnable,
    kParamVCO3RingModEnable,

    // Filter / Amp
    kParamFilterCutoff,
    kParamFilterResonance,
    kParamFilterMode,
    kParamFilterEnvAmount,
    kParamFilterLFOAmount,
    kParamFilterKeyTrack,
    kParamFilterDrive,

    // ADSR
    kParamAttack,
    kParamDecay,
    kParamSustain,
    kParamRelease,

    // LFO 1
    kParamLFORate,
    kParamLFODepth,
    kParamLFOShape,
    kParamLFOSync,

    // LFO 2
    kParamLFO2Rate,
    kParamLFO2Depth,
    kParamLFO2Shape,
    kParamLFO2Sync,

    // LFO 3
    kParamLFO3Rate,
    kParamLFO3Depth,
    kParamLFO3Shape,
    kParamLFO3Sync,

    // LFO 4
    kParamLFO4Rate,
    kParamLFO4Depth,
    kParamLFO4Shape,
    kParamLFO4Sync,

    // Arpeggiator
    kParamArpEnable,
    kParamArpMode,
    kParamArpRate,
    kParamArpOctaves,
    kParamArpSwing,
    kParamArpGate,
    kParamArpHold,
    kParamArpLatch,
    kParamArpTranspose,
    kParamArpRandom,
    kParamArpPatternLength,

    // DrSID drums
    kParamDrSidEnable,
    kParamDrSidKickTune,
    kParamDrSidKickDecay,
    kParamDrSidSnareTone,
    kParamDrSidSnareSnap,
    kParamDrSidHatTune,
    kParamDrSidHatDecay,
    kParamDrSidClapDecay,
    kParamDrSidCowbellTune,
    kParamDrSidVolume,

    // Sequencer (global)
    kParamSeqEnable,
    kParamSeqTempo,
    kParamSeqSwing,
    kParamSeqMode,
    kParamSeqLength,

    // Steps 1..32, each has Note / Velocity / Gate
    kParamSeqStep1Note,
    kParamSeqStep1Velocity,
    kParamSeqStep1Gate,
    kParamSeqStep2Note,
    kParamSeqStep2Velocity,
    kParamSeqStep2Gate,
    kParamSeqStep3Note,
    kParamSeqStep3Velocity,
    kParamSeqStep3Gate,
    kParamSeqStep4Note,
    kParamSeqStep4Velocity,
    kParamSeqStep4Gate,
    kParamSeqStep5Note,
    kParamSeqStep5Velocity,
    kParamSeqStep5Gate,
    kParamSeqStep6Note,
    kParamSeqStep6Velocity,
    kParamSeqStep6Gate,
    kParamSeqStep7Note,
    kParamSeqStep7Velocity,
    kParamSeqStep7Gate,
    kParamSeqStep8Note,
    kParamSeqStep8Velocity,
    kParamSeqStep8Gate,
    kParamSeqStep9Note,
    kParamSeqStep9Velocity,
    kParamSeqStep9Gate,
    kParamSeqStep10Note,
    kParamSeqStep10Velocity,
    kParamSeqStep10Gate,
    kParamSeqStep11Note,
    kParamSeqStep11Velocity,
    kParamSeqStep11Gate,
    kParamSeqStep12Note,
    kParamSeqStep12Velocity,
    kParamSeqStep12Gate,
    kParamSeqStep13Note,
    kParamSeqStep13Velocity,
    kParamSeqStep13Gate,
    kParamSeqStep14Note,
    kParamSeqStep14Velocity,
    kParamSeqStep14Gate,
    kParamSeqStep15Note,
    kParamSeqStep15Velocity,
    kParamSeqStep15Gate,
    kParamSeqStep16Note,
    kParamSeqStep16Velocity,
    kParamSeqStep16Gate,
    kParamSeqStep17Note,
    kParamSeqStep17Velocity,
    kParamSeqStep17Gate,
    kParamSeqStep18Note,
    kParamSeqStep18Velocity,
    kParamSeqStep18Gate,
    kParamSeqStep19Note,
    kParamSeqStep19Velocity,
    kParamSeqStep19Gate,
    kParamSeqStep20Note,
    kParamSeqStep20Velocity,
    kParamSeqStep20Gate,
    kParamSeqStep21Note,
    kParamSeqStep21Velocity,
    kParamSeqStep21Gate,
    kParamSeqStep22Note,
    kParamSeqStep22Velocity,
    kParamSeqStep22Gate,
    kParamSeqStep23Note,
    kParamSeqStep23Velocity,
    kParamSeqStep23Gate,
    kParamSeqStep24Note,
    kParamSeqStep24Velocity,
    kParamSeqStep24Gate,
    kParamSeqStep25Note,
    kParamSeqStep25Velocity,
    kParamSeqStep25Gate,
    kParamSeqStep26Note,
    kParamSeqStep26Velocity,
    kParamSeqStep26Gate,
    kParamSeqStep27Note,
    kParamSeqStep27Velocity,
    kParamSeqStep27Gate,
    kParamSeqStep28Note,
    kParamSeqStep28Velocity,
    kParamSeqStep28Gate,
    kParamSeqStep29Note,
    kParamSeqStep29Velocity,
    kParamSeqStep29Gate,
    kParamSeqStep30Note,
    kParamSeqStep30Velocity,
    kParamSeqStep30Gate,
    kParamSeqStep31Note,
    kParamSeqStep31Velocity,
    kParamSeqStep31Gate,
    kParamSeqStep32Note,
    kParamSeqStep32Velocity,
    kParamSeqStep32Gate,

    // Macros 1..8
    kParamMacro1,
    kParamMacro2,
    kParamMacro3,
    kParamMacro4,
    kParamMacro5,
    kParamMacro6,
    kParamMacro7,
    kParamMacro8,

    // Output / FX
    kParamOutputLimiter,
    kParamLimiterThreshold,
    kParamLimiterAttack,
    kParamLimiterRelease,
    kParamReverbMix,

    // ----------------------------------------------------------------------
    // Modulation Matrix (Phase 4 - TRUE IMPLEMENTATION)
    // One cable per destination.
    // Each destination has:
    // - Source: discrete 0..NUM_SOURCES where 0 = None
    // - Depth: 0..1 unipolar depth
    // ----------------------------------------------------------------------
    kParamModVCFCutoffSource,
    kParamModVCFCutoffDepth,
    kParamModVCFResonanceSource,
    kParamModVCFResonanceDepth,
    kParamModVCO1FreqSource,
    kParamModVCO1FreqDepth,
    kParamModVCO1PWSource,
    kParamModVCO1PWDepth,
    kParamModVCO2FreqSource,
    kParamModVCO2FreqDepth,
    kParamModVCO2PWSource,
    kParamModVCO2PWDepth,
    kParamModVCO3FreqSource,
    kParamModVCO3FreqDepth,
    kParamModVCO3PWSource,
    kParamModVCO3PWDepth,
    kParamModMasterVolumeSource,
    kParamModMasterVolumeDepth,

    // ----------------------------------------------------------------------
    // Synth Mode: register-driven SID interface
    // ----------------------------------------------------------------------
    kParamSynthModeEnable,

    // SID model controls (mirrors bits in $D41D pseudo SYSTEM/MODEL)
    // 0 = MOS6581, 1 = MOS8580
    kParamSidModel,
    // Enable 6581 ADSR bug quirks (when model=6581)
    kParamSidAdsrBug6581,

    // SID Register File (byte 0..255 mapped from normalized 0..1)
    // $D400..$D41C (29) + $D41D pseudo SYSTEM/MODEL (1)
    kParamSidRegD400,
    kParamSidRegD401,
    kParamSidRegD402,
    kParamSidRegD403,
    kParamSidRegD404,
    kParamSidRegD405,
    kParamSidRegD406,
    kParamSidRegD407,
    kParamSidRegD408,
    kParamSidRegD409,
    kParamSidRegD40A,
    kParamSidRegD40B,
    kParamSidRegD40C,
    kParamSidRegD40D,
    kParamSidRegD40E,
    kParamSidRegD40F,
    kParamSidRegD410,
    kParamSidRegD411,
    kParamSidRegD412,
    kParamSidRegD413,
    kParamSidRegD414,
    kParamSidRegD415,
    kParamSidRegD416,
    kParamSidRegD417,
    kParamSidRegD418,
    kParamSidRegD419,
    kParamSidRegD41A,
    kParamSidRegD41B,
    kParamSidRegD41C,
    kParamSidRegD41D,

    // ----------------------------------------------------------------------
    // GUI/HUD telemetry + virtual keyboard
    // ----------------------------------------------------------------------
    // Momentary action (button)
    kParamPanic,
    // Read-only meters (updated by processor)
    kParamMidiActivity,
    kParamLastNote,
    kParamSampleRateRO,
    kParamBufferSizeRO,
    kParamActiveVoicesRO,
    // Virtual keyboard -> processor note injection
    kParamVirtualNote,
    kParamVirtualGate,
    // Program (VST3 Program List / Program Change)
    kParamProgram,
    // Bank slot/command: canonical factory identity is 0..179; .arpbank v1 user-bank files are 0..127.
    kParamBankSlot,
    kParamBankCommand,

    // Helper: SID Clock System (0=PAL, 1=NTSC)
    kParamSidClockSystem,

    // ----------------------------------------------------------------------
    // Forensic / analog-domain options
    // ----------------------------------------------------------------------
    kParamForensicEnable,
    kParamForensicStartupRandom,
    kParamForensicClockJitterEnable,
    kParamForensicClockJitter,
    kParamForensicSupplyRippleEnable,
    kParamForensicSupplyRipple,
    kParamForensicThermalDriftEnable,
    kParamForensicThermalDrift,
    kParamForensicVoiceCrosstalkEnable,
    kParamForensicVoiceCrosstalk,
    kParamForensicExternalBleedEnable,
    kParamForensicExternalBleed,
    kParamForensicDigifix8580,

    // Math-correct forensic analog controls
    kParamForensicIntensity,
    kParamForensicEnvelopeTDM,
    kParamForensicD418Asymmetry,
    kParamForensicFilterOhmic,
    kParamForensicSystemNoise,
    kParamForensicMotherboard,
    kParamForensicADCBleed,
    kParamForensicBusCollision,
    kParamForensicPOTInput,
    kParamForensicTemp,
    kParamForensicSupply,
    kParamForensicRevision,
    kParamForensicChipSeed,

    // ----------------------------------------------------------------------
    // Compatibility-safe DrSID extensions.
    // Appended here so existing persistent parameter IDs remain stable.
    // ----------------------------------------------------------------------
    kParamDrSidCowbellDecay,
    kParamDrSidTomTune,
    kParamDrSidTomDecay,

    // ----------------------------------------------------------------------
    // SID analogue-core controls. Appended before hidden host MIDI bridge
    // parameters so all earlier authored patch IDs remain stable.
    // ----------------------------------------------------------------------
    kParamSidChipRevision,
    kParamSidExternalRcEnable,
    kParamSidOversamplingFactor,

    // ----------------------------------------------------------------------
    // Hidden host-neutral VST3 MIDI controller bridge parameters.
    // These are not part of authored patch state. They exist only so real hosts
    // can deliver per-channel MIDI controller streams via IMidiMapping /
    // inputParameterChanges without relying on the private standalone CC
    // transport path.
    // ----------------------------------------------------------------------
    kParamHostCtrlModWheelBase,
    kParamHostCtrlBreathBase      = kParamHostCtrlModWheelBase + 16,
    kParamHostCtrlExpressionBase  = kParamHostCtrlBreathBase + 16,
    kParamHostCtrlSustainBase     = kParamHostCtrlExpressionBase + 16,
    kParamHostCtrlSostenutoBase   = kParamHostCtrlSustainBase + 16,
    kParamHostCtrlChannelPressureBase = kParamHostCtrlSostenutoBase + 16,
    kParamHostCtrlPitchBendBase   = kParamHostCtrlChannelPressureBase + 16,
    kParamHostCtrlRpnMsbBase      = kParamHostCtrlPitchBendBase + 16,
    kParamHostCtrlRpnLsbBase      = kParamHostCtrlRpnMsbBase + 16,
    kParamHostCtrlNrpnMsbBase     = kParamHostCtrlRpnLsbBase + 16,
    kParamHostCtrlNrpnLsbBase     = kParamHostCtrlNrpnMsbBase + 16,
    kParamHostCtrlDataEntryMsbBase= kParamHostCtrlNrpnLsbBase + 16,
    kParamHostCtrlDataEntryLsbBase= kParamHostCtrlDataEntryMsbBase + 16,
    kParamHostCtrlLast            = kParamHostCtrlDataEntryLsbBase + 15,

    // ----------------------------------------------------------------------
    // Shared drum-machine model controls. Appended after the hidden host-MIDI
    // bridge block so every earlier authored parameter ID remains stable.
    // ----------------------------------------------------------------------
    kParamDrSidMachineModel,
    kParamDrSidAccentAmount,
    kParamDrSidOutputDrive,
    kParamDrSidHatMetal,
    kParamDrSidClapSpread,

    // C64 Classic Glide / arp-slide controls. Appended to preserve every
    // existing persistent parameter ID.
    kParamPortamentoStyle,
    kParamGlideDelta,
    kParamPortamentoArpGlide,

    // ----------------------------------------------------------------------
    // HI-FI / Super-Hires Transcendence mode. Appended for 0.0.1 v393 so
    // every earlier authored parameter ID remains stable.
    // ----------------------------------------------------------------------
    kParamHiFiEnable,
    kParamHiFiQuality,
    kParamHiFiOversampling,
    kParamHiFiMasterWidth,
    kParamHiFiTapeSaturation,
    kParamHiFiAnalogWarmth,
    kParamHiFiPsychoExciter,
    kParamHiFiStereoDepth,
    kParamHiFiVoiceDiffuser,

    // ----------------------------------------------------------------------
    // v909 Classic-mode authority closure. Appended so every earlier authored
    // parameter ID remains stable.
    //
    // Auto GM Drum Promotion: when enabled, a GM drum note (MIDI channel 10,
    // notes 35..81) may auto-promote the runtime into DrSID mode. Default OFF:
    // a user-selected Classic/BitPerfect (or SynthMode) render mode must never
    // be silently hijacked by channel-10 input. Dedicated drum component
    // flavors (DrumMachine / SID-808) bypass this parameter — they are drum
    // machines by construction.
    // ----------------------------------------------------------------------
    kParamAutoGmDrumPromotion,

    kNumParamsEnum = kNumParams};

static_assert((int)kParamAutoGmDrumPromotion + 1 == kNumParams, "kNumParams must match last ParamID + 1");

// ─── Audit #56/#60 — block-contiguity pins (catches accidental reordering) ──
// These compile-time guards turn silent parameter-block drift into immediate
// build failures. They pin three contracts the audit identified as fragile:
//
// 1. The 30 SID-register parameters $D400..$D41D are contiguous in ParamID
// space. If anyone inserts a new ParamID between them, the file-bank
// serializer and the RO-register filter both desync silently.
// 2. The 4 read-only SID registers $D419..$D41C are contiguous so the
// isTransientFilter / serializer / GUI consumers can use a range check
// instead of four enumerated cases.
// 3. The HiFi block (kParamHiFiEnable..kParamHiFiVoiceDiffuser) is exactly
// 9 entries — matches the documented "0.0.1 v393" append batch and
// every persistence test that pins it.
static_assert(static_cast<int>(kParamSidRegD41D) - static_cast<int>(kParamSidRegD400) == 29,
              "SID register block ($D400..$D41D) must be exactly 30 contiguous ParamIDs");
static_assert(static_cast<int>(kParamSidRegD41C) - static_cast<int>(kParamSidRegD419) == 3,
              "Read-only SID register block ($D419..$D41C) must be 4 contiguous ParamIDs (range-check invariant)");
static_assert(static_cast<int>(kParamSidRegD41A) == static_cast<int>(kParamSidRegD419) + 1,
              "RO SID register $D41A immediately follows $D419");
static_assert(static_cast<int>(kParamSidRegD41B) == static_cast<int>(kParamSidRegD419) + 2,
              "RO SID register $D41B is two slots after $D419");
static_assert(static_cast<int>(kParamHiFiVoiceDiffuser) - static_cast<int>(kParamHiFiEnable) == 8,
              "HiFi block holds exactly 9 ParamIDs (v393 append batch — bump kNumParams if extending)");

// Range-check helper for the read-only SID register filter. Replaces the
// 4-case switch sprawl with a single comparison; the static_asserts above
// guarantee the underlying enum range is contiguous.
constexpr inline bool isReadOnlySidRegisterParam(int id) noexcept {
    return id >= static_cast<int>(kParamSidRegD419) &&
           id <= static_cast<int>(kParamSidRegD41C);
}



// Canonical default SID chip: the HMOS-II 8580 R5 class (the "new SID" family,
// a.k.a. the 8580/8510-class die) is the factory default in every component
// flavor and factory patch. Selector index 3 == "MOS 8580 R5".
constexpr int kSidChipRevisionDefaultIndex = 3;

inline int sidChipRevisionIndexFromNormalized(float norm) noexcept {
    const float v = std::isfinite(norm) ? std::clamp(norm, 0.0f, 1.0f)
                                        : static_cast<float>(kSidChipRevisionDefaultIndex) / 3.0f;
    return std::clamp((int)std::lround(v * 3.0f), 0, 3);
}

constexpr inline float sidChipRevisionIndexToNormalized(int idx) noexcept {
    return static_cast<float>(std::clamp(idx, 0, 3)) / 3.0f;
}

constexpr inline const char* sidChipRevisionSelectorLabelFromIndex(int idx) noexcept {
    switch (std::clamp(idx, 0, 3)) {
        case 0: return "MOS 6581 R2";
        case 1: return "MOS 6581 R3";
        case 2: return "MOS 6581 R4";
        default: return "MOS 8580 R5";
    }
}

inline const char* sidChipRevisionSelectorLabel(float norm) noexcept {
    return sidChipRevisionSelectorLabelFromIndex(sidChipRevisionIndexFromNormalized(norm));
}

inline bool sidChipRevisionSelectorIs6581(float norm) noexcept {
    return sidChipRevisionIndexFromNormalized(norm) < 3;
}

inline uint8_t sidChipRevisionSelectorRevision(float norm) noexcept {
    switch (sidChipRevisionIndexFromNormalized(norm)) {
        case 0: return 2u;
        case 1: return 3u;
        case 2: return 4u;
        default: return 5u;
    }
}

enum class DrSidMachineModel : uint8_t {
    SidAuthentic = 0,
    AnalogX0X8 = 1,
};

inline int drSidMachineModelIndexFromNormalized(float norm) noexcept {
    const float v = std::isfinite(norm) ? std::clamp(norm, 0.0f, 1.0f) : 0.0f;
    return std::clamp((int)std::lround(v), 0, 1);
}

constexpr inline float drSidMachineModelIndexToNormalized(int idx) noexcept {
    return static_cast<float>(std::clamp(idx, 0, 1));
}

constexpr inline const char* drSidMachineModelDisplayNameFromIndex(int idx) noexcept {
    switch (std::clamp(idx, 0, 1)) {
        case 1: return "Analog X0X-8";
        default: return "SID Drum Core";
    }
}

inline const char* drSidMachineModelDisplayName(float norm) noexcept {
    return drSidMachineModelDisplayNameFromIndex(drSidMachineModelIndexFromNormalized(norm));
}

inline bool drSidMachineModelIsAnalogX0X8(float norm) noexcept {
    return drSidMachineModelIndexFromNormalized(norm) == 1;
}

inline uint8_t sidOversamplingFactorFromNormalized(float norm) noexcept {
    const int idx = std::clamp((int)std::lround((std::isfinite(norm) ? std::clamp(norm, 0.0f, 1.0f) : 0.0f) * 3.0f), 0, 3);
    switch (idx) {
        case 1: return 2u;
        case 2: return 4u;
        case 3: return 8u;
        default: return 1u;
    }
}

inline const char* sidOversamplingLabel(float norm) noexcept {
    switch (sidOversamplingFactorFromNormalized(norm)) {
        case 2u: return "2x";
        case 4u: return "4x";
        case 8u: return "8x";
        default: return "1x";
    }
}

constexpr inline bool isHostMidiBridgeParam(int id) noexcept {
    return id >= (int)kParamHostCtrlModWheelBase && id <= (int)kParamHostCtrlLast;
}

constexpr inline int hostMidiBridgeChannelForParam(int id) noexcept {
    return isHostMidiBridgeParam(id) ? ((id - (int)kParamHostCtrlModWheelBase) & 0x0F) : -1;
}

constexpr inline Steinberg::Vst::ParamID hostMidiBridgeBaseForParam(int id) noexcept {
    if (!isHostMidiBridgeParam(id)) return (Steinberg::Vst::ParamID)-1;
    return (Steinberg::Vst::ParamID)(id - ((id - (int)kParamHostCtrlModWheelBase) & 0x0F));
}

constexpr inline bool isLegacyVariantPresentationParam(int id) noexcept {
    switch (static_cast<ParamID>(id)) {
        case kParamSidModel:
        case kParamSidClockSystem:
            return true;
        default:
            return false;
    }
}

constexpr int kCanonicalFactoryPatchSlotCount = 180;
constexpr int kCanonicalFactoryPatchSlotMax = kCanonicalFactoryPatchSlotCount - 1;

constexpr inline float canonicalNormalizedBankSlotValue(int slot) noexcept {
    const int normalized = std::clamp(slot, 0, kCanonicalFactoryPatchSlotMax);
    return static_cast<float>(normalized) / static_cast<float>(kCanonicalFactoryPatchSlotMax);
}

inline int canonicalFactorySlotFromNormalizedBankSlot(float value) noexcept {
    const float v = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    return std::clamp(static_cast<int>(std::lround(v * static_cast<float>(kCanonicalFactoryPatchSlotMax))),
                      0,
                      kCanonicalFactoryPatchSlotMax);
}

constexpr inline float canonicalNormalizedFactoryProgramValue(int slot) noexcept {
    return canonicalNormalizedBankSlotValue(slot);
}

constexpr inline bool isRuntimeOnlyOrTransientParam(int id) noexcept {
    if (id < 0 || id >= kNumParams) return true;
    if (isHostMidiBridgeParam(id)) return true;
    switch (static_cast<ParamID>(id)) {
        case kParamPanic:
        case kParamMidiActivity:
        case kParamLastNote:
        case kParamSampleRateRO:
        case kParamBufferSizeRO:
        case kParamActiveVoicesRO:
        case kParamVirtualNote:
        case kParamVirtualGate:
        case kParamSidRegD419:
        case kParamSidRegD41A:
        case kParamSidRegD41B:
        case kParamSidRegD41C:
            return true;
        case kParamSidModel:
        case kParamSidClockSystem:
            return true;
        default:
            return false;
    }
}


constexpr inline bool isFactoryPatchAudioAuthorityParam(int id) noexcept {
    if (id < 0 || id >= kNumParams) return false;
    switch (static_cast<ParamID>(id)) {
        case kParamMasterVolume:
        case kParamVCO1Waveform:
        case kParamVCO2Waveform:
        case kParamVCO3Waveform:
        case kParamVCO1Level:
        case kParamVCO2Level:
        case kParamVCO3Level:
        case kParamVCO1PulseWidth:
        case kParamVCO2PulseWidth:
        case kParamVCO3PulseWidth:
        case kParamVCO1SyncEnable:
        case kParamVCO2SyncEnable:
        case kParamVCO3SyncEnable:
        case kParamVCO1RingModEnable:
        case kParamVCO2RingModEnable:
        case kParamVCO3RingModEnable:
        case kParamAttack:
        case kParamDecay:
        case kParamSustain:
        case kParamRelease:
        case kParamFilterCutoff:
        case kParamFilterResonance:
        case kParamFilterMode:
        case kParamFilterEnvAmount:
        case kParamSynthModeEnable:
        case kParamDrSidEnable:
        case kParamDrSidMachineModel:
        case kParamDrSidAccentAmount:
        case kParamDrSidOutputDrive:
        case kParamDrSidHatMetal:
        case kParamDrSidClapSpread:
        case kParamPortamentoStyle:
        case kParamGlideDelta:
        case kParamPortamentoArpGlide:
        case kParamSidRegD400:
        case kParamSidRegD401:
        case kParamSidRegD402:
        case kParamSidRegD403:
        case kParamSidRegD404:
        case kParamSidRegD405:
        case kParamSidRegD406:
        case kParamSidRegD407:
        case kParamSidRegD408:
        case kParamSidRegD409:
        case kParamSidRegD40A:
        case kParamSidRegD40B:
        case kParamSidRegD40C:
        case kParamSidRegD40D:
        case kParamSidRegD40E:
        case kParamSidRegD40F:
        case kParamSidRegD410:
        case kParamSidRegD411:
        case kParamSidRegD412:
        case kParamSidRegD413:
        case kParamSidRegD414:
        case kParamSidRegD415:
        case kParamSidRegD416:
        case kParamSidRegD417:
        case kParamSidRegD418:
        case kParamSidRegD41D:
        case kParamSidChipRevision:
        case kParamSidExternalRcEnable:
        case kParamSidOversamplingFactor:
            return true;
        default:
            return false;
    }
}

constexpr inline bool isFactorySnapshotMetadataOrTransientParam(int id) noexcept {
    if (id < 0 || id >= kNumParams) return true;
    if (isRuntimeOnlyOrTransientParam(id)) return true;
    switch (static_cast<ParamID>(id)) {
        case kParamProgram:
        case kParamBankSlot:
        case kParamBankCommand:
            return true;
        default:
            return false;
    }
}

constexpr inline bool isTransportResetStructuralAuthorityParam(int id) noexcept {
    if (id < 0 || id >= kNumParams) return true;
    switch (static_cast<ParamID>(id)) {
        case kParamProgram:
        case kParamBankSlot:
        case kParamBankCommand:
        case kParamSynthModeEnable:
        case kParamDrSidEnable:
            return true;
        default:
            return isRuntimeOnlyOrTransientParam(id);
    }
}

constexpr inline bool isValidParamIndex(int id) noexcept {
    return id >= 0 && id < kNumParams;
}

struct ParamInfo {
    const char* name;
    const char* unit;
    float defaultNorm; // normalized default 0..1
    bool automatable;
};

// Parameter metadata table (must be kNumParams long)
inline const std::array<ParamInfo, kNumParams> kParamInfos = []{
    std::array<ParamInfo, kNumParams> a{};

    auto set = [&](int id, const char* n, const char* u, float def, bool canAuto=true){
        a[(size_t)id] = ParamInfo{n, u, def, canAuto};
    };

    // Master
    set(kParamMasterVolume, "Master Volume", "norm", 0.8f);
    set(kParamMasterTune,   "Master Tune",   "cent", 0.5f);  // ±100 cents fine tune
    set(kParamPortamentoTime, "Portamento", "s", 0.0f);
    set(kParamVoiceMode, "Voice Mode", "", 0.0f);
    set(kParamVoiceSpread, "Voice Spread", "", 0.0f);

    // VCOs
    set(kParamVCO1Waveform, "VCO1 Waveform", "", 0.25f);
    set(kParamVCO1PulseWidth, "VCO1 Pulse Width", "", 0.5f);
    set(kParamVCO1Detune, "VCO1 Detune", "", 0.5f);
    set(kParamVCO1Level, "VCO1 Level", "", 0.8f);
    set(kParamVCO1LowFreqMode, "VCO1 LF Mode", "", 0.0f);
    set(kParamVCO1PWMDepth, "VCO1 PWM Depth", "", 0.0f);
    set(kParamVCO1SyncEnable, "VCO1 Sync", "", 0.0f);
    set(kParamVCO1RingModEnable, "VCO1 RingMod", "", 0.0f);

    // True-tune default: all three generated oscillators start at concert pitch.
    // Detune remains available per patch, but a clean/default note must not
    // sound falsely tuned merely because VCO2/VCO3 were pre-spread.
    set(kParamVCO2Waveform,    "VCO2 Waveform",    "", 0.125f); // Sawtooth (idx 1/8)
    set(kParamVCO2PulseWidth,  "VCO2 Pulse Width", "", 0.5f);
    set(kParamVCO2Detune,      "VCO2 Detune",      "", 0.5f);
    set(kParamVCO2Level,       "VCO2 Level",       "", 0.65f);
    set(kParamVCO2LowFreqMode, "VCO2 LF Mode",     "", 0.0f);
    set(kParamVCO2PWMDepth,    "VCO2 PWM Depth",   "", 0.0f);
    set(kParamVCO2SyncEnable,  "VCO2 Sync",        "", 0.0f);
    set(kParamVCO2RingModEnable,"VCO2 RingMod",    "", 0.0f);

    set(kParamVCO3Waveform,    "VCO3 Waveform",    "", 0.125f); // Sawtooth (idx 1/8)
    set(kParamVCO3PulseWidth,  "VCO3 Pulse Width", "", 0.5f);
    set(kParamVCO3Detune,      "VCO3 Detune",      "", 0.5f);
    set(kParamVCO3Level,       "VCO3 Level",       "", 0.65f);
    set(kParamVCO3LowFreqMode, "VCO3 LF Mode",     "", 0.0f);
    set(kParamVCO3PWMDepth,    "VCO3 PWM Depth",   "", 0.0f);
    set(kParamVCO3SyncEnable,  "VCO3 Sync",        "", 0.0f);
    set(kParamVCO3RingModEnable,"VCO3 RingMod",    "", 0.0f);

    // Filter — C64 authentic: LowPass 72%, Resonance 35%, slight env mod
    set(kParamFilterCutoff,    "Filter Cutoff",     "", 0.72f);
    set(kParamFilterResonance, "Filter Resonance",  "", 0.35f);
    set(kParamFilterMode,      "Filter Mode",       "", 1.0f / 7.0f);  // LowPass (index 1 of 8; index 0 is None/bypass)
    set(kParamFilterEnvAmount, "Filter Env Amount", "", 0.20f); // subtle filter sweep
    set(kParamFilterLFOAmount, "Filter LFO Amount", "", 0.0f);
    set(kParamFilterKeyTrack,  "Filter KeyTrack",   "", 0.0f);
    set(kParamFilterDrive,     "Filter Drive",      "", 0.0f);

    // ADSR — C64: A=0(2ms), D=4(24ms), S=8(53%), R=3(15ms)
    set(kParamAttack,  "Attack",  "", 0.0f);    // Attack = 0 → 2ms (instant)
    set(kParamDecay,   "Decay",   "", 0.267f);  // Decay = 4 → 24ms
    set(kParamSustain, "Sustain", "", 0.533f);  // Sustain = 8 → 53%
    set(kParamRelease, "Release", "", 0.2f);    // Release = 3 → 15ms

    // LFO1
    set(kParamLFORate, "LFO1 Rate", "Hz", 0.2f);
    set(kParamLFODepth, "LFO1 Depth", "", 0.5f);
    set(kParamLFOShape, "LFO1 Shape", "", 0.0f);
    set(kParamLFOSync, "LFO1 Sync", "", 0.0f);

    // LFO2
    set(kParamLFO2Rate, "LFO2 Rate", "Hz", 0.2f);
    set(kParamLFO2Depth, "LFO2 Depth", "", 0.0f);
    set(kParamLFO2Shape, "LFO2 Shape", "", 0.0f);
    set(kParamLFO2Sync, "LFO2 Sync", "", 0.0f);

    set(kParamLFO3Rate, "LFO3 Rate", "Hz", 0.2f);
    set(kParamLFO3Depth, "LFO3 Depth", "", 0.0f);
    set(kParamLFO3Shape, "LFO3 Shape", "", 0.0f);
    set(kParamLFO3Sync, "LFO3 Sync", "", 0.0f);

    set(kParamLFO4Rate, "LFO4 Rate", "Hz", 0.2f);
    set(kParamLFO4Depth, "LFO4 Depth", "", 0.0f);
    set(kParamLFO4Shape, "LFO4 Shape", "", 0.0f);
    set(kParamLFO4Sync, "LFO4 Sync", "", 0.0f);

    // Arp
    set(kParamArpEnable, "Arp Enable", "", 0.0f);
    set(kParamArpMode, "Arp Mode", "", 0.0f);
    set(kParamArpRate, "Arp Rate", "", 0.2f);
    set(kParamArpOctaves, "Arp Octaves", "", 0.0f);
    set(kParamArpSwing, "Arp Swing", "", 0.0f);
    set(kParamArpGate, "Arp Gate", "", 0.9f);
    set(kParamArpHold, "Arp Hold", "", 0.0f);
    set(kParamArpLatch, "Arp Latch", "", 0.0f);
    set(kParamArpTranspose, "Arp Transpose", "", 0.5f);
    set(kParamArpRandom, "Arp Random", "", 0.0f);
    set(kParamArpPatternLength, "Arp Pattern Length", "", 0.5f);

    // Drums
    set(kParamDrSidEnable, "DrSID Enable", "", 0.0f);
    set(kParamDrSidKickTune, "Kick Tune", "", 0.5f);
    set(kParamDrSidKickDecay, "Kick Decay", "", 0.2f);
    set(kParamDrSidSnareTone, "Snare Tone", "", 0.5f);
    set(kParamDrSidSnareSnap, "Snare Snap", "", 0.6f);
    set(kParamDrSidHatTune, "Hat Tune", "", 0.7f);
    set(kParamDrSidHatDecay, "Hat Decay", "", 0.2f);
    set(kParamDrSidClapDecay, "Clap Decay", "", 0.25f);
    set(kParamDrSidCowbellTune, "Cowbell Tune", "", 0.8f);
    set(kParamDrSidVolume, "DrSID Volume", "", 0.8f);

    // Sequencer globals
    set(kParamSeqEnable, "Seq Enable", "", 0.0f);
    set(kParamSeqTempo, "Seq Tempo", "bpm", 0.4f);
    set(kParamSeqSwing, "Seq Swing", "", 0.0f);
    set(kParamSeqMode, "Seq Mode", "", 0.0f);
    set(kParamSeqLength, "Seq Length", "steps", 0.5f);

    // Steps
    int id = kParamSeqStep1Note;
    // Audit #62 fix: emit UNIQUE names per step at registration time so
    // AUv2/AUv3/VST3 hosts that snapshot parameter names BEFORE the
    // controller's runtime name-fixup pass still get automation-lane
    // labels like "Seq Step 1 Note" / "Seq Step 1 Vel" / "Seq Step 1 Gate"
    // — not 96 duplicate "Seq Step Note"/"Seq Step Velocity"/"Seq Step Gate".
    //
    // The static buffer is sized for "Seq Step 32 Velocity" (max 19 chars
    // + nul). We use a small per-iteration scratch and pin the strings'
    // lifetime by storing them in a static per-step buffer array; the
    // host-side reads names before any seqlock contention so this is
    // strictly non-RT.
    static char stepNoteNames[32][24]    {};
    static char stepVelocityNames[32][24]{};
    static char stepGateNames[32][24]    {};
    for (int step = 1; step <= 32; ++step) {
        const int sidx = step - 1;
        std::snprintf(stepNoteNames[sidx],     sizeof(stepNoteNames[sidx]),     "Seq Step %d Note",     step);
        std::snprintf(stepVelocityNames[sidx], sizeof(stepVelocityNames[sidx]), "Seq Step %d Vel",      step);
        std::snprintf(stepGateNames[sidx],     sizeof(stepGateNames[sidx]),     "Seq Step %d Gate",     step);
        set(id++, stepNoteNames[sidx],     "", 0.5f);
        set(id++, stepVelocityNames[sidx], "", 1.0f);
        set(id++, stepGateNames[sidx],     "", 1.0f);
    }

    // Macros
    set(kParamMacro1, "Macro 1", "", 0.0f);
    set(kParamMacro2, "Macro 2", "", 0.0f);
    set(kParamMacro3, "Macro 3", "", 0.0f);
    set(kParamMacro4, "Macro 4", "", 0.0f);
    set(kParamMacro5, "Macro 5", "", 0.0f);
    set(kParamMacro6, "Macro 6", "", 0.0f);
    set(kParamMacro7, "Macro 7", "", 0.0f);
    set(kParamMacro8, "Macro 8", "", 0.0f);

    // Output / FX
    set(kParamOutputLimiter, "Output Limiter", "", 1.0f);
    set(kParamLimiterThreshold, "Limiter Threshold", "", 0.94f);
    set(kParamLimiterAttack, "Limiter Attack", "ms", 0.08f);
    set(kParamLimiterRelease, "Limiter Release", "ms", 0.35f);
    set(kParamReverbMix, "Reverb Mix", "", 0.0f);

    // Mod Matrix (default: no routing)
    set(kParamModVCFCutoffSource, "Mod: VCF Cutoff Source", "src", 0.0f);
    set(kParamModVCFCutoffDepth, "Mod: VCF Cutoff Depth", "", 0.0f);
    set(kParamModVCFResonanceSource, "Mod: VCF Res Source", "src", 0.0f);
    set(kParamModVCFResonanceDepth, "Mod: VCF Res Depth", "", 0.0f);
    set(kParamModVCO1FreqSource, "Mod: VCO1 Freq Source", "src", 0.0f);
    set(kParamModVCO1FreqDepth, "Mod: VCO1 Freq Depth", "", 0.0f);
    set(kParamModVCO1PWSource, "Mod: VCO1 PW Source", "src", 0.0f);
    set(kParamModVCO1PWDepth, "Mod: VCO1 PW Depth", "", 0.0f);
    set(kParamModVCO2FreqSource, "Mod: VCO2 Freq Source", "src", 0.0f);
    set(kParamModVCO2FreqDepth, "Mod: VCO2 Freq Depth", "", 0.0f);
    set(kParamModVCO2PWSource, "Mod: VCO2 PW Source", "src", 0.0f);
    set(kParamModVCO2PWDepth, "Mod: VCO2 PW Depth", "", 0.0f);
    set(kParamModVCO3FreqSource, "Mod: VCO3 Freq Source", "src", 0.0f);
    set(kParamModVCO3FreqDepth, "Mod: VCO3 Freq Depth", "", 0.0f);
    set(kParamModVCO3PWSource, "Mod: VCO3 PW Source", "src", 0.0f);
    set(kParamModVCO3PWDepth, "Mod: VCO3 PW Depth", "", 0.0f);
    set(kParamModMasterVolumeSource, "Mod: Volume Source", "src", 0.0f);
    set(kParamModMasterVolumeDepth, "Mod: Volume Depth", "", 0.0f);

    // ----------------------------------------------------------------------
    // Synth Mode (SID register-driven)
    // ----------------------------------------------------------------------
    // Default OFF: BitPerfect engine should be the default playback mode.
    // Synth Mode is an advanced register-driven mode.
    set(kParamSynthModeEnable, "Synth Mode (SID Reg)", "", 0.0f);
    set(kParamSidModel, "SID Model (legacy mirror)", "", 1.0f, false); // legacy presentation mirror only
    set(kParamSidAdsrBug6581, "6581 ADSR Bug", "", 0.0f);

    auto setSid = [&](int id, const char* name, float defNorm, bool canAuto=true){
        a[(size_t)id] = ParamInfo{name, "byte", defNorm, canAuto};
    };

    setSid(kParamSidRegD400, "$D400 FREQ1 LO", 0.0f);
    setSid(kParamSidRegD401, "$D401 FREQ1 HI", 0.0f);
    setSid(kParamSidRegD402, "$D402 PW1 LO",   0.0f);
    setSid(kParamSidRegD403, "$D403 PW1 HI",   0.0f);
    setSid(kParamSidRegD404, "$D404 CTRL1",    0.0f);
    setSid(kParamSidRegD405, "$D405 AD1",      0.0f);
    setSid(kParamSidRegD406, "$D406 SR1",      0.0f);

    setSid(kParamSidRegD407, "$D407 FREQ2 LO", 0.0f);
    setSid(kParamSidRegD408, "$D408 FREQ2 HI", 0.0f);
    setSid(kParamSidRegD409, "$D409 PW2 LO",   0.0f);
    setSid(kParamSidRegD40A, "$D40A PW2 HI",   0.0f);
    setSid(kParamSidRegD40B, "$D40B CTRL2",    0.0f);
    setSid(kParamSidRegD40C, "$D40C AD2",      0.0f);
    setSid(kParamSidRegD40D, "$D40D SR2",      0.0f);

    setSid(kParamSidRegD40E, "$D40E FREQ3 LO", 0.0f);
    setSid(kParamSidRegD40F, "$D40F FREQ3 HI", 0.0f);
    setSid(kParamSidRegD410, "$D410 PW3 LO",   0.0f);
    setSid(kParamSidRegD411, "$D411 PW3 HI",   0.0f);
    setSid(kParamSidRegD412, "$D412 CTRL3",    0.0f);
    setSid(kParamSidRegD413, "$D413 AD3",      0.0f);
    setSid(kParamSidRegD414, "$D414 SR3",      0.0f);

    setSid(kParamSidRegD415, "$D415 FC LO",    0.0f);
    setSid(kParamSidRegD416, "$D416 FC HI",    0.0f);
    setSid(kParamSidRegD417, "$D417 RES/FILT", 0.0f);
    setSid(kParamSidRegD418, "$D418 MODE/VOL", 15.0f/255.0f);

    // Readbacks (RO) - show as meters (not automatable)
    setSid(kParamSidRegD419, "$D419 POTX (RO)", 0.0f, false);
    setSid(kParamSidRegD41A, "$D41A POTY (RO)", 0.0f, false);
    setSid(kParamSidRegD41B, "$D41B OSC3 (RO)", 0.0f, false);
    setSid(kParamSidRegD41C, "$D41C ENV3 (RO)", 0.0f, false);

    // Pseudo system/model byte
    setSid(kParamSidRegD41D, "$D41D SYSTEM (pseudo)", 0.0f);

    // ----------------------------------------------------------------------
    // GUI/HUD telemetry + virtual keyboard
    // ----------------------------------------------------------------------
    set(kParamPanic, "PANIC (All Notes Off)", "", 0.0f, false);

    // Read-only meters (processor updates)
    set(kParamMidiActivity, "MIDI Activity (RO)", "", 0.0f, false);
    set(kParamLastNote, "Last Note (RO)", "", 0.0f, false);
    set(kParamSampleRateRO, "Sample Rate (RO)", "", 0.0f, false);
    set(kParamBufferSizeRO, "Buffer Size (RO)", "", 0.0f, false);
    set(kParamActiveVoicesRO, "Active Voices (RO)", "", 0.0f, false);

    // Virtual keyboard injection
    set(kParamVirtualNote, "Virtual Note", "", 0.0f);
    set(kParamVirtualGate, "Virtual Gate", "", 0.0f);

    
// Program selection (VST3 Program List)
// kParamProgram is a non-automatable factory identity mirror in v500+ paths.
set(kParamProgram, "Program", "", 0.0f, false);
set(kParamBankSlot, "Bank Slot", "", 0.0f, false);
set(kParamBankCommand, "Bank Command", "", 0.0f, false);
set(kParamSidClockSystem, "SID Clock System (legacy mirror)", "", 0.0f, false);
    set(kParamForensicEnable, "Forensic Enable", "", 0.0f);
    set(kParamForensicStartupRandom, "Startup Random", "", 0.0f);
    set(kParamForensicClockJitterEnable, "Clock Jitter Enable", "", 0.0f);
    set(kParamForensicClockJitter, "Clock Jitter", "%", 0.0f);
    set(kParamForensicSupplyRippleEnable, "Supply Ripple Enable", "", 0.0f);
    set(kParamForensicSupplyRipple, "Supply Ripple", "%", 0.0f);
    set(kParamForensicThermalDriftEnable, "Thermal Drift Enable", "", 0.0f);
    set(kParamForensicThermalDrift, "Thermal Drift", "%", 0.0f);
    set(kParamForensicVoiceCrosstalkEnable, "Voice Crosstalk Enable", "", 0.0f);
    set(kParamForensicVoiceCrosstalk, "Voice Crosstalk", "%", 0.0f);
    set(kParamForensicExternalBleedEnable, "External Bleed Enable", "", 0.0f);
    set(kParamForensicExternalBleed, "External Bleed", "%", 0.0f);
    set(kParamForensicDigifix8580, "8580 Digifix", "", 1.0f);
    set(kParamForensicIntensity, "Forensic Intensity", "%", 0.0f);
    set(kParamForensicTemp, "Chip Temperature °C", "degC", 0.375f);
    set(kParamForensicSupply, "Supply Voltage", "V", 0.5f);
    set(kParamForensicRevision, "Forensic Revision (legacy mirror)", "rev", 1.0f); // mirrors 8580 R5 default
    set(kParamForensicChipSeed, "Chip Variation", "seed", 0.86983866f);
    set(kParamForensicEnvelopeTDM, "Envelope TDM", "%", 0.0f);
    set(kParamForensicD418Asymmetry, "D418 Asymmetry", "%", 0.0f);
    set(kParamForensicFilterOhmic, "Filter Ohmic", "%", 0.0f);
    set(kParamForensicSystemNoise, "System Noise", "%", 0.0f);
    set(kParamForensicMotherboard, "Motherboard", "%", 0.0f);
    set(kParamForensicADCBleed, "ADC Bleed", "%", 0.0f);
    set(kParamForensicBusCollision, "Bus Collision", "%", 0.0f);
    set(kParamForensicPOTInput, "POT Input", "%", 0.0f);
    set(kParamDrSidCowbellDecay, "Cowbell Decay", "", 0.42f);
    set(kParamDrSidTomTune, "Tom Tune", "", 0.55f);
    set(kParamDrSidTomDecay, "Tom Decay", "", 0.48f); // v909: longer singing 808 tom default

    set(kParamSidChipRevision, "SID Chip Revision", "rev",
        sidChipRevisionIndexToNormalized(kSidChipRevisionDefaultIndex)); // MOS 8580 R5 (HMOS-II) default
    set(kParamSidExternalRcEnable, "C64 External RC Filter", "", 1.0f);
    set(kParamSidOversamplingFactor, "SID Oversampling", "x", 0.0f);

    for (int ch = 0; ch < 16; ++ch) {
        set((int)kParamHostCtrlModWheelBase + ch, "Host MIDI ModWheel", "", 0.0f, false);
        set((int)kParamHostCtrlBreathBase + ch, "Host MIDI Breath", "", 0.0f, false);
        set((int)kParamHostCtrlExpressionBase + ch, "Host MIDI Expression", "", 0.0f, false);
        set((int)kParamHostCtrlSustainBase + ch, "Host MIDI Sustain", "", 0.0f, false);
        set((int)kParamHostCtrlSostenutoBase + ch, "Host MIDI Sostenuto", "", 0.0f, false);
        set((int)kParamHostCtrlChannelPressureBase + ch, "Host MIDI Channel Pressure", "", 0.0f, false);
        set((int)kParamHostCtrlPitchBendBase + ch, "Host MIDI Pitch Bend", "", 0.5f, false);
        set((int)kParamHostCtrlRpnMsbBase + ch, "Host MIDI RPN MSB", "", 1.0f, false);
        set((int)kParamHostCtrlRpnLsbBase + ch, "Host MIDI RPN LSB", "", 1.0f, false);
        set((int)kParamHostCtrlNrpnMsbBase + ch, "Host MIDI NRPN MSB", "", 0.0f, false);
        set((int)kParamHostCtrlNrpnLsbBase + ch, "Host MIDI NRPN LSB", "", 0.0f, false);
        set((int)kParamHostCtrlDataEntryMsbBase + ch, "Host MIDI Data Entry MSB", "", 0.0f, false);
        set((int)kParamHostCtrlDataEntryLsbBase + ch, "Host MIDI Data Entry LSB", "", 0.0f, false);
    }

    set(kParamDrSidMachineModel, "Drum Machine Model", "", 0.0f);
    set(kParamDrSidAccentAmount, "Drum Accent", "", 0.68f);
    set(kParamDrSidOutputDrive, "Drum Drive", "", 0.18f);
    set(kParamDrSidHatMetal, "Hat Metal", "", 0.62f);
    set(kParamDrSidClapSpread, "Clap Spread", "", 0.54f);

    set(kParamPortamentoStyle, "Portamento Style", "", 0.0f);
    set(kParamGlideDelta, "C64 Glide Delta", "reg/frame", 32.0f / 255.0f);
    set(kParamPortamentoArpGlide, "Arp Glide Legato", "", 0.0f);
    set(kParamHiFiEnable, "HI-FI Enable", "", 0.0f);
    set(kParamHiFiQuality, "HI-FI Quality", "", 1.0f);
    set(kParamHiFiOversampling, "HI-FI Super-Hires", "x", 0.5f);
    set(kParamHiFiMasterWidth, "HI-FI Width", "", 0.60f);
    set(kParamHiFiTapeSaturation, "HI-FI Tape", "", 0.45f);
    set(kParamHiFiAnalogWarmth, "HI-FI Warmth", "", 0.68f);
    set(kParamHiFiPsychoExciter, "HI-FI Exciter", "", 0.82f);
    set(kParamHiFiStereoDepth, "HI-FI Depth", "", 0.65f);
    set(kParamHiFiVoiceDiffuser, "HI-FI Diffuser", "", 0.35f);

    // v909 Classic-mode authority: GM channel-10 auto-promotion is opt-in.
    set(kParamAutoGmDrumPromotion, "Auto GM Drum Promotion", "", 0.0f);

return a;
}();

constexpr inline float clampNormalized01(float v) noexcept {
    return v <= 0.0f ? 0.0f : (v >= 1.0f ? 1.0f : v);
}

// Number of equal intervals in the normalized representation of a discrete
// parameter (two values => one interval).  Zero means genuinely continuous.
// This is the single cardinality contract used by state repair and host
// metadata; keep semantic decisions here rather than inferring them from
// display names in each plug-in wrapper.
constexpr inline int normalizedParamStepCount(int id) noexcept {
    if (id < 0 || id >= kNumParams) return 0;

    if (id >= static_cast<int>(kParamSidRegD400) &&
        id <= static_cast<int>(kParamSidRegD41D)) return 255;

    if (id >= static_cast<int>(kParamSeqStep1Note) &&
        id <= static_cast<int>(kParamSeqStep32Gate)) {
        const int field = (id - static_cast<int>(kParamSeqStep1Note)) % 3;
        return field == 0 ? 127 : 0; // note is discrete; velocity/gate are continuous
    }

    if (id >= static_cast<int>(kParamModVCFCutoffSource) &&
        id <= static_cast<int>(kParamModMasterVolumeSource) &&
        ((id - static_cast<int>(kParamModVCFCutoffSource)) % 2) == 0) {
        return 21; // SidModSource: None plus 21 named sources
    }

    if (id >= static_cast<int>(kParamHostCtrlModWheelBase) &&
        id <= static_cast<int>(kParamHostCtrlLast)) {
        if (id >= static_cast<int>(kParamHostCtrlPitchBendBase) &&
            id < static_cast<int>(kParamHostCtrlPitchBendBase) + 16) return 16383;
        return 127;
    }

    switch (static_cast<ParamID>(id)) {
        // Boolean controls and momentary binary actions.
        case kParamVCO1LowFreqMode: case kParamVCO2LowFreqMode: case kParamVCO3LowFreqMode:
        case kParamVCO1SyncEnable: case kParamVCO2SyncEnable: case kParamVCO3SyncEnable:
        case kParamVCO1RingModEnable: case kParamVCO2RingModEnable: case kParamVCO3RingModEnable:
        case kParamLFOSync: case kParamLFO2Sync: case kParamLFO3Sync: case kParamLFO4Sync:
        case kParamArpEnable: case kParamArpHold: case kParamArpLatch:
        case kParamDrSidEnable: case kParamSeqEnable: case kParamOutputLimiter:
        case kParamSynthModeEnable: case kParamSidModel: case kParamSidAdsrBug6581:
        case kParamPanic: case kParamVirtualGate: case kParamBankCommand:
        case kParamSidClockSystem:
        case kParamForensicEnable: case kParamForensicStartupRandom:
        case kParamForensicClockJitterEnable: case kParamForensicSupplyRippleEnable:
        case kParamForensicThermalDriftEnable: case kParamForensicVoiceCrosstalkEnable:
        case kParamForensicExternalBleedEnable: case kParamForensicDigifix8580:
        case kParamSidExternalRcEnable: case kParamPortamentoArpGlide:
        case kParamHiFiEnable: case kParamAutoGmDrumPromotion:
        case kParamDrSidMachineModel:
            return 1;

        case kParamVCO1Waveform: case kParamVCO2Waveform: case kParamVCO3Waveform:
        case kParamFilterMode:
            return 7; // eight SID waveform/filter bit combinations
        case kParamVoiceMode: case kParamSeqMode: case kParamSidChipRevision:
        case kParamForensicRevision: case kParamSidOversamplingFactor:
        case kParamPortamentoStyle:
            return 3;
        case kParamArpMode: case kParamLFOShape: case kParamLFO2Shape:
        case kParamLFO3Shape: case kParamLFO4Shape:
            return 6;
        case kParamArpOctaves:
            return 3;
        case kParamArpTranspose:
            return 48;
        case kParamArpPatternLength: case kParamSeqLength:
            return 31;
        case kParamVirtualNote:
            return 127;
        case kParamProgram: case kParamBankSlot:
            return 179;
        case kParamHiFiQuality: case kParamHiFiOversampling:
            return 2;
        default:
            return 0;
    }
}

constexpr inline bool isBooleanNormalizedParam(int id) noexcept {
    if (normalizedParamStepCount(id) != 1) return false;
    // This binary enum is not an on/off switch even though it has two values.
    return id != static_cast<int>(kParamDrSidMachineModel);
}

constexpr inline bool isPatchPersistentParam(int id) noexcept {
    return id >= 0 && id < kNumParams && !isFactorySnapshotMetadataOrTransientParam(id);
}

inline float defaultNormalizedParamValue(int id) noexcept {
    if (id < 0 || id >= kNumParams) return 0.0f;
    const float v = kParamInfos[(size_t)id].defaultNorm;
    return std::isfinite(v) ? clampNormalized01(v) : 0.0f;
}

inline float sanitizeNormalizedParamValue(int id, float value, float fallback) noexcept {
    const float def = defaultNormalizedParamValue(id);
    const float fb = std::isfinite(fallback) ? clampNormalized01(fallback) : def;
    const float v = std::isfinite(value) ? value : fb;
    const float clamped = clampNormalized01(v);
    const int steps = normalizedParamStepCount(id);
    if (steps <= 0) return clamped;
    // These legacy DSP selectors decode equal-width half-open bins with floor,
    // not nearest-step rounding. Canonicalize to a stable grid point while
    // preserving the already-selected semantic value (important for authored
    // factory values such as waveform 0.22, which selects index 1).
    if (id == static_cast<int>(kParamVCO1Waveform) ||
        id == static_cast<int>(kParamVCO2Waveform) ||
        id == static_cast<int>(kParamVCO3Waveform) ||
        id == static_cast<int>(kParamFilterMode)) {
        const int index = std::clamp(static_cast<int>(std::floor(clamped * 8.0f)), 0, 7);
        return static_cast<float>(index) / 7.0f;
    }
    if (id == static_cast<int>(kParamArpOctaves)) {
        const int index = std::clamp(static_cast<int>(std::floor(clamped * 3.0f)), 0, 3);
        return static_cast<float>(index) / 3.0f;
    }
    return clampNormalized01(std::round(clamped * static_cast<float>(steps)) /
                             static_cast<float>(steps));
}

} // namespace ArpSID
