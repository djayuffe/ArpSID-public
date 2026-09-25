// ArpSIDDSPKernelAdapter.h
// ArpSID AUv3 — Objective-C++ Bridge between DSP Kernel and AUAudioUnit
//
// This class is the only component that knows about both the C++ DSP kernel
// (ArpSIDDSPKernel.hpp) and the Objective-C AUv3 runtime types.
// It runs the kernel on the render thread and forwards parameter/MIDI
// messages safely across thread boundaries.
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#pragma once

#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#include "common/arpsid_telemetry_snapshot.h"

#ifdef __cplusplus
#include <atomic>
#include <cstdint>
namespace ArpSID { class ArpSIDDSPKernel; }
#endif

NS_ASSUME_NONNULL_BEGIN

@interface ArpSIDDSPKernelAdapter : NSObject

/// Prepare the kernel for a given sample rate and maximum frame count.
- (void)setupWithSampleRate:(double)sampleRate maxFrames:(int)maxFrames;

/// Apply a CoreAudio configuration change without making factory slot/default
/// state audible authority. Used by AUv3/AUv2 allocation and stream-format
/// changes where the selected preset/kit and live parameters must survive.
- (void)setupPreservingAudioStateWithSampleRate:(double)sampleRate maxFrames:(int)maxFrames;

/// Called when the AU is deactivated — resets all engine state.
- (void)reset;

/// Set a normalized [0,1] parameter value from ANY thread.
- (void)setParameterID:(int)paramID value:(float)value;

/// Get the current normalized value for a parameter (any thread).
- (float)getParameterID:(int)paramID;

/// Publish sticky preset display identity without mutating DSP parameter mirrors.
- (void)setStickyPresetDisplaySlot:(NSInteger)slot;

/// Restore a complete normalized parameter snapshot after host reset without
/// re-entering preset loading. Non-realtime only.
- (void)restoreParameterSnapshot:(const float*)values count:(int)count;

/// Publish the owning AU flavor so the kernel can clamp mode policy consistently.
- (void)setComponentFlavor:(NSInteger)flavor;

/// UI/control-hub command surface for the render-owned C64/SID projection runtime.
/// command: 1=BOOT, 2=START, 3=STOP, 4=RESET, 5=LOAD PROJ BOOTSTRAP.
- (void)performC64ControlHubCommand:(NSInteger)command;

/// v838: live "VIC-II fast" CPU-saving toggle (default off = full cycle accuracy).
- (void)setC64VicFast:(BOOL)on;
- (BOOL)c64VicFast;

/// v839: live "6510 fast" toggle (default off). CPU stays bit-exact; skips the
/// per-cycle diagnostics snapshot only. Measured ~6% less PHI2 CPU.
- (void)setC64CpuFast:(BOOL)on;
- (BOOL)c64CpuFast;

/// Reset transient engine state while preserving the currently audible factory
/// preset and live host parameter image. This is the AUv2/Logic transport-start
/// reset path: it must not let default slot 0 become audio authority just
/// because the host calls AudioUnitReset. Non-realtime only.
- (void)resetPreservingParameterSnapshot:(const float*)values
                                   count:(int)count
                             factorySlot:(NSInteger)factorySlot
                  hasExplicitFactorySlot:(BOOL)hasExplicitFactorySlot;

/// Process one render block.
/// outputBufferList MUST have at least 1 bus with 1 or 2 channels.
- (void)processWithOutputBufferList:(AudioBufferList*)outputBufferList
                         frameCount:(AVAudioFrameCount)frameCount
                          timestamp:(const AudioTimeStamp*)timestamp;

/// Handle a MIDI note-on event (render thread).
- (void)handleNoteOn:(uint8_t)note channel:(uint8_t)channel velocity:(uint8_t)velocity;

/// Handle a MIDI note-off event (render thread).
- (void)handleNoteOff:(uint8_t)note channel:(uint8_t)channel velocity:(uint8_t)velocity;

