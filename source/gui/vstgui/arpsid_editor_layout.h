// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — editor layout shared by the cross-platform (VSTGUI) editor.
//
// Every tab of the canonical ArpSID GUI (tab_architecture.h) is a list of
// rows; every row is a list of sections. A section is either a titled group
// of parameter controls (the same groups the Cocoa editor builds with
// _sect:t:f:pids:) or a live display / model editor. Keeping this table free
// of UI-toolkit types lets a plain unit test prove that every user parameter
// is reachable from the editor.
#pragma once

#include "arpsid/gui/tab_architecture.h"
#include "parameter_ids.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ArpSID::GUI::EditorLayout {

enum class Display : std::uint8_t {
    None = 0,            // plain parameter section
    Oscilloscope,        // main output scope
    FilterResponse,      // filter curve from cutoff/resonance/mode
    FilterScopes,        // filter input/output scopes
    LfoWaves,            // four LFO shapes with live phase
    VcoScopes,           // three SID voice oscillator scopes
    SidRegisters,        // live $D400-$D41C register grid
    SeqSteps,            // 32-step note/velocity/gate editor (Seq Step params)
    DrumMeters,          // per-drum-class activity meters
    ForensicMeters,      // forensic activity readout
    HiFiMeters,          // Hi-Fi dry/wet/delta meters
    ModMatrixMonitor,    // LFO values / mod wheel / pressure readout
    SidCoreTimeline,     // SID bus register/value/write scopes
    C64Player,           // .sid load/eject, subtune, transport
    C64Machine,          // CPU/VIC/CIA/disassembly readout
    Bank,                // 180-slot factory patch browser
    Settings,            // SETTINGS model editor
    Mix,                 // MIX model editor
    Kit,                 // KIT model editor
    Digi,                // DIGI model editor + sample import
    Actions,             // panic / all-notes-off
};

inline constexpr int kEnd = -1;

struct Section {
    const char* title;
    Display display;
    float widthWeight;
    std::array<int, 24> params; // kEnd-terminated
};

struct Row {
    float heightWeight;
    std::array<Section, 5> sections; // title == nullptr terminates
};

struct Tab {
    ArpSIDTab id;
    std::array<Row, 4> rows; // heightWeight == 0 terminates
};

// Short helpers keep the table readable.
#define ARPSID_P(...) std::array<int, 24>{__VA_ARGS__, kEnd}
#define ARPSID_D(title, display, w) Section{title, display, w, std::array<int, 24>{kEnd}}
#define ARPSID_S(title, w, ...) Section{title, Display::None, w, ARPSID_P(__VA_ARGS__)}
#define ARPSID_SD(title, display, w, ...) Section{title, display, w, ARPSID_P(__VA_ARGS__)}
#define ARPSID_END Section{nullptr, Display::None, 0.f, std::array<int, 24>{kEnd}}

