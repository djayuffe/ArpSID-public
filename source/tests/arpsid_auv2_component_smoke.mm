// arpsid_auv2_component_smoke.mm
// ArpSID AUv2 — Host-side smoke render verification
//
// Tests the installed ArpSID.component through the public C AudioUnit API,
// exactly as a real host would. Exercises: discovery, instantiation, preset
// selection, format negotiation, MIDI note-on/off, multi-block render, and
// energy measurement. Exits 0 on pass, 1 on any failure.
//
// Run AFTER `arpsid_auv2_install_user` has placed the component in:
// ~/Library/Audio/Plug-Ins/Components/ArpSID.component
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#import <AudioUnit/AudioUnit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#import <cstdio>
#import <cmath>
#import <vector>
#import <cstring>
#import <algorithm>
#include "../parameter_ids.h"

// ─── Identity ─────────────────────────────────────────────────────────────────
static constexpr OSType kType         = 'aumu';
static constexpr OSType kSubtype      = 'ArpS';
static constexpr OSType kManufacturer = 'ASID';

// ─── Test parameters ──────────────────────────────────────────────────────────
static constexpr double   kSampleRate       = 44100.0;
static constexpr UInt32   kMaxFrames        = 512;
static constexpr int      kRenderBlockCount = 16;  // blocks after note-on
static constexpr int      kReleaseBlocks    = 8;   // blocks after note-off
static constexpr float    kMinEnergy        = 1e-10f;  // non-silent threshold
static constexpr uint8_t  kTestNote         = 60;  // middle C
static constexpr uint8_t  kTestVelocity     = 100;

#define SMOKE_CHECK(expr, msg) \
    do { \
        OSStatus _s = (expr); \
        if (_s != noErr) { \
            std::fprintf(stderr, "FAIL [%s]: OSStatus %d\n", msg, (int)_s); \
            return 1; \
        } \
        std::printf("OK   [%s]\n", msg); \
    } while(0)

#define SMOKE_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "FAIL [%s]\n", msg); \
            return 1; \
        } \
        std::printf("OK   [%s]\n", msg); \
    } while(0)

static int checkLastRenderErrorClear(AudioUnit au, const char* msg) {
    OSStatus last = 0;
    UInt32 size = sizeof(last);
    const OSStatus s = AudioUnitGetProperty(au,
                                            kAudioUnitProperty_LastRenderError,
                                            kAudioUnitScope_Global,
                                            0,
                                            &last,
                                            &size);
    if (s != noErr) {
        std::fprintf(stderr, "FAIL [%s]: GetProperty LastRenderError OSStatus %d\n", msg, (int)s);
        return 1;
    }
    if (size != sizeof(last) || last != noErr) {
        std::fprintf(stderr, "FAIL [%s]: LastRenderError %d\n", msg, (int)last);
        return 1;
    }
    std::printf("OK   [%s]\n", msg);
    return 0;
}