/// Handle a MIDI control-change event (render thread).
- (void)handleCC:(uint8_t)cc channel:(uint8_t)channel value:(uint8_t)value;

/// Handle a MIDI pitch-bend event (render thread).
/// bendValue: raw 14-bit combined value (0..16383, 8192 = centre).
- (void)handlePitchBend:(uint16_t)bendValue channel:(uint8_t)channel;

/// Handle channel aftertouch (render thread).
- (void)handleAftertouch:(uint8_t)pressure channel:(uint8_t)channel;

/// Send an all-notes-off / panic.
- (void)allNotesOff;

/// Enqueue a raw MIDI message into the kernel ingress ring.
/// Returns NO when the message is invalid or the ring is full.
- (BOOL)enqueueMIDIBytes:(const uint8_t*)data length:(uint32_t)length hostTime:(uint64_t)hostTime;

#ifdef __cplusplus
/// Direct C++ kernel access for AU render-block hot path.
- (ArpSID::ArpSIDDSPKernel*)kernelPtr;
#endif

@end

NS_ASSUME_NONNULL_END

NS_ASSUME_NONNULL_BEGIN

@interface ArpSIDDSPKernelAdapter (Telemetry)

/// Fill *out with the latest atomic telemetry snapshot (any thread).
- (void)readTelemetry:(ArpSIDTelemetry*)out;

/// Fill *out with the latest atomic telemetry snapshot, optionally skipping
/// scope-buffer copies when the current editor tab does not need them.
- (void)readTelemetry:(ArpSIDTelemetry*)out includeScopes:(BOOL)includeScopes;

/// Fill *out with the latest atomic telemetry snapshot, optionally skipping
/// scope-buffer copies and C64 chip/disassembly snapshots for light GUI polls.
- (void)readTelemetry:(ArpSIDTelemetry*)out includeScopes:(BOOL)includeScopes includeC64Snapshot:(BOOL)includeC64Snapshot;

/// Copy the most recent oscilloscope samples into buf (any thread).
/// buf must have room for at least maxSamples floats.
/// Returns the number of samples written.
- (int)readOscilloscope:(float*)buf maxSamples:(int)maxSamples;

/// Copy per-VCO scope snapshots from the bitperfect engine.
/// vco0/1/2: each must point to a 256-float buffer, or NULL to skip.
- (void)readVCOScope:(float*)vco0 vco1:(float*)vco1 vco2:(float*)vco2;

@end  // ArpSIDDSPKernelAdapter (Telemetry)

@interface ArpSIDDSPKernelAdapter (C64SidPlayerLoad)
- (BOOL)loadSidFileData:(NSData*)data;
- (BOOL)loadSidFileData:(NSData*)data subtune:(uint16_t)subtune;
- (void)unloadSidFile;
- (void)resetC64SidPlayerForEject;
@end  // ArpSIDDSPKernelAdapter (C64SidPlayerLoad)

NS_ASSUME_NONNULL_END

// ─── Diagnostic Counter Bridge (v549) ────────────────────────────────────────
// readDiagnosticCounters: — GUI thread only, fills a 23-field POD snapshot.
// storeAuv2DiagCounters: — render thread, RT-safe (atomic stores only).
#ifdef __cplusplus
#include "arpsid/gui/diagnostic_snapshot.h"

