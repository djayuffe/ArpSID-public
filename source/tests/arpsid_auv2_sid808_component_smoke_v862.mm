// arpsid_auv2_sid808_component_smoke_v862.mm
// Host-side AUv2 smoke test for the dedicated SID-808 subtype.
//
// This intentionally instantiates aumu/S808/ASID, not the generic ArpS
// subtype, because Logic users load the dedicated SID-808 component.

#import <AudioUnit/AudioUnit.h>
#import <AudioToolbox/AudioToolbox.h>
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
static constexpr UInt32 kMaxFrames = 512;
static constexpr double kMinEnergy = 1.0e-10;

#define CHECK_OS(expr, msg) \
    do { \
        const OSStatus _s = (expr); \
        if (_s != noErr) { \
            std::fprintf(stderr, "FAIL [%s]: OSStatus %d\n", msg, (int)_s); \
            return 1; \
        } \
        std::printf("OK   [%s]\n", msg); \
    } while (0)

#define CHECK_TRUE(expr, msg) \
    do { \
        if (!(expr)) { \
            std::fprintf(stderr, "FAIL [%s]\n", msg); \
            return 1; \
        } \
        std::printf("OK   [%s]\n", msg); \
    } while (0)

static double renderEnergy(AudioUnit au,
                           AudioBufferList* abl,
                           std::vector<float>& left,
                           std::vector<float>& right,
                           AudioTimeStamp& ts,
                           int blocks) {
    double energy = 0.0;
    for (int block = 0; block < blocks; ++block) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        AudioUnitRenderActionFlags flags = 0;
        const OSStatus s = AudioUnitRender(au, &flags, &ts, 0, kMaxFrames, abl);
        if (s != noErr) {
            std::fprintf(stderr, "FAIL [AudioUnitRender block %d]: OSStatus %d\n", block, (int)s);
            return -1.0;
        }
        for (UInt32 i = 0; i < kMaxFrames; ++i) {
            energy += (double)left[i] * (double)left[i] + (double)right[i] * (double)right[i];
        }
        ts.mSampleTime += kMaxFrames;
    }
    return energy;
}

