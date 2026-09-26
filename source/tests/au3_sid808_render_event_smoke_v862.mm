// Copyright (C) 2024-2026 Ulf Bertilsson
// au3_sid808_render_event_smoke_v862.mm
// Direct AUAudioUnit render-block smoke for dedicated SID-808 event ingress.

#import "../au3/ArpSIDAudioUnit.h"

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMIDI/CoreMIDI.h>
#import <Foundation/Foundation.h>

#include "../parameter_ids.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static constexpr OSType kType = 'aumu';
static constexpr OSType kSubtype = 'S808';
static constexpr OSType kManufacturer = 'ASID';
static constexpr double kSampleRate = 44100.0;
static constexpr AVAudioFrameCount kMaxFrames = 512;
static constexpr double kMinEnergy = 1.0e-10;

#define CHECK_TRUE(expr, msg) \
    do { \
        if (!(expr)) { \
            std::fprintf(stderr, "FAIL [%s]\n", msg); \
            return 1; \
        } \
        std::printf("OK   [%s]\n", msg); \
    } while (0)

#define CHECK_STATUS(expr, msg) \
    do { \
        const AUAudioUnitStatus _s = (expr); \
        if (_s != noErr) { \
            std::fprintf(stderr, "FAIL [%s]: status %d\n", msg, (int)_s); \
            return 1; \
        } \
        std::printf("OK   [%s]\n", msg); \
    } while (0)

static AudioBufferList* makePlanarAbl(std::vector<float>& left, std::vector<float>& right) {
    AudioBufferList* abl = static_cast<AudioBufferList*>(
        std::calloc(1, offsetof(AudioBufferList, mBuffers) + 2 * sizeof(AudioBuffer)));
    if (!abl) return nullptr;
    abl->mNumberBuffers = 2;
    abl->mBuffers[0].mNumberChannels = 1;
    abl->mBuffers[0].mDataByteSize = kMaxFrames * sizeof(float);
    abl->mBuffers[0].mData = left.data();
    abl->mBuffers[1].mNumberChannels = 1;
    abl->mBuffers[1].mDataByteSize = kMaxFrames * sizeof(float);
    abl->mBuffers[1].mData = right.data();
    return abl;
}

static double renderEnergy(AUInternalRenderBlock renderBlock,
                           AudioBufferList* abl,
                           std::vector<float>& left,
                           std::vector<float>& right,
                           AudioTimeStamp& ts,
                           const AURenderEvent* firstEvent,
                           int blocks) {
    double energy = 0.0;
    for (int block = 0; block < blocks; ++block) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        AudioUnitRenderActionFlags flags = 0;
        const AURenderEvent* events = (block == 0) ? firstEvent : nullptr;
        const AUAudioUnitStatus s = renderBlock(&flags, &ts, kMaxFrames, 0, abl, events, nullptr);
        if (s != noErr) {
            std::fprintf(stderr, "FAIL [render block %d]: status %d\n", block, (int)s);
            return -1.0;
        }
        for (AVAudioFrameCount i = 0; i < kMaxFrames; ++i) {
            energy += (double)left[i] * (double)left[i] + (double)right[i] * (double)right[i];
        }
        ts.mSampleTime += kMaxFrames;
    }
    return energy;
}

static AURenderEvent makeClassicMidiNoteEvent(UInt8 status, UInt8 note) {
    AURenderEvent ev{};
    ev.MIDI.next = nullptr;
    ev.MIDI.eventSampleTime = 0;
    ev.MIDI.eventType = AURenderEventMIDI;
    ev.MIDI.reserved = 0;
    ev.MIDI.length = 3;
    ev.MIDI.cable = 0;
    ev.MIDI.data[0] = status;
    ev.MIDI.data[1] = note;
    ev.MIDI.data[2] = 118;
    return ev;
}

static AURenderEvent makeMidi2UmpNoteEvent(UInt8 note) {
    AURenderEvent ev{};
    ev.MIDIEventsList.next = nullptr;
    ev.MIDIEventsList.eventSampleTime = 0;
    ev.MIDIEventsList.eventType = AURenderEventMIDIEventList;
    ev.MIDIEventsList.reserved = 0;
    ev.MIDIEventsList.cable = 0;
    MIDIEventPacket* pkt = MIDIEventListInit(&ev.MIDIEventsList.eventList, kMIDIProtocol_2_0);
    const UInt32 words[2] = {
        (UInt32)(0x40900000u | ((UInt32)(note & 0x7Fu) << 8u)),
        0xEC000000u  // 16-bit velocity in the high half-word.
    };
    pkt = MIDIEventListAdd(&ev.MIDIEventsList.eventList,
                           sizeof(ev.MIDIEventsList.eventList),
                           pkt,
                           0,
                           2,
                           words);
    if (!pkt) {
        ev.MIDIEventsList.eventList.numPackets = 0;
    }
    return ev;
}