inline const std::array<Tab, kTabCount>& tabs() {
    static const std::array<Tab, kTabCount> t = {{
        {ArpSIDTab::Main, {{
            {0.8f, {{ARPSID_S("MASTER", 1.4f, kParamMasterVolume, kParamMasterTune, kParamPortamentoTime,
                              kParamPortamentoStyle, kParamGlideDelta, kParamVoiceMode, kParamVoiceSpread),
                     ARPSID_D("OUTPUT", Display::Oscilloscope, 1.0f), ARPSID_END}}},
            {1.0f, {{ARPSID_S("VCO 1", 1.f, kParamVCO1Waveform, kParamVCO1PulseWidth, kParamVCO1PWMDepth, kParamVCO1Detune,
                              kParamVCO1Level, kParamVCO1LowFreqMode, kParamVCO1SyncEnable, kParamVCO1RingModEnable),
                     ARPSID_S("VCO 2", 1.f, kParamVCO2Waveform, kParamVCO2PulseWidth, kParamVCO2PWMDepth, kParamVCO2Detune,
                              kParamVCO2Level, kParamVCO2LowFreqMode, kParamVCO2SyncEnable, kParamVCO2RingModEnable),
                     ARPSID_S("VCO 3", 1.f, kParamVCO3Waveform, kParamVCO3PulseWidth, kParamVCO3PWMDepth, kParamVCO3Detune,
                              kParamVCO3Level, kParamVCO3LowFreqMode, kParamVCO3SyncEnable, kParamVCO3RingModEnable),
                     ARPSID_END}}},
            {1.1f, {{ARPSID_SD("FILTER", Display::FilterResponse, 1.7f, kParamFilterCutoff, kParamFilterResonance,
                               kParamFilterMode, kParamFilterDrive, kParamFilterKeyTrack),
                     ARPSID_S("ADSR", 0.8f, kParamAttack, kParamDecay, kParamSustain, kParamRelease),
                     ARPSID_S("OUTPUT / FX", 1.f, kParamOutputLimiter, kParamLimiterThreshold, kParamLimiterAttack,
                              kParamLimiterRelease, kParamReverbMix),
                     ARPSID_END}}},
        }}},
        {ArpSIDTab::LfoArp, {{
            {1.0f, {{ARPSID_S("LFO 1", 1.f, kParamLFORate, kParamLFODepth, kParamLFOShape, kParamLFOSync),
                     ARPSID_S("LFO 2", 1.f, kParamLFO2Rate, kParamLFO2Depth, kParamLFO2Shape, kParamLFO2Sync),
                     ARPSID_S("LFO 3", 1.f, kParamLFO3Rate, kParamLFO3Depth, kParamLFO3Shape, kParamLFO3Sync),
                     ARPSID_S("LFO 4", 1.f, kParamLFO4Rate, kParamLFO4Depth, kParamLFO4Shape, kParamLFO4Sync),
                     ARPSID_END}}},
            {0.8f, {{ARPSID_D("LFO WAVES", Display::LfoWaves, 1.f), ARPSID_END}}},
            {1.0f, {{ARPSID_S("ARPEGGIATOR", 1.f, kParamArpEnable, kParamArpMode, kParamArpRate, kParamArpOctaves,
                              kParamArpSwing, kParamArpGate, kParamArpHold, kParamArpLatch, kParamArpTranspose,
                              kParamArpRandom, kParamArpPatternLength, kParamPortamentoArpGlide),
                     ARPSID_END}}},
        }}},
        {ArpSIDTab::SidRegisters, {{
            {0.9f, {{ARPSID_S("SYNTH MODE", 1.f, kParamSynthModeEnable, kParamSidChipRevision, kParamSidExternalRcEnable,
                              kParamSidOversamplingFactor, kParamSidAdsrBug6581, kParamSidRegD41D),
                     ARPSID_D("LIVE REGISTERS", Display::SidRegisters, 1.4f), ARPSID_END}}},
            {1.0f, {{ARPSID_S("VOICE 1", 1.f, kParamSidRegD400, kParamSidRegD401, kParamSidRegD402, kParamSidRegD403,
                              kParamSidRegD404, kParamSidRegD405, kParamSidRegD406),
                     ARPSID_S("VOICE 2", 1.f, kParamSidRegD407, kParamSidRegD408, kParamSidRegD409, kParamSidRegD40A,
                              kParamSidRegD40B, kParamSidRegD40C, kParamSidRegD40D),
                     ARPSID_END}}},
            {1.0f, {{ARPSID_S("VOICE 3", 1.f, kParamSidRegD40E, kParamSidRegD40F, kParamSidRegD410, kParamSidRegD411,
                              kParamSidRegD412, kParamSidRegD413, kParamSidRegD414),
                     ARPSID_S("FILTER / VOLUME", 0.6f, kParamSidRegD415, kParamSidRegD416, kParamSidRegD417,
                              kParamSidRegD418),
                     ARPSID_END}}},
        }}},
        {ArpSIDTab::Sequencer, {{
            {0.7f, {{ARPSID_S("SEQUENCER", 1.f, kParamSeqEnable, kParamSeqTempo, kParamSeqSwing, kParamSeqMode,
                              kParamSeqLength),
                     ARPSID_S("DRUM PERFORMANCE", 1.6f, kParamDrSidKickTune, kParamDrSidKickDecay, kParamDrSidSnareSnap,
                              kParamDrSidSnareTone, kParamDrSidAccentAmount, kParamDrSidOutputDrive, kParamDrSidVolume),
                     ARPSID_END}}},
            {1.6f, {{ARPSID_D("STEPS", Display::SeqSteps, 1.f), ARPSID_END}}},
        }}},
        {ArpSIDTab::DrSid, {{
            {1.0f, {{ARPSID_S("DRSID ENGINE", 1.f, kParamDrSidEnable, kParamDrSidMachineModel, kParamDrSidVolume,
                              kParamDrSidAccentAmount, kParamDrSidOutputDrive, kParamAutoGmDrumPromotion),
                     ARPSID_D("DRUM ACTIVITY", Display::DrumMeters, 1.f), ARPSID_END}}},
            {1.0f, {{ARPSID_S("KICK / SNARE", 1.f, kParamDrSidKickTune, kParamDrSidKickDecay, kParamDrSidSnareTone,
                              kParamDrSidSnareSnap),
                     ARPSID_S("HATS / CLAP", 1.f, kParamDrSidHatTune, kParamDrSidHatDecay, kParamDrSidHatMetal,
                              kParamDrSidClapDecay, kParamDrSidClapSpread),
                     ARPSID_S("TOM / COWBELL", 1.f, kParamDrSidTomTune, kParamDrSidTomDecay, kParamDrSidCowbellTune,
                              kParamDrSidCowbellDecay),
                     ARPSID_END}}},
        }}},
        {ArpSIDTab::Filter, {{
            {1.0f, {{ARPSID_S("FILTER", 1.f, kParamFilterCutoff, kParamFilterResonance, kParamFilterMode,
                              kParamFilterDrive, kParamFilterKeyTrack),
                     ARPSID_D("RESPONSE", Display::FilterResponse, 1.f), ARPSID_END}}},
            {1.0f, {{ARPSID_S("ENV / MOTION", 1.f, kParamFilterEnvAmount, kParamFilterLFOAmount, kParamAttack,
                              kParamDecay, kParamSustain, kParamRelease),
                     ARPSID_S("CHIP / ANALOG", 1.f, kParamSidChipRevision, kParamSidExternalRcEnable,
                              kParamSidOversamplingFactor, kParamSidAdsrBug6581, kParamForensicFilterOhmic),
                     ARPSID_END}}},
            {1.0f, {{ARPSID_D("FILTER IN / OUT", Display::FilterScopes, 1.f), ARPSID_END}}},
        }}},
        {ArpSIDTab::Macro, {{
            {0.8f, {{ARPSID_S("MACROS", 1.f, kParamMacro1, kParamMacro2, kParamMacro3, kParamMacro4, kParamMacro5,
                              kParamMacro6, kParamMacro7, kParamMacro8),
                     ARPSID_D("MODULATION SOURCES", Display::ModMatrixMonitor, 0.6f), ARPSID_END}}},
            {1.2f, {{ARPSID_S("MOD MATRIX", 1.f, kParamModVCFCutoffSource, kParamModVCFCutoffDepth,
                              kParamModVCFResonanceSource, kParamModVCFResonanceDepth, kParamModVCO1FreqSource,
                              kParamModVCO1FreqDepth, kParamModVCO1PWSource, kParamModVCO1PWDepth,
                              kParamModVCO2FreqSource, kParamModVCO2FreqDepth, kParamModVCO2PWSource,
                              kParamModVCO2PWDepth, kParamModVCO3FreqSource, kParamModVCO3FreqDepth,
                              kParamModVCO3PWSource, kParamModVCO3PWDepth, kParamModMasterVolumeSource,
                              kParamModMasterVolumeDepth),
                     ARPSID_END}}},
        }}},
        {ArpSIDTab::Forensic, {{
            {1.0f, {{ARPSID_S("FORENSIC GLOBAL", 1.4f, kParamForensicEnable, kParamForensicIntensity,
                              kParamSidChipRevision, kParamSidExternalRcEnable, kParamSidOversamplingFactor,
                              kParamForensicStartupRandom, kParamForensicDigifix8580, kParamForensicRevision,
                              kParamForensicChipSeed),
                     ARPSID_D("ACTIVITY", Display::ForensicMeters, 0.6f), ARPSID_END}}},
            {1.0f, {{ARPSID_S("CLOCK / SUPPLY", 1.f, kParamForensicTemp, kParamForensicSupply,
                              kParamForensicClockJitterEnable, kParamForensicClockJitter,
                              kParamForensicSupplyRippleEnable, kParamForensicSupplyRipple,
                              kParamForensicThermalDriftEnable, kParamForensicThermalDrift),
                     ARPSID_S("VOICE / FILTER", 0.8f, kParamForensicVoiceCrosstalkEnable, kParamForensicVoiceCrosstalk,
                              kParamForensicExternalBleedEnable, kParamForensicExternalBleed,
                              kParamForensicEnvelopeTDM, kParamForensicFilterOhmic),
                     ARPSID_END}}},
            {1.0f, {{ARPSID_S("ADC / BUS / BOARD", 1.f, kParamForensicD418Asymmetry, kParamForensicSystemNoise,
                              kParamForensicMotherboard, kParamForensicADCBleed, kParamForensicBusCollision,
                              kParamForensicPOTInput),
                     ARPSID_D("VCO SCOPES", Display::VcoScopes, 1.f), ARPSID_END}}},
        }}},
        {ArpSIDTab::SidCore, {{
            {1.0f, {{ARPSID_D("SID REGISTERS", Display::SidRegisters, 1.f),
                     ARPSID_D("VCO SCOPES", Display::VcoScopes, 1.f), ARPSID_END}}},
            {1.0f, {{ARPSID_D("SID BUS TIMELINE", Display::SidCoreTimeline, 1.f), ARPSID_END}}},
        }}},
        {ArpSIDTab::C64, {{
            {0.8f, {{ARPSID_D("SID PLAYER", Display::C64Player, 1.f), ARPSID_END}}},
            {1.4f, {{ARPSID_D("MACHINE", Display::C64Machine, 1.f), ARPSID_END}}},
        }}},
        {ArpSIDTab::HiFi, {{
            {1.0f, {{ARPSID_S("MODE", 1.f, kParamHiFiEnable, kParamHiFiQuality, kParamHiFiOversampling),
                     ARPSID_S("STEREO", 1.f, kParamHiFiMasterWidth, kParamHiFiStereoDepth, kParamHiFiVoiceDiffuser),
                     ARPSID_END}}},
            {1.0f, {{ARPSID_S("COLOUR", 1.f, kParamHiFiAnalogWarmth, kParamHiFiTapeSaturation, kParamHiFiPsychoExciter),
                     ARPSID_D("DELTA MONITOR", Display::HiFiMeters, 1.f), ARPSID_END}}},
        }}},
        {ArpSIDTab::Bank, {{
            {1.0f, {{ARPSID_D("FACTORY BANK", Display::Bank, 1.f), ARPSID_END}}},
        }}},
        {ArpSIDTab::Options, {{
            {1.0f, {{ARPSID_S("SID AUTH / CLOCK", 1.f, kParamSidChipRevision, kParamSidExternalRcEnable,
                              kParamSidOversamplingFactor, kParamSidAdsrBug6581),
                     ARPSID_S("OUTPUT / SAFETY", 1.f, kParamOutputLimiter, kParamLimiterThreshold, kParamLimiterAttack,
                              kParamLimiterRelease, kParamReverbMix),
                     ARPSID_D("PERFORMANCE", Display::Actions, 0.5f), ARPSID_END}}},
            {1.0f, {{ARPSID_S("DRSID LOW END / KIT", 1.f, kParamDrSidEnable, kParamDrSidMachineModel, kParamDrSidVolume,
                              kParamDrSidAccentAmount, kParamDrSidKickTune, kParamDrSidKickDecay, kParamDrSidTomTune,
                              kParamDrSidTomDecay, kParamDrSidHatMetal, kParamDrSidClapSpread, kParamDrSidOutputDrive),
                     ARPSID_END}}},
            {1.0f, {{ARPSID_S("FORENSIC / BOARD", 1.f, kParamForensicEnable, kParamForensicStartupRandom,
                              kParamForensicDigifix8580, kParamForensicRevision, kParamForensicChipSeed,
                              kParamForensicIntensity),
                     ARPSID_END}}},
        }}},
        {ArpSIDTab::Settings, {{
            {1.0f, {{ARPSID_D("SETTINGS", Display::Settings, 1.f), ARPSID_END}}},
        }}},
        {ArpSIDTab::Mix, {{
            {1.0f, {{ARPSID_D("MIXER", Display::Mix, 1.f), ARPSID_END}}},
        }}},
        {ArpSIDTab::Kit, {{
            {1.0f, {{ARPSID_D("KIT", Display::Kit, 1.f), ARPSID_END}}},
        }}},
        {ArpSIDTab::Digi, {{
            {1.0f, {{ARPSID_D("DIGI", Display::Digi, 1.f), ARPSID_END}}},
        }}},
    }};
    return t;
}

