// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID full telemetry snapshot shared by AUv2, AUv3, VST3 and standalone.
// This POD is presentation transport only; render engines remain the authority.
//
// ABI/layout contract: fields are only ever APPENDED so existing prefix
// offsets stay stable for same-version modules. This is NOT a cross-version
// ABI guarantee — all binary components (plugin wrappers, GUI bridges,
// standalone) must be rebuilt together whenever this struct changes.
#pragma once

#include <cstdint>
#include <type_traits>

typedef struct {
    uint64_t token;
    uint8_t note;
    uint8_t channel;
    uint8_t flags;
    float velocity;
    float polyPressure;
} ArpSIDTokenTelemetry;

// v909 telemetry-truth closure: which backend (if any) receives applied SID
// projection writes for the C64/SID cockpit mirror. Consumers must not imply
// mirror accuracy when the backend is unavailable.
enum {
    kArpSIDProjectionMirrorBackendUnavailable       = 0, // no mirror sink exists
    kArpSIDProjectionMirrorBackendAU3Kernel         = 1, // AU3 kernel C64 telemetry mirror
    kArpSIDProjectionMirrorBackendUnavailablePhase2 = 2, // VST3/Phase2: mirror intentionally not owned
};

typedef struct {
    uint64_t telemetryFrameId;
    uint64_t scopeFrameId;
    uint64_t digiFrameId;
    uint64_t c64FrameId;
    uint64_t mainOscFrameId;
    uint64_t hostSampleStart;
    uint64_t hostSampleEnd;

    float peakL;
    float peakR;
    float rmsL;
    float rmsR;
    int activeVoices;
    int arpStep;
    int lastMidiNote;
    uint8_t sidRegs[30];
    double hostTempo;
    double hostBeat;
    bool hostPlaying;
    int renderMode;
    int sidModel;
    int programNumber;
    int bankSlot;
    int voiceMode;
    bool synthMode;
    bool drSidMode;
    bool psidActive;
    int drSidPlaybackMode;
    bool arpEnabled;
    bool arpFollowHost;
    bool seqEnabled;
    bool seqFollowHost;
    int seqStep;
    float seqTempoBpm;

    float drumVolume;
    float drumMachineModel;
    float drumAccentAmount;
    float drumOutputDrive;
    float drumHatMetal;
    float drumClapSpread;
    float drumKickTune;
    float drumKickDecay;
    float drumSnareTone;
    float drumSnareSnap;
    float drumHatTune;
    float drumHatDecay;
    float drumCowbellTune;
    float drumCowbellDecay;
    float drumTomTune;
    float drumTomDecay;
    float drumLevel[8];
    float drumVoiceLevel[3];
    float voiceEnvLevel[3];
    float gmDrumNoteLevel[47];
    int lastDrumNote;
    int lastDrumClass;
    float lastDrumVelocity;
    int sid808ConfiguredKit;
    uint64_t sid808RoutedHitCount;
    int sid808LastRoutedDrumClass;
    int sid808LastRoutedMidiNote;
    float sid808LastRoutedVelocity;
    float sid808OutputPeak;
    int sid808ActiveVoiceCount;
    uint64_t sid808SilentActiveBlockCount;
    uint64_t sid808ZeroPeakWithActiveVoiceCount;
    bool sid808SilentActiveSinceLastHit;
    float sid808RawPeakBeforeDc;
    float sid808RawMeanBeforeDc;
    float sid808PostDcPeak;
    float sid808PostDcMean;
    float sid808DcBlockerR;
    uint64_t sid808DcBlockerResetCount;
    float sid808LastSnareSnapPeak;
    float sid808LastSnareSnapRms;
    float sid808LastSnareBodyPeak;
    float sid808LastSnareBodyRms;
    uint64_t sid808SnareMicroStageAppliedCount;
    uint64_t sid808SnareMicroStageLateCount;
    bool sid808BridgeContextActive;
    bool sid808BridgeReplacedOutput;

    int digiActiveSlots;
    int digiConfiguredFactorySlots;
    int digiConfiguredUserImportSlots;
    int digiPlayingVoices;
    int digiStep;
    int digiLastSlot;
    int digiLastFactorySlot;
    uint32_t digiTriggerCount;
    uint32_t digiMidiTriggerCount;
    uint32_t digiMidiIgnoredCount;
    int digiLastMidiNote;
    int digiLastMidiChannel;
    int digiMidiRootNote;
    int digiMidiChannelFilter;
    uint32_t digiGuiPadAcceptedCount;
    uint32_t digiGuiPadIgnoredCount;
    int digiGuiPadLastSlot;
    int digiGuiPadLastVelocity;
    bool digiGuiPadLastAccepted;
    uint32_t digiUnavailableUserImports;
    float digiOutputPeak;
    float digiScope[128];
    uint32_t digiScopeWritePos;
    uint32_t digiD418WriteCount;
    uint32_t digiD418WritesThisBlock;
    uint32_t digiD418SidAcceptedWriteCount;
    uint32_t digiD418SidAcceptedWritesThisBlock;
    uint32_t digiD418WritesBlockedByIo;
    uint32_t digiD418WriteQueueOverflow;
    uint32_t digiD418CollisionCount;
    uint32_t digiD418OpenBusDriveCount;
    uint32_t digiD418TimelineDiscontinuityResetCount;
    uint32_t digiD418ForensicWritePos;
    uint32_t digiD418LastHostFrame;
    uint32_t digiD418LastPhi2Low;
    uint8_t digiAuthMode;
    uint8_t digiD418LastNibble;
    uint8_t digiD418LastOldD418;
    uint8_t digiD418LastD418;
    uint8_t digiD418LastOpenBus;
    uint8_t digiD418LastIoVisible;
    uint8_t digiD418LastSidAccepted;

    float mainOscScope[512];
    uint32_t mainOscScopeCount;
    float voiceScope[8][256];
    float vcoScope[3][256];
    float filterScopeIn[256];
    float filterScopeOut[256];
    uint8_t vcoActiveMask;

    float lfoValue[4];
    float lfoPhase[4];
    float env1Level;
    float lastNoteVelocity;
    float modWheelNorm;
    float focusedPitchBend;
    float focusedChannelPressure;
    float focusedPolyPressure;
    float randomValue;
    float forensicActivity;
    float forensicIntensity;
    bool forensicEnabled;
    float forensicClockJitter;
    float forensicSupplyRipple;
    float forensicThermalDrift;
    float forensicVoiceCrosstalk;
    float forensicExternalBleed;
    float forensicFilterOhmic;
    float forensicSystemNoise;
    float forensicD418Asymmetry;
    float forensicEnvelopeTDM;
    float forensicMotherboard;
    float forensicADCBleed;
    float forensicBusCollision;
    float forensicPotInput;
    bool forensicDigifix8580;
    bool hifiEnabled;
    int hifiQuality;
    int hifiOversampling;
    float hifiDryPeak;
    float hifiWetPeak;
    float hifiDeltaPeak;
    float hifiMonoCorrelation;
    float hifiSafetyGain;

    bool c64PlatformEnabled;
    bool c64MirrorEnabled;
    bool c64PsidRuntimeActive;
    bool c64ProjectionOnly;
    bool c64Pal;
    bool c64VideoFromFile;
    bool c64SidModelFromFile;
    uint8_t c64SidModel;
    uint8_t c64PsidLastParseResult;
    uint8_t c64PsidLastLoadFailure;
    bool c64RealtimeRunning;
    bool c64Booted;
    bool c64BootColdComplete;
    bool c64SidImageLoaded;
    bool c64SidInitBootstrapInstalled;
    bool c64SidInitDispatched;
    bool c64SidInitCompleted;
    bool c64SidPlayReady;
    bool c64SidPlayBootstrapInstalled;
    bool c64SidPlayDispatched;
    bool c64SidPlayCompleted;
    uint16_t c64SidBootstrapAddress;
    uint16_t c64SidPlayBootstrapAddress;
    uint32_t c64SidInitInstructionsExecuted;
    uint32_t c64SidPlayInstructionsExecuted;
    uint64_t c64SidInitStartPhi2;
    uint64_t c64SidInitEndPhi2;
    uint64_t c64SidPlayStartPhi2;
    uint64_t c64SidPlayEndPhi2;
    uint32_t c64SidPlayCallCount;
    uint64_t c64Phi2Cycle;
    uint64_t c64BlockIndex;
    uint32_t c64PlayCalls;
    float c64PlayRateHz;
    uint64_t c64HeavyTelemetryHostSampleCursor;
    uint64_t c64HeavyTelemetryNextSample;
    uint64_t c64HeavyTelemetryLastBlock;
    uint64_t c64HeavyTelemetryLastAudioPhaseSample;
    uint64_t c64HeavyTelemetryPeriodSamples;
    uint64_t c64HeavyTelemetrySkippedSnapshotCount;
    uint64_t c64HeavyTelemetryForcedSnapshotCount;
    uint64_t c64HeavyTelemetryCatchupClampCount;
    bool c64HeavyTelemetryDemandGated;
    bool c64ScalarTelemetryEveryBlock;
    uint8_t c64BusScopeDecimation;
    uint8_t c64BusScopeSourceLen;
    uint8_t c64BusScopeSnapshotLen;
    uint16_t c64CpuPc;
    uint8_t c64CpuA;
    uint8_t c64CpuX;
    uint8_t c64CpuY;
    uint8_t c64CpuSp;
    uint8_t c64CpuStatus;
    bool c64CpuJammed;
    bool c64IrqLine;
    bool c64NmiLine;
    bool c64TrapBrkAsJam;
    uint8_t c64ProcessorPort;
    uint16_t c64VicRaster;
    uint8_t c64VicCycle;
    bool c64VicBadline;
    bool c64VicBa;
    bool c64VicAec;
    bool c64VicSpriteDma;
    bool c64VicIrq;
    uint8_t c64VicHalfCycle;
    uint8_t c64VicMemoryBank;
    uint8_t c64VicActiveSpriteMask;
    uint64_t c64VicFrame;
    uint32_t c64VicTotalStolen;
    uint16_t c64VicFetchBase;
    uint16_t c64VicLastFetchAddress;
    uint8_t c64OpenBus;
    uint8_t c64OpenBusDecayMask;
    bool c64OpenBusDrivenWithinPersistence;
    uint64_t c64OpenBusAgePhi2;
    uint64_t c64OpenBusLastDrivenPhi2;
    uint32_t c64SidOpenBusReadCount;
    uint32_t c64ColorRamOpenBusReadCount;
    uint32_t c64PotxyOpenBusReadCount;
    uint8_t c64LastRead;
    uint8_t c64LastSidReg;
    uint8_t c64LastSidValue;
    uint64_t c64LastSidWriteCycle;
    float c64OpenBusScope[128];
    float c64SidBusScope[128];
    float c64SidRegScope[128];
    float c64SidValueScope[128];
    float c64SidWritePulseScope[128];
    float c64Phi2Scope[128];
    float c64IrqDmaScope[128];
    float c64ChipScope[128];
    uint32_t c64BusScopeWritePos;
    uint16_t c64MemoryWindowBase[8];
    uint8_t c64MemoryWindow[8][64];
    uint32_t c64MemoryWindowHash[8];
    uint8_t c64MemoryWindowChangedBytes[8];
    uint64_t c64MemoryWindowChangedByteMask[8];
    uint8_t c64MemoryWindowFirstChangedOffset[8];
    uint8_t c64MemoryWindowLastChangedOffset[8];
    uint32_t c64MemoryWindowDirtyMask;
    uint32_t c64MemoryWindowChangedMask;
    uint32_t c64MemoryCombinedHash;
    uint8_t c64Cia1Irq;
    uint8_t c64Cia2Irq;
    uint8_t c64CiaIrqMask[2];
    bool c64Cia1IrqLine;
    bool c64Cia2IrqLine;
    uint16_t c64CiaTimerA[2];
    uint16_t c64CiaTimerB[2];
    uint16_t c64CiaLatchA[2];
    uint16_t c64CiaLatchB[2];
    uint64_t c64CiaTimerAUnderflows[2];
    uint64_t c64CiaTimerBUnderflows[2];
    uint64_t c64CiaIrqEdges[2];
    uint64_t c64CiaCntRisingEdges[2];
    uint8_t c64CiaTod[2][4];
    uint8_t c64CiaTodAlarm[2][4];
    uint8_t c64CiaSerialBitsRemaining[2];
    bool c64CiaTimerAReloadPending[2];
    bool c64CiaTimerBReloadPending[2];
    bool c64CiaTimerAJustUnderflowed[2];
    bool c64CiaTimerBJustUnderflowed[2];
    bool c64CiaFlagLatched[2];
    bool c64CiaTodLatched[2];
    bool c64CiaTodStopped[2];
    bool c64CiaTodAlarmWriteMode[2];
    bool c64CiaSerialSelfClock[2];
    bool c64IecAtn;
    bool c64IecClk;
    bool c64IecData;
    bool c64IecSrq;
    bool c64TapeMotor;
    bool c64TapeSense;
    bool c64TapeWrite;
    bool c64TapeRead;
    uint64_t c64TapePulseCount;
    char psidTitle[33];
    char psidAuthor[33];
    char psidReleased[33];
    uint16_t psidSongs;
    uint16_t psidCurrentSubtune;
    uint16_t psidLoadAddress;
    uint16_t psidInitAddress;
    uint16_t psidPlayAddress;
    bool psidCiaIrqObserved;
    bool psidCiaCpuIrqLineObserved;
    bool psidCiaVectorEntered;
    bool psidCiaPlayAddressEntered;
    bool psidCiaAckObserved;
    uint64_t psidCiaTicksToIrq;
    uint64_t psidCiaTicksToVector;
    uint64_t psidCiaTicksToPlay;
    uint64_t psidCiaRunGeneration;
    uint64_t psidCiaServiceGeneration;
    uint16_t psidCiaIdleLoopAddress;
    uint8_t c64SidChipCount;
    uint16_t c64SidBase[5];
    bool c64ExternalKernalRom;
    bool c64ExternalBasicRom;
    bool c64ExternalCharacterRom;
    bool c64ExternalCompleteRomSet;
    uint32_t c64KernalRomChecksum;
    uint32_t c64BasicRomChecksum;
    uint32_t c64CharacterRomChecksum;
    uint16_t c64DisasmPc[3];
    uint8_t c64DisasmBytes[3][3];
    uint8_t c64DisasmByteCount[3];
    char c64DisasmLine[3][64];
    char c64DisasmText[3][32];
    uint16_t c64DisasmCallStack[8];
    uint8_t c64DebugEventCount;
    uint32_t c64DebugEventDropped;
    uint64_t c64DebugEventSequence;
    uint8_t c64DebugEventKind[16];
    uint8_t c64DebugEventA[16];
    uint8_t c64DebugEventB[16];
    uint8_t c64DebugEventC[16];
    uint16_t c64DebugEventAddress[16];
    uint32_t c64DebugEventValue[16];
    uint64_t c64DebugEventCycle[16];

    float mpkKnobValue[8];
    int lastMappedCC;
    float lastMappedCCValue;
    uint8_t activeTokenCount;
    uint8_t totalActiveTokenCount;
    ArpSIDTokenTelemetry tokens[8];

    // v909 telemetry-truth closure (appended to keep layout compatibility):
    // - projectionMirrorAvailable/projectionMirrorBackend: whether applied SID
    //   projection writes reach a C64/SID telemetry mirror sink. VST3/Phase2
    //   publishes an explicit "unavailable" instead of silently no-oping.
    // - noOutputBusActive: this block rendered into scratch because the host
    //   provided no valid output bus; the engine/FX state still advanced.
    // - telemetryRepresentsHostOutput: meters/scope were captured from the
    //   buffers actually handed to the host (false = scratch-render capture).
    bool projectionMirrorAvailable;
    uint8_t projectionMirrorBackend;
    bool noOutputBusActive;
    bool telemetryRepresentsHostOutput;
} ArpSIDTelemetry;

static_assert(std::is_trivially_copyable<ArpSIDTelemetry>::value,
              "ArpSIDTelemetry must remain POD-compatible for lock-free snapshots");