namespace ArpSID::GUI {
struct Auv3ScratchCounterAtomicTargets {
    std::atomic<uint64_t>* _Nullable renderScratchEpoch = nullptr;
    std::atomic<uint64_t>* _Nullable scratchUnderCapacity = nullptr;
};
struct Auv2DiagnosticCounterAtomicTargets {
    std::atomic<uint64_t>* _Nullable renderEpoch = nullptr;
    std::atomic<uint64_t>* _Nullable notifyCallbackViolationCount = nullptr;
    std::atomic<uint64_t>* _Nullable renderDrainTimeoutCount = nullptr;
    std::atomic<uint64_t>* _Nullable splitBrainDiagnosticCount = nullptr;
    std::atomic<uint64_t>* _Nullable scratchUnderCapacityCountAuv2 = nullptr;
    std::atomic<uint64_t>* _Nullable preNotifyFailureCount = nullptr;
    std::atomic<uint64_t>* _Nullable hostBufferScratchAttachCount = nullptr;
    std::atomic<uint64_t>* _Nullable bridgeDivertedRenderCount = nullptr;
    std::atomic<uint64_t>* _Nullable activityMutexRtViolations = nullptr;
    std::atomic<uint64_t>* _Nullable stateMutexRtViolations = nullptr;
    std::atomic<uint64_t>* _Nullable propListenerMutexRtViolations = nullptr;
    std::atomic<uint64_t>* _Nullable closeWaitMutexRtViolations = nullptr;
};
}

NS_ASSUME_NONNULL_BEGIN

@interface ArpSIDDSPKernelAdapter (DiagnosticCounters)

/// Fill *out with all 23 audit-stabilization counters.
/// Kernel-accessible counters are pulled via collectDiagnosticCounters();
/// AUv2/AUv3 host-layer counters are overlaid from per-render-block pushes.
/// Call on the GUI/main thread only.
- (void)readDiagnosticCounters:(ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot*)out;

/// Push the 11 AUv2 host-layer counter fields from *partial into the
/// adapter's atomic storage. Called from the AUv2 render thread each block.
/// RT-safe: only performs std::atomic::store(relaxed) operations.
/// Only the AUv2 fields (renderEpoch…closeWaitMutexRtViolations) are read.
- (void)storeAuv2DiagCounters:(const ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot*)partial;

/// Push the 2 AUv3 scratch counter fields into the adapter's atomic storage.
/// Called from the AUv3 render block each block.
/// RT-safe: only performs std::atomic::store(relaxed) operations.
- (void)storeAuv3ScratchEpoch:(uint64_t)epoch underCapacity:(uint64_t)underCapacity;

/// Capture RT-safe raw atomic targets at render-block creation time.
- (ArpSID::GUI::Auv3ScratchCounterAtomicTargets)auv3ScratchCounterAtomicTargets;

/// Capture RT-safe raw AUv2 diagnostic atomic targets outside render.
- (ArpSID::GUI::Auv2DiagnosticCounterAtomicTargets)auv2DiagnosticCounterAtomicTargets;

@end  // ArpSIDDSPKernelAdapter (DiagnosticCounters)

NS_ASSUME_NONNULL_END

// ─── SIDCORE Panel Model Bridge (v550) ───────────────────────────────────────
// setSidCorePanelModel: — GUI/main thread only. Wires (or unwires) the kernel's
// register-write publish path to the ViewController's _sidCoreModel_v547_ so
// the ArpSIDSidCoreTimelineView_v547 receives live SID register events.
#include "arpsid/gui/sidcore_panel_model.h"

NS_ASSUME_NONNULL_BEGIN


// ─── Pure 1:1 SID engine output mode ───────────────────────────────
// GUI/main thread safe setter/getter. The kernel stores the flag atomically and
// the render path observes it at block boundary without allocating or locking.
@interface ArpSIDDSPKernelAdapter (PureSid1Q1Output)
- (void)setPureSid1Q1OutputMode:(BOOL)enabled;
- (BOOL)pureSid1Q1OutputMode;
/// record the AU's own pre-post-FX pure SID engine stream as a DIGI REC source.
- (BOOL)startPureSid1Q1RecordCapture:(NSUInteger)maxFrames;
- (BOOL)copyAndStopPureSid1Q1RecordCapture:(float*)dst
                                  maxFrames:(NSUInteger)maxFrames
                                 frameCount:(NSUInteger*)frameCount
                                 sampleRate:(double*)sampleRate
                              droppedFrames:(NSUInteger*)droppedFrames;