#define SMOKE_CHECK_LAST_RENDER_CLEAR(au, msg) \
    do { \
        if (checkLastRenderErrorClear((au), (msg)) != 0) return 1; \
    } while(0)

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    @autoreleasepool {

    std::printf("=== ArpSID AUv2 Component Smoke Test ===\n");

    // ── 1. Discovery ──────────────────────────────────────────────────────────
    AudioComponentDescription desc{};
    desc.componentType         = kType;
    desc.componentSubType      = kSubtype;
    desc.componentManufacturer = kManufacturer;
    desc.componentFlags        = 0;
    desc.componentFlagsMask    = 0;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    SMOKE_ASSERT(comp != nullptr,
        "AudioComponentFindNext finds ArpSID AUv2 (ensure arpsid_auv2_install_user ran)");

    // ── 2. Instantiation ──────────────────────────────────────────────────────
    AudioComponentInstance au = nullptr;
    SMOKE_CHECK(AudioComponentInstanceNew(comp, &au),
        "AudioComponentInstanceNew");
    SMOKE_ASSERT(au != nullptr, "AudioUnit instance is non-null");

    // ── 3. Preset availability ────────────────────────────────────────────────
    CFArrayRef presets = nullptr;
    UInt32     presetsSize = sizeof(CFArrayRef);
    SMOKE_CHECK(AudioUnitGetProperty(au,
        kAudioUnitProperty_FactoryPresets,
        kAudioUnitScope_Global, 0,
        &presets, &presetsSize),
        "GetProperty FactoryPresets");
    SMOKE_ASSERT(presets && CFArrayGetCount(presets) > 0,
        "Component has at least one factory preset");
    if (presets) CFRelease(presets);

    UInt32 parameterListSize = 0;
    Boolean parameterListWritable = false;
    SMOKE_CHECK(AudioUnitGetPropertyInfo(au,
        kAudioUnitProperty_ParameterList,
        kAudioUnitScope_Global, 0,
        &parameterListSize, &parameterListWritable),
        "GetPropertyInfo ParameterList");
    SMOKE_ASSERT(parameterListSize >= sizeof(AudioUnitParameterID),
        "ParameterList has at least one host-visible parameter");
    std::vector<AudioUnitParameterID> hostVisibleParams(parameterListSize / sizeof(AudioUnitParameterID));
    SMOKE_CHECK(AudioUnitGetProperty(au,
        kAudioUnitProperty_ParameterList,
        kAudioUnitScope_Global, 0,
        hostVisibleParams.data(), &parameterListSize),
        "GetProperty ParameterList");
    hostVisibleParams.resize(parameterListSize / sizeof(AudioUnitParameterID));
    hostVisibleParams.erase(std::remove_if(hostVisibleParams.begin(),
                                           hostVisibleParams.end(),
                                           [](AudioUnitParameterID pid) {
                                               return pid == (AudioUnitParameterID)ArpSID::kParamProgram ||
                                                      pid == (AudioUnitParameterID)ArpSID::kParamBankSlot;
                                           }),
                            hostVisibleParams.end());
    SMOKE_ASSERT(!hostVisibleParams.empty(),
        "ParameterList has host-writable replay candidates");

    // ── 4. Set master volume parameter ───────────────────────────────────────
    // Parameter 0 is kParamMasterVolume in the ArpSID parameter namespace.
    SMOKE_CHECK(AudioUnitSetParameter(au,
        0, kAudioUnitScope_Global, 0,
        0.8f, 0),
        "SetParameter kParamMasterVolume=0.8");

    // ── 5. Set MaximumFramesPerSlice ──────────────────────────────────────────
    const UInt32 maxFrames = kMaxFrames;
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_MaximumFramesPerSlice,
        kAudioUnitScope_Global, 0,
        &maxFrames, sizeof(maxFrames)),
        "SetProperty MaximumFramesPerSlice");

    // ── 6. Set stereo float output format ────────────────────────────────────
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate       = kSampleRate;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagsNativeFloatPacked |
                            kAudioFormatFlagIsNonInterleaved;
    fmt.mBytesPerPacket   = sizeof(float);
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerFrame    = sizeof(float);
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel   = 32;
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Output, 0,
        &fmt, sizeof(fmt)),
        "SetProperty StreamFormat stereo float32");

    // ── 7. Initialize ─────────────────────────────────────────────────────────
    SMOKE_CHECK(AudioUnitInitialize(au),
        "AudioUnitInitialize");

    // ── 8. Reset ──────────────────────────────────────────────────────────────
    SMOKE_CHECK(AudioUnitReset(au, kAudioUnitScope_Global, 0),
        "AudioUnitReset");
    SMOKE_CHECK_LAST_RENDER_CLEAR(au, "LastRenderError clear after initialize/reset");

    // ── 9. Select first factory preset ───────────────────────────────────────
    AUPreset ap{};
    ap.presetNumber = 0;
    ap.presetName   = CFSTR("Smoke Preset 0");
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_PresentPreset,
        kAudioUnitScope_Global, 0,
        &ap, sizeof(ap)),
        "SetProperty PresentPreset[0]");

    // ── 10. Allocate render buffers ───────────────────────────────────────────
    std::vector<float> bufL(kMaxFrames, 0.0f);
    std::vector<float> bufR(kMaxFrames, 0.0f);

    AudioBufferList* abl = static_cast<AudioBufferList*>(
        std::malloc(offsetof(AudioBufferList, mBuffers) + 2 * sizeof(AudioBuffer)));
    abl->mNumberBuffers          = 2;
    abl->mBuffers[0].mNumberChannels = 1;
    abl->mBuffers[0].mDataByteSize   = kMaxFrames * sizeof(float);
    abl->mBuffers[0].mData           = bufL.data();
    abl->mBuffers[1].mNumberChannels = 1;
    abl->mBuffers[1].mDataByteSize   = kMaxFrames * sizeof(float);
    abl->mBuffers[1].mData           = bufR.data();
    std::vector<float> tinyBufL(1, 0.0f);
    std::vector<float> tinyBufR(1, 0.0f);
    AudioBufferList* tinyAbl = static_cast<AudioBufferList*>(
        std::malloc(offsetof(AudioBufferList, mBuffers) + 2 * sizeof(AudioBuffer)));
    tinyAbl->mNumberBuffers = 2;
    tinyAbl->mBuffers[0].mNumberChannels = 1;
    tinyAbl->mBuffers[0].mDataByteSize   = sizeof(float);
    tinyAbl->mBuffers[0].mData           = tinyBufL.data();
    tinyAbl->mBuffers[1].mNumberChannels = 1;
    tinyAbl->mBuffers[1].mDataByteSize   = sizeof(float);
    tinyAbl->mBuffers[1].mData           = tinyBufR.data();
    AudioBufferList* nullAbl = static_cast<AudioBufferList*>(
        std::malloc(offsetof(AudioBufferList, mBuffers) + 2 * sizeof(AudioBuffer)));
    nullAbl->mNumberBuffers = 2;
    nullAbl->mBuffers[0].mNumberChannels = 1;
    nullAbl->mBuffers[0].mDataByteSize   = 0;
    nullAbl->mBuffers[0].mData           = nullptr;
    nullAbl->mBuffers[1].mNumberChannels = 1;
    nullAbl->mBuffers[1].mDataByteSize   = 0;
    nullAbl->mBuffers[1].mData           = nullptr;

    AudioTimeStamp ts{};
    ts.mSampleTime = 0.0;
    ts.mFlags      = kAudioTimeStampSampleTimeValid;

    // ── 11. Note on ───────────────────────────────────────────────────────────
    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x90, kTestNote, kTestVelocity, 0),
        "MusicDeviceMIDIEvent note-on C4 vel=100");

    // ── 12. Logic-style startup pressure: bulk parameter replay and scheduled
    // automation may overrun the timed ingress queue. The AUv2 wrapper must keep
    // the public host calls noErr; kernel telemetry records any dirty-flush
    // fallback internally.
    for (int round = 0; round < 4; ++round) {
        for (AudioUnitParameterID pid : hostVisibleParams) {
            const float value = (float)(((pid * 17 + round * 23) % 101) / 100.0);
            const OSStatus ps = AudioUnitSetParameter(au,
                                                      pid,
                                                      kAudioUnitScope_Global,
                                                      0,
                                                      value,
                                                      (UInt32)((pid + round) & 127));
            if (ps != noErr) {
                std::fprintf(stderr, "FAIL [Bulk Logic-style parameter replay pid %d round %d]: %d\n",
                             pid, round, (int)ps);
                return 1;
            }
        }
    }
    std::printf("OK   [Bulk Logic-style parameter replay remains host-stable]\n");
    SMOKE_CHECK_LAST_RENDER_CLEAR(au, "LastRenderError clear after bulk parameter replay");

    std::vector<AudioUnitParameterEvent> scheduled(128);
    for (size_t i = 0; i < scheduled.size(); ++i) {
        AudioUnitParameterEvent& ev = scheduled[i];
        ev.scope = kAudioUnitScope_Global;
        ev.element = 0;
        ev.parameter = hostVisibleParams[i % hostVisibleParams.size()];
        if ((i & 1u) == 0u) {
            ev.eventType = kParameterEvent_Immediate;
            ev.eventValues.immediate.bufferOffset = (SInt32)(i & 255u);
            ev.eventValues.immediate.value = (float)((i % 97u) / 96.0);
        } else {
            ev.eventType = kParameterEvent_Ramped;
            ev.eventValues.ramp.startBufferOffset = (SInt32)(i & 127u);
            ev.eventValues.ramp.durationInFrames = 512;
            ev.eventValues.ramp.startValue = 0.0f;
            ev.eventValues.ramp.endValue = (float)((i % 89u) / 88.0);
        }
    }
    SMOKE_CHECK(AudioUnitScheduleParameters(au, scheduled.data(), (UInt32)scheduled.size()),
        "ScheduleParameters pressure remains host-stable");
    SMOKE_CHECK_LAST_RENDER_CLEAR(au, "LastRenderError clear after scheduled parameter pressure");

    // ── 13. Fractional single-sample AU render must stay sane even under
    // duplicate GUI-style parameter writes.
    for (int i = 0; i < 256; ++i) {
        SMOKE_CHECK(AudioUnitSetParameter(au,
            static_cast<AudioUnitParameterID>(ArpSID::kParamFilterCutoff),
            kAudioUnitScope_Global, 0,
            0.41f, 0),
            "SetParameter duplicate kParamFilterCutoff=0.41");
    }
    for (int sample = 0; sample < 2; ++sample) {
        tinyBufL[0] = 0.0f;
        tinyBufR[0] = 0.0f;
        AudioUnitRenderActionFlags tinyFlags = 0;
        SMOKE_CHECK(AudioUnitRender(au, &tinyFlags, &ts, 0, 1, tinyAbl),
            "AudioUnitRender single-frame sanity render");
        SMOKE_ASSERT(std::isfinite(tinyBufL[0]) && std::isfinite(tinyBufR[0]),
            "Single-frame AU render stays finite");
        SMOKE_ASSERT(std::fabs(tinyBufL[0]) < 4.0f && std::fabs(tinyBufR[0]) < 4.0f,
            "Single-frame AU render stays bounded");
        ts.mSampleTime += 1.0;
    }
    SMOKE_CHECK_LAST_RENDER_CLEAR(au, "LastRenderError clear after single-frame renders");

    AudioUnitRenderActionFlags nullFlags = 0;
    SMOKE_CHECK(AudioUnitRender(au, &nullFlags, &ts, 0, 64, nullAbl),
        "AudioUnitRender with null host buffers attaches scratch and returns noErr");
    SMOKE_ASSERT((nullFlags & kAudioUnitRenderAction_OutputIsSilence) == 0 ||
                 nullAbl->mBuffers[0].mData != nullptr,
        "Null-buffer render produced a valid scratch-backed AudioBufferList");
    SMOKE_CHECK_LAST_RENDER_CLEAR(au, "LastRenderError clear after scratch-backed render");

    // ── 14. Render blocks — measure energy ────────────────────────────────────
    double totalEnergy = 0.0;
    double peakSample  = 0.0;

    for (int block = 0; block < kRenderBlockCount; ++block) {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);

        AudioUnitRenderActionFlags flags = 0;
        OSStatus rs = AudioUnitRender(au, &flags, &ts, 0, kMaxFrames, abl);
        if (rs != noErr) {
            std::fprintf(stderr, "FAIL [AudioUnitRender block %d]: %d\n", block, (int)rs);
            std::free(abl);
            return 1;
        }

        for (UInt32 i = 0; i < kMaxFrames; ++i) {
            const float l = bufL[i];
            const float r = bufR[i];
            totalEnergy += (double)(l*l + r*r);
            const float pk = std::max(std::fabs(l), std::fabs(r));
            if ((double)pk > peakSample) peakSample = (double)pk;
        }

        ts.mSampleTime += kMaxFrames;
    }

    std::printf("     Energy over %d render blocks: %.6e  peak: %.6f\n",
                kRenderBlockCount, totalEnergy, peakSample);
    SMOKE_ASSERT(totalEnergy > kMinEnergy,
        "Rendered audio is non-silent after note-on");

    // ── 14. Note off ──────────────────────────────────────────────────────────
    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x80, kTestNote, 0, 0),
        "MusicDeviceMIDIEvent note-off C4");

    // ── 15. Render release tail ───────────────────────────────────────────────
    for (int block = 0; block < kReleaseBlocks; ++block) {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        AudioUnitRenderActionFlags flags = 0;
        AudioUnitRender(au, &flags, &ts, 0, kMaxFrames, abl);
        ts.mSampleTime += kMaxFrames;
    }
    std::printf("OK   [Rendered %d release blocks after note-off]\n", kReleaseBlocks);

    // ── 15a. Saturated UI/host MIDI ingress must not lose note-offs ──────────
    // The kernel raw-MIDI ring has 8191 usable entries. Fill it with note-ons,
    // then deliver releases while it is saturated. The per-note overflow
    // fallback must append those releases after primary queue drain so no SID
    // gate remains stuck.
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamDrSidEnable, kAudioUnitScope_Global, 0, 0.0f, 0),
        "Disable DrSID before saturated note-off test");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamSynthModeEnable, kAudioUnitScope_Global, 0, 0.0f, 0),
        "Select classic engine before saturated note-off test");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamRelease, kAudioUnitScope_Global, 0, 0.0f, 0),
        "Set minimum release before saturated note-off test");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamReverbMix, kAudioUnitScope_Global, 0, 0.0f, 0),
        "Disable reverb before saturated note-off test");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamForensicEnable, kAudioUnitScope_Global, 0, 0.0f, 0),
        "Disable forensic noise before saturated note-off test");
    constexpr int kSaturatedMidiEventCount = 9000;
    for (int i = 0; i < kSaturatedMidiEventCount; ++i) {
        const UInt32 note = static_cast<UInt32>(36 + (i % 60));
        const OSStatus s = MusicDeviceMIDIEvent(au, 0x90, note, 96, 0);
        if (s != noErr) {
            std::fprintf(stderr, "FAIL [saturated note-on %d]: OSStatus %d\n", i, (int)s);
            return 1;
        }
    }
    for (int i = 0; i < kSaturatedMidiEventCount; ++i) {
        const UInt32 note = static_cast<UInt32>(36 + (i % 60));
        const OSStatus s = MusicDeviceMIDIEvent(au, 0x80, note, 0, 0);
        if (s != noErr) {
            std::fprintf(stderr, "FAIL [saturated note-off %d]: OSStatus %d\n", i, (int)s);
            return 1;
        }
    }
    double saturatedRecoveryTailEnergy = 0.0;
    for (int block = 0; block < 16; ++block) {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        AudioUnitRenderActionFlags flags = 0;
        SMOKE_CHECK(AudioUnitRender(au, &flags, &ts, 0, kMaxFrames, abl),
            "AudioUnitRender saturated note-off recovery");
        if (block >= 12) {
            for (UInt32 i = 0; i < kMaxFrames; ++i) {
                saturatedRecoveryTailEnergy +=
                    static_cast<double>(bufL[i]) * static_cast<double>(bufL[i]) +
                    static_cast<double>(bufR[i]) * static_cast<double>(bufR[i]);
            }
        }
        ts.mSampleTime += kMaxFrames;
    }
    std::printf("     Saturated recovery tail energy: %.6e\n",
                saturatedRecoveryTailEnergy);
    SMOKE_ASSERT(saturatedRecoveryTailEnergy < 1.0e-5,
        "Saturated MIDI note-offs leave no sustained/stuck SID tone");

    // ── 16. Host reset must preserve the loaded preset and playable sound ─────
    AUPreset wetPreset{};
    wetPreset.presetNumber = 46; // Orchestral Harp: wet pad-role preset
    wetPreset.presetName   = CFSTR("Reset Wet Preset");
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_PresentPreset,
        kAudioUnitScope_Global, 0,
        &wetPreset, sizeof(wetPreset)),
        "SetProperty PresentPreset[46] before reset");
    SMOKE_CHECK(AudioUnitReset(au, kAudioUnitScope_Global, 0),
        "AudioUnitReset preserves projected patch state");

    AUPreset currentPreset{};
    UInt32 currentPresetSize = sizeof(currentPreset);
    SMOKE_CHECK(AudioUnitGetProperty(au,
        kAudioUnitProperty_PresentPreset,
        kAudioUnitScope_Global, 0,
        &currentPreset, &currentPresetSize),
        "GetProperty PresentPreset after AudioUnitReset");
    SMOKE_ASSERT(currentPreset.presetNumber == wetPreset.presetNumber,
        "PresentPreset survives AudioUnitReset");

    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x90, kTestNote, kTestVelocity, 0),
        "MusicDeviceMIDIEvent note-on after AudioUnitReset");
    double resetEnergy = 0.0;
    for (int block = 0; block < 8; ++block) {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        AudioUnitRenderActionFlags flags = 0;
        SMOKE_CHECK(AudioUnitRender(au, &flags, &ts, 0, kMaxFrames, abl),
            "AudioUnitRender after AudioUnitReset");
        for (UInt32 i = 0; i < kMaxFrames; ++i) {
            const float l = bufL[i];
            const float r = bufR[i];
            resetEnergy += (double)(l*l + r*r);
        }
        ts.mSampleTime += kMaxFrames;
    }
    SMOKE_ASSERT(resetEnergy > kMinEnergy,
        "Rendered audio remains non-silent after AudioUnitReset");
    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x80, kTestNote, 0, 0),
        "MusicDeviceMIDIEvent note-off after AudioUnitReset");

    // ── 15b. GM ch10 should auto-promote into DrSID mode ───────────────────
    AUPreset playPreset{};
    playPreset.presetNumber = 0;
    playPreset.presetName   = CFSTR("Auto GM Start Preset");
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_PresentPreset,
        kAudioUnitScope_Global, 0,
        &playPreset, sizeof(playPreset)),
        "SetProperty PresentPreset[0] before auto-GM drum test");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamDrSidEnable, kAudioUnitScope_Global, 0,
        0.0f, 0),
        "SetParameter kParamDrSidEnable=0 before auto-GM drum test");
    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x99, 36, 110, 0),
        "MusicDeviceMIDIEvent ch10 auto-GM kick");
    double autoDrumEnergy = 0.0;
    for (int block = 0; block < 4; ++block) {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        AudioUnitRenderActionFlags flags = 0;
        SMOKE_CHECK(AudioUnitRender(au, &flags, &ts, 0, kMaxFrames, abl),
            "AudioUnitRender after auto-GM drum promotion");
        for (UInt32 i = 0; i < kMaxFrames; ++i)
            autoDrumEnergy += (double)(bufL[i]*bufL[i] + bufR[i]*bufR[i]);
        ts.mSampleTime += kMaxFrames;
    }
    SMOKE_ASSERT(autoDrumEnergy > kMinEnergy,
        "GM ch10 drum note renders non-silent audio without manual DrSID enable");
    AudioUnitParameterValue autoDrSidNorm = 0.0f;
    SMOKE_CHECK(AudioUnitGetParameter(au,
        ArpSID::kParamDrSidEnable,
        kAudioUnitScope_Global, 0,
        &autoDrSidNorm),
        "GetParameter kParamDrSidEnable after auto-GM drum promotion");
    SMOKE_ASSERT(autoDrSidNorm > 0.5f,
        "GM ch10 drum note promotes the AU into DrSID mode");

    // ── 15c. DrSID drum-box path: factory drum preset + direct GM hits + seq ──
    AUPreset drumPreset{};
    drumPreset.presetNumber = 116; // Taiko Drum: authored DrSID-role preset
    drumPreset.presetName   = CFSTR("DrSID Drum Preset");
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_PresentPreset,
        kAudioUnitScope_Global, 0,
        &drumPreset, sizeof(drumPreset)),
        "SetProperty PresentPreset[116] drum-role preset");
    AudioUnitParameterValue drSidPresetNorm = 0.0f;
    SMOKE_CHECK(AudioUnitGetParameter(au,
        ArpSID::kParamDrSidEnable,
        kAudioUnitScope_Global, 0,
        &drSidPresetNorm),
        "GetParameter kParamDrSidEnable after drum-role preset");
    SMOKE_ASSERT(drSidPresetNorm > 0.5f,
        "Drum-role factory preset enables DrSID mode");

    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamDrSidEnable, kAudioUnitScope_Global, 0,
        1.0f, 0),
        "SetParameter kParamDrSidEnable=1");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamSeqEnable, kAudioUnitScope_Global, 0,
        0.0f, 0),
        "SetParameter kParamSeqEnable=0 before drum-hit check");
    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x99, 36, 110, 0),
        "MusicDeviceMIDIEvent ch10 drum hit Kick");
    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x99, 41, 104, 0),
        "MusicDeviceMIDIEvent ch10 drum hit LowFloorTom");
    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x99, 49, 96, 0),
        "MusicDeviceMIDIEvent ch10 drum hit CrashCymbal1");
    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x99, 56, 100, 0),
        "MusicDeviceMIDIEvent ch10 drum hit Cowbell");
    double drumHitEnergy = 0.0;
    for (int block = 0; block < 6; ++block) {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        AudioUnitRenderActionFlags flags = 0;
        SMOKE_CHECK(AudioUnitRender(au, &flags, &ts, 0, kMaxFrames, abl),
            "AudioUnitRender after DrSID drum hits");
        for (UInt32 i = 0; i < kMaxFrames; ++i)
            drumHitEnergy += (double)(bufL[i]*bufL[i] + bufR[i]*bufR[i]);
        ts.mSampleTime += kMaxFrames;
    }
    SMOKE_ASSERT(drumHitEnergy > kMinEnergy,
        "DrSID drum hits render non-silent audio");

    const struct { int note; float vel; float gate; } drumPattern[4] = {
        {36, 1.00f, 1.0f},
        {42, 0.72f, 0.8f},
        {38, 0.92f, 1.0f},
        {46, 0.76f, 0.9f},
    };
    for (int step = 0; step < 4; ++step) {
        const AudioUnitParameterID base = (AudioUnitParameterID)ArpSID::kParamSeqStep1Note + (AudioUnitParameterID)(step * 3);
        SMOKE_CHECK(AudioUnitSetParameter(au, base + 0, kAudioUnitScope_Global, 0,
            std::clamp((float)drumPattern[step].note / 127.0f, 0.0f, 1.0f), 0),
            "SetParameter Seq Step Note");
        SMOKE_CHECK(AudioUnitSetParameter(au, base + 1, kAudioUnitScope_Global, 0,
            drumPattern[step].vel, 0),
            "SetParameter Seq Step Velocity");
        SMOKE_CHECK(AudioUnitSetParameter(au, base + 2, kAudioUnitScope_Global, 0,
            drumPattern[step].gate, 0),
            "SetParameter Seq Step Gate");
    }
    for (int step = 4; step < 32; ++step) {
        const AudioUnitParameterID gateParam = (AudioUnitParameterID)ArpSID::kParamSeqStep1Gate + (AudioUnitParameterID)(step * 3);
        SMOKE_CHECK(AudioUnitSetParameter(au, gateParam, kAudioUnitScope_Global, 0, 0.0f, 0),
            "Clear unused Seq Step Gate");
    }
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamSeqLength, kAudioUnitScope_Global, 0,
        3.0f / 31.0f, 0),
        "SetParameter kParamSeqLength=4");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamSeqMode, kAudioUnitScope_Global, 0,
        0.0f, 0),
        "SetParameter kParamSeqMode=Forward");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamSeqTempo, kAudioUnitScope_Global, 0,
        0.55f, 0),
        "SetParameter kParamSeqTempo internal");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamSeqEnable, kAudioUnitScope_Global, 0,
        1.0f, 0),
        "SetParameter kParamSeqEnable=1");
    double drumSeqEnergy = 0.0;
    for (int block = 0; block < 10; ++block) {
        std::fill(bufL.begin(), bufL.end(), 0.0f);
        std::fill(bufR.begin(), bufR.end(), 0.0f);
        AudioUnitRenderActionFlags flags = 0;
        SMOKE_CHECK(AudioUnitRender(au, &flags, &ts, 0, kMaxFrames, abl),
            "AudioUnitRender internal DrSID sequencer");
        for (UInt32 i = 0; i < kMaxFrames; ++i)
            drumSeqEnergy += (double)(bufL[i]*bufL[i] + bufR[i]*bufR[i]);
        ts.mSampleTime += kMaxFrames;
    }
    SMOKE_ASSERT(drumSeqEnergy > kMinEnergy,
        "Internal DrSID sequencer renders non-silent audio");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamSeqEnable, kAudioUnitScope_Global, 0,
        0.0f, 0),
        "SetParameter kParamSeqEnable=0 after drum-box check");
    SMOKE_CHECK(AudioUnitSetParameter(au,
        ArpSID::kParamDrSidEnable, kAudioUnitScope_Global, 0,
        0.0f, 0),
        "SetParameter kParamDrSidEnable=0 after drum-box check");

    // ── 16. Reconfigure sample rate / slice size in-place ─────────────────────
    SMOKE_CHECK(AudioUnitUninitialize(au),
        "AudioUnitUninitialize before 96k reconfigure");
    const UInt32 maxFrames96k = 2048;
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_MaximumFramesPerSlice,
        kAudioUnitScope_Global, 0,
        &maxFrames96k, sizeof(maxFrames96k)),
        "SetProperty MaximumFramesPerSlice[2048]");
    AudioStreamBasicDescription fmt96 = fmt;
    fmt96.mSampleRate = 96000.0;
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Output, 0,
        &fmt96, sizeof(fmt96)),
        "SetProperty StreamFormat stereo float32 @ 96k");
    SMOKE_CHECK(AudioUnitInitialize(au),
        "AudioUnitInitialize after 96k reconfigure");
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_PresentPreset,
        kAudioUnitScope_Global, 0,
        &wetPreset, sizeof(wetPreset)),
        "SetProperty PresentPreset[46] after 96k reconfigure");

    std::vector<float> buf96L(maxFrames96k, 0.0f);
    std::vector<float> buf96R(maxFrames96k, 0.0f);
    AudioBufferList* abl96 = static_cast<AudioBufferList*>(
        std::malloc(offsetof(AudioBufferList, mBuffers) + 2 * sizeof(AudioBuffer)));
    abl96->mNumberBuffers = 2;
    abl96->mBuffers[0].mNumberChannels = 1;
    abl96->mBuffers[0].mDataByteSize = maxFrames96k * sizeof(float);
    abl96->mBuffers[0].mData = buf96L.data();
    abl96->mBuffers[1].mNumberChannels = 1;
    abl96->mBuffers[1].mDataByteSize = maxFrames96k * sizeof(float);
    abl96->mBuffers[1].mData = buf96R.data();

    AudioTimeStamp ts96{};
    ts96.mSampleTime = 0.0;
    ts96.mFlags = kAudioTimeStampSampleTimeValid;

    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x90, kTestNote, kTestVelocity, 0),
        "MusicDeviceMIDIEvent note-on after 96k reconfigure");
    double energy96k = 0.0;
    for (int block = 0; block < 4; ++block) {
        std::fill(buf96L.begin(), buf96L.end(), 0.0f);
        std::fill(buf96R.begin(), buf96R.end(), 0.0f);
        AudioUnitRenderActionFlags flags = 0;
        SMOKE_CHECK(AudioUnitRender(au, &flags, &ts96, 0, maxFrames96k, abl96),
            "AudioUnitRender after 96k reconfigure");
        for (UInt32 i = 0; i < maxFrames96k; ++i) {
            const float l = buf96L[i];
            const float r = buf96R[i];
            energy96k += (double)(l*l + r*r);
        }
        ts96.mSampleTime += maxFrames96k;
    }
    SMOKE_ASSERT(energy96k > kMinEnergy,
        "Rendered audio remains non-silent after 96k reconfigure");
    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x80, kTestNote, 0, 0),
        "MusicDeviceMIDIEvent note-off after 96k reconfigure");
    std::free(abl96);

    // ── 16b. Reconfigure again for 192k DrSID drum rendering ─────────────────
    SMOKE_CHECK(AudioUnitUninitialize(au),
        "AudioUnitUninitialize before 192k DrSID reconfigure");
    const UInt32 maxFrames192k = 4096;
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_MaximumFramesPerSlice,
        kAudioUnitScope_Global, 0,
        &maxFrames192k, sizeof(maxFrames192k)),
        "SetProperty MaximumFramesPerSlice[4096]");
    AudioStreamBasicDescription fmt192 = fmt;
    fmt192.mSampleRate = 192000.0;
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Output, 0,
        &fmt192, sizeof(fmt192)),
        "SetProperty StreamFormat stereo float32 @ 192k");
    SMOKE_CHECK(AudioUnitInitialize(au),
        "AudioUnitInitialize after 192k reconfigure");
    SMOKE_CHECK(AudioUnitSetProperty(au,
        kAudioUnitProperty_PresentPreset,
        kAudioUnitScope_Global, 0,
        &drumPreset, sizeof(drumPreset)),
        "SetProperty PresentPreset[116] after 192k reconfigure");

    std::vector<float> buf192L(maxFrames192k, 0.0f);
    std::vector<float> buf192R(maxFrames192k, 0.0f);
    AudioBufferList* abl192 = static_cast<AudioBufferList*>(
        std::malloc(offsetof(AudioBufferList, mBuffers) + 2 * sizeof(AudioBuffer)));
    abl192->mNumberBuffers = 2;
    abl192->mBuffers[0].mNumberChannels = 1;
    abl192->mBuffers[0].mDataByteSize = maxFrames192k * sizeof(float);
    abl192->mBuffers[0].mData = buf192L.data();
    abl192->mBuffers[1].mNumberChannels = 1;
    abl192->mBuffers[1].mDataByteSize = maxFrames192k * sizeof(float);
    abl192->mBuffers[1].mData = buf192R.data();

    AudioTimeStamp ts192{};
    ts192.mSampleTime = 0.0;
    ts192.mFlags = kAudioTimeStampSampleTimeValid;

    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x99, 36, 110, 0),
        "MusicDeviceMIDIEvent ch10 drum hit Kick @ 192k");
    SMOKE_CHECK(MusicDeviceMIDIEvent(au, 0x99, 49, 96, 0),
        "MusicDeviceMIDIEvent ch10 drum hit Crash @ 192k");
    double energy192k = 0.0;
    for (int block = 0; block < 4; ++block) {
        std::fill(buf192L.begin(), buf192L.end(), 0.0f);
        std::fill(buf192R.begin(), buf192R.end(), 0.0f);
        AudioUnitRenderActionFlags flags = 0;
        SMOKE_CHECK(AudioUnitRender(au, &flags, &ts192, 0, maxFrames192k, abl192),
            "AudioUnitRender after 192k DrSID reconfigure");
        for (UInt32 i = 0; i < maxFrames192k; ++i)
            energy192k += (double)(buf192L[i]*buf192L[i] + buf192R[i]*buf192R[i]);
        ts192.mSampleTime += maxFrames192k;
    }
    SMOKE_ASSERT(energy192k > kMinEnergy,
        "DrSID drum rendering remains non-silent after 192k reconfigure");
    std::free(abl192);

    // ── 18. ClassInfo (state round-trip) ──────────────────────────────────────
    CFPropertyListRef stateOut = nullptr;
    UInt32 stateSize = sizeof(CFPropertyListRef);
    SMOKE_CHECK(AudioUnitGetProperty(au,
        kAudioUnitProperty_ClassInfo,
        kAudioUnitScope_Global, 0,
        &stateOut, &stateSize),
        "GetProperty ClassInfo (state save)");
    SMOKE_ASSERT(stateOut != nullptr, "ClassInfo dict is non-null");
    if (stateOut) {
        // Round-trip: restore the same state.
        SMOKE_CHECK(AudioUnitSetProperty(au,
            kAudioUnitProperty_ClassInfo,
            kAudioUnitScope_Global, 0,
            &stateOut, sizeof(CFPropertyListRef)),
            "SetProperty ClassInfo (state restore round-trip)");
        SMOKE_CHECK(AudioUnitSetProperty(au,
            kAudioUnitProperty_ClassInfoFromDocument,
            kAudioUnitScope_Global, 0,
            &stateOut, sizeof(CFPropertyListRef)),
            "SetProperty ClassInfoFromDocument (state restore round-trip)");

        // Re-entry regression stress: alternate PresentPreset with both ClassInfo
        // setters so synchronous AUAudioUnit preset notifications cannot recurse
        // back through the AUv2 property bridge.
        // Cover the wet factory roles that used to crash on first AU render:
        // bell presets (Harpsichord/Glockenspiel) and pad presets
        // (Orchestral Harp/String Ensemble) all enable reverb in the factory map.
        for (SInt32 presetNumber : { 0, 6, 9, 46, 48, 49, 50, 0 }) {
            AUPreset stressPreset{};
            stressPreset.presetNumber = presetNumber;
            stressPreset.presetName = CFSTR("Reentry Stress Preset");
            SMOKE_CHECK(AudioUnitSetProperty(au,
                kAudioUnitProperty_PresentPreset,
                kAudioUnitScope_Global, 0,
                &stressPreset, sizeof(stressPreset)),
                "Stress SetProperty PresentPreset");
            SMOKE_CHECK(AudioUnitSetProperty(au,
                kAudioUnitProperty_ClassInfo,
                kAudioUnitScope_Global, 0,
                &stateOut, sizeof(CFPropertyListRef)),
                "Stress SetProperty ClassInfo");
            SMOKE_CHECK(AudioUnitSetProperty(au,
                kAudioUnitProperty_ClassInfoFromDocument,
                kAudioUnitScope_Global, 0,
                &stateOut, sizeof(CFPropertyListRef)),
                "Stress SetProperty ClassInfoFromDocument");

            std::fill(bufL.begin(), bufL.end(), 0.0f);
            std::fill(bufR.begin(), bufR.end(), 0.0f);
            AudioUnitRenderActionFlags flags = 0;
            SMOKE_CHECK(AudioUnitRender(au, &flags, &ts, 0, kMaxFrames, abl),
                "AudioUnitRender after preset bridge stress");
            ts.mSampleTime += kMaxFrames;
        }
        CFRelease(stateOut);
    }

    // ── 19. Uninitialize and close ────────────────────────────────────────────
    SMOKE_CHECK(AudioUnitUninitialize(au),  "AudioUnitUninitialize");
    SMOKE_CHECK(AudioComponentInstanceDispose(au), "AudioComponentInstanceDispose");

    std::free(abl);
    std::free(tinyAbl);
    std::free(nullAbl);

    std::printf("=== PASS — ArpSID AUv2 smoke test complete ===\n");
    return 0;

    } // @autoreleasepool
}