#undef ARPSID_P
#undef ARPSID_D
#undef ARPSID_S
#undef ARPSID_SD
#undef ARPSID_END

// Parameters edited by a display rather than a plain control: the STEPS grid
// edits the 32 sequencer steps (note, velocity, gate per step).
inline bool displayEditsParam(Display d, int id) noexcept {
    if (d == Display::SeqSteps)
        return id >= static_cast<int>(kParamSeqStep1Note) && id <= static_cast<int>(kParamSeqStep32Gate);
    return false;
}

// Index (0..16, production tab order) of the first tab that shows a control
// for the parameter, or -1 for parameters without one (see
// isEditorExemptParam). The VST3 controller uses it to group parameters into
// one unit per tab.
inline int tabIndexForParam(int id) noexcept {
    const auto& all = tabs();
    for (std::size_t t = 0; t < all.size(); ++t)
        for (const auto& row : all[t].rows) {
            if (row.heightWeight <= 0.f) break;
            for (const auto& sec : row.sections) {
                if (!sec.title) break;
                if (displayEditsParam(sec.display, id)) return static_cast<int>(t);
                for (int p : sec.params) {
                    if (p == kEnd) break;
                    if (p == id) return static_cast<int>(t);
                }
            }
        }
    return -1;
}

// Parameters the editor intentionally has no control for: host MIDI
// controller mirrors (driven by incoming MIDI), read-only telemetry, the
// program/bank selectors (the preset browser drives them) and legacy mirrors.
inline bool isEditorExemptParam(int id) noexcept {
    if (id >= static_cast<int>(kParamHostCtrlModWheelBase) && id <= static_cast<int>(kParamHostCtrlLast))
        return true;
    switch (id) {
        case kParamSidModel: case kParamSidClockSystem:            // legacy mirrors
        case kParamProgram: case kParamBankSlot: case kParamBankCommand:
        case kParamPanic: case kParamVirtualNote: case kParamVirtualGate:
        case kParamSidRegD419: case kParamSidRegD41A:               // POTX, POTY (read-only)
        case kParamSidRegD41B: case kParamSidRegD41C:               // OSC3, ENV3 (read-only)
        case kParamMidiActivity: case kParamLastNote: case kParamSampleRateRO:
        case kParamBufferSizeRO: case kParamActiveVoicesRO:
            return true;
        default:
            return false;
    }
}

} // namespace ArpSID::GUI::EditorLayout