static int renderNote(AudioUnit au,
                      UInt8 status,
                      UInt8 note,
                      const char* label,
                      AudioBufferList* abl,
                      std::vector<float>& left,
                      std::vector<float>& right,
                      AudioTimeStamp& ts) {
    CHECK_OS(MusicDeviceMIDIEvent(au, status, note, 118, 0), label);
    const double energy = renderEnergy(au, abl, left, right, ts, 12);
    if (energy < 0.0) return 1;
    std::printf("INFO [%s energy]: %.12e\n", label, energy);
    CHECK_TRUE(energy > kMinEnergy, label);
    CHECK_OS(MusicDeviceMIDIEvent(au, (UInt8)((status & 0x0Fu) | 0x80u), note, 0, 0),
             "MusicDeviceMIDIEvent SID808 note-off");
    (void)renderEnergy(au, abl, left, right, ts, 4);
    return 0;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    @autoreleasepool {
        std::printf("=== ArpSID AUv2 SID-808 Component Smoke Test ===\n");

        AudioComponentDescription desc{};
        desc.componentType = kType;
        desc.componentSubType = kSubtype;
        desc.componentManufacturer = kManufacturer;

        AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
        CHECK_TRUE(comp != nullptr, "AudioComponentFindNext finds aumu/S808/ASID");

        AudioComponentInstance au = nullptr;
        CHECK_OS(AudioComponentInstanceNew(comp, &au), "AudioComponentInstanceNew SID808");
        CHECK_TRUE(au != nullptr, "SID808 AudioUnit instance is non-null");

        CFArrayRef presets = nullptr;
        UInt32 presetsSize = sizeof(presets);
        CHECK_OS(AudioUnitGetProperty(au,
                                      kAudioUnitProperty_FactoryPresets,
                                      kAudioUnitScope_Global,
                                      0,
                                      &presets,
                                      &presetsSize),
                 "GetProperty FactoryPresets SID808");
        CHECK_TRUE(presets && CFArrayGetCount(presets) >= 30,
                   "SID808 exposes canonical factory kits");
        if (presets) CFRelease(presets);

        const UInt32 maxFrames = kMaxFrames;
        CHECK_OS(AudioUnitSetProperty(au,
                                      kAudioUnitProperty_MaximumFramesPerSlice,
                                      kAudioUnitScope_Global,
                                      0,
                                      &maxFrames,
                                      sizeof(maxFrames)),
                 "SetProperty MaximumFramesPerSlice");

        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = kSampleRate;
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        fmt.mBytesPerPacket = sizeof(float);
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerFrame = sizeof(float);
        fmt.mChannelsPerFrame = 2;
        fmt.mBitsPerChannel = 32;
        CHECK_OS(AudioUnitSetProperty(au,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output,
                                      0,
                                      &fmt,
                                      sizeof(fmt)),
                 "SetProperty StreamFormat stereo float32");

        CHECK_OS(AudioUnitInitialize(au), "AudioUnitInitialize SID808");
        CHECK_OS(AudioUnitReset(au, kAudioUnitScope_Global, 0), "AudioUnitReset SID808");

        AUPreset sid808Preset{};
        sid808Preset.presetNumber = 120;
        sid808Preset.presetName = CFSTR("SID-808 Classic Kit");
        CHECK_OS(AudioUnitSetProperty(au,
                                      kAudioUnitProperty_PresentPreset,
                                      kAudioUnitScope_Global,
                                      0,
                                      &sid808Preset,
                                      sizeof(sid808Preset)),
                 "SetProperty PresentPreset[120] SID808");

        AudioUnitParameterValue drSidEnable = 0.0f;
        CHECK_OS(AudioUnitGetParameter(au,
                                       ArpSID::kParamDrSidEnable,
                                       kAudioUnitScope_Global,
                                       0,
                                       &drSidEnable),
                 "GetParameter kParamDrSidEnable SID808");
        CHECK_TRUE(drSidEnable > 0.5f, "SID808 preset keeps drum mode enabled");

        AudioUnitParameterValue machineModel = 0.0f;
        CHECK_OS(AudioUnitGetParameter(au,
                                       ArpSID::kParamDrSidMachineModel,
                                       kAudioUnitScope_Global,
                                       0,
                                       &machineModel),
                 "GetParameter kParamDrSidMachineModel SID808");
        CHECK_TRUE(machineModel > 0.5f, "SID808 preset keeps SID-808 machine model selected");

        CHECK_OS(AudioUnitSetParameter(au,
                                       ArpSID::kParamSeqEnable,
                                       kAudioUnitScope_Global,
                                       0,
                                       0.0f,
                                       0),
                 "SetParameter kParamSeqEnable=0");

        std::vector<float> left(kMaxFrames, 0.0f);
        std::vector<float> right(kMaxFrames, 0.0f);
        AudioBufferList* abl = static_cast<AudioBufferList*>(
            std::malloc(offsetof(AudioBufferList, mBuffers) + 2 * sizeof(AudioBuffer)));
        CHECK_TRUE(abl != nullptr, "allocate AudioBufferList");
        abl->mNumberBuffers = 2;
        abl->mBuffers[0].mNumberChannels = 1;
        abl->mBuffers[0].mDataByteSize = kMaxFrames * sizeof(float);
        abl->mBuffers[0].mData = left.data();
        abl->mBuffers[1].mNumberChannels = 1;
        abl->mBuffers[1].mDataByteSize = kMaxFrames * sizeof(float);
        abl->mBuffers[1].mData = right.data();

        AudioTimeStamp ts{};
        ts.mSampleTime = 0.0;
        ts.mFlags = kAudioTimeStampSampleTimeValid;

        if (renderNote(au, 0x90u, 36, "MusicDeviceMIDIEvent SID808 ch1 GM kick", abl, left, right, ts) != 0) {
            std::free(abl);
            AudioComponentInstanceDispose(au);
            return 1;
        }
        if (renderNote(au, 0x90u, 60, "MusicDeviceMIDIEvent SID808 ch1 GM note 60", abl, left, right, ts) != 0) {
            std::free(abl);
            AudioComponentInstanceDispose(au);
            return 1;
        }
        if (renderNote(au, 0x99u, 36, "MusicDeviceMIDIEvent SID808 ch10 GM kick", abl, left, right, ts) != 0) {
            std::free(abl);
            AudioComponentInstanceDispose(au);
            return 1;
        }

        std::free(abl);
        CHECK_OS(AudioComponentInstanceDispose(au), "AudioComponentInstanceDispose SID808");
        std::printf("ArpSID AUv2 SID-808 smoke PASS\n");
        return 0;
    }
}
