// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — kernel -> ArpSIDTelemetry projection shared by every wrapper.
//
// Single authority for turning an ArpSIDDSPKernel telemetry frame into the
// sanitized ArpSIDTelemetry the GUIs consume. Used by the AU kernel adapter
// and by the VST3 kernel host, so AUv2/AUv3/Standalone and VST3 editors show
// the same meters, scopes and C64 state.
#pragma once

#include "ArpSIDDSPKernel.hpp"
#include "../common/arpsid_telemetry_snapshot.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ArpSID::TelemetryFill {

// Copies a fixed-size, possibly unterminated char array into a C string
// field, stopping at the first NUL and always terminating the destination.
template <std::size_t N, typename Src>
inline void arpsidCopyFixedString(char (&dst)[N], const Src& src) {
    static_assert(N > 0, "destination must hold the terminator");
    const std::size_t limit = std::min<std::size_t>(src.size(), N - 1u);
    std::size_t n = 0;
    while (n < limit && src[n] != '\0') ++n;
    if (n > 0) std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

inline float ArpSIDSanitizeUnitFloat(float v, int pid = -1) noexcept {
    return ArpSID::sanitizeNormalizedParamValue(pid, v, ArpSID::defaultNormalizedParamValue(pid));
}
inline double ArpSIDSanitizeFiniteDouble(double v, double fallback = 0.0) noexcept {
    return std::isfinite(v) ? v : fallback;
}
inline int ArpSIDSanitizeRangeInt(int v, int lo, int hi, int fallback) noexcept {
    if (v < lo || v > hi) return fallback;
    return v;
}
inline float ArpSIDSanitizeScopeSample(float v) noexcept {
    if (!std::isfinite(v)) return 0.0f;
    return std::clamp(v, -1.25f, 1.25f);
}
inline float ArpSIDSanitizeBipolarFloat(float v, float fallback = 0.0f) noexcept {
    if (!std::isfinite(v)) return fallback;
    return std::clamp(v, -1.0f, 1.0f);
}

// Fills *out from the kernel. includeScopes / includeC64Snapshot are the GUI
// demand signals: heavy scope and C64 snapshot work only runs while a view
// that shows them is polling.
inline void fillTelemetryFromKernel(ArpSID::ArpSIDDSPKernel& k,
                                    ArpSIDTelemetry* out,
                                    bool includeScopes,
                                    bool includeC64Snapshot) noexcept {
    if (!out) return;
    std::memset(out, 0, sizeof(ArpSIDTelemetry));
    out->lastMidiNote = -1;
    out->hostTempo    = 120.0;

    // Visible C64 panes are the demand signal for heavy C64/PSID snapshot work.
    // The audio thread keeps scalar meters fresh always, but only builds disassembly/
    // chip snapshots while a C64-facing GUI view is actively polling.
    if (includeC64Snapshot) {
        k.noteC64TelemetryRequest();
    }
    if (includeScopes) {
        k.notePresentationScopeRequest();
    }
    const auto t = k.readTelemetry(includeScopes);
    out->telemetryFrameId = t.telemetryFrameId;
    out->digiFrameId = t.digiFrameId;
    out->c64FrameId = t.c64BlockIndex;
    out->mainOscFrameId = t.mainOscFrameId;
    out->hostSampleStart = t.hostSampleStart;
    out->hostSampleEnd = t.hostSampleEnd;
    out->peakL        = ArpSIDSanitizeUnitFloat(t.peakL);
    out->peakR        = ArpSIDSanitizeUnitFloat(t.peakR);
    out->rmsL         = ArpSIDSanitizeUnitFloat(t.rmsL);
    out->rmsR         = ArpSIDSanitizeUnitFloat(t.rmsR);
    out->activeVoices = ArpSIDSanitizeRangeInt(t.activeVoices, 0, 8, 0);
    out->arpStep      = std::max(0, t.arpStep);
    out->lastMidiNote = ArpSIDSanitizeRangeInt(t.lastMidiNote, -1, 127, -1);
    out->hostTempo    = ArpSIDSanitizeFiniteDouble(t.hostTempo, 120.0);
    out->hostBeat     = std::max(0.0, ArpSIDSanitizeFiniteDouble(t.hostBeat, 0.0));
    out->hostPlaying  = t.hostPlaying;
    out->renderMode   = ArpSIDSanitizeRangeInt(t.renderMode, 0, 3, 0);
    out->sidModel     = ArpSIDSanitizeRangeInt(t.sidModel, 0, 1, 1);
    out->programNumber = ArpSIDSanitizeRangeInt(t.programNumber, 0, ArpSID::kCanonicalFactoryPatchSlotMax, 0);
    out->bankSlot      = ArpSIDSanitizeRangeInt(t.bankSlot, 0, ArpSID::kCanonicalFactoryPatchSlotMax, 0);
    out->voiceMode     = ArpSIDSanitizeRangeInt(t.voiceMode, 0, 3, 0);
    out->synthMode          = t.synthMode;
    out->drSidMode          = t.drSidMode;
    out->psidActive         = t.psidActive;
    out->drSidPlaybackMode  = t.drSidPlaybackMode;  // B10
    out->arpEnabled   = t.arpEnabled;
    out->arpFollowHost = t.arpFollowHost;
    out->seqEnabled   = t.seqEnabled;
    out->seqFollowHost = t.seqFollowHost;
    out->seqStep      = std::max(0, t.seqStep);
    out->seqTempoBpm  = std::clamp(std::isfinite(t.seqTempoBpm) ? t.seqTempoBpm : 120.0f, 20.0f, 400.0f);
    out->drumVolume = ArpSIDSanitizeUnitFloat(t.drumVolume, ArpSID::kParamDrSidVolume);
    out->drumMachineModel = ArpSIDSanitizeUnitFloat(t.drumMachineModel, ArpSID::kParamDrSidMachineModel);
    out->drumAccentAmount = ArpSIDSanitizeUnitFloat(t.drumAccentAmount, ArpSID::kParamDrSidAccentAmount);
    out->drumOutputDrive = ArpSIDSanitizeUnitFloat(t.drumOutputDrive, ArpSID::kParamDrSidOutputDrive);
    out->drumHatMetal = ArpSIDSanitizeUnitFloat(t.drumHatMetal, ArpSID::kParamDrSidHatMetal);
    out->drumClapSpread = ArpSIDSanitizeUnitFloat(t.drumClapSpread, ArpSID::kParamDrSidClapSpread);
    out->drumKickTune = ArpSIDSanitizeUnitFloat(t.drumKickTune, ArpSID::kParamDrSidKickTune);
    out->drumKickDecay = ArpSIDSanitizeUnitFloat(t.drumKickDecay, ArpSID::kParamDrSidKickDecay);
    out->drumSnareTone = ArpSIDSanitizeUnitFloat(t.drumSnareTone, ArpSID::kParamDrSidSnareTone);
    out->drumSnareSnap = ArpSIDSanitizeUnitFloat(t.drumSnareSnap, ArpSID::kParamDrSidSnareSnap);
    out->drumHatTune = ArpSIDSanitizeUnitFloat(t.drumHatTune, ArpSID::kParamDrSidHatTune);
    out->drumHatDecay = ArpSIDSanitizeUnitFloat(t.drumHatDecay, ArpSID::kParamDrSidHatDecay);
    out->drumCowbellTune = ArpSIDSanitizeUnitFloat(t.drumCowbellTune, ArpSID::kParamDrSidCowbellTune);
    out->drumCowbellDecay = ArpSIDSanitizeUnitFloat(t.drumCowbellDecay, ArpSID::kParamDrSidCowbellDecay);
    out->drumTomTune = ArpSIDSanitizeUnitFloat(t.drumTomTune, ArpSID::kParamDrSidTomTune);
    out->drumTomDecay = ArpSIDSanitizeUnitFloat(t.drumTomDecay, ArpSID::kParamDrSidTomDecay);
    for (int i = 0; i < 8; ++i) out->drumLevel[i] = ArpSIDSanitizeUnitFloat(t.drumLevel[i]);
    for (int i = 0; i < 3; ++i) out->drumVoiceLevel[i] = ArpSIDSanitizeUnitFloat(t.drumVoiceLevel[i]);
    for (int i = 0; i < 3; ++i) out->voiceEnvLevel[i] = ArpSIDSanitizeUnitFloat(t.voiceEnvLevel[i]);
    for (int i = 0; i < 47; ++i) out->gmDrumNoteLevel[i] = ArpSIDSanitizeUnitFloat(t.gmDrumNoteLevel[i]);
    out->lastDrumNote = ArpSIDSanitizeRangeInt(t.lastDrumNote, -1, 127, -1);
    out->lastDrumClass = ArpSIDSanitizeRangeInt(t.lastDrumClass, 0, 255, 255);
    out->lastDrumVelocity = ArpSIDSanitizeUnitFloat(t.lastDrumVelocity);
    out->sid808ConfiguredKit = ArpSIDSanitizeRangeInt(t.sid808ConfiguredKit, -1, ArpSID::kCanonicalFactoryPatchSlotMax, -1);
    out->sid808RoutedHitCount = t.sid808RoutedHitCount;
    out->sid808LastRoutedDrumClass = ArpSIDSanitizeRangeInt(t.sid808LastRoutedDrumClass, 0, 255, 255);
    out->sid808LastRoutedMidiNote = ArpSIDSanitizeRangeInt(t.sid808LastRoutedMidiNote, -1, 127, -1);
    out->sid808LastRoutedVelocity = ArpSIDSanitizeUnitFloat(t.sid808LastRoutedVelocity);
    out->sid808OutputPeak = ArpSIDSanitizeUnitFloat(t.sid808OutputPeak);
    out->sid808ActiveVoiceCount = ArpSIDSanitizeRangeInt(t.sid808ActiveVoiceCount, 0, 3, 0);
    out->sid808SilentActiveBlockCount = t.sid808SilentActiveBlockCount;
    out->sid808ZeroPeakWithActiveVoiceCount = t.sid808ZeroPeakWithActiveVoiceCount;
    out->sid808SilentActiveSinceLastHit = t.sid808SilentActiveSinceLastHit;
    out->sid808RawPeakBeforeDc = ArpSIDSanitizeUnitFloat(t.sid808RawPeakBeforeDc);
    out->sid808RawMeanBeforeDc = ArpSIDSanitizeBipolarFloat(t.sid808RawMeanBeforeDc);
    out->sid808PostDcPeak = ArpSIDSanitizeUnitFloat(t.sid808PostDcPeak);
    out->sid808PostDcMean = ArpSIDSanitizeBipolarFloat(t.sid808PostDcMean);
    out->sid808DcBlockerR = ArpSIDSanitizeUnitFloat(t.sid808DcBlockerR);
    out->sid808DcBlockerResetCount = t.sid808DcBlockerResetCount;
    out->sid808LastSnareSnapPeak = ArpSIDSanitizeUnitFloat(t.sid808LastSnareSnapPeak);
    out->sid808LastSnareSnapRms = ArpSIDSanitizeUnitFloat(t.sid808LastSnareSnapRms);
    out->sid808LastSnareBodyPeak = ArpSIDSanitizeUnitFloat(t.sid808LastSnareBodyPeak);
    out->sid808LastSnareBodyRms = ArpSIDSanitizeUnitFloat(t.sid808LastSnareBodyRms);
    out->sid808SnareMicroStageAppliedCount = t.sid808SnareMicroStageAppliedCount;
    out->sid808SnareMicroStageLateCount = t.sid808SnareMicroStageLateCount;
    out->sid808BridgeContextActive = t.sid808BridgeContextActive;
    out->sid808BridgeReplacedOutput = t.sid808BridgeReplacedOutput;
    out->digiActiveSlots = ArpSIDSanitizeRangeInt(t.digiActiveSlots, 0, 8, 0);
    out->digiConfiguredFactorySlots = ArpSIDSanitizeRangeInt(t.digiConfiguredFactorySlots, 0, 8, 0);
    out->digiConfiguredUserImportSlots = ArpSIDSanitizeRangeInt(t.digiConfiguredUserImportSlots, 0, 8, 0);
    out->digiPlayingVoices = ArpSIDSanitizeRangeInt(t.digiPlayingVoices, 0, 8, 0);
    out->digiStep = ArpSIDSanitizeRangeInt(t.digiStep, 0, 31, 0);
    out->digiLastSlot = ArpSIDSanitizeRangeInt(t.digiLastSlot, 0, 255, 255);
    out->digiLastFactorySlot = ArpSIDSanitizeRangeInt(t.digiLastFactorySlot, 0, 65535, 0);
    out->digiTriggerCount = t.digiTriggerCount;
    out->digiMidiTriggerCount = t.digiMidiTriggerCount;
    out->digiMidiIgnoredCount = t.digiMidiIgnoredCount;
    out->digiLastMidiNote = t.digiLastMidiNote;
    out->digiLastMidiChannel = t.digiLastMidiChannel;
    out->digiMidiRootNote = ArpSIDSanitizeRangeInt(t.digiMidiRootNote, 0, 120, 60);
    out->digiMidiChannelFilter = ArpSIDSanitizeRangeInt(t.digiMidiChannelFilter, 0, 16, 16);
    out->digiGuiPadAcceptedCount = t.digiGuiPadAcceptedCount;
    out->digiGuiPadIgnoredCount = t.digiGuiPadIgnoredCount;
    out->digiGuiPadLastSlot = ArpSIDSanitizeRangeInt(t.digiGuiPadLastSlot, -1, 7, -1);
    out->digiGuiPadLastVelocity = ArpSIDSanitizeRangeInt(t.digiGuiPadLastVelocity, 0, 127, 0);
    out->digiGuiPadLastAccepted = t.digiGuiPadLastAccepted ? true : false;
    out->digiUnavailableUserImports = t.digiUnavailableUserImports;
    out->digiOutputPeak = ArpSIDSanitizeUnitFloat(t.digiOutputPeak);
    if (includeScopes) {
        // DIGI engines already publish their circular storage in chronological
        // oldest-to-newest order through copyScope().
        for (int i = 0; i < 128; ++i) out->digiScope[i] = ArpSIDSanitizeScopeSample(t.digiScope[i]);
    }
    out->digiScopeWritePos = 0u;
    // Copy authentic DIGI D418 telemetry into the UI structure. When the
    // legacy sampler is active these values will be zero or 0xFF as
    // appropriate.
    out->digiD418WriteCount = t.digiD418WriteCount;
    out->digiD418WritesThisBlock = t.digiD418WritesThisBlock;
    out->digiD418SidAcceptedWriteCount = t.digiD418SidAcceptedWriteCount;
    out->digiD418SidAcceptedWritesThisBlock = t.digiD418SidAcceptedWritesThisBlock;
    out->digiD418WritesBlockedByIo = t.digiD418WritesBlockedByIo;
    out->digiD418WriteQueueOverflow = t.digiD418WriteQueueOverflow;
    out->digiD418CollisionCount = t.digiD418CollisionCount;
    out->digiD418OpenBusDriveCount = t.digiD418OpenBusDriveCount;
    out->digiD418TimelineDiscontinuityResetCount = t.digiD418TimelineDiscontinuityResetCount;
    out->digiD418ForensicWritePos = t.digiD418ForensicWritePos;
    out->digiD418LastHostFrame = t.digiD418LastHostFrame;
    out->digiD418LastPhi2Low = t.digiD418LastPhi2Low;
    out->digiAuthMode = t.digiAuthMode;
    out->digiD418LastNibble = t.digiD418LastNibble;
    out->digiD418LastOldD418 = t.digiD418LastOldD418;
    out->digiD418LastD418 = t.digiD418LastD418;
    out->digiD418LastOpenBus = t.digiD418LastOpenBus;
    out->digiD418LastIoVisible = t.digiD418LastIoVisible;
    out->digiD418LastSidAccepted = t.digiD418LastSidAccepted;
    out->env1Level    = ArpSIDSanitizeUnitFloat(t.env1Level);
    out->lastNoteVelocity = ArpSIDSanitizeUnitFloat(t.lastNoteVelocity);
    out->modWheelNorm = ArpSIDSanitizeUnitFloat(t.modWheelNorm);
    out->focusedPitchBend = ArpSIDSanitizeBipolarFloat(t.focusedPitchBend);
    out->focusedChannelPressure = ArpSIDSanitizeUnitFloat(t.focusedChannelPressure);
    out->focusedPolyPressure = ArpSIDSanitizeUnitFloat(t.focusedPolyPressure);
    out->randomValue = ArpSIDSanitizeBipolarFloat(t.randomValue);
    out->forensicActivity = ArpSIDSanitizeUnitFloat(t.forensicActivity);
    out->forensicIntensity = ArpSIDSanitizeUnitFloat(t.forensicIntensity);
    out->forensicEnabled = t.forensicEnabled;
    out->forensicClockJitter = ArpSIDSanitizeUnitFloat(t.forensicClockJitter);
    out->forensicSupplyRipple = ArpSIDSanitizeUnitFloat(t.forensicSupplyRipple);
    out->forensicThermalDrift = ArpSIDSanitizeUnitFloat(t.forensicThermalDrift);
    out->forensicVoiceCrosstalk = ArpSIDSanitizeUnitFloat(t.forensicVoiceCrosstalk);
    out->forensicExternalBleed = ArpSIDSanitizeUnitFloat(t.forensicExternalBleed);
    out->forensicFilterOhmic = ArpSIDSanitizeUnitFloat(t.forensicFilterOhmic);
    out->forensicSystemNoise = ArpSIDSanitizeUnitFloat(t.forensicSystemNoise);
    out->forensicD418Asymmetry = ArpSIDSanitizeUnitFloat(t.forensicD418Asymmetry);
    out->forensicEnvelopeTDM = ArpSIDSanitizeUnitFloat(t.forensicEnvelopeTDM);
    out->forensicMotherboard = ArpSIDSanitizeUnitFloat(t.forensicMotherboard);
    out->forensicADCBleed = ArpSIDSanitizeUnitFloat(t.forensicADCBleed);
    out->forensicBusCollision = ArpSIDSanitizeUnitFloat(t.forensicBusCollision);
    out->forensicPotInput = ArpSIDSanitizeUnitFloat(t.forensicPotInput);
    out->forensicDigifix8580 = t.forensicDigifix8580;
    out->hifiEnabled = t.hifiEnabled;
    out->hifiQuality = ArpSIDSanitizeRangeInt(t.hifiQuality, 0, 2, 0);
    out->hifiOversampling = ArpSIDSanitizeRangeInt(t.hifiOversampling, 1, 16, 8);
    out->hifiDryPeak = ArpSIDSanitizeUnitFloat(t.hifiDryPeak);
    out->hifiWetPeak = ArpSIDSanitizeUnitFloat(t.hifiWetPeak);
    out->hifiDeltaPeak = ArpSIDSanitizeUnitFloat(t.hifiDeltaPeak);
    out->hifiMonoCorrelation = ArpSIDSanitizeBipolarFloat(t.hifiMonoCorrelation);
    out->hifiSafetyGain = ArpSIDSanitizeUnitFloat(t.hifiSafetyGain);
    out->c64PlatformEnabled  = t.c64PlatformEnabled;
    out->c64MirrorEnabled = t.c64MirrorEnabled;
    out->c64PsidRuntimeActive = t.c64PsidRuntimeActive;
    // v964 telemetry audit: these two were declared in ArpSIDTelemetry and
    // published by the kernel, but never copied here — the GUI could only say
    // "load failed" while the exact PSID parse/load rejection reason sat unread.
    out->c64PsidLastParseResult = t.c64PsidLastParseResult;
    out->c64PsidLastLoadFailure = t.c64PsidLastLoadFailure;
    out->c64ProjectionOnly = t.c64ProjectionOnly;
    // v909 telemetry-truth closure: the AU3 kernel owns a real applied-write
    // projection mirror sink (runtimeMirrorAppliedProjectionWrite), and its
    // meters are always captured from the host-rendered buffers.
    out->projectionMirrorAvailable = true;
    out->projectionMirrorBackend = kArpSIDProjectionMirrorBackendAU3Kernel;
    out->noOutputBusActive = false;
    out->telemetryRepresentsHostOutput = true;
    out->c64Pal              = t.c64Pal;
    out->c64VideoFromFile    = t.c64VideoFromFile;
    out->c64SidModelFromFile = t.c64SidModelFromFile;
    out->c64SidModel         = t.c64SidModel;
    out->c64RealtimeRunning  = t.c64RealtimeRunning;
    out->c64Booted           = t.c64Booted;
    out->c64Phi2Cycle = t.c64Phi2Cycle;
    out->c64BlockIndex = t.c64BlockIndex;
    out->c64PlayCalls = t.c64PlayCalls;
    out->c64PlayRateHz = t.c64PlayRateHz;
    out->c64CpuPc = t.c64CpuPc;
    out->c64CpuA = t.c64CpuA;
    out->c64CpuX = t.c64CpuX;
    out->c64CpuY = t.c64CpuY;
    out->c64CpuSp = t.c64CpuSp;
    out->c64CpuStatus = t.c64CpuStatus;
    out->c64CpuJammed = t.c64CpuJammed;
    out->c64IrqLine = t.c64IrqLine;
    out->c64NmiLine = t.c64NmiLine;
    out->c64TrapBrkAsJam = t.c64TrapBrkAsJam;
    out->c64ProcessorPort = t.c64ProcessorPort;
    out->c64VicRaster = t.c64VicRaster;
    out->c64VicCycle = t.c64VicCycle;
    out->c64VicBadline = t.c64VicBadline;
    out->c64VicBa = t.c64VicBa;
    out->c64VicAec = t.c64VicAec;
    out->c64VicSpriteDma = t.c64VicSpriteDma;
    out->c64VicHalfCycle = t.c64VicHalfCycle;
    out->c64VicFrame = t.c64VicFrame;
    out->c64VicTotalStolen = t.c64VicTotalStolen;
    out->c64OpenBus = t.c64OpenBus;
    out->c64OpenBusDecayMask = t.c64OpenBusDecayMask;
    out->c64OpenBusDrivenWithinPersistence = t.c64OpenBusDrivenWithinPersistence;
    out->c64OpenBusAgePhi2 = t.c64OpenBusAgePhi2;
    out->c64OpenBusLastDrivenPhi2 = t.c64OpenBusLastDrivenPhi2;
    out->c64SidOpenBusReadCount = t.c64SidOpenBusReadCount;
    out->c64ColorRamOpenBusReadCount = t.c64ColorRamOpenBusReadCount;
    out->c64PotxyOpenBusReadCount = t.c64PotxyOpenBusReadCount;
    out->c64LastRead = t.c64LastRead;
    out->c64LastSidReg = t.c64LastSidReg;
    out->c64LastSidValue = t.c64LastSidValue;
    out->c64LastSidWriteCycle = t.c64LastSidWriteCycle;
    if (includeScopes) {
        const uint32_t wp = t.c64BusScopeWritePos & 127u;
        for (int i = 0; i < 128; ++i) {
            const uint32_t src = (wp + static_cast<uint32_t>(i)) & 127u;
            out->c64OpenBusScope[i] = ArpSIDSanitizeBipolarFloat(t.c64OpenBusScope[src]);
            out->c64SidBusScope[i] = ArpSIDSanitizeBipolarFloat(t.c64SidBusScope[src]);
            out->c64SidRegScope[i] = ArpSIDSanitizeBipolarFloat(t.c64SidRegScope[src]);
            out->c64SidValueScope[i] = ArpSIDSanitizeBipolarFloat(t.c64SidValueScope[src]);
            out->c64SidWritePulseScope[i] = ArpSIDSanitizeBipolarFloat(t.c64SidWritePulseScope[src]);
            out->c64Phi2Scope[i] = ArpSIDSanitizeBipolarFloat(t.c64Phi2Scope[src]);
            out->c64IrqDmaScope[i] = ArpSIDSanitizeBipolarFloat(t.c64IrqDmaScope[src]);
            out->c64ChipScope[i] = ArpSIDSanitizeBipolarFloat(t.c64ChipScope[src]);
        }
    }
    out->c64BusScopeWritePos = 0u;
    out->c64BusScopeDecimation = 1u;
    out->c64BusScopeSourceLen = 128u;
    out->c64BusScopeSnapshotLen = 128u;
    out->c64Cia1Irq = t.c64Cia1Irq;
    out->c64Cia2Irq = t.c64Cia2Irq;
    out->c64Cia1IrqLine = t.c64Cia1IrqLine;
    out->c64Cia2IrqLine = t.c64Cia2IrqLine;
    out->c64IecAtn = t.c64IecAtn;
    out->c64IecClk = t.c64IecClk;
    out->c64IecData = t.c64IecData;
    out->c64IecSrq = t.c64IecSrq;
    out->c64TapeMotor = t.c64TapeMotor;
    out->c64TapeSense = t.c64TapeSense;
    out->c64TapeWrite = t.c64TapeWrite;
    out->c64TapeRead = t.c64TapeRead;
    out->c64TapePulseCount = t.c64TapePulseCount;
    ArpSID::C64::C64ChipSnapshot c64Snapshot{};
    const bool hasC64Snapshot = includeC64Snapshot && k.readC64Telemetry(c64Snapshot);
    if (!hasC64Snapshot) {
        std::strncpy(out->c64DisasmLine[0], "No .sid file loaded -- click Load .sid", sizeof(out->c64DisasmLine[0]) - 1u);
        out->c64DisasmLine[0][sizeof(out->c64DisasmLine[0]) - 1u] = '\0';
        std::strncpy(out->c64DisasmText[0], "IDLE", sizeof(out->c64DisasmText[0]) - 1u);
        out->c64DisasmText[0][sizeof(out->c64DisasmText[0]) - 1u] = '\0';
    }
    if (hasC64Snapshot) {
        out->c64FrameId = c64Snapshot.blockIndex;
        out->c64BootColdComplete = c64Snapshot.bootColdComplete;
        out->c64SidImageLoaded = c64Snapshot.sidImageLoaded;
        out->c64SidInitBootstrapInstalled = c64Snapshot.sidInitBootstrapInstalled;
        out->c64SidInitDispatched = c64Snapshot.sidInitDispatched;
        out->c64SidInitCompleted = c64Snapshot.sidInitCompleted;
        out->c64SidPlayReady = c64Snapshot.sidPlayReady;
        out->c64SidPlayBootstrapInstalled = c64Snapshot.sidPlayBootstrapInstalled;
        out->c64SidPlayDispatched = c64Snapshot.sidPlayDispatched;
        out->c64SidPlayCompleted = c64Snapshot.sidPlayCompleted;
        out->c64SidBootstrapAddress = c64Snapshot.sidBootstrapAddress;
        out->c64SidPlayBootstrapAddress = c64Snapshot.sidPlayBootstrapAddress;
        out->c64SidInitInstructionsExecuted = c64Snapshot.sidInitInstructionsExecuted;
        out->c64SidPlayInstructionsExecuted = c64Snapshot.sidPlayInstructionsExecuted;
        out->c64SidInitStartPhi2 = c64Snapshot.sidInitStartPhi2;
        out->c64SidInitEndPhi2 = c64Snapshot.sidInitEndPhi2;
        out->c64SidPlayStartPhi2 = c64Snapshot.sidPlayStartPhi2;
        out->c64SidPlayEndPhi2 = c64Snapshot.sidPlayEndPhi2;
        out->c64SidPlayCallCount = c64Snapshot.sidPlayCallCount;
        out->c64VicIrq = c64Snapshot.vicIrq;
        out->c64VicMemoryBank = c64Snapshot.vicMemoryBank;
        out->c64VicActiveSpriteMask = c64Snapshot.vicActiveSpriteMask;
        out->c64VicFetchBase = c64Snapshot.vicFetchBase;
        out->c64VicLastFetchAddress = c64Snapshot.vicLastFetchAddress;
        out->c64CiaIrqMask[0] = c64Snapshot.cia1IrqMask;
        out->c64CiaIrqMask[1] = c64Snapshot.cia2IrqMask;
        const ArpSID::C64::CiaTimerPhaseSnapshot phases[2] = {
            c64Snapshot.cia1Phase, c64Snapshot.cia2Phase
        };
        for (int cia = 0; cia < 2; ++cia) {
            const auto& phase = phases[cia];
            out->c64CiaTimerA[cia] = phase.timerA;
            out->c64CiaTimerB[cia] = phase.timerB;
            out->c64CiaLatchA[cia] = phase.latchA;
            out->c64CiaLatchB[cia] = phase.latchB;
            out->c64CiaTimerAUnderflows[cia] = phase.timerAUnderflows;
            out->c64CiaTimerBUnderflows[cia] = phase.timerBUnderflows;
            out->c64CiaIrqEdges[cia] = phase.irqEdges;
            out->c64CiaCntRisingEdges[cia] = phase.cntRisingEdges;
            out->c64CiaTod[cia][0] = phase.todTenths;
            out->c64CiaTod[cia][1] = phase.todSeconds;
            out->c64CiaTod[cia][2] = phase.todMinutes;
            out->c64CiaTod[cia][3] = phase.todHours;
            out->c64CiaTodAlarm[cia][0] = phase.todAlarmTenths;
            out->c64CiaTodAlarm[cia][1] = phase.todAlarmSeconds;
            out->c64CiaTodAlarm[cia][2] = phase.todAlarmMinutes;
            out->c64CiaTodAlarm[cia][3] = phase.todAlarmHours;
            out->c64CiaSerialBitsRemaining[cia] = phase.serialBitsRemaining;
            out->c64CiaTimerAReloadPending[cia] = phase.timerAReloadPending;
            out->c64CiaTimerBReloadPending[cia] = phase.timerBReloadPending;
            out->c64CiaTimerAJustUnderflowed[cia] = phase.timerAJustUnderflowed;
            out->c64CiaTimerBJustUnderflowed[cia] = phase.timerBJustUnderflowed;
            out->c64CiaFlagLatched[cia] = phase.flagLatched;
            out->c64CiaTodLatched[cia] = phase.todLatched;
            out->c64CiaTodStopped[cia] = phase.todStopped;
            out->c64CiaTodAlarmWriteMode[cia] = phase.todAlarmWriteMode;
            out->c64CiaSerialSelfClock[cia] = phase.serialSelfClock;
        }
        for (int i = 0; i < 3; ++i) {
            out->c64DisasmPc[i] = c64Snapshot.disasmPc[(size_t)i];
            out->c64DisasmByteCount[i] = c64Snapshot.disasmByteCount[(size_t)i];
            for (int b = 0; b < 3; ++b) out->c64DisasmBytes[i][b] = c64Snapshot.disasmBytes[(size_t)i][(size_t)b];
            std::strncpy(out->c64DisasmLine[i], c64Snapshot.disasmLine[(size_t)i].data(), sizeof(out->c64DisasmLine[i]) - 1u);
            out->c64DisasmLine[i][sizeof(out->c64DisasmLine[i]) - 1u] = '\0';
            std::strncpy(out->c64DisasmText[i], c64Snapshot.disasmText[(size_t)i].data(), sizeof(out->c64DisasmText[i]) - 1u);
            out->c64DisasmText[i][sizeof(out->c64DisasmText[i]) - 1u] = '\0';
        }
        for (int i = 0; i < 8; ++i) out->c64DisasmCallStack[i] = c64Snapshot.disasmCallStack[(size_t)i];
        out->c64DebugEventCount = c64Snapshot.debugEventCount;
        out->c64DebugEventDropped = c64Snapshot.debugEventDropped;
        out->c64DebugEventSequence = c64Snapshot.debugEventSequence;
        for (int i = 0; i < 16; ++i) {
            out->c64DebugEventKind[i] = c64Snapshot.debugEvents[(size_t)i].kind;
            out->c64DebugEventA[i] = c64Snapshot.debugEvents[(size_t)i].a;
            out->c64DebugEventB[i] = c64Snapshot.debugEvents[(size_t)i].b;
            out->c64DebugEventC[i] = c64Snapshot.debugEvents[(size_t)i].c;
            out->c64DebugEventAddress[i] = c64Snapshot.debugEvents[(size_t)i].address;
            out->c64DebugEventValue[i] = c64Snapshot.debugEvents[(size_t)i].value;
            out->c64DebugEventCycle[i] = c64Snapshot.debugEvents[(size_t)i].cycle;
        }
        for (int w = 0; w < 8; ++w) {
            out->c64MemoryWindowBase[w] = c64Snapshot.memoryWindowBase[(size_t)w];
            out->c64MemoryWindowHash[w] = c64Snapshot.memoryWindowHash[(size_t)w];
            out->c64MemoryWindowChangedBytes[w] = c64Snapshot.memoryWindowChangedBytes[(size_t)w];
            out->c64MemoryWindowChangedByteMask[w] = c64Snapshot.memoryWindowChangedByteMask[(size_t)w];
            out->c64MemoryWindowFirstChangedOffset[w] = c64Snapshot.memoryWindowFirstChangedOffset[(size_t)w];
            out->c64MemoryWindowLastChangedOffset[w] = c64Snapshot.memoryWindowLastChangedOffset[(size_t)w];
            for (int i = 0; i < 64; ++i) out->c64MemoryWindow[w][i] = c64Snapshot.memoryWindow[(size_t)w][(size_t)i];
        }
        out->c64MemoryWindowDirtyMask = c64Snapshot.memoryWindowDirtyMask;
        out->c64MemoryWindowChangedMask = c64Snapshot.memoryWindowChangedMask;
        out->c64MemoryCombinedHash = c64Snapshot.memoryWindowCombinedHash;
        arpsidCopyFixedString(out->psidTitle, c64Snapshot.psidTitle);
        arpsidCopyFixedString(out->psidAuthor, c64Snapshot.psidAuthor);
        arpsidCopyFixedString(out->psidReleased, c64Snapshot.psidReleased);
        out->psidSongs = c64Snapshot.psidSongs;
        out->psidCurrentSubtune = c64Snapshot.psidCurrentSubtune;
        out->psidLoadAddress = c64Snapshot.sidLoadAddress;
        out->psidInitAddress = c64Snapshot.sidInitAddress;
        out->psidPlayAddress = c64Snapshot.sidPlayAddress;
        out->psidCiaIrqObserved = c64Snapshot.psidCiaIrqObserved;
        out->psidCiaCpuIrqLineObserved = c64Snapshot.psidCiaCpuIrqLineObserved;
        out->psidCiaVectorEntered = c64Snapshot.psidCiaVectorEntered;
        out->psidCiaPlayAddressEntered = c64Snapshot.psidCiaPlayAddressEntered;
        out->psidCiaAckObserved = c64Snapshot.psidCiaAckObserved;
        out->psidCiaTicksToIrq = c64Snapshot.psidCiaTicksToIrq;
        out->psidCiaTicksToVector = c64Snapshot.psidCiaTicksToVector;
        out->psidCiaTicksToPlay = c64Snapshot.psidCiaTicksToPlay;
        out->psidCiaRunGeneration = c64Snapshot.psidCiaRunGeneration;
        out->psidCiaServiceGeneration = c64Snapshot.psidCiaServiceGeneration;
        out->psidCiaIdleLoopAddress = c64Snapshot.psidCiaIdleLoopAddress;
        out->c64SidChipCount = c64Snapshot.sidChipCount;
        for (int i = 0; i < 5; ++i) out->c64SidBase[i] = c64Snapshot.sidBase[(size_t)i];
        out->c64HeavyTelemetryHostSampleCursor = c64Snapshot.heavyTelemetryHostSampleCursor;
        out->c64HeavyTelemetryNextSample = c64Snapshot.heavyTelemetryNextSample;
        out->c64HeavyTelemetryLastBlock = c64Snapshot.heavyTelemetryLastBlock;
        out->c64HeavyTelemetryLastAudioPhaseSample = c64Snapshot.heavyTelemetryLastAudioPhaseSample;
        out->c64HeavyTelemetryPeriodSamples = c64Snapshot.heavyTelemetryPeriodSamples;
        out->c64HeavyTelemetrySkippedSnapshotCount = c64Snapshot.heavyTelemetrySkippedSnapshotCount;
        out->c64HeavyTelemetryForcedSnapshotCount = c64Snapshot.heavyTelemetryForcedSnapshotCount;
        out->c64HeavyTelemetryCatchupClampCount = c64Snapshot.heavyTelemetryCatchupClampCount;
        out->c64HeavyTelemetryDemandGated = c64Snapshot.heavyTelemetryDemandGated;
        out->c64ScalarTelemetryEveryBlock = c64Snapshot.scalarTelemetryEveryBlock;
        out->c64BusScopeDecimation = c64Snapshot.busScopeDecimation;
        out->c64BusScopeSourceLen = c64Snapshot.busScopeSourceLen;
        out->c64BusScopeSnapshotLen = c64Snapshot.busScopeSnapshotLen;
        out->c64OpenBusDecayMask = c64Snapshot.openBusDecayMask;
        out->c64OpenBusDrivenWithinPersistence = c64Snapshot.openBusDrivenWithinPersistence;
        out->c64OpenBusAgePhi2 = c64Snapshot.openBusAgePhi2;
        out->c64OpenBusLastDrivenPhi2 = c64Snapshot.openBusLastDrivenPhi2;
        out->c64SidOpenBusReadCount = c64Snapshot.sidOpenBusReadCount;
        out->c64ColorRamOpenBusReadCount = c64Snapshot.colorRamOpenBusReadCount;
        out->c64PotxyOpenBusReadCount = c64Snapshot.potxyOpenBusReadCount;
        out->c64ExternalKernalRom = c64Snapshot.externalKernalRom;
        out->c64ExternalBasicRom = c64Snapshot.externalBasicRom;
        out->c64ExternalCharacterRom = c64Snapshot.externalCharacterRom;
        out->c64ExternalCompleteRomSet = c64Snapshot.externalCompleteRomSet;
        out->c64KernalRomChecksum = c64Snapshot.kernalRomChecksum;
        out->c64BasicRomChecksum = c64Snapshot.basicRomChecksum;
        out->c64CharacterRomChecksum = c64Snapshot.characterRomChecksum;
    }
    for (int i = 0; i < 8; ++i) out->mpkKnobValue[i] = ArpSIDSanitizeUnitFloat(t.mpkKnobValue[i]);
    out->lastMappedCC = ArpSIDSanitizeRangeInt(t.lastMappedCC, -1, 127, -1);
    out->lastMappedCCValue = ArpSIDSanitizeUnitFloat(t.lastMappedCCValue);
    out->activeTokenCount = (uint8_t)std::clamp(t.activeTokenCount, 0, 8);
    out->totalActiveTokenCount = (uint8_t)std::clamp(t.totalActiveTokenCount, 0, 64);
    for (int i = 0; i < 4; ++i) {
        out->lfoValue[i] = ArpSIDSanitizeBipolarFloat(t.lfoValue[i]);
        out->lfoPhase[i] = ArpSIDSanitizeUnitFloat(t.lfoPhase[i]);
    }
    for (int i = 0; i < 8; ++i) {
        out->tokens[i].token = t.tokens[(size_t)i].token;
        out->tokens[i].note = (uint8_t)ArpSIDSanitizeRangeInt(t.tokens[(size_t)i].note, 0, 127, 0);
        out->tokens[i].channel = (uint8_t)ArpSIDSanitizeRangeInt(t.tokens[(size_t)i].channel, 0, 15, 0);
        out->tokens[i].flags = 0u;
        if (t.tokens[(size_t)i].sustained) out->tokens[i].flags |= 0x01u;
        if (t.tokens[(size_t)i].sostenuto) out->tokens[i].flags |= 0x02u;
        if (t.tokens[(size_t)i].focused) out->tokens[i].flags |= 0x04u;
        out->tokens[i].velocity = ArpSIDSanitizeUnitFloat(t.tokens[(size_t)i].velocity);
        out->tokens[i].polyPressure = ArpSIDSanitizeUnitFloat(t.tokens[(size_t)i].polyPressure);
    }
    memcpy(out->sidRegs, t.sidRegs, sizeof(out->sidRegs));

    if (includeScopes) {
        // Scope payload readers are backed by the demand signal sent before
        // readTelemetry(); now the returned payload also gets C64-safe fallback
        // data from the render-published presentation snapshot.
        static const int kScope = 256;
        static thread_local float voiceRaw[8][kScope];
        static thread_local float oscRaw[3][kScope];
        static thread_local float filtRaw[2][kScope];
        uint8_t activeMask = 0;
        uint32_t writePos = 0;
        uint64_t scopeFrameId = 0u;
        k.getPresentationFullScopeSnapshot(
            voiceRaw, oscRaw, filtRaw, activeMask, writePos, scopeFrameId);
        out->scopeFrameId = scopeFrameId;
        const uint32_t wp = writePos & 255u;
        for (int v = 0; v < 8; ++v) {
            for (int i = 0; i < kScope; ++i) {
                out->voiceScope[v][i] = ArpSIDSanitizeScopeSample(voiceRaw[v][(wp + static_cast<uint32_t>(i)) & 255u]);
            }
        }
        for (int v = 0; v < 3; ++v) {
            for (int i = 0; i < kScope; ++i) {
                out->vcoScope[v][i] = ArpSIDSanitizeScopeSample(oscRaw[v][(wp + static_cast<uint32_t>(i)) & 255u]);
            }
        }
        for (int i = 0; i < kScope; ++i) {
            const uint32_t src = (wp + static_cast<uint32_t>(i)) & 255u;
            out->filterScopeIn[i] = ArpSIDSanitizeScopeSample(filtRaw[0][src]);
            out->filterScopeOut[i] = ArpSIDSanitizeScopeSample(filtRaw[1][src]);
        }
        out->vcoActiveMask = static_cast<uint8_t>(activeMask & 0x07u);
        out->mainOscScopeCount = static_cast<uint32_t>(
            k.readOscilloscope(out->mainOscScope, 512));
    }
}

} // namespace ArpSID::TelemetryFill