static int renderAndAssert(AUInternalRenderBlock renderBlock,
                           AudioBufferList* abl,
                           std::vector<float>& left,
                           std::vector<float>& right,
                           AudioTimeStamp& ts,
                           AURenderEvent& ev,
                           const char* label) {
    const double energy = renderEnergy(renderBlock, abl, left, right, ts, &ev, 12);
    if (energy < 0.0) return 1;
    std::printf("INFO [%s energy]: %.12e\n", label, energy);
    CHECK_TRUE(energy > kMinEnergy, label);
    (void)renderEnergy(renderBlock, abl, left, right, ts, nullptr, 4);
    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        std::printf("=== ArpSID AUAudioUnit SID-808 Render Event Smoke ===\n");

        AudioComponentDescription desc{};
        desc.componentType = kType;
        desc.componentSubType = kSubtype;
        desc.componentManufacturer = kManufacturer;

        NSError* error = nil;
        ArpSIDAudioUnit* au = [[ArpSIDAudioUnit alloc] initWithComponentDescription:desc
                                                                            options:0
                                                                              error:&error];
        CHECK_TRUE(au != nil && error == nil, "init ArpSIDAudioUnit S808");
        CHECK_TRUE([au componentFlavor] == 3, "component flavor is Sid808");

        AUAudioUnitPreset* preset = [[AUAudioUnitPreset alloc] init];
        preset.number = 120;
        preset.name = @"SID-808 Classic Kit";
        au.currentPreset = preset;

        AVAudioFormat* format = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                                                  sampleRate:kSampleRate
                                                                    channels:2
                                                                 interleaved:NO];
        CHECK_TRUE(format != nil, "create output format");
        NSError* busError = nil;
        CHECK_TRUE([[au.outputBusses objectAtIndexedSubscript:0] setFormat:format error:&busError],
                   "set output bus format");
        au.maximumFramesToRender = kMaxFrames;
        NSError* allocError = nil;
        CHECK_TRUE([au allocateRenderResourcesAndReturnError:&allocError],
                   "allocate render resources");

        AUInternalRenderBlock renderBlock = au.internalRenderBlock;
        CHECK_TRUE(renderBlock != nil, "internalRenderBlock exists");

        std::vector<float> left(kMaxFrames, 0.0f);
        std::vector<float> right(kMaxFrames, 0.0f);
        AudioBufferList* abl = makePlanarAbl(left, right);
        CHECK_TRUE(abl != nullptr, "allocate render buffers");

        AudioTimeStamp ts{};
        ts.mSampleTime = 0.0;
        ts.mFlags = kAudioTimeStampSampleTimeValid;

        (void)renderEnergy(renderBlock, abl, left, right, ts, nullptr, 2);

        AURenderEvent classic = makeClassicMidiNoteEvent(0x90u, 36);
        if (renderAndAssert(renderBlock, abl, left, right, ts, classic,
                            "AURenderEventMIDI SID808 ch1 kick") != 0) {
            std::free(abl);
            [au deallocateRenderResources];
            return 1;
        }

        AURenderEvent classicNote60 = makeClassicMidiNoteEvent(0x90u, 60);
        if (renderAndAssert(renderBlock, abl, left, right, ts, classicNote60,
                            "AURenderEventMIDI SID808 ch1 note 60") != 0) {
            std::free(abl);
            [au deallocateRenderResources];
            return 1;
        }

        AURenderEvent midi2 = makeMidi2UmpNoteEvent(36);
        CHECK_TRUE(midi2.MIDIEventsList.eventList.numPackets == 1,
                   "build MIDI 2.0 UMP event list");
        if (renderAndAssert(renderBlock, abl, left, right, ts, midi2,
                            "AURenderEventMIDIEventList MIDI 2.0 SID808 kick") != 0) {
            std::free(abl);
            [au deallocateRenderResources];
            return 1;
        }

        std::free(abl);
        [au deallocateRenderResources];
        std::printf("ArpSID AUAudioUnit SID-808 render-event smoke PASS\n");
        return 0;
    }
}
