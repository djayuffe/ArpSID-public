// ArpSIDAudioUnit.h
// ArpSID AUv3 — AUAudioUnit Subclass
//
// Registered component type: aumu / ASID / ArpS
// Capabilities: Instrument, MIDI input, stereo output, full parameter tree,
// canonical full-state save/restore, Logic Pro ready.
//
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT

#pragma once

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#ifdef __cplusplus
#include <atomic>
#include <cstdint>

struct ArpSIDHostTransportSnapshotAccess {
    std::atomic<uint64_t>* _Nullable generation = nullptr;
    std::atomic<double>* _Nullable bpm = nullptr;
    std::atomic<double>* _Nullable beat = nullptr;
    std::atomic<double>* _Nullable sampleRate = nullptr;
    std::atomic<double>* _Nullable loopStart = nullptr;
    std::atomic<double>* _Nullable loopEnd = nullptr;
    std::atomic<int>* _Nullable frameCount = nullptr;
    std::atomic<uint32_t>* _Nullable flags = nullptr;

    bool valid() const noexcept {
        return generation && bpm && beat && sampleRate &&
               loopStart && loopEnd && frameCount && flags;
    }
};
#endif

NS_ASSUME_NONNULL_BEGIN

/// Main AUv3 plug-in class. The .appex extension entry point instantiates this
/// class via the AudioComponentBundle / NSExtensionPrincipalClass chain.
/// Logic Pro and other AUv3 hosts create and manage its lifetime.
@interface ArpSIDAudioUnit : AUAudioUnit

/// Read-only: sample rate currently configured by the host.
@property (nonatomic, readonly) double currentSampleRate;

/// Convenience: retrieve a normalized [0..1] parameter value by its integer ID.
- (float)getParameterValue:(int)paramID;
- (void)injectMIDIBytes:(const uint8_t*)data length:(uint32_t)length;

/// Convenience: set a parameter value from any thread (thread-safe).
- (void)setParameterValue:(float)value forID:(int)paramID;
- (id)debugAdapter;
- (void)writeSIDRegister:(uint8_t)regIndex value:(uint8_t)value;
- (void)performC64ControlHubCommand:(NSInteger)command;
- (BOOL)loadSidFileData:(NSData*)data;
- (BOOL)loadSidFileData:(NSData*)data subtune:(uint16_t)subtune;
- (void)unloadSidFile;
- (void)resetC64SidPlayerForEject;
- (void)prepareStandaloneWithSampleRate:(double)sampleRate maxFrames:(AVAudioFrameCount)maxFrames;
- (void)refreshHostTransportSnapshotForFrameCount:(AUAudioFrameCount)frames;

/// Factory preset list — the AUParameterTree preset mechanism uses this.
- (NSArray<AUAudioUnitPreset*>*)factoryPresets;

- (BOOL)applyUserFactoryPresetNumber:(NSInteger)slot;
- (BOOL)setCurrentFactoryPresetMetadataOnlyForSlot:(NSInteger)slot;
- (BOOL)setRestoredFactoryPresetMetadataOnlyForSlot:(NSInteger)slot;
- (NSInteger)stickyFactoryPresetNumber;
- (BOOL)hasStickyUserFactoryPresetSelection;
- (NSInteger)componentFlavor;

#ifdef __cplusplus
- (ArpSIDHostTransportSnapshotAccess)hostTransportSnapshotAccess;
#endif

@end

NS_ASSUME_NONNULL_END