- (BOOL)pureSid1Q1RecordCaptureStatusFrames:(NSUInteger*)frameCount
                                  sampleRate:(double*)sampleRate
                               droppedFrames:(NSUInteger*)droppedFrames
                                        peak:(float*)peak
                                         rms:(float*)rms;
@end

@interface ArpSIDDSPKernelAdapter (SidCorePanel)

/// Wire the SIDCORE panel model to the kernel's register-write publish path.
/// Pass nullptr to unwire (e.g. on AU teardown).
/// GUI/main thread only. The kernel will see the new pointer on the next
/// processBlock boundary (atomic release/acquire).
- (void)setSidCorePanelModel:(ArpSID::GUI::SidCorePanelModel* _Nullable)model;

@end  // ArpSIDDSPKernelAdapter (SidCorePanel)

NS_ASSUME_NONNULL_END

// ─── Settings Persistence Bridge (v551) ──────────────────────────────────────
// getSettingsModel: / setSettingsModel: — GUI/main thread only.
// Allows the ViewController to push user-changed settings into the adapter
// and the AUv3/AUv2 state-save path to persist them as a 32-byte blob in
// the AU state dictionary under @"ArpSIDSettings_v1".
#include "arpsid/gui/settings_panel_model.h"

NS_ASSUME_NONNULL_BEGIN

@interface ArpSIDDSPKernelAdapter (SettingsPersistence)

/// Copy the current settings into *out. GUI/main thread only.
- (void)getSettingsModel:(ArpSID::GUI::SettingsPanelModel*)out;

/// Store a sanitized copy of *m. GUI/main thread only.
- (void)setSettingsModel:(const ArpSID::GUI::SettingsPanelModel*)m;

@end  // ArpSIDDSPKernelAdapter (SettingsPersistence)

NS_ASSUME_NONNULL_END

// ─── DIGI State Persistence Bridge (v565/v596) ───────────────────────────────
// GUI/main thread only. New code must use the atomic model+sample-bank
// selectors below so DIGI handles and imported clips are read/written as one
// matched persistence unit. The legacy split selectors are fail-closed
// compatibility stubs: split writes are no-ops and split reads return default
// empty blobs so old callers cannot observe or serialize half of the pair.
#include "arpsid/gui/digi_panel_model.h"
#include "arpsid/gui/digi_sample_bank_v596.h"

NS_ASSUME_NONNULL_BEGIN

@interface ArpSIDDSPKernelAdapter (DigiStatePersistence)

/// Legacy split read: compatibility only; fail-closed default. Use atomic pair API.
- (void)getDigiModel:(ArpSID::GUI::DigiPanelModel*)out;

/// Legacy split write: compatibility only; fail-closed no-op. Use atomic pair API.
- (void)setDigiModel:(const ArpSID::GUI::DigiPanelModel*)m;

/// Legacy split read: compatibility only; fail-closed default. Use atomic pair API.
- (void)getDigiSampleBank:(ArpSID::GUI::DigiSampleBankBlob*)out;

/// Copy a matched DIGI model + sample bank snapshot. GUI/main thread only.
- (void)getDigiModel:(ArpSID::GUI::DigiPanelModel*)model
          sampleBank:(ArpSID::GUI::DigiSampleBankBlob*)bank;

/// Legacy split write: compatibility only; fail-closed no-op. Use atomic pair API.
- (void)setDigiSampleBank:(const ArpSID::GUI::DigiSampleBankBlob*)bank;

/// Store a sanitized DIGI model and sample bank in one kernel publication. GUI/main thread only.
- (void)setDigiModel:(const ArpSID::GUI::DigiPanelModel*)model
          sampleBank:(const ArpSID::GUI::DigiSampleBankBlob*)bank;

/// Quantize a decoded mono float buffer into the saved sample bank and point slot at it.
- (BOOL)setDigiUserSampleForSlot:(NSInteger)slot
                         samples:(const float*)samples
                      frameCount:(NSUInteger)frameCount
                      sampleRate:(double)sampleRate
                            name:(NSString* _Nullable)name;

@end  // ArpSIDDSPKernelAdapter (DigiStatePersistence)

@interface ArpSIDDSPKernelAdapter (DigiD418RuntimePolicyPass104)

/// Set runtime DIGI render policy. Release clamps to 0=AUTH C64-bus D418 or 1=FAST private D418; 2=LegacyFloatLayer is accepted only in ARPSID_ENABLE_LEGACY_FLOAT_DIGI debug builds.
- (void)setDigiD418RuntimeMode:(uint8_t)mode rateHz:(uint32_t)rateHz;

/// Read runtime DIGI render policy for GUI initialization.
- (void)getDigiD418RuntimeMode:(uint8_t*)mode rateHz:(uint32_t*)rateHz;

/// Clear DIGI $D418 HUD/counter telemetry without mutating audio state. GUI/main thread only.
- (void)clearDigiD418RuntimeTelemetry;

/// Queue a GUI audition pad trigger for the next render block. GUI/main thread only; RT consumption is atomic.
- (void)triggerDigiPadSlot:(uint8_t)slot velocity:(uint8_t)velocity;

/// Configure MIDI pad mapping for DIGI. rootNote maps to slot 0; channelFilter 0..15 or 16=OMNI.
- (void)setDigiMidiPadRootNote:(uint8_t)rootNote channelFilter:(uint8_t)channelFilter;

/// Read MIDI pad mapping for DIGI. channelFilter returns 16 for OMNI.
- (void)getDigiMidiPadRootNote:(uint8_t*)rootNote channelFilter:(uint8_t*)channelFilter;

@end  // ArpSIDDSPKernelAdapter (DigiD418RuntimePolicyPass104)

NS_ASSUME_NONNULL_END

// ─── MIX State Persistence Bridge (v561) ─────────────────────────────────────
// getMixModel: / setMixModel: — GUI/main thread only.
// Allows the ViewController to push user-changed MIX state into the adapter
// and the AUv3/AUv2 state-save path to persist it as a 1264-byte blob under
// @"ArpSIDMix_v561" in the AU state dictionary.
#include "arpsid/gui/mix_panel_model.h"

NS_ASSUME_NONNULL_BEGIN

@interface ArpSIDDSPKernelAdapter (MixStatePersistence)

/// Copy the current MIX model into *out. GUI/main thread only.
- (void)getMixModel:(ArpSID::GUI::MixPanelModel*)out;

/// Store a sanitized copy of *m. GUI/main thread only.
- (void)setMixModel:(const ArpSID::GUI::MixPanelModel*)m;

@end  // ArpSIDDSPKernelAdapter (MixStatePersistence)

NS_ASSUME_NONNULL_END

// ─── KIT State Persistence Bridge (v560) ─────────────────────────────────────
// getKitStateBlob: / setKitStateBlob: — GUI/main thread only.
// Allows the ViewController to push the four KIT data models into the adapter
// and the AUv3/AUv2 state-save path to persist them as a 1388-byte blob under
// @"ArpSIDKit_v560" in the AU state dictionary.
#include "arpsid/gui/kit_state_blob.h"

NS_ASSUME_NONNULL_BEGIN

@interface ArpSIDDSPKernelAdapter (KitStatePersistence)

/// Copy the current KIT state blob into *out. GUI/main thread only.
- (void)getKitStateBlob:(ArpSID::GUI::KitStateBlob*)out;

/// Store a sanitized copy of *b. GUI/main thread only.
- (void)setKitStateBlob:(const ArpSID::GUI::KitStateBlob*)b;

@end  // ArpSIDDSPKernelAdapter (KitStatePersistence)

NS_ASSUME_NONNULL_END
#endif // __cplusplus
