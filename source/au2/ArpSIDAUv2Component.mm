// Copyright (C) 2024-2026 Ulf Bertilsson
#import "ArpSIDAUv2Shared.h"

#import "../au3/ArpSIDAudioUnit.h"
#import "../au3/ArpSIDComponentFlavor.h"
#import "../au3/ArpSIDDSPKernelAdapter.h"
#include "../au3/ArpSIDDSPKernel.hpp"
#import "../au3/ArpSIDAUEventTranslator.h"
#import "../au3/ArpSIDViewController.h"

#include "parameter_ids.h"
#include "factory_patch_params.h"
#include "arpsid/core/sid_event_timing.h"
#include "arpsid/core/sid_runtime_forensic_config.h"
#include "arpsid/core/sid_parameter_presentation.h"
#include "arpsid/core/sid_runtime_host_policy.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "arpsid/core/sid_variant_ops.h"
#include "arpsid/core/sid_realtime_guard.h"
#include "arpsid/core/sid_chip.h"
#include "arpsid/core/render_epoch.h"
#include "arpsid/core/audited_mutex.h"

#import <AppKit/AppKit.h>
#import <AudioToolbox/AUCocoaUIView.h>
#import <AudioToolbox/AudioUnitProperties.h>
#import <objc/runtime.h>
#import <dispatch/dispatch.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <memory>
#include <limits>
#include <new>
#include <thread>
#include <vector>

namespace {

using namespace ArpSIDAUv2;

struct ArpSIDAUv2Instance;
struct ArpSIDAUv2Wrapper {
    AudioComponentPlugInInterface plugInInterface{};
    ArpSIDAUv2Instance* impl = nullptr;
};

static inline ArpSID::ComponentFlavor componentFlavorForInstance(const ArpSIDAUv2Instance* impl) noexcept;

static const void* kArpSIDAUv2ViewControllerAssociationKey = &kArpSIDAUv2ViewControllerAssociationKey;
static const void* kArpSIDAUv2ContextNameAssociationKey = &kArpSIDAUv2ContextNameAssociationKey;
static const void* kArpSIDAUv2AudioUnitControllerAssociationKey = &kArpSIDAUv2AudioUnitControllerAssociationKey;
static const void* kArpSIDAUv2AudioUnitViewAssociationKey = &kArpSIDAUv2AudioUnitViewAssociationKey;

static ArpSIDViewController* associatedViewControllerForAudioUnit(ArpSIDAudioUnit* audioUnit) noexcept {
    if (!audioUnit) return nil;
    id controller = objc_getAssociatedObject(audioUnit, &kArpSIDAUv2AudioUnitControllerAssociationKey);
    return [controller isKindOfClass:[ArpSIDViewController class]] ? (ArpSIDViewController*)controller : nil;
}

// audit P0-9: tear down an editor that the Cocoa view factory built but that the
// host never received (the off-main build completed after the caller had already
// timed out and returned nil). Disposes the controller and clears the controller/
// view associations so no orphan editor stays connected to the AudioUnit. MAIN
// THREAD ONLY (AppKit/editor teardown), and idempotent (safe if already cleared).
static void disposeAbandonedAUv2Editor(ArpSIDAudioUnit* audioUnit) {
    if (!audioUnit) return;
    ArpSIDViewController* controller = associatedViewControllerForAudioUnit(audioUnit);
    if (controller && [controller respondsToSelector:@selector(prepareForFinalEditorDisposal)]) {
        @try { [controller prepareForFinalEditorDisposal]; } @catch (NSException* __unused ex) {}
    }
    objc_setAssociatedObject(audioUnit, &kArpSIDAUv2AudioUnitViewAssociationKey, nil,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(audioUnit, &kArpSIDAUv2AudioUnitControllerAssociationKey, nil,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

// AUv2 does not own a render-event queue. Host parameter/MIDI entrypoints
// enqueue directly into the canonical kernel intent/ingress queues, and
// componentRender() passes a null AU render-event list to the shared AUv3
// render block. This avoids a second timestamp/sort/overflow authority in AUv2.

static constexpr UInt32 kAuv2DefaultMaxFrames = 8192u;
static constexpr UInt32 kAuv2MinMaxFrames = 64u;
static constexpr UInt32 kAuv2HardMaxFrames = 65536u;
static constexpr UInt32 kAuv2ScratchHeadroomFrames = 2048u;
static constexpr UInt32 kAuv2ScratchMultiplier = 2u;

static inline double sanitizeAuv2SampleRate(double sr) noexcept {
    return ArpSIDSanitizeHostSampleRate(sr);
}

static inline UInt32 roundUpAuv2FrameQuantum(UInt32 frames) noexcept {
    UInt32 v = std::max<UInt32>(kAuv2MinMaxFrames, frames);
    v -= 1u;
    v |= v >> 1u;
    v |= v >> 2u;
    v |= v >> 4u;
    v |= v >> 8u;
    v |= v >> 16u;
    return v + 1u;
}

static inline void applyAuv2ComponentFlavorPolicyToStateRoot(ArpSID::ComponentFlavor flavor,
                                                             ArpSID::SidStateRootV1& root) noexcept {
    switch (flavor) {
        case ArpSID::ComponentFlavor::Instrument:
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 1.0f);
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 0.0f);
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamArpEnable, 0.0f);
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSeqEnable, 0.0f);
            break;
        case ArpSID::ComponentFlavor::DrumMachine:
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 0.0f);
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 1.0f);
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamArpEnable, 0.0f);
            // v950: DrumMachine flavor must not destructively clear a restored
            // SeqEnable; DrSID/SID808 owns SEQ as drum-pattern transport.
            break;
        case ArpSID::ComponentFlavor::Sid808:
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 0.0f);
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 1.0f);
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamArpEnable, 0.0f);
            // v950: SID808 flavor preserves SeqEnable as drum-pattern transport.
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidMachineModel, 1.0f);
            break;
        case ArpSID::ComponentFlavor::C64SidPlayer:
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 0.0f);
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 0.0f);
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamArpEnable, 0.0f);
            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSeqEnable, 0.0f);
            break;
        case ArpSID::ComponentFlavor::Hybrid:
        default:
            break;
    }
    ArpSID::sidEnsureSemanticParameterEntries(root);
}

static inline UInt32 sanitizeAuv2MaxFrames(UInt32 frames) noexcept {
    const UInt32 clamped = std::min<UInt32>(std::max<UInt32>(kAuv2MinMaxFrames, frames), kAuv2HardMaxFrames);
    return std::min<UInt32>(roundUpAuv2FrameQuantum(clamped), kAuv2HardMaxFrames);
}


struct Auv2FixedFloatScratch {
    std::unique_ptr<float[]> data{};
    size_t capacity = 0;

    bool ensureCapacityNonRealtime(size_t needed) {
        if (capacity >= needed) return true;
        // AUv2 v391: this function is configuration/open/initialize only.
        // componentRender() is guarded and may only consume already-reserved memory.
        ArpSID::sidRealtimeGuardForbidAllocation("AUv2 scratch resize must be non-RT/configuration only");
        std::unique_ptr<float[]> next(new (std::nothrow) float[needed]());
        if (!next) return false;
        data = std::move(next);
        capacity = needed;
        return true;
    }

    bool hasCapacity(size_t needed) const noexcept { return data && capacity >= needed; }
    float* ptr() noexcept { return data.get(); }
    const float* ptr() const noexcept { return data.get(); }
};

static inline bool ensureFloatScratchCapacityNonRealtime(Auv2FixedFloatScratch& scratch, size_t needed) {
    return scratch.ensureCapacityNonRealtime(needed);
}

static inline bool auv2ParamIsHostVisible(int pid) {
    if (pid < 0 || pid >= ArpSID::kNumParams) return false;
    if (ArpSID::isHostMidiBridgeParam(pid)) return false;
    return ArpSID::kParamInfos[(size_t)pid].automatable;
}

static std::vector<AudioUnitParameterID> buildHostVisibleParameterList() {
    std::vector<AudioUnitParameterID> ids;
    ids.reserve((size_t)ArpSID::kNumParams);
    for (int pid = 0; pid < ArpSID::kNumParams; ++pid) {
        if (auv2ParamIsHostVisible(pid)) {
            ids.push_back((AudioUnitParameterID)pid);
        }
    }
    return ids;
}

static const std::vector<AudioUnitParameterID>& hostVisibleParameterList() {
    static const std::vector<AudioUnitParameterID> sIDs = buildHostVisibleParameterList();
    return sIDs;
}

static inline bool auv2ParameterIsBooleanParamID(AudioUnitParameterID pid) noexcept {
    return pid < (AudioUnitParameterID)ArpSID::kNumParams &&
           ArpSID::isBooleanNormalizedParam(static_cast<int>(pid));
}

static inline UInt32 auv2ParameterStepCountForParamID(AudioUnitParameterID pid) noexcept {
    if (pid >= (AudioUnitParameterID)ArpSID::kNumParams) return 0;
    return static_cast<UInt32>(ArpSID::normalizedParamStepCount(static_cast<int>(pid)));
}

static inline AudioUnitParameterUnit auv2ParameterUnitForInfo(AudioUnitParameterID pid,
                                                             const ArpSID::ParamInfo&) noexcept {
    // v966: unit identity comes from the shared typed descriptor instead of
    // per-wrapper unit-string matching (which missed "cent" vs "ct" and
    // "%" vs "pct" and reported Generic for those parameters).
    switch (ArpSID::sidParameterUnitDescriptor((int)pid).unit) {
        case ArpSID::SidParameterUnit::Boolean: return kAudioUnitParameterUnit_Boolean;
        case ArpSID::SidParameterUnit::Seconds: return kAudioUnitParameterUnit_Seconds;
        case ArpSID::SidParameterUnit::Milliseconds: return kAudioUnitParameterUnit_Milliseconds;
        case ArpSID::SidParameterUnit::Hertz: return kAudioUnitParameterUnit_Hertz;
        case ArpSID::SidParameterUnit::Cents: return kAudioUnitParameterUnit_Cents;
        case ArpSID::SidParameterUnit::Bpm: return kAudioUnitParameterUnit_BPM;
        case ArpSID::SidParameterUnit::Steps: return kAudioUnitParameterUnit_Indexed;
        case ArpSID::SidParameterUnit::Percent: return kAudioUnitParameterUnit_Percent;
        case ArpSID::SidParameterUnit::Celsius: return kAudioUnitParameterUnit_Generic;  // CoreAudio has no Celsius unit; text carries the °C suffix
        case ArpSID::SidParameterUnit::Volts: return kAudioUnitParameterUnit_Generic;
        case ArpSID::SidParameterUnit::Byte: return kAudioUnitParameterUnit_Indexed;
        case ArpSID::SidParameterUnit::Index: return kAudioUnitParameterUnit_Indexed;
        case ArpSID::SidParameterUnit::IndexedLabel: return kAudioUnitParameterUnit_Indexed;
        case ArpSID::SidParameterUnit::Seed: return kAudioUnitParameterUnit_Indexed;
        case ArpSID::SidParameterUnit::Normalized:
        default: return kAudioUnitParameterUnit_Generic;
    }
}

static inline bool auv2ParameterIsStepped(AudioUnitParameterID pid,
                                          const ArpSID::ParamInfo&,
                                          UInt32* outStepCount) noexcept {
    const UInt32 stepCount = auv2ParameterStepCountForParamID(pid);
    if (outStepCount) *outStepCount = stepCount;
    return stepCount > 0;
}

// v966: AUv2 display and text parsing delegate to the shared parameter-ID-
// aware presentation authority (arpsid/core/sid_parameter_presentation.h) so
// AUv2, AUv3 and VST3 render identical semantic text from identical laws.
static NSString* stringForNormalizedParameterValue(AudioUnitParameterID pid, AudioUnitParameterValue value) {
    char buf[64] = {};
    if (!ArpSID::SidParameterPresentation::formatNormalized((int)pid, (float)value, buf, sizeof(buf)))
        return @"0.0000";
    NSString* s = [NSString stringWithUTF8String:buf];
    return s ? s : @"0.0000";
}

static bool normalizedParameterValueFromString(AudioUnitParameterID pid, CFStringRef inString, AudioUnitParameterValue* outValue) {
    if (!inString || !outValue || pid >= (AudioUnitParameterID)ArpSID::kNumParams) return false;
    NSString* s = (__bridge NSString*)inString;
    const char* utf8 = s.UTF8String;
    float normalized = 0.0f;
    if (!ArpSID::SidParameterPresentation::parseToNormalized((int)pid, utf8, normalized))
        return false;
    *outValue = normalized;
    return true;
}

static AudioStreamBasicDescription makeCanonicalOutputFormat(Float64 sampleRate, UInt32 channels) {
    AudioStreamBasicDescription asbd{};
    asbd.mSampleRate = sanitizeAuv2SampleRate(sampleRate);
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsFloat |
                        kAudioFormatFlagIsPacked |
                        kAudioFormatFlagsNativeEndian |
                        kAudioFormatFlagIsNonInterleaved;
    asbd.mBytesPerPacket = sizeof(Float32);
    asbd.mFramesPerPacket = 1;
    asbd.mBytesPerFrame = sizeof(Float32);
    asbd.mChannelsPerFrame = channels;
    asbd.mBitsPerChannel = 8 * sizeof(Float32);
    return asbd;
}

static bool isSupportedOutputFormat(const AudioStreamBasicDescription& asbd) {
    if (!ArpSIDIsCanonicalHostSampleRate(asbd.mSampleRate)) return false;
    if (asbd.mChannelsPerFrame != 1 && asbd.mChannelsPerFrame != 2) return false;
    if (asbd.mFormatID != kAudioFormatLinearPCM) return false;
    const UInt32 requiredFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    if ((asbd.mFormatFlags & requiredFlags) != requiredFlags) return false;
    if (asbd.mBitsPerChannel != 32) return false;
    const bool nonInterleaved = (asbd.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
    const UInt32 expectedBytesPerFrame = nonInterleaved ? sizeof(Float32) : (asbd.mChannelsPerFrame * sizeof(Float32));
    if (asbd.mBytesPerFrame != expectedBytesPerFrame) return false;
    if (asbd.mBytesPerPacket != expectedBytesPerFrame) return false;
    return true;
}

static bool audioUnitSupportsSettableFormat(ArpSIDAudioUnit* audioUnit, AVAudioFormat* newFormat) {
    if (!audioUnit || !newFormat) return false;
    AUAudioUnitBusArray* outputBusses = audioUnit.outputBusses;
    if (outputBusses.count < 1) return false;
    AUAudioUnitBus* outputBus = [outputBusses objectAtIndexedSubscript:0];
    NSError* formatError = nil;
    if (![outputBus setFormat:newFormat error:&formatError]) {
        return false;
    }
    return true;
}

static AudioBufferList* prepareOutputBufferList(AudioBufferList* ioData,
                                                UInt32 frameCount,
                                                UInt32 renderedChannels,
                                                Auv2FixedFloatScratch& monoScratch,
                                                Auv2FixedFloatScratch& stereoScratch,
                                                bool* outAttachedScratch = nullptr) noexcept {
    if (outAttachedScratch) *outAttachedScratch = false;
    if (!ioData || ioData->mNumberBuffers == 0) return nullptr;
    if (frameCount == 0) return ioData;

    // AUv2 v418 auval slicing guard: hosts normally provide correctly sized
    // mData, but auval/Logic can exercise paths where mData is null or a stale
    // byte-size is carried across format/slicing transitions. Do not let the
    // wrapped AUv3 render see malformed buffers and return -10867/-50. If the
    // AUv2 call contract was otherwise valid, bridge through pre-reserved owned
    // scratch. This function never allocates or locks on the render thread.
    if (ioData->mNumberBuffers == 1) {
        AudioBuffer& b = ioData->mBuffers[0];
        const UInt32 channels = std::max<UInt32>(1u, std::max<UInt32>(b.mNumberChannels, renderedChannels));
        const size_t needed = (size_t)frameCount * channels;
        const UInt32 neededBytes = (UInt32)std::min<size_t>(needed * sizeof(float), std::numeric_limits<UInt32>::max());
        if (!b.mData || b.mDataByteSize < neededBytes || b.mNumberChannels == 0) {
            if (!stereoScratch.hasCapacity(needed)) return nullptr;
            std::memset(stereoScratch.ptr(), 0, needed * sizeof(float));
            b.mData = stereoScratch.ptr();
            b.mDataByteSize = neededBytes;
            b.mNumberChannels = channels;
            if (outAttachedScratch) *outAttachedScratch = true; // audit #50
        }
        return ioData;
    }

    const UInt32 scratchBuffers = std::min<UInt32>(std::max<UInt32>(1u, renderedChannels), ioData->mNumberBuffers);
    bool needsScratch = false;
    for (UInt32 i = 0; i < scratchBuffers; ++i) {
        const AudioBuffer& b = ioData->mBuffers[i];
        const UInt32 neededBytes = frameCount * (UInt32)sizeof(float);
        if (!b.mData || b.mDataByteSize < neededBytes || b.mNumberChannels == 0) {
            needsScratch = true;
            break;
        }
    }
    if (!needsScratch) return ioData;

    const size_t needed = (size_t)frameCount * scratchBuffers;
    if (!monoScratch.hasCapacity(needed)) return nullptr;
    std::memset(monoScratch.ptr(), 0, needed * sizeof(float));
    const size_t stride = (size_t)frameCount;
    for (UInt32 j = 0; j < scratchBuffers; ++j) {
        AudioBuffer& pb = ioData->mBuffers[j];
        pb.mData = monoScratch.ptr() + (size_t)j * stride;
        pb.mDataByteSize = frameCount * sizeof(float);
        pb.mNumberChannels = 1;
    }
    if (outAttachedScratch) *outAttachedScratch = true; // audit #50
    return ioData;
}

static inline AudioUnitRenderActionFlags auv2SanitizeHostRenderFlags(AudioUnitRenderActionFlags flags) noexcept {
    // Hosts may hand AUv2 stale action bits back to render. componentRender() owns
    // pre/post/error/silence publication for the current slice, so strip all
    // producer-owned bits before the slice starts. Keep host/request bits such as
    // offline/preflight semantics intact.
    flags &= ~(kAudioUnitRenderAction_PreRender |
               kAudioUnitRenderAction_PostRender |
               kAudioUnitRenderAction_PostRenderError |
               kAudioUnitRenderAction_OutputIsSilence);
    return flags;
}

static inline void auv2UpdateSilenceFlag(AudioUnitRenderActionFlags* flags,
                                         const AudioBufferList* output,
                                         UInt32 frameCount,
                                         OSStatus status,
                                         bool preserveExplicitSilence = false) noexcept {
    if (!flags) return;
    if (preserveExplicitSilence) {
        *flags |= kAudioUnitRenderAction_OutputIsSilence;
        return;
    }
    if (status != noErr || !output || frameCount == 0) {
        *flags |= kAudioUnitRenderAction_OutputIsSilence;
        return;
    }
    // Hard realtime guard: do not perform an O(N) post-render buffer scan on
    // the AUv2 audio thread. A successful synth render is treated as potentially
    // audible; actual meter/silence analysis belongs to the DSP telemetry layer.
    (void)output;
    (void)frameCount;
    *flags &= ~kAudioUnitRenderAction_OutputIsSilence;
}


static inline bool auv2FactoryDefinitionIsDrumAuthored(const ArpSID::PatchDefinition& def) noexcept {
    return def.usage.role == ArpSID::PatchRole::Drum ||
           def.usage.family == ArpSID::HistoricalFamilyId::DrumKit;
}

static NSInteger auv2StartupFactorySlotForFlavor(ArpSID::ComponentFlavor flavor) noexcept {
    if (flavor == ArpSID::ComponentFlavor::C64SidPlayer) return 0;
    if (flavor == ArpSID::ComponentFlavor::Sid808) {
        const auto& defs = ArpSID::getFactoryPatchDefinitions();
        if (defs.size() > 120u && auv2FactoryDefinitionIsDrumAuthored(defs[120])) return 120;
    }
    if (!ArpSID::componentFlavorIsDedicatedDrum(flavor)) return 0;
    const auto& defs = ArpSID::getFactoryPatchDefinitions();
    for (size_t i = 0; i < defs.size(); ++i) {
        if (auv2FactoryDefinitionIsDrumAuthored(defs[i])) return (NSInteger)i;
    }
    return 0;
}

static inline bool auv2FactorySlotAllowedForFlavor(ArpSID::ComponentFlavor flavor, NSInteger slot) noexcept {
    const auto& defs = ArpSID::getFactoryPatchDefinitions();
    if (defs.empty()) return slot == 0;
    const NSInteger normalized = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)slot);
    if ((size_t)normalized >= defs.size()) return !ArpSID::componentFlavorIsDedicatedDrum(flavor);
    const bool isDrum = auv2FactoryDefinitionIsDrumAuthored(defs[(size_t)normalized]);
    switch (flavor) {
        case ArpSID::ComponentFlavor::Instrument:
            return !isDrum;
        case ArpSID::ComponentFlavor::C64SidPlayer:
            return normalized == 0;
        case ArpSID::ComponentFlavor::Sid808:
            return ArpSID::isSid808FactorySlot((int)normalized);
        case ArpSID::ComponentFlavor::DrumMachine:
            return isDrum;
        case ArpSID::ComponentFlavor::Hybrid:
        default:
            return true;
    }
}

static NSString* auv2FactoryPresetTitleForFlavor(ArpSID::ComponentFlavor flavor,
                                                 NSInteger presetNumber,
                                                 NSString* baseName) {
    const NSInteger normalized = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)presetNumber);
    NSString* resolvedName = baseName.length > 0 ? baseName : @"Factory Patch";
    if (flavor == ArpSID::ComponentFlavor::Sid808) {
        return [NSString stringWithFormat:@"808 K%03ld · %@", (long)(normalized + 1), resolvedName];
    }
    if (ArpSID::componentFlavorIsDedicatedDrum(flavor)) {
        return [NSString stringWithFormat:@"K%03ld · %@", (long)(normalized + 1), resolvedName];
    }
    return [NSString stringWithFormat:@"P%03ld · %@", (long)(normalized + 1), resolvedName];
}

static NSArray<AUAudioUnitPreset*>* staticFactoryPresets(ArpSID::ComponentFlavor flavor);

static CFArrayRef cachedFactoryPresetArray(ArpSID::ComponentFlavor flavor) {
    // AUv2 FactoryPresets returns an owned CFArrayRef. Build the backing AUPreset
    // structs exactly once on a non-RT/property thread and return CFRetained views.
    // The CFArray deliberately uses NULL callbacks because its elements are stable
    // pointers into process-lifetime storage; this avoids per-query allocator/copy
    // callback churn and matches the host-side "treat entry as AUPreset*" contract.
    // The inner CFStringRefs are also process-lifetime and therefore share the
    // component bundle lifetime intentionally; if the bundle ever changed to a
    // shorter unload lifecycle this representation would need revisiting.
    struct CachedFactoryPresetStorage {
        std::vector<AUPreset> presets;
        std::vector<CFStringRef> names;
        CFArrayRef array = nullptr;
    };

    // RT ISOLATION NOTE: `storageByFlavor` is the only mutable global in the AUv2
    // component. It is written ONCE per flavor (via dispatch_once) during the first
    // kAudioUnitProperty_FactoryPresets query — always on a non-RT thread (host setup
    // time). After the first write it is STRICTLY READ-ONLY. Multiple AUv2 instances
    // of the same flavor share this cache safely; no render-thread access ever occurs.
    static std::array<CachedFactoryPresetStorage, ArpSID::kComponentFlavorCount> storageByFlavor{};
    static std::array<dispatch_once_t, ArpSID::kComponentFlavorCount> onceTokens{};
    const size_t flavorIndex = ArpSID::componentFlavorIndex(flavor);
    CachedFactoryPresetStorage* storage = &storageByFlavor[flavorIndex];
    dispatch_once(&onceTokens[flavorIndex], ^{
        NSArray<AUAudioUnitPreset*>* sourcePresets = staticFactoryPresets(flavor);
        storage->presets.reserve((size_t)[sourcePresets count]);
        storage->names.reserve((size_t)[sourcePresets count]);
        std::vector<const void*> values;
        values.reserve((size_t)[sourcePresets count]);

        for (AUAudioUnitPreset* preset in sourcePresets) {
            CFStringRef name = preset.name ? CFStringCreateCopy(kCFAllocatorDefault, (__bridge CFStringRef)preset.name) : nullptr;
            storage->names.push_back(name);
            AUPreset out{};
            out.presetNumber = (SInt32)preset.number;
            out.presetName = name;
            storage->presets.push_back(out);
        }
        for (const AUPreset& preset : storage->presets) values.push_back(&preset);
        storage->array = CFArrayCreate(kCFAllocatorDefault,
                                       values.empty() ? nullptr : values.data(),
                                       (CFIndex)values.size(),
                                       nullptr);
    });

    return storage->array ? (CFArrayRef)CFRetain(storage->array) : nullptr;
}

static NSString* defaultFactoryPresetNameForNumber(NSInteger presetNumber,
                                                   ArpSID::ComponentFlavor flavor = ArpSID::ComponentFlavor::Hybrid) {
    const NSInteger normalized = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)presetNumber);
    const auto& defs = ArpSID::getFactoryPatchDefinitions();
    if ((size_t)normalized < defs.size()) {
        NSString* baseName = [NSString stringWithUTF8String:defs[(size_t)normalized].displayName.c_str()];
        if (baseName.length == 0) baseName = @"Factory Patch";
        return auv2FactoryPresetTitleForFlavor(flavor, normalized, baseName);
    }
    return @"Factory Patch";
}

static AUAudioUnitPreset* makeFactoryPresetObject(NSInteger presetNumber,
                                                  NSString* preferredName = nil,
                                                  ArpSID::ComponentFlavor flavor = ArpSID::ComponentFlavor::Hybrid) {
    AUAudioUnitPreset* preset = [[AUAudioUnitPreset alloc] init];
    if (!preset) return nil;
    const NSInteger normalized = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)presetNumber);
    preset.number = normalized;
    NSString* resolvedName = preferredName.length > 0 ? preferredName : defaultFactoryPresetNameForNumber(normalized, flavor);
    preset.name = resolvedName.length > 0 ? resolvedName : @"Factory Patch";
    return preset;
}

static NSArray<AUAudioUnitPreset*>* staticFactoryPresets(ArpSID::ComponentFlavor flavor) {
    NSMutableArray<AUAudioUnitPreset*>* presets = [NSMutableArray array];
    const auto& defs = ArpSID::getFactoryPatchDefinitions();
    for (size_t i = 0; i < defs.size(); ++i) {
        if (!auv2FactorySlotAllowedForFlavor(flavor, (NSInteger)i)) continue;
        AUAudioUnitPreset* preset = makeFactoryPresetObject((NSInteger)i, nil, flavor);
        if (preset) [presets addObject:preset];
    }
    return [presets copy];
}

struct ArpSIDAUv2Instance {
    struct PropertyListenerEntry {
        AudioUnitPropertyID propertyID = 0;
        AudioUnitPropertyListenerProc proc = nullptr;
        void* userData = nullptr;
    };

    struct RenderNotifyEntry {
        AURenderCallback proc = nullptr;
        void* userData = nullptr;
    };

    struct PublishedRenderNotifySlot {
        std::atomic<AURenderCallback> proc{nullptr};
        std::atomic<void*> userData{nullptr};
    };

    struct PublishedRenderNotifyTable {
        static constexpr UInt32 kCapacity = 64;
        // Even = stable snapshot, odd = non-RT publisher is mutating slots.
        // Render never calls through a slot unless it copied a fully stable
        // generation before and after the copy. This prevents proc/userData
        // pair tearing during add/remove without taking a render-thread lock.
        std::atomic<uint32_t> generation{0u};
        std::atomic<UInt32> count{0};
        std::array<PublishedRenderNotifySlot, kCapacity> entries{};
    };

    struct Auv2StateSnapshot {
        // AUv2 split-brain guard. This is the single render/UI-visible
        // mirror of the bridge state: preset authority, parameter generation,
        // render configuration generation and scratch/init readiness are
        // published together under a tiny seqlock. Readers never take locks.
        std::atomic<uint64_t> sequence{0u};
        std::atomic<uint64_t> parameterGeneration{0u};
        std::atomic<uint64_t> presetGeneration{0u};
        std::atomic<uint64_t> wrappedGeneration{0u};
        std::atomic<uint64_t> renderGeneration{0u};
        std::atomic<int> presetSlot{0};
        std::atomic<float> bankSlotNorm{ArpSID::canonicalNormalizedBankSlotValue(0)};
        std::atomic<float> programNorm{ArpSID::canonicalNormalizedProgramValue(0)};
        std::atomic<UInt32> maxFrames{0u};
        std::atomic<uint32_t> flags{0u};
    };

    enum Auv2StateSnapshotFlags : uint32_t {
        kAuv2StateInitialized = 1u << 0,
        kAuv2StateScratchReady = 1u << 1,
        kAuv2StateClosing = 1u << 2,
        kAuv2StatePresetApply = 1u << 3
    };

    AudioComponentInstance componentInstance = nullptr;
    AudioComponentDescription componentDescription{};
    __strong ArpSIDAudioUnit* audioUnit = nil;
    __strong ArpSIDDSPKernelAdapter* adapter = nil;
    AUInternalRenderBlock renderBlock = nil;
    std::atomic<void*> publishedRenderBlock{nullptr};
    std::atomic<void*> publishedKernel{nullptr};
    ArpSID::GUI::Auv2DiagnosticCounterAtomicTargets publishedAuv2DiagTargets{};
    HostCallbackInfo hostCallbacks{};
    AudioStreamBasicDescription outputFormat = makeCanonicalOutputFormat(kArpSIDDefaultSampleRate, 2);
    std::atomic<UInt32> maxFramesPerSlice{kAuv2DefaultMaxFrames};
    UInt32 renderQuality = kRenderQuality_Max;
    std::atomic<OSStatus> lastRenderError{noErr};
    // AUv2 v416: observable realtime/render diagnostics. These counters are
    // relaxed atomics only; they never allocate, lock, scan audio, or call host code.
    std::atomic<uint64_t> failClosedSilentSliceCount{0u};
    std::atomic<uint64_t> wrappedRenderDiagnosticCount{0u};
    std::atomic<uint64_t> renderNotifyDiagnosticCount{0u};
    std::atomic<uint64_t> splitBrainDiagnosticCount{0u};
    std::atomic<uint64_t> realtimeGuardViolationSliceCount{0u};
    // AUv2 render-notify dispatch is required by auval/Logic. This flag remains
    // ABI/diagnostic state only; callbacks are dispatched through the bounded
    // published table below, with violations attributed per callback.
    std::atomic<uint8_t>  strictRealtimeNotifyMode{1u};
    std::atomic<uint64_t> renderNotifySkippedUnstablePublicationCount{0u};
    // AUv2 v417: auval/Logic can legally hit render immediately after a
    // reset/slicing boundary while the wrapped AUv3 bridge is being republished.
    // Once the AUv2 call contract itself is valid, readiness holes must be
    // observable diagnostics + silence/noErr, never host-visible -10867.
    std::atomic<uint64_t> renderReadinessDiagnosticCount{0u};
    // AUv2 v418: render buffer preparation/scratch diagnostics. A valid AUv2
    // slice must not fail auval just because the wrapped AUv3 buffer bridge is
    // temporarily unable to use host memory directly; it becomes silence/noErr.
    std::atomic<uint64_t> renderOutputBufferDiagnosticCount{0u};
    bool inPlaceProcessing = false;
    bool offlineRender = false;
    std::atomic<bool> initialized{false};
    std::atomic<bool> outputScratchReady{false};
    std::atomic<uint64_t> renderConfigurationGeneration{0u};
    std::atomic<uint64_t> parameterAuthorityGeneration{0u};
    std::atomic<uint64_t> presetBridgeGeneration{0u};
    // Audit #1 — unified render epoch. Bumped alongside every state-change
    // path that already bumps `renderConfigurationGeneration` or
    // `wrappedStateGeneration`. componentRender() snapshots this epoch at
    // entry via `RenderEpochScope` and verifies on exit. If authority changes
    // after a successful wrapped render, preserve the completed audio and
    // report split-brain telemetry; only a failed render is silenced.
    ArpSID::RenderEpochCounter renderEpoch{};
    // Audit #2 — per-notify-callback RT-violation attribution counter.
    // Incremented by callAuv2RenderNotifyTable() with the thread-local
    // realtime-guard delta around each notify proc invocation. Lets
    // diagnostic tooling identify misbehaving host/client notify callbacks
    // (which AUv2 contractually invokes on the render thread).
    std::atomic<uint64_t> notifyCallbackViolationCount{0u};
    // Audit #4 — bounded grace-wait timeouts. Incremented when
    // publishRenderBlock() couldn't drain active render users within the
    // 50 ms deadline before ARC released the old strong block reference.
    // A non-zero value indicates a misbehaving host or a render that hangs.
    std::atomic<uint64_t> renderDrainTimeoutCount{0u};
    // Audit #5 — early-detected scratch under-capacity at render entry.
    // Incremented when the wrapper recognizes inNumberFrames exceeds the
    // pre-allocated scratch capacity before prepareOutputBufferList() can
    // fail mid-render. Exists alongside renderReadinessDiagnosticCount so
    // diagnostics can distinguish "scratch too small" from other readiness
    // failures.
    std::atomic<uint64_t> scratchUnderCapacityCount{0u};
    // fix-order #18 — AUv2 ramp expansion is lossy under parameter-intent queue
    // pressure. Count generated ramp anchors and the subset whose timed intent
    // could not be queued, so diagnostics can distinguish host ramp anchor-drop
    // from generic parameter ingress drops / dirty-flush fallback.
    std::atomic<uint64_t> auv2RampAnchorScheduledCount{0u};
    std::atomic<uint64_t> auv2RampAnchorDroppedCount{0u};
    // Retained diagnostic ABI field. Wrapper-side diversion is disabled, so
    // this remains zero.
    std::atomic<uint64_t> bridgeDivertedRenderCount{0u};
    // Audit #49 — pre-notify-callback failure count. Incremented when a host
    // or client render-notify callback returned non-noErr in the PreRender
    // phase. The legacy path used to re-invoke the notify table during the
    // error response (doubled callbacks); the fix-up suppresses that and
    // attributes the failure to this counter instead.
    std::atomic<uint64_t> preNotifyFailureCount{0u};
    // Audit #50 — host-buffer scratch attach count. Incremented whenever
    // prepareOutputBufferList() substitutes our owned scratch into a host
    // AudioBufferList whose mData was null or undersized. The audit warns
    // this can mutate host memory (mData/mDataByteSize/mNumberChannels);
    // surfacing the count lets diagnostics see how often that path fires
    // in practice instead of treating it as silent recovery.
    std::atomic<uint64_t> hostBufferScratchAttachCount{0u};
    Auv2StateSnapshot publishedState{};
    Auv2FixedFloatScratch ownedPlanarScratch;
    Auv2FixedFloatScratch ownedInterleavedScratch;
    // Audit #46 — all 4 instance mutexes are NonRealtime by policy.
    // The wrappers check `sidRealtimeGuardActive()` on every lock attempt;
    // an accidental render-thread acquisition increments
    // `*Mutex.rtViolationCount()` AND `sidRealtimeGuardRecordViolation`.
    // Production diagnostics can observe these counters to catch
    // refactor-introduced RT-touches before they become audible glitches.
    mutable ArpSID::AuditedActivityMutex         activityMutex; // non-RT configuration/state gate
    mutable ArpSID::AuditedStateMutex            stateMutex;
    ArpSID::AuditedPropertyListenerMutex         propertyListenerMutex;
    ArpSID::AuditedCloseWaitMutex                closeWaitMutex;
    // Audit #46 — `condition_variable_any` accepts any BasicLockable, so
    // we can pair it with `AuditedCloseWaitMutex` instead of plain std::mutex.
    std::condition_variable_any closeWaitCv;
    std::vector<PropertyListenerEntry> propertyListeners;
    std::vector<RenderNotifyEntry> renderNotifyListeners;
    PublishedRenderNotifyTable publishedRenderNotify{};
    std::array<std::atomic<float>, ArpSID::kNumParams> parameterCache{};
    std::atomic<uint32_t> activeUsers{0u};
    std::atomic<uint32_t> activeRenderUsers{0u};
    std::atomic<bool> presetApplyInProgress{false};
    std::atomic<bool> isClosing{false};
    NSInteger pinnedFactoryPresetNumber = 0;
    NSInteger pinnedBankSlotNumber = 0;
    NSInteger cachedPresentPresetNumber = 0;
    bool hasExplicitPresetSelection = false;
    __strong NSString* cachedPresentPresetName = nil;
    // Host-owned user preset (PresentPreset with a negative number): the host
    // keeps the state; we only report its name/number back. Cleared by any
    // genuine factory-preset selection.
    __strong NSString* userPresentPresetName = nil;
    std::atomic<int> pinnedPresetSlotCache{0};
    std::atomic<float> pinnedBankSlotParamCache{ArpSID::canonicalNormalizedBankSlotValue(0)};
    std::atomic<float> pinnedProgramParamCache{ArpSID::canonicalNormalizedProgramValue(0)};
    std::atomic<uint64_t> wrappedStateGeneration{0u};
    ArpSIDHostTransportSnapshotAccess hostTransportSnapshotAccess{};
    // AUv2/Logic realtime guard: legacy host transport callbacks are polled
    // from a non-render thread. componentRender() consumes only the atomics
    // behind hostTransportSnapshotAccess before it enters the AUv3 render block.
    std::atomic<bool> hostTransportPollerStop{false};
    std::atomic<bool> hostTransportPollerStarted{false};
    std::atomic<uint64_t> hostTransportPollCount{0u};
    std::thread hostTransportPoller{};


    std::atomic<double> nextRenderSampleTime{0.0};

    ArpSIDAUv2Instance() {
        for (int i = 0; i < ArpSID::kNumParams; ++i) {
            parameterCache[(size_t)i].store(ArpSID::defaultNormalizedParamValue(i), std::memory_order_release);
        }
    }
    ~ArpSIDAUv2Instance() = default;
};

struct Auv2StateSnapshotValue {
    uint64_t sequence = 0u;
    uint64_t parameterGeneration = 0u;
    uint64_t presetGeneration = 0u;
    uint64_t wrappedGeneration = 0u;
    uint64_t renderGeneration = 0u;
    int presetSlot = 0;
    float bankSlotNorm = ArpSID::canonicalNormalizedBankSlotValue(0);
    float programNorm = ArpSID::canonicalNormalizedProgramValue(0);
    UInt32 maxFrames = 0u;
    uint32_t flags = 0u;
};

static inline uint32_t auv2BuildStateFlags(const ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return 0u;
    uint32_t flags = 0u;
    if (impl->initialized.load(std::memory_order_acquire)) flags |= ArpSIDAUv2Instance::kAuv2StateInitialized;
    if (impl->outputScratchReady.load(std::memory_order_acquire)) flags |= ArpSIDAUv2Instance::kAuv2StateScratchReady;
    if (impl->isClosing.load(std::memory_order_acquire)) flags |= ArpSIDAUv2Instance::kAuv2StateClosing;
    if (impl->presetApplyInProgress.load(std::memory_order_acquire)) flags |= ArpSIDAUv2Instance::kAuv2StatePresetApply;
    return flags;
}

static void publishAuv2StateSnapshot(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return;
    // Split-brain guard: all mirror fields are published behind a seqlock.
    auto& snap = impl->publishedState;
    const uint64_t begin = snap.sequence.fetch_add(1u, std::memory_order_acq_rel) + 1u;
    (void)begin; // odd == publisher active
    std::atomic_thread_fence(std::memory_order_release);
    snap.parameterGeneration.store(impl->parameterAuthorityGeneration.load(std::memory_order_acquire), std::memory_order_release);
    snap.presetGeneration.store(impl->presetBridgeGeneration.load(std::memory_order_acquire), std::memory_order_release);
    snap.wrappedGeneration.store(impl->wrappedStateGeneration.load(std::memory_order_acquire), std::memory_order_release);
    snap.renderGeneration.store(impl->renderConfigurationGeneration.load(std::memory_order_acquire), std::memory_order_release);
    snap.presetSlot.store(impl->pinnedPresetSlotCache.load(std::memory_order_acquire), std::memory_order_release);
    snap.bankSlotNorm.store(impl->pinnedBankSlotParamCache.load(std::memory_order_acquire), std::memory_order_release);
    snap.programNorm.store(impl->pinnedProgramParamCache.load(std::memory_order_acquire), std::memory_order_release);
    snap.maxFrames.store(impl->maxFramesPerSlice.load(std::memory_order_acquire), std::memory_order_release);
    snap.flags.store(auv2BuildStateFlags(impl), std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    snap.sequence.fetch_add(1u, std::memory_order_acq_rel); // even == stable
}

static bool readAuv2StateSnapshotStable(const ArpSIDAUv2Instance* impl, Auv2StateSnapshotValue& out) noexcept {
    if (!impl) return false;
    const auto& snap = impl->publishedState;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const uint64_t s0 = snap.sequence.load(std::memory_order_acquire);
        if ((s0 & 1u) != 0u) continue;
        Auv2StateSnapshotValue v{};
        v.sequence = s0;
        v.parameterGeneration = snap.parameterGeneration.load(std::memory_order_acquire);
        v.presetGeneration = snap.presetGeneration.load(std::memory_order_acquire);
        v.wrappedGeneration = snap.wrappedGeneration.load(std::memory_order_acquire);
        v.renderGeneration = snap.renderGeneration.load(std::memory_order_acquire);
        v.presetSlot = snap.presetSlot.load(std::memory_order_acquire);
        v.bankSlotNorm = snap.bankSlotNorm.load(std::memory_order_acquire);
        v.programNorm = snap.programNorm.load(std::memory_order_acquire);
        v.maxFrames = snap.maxFrames.load(std::memory_order_acquire);
        v.flags = snap.flags.load(std::memory_order_acquire);
        const uint64_t s1 = snap.sequence.load(std::memory_order_acquire);
        if (s0 == s1 && (s1 & 1u) == 0u) { out = v; return true; }
    }
    return false;
}

static bool auv2BridgeSnapshotInvariantHolds(const Auv2StateSnapshotValue& v) noexcept {
    // central reader-side invariant for tests and defensive use. A render
    // snapshot is valid only when init/scratch/maxFrames agree and no preset
    // mutation is in progress. This is a cheap pure check; it takes no locks.
    const bool initialized = (v.flags & ArpSIDAUv2Instance::kAuv2StateInitialized) != 0u;
    const bool scratchReady = (v.flags & ArpSIDAUv2Instance::kAuv2StateScratchReady) != 0u;
    const bool closing = (v.flags & ArpSIDAUv2Instance::kAuv2StateClosing) != 0u;
    const bool presetApply = (v.flags & ArpSIDAUv2Instance::kAuv2StatePresetApply) != 0u;
    return !closing && !presetApply && initialized && scratchReady && v.maxFrames > 0u;
}

static inline void publishAuv2ParameterAuthorityChange(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return;
    impl->parameterAuthorityGeneration.fetch_add(1u, std::memory_order_acq_rel);
    publishAuv2StateSnapshot(impl);
}

static inline void publishAuv2PresetAuthorityChange(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return;
    impl->presetBridgeGeneration.fetch_add(1u, std::memory_order_acq_rel);
    impl->renderEpoch.bump();
    publishAuv2StateSnapshot(impl);
}

static inline void publishAuv2WrappedAuthorityChange(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return;
    // one canonical publisher for AUv3/wrapped-state authority changes.
    // Raw fetch_add sites are forbidden because they can update the generation
    // without updating the seqlock snapshot that render/UI readers consume.
    impl->wrappedStateGeneration.fetch_add(1u, std::memory_order_acq_rel);
    impl->renderEpoch.bump();
    publishAuv2StateSnapshot(impl);
}

static inline void auv2ZeroRenderedAudioBufferList(AudioBufferList* data, UInt32 frameCount, UInt32 renderedChannels) noexcept {
    if (!data || frameCount == 0) return;
    const UInt32 channels = std::max<UInt32>(1u, std::min<UInt32>(2u, renderedChannels));
    if (data->mNumberBuffers == 1) {
        AudioBuffer& b = data->mBuffers[0];
        if (!b.mData || b.mDataByteSize == 0) return;
        const UInt32 ch = std::min<UInt32>(std::max<UInt32>(1u, b.mNumberChannels), channels);
        const size_t bytes = std::min<size_t>((size_t)b.mDataByteSize, (size_t)frameCount * ch * sizeof(float));
        std::memset(b.mData, 0, bytes);
        return;
    }
    const UInt32 n = std::min<UInt32>(data->mNumberBuffers, channels);
    for (UInt32 i = 0; i < n; ++i) {
        AudioBuffer& b = data->mBuffers[i];
        if (b.mData && b.mDataByteSize > 0) {
            const size_t bytes = std::min<size_t>((size_t)b.mDataByteSize, (size_t)frameCount * sizeof(float));
            std::memset(b.mData, 0, bytes);
        }
    }
}

[[maybe_unused]] static inline void auv2ZeroAudioBufferList(AudioBufferList* data) noexcept {
    auv2ZeroRenderedAudioBufferList(data, UINT32_MAX / 8u, 2u);
}

static inline OSStatus auv2FailRender(ArpSIDAUv2Instance* impl,
                                      AudioUnitRenderActionFlags* flags,
                                      AudioBufferList* data,
                                      OSStatus status,
                                      UInt32 requestedFrames) noexcept {
    // v872: silence the ACTUAL host buffer, not just maxFramesPerSlice. Callers pass
    // inNumberFrames. auv2ZeroRenderedAudioBufferList clamps to mDataByteSize so this
    // is always in-bounds; taking the max of the requested and max frames means an
    // oversized (TooManyFramesToProcess) render is fully silenced instead of leaving a
    // non-zero tail past maxFrames, while normal failures still clear the whole buffer.
    const UInt32 maxFrames = impl ? sanitizeAuv2MaxFrames(impl->maxFramesPerSlice.load(std::memory_order_acquire)) : 0u;
    const UInt32 frames = std::max<UInt32>(requestedFrames, maxFrames);
    const UInt32 channels = impl ? std::max<UInt32>(1u, std::min<UInt32>(2u, impl->outputFormat.mChannelsPerFrame)) : 2u;
    auv2ZeroRenderedAudioBufferList(data, frames, channels);
    if (flags) *flags |= kAudioUnitRenderAction_OutputIsSilence;
    if (impl) impl->lastRenderError.store(status, std::memory_order_release);
    return status;
}



static inline OSStatus auv2FailClosedSilentNoErr(ArpSIDAUv2Instance* impl,
                                                 AudioUnitRenderActionFlags* flags,
                                                 AudioBufferList* data,
                                                 UInt32 frameCount,
                                                 UInt32 renderedChannels,
                                                 OSStatus diagnosticStatus,
                                                 std::atomic<uint64_t>* diagnosticCounter = nullptr) noexcept {
    auv2ZeroRenderedAudioBufferList(data, frameCount, renderedChannels);
    if (flags) *flags |= kAudioUnitRenderAction_OutputIsSilence;
    if (impl) {
        // Logic treats kAudioUnitProperty_LastRenderError as a host-visible
        // failure signal. This helper's contract is silence + noErr; keep the
        // diagnostic in counters only so recoverable startup/readiness pressure
        // does not surface as "plug-in reported a problem" in Logic.
        impl->lastRenderError.store(noErr, std::memory_order_release);
        impl->failClosedSilentSliceCount.fetch_add(1u, std::memory_order_relaxed);
    }
    if (diagnosticCounter) diagnosticCounter->fetch_add(1u, std::memory_order_relaxed);
    return noErr;
}

class Auv2RealtimeGuardSliceAudit final {
public:
    explicit Auv2RealtimeGuardSliceAudit(ArpSIDAUv2Instance* impl) noexcept
        : impl_(impl), violationsAtEntry_(ArpSID::sidRealtimeGuardViolationCount()) {}
    ~Auv2RealtimeGuardSliceAudit() noexcept {
        if (!impl_ || counted_) return;
        if (violationObserved()) {
            impl_->realtimeGuardViolationSliceCount.fetch_add(1u, std::memory_order_relaxed);
            // The rendered slice may already have returned noErr and valid audio.
            // Record the violation as telemetry, not as LastRenderError, because
            // Logic polls that property as a plug-in failure latch.
            impl_->lastRenderError.store(noErr, std::memory_order_release);
        }
    }
    bool violationObserved() const noexcept {
        return ArpSID::sidRealtimeGuardViolationCount() != violationsAtEntry_;
    }
    void markCounted() noexcept { counted_ = true; }
    Auv2RealtimeGuardSliceAudit(const Auv2RealtimeGuardSliceAudit&) = delete;
    Auv2RealtimeGuardSliceAudit& operator=(const Auv2RealtimeGuardSliceAudit&) = delete;
private:
    ArpSIDAUv2Instance* impl_ = nullptr;
    uint64_t violationsAtEntry_ = 0u;
    bool counted_ = false;
};

static inline ArpSIDAUv2Wrapper* wrapperFor(void* self) noexcept {
    return reinterpret_cast<ArpSIDAUv2Wrapper*>(self);
}

static inline ArpSIDAUv2Instance* implFor(void* self) noexcept {
    ArpSIDAUv2Wrapper* wrapper = wrapperFor(self);
    return wrapper ? wrapper->impl : nullptr;
}

static void endInstanceUse(ArpSIDAUv2Instance* impl) noexcept;

static bool beginInstanceUse(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return false;
    uint32_t active = impl->activeUsers.load(std::memory_order_acquire);
    for (;;) {
        // Close is a one-way gate. The pre-CAS check rejects all new users once
        // componentClose() publishes isClosing=true. The post-CAS check closes
        // the only remaining race: a user that increments activeUsers after the
        // pre-check but before observing the close publication immediately drops
        // its reference, wakes the closer, and returns false. This keeps render,
        // property, and UI entrypoints from entering teardown-owned state without
        // taking a render-thread lock or relying on ObjC object lifetime.
        if (impl->isClosing.load(std::memory_order_acquire)) return false;
        if (impl->activeUsers.compare_exchange_weak(active,
                                                    active + 1u,
                                                    std::memory_order_acq_rel,
                                                    std::memory_order_acquire)) {
            if (impl->isClosing.load(std::memory_order_acquire)) {
                endInstanceUse(impl);
                return false;
            }
            return true;
        }
    }
}

static void endInstanceUse(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return;
    const uint32_t prev = impl->activeUsers.fetch_sub(1u, std::memory_order_acq_rel);
    if (prev == 1u) {
        // componentRender owns ScopedInstanceUse; this is a potential render-thread
        // destructor path. Do not take closeWaitMutex here; the waiting side
        // owns the mutex/predicate and notify_all() itself does not require it.
        impl->closeWaitCv.notify_all();
    }
}

class ScopedInstanceUse {
public:
    explicit ScopedInstanceUse(void* self) noexcept
    : impl_(implFor(self)) {
        if (!beginInstanceUse(impl_)) impl_ = nullptr;
    }

    ~ScopedInstanceUse() {
        endInstanceUse(impl_);
    }

    ArpSIDAUv2Instance* get() const noexcept { return impl_; }
    explicit operator bool() const noexcept { return impl_ != nullptr; }

private:
    ArpSIDAUv2Instance* impl_ = nullptr;
};

static ArpSIDAudioUnit* retainedAudioUnitForInstance(ArpSIDAUv2Instance* impl) {
    if (!impl) return nil;
    if (impl->isClosing.load(std::memory_order_acquire)) return nil;
    std::lock_guard<ArpSID::AuditedStateMutex> lock(impl->stateMutex);
    if (impl->isClosing.load(std::memory_order_acquire)) return nil;
    ArpSIDAudioUnit* audioUnit = impl->audioUnit;
    return audioUnit;
}

static ArpSIDDSPKernelAdapter* retainedAdapterForInstance(ArpSIDAUv2Instance* impl) {
    if (!impl) return nil;
    if (impl->isClosing.load(std::memory_order_acquire)) return nil;
    std::lock_guard<ArpSID::AuditedStateMutex> lock(impl->stateMutex);
    if (impl->isClosing.load(std::memory_order_acquire)) return nil;
    ArpSIDDSPKernelAdapter* adapter = impl->adapter;
    return adapter;
}

static void cachePresentPresetState(ArpSIDAUv2Instance* impl,
                                    NSInteger presetNumber,
                                    NSString* presetName,
                                    bool explicitSelection = true);

static void setUserPresentPresetName(ArpSIDAUv2Instance* impl, NSString* name);

static NSInteger cachedPinnedPresetSlotForInstance(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return 0;
    return (NSInteger)ArpSID::normalizeFactoryPatchSlot(
        impl->pinnedPresetSlotCache.load(std::memory_order_acquire));
}

static bool readStickyPresetSlotFromWrappedAudioUnit(ArpSIDAUv2Instance* impl, NSInteger* outSlot) {
    if (outSlot) *outSlot = -1;
    if (!impl) return false;
    ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
    if (!audioUnit) return false;
    if (![audioUnit respondsToSelector:@selector(hasStickyUserFactoryPresetSelection)] ||
        ![audioUnit respondsToSelector:@selector(stickyFactoryPresetNumber)] ||
        ![audioUnit hasStickyUserFactoryPresetSelection]) {
        return false;
    }
    const NSInteger slot = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)[audioUnit stickyFactoryPresetNumber]);
    if (outSlot) *outSlot = slot;
    return true;
}

static bool synchronizeAuv2PresetCacheFromWrappedAudioUnit(ArpSIDAUv2Instance* impl) {
    NSInteger slot = -1;
    if (!readStickyPresetSlotFromWrappedAudioUnit(impl, &slot)) return false;
    if ((NSInteger)ArpSID::normalizeFactoryPatchSlot((int)slot) != cachedPinnedPresetSlotForInstance(impl)) {
        setUserPresentPresetName(impl, nil); // GUI picked a different factory preset
    }
    cachePresentPresetState(impl, slot, defaultFactoryPresetNameForNumber(slot, componentFlavorForInstance(impl)), true);
    return true;
}

static float pinnedPresetParameterValueForInstance(ArpSIDAUv2Instance* impl,
                                                   AudioUnitParameterID pid) noexcept;

static inline float cachedParameterValueForInstance(const ArpSIDAUv2Instance* impl,
                                                    AudioUnitParameterID pid) noexcept {
    if (!impl || pid >= (AudioUnitParameterID)ArpSID::kNumParams) {
        return ArpSID::defaultNormalizedParamValue((int)pid);
    }
    return impl->parameterCache[(size_t)pid].load(std::memory_order_acquire);
}

static inline void cacheParameterValueForInstance(ArpSIDAUv2Instance* impl,
                                                  AudioUnitParameterID pid,
                                                  float value,
                                                  bool publishAuthority = true) noexcept {
    if (!impl || pid >= (AudioUnitParameterID)ArpSID::kNumParams) return;
    const float clean = ArpSID::sanitizeNormalizedParamValue((int)pid,
                                                             value,
                                                             ArpSID::defaultNormalizedParamValue((int)pid));
    impl->parameterCache[(size_t)pid].store(clean, std::memory_order_release);
    if (publishAuthority) publishAuv2ParameterAuthorityChange(impl);
}

static inline void publishAuv2ParameterAuthorityBatchComplete(ArpSIDAUv2Instance* impl) noexcept {
    // batch AUv2 mirror refreshes into one generation. This prevents
    // GetParameter/render/UI readers from observing a half-restored parameter
    // mirror while ClassInfo, reset, or wrapped AUv3 synchronization is running.
    publishAuv2ParameterAuthorityChange(impl);
}

static void refreshParameterCacheFromAudioUnit(ArpSIDAUv2Instance* impl) {
    ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
    if (!impl || !audioUnit) return;
    for (int pid = 0; pid < ArpSID::kNumParams; ++pid) {
        if (pid == ArpSID::kParamProgram || pid == ArpSID::kParamBankSlot) {
            cacheParameterValueForInstance(impl,
                                           (AudioUnitParameterID)pid,
                                           pinnedPresetParameterValueForInstance(impl, (AudioUnitParameterID)pid),
                                           false);
            continue;
        }
        cacheParameterValueForInstance(impl,
                                       (AudioUnitParameterID)pid,
                                       [audioUnit getParameterValue:pid],
                                       false);
    }
    publishAuv2ParameterAuthorityBatchComplete(impl);
}

using Auv2ParameterSnapshot = std::array<float, ArpSID::kNumParams>;

static Auv2ParameterSnapshot snapshotParameterCacheForReset(const ArpSIDAUv2Instance* impl) noexcept {
    Auv2ParameterSnapshot snap{};
    for (int pid = 0; pid < ArpSID::kNumParams; ++pid) {
        snap[(size_t)pid] = cachedParameterValueForInstance(impl, (AudioUnitParameterID)pid);
    }
    return snap;
}

static void restoreParameterCacheAfterReset(ArpSIDAUv2Instance* impl,
                                            ArpSIDDSPKernelAdapter* adapter,
                                            const Auv2ParameterSnapshot& snap) noexcept {
    if (!impl) return;
    for (int pid = 0; pid < ArpSID::kNumParams; ++pid) {
        const AudioUnitParameterID auPid = (AudioUnitParameterID)pid;
        const float value = (pid == ArpSID::kParamProgram || pid == ArpSID::kParamBankSlot)
            ? pinnedPresetParameterValueForInstance(impl, auPid)
            : snap[(size_t)pid];
        cacheParameterValueForInstance(impl, auPid, value, false);
    }
    publishAuv2ParameterAuthorityBatchComplete(impl);
    if (adapter) {
        Auv2ParameterSnapshot adapterSnap = snap;
        adapterSnap[(size_t)ArpSID::kParamBankSlot] = pinnedPresetParameterValueForInstance(impl, (AudioUnitParameterID)ArpSID::kParamBankSlot);
        adapterSnap[(size_t)ArpSID::kParamProgram] = pinnedPresetParameterValueForInstance(impl, (AudioUnitParameterID)ArpSID::kParamProgram);
        [adapter restoreParameterSnapshot:adapterSnap.data() count:ArpSID::kNumParams];
    }
}

static void cachePresentPresetState(ArpSIDAUv2Instance* impl,
                                    NSInteger presetNumber,
                                    NSString* presetName,
                                    bool explicitSelection) {
    if (!impl) return;
    const ArpSID::ComponentFlavor flavor = componentFlavorForInstance(impl);
    const NSInteger requested = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)presetNumber);
    const NSInteger normalized = auv2FactorySlotAllowedForFlavor(flavor, requested)
        ? requested
        : auv2StartupFactorySlotForFlavor(flavor);
    NSString* resolvedName = presetName.length > 0 ? [presetName copy]
                                                   : [defaultFactoryPresetNameForNumber(normalized, flavor) copy];
    {
        std::lock_guard<ArpSID::AuditedStateMutex> lock(impl->stateMutex);
        impl->pinnedFactoryPresetNumber = normalized;
        impl->pinnedBankSlotNumber = normalized;
        impl->cachedPresentPresetNumber = normalized;
        if (explicitSelection) impl->hasExplicitPresetSelection = true;
        impl->cachedPresentPresetName = resolvedName.length > 0 ? resolvedName : @"Factory Patch";
    }
    const float bankNorm = ArpSID::canonicalNormalizedBankSlotValue((int)normalized);
    const float programNorm = ArpSID::canonicalNormalizedFactoryProgramValue((int)normalized);
    impl->pinnedPresetSlotCache.store((int)normalized, std::memory_order_release);
    impl->pinnedBankSlotParamCache.store(bankNorm, std::memory_order_release);
    impl->pinnedProgramParamCache.store(programNorm, std::memory_order_release);
    // mirror explicit preset selection into AUv2 parameter readback so the
    // visible patch number cannot be overwritten by later host BankSlot replay.
    cacheParameterValueForInstance(impl, (AudioUnitParameterID)ArpSID::kParamBankSlot, bankNorm, false);
    cacheParameterValueForInstance(impl, (AudioUnitParameterID)ArpSID::kParamProgram, programNorm, false);
    publishAuv2ParameterAuthorityBatchComplete(impl);
    publishAuv2PresetAuthorityChange(impl);
}

static void cachePresentPresetStateForPreset(ArpSIDAUv2Instance* impl, AUAudioUnitPreset* preset) {
    if (!preset) {
        cachePresentPresetState(impl, auv2StartupFactorySlotForFlavor(componentFlavorForInstance(impl)), nil, false);
        return;
    }
    cachePresentPresetState(impl, preset.number, preset.name);
}

static void cacheParameterValuesFromFactoryPresetRoot(ArpSIDAUv2Instance* impl, NSInteger presetNumber) {
    if (!impl) return;
    const NSInteger normalized = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)presetNumber);
    ArpSID::SidStateRootV1 root = ArpSID::makeFactoryPatchStateRootForSlot((int)normalized);
    if (!root.valid()) return;
    applyAuv2ComponentFlavorPolicyToStateRoot(componentFlavorForInstance(impl), root);

    std::array<float, ArpSID::kNumParams> params{};
    ArpSID::exportPersistentPresentationParamsFromStateRoot(root, params.data(), ArpSID::kNumParams);
    for (int pid = 0; pid < ArpSID::kNumParams; ++pid) {
        const AudioUnitParameterID auPid = (AudioUnitParameterID)pid;
        const float value = (pid == ArpSID::kParamProgram || pid == ArpSID::kParamBankSlot)
            ? pinnedPresetParameterValueForInstance(impl, auPid)
            : params[(size_t)pid];
        cacheParameterValueForInstance(impl, auPid, value, false);
    }
    publishAuv2ParameterAuthorityBatchComplete(impl);
}

static void snapshotPresentPresetState(ArpSIDAUv2Instance* impl,
                                      NSInteger* outPresetNumber,
                                      NSString** outPresetName,
                                      bool* outHasExplicitSelection = nullptr,
                                      bool* outIsUserPreset = nullptr) {
    if (outPresetNumber) *outPresetNumber = 0;
    if (outPresetName) *outPresetName = @"";
    if (outIsUserPreset) *outIsUserPreset = false;
    if (!impl) return;
    std::lock_guard<ArpSID::AuditedStateMutex> lock(impl->stateMutex);
    if (outPresetNumber) *outPresetNumber = impl->cachedPresentPresetNumber;
    if (outHasExplicitSelection) *outHasExplicitSelection = impl->hasExplicitPresetSelection;
    const bool isUserPreset = impl->userPresentPresetName.length > 0;
    if (outIsUserPreset) *outIsUserPreset = isUserPreset;
    if (outPresetName) {
        NSString* presetName = isUserPreset ? impl->userPresentPresetName : impl->cachedPresentPresetName;
        *outPresetName = presetName.length > 0 ? presetName : @"";
    }
}

static void setUserPresentPresetName(ArpSIDAUv2Instance* impl, NSString* name) {
    if (!impl) return;
    std::lock_guard<ArpSID::AuditedStateMutex> lock(impl->stateMutex);
    impl->userPresentPresetName = name.length > 0 ? [name copy] : nil;
}


static float pinnedPresetParameterValueForInstance(ArpSIDAUv2Instance* impl,
                                                   AudioUnitParameterID pid) noexcept {
    // RT-safe/automation-safe readback path: do not take stateMutex and do not
    // query AUAudioUnit sticky state from GetParameter. Non-RT preset/property
    // paths publish these atomics via cachePresentPresetState().
    if (!impl) return ArpSID::defaultNormalizedParamValue((int)pid);
    if (pid == (AudioUnitParameterID)ArpSID::kParamBankSlot) {
        return impl->pinnedBankSlotParamCache.load(std::memory_order_acquire);
    }
    if (pid == (AudioUnitParameterID)ArpSID::kParamProgram) {
        return impl->pinnedProgramParamCache.load(std::memory_order_acquire);
    }
    return ArpSID::defaultNormalizedParamValue((int)pid);
}

static inline bool tryBeginPresetBridgeApply(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return false;
    bool expected = false;
    const bool accepted = impl->presetApplyInProgress.compare_exchange_strong(expected,
                                                                              true,
                                                                              std::memory_order_acq_rel,
                                                                              std::memory_order_acquire);
    if (accepted) publishAuv2PresetAuthorityChange(impl);
    if (!accepted) {
        NSLog(@"[ArpSIDAUv2] preset bridge apply skipped because another preset apply is already in progress");
    }
    return accepted;
}

static inline void endPresetBridgeApply(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return;
    impl->presetApplyInProgress.store(false, std::memory_order_release);
    publishAuv2PresetAuthorityChange(impl);
}


static bool withPresetBridgeApply(ArpSIDAUv2Instance* impl,
                                  bool assumeAlreadyActive,
                                  void (^body)(void)) {
    if (!impl || !body) return false;
    bool acquired = false;
    if (!assumeAlreadyActive) {
        if (!tryBeginPresetBridgeApply(impl)) return false;
        acquired = true;
    }
    @try {
        body();
    } @finally {
        if (acquired) endPresetBridgeApply(impl);
    }
    return true;
}

static bool setAudioUnitPresetMetadataOnlySafely(ArpSIDAUv2Instance* impl,
                                                AUAudioUnitPreset* preset,
                                                bool assumeAlreadyActive = false) {
    if (!impl || !preset) return false;
    cachePresentPresetStateForPreset(impl, preset);
    const NSInteger resolvedSlot = cachedPinnedPresetSlotForInstance(impl);
    ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
    if (!audioUnit) return false;
    return withPresetBridgeApply(impl, assumeAlreadyActive, ^{
        // AUv2 metadata bootstrap/restores must publish the selected slot
        // as restored sticky authority. The old metadata-only API updated labels
        // but did not necessarily protect the AUAudioUnit side against later
        // host currentPreset/PresentPreset replay.
        if ([audioUnit respondsToSelector:@selector(setRestoredFactoryPresetMetadataOnlyForSlot:)]) {
            [audioUnit setRestoredFactoryPresetMetadataOnlyForSlot:resolvedSlot];
        } else if ([audioUnit respondsToSelector:@selector(setCurrentFactoryPresetMetadataOnlyForSlot:)]) {
            [audioUnit setCurrentFactoryPresetMetadataOnlyForSlot:resolvedSlot];
        }
    });
}

static bool readCurrentHostTransportState(ArpSIDAUv2Instance* impl,
                                          AUHostTransportStateFlags* outFlags,
                                          double* outCurrentSample,
                                          double* outCycleStartSample,
                                          double* outCycleEndSample) noexcept;

static bool auv2HostTransportIsMoving(ArpSIDAUv2Instance* impl) noexcept {
    AUHostTransportStateFlags flags = 0;
    if (!readCurrentHostTransportState(impl, &flags, nullptr, nullptr, nullptr)) return false;
    return (flags & AUHostTransportStateMoving) != 0;
}


static AUAudioUnitPreset* makePinnedFactoryPresetObject(ArpSIDAUv2Instance* impl) {
    if (!impl) return makeFactoryPresetObject(0, nil);
    NSInteger presetNumber = 0;
    NSString* presetName = nil;
    snapshotPresentPresetState(impl, &presetNumber, &presetName);
    const ArpSID::ComponentFlavor flavor = componentFlavorForInstance(impl);
    if (presetName.length == 0) presetName = defaultFactoryPresetNameForNumber(presetNumber, flavor);
    return makeFactoryPresetObject(presetNumber, presetName, flavor);
}


static bool fillPresentPresetForInstance(ArpSIDAUv2Instance* impl, AUPreset* outPreset) noexcept {
    if (!impl || !outPreset) return false;
    std::memset(outPreset, 0, sizeof(*outPreset));
    // PresentPreset readback must use the same sticky authority as
    // kParamBankSlot/kParamProgram readback. Reading only AUv2's cached mirror
    // can show stale slot 0 after GUI-side sticky selection.
    const NSInteger presetNumber = cachedPinnedPresetSlotForInstance(impl);
    NSString* presetName = nil;
    bool isUserPreset = false;
    snapshotPresentPresetState(impl, nullptr, &presetName, nullptr, &isUserPreset);
    if (presetName.length == 0) presetName = defaultFactoryPresetNameForNumber(presetNumber, componentFlavorForInstance(impl));
    outPreset->presetNumber = isUserPreset ? (SInt32)-1 : (SInt32)presetNumber;
    outPreset->presetName = (__bridge_retained CFStringRef)[(presetName.length > 0 ? presetName : @"") copy];
    return true;

}

static inline void resetPendingRenderState(ArpSIDAUv2Instance* impl) noexcept {
    // AUv2 no longer owns a private render-event queue. Parameter and MIDI
    // events are pushed into the canonical kernel intent/ingress queues at the
    // host entrypoint. This function intentionally only resets AUv2's synthetic
    // render timestamp fallback.
    if (!impl) return;
    impl->nextRenderSampleTime.store(0.0, std::memory_order_release);
}

static void publishRenderNotifyTableLocked(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return;
    auto& table = impl->publishedRenderNotify;
    // Single-writer seqlock publication. This is non-RT only: propertyListenerMutex
    // serializes publishers. Render snapshots the table only when generation is
    // stable/even before and after copying.
    uint32_t gen = table.generation.load(std::memory_order_relaxed);
    if ((gen & 1u) != 0u) ++gen;
    table.generation.store(gen + 1u, std::memory_order_release);

    const UInt32 count = (UInt32)std::min<size_t>(impl->renderNotifyListeners.size(),
                                                  ArpSIDAUv2Instance::PublishedRenderNotifyTable::kCapacity);
    for (UInt32 i = 0; i < ArpSIDAUv2Instance::PublishedRenderNotifyTable::kCapacity; ++i) {
        // First clear the callable pointer. A concurrent render that started on
        // the old generation either copies the old coherent pair before this, or
        // sees nullptr and skips. It must never see old proc + new userData.
        table.entries[(size_t)i].proc.store(nullptr, std::memory_order_release);
    }
    for (UInt32 i = 0; i < ArpSIDAUv2Instance::PublishedRenderNotifyTable::kCapacity; ++i) {
        AURenderCallback proc = nullptr;
        void* user = nullptr;
        if (i < count) {
            proc = impl->renderNotifyListeners[(size_t)i].proc;
            user = impl->renderNotifyListeners[(size_t)i].userData;
        }
        // Audit #3 fix: pair userData and proc with the same release ordering.
        // The previous relaxed store on userData required readers to rely on the
        // outer seqlock retry for per-slot coherence; pairing release ↔ acquire
        // on the (userData, proc) pair removes any per-slot torn-pair window
        // even before the seqlock retry kicks in.
        table.entries[(size_t)i].userData.store(user, std::memory_order_release);
        table.entries[(size_t)i].proc.store(proc, std::memory_order_release);
    }
    table.count.store(count, std::memory_order_release);
    // Make the publication barrier explicit: all entry userData/proc stores and
    // count must be visible before render sees the final even generation. The
    // final release store is sufficient, this fence documents and hardens the
    // seqlock contract for future edits.
    std::atomic_thread_fence(std::memory_order_release);
    table.generation.store(gen + 2u, std::memory_order_release);
}

// ─── AUv2 render-notify dispatch ─────────────────────────────────────────────
// auval and Logic expect AudioUnitAddRenderNotify to succeed and for registered
// callbacks to be called around render. Registration publishes into a fixed-size
// seqlock table on the non-RT path; componentRender() snapshots that table with
// no locking or allocation and attributes any realtime-guard violations to the
// offending callback.
static OSStatus callAuv2RenderNotifyTable(ArpSIDAUv2Instance* impl,
                                        AudioUnitRenderActionFlags* flags,
                                        const AudioTimeStamp* timestamp,
                                        UInt32 bus,
                                        UInt32 frames,
                                        AudioBufferList* data) noexcept {
    if (!impl) return noErr;
    auto& table = impl->publishedRenderNotify;
    const uint32_t gen0 = table.generation.load(std::memory_order_acquire);
    if ((gen0 & 1u) != 0u) {
        impl->renderNotifySkippedUnstablePublicationCount.fetch_add(1u, std::memory_order_relaxed);
        return noErr;
    }

    struct LocalNotify {
        AURenderCallback proc = nullptr;
        void* userData = nullptr;
    };
    std::array<LocalNotify, ArpSIDAUv2Instance::PublishedRenderNotifyTable::kCapacity> local{};
    const UInt32 count = std::min<UInt32>(
        table.count.load(std::memory_order_acquire),
        ArpSIDAUv2Instance::PublishedRenderNotifyTable::kCapacity);
    for (UInt32 i = 0; i < count; ++i) {
        local[(size_t)i].proc = table.entries[(size_t)i].proc.load(std::memory_order_acquire);
        local[(size_t)i].userData = table.entries[(size_t)i].userData.load(std::memory_order_acquire);
    }

    const uint32_t gen1 = table.generation.load(std::memory_order_acquire);
    if (gen0 != gen1 || (gen1 & 1u) != 0u) {
        impl->renderNotifySkippedUnstablePublicationCount.fetch_add(1u, std::memory_order_relaxed);
        return noErr;
    }

    OSStatus firstError = noErr;
    for (UInt32 i = 0; i < count; ++i) {
        AURenderCallback proc = local[(size_t)i].proc;
        if (!proc) continue;
        const uint64_t violationsBefore = ArpSID::sidRealtimeGuardViolationCount();
        const OSStatus status = proc(local[(size_t)i].userData, flags, timestamp, bus, frames, data);
        const uint64_t violationsAfter = ArpSID::sidRealtimeGuardViolationCount();
        if (violationsAfter > violationsBefore) {
            impl->notifyCallbackViolationCount.fetch_add(violationsAfter - violationsBefore,
                                                         std::memory_order_relaxed);
        }
        if (status != noErr && firstError == noErr) firstError = status;
    }
    return firstError;
}


static inline void waitForRenderUsersToDrainBounded_(ArpSIDAUv2Instance* impl,
                                                     int spinMicrosBeforeYield = 100) noexcept {
    if (!impl) return;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    int spins = 0;
    while (impl->activeRenderUsers.load(std::memory_order_acquire) != 0u) {
        if (std::chrono::steady_clock::now() >= deadline) {
            impl->renderDrainTimeoutCount.fetch_add(1u, std::memory_order_relaxed);
            return;
        }
        if (spins < spinMicrosBeforeYield) {
            ++spins;
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
}

static inline void publishRenderBlock(ArpSIDAUv2Instance* impl, AUInternalRenderBlock block) noexcept {
    if (!impl) return;
    if (block) {
        // Audit #4: defer ARC release of the prior block. Stash the existing
        // strong reference in a local so ARC does NOT decrement its refcount
        // when `impl->renderBlock = block` overwrites the slot — the local
        // keeps the old block alive until we've drained any in-flight render
        // that may still be dereferencing its raw pointer.
        __strong AUInternalRenderBlock deferredOldBlock = impl->renderBlock;  // ARC retain on assignment
        impl->renderBlock = block;
        std::atomic_thread_fence(std::memory_order_release);
        impl->publishedRenderBlock.store((__bridge void*)block, std::memory_order_release);
        // Bounded grace wait — if any render is currently calling the OLD raw
        // pointer it must finish before our `deferredOldBlock` goes out of
        // scope (and ARC finally releases the strong reference).
        waitForRenderUsersToDrainBounded_(impl);
        (void)deferredOldBlock; // scope-end ARC release happens here
    } else {
        // Audit #4: withdraw raw RT pointer first, THEN bounded-wait for any
        // in-flight render before releasing the strong reference. Without this
        // wait, render could be holding a raw pointer it acquired before the
        // nullptr store while ARC simultaneously deallocates the block.
        impl->publishedRenderBlock.store(nullptr, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_acq_rel);
        waitForRenderUsersToDrainBounded_(impl);
        impl->renderBlock = nil;
    }
}

static inline AUInternalRenderBlock loadPublishedRenderBlock(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return nil;
    void* raw = impl->publishedRenderBlock.load(std::memory_order_acquire);
    if (!raw) return nil;
    return (__bridge AUInternalRenderBlock)raw;
}

static inline void publishKernelPointer(ArpSIDAUv2Instance* impl, ArpSID::ArpSIDDSPKernel* kernel) noexcept {
    if (!impl) return;
    impl->publishedKernel.store(reinterpret_cast<void*>(kernel), std::memory_order_release);
}

static inline ArpSID::ArpSIDDSPKernel* loadPublishedKernelPointer(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return nullptr;
    return reinterpret_cast<ArpSID::ArpSIDDSPKernel*>(impl->publishedKernel.load(std::memory_order_acquire));
}

// v549: RT-safe adapter access for diagnostic counter push from render thread.
static inline void publishAdapterPointer(ArpSIDAUv2Instance* impl,
                                         ArpSIDDSPKernelAdapter* __unsafe_unretained adapter) noexcept {
    if (!impl) return;
    impl->publishedAuv2DiagTargets = adapter
        ? [adapter auv2DiagnosticCounterAtomicTargets]
        : ArpSID::GUI::Auv2DiagnosticCounterAtomicTargets{};
}

// AUv2 host callback bridge helpers. The shared AUv3 render block remains
// callback-free and reads only a published atomic transport snapshot. The AUv2
// shim is responsible for translating legacy HostCallbackInfo state into those
// atomics at the render boundary and into AUAudioUnit-compatible blocks outside
// render for the rest of the stack.
static inline bool callbacks_have_beat_and_tempo(const HostCallbackInfo& callbacks) noexcept {
    return callbacks.beatAndTempoProc != nullptr;
}

static bool readCurrentHostBeatAndTempoFromCallbacks(const HostCallbackInfo& callbacks,
                                                     double* outBeat,
                                                     double* outTempo) noexcept {
    // Default to NaN (not zero) so the caller can distinguish "host didn't
    // populate this field" from "host explicitly reported beat 0 / tempo 0".
    // The publisher uses NaN as a 'no data' sentinel to preserve the previous
    // valid snapshot value instead of snapping beat to 0 on partial callbacks.
    if (outBeat) *outBeat = std::numeric_limits<double>::quiet_NaN();
    if (outTempo) *outTempo = std::numeric_limits<double>::quiet_NaN();
    if (!callbacks.beatAndTempoProc) return false;
    Float64 beat = 0.0;
    Float64 tempo = 0.0;
    const OSStatus status = callbacks.beatAndTempoProc(callbacks.hostUserData, &beat, &tempo);
    if (status != noErr) return false;
    if (outBeat && std::isfinite(beat) && beat >= 0.0) *outBeat = beat;
    if (outTempo && std::isfinite(tempo) && tempo >= 1.0 && tempo <= 1000.0) *outTempo = tempo;
    return true;
}

static bool readCurrentHostMusicalTimeLocationFromCallbacks(const HostCallbackInfo& callbacks,
                                                            NSInteger* outSampleOffsetToNextBeat,
                                                            double* outTimeSigNum,
                                                            NSInteger* outTimeSigDen,
                                                            double* outMeasureDownbeat) noexcept {
    if (outSampleOffsetToNextBeat) *outSampleOffsetToNextBeat = 0;
    if (outTimeSigNum) *outTimeSigNum = 4.0;
    if (outTimeSigDen) *outTimeSigDen = 4;
    if (outMeasureDownbeat) *outMeasureDownbeat = 0.0;
    if (!callbacks.musicalTimeLocationProc) return false;
    UInt32 sampleOffsetToNextBeat = 0;
    Float32 timeSigNum = 4.0f;
    UInt32 timeSigDen = 4u;
    Float64 measureDownbeat = 0.0;
    const OSStatus status = callbacks.musicalTimeLocationProc(callbacks.hostUserData,
                                                              &sampleOffsetToNextBeat,
                                                              &timeSigNum,
                                                              &timeSigDen,
                                                              &measureDownbeat);
    if (status != noErr) return false;
    if (outSampleOffsetToNextBeat) *outSampleOffsetToNextBeat = (NSInteger)sampleOffsetToNextBeat;
    if (outTimeSigNum && std::isfinite((double)timeSigNum) && timeSigNum >= 1.0f && timeSigNum <= 64.0f) {
        *outTimeSigNum = (double)timeSigNum;
    }
    if (outTimeSigDen && timeSigDen >= 1u && timeSigDen <= 64u) *outTimeSigDen = (NSInteger)timeSigDen;
    if (outMeasureDownbeat && std::isfinite(measureDownbeat) && measureDownbeat >= 0.0) *outMeasureDownbeat = measureDownbeat;
    return true;
}

static bool readCurrentHostTransportStateFromCallbacks(const HostCallbackInfo& callbacks,
                                                       AUHostTransportStateFlags* outFlags,
                                                       double* outCurrentSample,
                                                       double* outCycleStartBeat,
                                                       double* outCycleEndBeat) noexcept {
    if (outFlags) *outFlags = 0;
    if (outCurrentSample) *outCurrentSample = 0.0;
    if (outCycleStartBeat) *outCycleStartBeat = 0.0;
    if (outCycleEndBeat) *outCycleEndBeat = 0.0;

    Boolean isPlaying = false;
    Boolean isRecording = false;
    Boolean transportChanged = false;
    Float64 currentSample = 0.0;
    Boolean isCycling = false;
    Float64 cycleStartBeat = 0.0;
    Float64 cycleEndBeat = 0.0;
    OSStatus status = kAudioUnitErr_NoConnection;

    if (callbacks.transportStateProc2) {
        status = callbacks.transportStateProc2(callbacks.hostUserData,
                                               &isPlaying,
                                               &isRecording,
                                               &transportChanged,
                                               &currentSample,
                                               &isCycling,
                                               &cycleStartBeat,
                                               &cycleEndBeat);
    } else if (callbacks.transportStateProc) {
        status = callbacks.transportStateProc(callbacks.hostUserData,
                                              &isPlaying,
                                              &transportChanged,
                                              &currentSample,
                                              &isCycling,
                                              &cycleStartBeat,
                                              &cycleEndBeat);
    }
    if (status != noErr) return false;

    AUHostTransportStateFlags flags = 0;
    if (isPlaying) flags |= AUHostTransportStateMoving;
    if (isRecording) flags |= AUHostTransportStateRecording;
    if (transportChanged) flags |= AUHostTransportStateChanged;
    if (isCycling) flags |= AUHostTransportStateCycling;
    if (outFlags) *outFlags = flags;
    if (outCurrentSample && std::isfinite(currentSample)) *outCurrentSample = currentSample;
    if (outCycleStartBeat && std::isfinite(cycleStartBeat)) *outCycleStartBeat = cycleStartBeat;
    if (outCycleEndBeat && std::isfinite(cycleEndBeat)) *outCycleEndBeat = cycleEndBeat;
    return true;
}

[[maybe_unused]] static bool readPublishedHostMusicalContext(ArpSIDAUv2Instance* impl,
                                            double* outBeat,
                                            double* outTempo,
                                            double* outTimeSigNum,
                                            NSInteger* outTimeSigDen,
                                            NSInteger* outSampleOffsetToNextBeat,
                                            double* outMeasureDownbeat) noexcept {
    if (outBeat) *outBeat = 0.0;
    if (outTempo) *outTempo = 120.0;
    if (outTimeSigNum) *outTimeSigNum = 4.0;
    if (outTimeSigDen) *outTimeSigDen = 4;
    if (outSampleOffsetToNextBeat) *outSampleOffsetToNextBeat = 0;
    if (outMeasureDownbeat) *outMeasureDownbeat = 0.0;
    if (!impl || !impl->hostTransportSnapshotAccess.valid()) return false;
    const auto& a = impl->hostTransportSnapshotAccess;
    // Two-phase seqlock: only an even, non-zero generation is a coherent snapshot.
    const uint64_t gen0 = a.generation->load(std::memory_order_acquire);
    if (gen0 == 0u || (gen0 & 1u) != 0u) return false;
    const double bpm = a.bpm->load(std::memory_order_relaxed);
    const double beat = a.beat->load(std::memory_order_relaxed);
    const uint64_t gen1 = a.generation->load(std::memory_order_acquire);
    if (gen0 != gen1) return false;
    if (outTempo && std::isfinite(bpm) && bpm >= 1.0 && bpm <= 1000.0) *outTempo = bpm;
    if (outBeat && std::isfinite(beat) && beat >= 0.0) *outBeat = beat;
    return true;
}

[[maybe_unused]] static bool readPublishedHostTransportState(ArpSIDAUv2Instance* impl,
                                            AUHostTransportStateFlags* outFlags,
                                            double* outCurrentSample,
                                            double* outCycleStartBeat,
                                            double* outCycleEndBeat) noexcept {
    if (outFlags) *outFlags = 0;
    if (outCurrentSample) *outCurrentSample = 0.0;
    if (outCycleStartBeat) *outCycleStartBeat = 0.0;
    if (outCycleEndBeat) *outCycleEndBeat = 0.0;
    if (!impl || !impl->hostTransportSnapshotAccess.valid()) return false;
    const auto& a = impl->hostTransportSnapshotAccess;
    // Two-phase seqlock: only an even, non-zero generation is a coherent snapshot.
    const uint64_t gen0 = a.generation->load(std::memory_order_acquire);
    if (gen0 == 0u || (gen0 & 1u) != 0u) return false;
    const uint32_t flags = a.flags->load(std::memory_order_relaxed);
    const double loopStart = a.loopStart->load(std::memory_order_relaxed);
    const double loopEnd = a.loopEnd->load(std::memory_order_relaxed);
    const uint64_t gen1 = a.generation->load(std::memory_order_acquire);
    if (gen0 != gen1) return false;
    AUHostTransportStateFlags auFlags = 0;
    if ((flags & 2u) != 0) auFlags |= AUHostTransportStateMoving;
    if ((flags & 4u) != 0) auFlags |= AUHostTransportStateCycling;
    if (outFlags) *outFlags = auFlags;
    if (outCycleStartBeat && std::isfinite(loopStart)) *outCycleStartBeat = loopStart;
    if (outCycleEndBeat && std::isfinite(loopEnd)) *outCycleEndBeat = loopEnd;
    return (flags & 1u) != 0u;
}

static bool readCurrentHostMusicalContext(ArpSIDAUv2Instance* impl,
                                          double* outBeat,
                                          double* outTempo,
                                          double* outTimeSigNum,
                                          NSInteger* outTimeSigDen,
                                          NSInteger* outSampleOffsetToNextBeat,
                                          double* outMeasureDownbeat) noexcept {
    if (outBeat) *outBeat = 0.0;
    if (outTempo) *outTempo = 120.0;
    if (outTimeSigNum) *outTimeSigNum = 4.0;
    if (outTimeSigDen) *outTimeSigDen = 4;
    if (outSampleOffsetToNextBeat) *outSampleOffsetToNextBeat = 0;
    if (outMeasureDownbeat) *outMeasureDownbeat = 0.0;
    if (!impl || !impl->audioUnit) return false;
    AUHostMusicalContextBlock block = impl->audioUnit.musicalContextBlock;
    if (block) {
        double bpm = 0.0;
        double num = 0.0;
        double beat = 0.0;
        double measureDownbeat = 0.0;
        NSInteger den = 4;
        NSInteger sampleOffsetToNextBeat = 0;
        if (!block(&bpm, &num, &den, &beat, &sampleOffsetToNextBeat, &measureDownbeat)) return false;
        if (outTempo && std::isfinite(bpm) && bpm >= 1.0 && bpm <= 1000.0) *outTempo = bpm;
        if (outBeat && std::isfinite(beat) && beat >= 0.0) *outBeat = beat;
        if (outTimeSigNum && std::isfinite(num) && num >= 1.0 && num <= 64.0) *outTimeSigNum = num;
        if (outTimeSigDen && den >= 1 && den <= 64) *outTimeSigDen = den;
        if (outSampleOffsetToNextBeat) *outSampleOffsetToNextBeat = sampleOffsetToNextBeat;
        if (outMeasureDownbeat && std::isfinite(measureDownbeat) && measureDownbeat >= 0.0) *outMeasureDownbeat = measureDownbeat;
        return true;
    }

    const bool haveBeatTempo = readCurrentHostBeatAndTempoFromCallbacks(impl->hostCallbacks, outBeat, outTempo);
    const bool haveTimeLocation = readCurrentHostMusicalTimeLocationFromCallbacks(impl->hostCallbacks,
                                                                                  outSampleOffsetToNextBeat,
                                                                                  outTimeSigNum,
                                                                                  outTimeSigDen,
                                                                                  outMeasureDownbeat);
    return haveBeatTempo || haveTimeLocation;
}

static bool readCurrentHostTransportState(ArpSIDAUv2Instance* impl,
                                          AUHostTransportStateFlags* outFlags,
                                          double* outCurrentSample,
                                          double* outCycleStartBeat,
                                          double* outCycleEndBeat) noexcept {
    if (outFlags) *outFlags = 0;
    if (outCurrentSample) *outCurrentSample = 0.0;
    if (outCycleStartBeat) *outCycleStartBeat = 0.0;
    if (outCycleEndBeat) *outCycleEndBeat = 0.0;
    if (!impl || !impl->audioUnit) return false;
    AUHostTransportStateBlock block = impl->audioUnit.transportStateBlock;
    if (block) {
        AUHostTransportStateFlags flags = 0;
        double sample = 0.0;
        double start = 0.0;
        double end = 0.0;
        if (!block(&flags, &sample, &start, &end)) return false;
        if (outFlags) *outFlags = flags;
        if (outCurrentSample && std::isfinite(sample)) *outCurrentSample = sample;
        if (outCycleStartBeat && std::isfinite(start)) *outCycleStartBeat = start;
        if (outCycleEndBeat && std::isfinite(end)) *outCycleEndBeat = end;
        return true;
    }
    return readCurrentHostTransportStateFromCallbacks(impl->hostCallbacks,
                                                      outFlags,
                                                      outCurrentSample,
                                                      outCycleStartBeat,
                                                      outCycleEndBeat);
}

static void publishHostTransportSnapshotFromAuv2Callbacks(ArpSIDAUv2Instance* impl,
                                                          UInt32 frames) noexcept {
    if (!impl || !impl->hostTransportSnapshotAccess.valid()) return;

    // Sentinels (NaN) keep "host did not provide this field" distinguishable
    // from "host provided zero". A real zero is a valid beat-zero / 0 BPM
    // boundary, NaN means the callback didn't write the field.
    double hostBeat = std::numeric_limits<double>::quiet_NaN();
    double hostTempo = std::numeric_limits<double>::quiet_NaN();
    (void)readCurrentHostBeatAndTempoFromCallbacks(impl->hostCallbacks, &hostBeat, &hostTempo);

    AUHostTransportStateFlags hostFlags = 0;
    double currentSample = 0.0;
    double cycleStartBeat = 0.0;
    double cycleEndBeat = 0.0;
    const bool haveTransportState = readCurrentHostTransportStateFromCallbacks(impl->hostCallbacks,
                                                                               &hostFlags,
                                                                               &currentSample,
                                                                               &cycleStartBeat,
                                                                               &cycleEndBeat);
    (void)currentSample;

    // Preserve the previously published BPM/beat when the host didn't fill
    // them in this poll. Snapping beat back to 0 (the historical behaviour)
    // confused the DSP transport-discontinuity logic into treating every poll
    // with a partial callback as a host-seek-to-bar-1, which restarted the
    // sequencer and choked DrSID notes.
    const double prevTempo = impl->hostTransportSnapshotAccess.bpm
        ? impl->hostTransportSnapshotAccess.bpm->load(std::memory_order_relaxed)
        : 120.0;
    const double prevBeatForFallback = impl->hostTransportSnapshotAccess.beat
        ? impl->hostTransportSnapshotAccess.beat->load(std::memory_order_relaxed)
        : 0.0;
    const double safeTempo = (std::isfinite(hostTempo) && hostTempo >= 1.0 && hostTempo <= 1000.0)
        ? hostTempo
        : ((std::isfinite(prevTempo) && prevTempo >= 1.0 && prevTempo <= 1000.0) ? prevTempo : 120.0);
    const double safeBeat = (std::isfinite(hostBeat) && hostBeat >= 0.0)
        ? hostBeat
        : ((std::isfinite(prevBeatForFallback) && prevBeatForFallback >= 0.0) ? prevBeatForFallback : 0.0);
    const double safeCycleStartBeat = (std::isfinite(cycleStartBeat) && cycleStartBeat >= 0.0) ? cycleStartBeat : 0.0;
    const double safeCycleEndBeat = (std::isfinite(cycleEndBeat) && cycleEndBeat >= safeCycleStartBeat)
        ? cycleEndBeat
        : safeCycleStartBeat;
    const double safeSampleRate = sanitizeAuv2SampleRate(impl->outputFormat.mSampleRate);
    uint32_t snapshotFlags = 0u;
    if (haveTransportState) snapshotFlags |= 1u;
    if ((hostFlags & AUHostTransportStateMoving) != 0) snapshotFlags |= 2u;
    if ((hostFlags & AUHostTransportStateCycling) != 0) snapshotFlags |= 4u;

    // Beat-movement playback inference. Some AUv2 hosts (and some host
    // configurations) provide a working beatAndTempoProc but no usable
    // transportStateProc. Without a transport-state callback we never know
    // whether the host is actually playing; the runtime's safe default is to
    // treat the play state as "unknown" (playStateKnown=false). That keeps us
    // from synthesising spurious stop edges, but it also means a host that is
    // genuinely playing never opens the gate on the sequencer/DrSID grid.
    //
    // Use the beat position from the previous published snapshot as the lever:
    // if the host's beatAndTempoProc reports a beat that has strictly advanced
    // between polls and the transport-state callback is absent or stuck, we
    // can safely infer that the transport is moving. Static-beat or backward
    // motion (rewind / stop position parked) does not flip the inference on.
    //
    // Guard rails:
    // • Beat must have actually moved (not the first publish where prev=0).
    // • Motion must be forward AND small enough to look like real playback,
    // not a host seek/load. At a 5 ms poll with 200 BPM that is at most
    // ~0.017 beats per poll; we accept up to 1 beat per poll which still
    // comfortably covers slow poll cadences without admitting jumps.
    // • Must have a valid beatAndTempo callback (otherwise prevBeat being
    // stale could trigger a one-shot false inference).
    if (!haveTransportState &&
        std::isfinite(hostBeat) &&
        callbacks_have_beat_and_tempo(impl->hostCallbacks))
    {
        const double prevBeat = impl->hostTransportSnapshotAccess.beat
            ? impl->hostTransportSnapshotAccess.beat->load(std::memory_order_relaxed)
            : 0.0;
        const double beatDelta = safeBeat - prevBeat;
        // 1/64 beat is well above floating-point jitter and below any musical
        // step. Cap at 1.0 beat to reject host-seeks and project-loads.
        if (std::isfinite(beatDelta) && beatDelta > (1.0 / 64.0) && beatDelta < 1.0) {
            snapshotFlags |= 1u; // playStateKnown — inferred from motion
            snapshotFlags |= 2u; // Moving — inferred from forward beat motion
        }
    }

    // Two-phase odd/even seqlock write. The render-side reader
    // (ArpSIDReadHostTransportSnapshotCoherent) requires that a complete,
    // consistent snapshot has an even, non-zero generation. The previous
    // implementation used a single fetch_add(1) which alternated even/odd
    // and caused the reader to reject ~50% of all publishes, in turn
    // flipping host.transportPlaying off every ~20 ms and producing
    // sequencer/DrSID/SID-808 stutter during Logic playback.
    uint64_t seq = impl->hostTransportSnapshotAccess.generation->load(std::memory_order_relaxed);
    if ((seq & 1u) != 0u) ++seq;
    impl->hostTransportSnapshotAccess.generation->store(seq + 1u, std::memory_order_release);  // odd: write in progress
    impl->hostTransportSnapshotAccess.bpm->store(safeTempo, std::memory_order_relaxed);
    impl->hostTransportSnapshotAccess.beat->store(safeBeat, std::memory_order_relaxed);
    impl->hostTransportSnapshotAccess.sampleRate->store(safeSampleRate, std::memory_order_relaxed);
    impl->hostTransportSnapshotAccess.loopStart->store(safeCycleStartBeat, std::memory_order_relaxed);
    impl->hostTransportSnapshotAccess.loopEnd->store(safeCycleEndBeat, std::memory_order_relaxed);
    impl->hostTransportSnapshotAccess.frameCount->store((int)frames, std::memory_order_relaxed);
    impl->hostTransportSnapshotAccess.flags->store(snapshotFlags, std::memory_order_relaxed);
    impl->hostTransportSnapshotAccess.generation->store(seq + 2u, std::memory_order_release);  // even: snapshot complete
}

static bool auv2HasLegacyHostCallbacks(const HostCallbackInfo& callbacks) noexcept {
    return callbacks.beatAndTempoProc || callbacks.musicalTimeLocationProc ||
           callbacks.transportStateProc || callbacks.transportStateProc2;
}

static void stopAuv2HostTransportPoller(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return;
    impl->hostTransportPollerStop.store(true, std::memory_order_release);
    if (impl->hostTransportPoller.joinable()) {
        impl->hostTransportPoller.join();
    }
    impl->hostTransportPollerStarted.store(false, std::memory_order_release);
}

static void startAuv2HostTransportPoller(ArpSIDAUv2Instance* impl) {
    if (!impl || impl->isClosing.load(std::memory_order_acquire)) return;
    if (!impl->hostTransportSnapshotAccess.valid()) return;
    if (!auv2HasLegacyHostCallbacks(impl->hostCallbacks)) return;
    bool expected = false;
    if (!impl->hostTransportPollerStarted.compare_exchange_strong(expected, true,
                                                                  std::memory_order_acq_rel,
                                                                  std::memory_order_acquire)) {
        return;
    }
    impl->hostTransportPollerStop.store(false, std::memory_order_release);
    impl->hostTransportPoller = std::thread([impl] {
        while (!impl->hostTransportPollerStop.load(std::memory_order_acquire) &&
               !impl->isClosing.load(std::memory_order_acquire)) {
            const UInt32 frames = sanitizeAuv2MaxFrames(impl->maxFramesPerSlice.load(std::memory_order_acquire));
            publishHostTransportSnapshotFromAuv2Callbacks(impl, frames);
            impl->hostTransportPollCount.fetch_add(1u, std::memory_order_relaxed);
            // 5 ms poll keeps transport-edge latency below one render block at
            // 256/44100 (~5.8 ms). Legacy AUv2 host procs are documented safe
            // to call off the render thread at this cadence.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        impl->hostTransportPollerStarted.store(false, std::memory_order_release);
    });
}

static void syncHostCallbacksIntoAudioUnit(ArpSIDAUv2Instance* impl) noexcept {
    if (!impl || !impl->audioUnit) return;

    // AUv2-wrapped AUv3 must never retain AUHostMusicalContextBlock or
    // AUHostTransportStateBlock closures that can re-enter AUv2 legacy host
    // callback proc pointers through AUv3 property/render-adjacent paths.
    // AUv2 is the sole owner that may read legacy HostCallbackInfo, and it
    // publishes transport into hostTransportSnapshotAccess from the non-render
    // poller. The wrapped AUv3 consumes only that atomic snapshot via its
    // runtime transport access; blocks are deliberately nil for the AUv2 shim.
    impl->audioUnit.musicalContextBlock = nil;
    impl->audioUnit.transportStateBlock = nil;

    const HostCallbackInfo callbacks = impl->hostCallbacks;
    if (!auv2HasLegacyHostCallbacks(callbacks)) {
        stopAuv2HostTransportPoller(impl);
        if (impl->hostTransportSnapshotAccess.valid()) {
            const UInt32 clearedFrames = sanitizeAuv2MaxFrames(impl->maxFramesPerSlice.load(std::memory_order_acquire));
            uint64_t seq = impl->hostTransportSnapshotAccess.generation->load(std::memory_order_relaxed);
            if ((seq & 1u) != 0u) ++seq;
            impl->hostTransportSnapshotAccess.generation->store(seq + 1u, std::memory_order_release);  // odd
            impl->hostTransportSnapshotAccess.bpm->store(120.0, std::memory_order_relaxed);
            impl->hostTransportSnapshotAccess.beat->store(0.0, std::memory_order_relaxed);
            impl->hostTransportSnapshotAccess.sampleRate->store(sanitizeAuv2SampleRate(impl->outputFormat.mSampleRate), std::memory_order_relaxed);
            impl->hostTransportSnapshotAccess.loopStart->store(0.0, std::memory_order_relaxed);
            impl->hostTransportSnapshotAccess.loopEnd->store(0.0, std::memory_order_relaxed);
            impl->hostTransportSnapshotAccess.frameCount->store((int)clearedFrames, std::memory_order_relaxed);
            impl->hostTransportSnapshotAccess.flags->store(0u, std::memory_order_relaxed);
            impl->hostTransportSnapshotAccess.generation->store(seq + 2u, std::memory_order_release);  // even
        }
        return;
    }

    if (impl->hostTransportSnapshotAccess.valid()) {
        publishHostTransportSnapshotFromAuv2Callbacks(impl,
                                                      sanitizeAuv2MaxFrames(impl->maxFramesPerSlice.load(std::memory_order_acquire)));
        startAuv2HostTransportPoller(impl);
    }
}
static const AudioTimeStamp* prepareRenderTimestamp(ArpSIDAUv2Instance* impl,
                                                    const AudioTimeStamp* incoming,
                                                    AudioTimeStamp& storage) noexcept {
    if (incoming) return incoming;
    storage = {};
    storage.mFlags = kAudioTimeStampSampleTimeValid;
    storage.mSampleTime = impl ? impl->nextRenderSampleTime.load(std::memory_order_acquire) : 0.0;
    return &storage;
}

static void commitRenderTimestamp(ArpSIDAUv2Instance* impl,
                                  const AudioTimeStamp* timestamp,
                                  UInt32 frames) noexcept {
    if (!impl) return;
    if (timestamp && (timestamp->mFlags & kAudioTimeStampSampleTimeValid)) {
        impl->nextRenderSampleTime.store(timestamp->mSampleTime + static_cast<double>(frames), std::memory_order_release);
    } else {
        impl->nextRenderSampleTime.store(impl->nextRenderSampleTime.load(std::memory_order_acquire) + static_cast<double>(frames), std::memory_order_release);
    }
}

static inline void suspendAuv2RenderingAndWait(ArpSIDAUv2Instance* impl) {
    if (!impl) return;
    impl->initialized.store(false, std::memory_order_release);
    std::unique_lock<ArpSID::AuditedCloseWaitMutex> waitLock(impl->closeWaitMutex);
    impl->closeWaitCv.wait(waitLock, [&] {
        return impl->activeRenderUsers.load(std::memory_order_acquire) == 0u;
    });
}

struct ScopedAuv2RenderUse {
    ArpSIDAUv2Instance* impl = nullptr;
    explicit ScopedAuv2RenderUse(ArpSIDAUv2Instance* i) noexcept : impl(i) {
        if (impl) impl->activeRenderUsers.fetch_add(1u, std::memory_order_acq_rel);
    }
    ~ScopedAuv2RenderUse() {
        if (!impl) return;
        const uint32_t prev = impl->activeRenderUsers.fetch_sub(1u, std::memory_order_acq_rel);
        if (prev == 1u) {
            // Render-thread destructor path: never take closeWaitMutex here.
            // The waiting side owns the mutex/predicate; notify_all() itself
            // does not require the notifying thread to hold the mutex.
            impl->closeWaitCv.notify_all();
        }
    }
    ScopedAuv2RenderUse(const ScopedAuv2RenderUse&) = delete;
    ScopedAuv2RenderUse& operator=(const ScopedAuv2RenderUse&) = delete;
};

static bool ensureOwnedOutputScratch(ArpSIDAUv2Instance* impl) {
    if (!impl) return false;
    // AUv2 v391 realtime guard: reserve to the hard AUv2 ceiling on a
    // configuration thread. componentRender() never calls new/reserve/resize.
    const UInt32 channels = std::max<UInt32>(1u, std::min<UInt32>(2u, impl->outputFormat.mChannelsPerFrame));
    const size_t hardFrames = static_cast<size_t>(kAuv2HardMaxFrames);
    const size_t reserveFrames = std::max<size_t>(hardFrames * kAuv2ScratchMultiplier, hardFrames + kAuv2ScratchHeadroomFrames);
    if (!ensureFloatScratchCapacityNonRealtime(impl->ownedPlanarScratch, reserveFrames * 2u)) {
        impl->outputScratchReady.store(false, std::memory_order_release);
        publishAuv2StateSnapshot(impl);
        return false;
    }
    if (!ensureFloatScratchCapacityNonRealtime(impl->ownedInterleavedScratch, reserveFrames * channels)) {
        impl->outputScratchReady.store(false, std::memory_order_release);
        publishAuv2StateSnapshot(impl);
        return false;
    }
    impl->outputScratchReady.store(true, std::memory_order_release);
    publishAuv2StateSnapshot(impl);
    return true;
}

static OSStatus syncRuntimeConfigurationLocked(ArpSIDAUv2Instance* impl) {
    ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
    if (!impl || !audioUnit) return paramErr;
    const UInt32 sanitizedMaxFrames = sanitizeAuv2MaxFrames(impl->maxFramesPerSlice.load(std::memory_order_acquire));
    impl->maxFramesPerSlice.store(sanitizedMaxFrames, std::memory_order_release);
    impl->outputFormat.mSampleRate = sanitizeAuv2SampleRate(impl->outputFormat.mSampleRate);
    if (impl->outputFormat.mChannelsPerFrame != 1 && impl->outputFormat.mChannelsPerFrame != 2) {
        impl->outputFormat = makeCanonicalOutputFormat(impl->outputFormat.mSampleRate, 2);
    }
    audioUnit.maximumFramesToRender = sanitizedMaxFrames;
    AudioStreamBasicDescription asbd = impl->outputFormat;
    AVAudioFormat* format = [[AVAudioFormat alloc] initWithStreamDescription:&asbd];
    if (format && !audioUnitSupportsSettableFormat(audioUnit, format)) {
        return kAudioUnitErr_FormatNotSupported;
    }
    if (!ensureOwnedOutputScratch(impl)) {
        return kAudioUnitErr_TooManyFramesToProcess;
    }
    [audioUnit prepareStandaloneWithSampleRate:impl->outputFormat.mSampleRate maxFrames:sanitizedMaxFrames];
    ArpSIDDSPKernelAdapter* adapter = (ArpSIDDSPKernelAdapter*)[audioUnit debugAdapter];
    ArpSID::ArpSIDDSPKernel* kernel = adapter ? [adapter kernelPtr] : nullptr;
    {
        std::lock_guard<ArpSID::AuditedStateMutex> lock(impl->stateMutex);
        impl->adapter = adapter;
    }
    impl->hostTransportSnapshotAccess = [audioUnit hostTransportSnapshotAccess];

    // AUv2 v419 auval correctness: AudioUnitInitialize/Open must establish the
    // AUv2 wrapper contract even if the wrapped AUv3 render block/kernel is not
    // synchronously visible for one host lifecycle edge. Returning
    // kAudioUnitErr_Uninitialized here poisons Component Manager state and later
    // auval slicing reports -10867 before componentRender() can fail closed.
    // Configuration failures that are true contract/config errors still return
    // errors above. Missing wrapped entry points become observable readiness
    // diagnostics; render will publish silence/noErr until a stable block/kernel
    // is republished by reset/reconfiguration.
    const bool hadRenderBlock = (loadPublishedRenderBlock(impl) != nullptr);
    const bool hadKernel = (loadPublishedKernelPointer(impl) != nullptr);
    if (kernel) {
        publishKernelPointer(impl, kernel);
    }
    publishAdapterPointer(impl, adapter);  // v549: RT-safe diag push path
    // Source-shape compatibility: publishRenderBlock(impl, [audioUnit internalRenderBlock])
    AUInternalRenderBlock freshRenderBlock = [audioUnit internalRenderBlock];
    if (freshRenderBlock) {
        publishRenderBlock(impl, freshRenderBlock);
    }
    resetPendingRenderState(impl);
    std::atomic_thread_fence(std::memory_order_release);
    impl->renderConfigurationGeneration.fetch_add(1u, std::memory_order_acq_rel);
    impl->renderEpoch.bump();
    publishAuv2StateSnapshot(impl);
    publishHostTransportSnapshotFromAuv2Callbacks(impl, sanitizedMaxFrames);
    startAuv2HostTransportPoller(impl);
    if ((!freshRenderBlock && !hadRenderBlock) || (!kernel && !hadKernel)) {
        impl->lastRenderError.store(noErr, std::memory_order_release);
        impl->renderReadinessDiagnosticCount.fetch_add(1u, std::memory_order_relaxed);
    }
    return noErr;
}

static OSStatus syncRuntimeConfiguration(ArpSIDAUv2Instance* impl) {
    if (!impl) return paramErr;
    suspendAuv2RenderingAndWait(impl);
    std::unique_lock<ArpSID::AuditedActivityMutex> activityLock(impl->activityMutex);
    const OSStatus status = syncRuntimeConfigurationLocked(impl);
    impl->lastRenderError.store(status, std::memory_order_release);
    if (status == noErr) {
        impl->initialized.store(true, std::memory_order_release);
        publishAuv2StateSnapshot(impl);
    } else {
        // never leave a previous successful initialize visible after a failed reconfiguration.
        impl->initialized.store(false, std::memory_order_release);
        impl->outputScratchReady.store(false, std::memory_order_release);
        publishAuv2StateSnapshot(impl);
    }
    return status;
}

static NSString* contextNameForInstance(ArpSIDAUv2Instance* impl) {
    ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
    if (!audioUnit) return nil;
    return (NSString*)objc_getAssociatedObject(audioUnit, kArpSIDAUv2ContextNameAssociationKey);
}

static inline ArpSID::ComponentFlavor componentFlavorForInstance(const ArpSIDAUv2Instance* impl) noexcept {
    if (!impl) return ArpSID::ComponentFlavor::Hybrid;
    return ArpSID::componentFlavorFromDescription(impl->componentDescription);
}

static NSString* auv2FlavorIconBaseName(ArpSID::ComponentFlavor flavor) {
    switch (flavor) {
        case ArpSID::ComponentFlavor::Instrument:  return @"ArpSIDInstrument";
        case ArpSID::ComponentFlavor::DrumMachine: return @"ArpSIDDrumMachine";
        case ArpSID::ComponentFlavor::Sid808:      return @"ArpSIDSid808";
        case ArpSID::ComponentFlavor::C64SidPlayer:return @"ArpSIDC64Player";
        case ArpSID::ComponentFlavor::Hybrid:
        default:                                   return @"ArpSIDHybrid";
    }
}

static NSURL* auv2FlavorIconURLForInstance(ArpSIDAUv2Instance* impl) {
    NSBundle* bundle = [NSBundle bundleWithIdentifier:@"com.arpsid.auv2"];
    if (!bundle) bundle = [NSBundle bundleForClass:[ArpSIDAudioUnit class]];
    if (!bundle) return nil;
    NSString* baseName = auv2FlavorIconBaseName(componentFlavorForInstance(impl));
    NSURL* iconURL = [bundle URLForResource:baseName withExtension:@"png"];
    if (!iconURL && ![baseName isEqualToString:@"ArpSIDHybrid"]) {
        iconURL = [bundle URLForResource:@"ArpSIDHybrid" withExtension:@"png"];
    }
    return iconURL;
}

static OSStatus setContextNameForInstance(ArpSIDAUv2Instance* impl, NSString* contextName) {
    if (!impl || !impl->audioUnit) return kAudioUnitErr_InvalidProperty;
    objc_setAssociatedObject(impl->audioUnit,
                             kArpSIDAUv2ContextNameAssociationKey,
                             contextName ? [contextName copy] : nil,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return noErr;
}

static OSStatus validateScopeAndElementForStreamProperty(AudioUnitScope scope, AudioUnitElement element) noexcept {
    if (scope != kAudioUnitScope_Output) return kAudioUnitErr_InvalidScope;
    if (element != 0) return kAudioUnitErr_InvalidElement;
    return noErr;
}

static bool validateGlobalScope(AudioUnitScope scope, AudioUnitElement element) {
    return scope == kAudioUnitScope_Global && element == 0;
}

static bool validateParameterAddress(AudioUnitParameterID inID) {
    return inID < (AudioUnitParameterID)ArpSID::kNumParams && auv2ParamIsHostVisible((int)inID);
}

static bool isGMDrumPromotionNoteOn(uint8_t status, uint8_t note, uint8_t velocity) noexcept {
    const uint8_t channel = status & 0x0Fu;
    const uint8_t type = status & 0xF0u;
    return type == 0x90u && velocity > 0u && ArpSID::sidCanonicalGMDrumPromotionCandidate(channel, note);
}

static void promoteDrSidModeForIncomingGMNote(ArpSIDAUv2Instance* impl,
                                              uint8_t status,
                                              uint8_t note,
                                              uint8_t velocity,
                                              UInt32 sampleOffset) {
    (void)sampleOffset;
    if (!impl || !isGMDrumPromotionNoteOn(status, note, velocity)) return;
    const uint8_t channel = status & 0x0Fu;
    const bool alreadyDrSid = cachedParameterValueForInstance(impl, (AudioUnitParameterID)ArpSID::kParamDrSidEnable) > 0.5f;
    // v909 Classic-mode authority: same flavor/opt-in law as AU3/VST3 — a
    // user-selected Classic/BitPerfect mode is never hijacked by channel-10
    // notes unless kParamAutoGmDrumPromotion is explicitly enabled (dedicated
    // drum flavors always allow; Instrument/C64SidPlayer never).
    const ArpSID::ComponentFlavor flavor = componentFlavorForInstance(impl);
    const bool autoPromotionAllowed = ArpSID::sidCanonicalGMDrumAutoPromotionAllowed(
        ArpSID::componentFlavorIsDedicatedDrum(flavor),
        flavor == ArpSID::ComponentFlavor::Hybrid,
        cachedParameterValueForInstance(impl, (AudioUnitParameterID)ArpSID::kParamAutoGmDrumPromotion) > 0.5f);
    const auto decision = ArpSID::sidCanonicalEvaluateGMDrSidPromotion(channel, note, true, alreadyDrSid,
                                                                       autoPromotionAllowed);
    if (!decision.promote) return;

    cacheParameterValueForInstance(impl, (AudioUnitParameterID)decision.disableSynthParam, decision.disableSynthValue);
    cacheParameterValueForInstance(impl, (AudioUnitParameterID)decision.enableDrSidParam, decision.enableDrSidValue);
}

static void notifyPropertyListeners(ArpSIDAUv2Instance* impl,
                                    AudioUnitPropertyID propertyID,
                                    AudioUnitScope scope,
                                    AudioUnitElement element) {
    if (!impl) return;
    if (!impl->componentInstance) return;
    std::vector<ArpSIDAUv2Instance::PropertyListenerEntry> listeners;
    {
        std::lock_guard<ArpSID::AuditedPropertyListenerMutex> lock(impl->propertyListenerMutex);
        listeners = impl->propertyListeners;
    }
    AudioUnit au = reinterpret_cast<AudioUnit>(impl->componentInstance);
    for (const auto& listener : listeners) {
        if (!listener.proc) continue;
        if (listener.propertyID != propertyID) continue;
        listener.proc(listener.userData, au, propertyID, scope, element);
    }
}

static NSString* const kArpSIDAUv2WrappedPresetNumberKey = @"ArpSIDAUv2WrappedPresetNumber";
static NSString* const kArpSIDAUv2WrappedPresetNameKey = @"ArpSIDAUv2WrappedPresetName";
static NSString* const kArpSIDAUv2WrappedPinnedPresetNumberKey = @"ArpSIDAUv2WrappedPinnedPresetNumber";
static NSString* const kArpSIDAUv2WrappedPinnedBankSlotKey = @"ArpSIDAUv2WrappedPinnedBankSlot";
static NSString* const kArpSIDAUv2WrappedStateGenerationKey = @"ArpSIDAUv2WrappedStateGeneration";
static NSString* const kArpSIDAUv2WrappedHostTempoHintKey = @"ArpSIDAUv2WrappedHostTempoHint";
static NSString* const kArpSIDAUv2WrappedHostBeatHintKey = @"ArpSIDAUv2WrappedHostBeatHint";
static NSString* const kArpSIDAUv2WrappedHostPlayingHintKey = @"ArpSIDAUv2WrappedHostPlayingHint";
static NSString* const kArpSIDAUv2WrappedHostLoopingHintKey = @"ArpSIDAUv2WrappedHostLoopingHint";
static NSString* const kArpSIDAUv2WrappedHostLoopStartHintKey = @"ArpSIDAUv2WrappedHostLoopStartHint";
static NSString* const kArpSIDAUv2WrappedHostLoopEndHintKey = @"ArpSIDAUv2WrappedHostLoopEndHint";
static NSString* const kArpSIDAUv2WrappedHostTimeSigNumHintKey = @"ArpSIDAUv2WrappedHostTimeSigNumHint";
static NSString* const kArpSIDAUv2WrappedHostTimeSigDenHintKey = @"ArpSIDAUv2WrappedHostTimeSigDenHint";
static NSString* const kArpSIDAUv2WrappedHostMeasureDownbeatHintKey = @"ArpSIDAUv2WrappedHostMeasureDownbeatHint";
static NSString* const kArpSIDAUv2WrappedSeqTempoNormKey = @"ArpSIDAUv2WrappedSeqTempoNorm";
static NSString* const kArpSIDAUv2WrappedArpTransposeNormKey = @"ArpSIDAUv2WrappedArpTransposeNorm";
static NSString* const kArpSIDAUv2WrappedProgramNormKey = @"ArpSIDAUv2WrappedProgramNorm";
static NSString* const kArpSIDAUv2WrappedBankSlotNormKey = @"ArpSIDAUv2WrappedBankSlotNorm";
static NSString* const kArpSIDAUv2WrappedArpEnableNormKey = @"ArpSIDAUv2WrappedArpEnableNorm";
static NSString* const kArpSIDAUv2WrappedArpModeNormKey = @"ArpSIDAUv2WrappedArpModeNorm";
static NSString* const kArpSIDAUv2WrappedArpRateNormKey = @"ArpSIDAUv2WrappedArpRateNorm";
static NSString* const kArpSIDAUv2WrappedArpOctavesNormKey = @"ArpSIDAUv2WrappedArpOctavesNorm";
static NSString* const kArpSIDAUv2WrappedArpSwingNormKey = @"ArpSIDAUv2WrappedArpSwingNorm";
static NSString* const kArpSIDAUv2WrappedArpGateNormKey = @"ArpSIDAUv2WrappedArpGateNorm";
static NSString* const kArpSIDAUv2WrappedArpHoldNormKey = @"ArpSIDAUv2WrappedArpHoldNorm";
static NSString* const kArpSIDAUv2WrappedArpLatchNormKey = @"ArpSIDAUv2WrappedArpLatchNorm";
static NSString* const kArpSIDAUv2WrappedArpRandomNormKey = @"ArpSIDAUv2WrappedArpRandomNorm";
static NSString* const kArpSIDAUv2WrappedArpPatternLengthNormKey = @"ArpSIDAUv2WrappedArpPatternLengthNorm";
static NSString* const kArpSIDAUv2WrappedSeqEnableNormKey = @"ArpSIDAUv2WrappedSeqEnableNorm";
static NSString* const kArpSIDAUv2WrappedSeqSwingNormKey = @"ArpSIDAUv2WrappedSeqSwingNorm";
static NSString* const kArpSIDAUv2WrappedSeqModeNormKey = @"ArpSIDAUv2WrappedSeqModeNorm";
static NSString* const kArpSIDAUv2WrappedSeqLengthNormKey = @"ArpSIDAUv2WrappedSeqLengthNorm";

struct ArpSIDAUv2AuxParamBinding {
    NSString* const* key;
    int paramID;
};

static const ArpSIDAUv2AuxParamBinding kArpSIDAUv2AuxParamBindings[] = {
    {&kArpSIDAUv2WrappedSeqTempoNormKey, ArpSID::kParamSeqTempo},
    {&kArpSIDAUv2WrappedArpTransposeNormKey, ArpSID::kParamArpTranspose},
    {&kArpSIDAUv2WrappedProgramNormKey, ArpSID::kParamProgram},
    {&kArpSIDAUv2WrappedBankSlotNormKey, ArpSID::kParamBankSlot},
    {&kArpSIDAUv2WrappedArpEnableNormKey, ArpSID::kParamArpEnable},
    {&kArpSIDAUv2WrappedArpModeNormKey, ArpSID::kParamArpMode},
    {&kArpSIDAUv2WrappedArpRateNormKey, ArpSID::kParamArpRate},
    {&kArpSIDAUv2WrappedArpOctavesNormKey, ArpSID::kParamArpOctaves},
    {&kArpSIDAUv2WrappedArpSwingNormKey, ArpSID::kParamArpSwing},
    {&kArpSIDAUv2WrappedArpGateNormKey, ArpSID::kParamArpGate},
    {&kArpSIDAUv2WrappedArpHoldNormKey, ArpSID::kParamArpHold},
    {&kArpSIDAUv2WrappedArpLatchNormKey, ArpSID::kParamArpLatch},
    {&kArpSIDAUv2WrappedArpRandomNormKey, ArpSID::kParamArpRandom},
    {&kArpSIDAUv2WrappedArpPatternLengthNormKey, ArpSID::kParamArpPatternLength},
    {&kArpSIDAUv2WrappedSeqEnableNormKey, ArpSID::kParamSeqEnable},
    {&kArpSIDAUv2WrappedSeqSwingNormKey, ArpSID::kParamSeqSwing},
    {&kArpSIDAUv2WrappedSeqModeNormKey, ArpSID::kParamSeqMode},
    {&kArpSIDAUv2WrappedSeqLengthNormKey, ArpSID::kParamSeqLength},
};

static float normalizedParamValueFromStateDictionary(NSDictionary* state, int paramID, float fallback) {
    if (!state) return std::clamp(fallback, 0.0f, 1.0f);
    NSData* blob = state[@"ArpSIDStateRootBlob"];
    if (![blob isKindOfClass:[NSData class]] || blob.length < sizeof(ArpSID::SidBinaryStateHeader)) return std::clamp(fallback, 0.0f, 1.0f);
    ArpSID::SidStateRootV1 root{};
    if (!ArpSID::decodeSidStateRootBinary((const uint8_t*)blob.bytes, (size_t)blob.length, root) || !root.valid()) return std::clamp(fallback, 0.0f, 1.0f);
    float value = fallback;
    bool found = false;
    for (const auto& e : root.patch.parameters.semantic_entries) {
        if ((int)e.param_id == paramID) { value = e.value; found = true; break; }
    }
    if (!found && paramID >= 0 && root.patch.parameters.values.size() > (size_t)paramID) value = root.patch.parameters.values[(size_t)paramID];
    if (!std::isfinite(value)) value = fallback;
    return std::clamp(value, 0.0f, 1.0f);
}


static NSInteger deriveBankSlotFromStateDictionary(NSDictionary* state) {
    if (!state) return -1;
    NSData* blob = state[@"ArpSIDStateRootBlob"];
    if (![blob isKindOfClass:[NSData class]]) return -1;
    if (blob.length < sizeof(ArpSID::SidBinaryStateHeader)) return -1;
    ArpSID::SidStateRootV1 root{};
    if (!ArpSID::decodeSidStateRootBinary((const uint8_t*)blob.bytes, (size_t)blob.length, root) || !root.valid()) {
        return -1;
    }
    float bankNorm = ArpSID::defaultNormalizedParamValue(ArpSID::kParamBankSlot);
    bool foundBank = false;
    for (const auto& e : root.patch.parameters.semantic_entries) {
        if ((int)e.param_id == ArpSID::kParamBankSlot) {
            bankNorm = e.value;
            foundBank = true;
            break;
        }
    }
    if (!foundBank && root.patch.parameters.values.size() > (size_t)ArpSID::kParamBankSlot) {
        bankNorm = root.patch.parameters.values[(size_t)ArpSID::kParamBankSlot];
        foundBank = true;
    }
    if (foundBank && std::isfinite(bankNorm)) {
        const int bankSlot = ArpSID::canonicalFactorySlotFromNormalizedBankSlot(bankNorm);
        return (NSInteger)ArpSID::canonicalFactorySlotForRoot(bankSlot);
    }
    // Do not fall back to kParamProgram. Imported MIDI/Logic playback can carry
    // GM program metadata (often piano/program 0), and using it here turns song
    // metadata into ArpSID preset identity. Missing BankSlot means fallback to the
    // already pinned/current AU preset, not Program.
    return -1;
}

static NSInteger sanitizedFactoryPresetNumberFromDictionary(ArpSIDAUv2Instance* impl, NSDictionary* state) {
    const ArpSID::ComponentFlavor flavor = componentFlavorForInstance(impl);
    const NSInteger derived = deriveBankSlotFromStateDictionary(state);
    if (derived >= 0) {
        const NSInteger requested = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)derived);
        return auv2FactorySlotAllowedForFlavor(flavor, requested)
            ? requested
            : auv2StartupFactorySlotForFlavor(flavor);
    }
    NSInteger pinnedNumber = 0;
    snapshotPresentPresetState(impl, &pinnedNumber, nullptr);
    const NSInteger requested = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)pinnedNumber);
    return auv2FactorySlotAllowedForFlavor(flavor, requested)
        ? requested
        : auv2StartupFactorySlotForFlavor(flavor);
}

static NSDictionary* wrapClassInfoDictionary(ArpSIDAUv2Instance* impl, NSDictionary* state) {
    NSMutableDictionary* wrapped = [NSMutableDictionary dictionary];
    const OSType componentType = impl ? impl->componentDescription.componentType : kComponentType;
    const OSType componentSubType = impl ? impl->componentDescription.componentSubType : kComponentSubType;
    wrapped[@kAUPresetTypeKey] = @(componentType);
    wrapped[@kAUPresetSubtypeKey] = @(componentSubType);
    wrapped[@kAUPresetManufacturerKey] = @(kComponentManufacturer);
    wrapped[@kAUPresetVersionKey] = @(kComponentVersion);
    wrapped[@kAUPresetRenderQualityKey] = @(impl ? impl->renderQuality : kRenderQuality_Max);
    // AUv2 ClassInfo wrapping must not derive preset identity
    // from SidStateRoot BankSlot/Program mirrors. Logic asks for ClassInfo around
    // Stop/Play and may later replay it; if the root mirror is stale/default 0,
    // preferring it serializes a self-inflicted patch-0 reset. The AUv2 sticky
    // PresentPreset/BankSlot authority is the only preset identity source here.
    const NSInteger effectivePresetNumber = cachedPinnedPresetSlotForInstance(impl);
    NSString* pinnedName = nil;
    snapshotPresentPresetState(impl, nullptr, &pinnedName);
    const ArpSID::ComponentFlavor flavor = componentFlavorForInstance(impl);
    NSString* presetName = (pinnedName.length > 0)
        ? pinnedName
        : defaultFactoryPresetNameForNumber(effectivePresetNumber, flavor);
    wrapped[@kAUPresetNameKey] = presetName;
    wrapped[kArpSIDAUv2WrappedPresetNumberKey] = @(effectivePresetNumber);
    wrapped[kArpSIDAUv2WrappedPinnedPresetNumberKey] = @(effectivePresetNumber);
    wrapped[kArpSIDAUv2WrappedPinnedBankSlotKey] = @(effectivePresetNumber);
    wrapped[kArpSIDAUv2WrappedPresetNameKey] = presetName;
    wrapped[kArpSIDAUv2WrappedStateGenerationKey] = @(impl ? (NSInteger)impl->wrappedStateGeneration.load(std::memory_order_acquire) : 0);
    for (const auto& binding : kArpSIDAUv2AuxParamBindings) {
        if (binding.paramID == ArpSID::kParamBankSlot) {
            wrapped[*binding.key] = @(ArpSID::canonicalNormalizedBankSlotValue((int)effectivePresetNumber));
            continue;
        }
        if (binding.paramID == ArpSID::kParamProgram) {
            wrapped[*binding.key] = @(ArpSID::canonicalNormalizedFactoryProgramValue((int)effectivePresetNumber));
            continue;
        }
        wrapped[*binding.key] = @(normalizedParamValueFromStateDictionary(state, binding.paramID, ArpSID::defaultNormalizedParamValue(binding.paramID)));
    }
    double hostBeat = 0.0;
    double hostTempo = 120.0;
    double hostTimeSigNum = 4.0;
    NSInteger hostTimeSigDen = 4;
    double hostMeasureDownbeat = 0.0;
    AUHostTransportStateFlags hostFlags = 0;
    double currentSample = 0.0, cycleStartBeat = 0.0, cycleEndBeat = 0.0;
    if (impl) {
        (void)readCurrentHostMusicalContext(impl, &hostBeat, &hostTempo, &hostTimeSigNum, &hostTimeSigDen, nullptr, &hostMeasureDownbeat);
        (void)readCurrentHostTransportState(impl, &hostFlags, &currentSample, &cycleStartBeat, &cycleEndBeat);
    }
    wrapped[kArpSIDAUv2WrappedHostTempoHintKey] = @(std::isfinite(hostTempo) && hostTempo >= 1.0 && hostTempo <= 1000.0 ? hostTempo : 120.0);
    wrapped[kArpSIDAUv2WrappedHostBeatHintKey] = @(std::isfinite(hostBeat) && hostBeat >= 0.0 ? hostBeat : 0.0);
    wrapped[kArpSIDAUv2WrappedHostPlayingHintKey] = @((hostFlags & AUHostTransportStateMoving) != 0);
    wrapped[kArpSIDAUv2WrappedHostLoopingHintKey] = @((hostFlags & AUHostTransportStateCycling) != 0);
    wrapped[kArpSIDAUv2WrappedHostLoopStartHintKey] = @(std::isfinite(cycleStartBeat) && cycleStartBeat >= 0.0 ? cycleStartBeat : 0.0);
    wrapped[kArpSIDAUv2WrappedHostLoopEndHintKey] = @(std::isfinite(cycleEndBeat) && cycleEndBeat >= cycleStartBeat ? cycleEndBeat : std::max(0.0, cycleStartBeat));
    wrapped[kArpSIDAUv2WrappedHostTimeSigNumHintKey] = @(std::isfinite(hostTimeSigNum) && hostTimeSigNum >= 1.0 ? hostTimeSigNum : 4.0);
    wrapped[kArpSIDAUv2WrappedHostTimeSigDenHintKey] = @(hostTimeSigDen >= 1 ? hostTimeSigDen : 4);
    wrapped[kArpSIDAUv2WrappedHostMeasureDownbeatHintKey] = @(std::isfinite(hostMeasureDownbeat) && hostMeasureDownbeat >= 0.0 ? hostMeasureDownbeat : 0.0);
    wrapped[@kAUPresetDataKey] = state ? [state copy] : @{};
    return wrapped;
}

static NSDictionary* unwrapClassInfoDictionary(NSDictionary* state) {
    if (!state) return nil;
    id nested = state[@kAUPresetDataKey];
    if ([nested isKindOfClass:[NSDictionary class]]) {
        return (NSDictionary*)nested;
    }
    return state;
}

static AUAudioUnitPreset* resolveWrappedPresetForState(ArpSIDAUv2Instance* impl, NSDictionary* wrappedState) {
    if (!impl || !wrappedState) return nil;
    NSNumber* pinnedPresetNumber = wrappedState[kArpSIDAUv2WrappedPinnedPresetNumberKey];
    NSNumber* presetNumber = wrappedState[kArpSIDAUv2WrappedPresetNumberKey];
    NSString* presetName = wrappedState[kArpSIDAUv2WrappedPresetNameKey];
    NSInteger targetNumber = 0;
    if ([pinnedPresetNumber isKindOfClass:[NSNumber class]]) {
        targetNumber = pinnedPresetNumber.integerValue;
    } else if ([presetNumber isKindOfClass:[NSNumber class]]) {
        targetNumber = presetNumber.integerValue;
    } else {
        targetNumber = sanitizedFactoryPresetNumberFromDictionary(impl, unwrapClassInfoDictionary(wrappedState));
    }
    NSString* resolvedName = [presetName isKindOfClass:[NSString class]] && presetName.length > 0
        ? presetName
        : defaultFactoryPresetNameForNumber(targetNumber, componentFlavorForInstance(impl));
    return makeFactoryPresetObject(targetNumber, resolvedName, componentFlavorForInstance(impl));
}

static void restorePresetMetadataFromWrappedState(ArpSIDAUv2Instance* impl, NSDictionary* wrappedState) {
    if (!impl || !wrappedState) return;
    AUAudioUnitPreset* preset = resolveWrappedPresetForState(impl, wrappedState);
    if (!preset) return;
    ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);

    if (audioUnit &&
        [audioUnit respondsToSelector:@selector(hasStickyUserFactoryPresetSelection)] &&
        [audioUnit hasStickyUserFactoryPresetSelection] &&
        [audioUnit respondsToSelector:@selector(stickyFactoryPresetNumber)]) {
        const NSInteger sticky = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)[audioUnit stickyFactoryPresetNumber]);
        const NSInteger incoming = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)preset.number);
        if (incoming != sticky) {
            // Stale ClassInfo replay must not demote a live user-selected
            // preset. Preserve sticky AU identity; real audio restore is handled
            // by setFullState.
            preset = makeFactoryPresetObject(sticky,
                                            defaultFactoryPresetNameForNumber(sticky, componentFlavorForInstance(impl)),
                                            componentFlavorForInstance(impl));
        }
    }

    cachePresentPresetStateForPreset(impl, preset);
    NSNumber* generation = wrappedState[kArpSIDAUv2WrappedStateGenerationKey];
    if ([generation isKindOfClass:[NSNumber class]]) {
        impl->wrappedStateGeneration.store((uint64_t)std::max<NSInteger>(0, generation.integerValue), std::memory_order_release);
        impl->renderEpoch.bump();
        publishAuv2StateSnapshot(impl);
    }
    if (audioUnit && [audioUnit respondsToSelector:@selector(setRestoredFactoryPresetMetadataOnlyForSlot:)]) {
        [audioUnit setRestoredFactoryPresetMetadataOnlyForSlot:preset.number];
    } else if (audioUnit && [audioUnit respondsToSelector:@selector(setCurrentFactoryPresetMetadataOnlyForSlot:)]) {
        [audioUnit setCurrentFactoryPresetMetadataOnlyForSlot:preset.number];
    }
    for (const auto& binding : kArpSIDAUv2AuxParamBindings) {
        if (binding.paramID == ArpSID::kParamProgram || binding.paramID == ArpSID::kParamBankSlot) {
            continue; // sticky preset identity is restored only from pinned preset metadata
        }
        NSNumber* value = wrappedState[*binding.key];
        if (audioUnit && [value isKindOfClass:[NSNumber class]]) {
            [audioUnit setParameterValue:(float)std::clamp(value.floatValue, 0.0f, 1.0f) forID:binding.paramID];
        }
    }
    id debugAdapter = audioUnit ? [audioUnit debugAdapter] : nil;
    if (ArpSID::ArpSIDDSPKernel* kernel = debugAdapter ? [(ArpSIDDSPKernelAdapter*)debugAdapter kernelPtr] : nullptr) {
        NSNumber* hostTempoHint = wrappedState[kArpSIDAUv2WrappedHostTempoHintKey];
        NSNumber* hostBeatHint = wrappedState[kArpSIDAUv2WrappedHostBeatHintKey];
        NSNumber* hostPlayingHint = wrappedState[kArpSIDAUv2WrappedHostPlayingHintKey];
        if ([hostTempoHint isKindOfClass:[NSNumber class]]) { const double bpm = hostTempoHint.doubleValue; if (std::isfinite(bpm) && bpm >= 1.0 && bpm <= 1000.0) kernel->setHostTempo(bpm); }
        if ([hostBeatHint isKindOfClass:[NSNumber class]]) { const double beat = hostBeatHint.doubleValue; if (std::isfinite(beat) && beat >= 0.0) kernel->setTransportBeatPosition(beat); }
        if ([hostPlayingHint isKindOfClass:[NSNumber class]]) kernel->setTransportPlaying(hostPlayingHint.boolValue);
    }
}

static OSStatus fillParameterInfo(AudioUnitParameterID paramID, AudioUnitParameterInfo* outInfo) {
    if (!outInfo || !validateParameterAddress(paramID)) return kAudioUnitErr_InvalidParameter;
    const ArpSID::ParamInfo& info = ArpSID::kParamInfos[(size_t)paramID];
    std::memset(outInfo, 0, sizeof(*outInfo));

    NSString* name = [NSString stringWithUTF8String:(info.name ? info.name : "Parameter")];
    const char* utf8 = [name UTF8String];
    std::strncpy(outInfo->name, utf8 ? utf8 : "Parameter", sizeof(outInfo->name) - 1);
    outInfo->cfNameString = (__bridge_retained CFStringRef)[name copy];
    outInfo->unit = auv2ParameterUnitForInfo(paramID, info);
    outInfo->minValue = 0.0f;
    outInfo->maxValue = 1.0f;
    outInfo->defaultValue = ArpSID::defaultNormalizedParamValue((int)paramID);
    const bool isPresetMirror = (paramID == (AudioUnitParameterID)ArpSID::kParamProgram ||
                                 paramID == (AudioUnitParameterID)ArpSID::kParamBankSlot);
    outInfo->flags = kAudioUnitParameterFlag_IsReadable |
                     kAudioUnitParameterFlag_IsHighResolution |
                     kAudioUnitParameterFlag_HasCFNameString |
                     kAudioUnitParameterFlag_CFNameRelease;
    // Program/BankSlot are sticky preset mirrors. AUv2 exposes them for readback
    // only. Marking them writable/rampable gives Logic another metadata replay
    // lane at Play, bypassing PresentPreset/ClassInfo guards.
    if (!isPresetMirror) {
        outInfo->flags |= kAudioUnitParameterFlag_IsWritable;
    }

    UInt32 stepCount = 0;
    if (auv2ParameterIsStepped(paramID, info, &stepCount)) {
        if (!isPresetMirror) outInfo->flags |= kAudioUnitParameterFlag_CanRamp;
        if (outInfo->unit == kAudioUnitParameterUnit_Boolean) {
            outInfo->minValue = 0.0f;
            outInfo->maxValue = 1.0f;
        }
    } else if (!isPresetMirror) {
        outInfo->flags |= kAudioUnitParameterFlag_CanRamp;
    }
    return noErr;
}

static AudioComponentMethod lookupSelector(SInt16 selector);

static OSStatus componentOpen(void* self, AudioComponentInstance instance) {
    ArpSID::prewarmAllSidTables(); // non-RT Phase 0 prewarm before any render touch
    ArpSIDAUv2Instance* impl = implFor(self);
    if (!impl) return paramErr;
    impl->componentInstance = instance;
    syncHostCallbacksIntoAudioUnit(impl);
    // Build scratch, render block and kernel pointer on the open/configuration
    // thread so an eager host render never observes a half-published wrapper.
    const OSStatus syncStatus = syncRuntimeConfiguration(impl);
    if (syncStatus != noErr) return syncStatus;
    refreshParameterCacheFromAudioUnit(impl);
    return noErr;
}

static OSStatus componentClose(void* self) {
    ArpSIDAUv2Wrapper* wrapper = wrapperFor(self);
    ArpSIDAUv2Instance* impl = implFor(self);
    if (!wrapper || !impl) return paramErr;
    impl->initialized.store(false, std::memory_order_release);
    impl->isClosing.store(true, std::memory_order_release);
    publishAuv2StateSnapshot(impl);
    stopAuv2HostTransportPoller(impl);
    {
        std::unique_lock<ArpSID::AuditedCloseWaitMutex> waitLock(impl->closeWaitMutex);
        impl->closeWaitCv.wait(waitLock, [&] {
            return impl->activeUsers.load(std::memory_order_acquire) == 0u;
        });
    }
    {
        std::unique_lock<ArpSID::AuditedActivityMutex> activityLock(impl->activityMutex);
        std::lock_guard<ArpSID::AuditedStateMutex> lock(impl->stateMutex);
        if (impl->audioUnit) {
            impl->audioUnit.musicalContextBlock = nil;
            impl->audioUnit.transportStateBlock = nil;
        }
        impl->hostTransportSnapshotAccess = ArpSIDHostTransportSnapshotAccess{};
        publishKernelPointer(impl, nullptr);
        publishAdapterPointer(impl, nil);  // v549
    publishRenderBlock(impl, nil);
        impl->adapter = nil;
        impl->audioUnit = nil;
    }
    delete impl;
    wrapper->impl = nullptr;
    delete wrapper;
    return noErr;
}

static AudioComponentMethod componentLookup(SInt16 selector) {
    return lookupSelector(selector);
}

static OSStatus componentInitialize(void* self) {
    ArpSID::prewarmAllSidTables(); // idempotent non-RT defensive prewarm
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
    if (!impl || !audioUnit) return paramErr;
    std::unique_lock<ArpSID::AuditedActivityMutex> activityLock(impl->activityMutex);
    // AUv2 pinned preset is the authority during initialize.
    // Do not trust audioUnit.currentPreset here: Logic can leave AUAudioUnit
    // metadata at factory slot 0 while AUv2 still has the selected patch.
    AUAudioUnitPreset* pinnedPreset = makePinnedFactoryPresetObject(impl);
    if (pinnedPreset) {
        // Pass 2: initialize is metadata bootstrap only. Calling
        // audioUnit.currentPreset here applies a factory root and can overwrite
        // project/host-restored sound. AUv2 pinned preset remains authority.
        (void)setAudioUnitPresetMetadataOnlySafely(impl, pinnedPreset);
    }
    syncHostCallbacksIntoAudioUnit(impl);
    const OSStatus syncStatus = syncRuntimeConfigurationLocked(impl);
    if (syncStatus != noErr) return syncStatus;
    refreshParameterCacheFromAudioUnit(impl);
    resetPendingRenderState(impl);
    impl->initialized.store(true, std::memory_order_release);
    impl->lastRenderError.store(noErr, std::memory_order_release);
    publishAuv2StateSnapshot(impl);
    return noErr;
}

static OSStatus componentUninitialize(void* self) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl) return paramErr;
    suspendAuv2RenderingAndWait(impl);
    std::unique_lock<ArpSID::AuditedActivityMutex> activityLock(impl->activityMutex);
    stopAuv2HostTransportPoller(impl);
    ArpSIDDSPKernelAdapter* adapter = retainedAdapterForInstance(impl);
    if (adapter) [adapter reset];
    resetPendingRenderState(impl);
    impl->initialized.store(false, std::memory_order_release);
    impl->lastRenderError.store(noErr, std::memory_order_release);
    publishAuv2StateSnapshot(impl);
    return noErr;
}

static bool shouldRejectStaleClassInfoReplay(ArpSIDAUv2Instance* impl, NSDictionary* wrappedState) {
    if (!impl || !wrappedState) return false;
    if (!synchronizeAuv2PresetCacheFromWrappedAudioUnit(impl)) return false;
    const NSInteger sticky = cachedPinnedPresetSlotForInstance(impl);
    if (sticky == 0) return false;
    AUAudioUnitPreset* incomingPreset = resolveWrappedPresetForState(impl, wrappedState);
    if (!incomingPreset) return false;
    const NSInteger incoming = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)incomingPreset.number);
    if (incoming == sticky) return false;
    cachePresentPresetState(impl, sticky, defaultFactoryPresetNameForNumber(sticky, componentFlavorForInstance(impl)), true);
    ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
    if (audioUnit && [audioUnit respondsToSelector:@selector(setRestoredFactoryPresetMetadataOnlyForSlot:)]) {
        [audioUnit setRestoredFactoryPresetMetadataOnlyForSlot:sticky];
    } else if (audioUnit && [audioUnit respondsToSelector:@selector(setCurrentFactoryPresetMetadataOnlyForSlot:)]) {
        [audioUnit setCurrentFactoryPresetMetadataOnlyForSlot:sticky];
    }
    return true;
}

static OSStatus componentGetPropertyInfo(void* self,
                                         AudioUnitPropertyID inID,
                                         AudioUnitScope inScope,
                                         AudioUnitElement inElement,
                                         UInt32* outDataSize,
                                         Boolean* outWritable) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl) return paramErr;

    auto setInfo = [&](UInt32 size, Boolean writable) -> OSStatus {
        if (outDataSize) *outDataSize = size;
        if (outWritable) *outWritable = writable;
        return noErr;
    };

    switch (inID) {
        case kAudioUnitProperty_ClassInfo:
        case kAudioUnitProperty_ClassInfoFromDocument:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(CFDictionaryRef), true);
        case kAudioUnitProperty_ParameterList:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)(hostVisibleParameterList().size() * sizeof(AudioUnitParameterID)), false);
        case kAudioUnitProperty_ParameterInfo:
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
            if (!validateParameterAddress(inElement)) return kAudioUnitErr_InvalidParameter;
            return setInfo((UInt32)sizeof(AudioUnitParameterInfo), false);
        case kAudioUnitProperty_FastDispatch:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(void*), false);
        case kAudioUnitProperty_StreamFormat:
            if (const OSStatus status = validateScopeAndElementForStreamProperty(inScope, inElement); status != noErr) return status;
            return setInfo((UInt32)sizeof(AudioStreamBasicDescription), true);
        case kAudioUnitProperty_SampleRate:
            if (const OSStatus status = validateScopeAndElementForStreamProperty(inScope, inElement); status != noErr) return status;
            return setInfo((UInt32)sizeof(Float64), true);
        case kAudioUnitProperty_ElementCount:
            switch (inScope) {
                case kAudioUnitScope_Input:
                case kAudioUnitScope_Output:
                case kAudioUnitScope_Global:
                    break;
                default:
                    return kAudioUnitErr_InvalidScope;
            }
            return setInfo((UInt32)sizeof(UInt32), false);
        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(Float64), false);
        case kAudioUnitProperty_SupportedNumChannels:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)(2 * sizeof(AUChannelInfo)), false);
        case kAudioUnitProperty_MaximumFramesPerSlice:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(UInt32), true);
        case kAudioUnitProperty_FactoryPresets:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(CFArrayRef), false);
        case kAudioUnitProperty_PresentPreset:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(AUPreset), true);
        case kAudioUnitProperty_HostCallbacks:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(HostCallbackInfo), true);
        case kAudioUnitProperty_ContextName:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(CFStringRef), true);
        case kAudioUnitProperty_CocoaUI:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(AudioUnitCocoaViewInfo), false);
        case kAudioUnitProperty_IconLocation:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(CFURLRef), false);
        case kAudioUnitProperty_LastRenderError:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(OSStatus), false);
        case kAudioUnitProperty_InPlaceProcessing:
        case kAudioUnitProperty_OfflineRender:
        case kAudioUnitProperty_RenderQuality:
        case kAudioUnitProperty_SupportsMPE:
        case kMusicDeviceProperty_SupportsStartStopNote:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(UInt32), inID == kAudioUnitProperty_InPlaceProcessing ||
                                               inID == kAudioUnitProperty_OfflineRender ||
                                               inID == kAudioUnitProperty_RenderQuality);
        case kAudioUnitProperty_ParameterStringFromValue:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(AudioUnitParameterStringFromValue), false);
        case kAudioUnitProperty_ParameterValueFromString:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(AudioUnitParameterValueFromString), false);
        case kArpSIDPropertyPsidData:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo(0u, true);
        case kPropertyBridgeObject:
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return setInfo((UInt32)sizeof(void*), false);
        default:
            return kAudioUnitErr_InvalidProperty;
    }
}

static OSStatus componentGetProperty(void* self,
                                     AudioUnitPropertyID inID,
                                     AudioUnitScope inScope,
                                     AudioUnitElement inElement,
                                     void* outData,
                                     UInt32* ioDataSize) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl || !outData || !ioDataSize) return paramErr;

    const auto requireSize = [&](UInt32 minSize) -> bool {
        return *ioDataSize >= minSize;
    };

    switch (inID) {
        case kAudioUnitProperty_ClassInfo:
        case kAudioUnitProperty_ClassInfoFromDocument: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(CFDictionaryRef))) return kAudioUnitErr_InvalidPropertyValue;
            ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
            NSDictionary* state = nil;
            if (audioUnit) {
                state = (inID == kAudioUnitProperty_ClassInfo) ? [audioUnit fullState] : [audioUnit fullStateForDocument];
            } else if (!impl->isClosing.load(std::memory_order_acquire)) {
                return kAudioUnitErr_Uninitialized;
            }
            (void)synchronizeAuv2PresetCacheFromWrappedAudioUnit(impl);
            NSDictionary* wrapped = wrapClassInfoDictionary(impl, state);
            CFDictionaryRef value = (__bridge_retained CFDictionaryRef)[wrapped copy]; // Create Rule: host owns and releases.
            std::memcpy(outData, &value, sizeof(value));
            *ioDataSize = (UInt32)sizeof(value);
            return noErr;
        }
        case kAudioUnitProperty_ParameterList: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            const auto& ids = hostVisibleParameterList();
            const UInt32 bytes = (UInt32)(ids.size() * sizeof(AudioUnitParameterID));
            if (!requireSize(bytes)) return kAudioUnitErr_InvalidPropertyValue;
            std::memcpy(outData, ids.data(), bytes);
            *ioDataSize = bytes;
            return noErr;
        }
        case kAudioUnitProperty_ParameterInfo: {
            if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(AudioUnitParameterInfo))) return kAudioUnitErr_InvalidPropertyValue;
            AudioUnitParameterInfo info{};
            const OSStatus status = fillParameterInfo(inElement, &info);
            if (status != noErr) return status;
            std::memcpy(outData, &info, sizeof(info));
            *ioDataSize = (UInt32)sizeof(info);
            return noErr;
        }
        case kAudioUnitProperty_FastDispatch: {
            if (!validateGlobalScope(inScope, 0)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(void*))) return kAudioUnitErr_InvalidPropertyValue;
            AudioComponentMethod method = lookupSelector((SInt16)inElement);
            std::memcpy(outData, &method, sizeof(method));
            *ioDataSize = (UInt32)sizeof(method);
            return method ? noErr : kAudioUnitErr_InvalidProperty;
        }
        case kAudioUnitProperty_StreamFormat: {
            if (const OSStatus status = validateScopeAndElementForStreamProperty(inScope, inElement); status != noErr) return status;
            if (!requireSize((UInt32)sizeof(AudioStreamBasicDescription))) return kAudioUnitErr_InvalidPropertyValue;
            std::memcpy(outData, &impl->outputFormat, sizeof(impl->outputFormat));
            *ioDataSize = (UInt32)sizeof(impl->outputFormat);
            return noErr;
        }
        case kAudioUnitProperty_SampleRate: {
            if (const OSStatus status = validateScopeAndElementForStreamProperty(inScope, inElement); status != noErr) return status;
            if (!requireSize((UInt32)sizeof(Float64))) return kAudioUnitErr_InvalidPropertyValue;
            const Float64 sampleRate = impl->outputFormat.mSampleRate;
            std::memcpy(outData, &sampleRate, sizeof(sampleRate));
            *ioDataSize = (UInt32)sizeof(sampleRate);
            return noErr;
        }
        case kAudioUnitProperty_ElementCount: {
            if (!requireSize((UInt32)sizeof(UInt32))) return kAudioUnitErr_InvalidPropertyValue;
            UInt32 count = 0;
            switch (inScope) {
                case kAudioUnitScope_Input: count = 0; break;
                case kAudioUnitScope_Output: count = 1; break;
                case kAudioUnitScope_Global: count = 1; break;
                default: return kAudioUnitErr_InvalidScope;
            }
            std::memcpy(outData, &count, sizeof(count));
            *ioDataSize = (UInt32)sizeof(count);
            return noErr;
        }
        case kAudioUnitProperty_Latency: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(Float64))) return kAudioUnitErr_InvalidPropertyValue;
            ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
            const Float64 latency = audioUnit ? (Float64)audioUnit.latency : 0.0;
            std::memcpy(outData, &latency, sizeof(latency));
            *ioDataSize = (UInt32)sizeof(latency);
            return noErr;
        }
        case kAudioUnitProperty_TailTime: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(Float64))) return kAudioUnitErr_InvalidPropertyValue;
            ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
            const Float64 tailTime = audioUnit ? (Float64)audioUnit.tailTime : 0.0;
            std::memcpy(outData, &tailTime, sizeof(tailTime));
            *ioDataSize = (UInt32)sizeof(tailTime);
            return noErr;
        }
        case kAudioUnitProperty_SupportedNumChannels: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            static const std::array<AUChannelInfo, 2> channelInfo = {{{0, 1}, {0, 2}}};
            const UInt32 bytes = (UInt32)(channelInfo.size() * sizeof(AUChannelInfo));
            if (!requireSize(bytes)) return kAudioUnitErr_InvalidPropertyValue;
            std::memcpy(outData, channelInfo.data(), bytes);
            *ioDataSize = bytes;
            return noErr;
        }
        case kAudioUnitProperty_MaximumFramesPerSlice: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(UInt32))) return kAudioUnitErr_InvalidPropertyValue;
            const UInt32 maxFrames = impl->maxFramesPerSlice.load(std::memory_order_acquire);
            std::memcpy(outData, &maxFrames, sizeof(maxFrames));
            *ioDataSize = (UInt32)sizeof(maxFrames);
            return noErr;
        }
        case kAudioUnitProperty_FactoryPresets: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(CFArrayRef))) return kAudioUnitErr_InvalidPropertyValue;
            CFArrayRef presets = cachedFactoryPresetArray(componentFlavorForInstance(impl)); // Create Rule: host owns and releases.
            std::memcpy(outData, &presets, sizeof(presets));
            *ioDataSize = (UInt32)sizeof(presets);
            return noErr;
        }
        case kAudioUnitProperty_PresentPreset: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(AUPreset))) return kAudioUnitErr_InvalidPropertyValue;
            (void)synchronizeAuv2PresetCacheFromWrappedAudioUnit(impl);
            AUPreset preset{};
            (void)fillPresentPresetForInstance(impl, &preset);
            std::memcpy(outData, &preset, sizeof(preset));
            *ioDataSize = (UInt32)sizeof(preset);
            return noErr;
        }
        case kAudioUnitProperty_ContextName: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(CFStringRef))) return kAudioUnitErr_InvalidPropertyValue;
            NSString* contextName = contextNameForInstance(impl);
            CFStringRef value = contextName ? (__bridge_retained CFStringRef)[contextName copy] : nullptr;
            std::memcpy(outData, &value, sizeof(value));
            *ioDataSize = (UInt32)sizeof(value);
            return noErr;
        }
        case kAudioUnitProperty_CocoaUI: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(AudioUnitCocoaViewInfo))) return kAudioUnitErr_InvalidPropertyValue;
            AudioUnitCocoaViewInfo info{};
            NSBundle* bundle = [NSBundle bundleWithIdentifier:@"com.arpsid.auv2"];
            if (!bundle) bundle = [NSBundle bundleForClass:[ArpSIDAudioUnit class]];
            if (!bundle) return kAudioUnitErr_InvalidPropertyValue;
            info.mCocoaAUViewBundleLocation = (__bridge_retained CFURLRef)[bundle.bundleURL copy];
            info.mCocoaAUViewClass[0] = (__bridge_retained CFStringRef)[NSString stringWithUTF8String:kCocoaViewFactoryClassName];
            std::memcpy(outData, &info, sizeof(info));
            *ioDataSize = (UInt32)sizeof(info);
            return noErr;
        }
        case kAudioUnitProperty_IconLocation: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(CFURLRef))) return kAudioUnitErr_InvalidPropertyValue;
            NSURL* iconURL = auv2FlavorIconURLForInstance(impl);
            if (!iconURL) return kAudioUnitErr_InvalidPropertyValue;
            CFURLRef value = (__bridge_retained CFURLRef)[iconURL copy];
            std::memcpy(outData, &value, sizeof(value));
            *ioDataSize = (UInt32)sizeof(value);
            return noErr;
        }
        case kAudioUnitProperty_LastRenderError: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(OSStatus))) return kAudioUnitErr_InvalidPropertyValue;
            OSStatus value = impl->lastRenderError.load(std::memory_order_acquire);
            std::memcpy(outData, &value, sizeof(value));
            *ioDataSize = (UInt32)sizeof(value);
            return noErr;
        }
        case kAudioUnitProperty_InPlaceProcessing: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(UInt32))) return kAudioUnitErr_InvalidPropertyValue;
            const UInt32 value = impl->inPlaceProcessing ? 1u : 0u;
            std::memcpy(outData, &value, sizeof(value));
            *ioDataSize = (UInt32)sizeof(value);
            return noErr;
        }
        case kAudioUnitProperty_OfflineRender: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(UInt32))) return kAudioUnitErr_InvalidPropertyValue;
            const UInt32 value = impl->offlineRender ? 1u : 0u;
            std::memcpy(outData, &value, sizeof(value));
            *ioDataSize = (UInt32)sizeof(value);
            return noErr;
        }
        case kAudioUnitProperty_RenderQuality: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(UInt32))) return kAudioUnitErr_InvalidPropertyValue;
            std::memcpy(outData, &impl->renderQuality, sizeof(impl->renderQuality));
            *ioDataSize = (UInt32)sizeof(impl->renderQuality);
            return noErr;
        }
        case kAudioUnitProperty_SupportsMPE: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(UInt32))) return kAudioUnitErr_InvalidPropertyValue;
            ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
            const UInt32 value = (audioUnit && audioUnit.supportsMPE) ? 1u : 0u;
            std::memcpy(outData, &value, sizeof(value));
            *ioDataSize = (UInt32)sizeof(value);
            return noErr;
        }
        case kMusicDeviceProperty_SupportsStartStopNote: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(UInt32))) return kAudioUnitErr_InvalidPropertyValue;
            const UInt32 value = 0u;
            std::memcpy(outData, &value, sizeof(value));
            *ioDataSize = (UInt32)sizeof(value);
            return noErr;
        }
        case kAudioUnitProperty_ParameterStringFromValue: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(AudioUnitParameterStringFromValue))) return kAudioUnitErr_InvalidPropertyValue;
            AudioUnitParameterStringFromValue* sfv = (AudioUnitParameterStringFromValue*)outData;
            if (!validateParameterAddress(sfv->inParamID)) return kAudioUnitErr_InvalidParameter;
            const AudioUnitParameterValue v = sfv->inValue
                ? *sfv->inValue
                : cachedParameterValueForInstance(impl, sfv->inParamID);
            NSString* stringValue = stringForNormalizedParameterValue(sfv->inParamID, v);
            sfv->outString = (__bridge_retained CFStringRef)[stringValue copy];
            *ioDataSize = (UInt32)sizeof(*sfv);
            return noErr;
        }
        case kAudioUnitProperty_ParameterValueFromString: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(AudioUnitParameterValueFromString))) return kAudioUnitErr_InvalidPropertyValue;
            AudioUnitParameterValueFromString* vfs = (AudioUnitParameterValueFromString*)outData;
            if (!validateParameterAddress(vfs->inParamID)) return kAudioUnitErr_InvalidParameter;
            if (!normalizedParameterValueFromString(vfs->inParamID, vfs->inString, &vfs->outValue)) {
                return kAudioUnitErr_InvalidPropertyValue;
            }
            *ioDataSize = (UInt32)sizeof(*vfs);
            return noErr;
        }
        case kPropertyBridgeObject: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (!requireSize((UInt32)sizeof(void*))) return kAudioUnitErr_InvalidPropertyValue;
            ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
            if (!audioUnit) return kAudioUnitErr_Uninitialized;
            void* bridge = (__bridge void*)audioUnit;
            std::memcpy(outData, &bridge, sizeof(bridge));
            *ioDataSize = (UInt32)sizeof(bridge);
            return noErr;
        }
        case kArpSIDPropertyPsidData: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            return kAudioUnitErr_InvalidProperty;
        }
        default:
            return kAudioUnitErr_InvalidProperty;
    }
}

static OSStatus componentSetProperty(void* self,
                                     AudioUnitPropertyID inID,
                                     AudioUnitScope inScope,
                                     AudioUnitElement inElement,
                                     const void* inData,
                                     UInt32 inDataSize) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl || (!inData && inDataSize > 0)) return paramErr;

    switch (inID) {
        case kAudioUnitProperty_ClassInfo:
        case kAudioUnitProperty_ClassInfoFromDocument: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (inDataSize != sizeof(CFDictionaryRef)) return kAudioUnitErr_InvalidPropertyValue;
            // AUAudioUnit may synchronously re-notify the same property while the
            // outer preset/state apply is still active. Swallow that re-entry.
            if (!tryBeginPresetBridgeApply(impl)) return noErr;
            ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
            if (!audioUnit) {
                endPresetBridgeApply(impl);
                return kAudioUnitErr_Uninitialized;
            }
            NSDictionary* wrappedState = (__bridge NSDictionary*)(*(const CFDictionaryRef*)inData);
            NSDictionary* state = unwrapClassInfoDictionary(wrappedState);
            if (shouldRejectStaleClassInfoReplay(impl, wrappedState)) {
                publishAuv2WrappedAuthorityChange(impl);
                notifyPropertyListeners(impl, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
                notifyPropertyListeners(impl, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0);
                notifyPropertyListeners(impl, kAudioUnitProperty_ClassInfoFromDocument, kAudioUnitScope_Global, 0);
                endPresetBridgeApply(impl);
                return noErr;
            }
            ArpSIDViewController* viewController = associatedViewControllerForAudioUnit(audioUnit);
            std::unique_lock<ArpSID::AuditedActivityMutex> activityLock(impl->activityMutex);
            if (viewController) [viewController beginHostPresetApply];
            @try {
                if (inID == kAudioUnitProperty_ClassInfo) {
                    [audioUnit setFullState:state];
                } else {
                    [audioUnit setFullStateForDocument:state];
                }
                publishAuv2WrappedAuthorityChange(impl);
                restorePresetMetadataFromWrappedState(impl, wrappedState);
                refreshParameterCacheFromAudioUnit(impl);
            } @finally {
                if (viewController) [viewController endHostPresetApply];
                endPresetBridgeApply(impl);
            }
            notifyPropertyListeners(impl, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
            notifyPropertyListeners(impl, inID, kAudioUnitScope_Global, 0);
            return noErr;
        }
        case kAudioUnitProperty_StreamFormat: {
            if (const OSStatus status = validateScopeAndElementForStreamProperty(inScope, inElement); status != noErr) return status;
            if (inDataSize != sizeof(AudioStreamBasicDescription)) return kAudioUnitErr_InvalidPropertyValue;
            const auto* asbd = static_cast<const AudioStreamBasicDescription*>(inData);
            if (!isSupportedOutputFormat(*asbd)) return kAudioUnitErr_FormatNotSupported;
            const AudioStreamBasicDescription previousFormat = impl->outputFormat;
            impl->outputFormat = *asbd;
            const OSStatus syncStatus = syncRuntimeConfiguration(impl);
            if (syncStatus != noErr) {
                impl->outputFormat = previousFormat;
                return syncStatus;
            }
            notifyPropertyListeners(impl, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0);
            notifyPropertyListeners(impl, kAudioUnitProperty_SampleRate, kAudioUnitScope_Output, 0);
            return noErr;
        }
        case kAudioUnitProperty_SampleRate: {
            if (const OSStatus status = validateScopeAndElementForStreamProperty(inScope, inElement); status != noErr) return status;
            if (inDataSize != sizeof(Float64)) return kAudioUnitErr_InvalidPropertyValue;
            const Float64 sampleRate = *(const Float64*)inData;
            AudioStreamBasicDescription format = impl->outputFormat;
            format.mSampleRate = sampleRate;
            if (!isSupportedOutputFormat(format)) return kAudioUnitErr_FormatNotSupported;
            const AudioStreamBasicDescription previousFormat = impl->outputFormat;
            impl->outputFormat = format;
            const OSStatus syncStatus = syncRuntimeConfiguration(impl);
            if (syncStatus != noErr) {
                impl->outputFormat = previousFormat;
                return syncStatus;
            }
            notifyPropertyListeners(impl, kAudioUnitProperty_SampleRate, kAudioUnitScope_Output, 0);
            notifyPropertyListeners(impl, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0);
            return noErr;
        }
        case kAudioUnitProperty_MaximumFramesPerSlice: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (inDataSize != sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
            const UInt32 maxFrames = sanitizeAuv2MaxFrames(*(const UInt32*)inData);
            const UInt32 previousMaxFrames = impl->maxFramesPerSlice.load(std::memory_order_acquire);
            impl->maxFramesPerSlice.store(maxFrames, std::memory_order_release);
            const OSStatus syncStatus = syncRuntimeConfiguration(impl);
            if (syncStatus != noErr) {
                impl->maxFramesPerSlice.store(previousMaxFrames, std::memory_order_release);
                return syncStatus;
            }
            notifyPropertyListeners(impl, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0);
            return noErr;
        }
        case kAudioUnitProperty_PresentPreset: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (inDataSize != sizeof(AUPreset)) return kAudioUnitErr_InvalidPropertyValue;
            // AUAudioUnit may synchronously re-notify PresentPreset from inside
            // setCurrentPreset:. Swallow that re-entry instead of recursing.
            if (!tryBeginPresetBridgeApply(impl)) return noErr;
            const AUPreset* preset = static_cast<const AUPreset*>(inData);
            // A negative number is a host-owned user preset: the host restores
            // its state through ClassInfo and only labels it here. Record the
            // name (reported back by PresentPreset and in ClassInfo) and leave
            // the audible state and the factory-slot authority untouched.
            if (preset->presetNumber < 0) {
                NSString* userName = preset->presetName ? (__bridge NSString*)preset->presetName : nil;
                setUserPresentPresetName(impl, userName.length > 0 ? userName : @"User Preset");
                endPresetBridgeApply(impl);
                notifyPropertyListeners(impl, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
                return noErr;
            }
            setUserPresentPresetName(impl, nil); // explicit factory preset selection
            const ArpSID::ComponentFlavor flavor = componentFlavorForInstance(impl);
            const NSInteger requestedNumber = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)preset->presetNumber);
            const NSInteger safeNumber = auv2FactorySlotAllowedForFlavor(flavor, requestedNumber)
                ? requestedNumber
                : auv2StartupFactorySlotForFlavor(flavor);
            NSString* incomingName = nil;
            if (preset->presetName) incomingName = (__bridge NSString*)preset->presetName;

            // PresentPreset is an ambiguous host metadata channel. Logic may replay
            // stale preset numbers at transport boundaries; explicit preset authority
            // must remain the only source allowed to change the audible factory slot.
            // Critical AUv2/AUv3 bridge nuance: the user may have selected the preset
            // in the Cocoa GUI directly on the wrapped AUv3. In that case impl's
            // AUv2 mirror can still say "not explicit", while the wrapped AU is the
            // real sticky authority. Pull that sticky slot before deciding whether
            // a host PresentPreset write is intent or replay.
            const bool wrappedHasStickyPreset = synchronizeAuv2PresetCacheFromWrappedAudioUnit(impl);
            const NSInteger stickySlot = cachedPinnedPresetSlotForInstance(impl);
            bool explicitImplPreset = false;
            snapshotPresentPresetState(impl, nullptr, nullptr, &explicitImplPreset);
            const bool explicitPresetAuthority = explicitImplPreset || wrappedHasStickyPreset;
            const bool transportMoving = auv2HostTransportIsMoving(impl);
            const bool stalePresetReplay = explicitPresetAuthority && transportMoving && safeNumber != stickySlot;
            const bool metadataEcho = explicitPresetAuthority && safeNumber == stickySlot;
            if (stalePresetReplay || metadataEcho) {
                // Protect the explicit sticky preset authority, including a
                // deliberate slot-0 selection mirrored from the wrapped AUv3 GUI.
                // Stopped host PresentPreset changes are real selection intent;
                // only moving-transport mismatches are treated as stale replay.
                cachePresentPresetState(impl, stickySlot, defaultFactoryPresetNameForNumber(stickySlot, flavor), true);
                endPresetBridgeApply(impl);
                return noErr;
            }

            // Defensive fallback for pre-sticky construction only. During transport, any
            // PresentPreset write is still replay, not explicit user intent.
            if (auv2HostTransportIsMoving(impl)) {
                endPresetBridgeApply(impl);
                return noErr;
            }

            AUAudioUnitPreset* target = makeFactoryPresetObject(safeNumber, incomingName, flavor);
            cachePresentPresetStateForPreset(impl, target);
            publishAuv2WrappedAuthorityChange(impl);
            ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
            ArpSIDViewController* viewController = audioUnit ? associatedViewControllerForAudioUnit(audioUnit) : nil;
            std::unique_lock<ArpSID::AuditedActivityMutex> activityLock(impl->activityMutex);
            if (viewController) [viewController beginHostPresetApply];
            BOOL appliedAudibleFactoryPreset = NO;
            @try {
                ArpSIDAudioUnit* presetAU = retainedAudioUnitForInstance(impl);
                if (presetAU && [presetAU respondsToSelector:@selector(applyUserFactoryPresetNumber:)]) {
                    appliedAudibleFactoryPreset = [presetAU applyUserFactoryPresetNumber:safeNumber];
                } else if (presetAU && [presetAU respondsToSelector:@selector(setRestoredFactoryPresetMetadataOnlyForSlot:)]) {
                    (void)[presetAU setRestoredFactoryPresetMetadataOnlyForSlot:safeNumber];
                }
                refreshParameterCacheFromAudioUnit(impl);
                if (appliedAudibleFactoryPreset) {
                    cacheParameterValuesFromFactoryPresetRoot(impl, safeNumber);
                }
            } @finally {
                if (viewController) [viewController endHostPresetApply];
                endPresetBridgeApply(impl);
            }
            notifyPropertyListeners(impl, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);
            notifyPropertyListeners(impl, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0);
            notifyPropertyListeners(impl, kAudioUnitProperty_ClassInfoFromDocument, kAudioUnitScope_Global, 0);
            return noErr;
        }
        case kAudioUnitProperty_HostCallbacks: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (inDataSize != sizeof(HostCallbackInfo)) return kAudioUnitErr_InvalidPropertyValue;
            std::unique_lock<ArpSID::AuditedActivityMutex> activityLock(impl->activityMutex);
            stopAuv2HostTransportPoller(impl);
            impl->hostCallbacks = *(const HostCallbackInfo*)inData;
            syncHostCallbacksIntoAudioUnit(impl);
            return noErr;
        }
        case kAudioUnitProperty_ContextName: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (inDataSize != sizeof(CFStringRef)) return kAudioUnitErr_InvalidPropertyValue;
            CFStringRef cfName = *(const CFStringRef*)inData;
            const OSStatus status = setContextNameForInstance(impl, cfName ? (__bridge NSString*)cfName : nil);
            if (status == noErr) {
                notifyPropertyListeners(impl, kAudioUnitProperty_ContextName, kAudioUnitScope_Global, 0);
            }
            return status;
        }
        case kAudioUnitProperty_InPlaceProcessing: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (inDataSize != sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
            impl->inPlaceProcessing = (*(const UInt32*)inData) != 0;
            return noErr;
        }
        case kAudioUnitProperty_OfflineRender: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (inDataSize != sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
            impl->offlineRender = (*(const UInt32*)inData) != 0;
            return noErr;
        }
        case kAudioUnitProperty_RenderQuality: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            if (inDataSize != sizeof(UInt32)) return kAudioUnitErr_InvalidPropertyValue;
            impl->renderQuality = *(const UInt32*)inData;
            return noErr;
        }
        case kArpSIDPropertyPsidData: {
            if (!validateGlobalScope(inScope, inElement)) return kAudioUnitErr_InvalidScope;
            ArpSID::ArpSIDDSPKernel* kernel = loadPublishedKernelPointer(impl);
            if (!kernel) return kAudioUnitErr_Uninitialized;
            if (!inData || inDataSize == 0u) {
                kernel->unloadPsid();
                notifyPropertyListeners(impl, kArpSIDPropertyPsidData, kAudioUnitScope_Global, 0);
                return noErr;
            }
            const bool ok = kernel->loadPsidData(inData, static_cast<size_t>(inDataSize), 0u);
            if (!ok) return kAudioUnitErr_InvalidPropertyValue;
            notifyPropertyListeners(impl, kArpSIDPropertyPsidData, kAudioUnitScope_Global, 0);
            return noErr;
        }
        default:
            return kAudioUnitErr_InvalidProperty;
    }
}

static OSStatus componentGetParameter(void* self,
                                      AudioUnitParameterID inID,
                                      AudioUnitScope inScope,
                                      AudioUnitElement inElement,
                                      AudioUnitParameterValue* outValue) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl || !outValue) return paramErr;
    if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
    if (inElement != 0) return kAudioUnitErr_InvalidElement;
    if (!validateParameterAddress(inID)) return kAudioUnitErr_InvalidParameter;
    if (inID == (AudioUnitParameterID)ArpSID::kParamProgram ||
        inID == (AudioUnitParameterID)ArpSID::kParamBankSlot) {
        *outValue = pinnedPresetParameterValueForInstance(impl, inID);
        return noErr;
    }
    *outValue = cachedParameterValueForInstance(impl, inID);
    return noErr;
}

static OSStatus componentSetParameter(void* self,
                                      AudioUnitParameterID inID,
                                      AudioUnitScope inScope,
                                      AudioUnitElement inElement,
                                      AudioUnitParameterValue inValue,
                                      UInt32 inBufferOffsetInFrames) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (inScope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
    if (inElement != 0) return kAudioUnitErr_InvalidElement;
    if (!impl || !validateParameterAddress(inID)) return kAudioUnitErr_InvalidParameter;
    const float value = ArpSID::sanitizeNormalizedParamValue((int)inID, inValue, ArpSID::defaultNormalizedParamValue((int)inID));
    // Program/BankSlot host writes are metadata replay, not preset
    // authority. Do not cache or enqueue them; otherwise UI/readback still jumps
    // to piano/GM patch numbers on Play even though currentPreset is protected.
    if (inID == (AudioUnitParameterID)ArpSID::kParamProgram ||
        inID == (AudioUnitParameterID)ArpSID::kParamBankSlot) {
        return noErr;
    }
    ArpSID::ArpSIDDSPKernel* kernel = loadPublishedKernelPointer(impl);
    if (!kernel) return kAudioUnitErr_Uninitialized;
    const bool queued = kernel->enqueueParameterIntent((int)inID, value, (int32_t)inBufferOffsetInFrames, 0);
    cacheParameterValueForInstance(impl, inID, value);
    if (!queued) {
        // Logic can replay large parameter sets while building the instrument
        // graph. The kernel already marked the value dirty before attempting
        // the timed queue push, so a full queue means lost sample-accurate
        // timing, not a failed host call. Keep the AUv2 surface noErr and let
        // kernel telemetry report the dirty-flush fallback.
    }
    return noErr;
}

static OSStatus componentScheduleParameters(void* self,
                                            const AudioUnitParameterEvent* events,
                                            UInt32 numEvents) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl || (!events && numEvents > 0)) return paramErr;
    ArpSID::ArpSIDDSPKernel* kernel = loadPublishedKernelPointer(impl);
    if (!kernel) return kAudioUnitErr_Uninitialized;

    for (UInt32 i = 0; i < numEvents; ++i) {
        const AudioUnitParameterEvent& ev = events[i];
        if (ev.scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
        if (ev.element != 0) return kAudioUnitErr_InvalidElement;
        if (!validateParameterAddress(ev.parameter)) return kAudioUnitErr_InvalidParameter;
        switch (ev.eventType) {
            case kParameterEvent_Immediate: {
                if (ev.parameter == (AudioUnitParameterID)ArpSID::kParamProgram ||
                    ev.parameter == (AudioUnitParameterID)ArpSID::kParamBankSlot) {
                    break;
                }
                const float value = ArpSID::sanitizeNormalizedParamValue((int)ev.parameter,
                                                                         ev.eventValues.immediate.value,
                                                                         ArpSID::defaultNormalizedParamValue((int)ev.parameter));
                const bool queued = kernel->enqueueParameterIntent((int)ev.parameter,
                                                                   value,
                                                                   (int32_t)ev.eventValues.immediate.bufferOffset,
                                                                   0);
                cacheParameterValueForInstance(impl, ev.parameter, value);
                if (!queued) {
                    // Same policy as componentSetParameter: the latest value is
                    // preserved through the kernel dirty-flush fallback, so do
                    // not make Logic treat transient queue pressure as an AU
                    // failure.
                }
                // do not pin AU PresentPreset from scheduled host parameter events.
                break;
            }
            case kParameterEvent_Ramped: {
                if (ev.parameter == (AudioUnitParameterID)ArpSID::kParamProgram ||
                    ev.parameter == (AudioUnitParameterID)ArpSID::kParamBankSlot) {
                    break;
                }
                const UInt32 duration = std::max<UInt32>(1u, ev.eventValues.ramp.durationInFrames);
                const float startValue = cachedParameterValueForInstance(impl, ev.parameter);
                const float endValue = ArpSID::sanitizeNormalizedParamValue((int)ev.parameter,
                                                                            ev.eventValues.ramp.endValue,
                                                                            startValue);
                const UInt32 anchors = std::min<UInt32>(64u, std::max<UInt32>(2u, duration));
                bool anyDropped = false;
                for (UInt32 anchor = 0; anchor < anchors; ++anchor) {
                    const double t = (anchors <= 1u) ? 1.0 : (double)anchor / (double)(anchors - 1u);
                    const UInt32 sampleOffset = ev.eventValues.ramp.startBufferOffset +
                        (UInt32)std::llround((double)(duration - 1u) * t);
                    const float value = ArpSID::sanitizeNormalizedParamValue((int)ev.parameter,
                                                                              startValue + (endValue - startValue) * (float)t,
                                                                              startValue);
                    impl->auv2RampAnchorScheduledCount.fetch_add(1u, std::memory_order_relaxed);
                    const bool queued = kernel->enqueueParameterIntent((int)ev.parameter, value, (int32_t)sampleOffset, 0);
                    if (!queued) {
                        anyDropped = true;
                        impl->auv2RampAnchorDroppedCount.fetch_add(1u, std::memory_order_relaxed);
                    }
                }
                cacheParameterValueForInstance(impl, ev.parameter, endValue);
                if (anyDropped) {
                    // Preserve host stability: anchor-drop telemetry records
                    // the timing loss, while the end value still reaches the
                    // kernel via the dirty-flush fallback.
                }
                // do not pin AU PresentPreset from scheduled host parameter ramps.
                break;
            }
            default:
                break;
        }
    }
    return noErr;
}

static OSStatus componentRender(void* self,
                                AudioUnitRenderActionFlags* ioActionFlags,
                                const AudioTimeStamp* inTimeStamp,
                                UInt32 inOutputBusNumber,
                                UInt32 inNumberFrames,
                                AudioBufferList* ioData) {
    if (ioActionFlags) *ioActionFlags = auv2SanitizeHostRenderFlags(*ioActionFlags);
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl || !ioData) return auv2FailRender(impl, ioActionFlags, ioData, paramErr, inNumberFrames);
    // Audit #48: every render-entry path — including the fail-closed
    // early-exit paths below that write zero buffers into the host's
    // AudioBufferList — must increment activeRenderUsers so a concurrent
    // close cannot race the buffer write. The legacy code put this scope
    // construction at the bottom of the readiness checks (after a dozen
    // possible early returns that all wrote to host memory); move it
    // here so it covers the full body.
    ScopedAuv2RenderUse renderUse(impl);
    if (inOutputBusNumber != 0) {
        return auv2FailRender(impl, ioActionFlags, ioData, kAudioUnitErr_InvalidElement, inNumberFrames);
    }
    const UInt32 maxFrames = impl->maxFramesPerSlice.load(std::memory_order_acquire);
    const UInt32 renderedChannels = std::max<UInt32>(1u, std::min<UInt32>(2u, impl->outputFormat.mChannelsPerFrame));
    if (inNumberFrames == 0) {
        return noErr;
    }
    if (maxFrames == 0u) {
        return auv2FailClosedSilentNoErr(impl,
                                         ioActionFlags,
                                         ioData,
                                         inNumberFrames,
                                         renderedChannels,
                                         kAudioUnitErr_Uninitialized,
                                         &impl->renderReadinessDiagnosticCount);
    }
    if (inNumberFrames > maxFrames || inNumberFrames > kAuv2HardMaxFrames) {
        return auv2FailRender(impl, ioActionFlags, ioData, kAudioUnitErr_TooManyFramesToProcess, inNumberFrames);
    }

    // AUv2 v420 auval slicing guard: prepare backing memory before any
    // diagnostic fail-closed return. auval can supply AudioBufferList entries
    // with mData == nullptr during the 64-frame sliced-render probe. Readiness
    // and bridge-snapshot diagnostics must therefore not call the fail-closed
    // zeroing helpers against raw host buffers before scratch has been attached.
    // This preserves the v405/v417 policy: valid AUv2 slice + transient internal
    // authority/readiness issue => silence + noErr, never -10867 and never a
    // null-buffer crash.
    // Audit #5: explicit pre-check that the pre-allocated scratch can hold
    // worst-case (frames × max_channels) BEFORE prepareOutputBufferList()
    // runs. The scratch is sized at maxFrames-establishment time via
    // ensureFloatScratchCapacityNonRealtime(); if the host hands us an
    // inNumberFrames larger than that contract, fail-closed early with a
    // dedicated diagnostic counter so "scratch too small" is distinguishable
    // from generic readiness failures. This makes the failure deterministic
    // and host-attributable instead of a mid-render fall-through.
    {
        const size_t neededStereo = static_cast<size_t>(inNumberFrames) * 2u;
        const size_t neededPlanar = static_cast<size_t>(inNumberFrames) *
                                    std::max<UInt32>(1u, renderedChannels);
        const bool stereoOk = impl->ownedInterleavedScratch.hasCapacity(neededStereo);
        const bool planarOk = impl->ownedPlanarScratch.hasCapacity(neededPlanar);
        if (!stereoOk || !planarOk) {
            impl->scratchUnderCapacityCount.fetch_add(1u, std::memory_order_relaxed);
            return auv2FailClosedSilentNoErr(impl,
                                             ioActionFlags,
                                             ioData,
                                             inNumberFrames,
                                             renderedChannels,
                                             kAudioUnitErr_TooManyFramesToProcess,
                                             &impl->renderOutputBufferDiagnosticCount);
        }
    }
    // Source-shape compatibility: prepareOutputBufferList(ioData, inNumberFrames, renderedChannels
    bool attachedScratch = false;
    AudioBufferList* output = prepareOutputBufferList(ioData,
                                                      inNumberFrames,
                                                      renderedChannels,
                                                      impl->ownedPlanarScratch,
                                                      impl->ownedInterleavedScratch,
                                                      &attachedScratch);
    if (attachedScratch) {
        // Audit #50: surface the host-buffer mutation event so diagnostic
        // tooling can see how often hosts hand us malformed AudioBufferLists.
        impl->hostBufferScratchAttachCount.fetch_add(1u, std::memory_order_relaxed);
    }
    if (!output) {
        return auv2FailClosedSilentNoErr(impl,
                                         ioActionFlags,
                                         ioData,
                                         inNumberFrames,
                                         renderedChannels,
                                         kAudioUnitErr_TooManyFramesToProcess,
                                         &impl->renderOutputBufferDiagnosticCount);
    }

    // AUv2 v421 sliced-render guard: keep true configuration errors real.
    // True contract errors remain host-visible AU errors; only transient
    // bridge/render-block publication races become silent diagnostics.
    // but convert transient bridge-authority/render-block publication races to
    // silent noErr after output backing is prepared. auval's 64-frame slicing
    // probe may supply mData == nullptr, so every noErr path below must hand
    // back valid backing memory.
    if (impl->isClosing.load(std::memory_order_acquire) ||
        !impl->initialized.load(std::memory_order_acquire)) {
        return auv2FailClosedSilentNoErr(impl,
                                         ioActionFlags,
                                         output,
                                         inNumberFrames,
                                         renderedChannels,
                                         kAudioUnitErr_Uninitialized,
                                         &impl->renderReadinessDiagnosticCount);
    }
    if (!impl->outputScratchReady.load(std::memory_order_acquire)) {
        return auv2FailClosedSilentNoErr(impl,
                                         ioActionFlags,
                                         output,
                                         inNumberFrames,
                                         renderedChannels,
                                         kAudioUnitErr_Uninitialized,
                                         &impl->renderReadinessDiagnosticCount);
    }

    Auv2StateSnapshotValue bridgeSnapshot{};
    if (!readAuv2StateSnapshotStable(impl, bridgeSnapshot) ||
        !auv2BridgeSnapshotInvariantHolds(bridgeSnapshot)) {
        return auv2FailClosedSilentNoErr(impl,
                                         ioActionFlags,
                                         output,
                                         inNumberFrames,
                                         renderedChannels,
                                         kAudioUnitErr_Uninitialized,
                                         &impl->renderReadinessDiagnosticCount);
    }
    const uint64_t renderGenAtEntry = bridgeSnapshot.renderGeneration;
    const uint64_t wrappedGenAtEntry = bridgeSnapshot.wrappedGeneration;
    // Audit #1: capture the unified render epoch at entry. This snapshot
    // synchronizes-with every non-RT state-change path that calls
    // `impl->renderEpoch.bump()`. After the wrapped render block returns we
    // verify the epoch held. A broken epoch is the canonical detection signal
    // alongside the seqlock-snapshot delta check below; successful rendered
    // audio is preserved while the authority change is reported as telemetry.
    ArpSID::RenderEpochScope renderEpochScope(impl->renderEpoch);
    AUInternalRenderBlock renderBlock = loadPublishedRenderBlock(impl);
    if (!renderBlock) {
        return auv2FailClosedSilentNoErr(impl,
                                         ioActionFlags,
                                         output,
                                         inNumberFrames,
                                         renderedChannels,
                                         kAudioUnitErr_Uninitialized,
                                         &impl->renderReadinessDiagnosticCount);
    }

    // Audit #48: `renderUse` is already in scope from the top of
    // componentRender (covers every fail-closed/early-exit path above).
    ArpSID::SidRealtimeScope arpsidRtScope_("ArpSIDAUv2Component::componentRender");
    Auv2RealtimeGuardSliceAudit realtimeAudit(impl);
    // AUv2 v391: SidRealtimeScope makes allocation/lock/host-callback guard checks active.
    ArpSID::requireSidTablesPrewarmedForRealtime("ArpSIDAUv2Component::componentRender missing SID table prewarm");
    if (realtimeAudit.violationObserved()) {
        realtimeAudit.markCounted();
        return auv2FailClosedSilentNoErr(impl,
                                         ioActionFlags,
                                         output,
                                         inNumberFrames,
                                         renderedChannels,
                                         kAudioUnitErr_CannotDoInCurrentContext,
                                         &impl->realtimeGuardViolationSliceCount);
    }
    AudioTimeStamp renderTimeStamp{};
    const AudioTimeStamp* effectiveTimeStamp = prepareRenderTimestamp(impl, inTimeStamp, renderTimeStamp);
    // Never call Logic/AUv2 legacy transport callbacks on the render thread.
    // A non-render poller publishes host transport into atomics; the AUv3
    // render block consumes those atomics through hostTransportSnapshotAccess.
    AudioUnitRenderActionFlags preFlags = ioActionFlags ? *ioActionFlags : 0;
    preFlags |= kAudioUnitRenderAction_PreRender;
    const OSStatus preNotifyStatus = callAuv2RenderNotifyTable(impl,
                                                               &preFlags,
                                                               effectiveTimeStamp,
                                                               inOutputBusNumber,
                                                               inNumberFrames,
                                                               output);
    if (ioActionFlags) {
        *ioActionFlags = (preFlags & ~kAudioUnitRenderAction_PreRender);
    }
    if (preNotifyStatus != noErr) {
        // Audit #49: a pre-render notify callback failed. The legacy path
        // re-invoked the (untrusted, potentially misbehaving) notify table
        // a SECOND time with PostRender+PostRenderError flags before
        // fail-closing — calling client code again from inside an error
        // path is exactly what the audit identified as an attack surface
        // (one bad callback could now run twice per failed render).
        //
        // The fix: zero the output, set OutputIsSilence on the action
        // flags so the host knows what we produced, increment the
        // diagnostic counter that records the failure (also tracks WHICH
        // callback misbehaved via the per-callback attribution from
        // skive 5), and fail-close. We do NOT call back into the notify
        // table on the error path. Observers that need to know about
        // failed renders can rely on the kAudioUnitRenderAction_OutputIsSilence
        // bit + the returned OSStatus; AUv2's contract does not require
        // a post-render-error callback when pre-render itself failed.
        auv2ZeroRenderedAudioBufferList(output, inNumberFrames, renderedChannels);
        if (ioActionFlags) {
            *ioActionFlags |= kAudioUnitRenderAction_OutputIsSilence;
        }
        impl->preNotifyFailureCount.fetch_add(1u, std::memory_order_relaxed);
        return auv2FailClosedSilentNoErr(impl,
                                           ioActionFlags,
                                           output,
                                           inNumberFrames,
                                           renderedChannels,
                                           preNotifyStatus,
                                           &impl->renderNotifyDiagnosticCount);
    }
    // AUv2 v391: forensic/thermal parameters are already in the canonical
    // parameter ingress/cache and are consumed by the shared kernel per block.
    // Render performs no ObjC/host calls and no allocation here.
    const AURenderEvent* realtimeEventListHead = nullptr;

    // The wrapped kernel is the sole owner of MIDI, sequencer, automation,
    // transport, preset, and synthesis state. A separate wrapper-side engine
    // cannot safely replace it without duplicating those ordered event streams.
    const AUAudioUnitStatus status = renderBlock(ioActionFlags,
                                                  effectiveTimeStamp,
                                                  (AVAudioFrameCount)inNumberFrames,
                                                  (NSInteger)inOutputBusNumber,
                                                  output,
                                                  realtimeEventListHead,
                                                  nil);
    Auv2StateSnapshotValue bridgeSnapshotAfter{};
    const bool stableAfter = readAuv2StateSnapshotStable(impl, bridgeSnapshotAfter);
    const bool renderEpochHeld = renderEpochScope.held();
    const bool postRenderAuthorityChanged =
        !stableAfter ||
        !auv2BridgeSnapshotInvariantHolds(bridgeSnapshotAfter) ||
        bridgeSnapshotAfter.renderGeneration != renderGenAtEntry ||
        bridgeSnapshotAfter.wrappedGeneration != wrappedGenAtEntry ||
        bridgeSnapshotAfter.maxFrames != bridgeSnapshot.maxFrames ||
        !renderEpochHeld;
    if (postRenderAuthorityChanged) {
        // The wrapped AU/kernel has already advanced one block. Zeroing here
        // creates a guaranteed audible discontinuity, so preserve successful
        // rendered audio and surface the authority change as telemetry.
        impl->splitBrainDiagnosticCount.fetch_add(1u, std::memory_order_relaxed);
    }
    if (status != noErr) {
        // A failed render cannot be trusted to have produced valid samples.
        // Zero before post-render-error notification and before final silence
        // flag evaluation.
        auv2ZeroRenderedAudioBufferList(output, inNumberFrames, renderedChannels);
    }
    AudioUnitRenderActionFlags postFlags = ioActionFlags ? *ioActionFlags : 0;
    postFlags |= kAudioUnitRenderAction_PostRender;
    if (status != noErr) postFlags |= (kAudioUnitRenderAction_PostRenderError |
                                       kAudioUnitRenderAction_OutputIsSilence);
    const OSStatus postNotifyStatus = callAuv2RenderNotifyTable(impl,
                                                                &postFlags,
                                                                effectiveTimeStamp,
                                                                inOutputBusNumber,
                                                                inNumberFrames,
                                                                output);
    if (status == noErr && postNotifyStatus != noErr) postFlags |= kAudioUnitRenderAction_PostRenderError;
    if (ioActionFlags) {
        *ioActionFlags = (postFlags & ~(kAudioUnitRenderAction_PostRender | kAudioUnitRenderAction_PostRenderError));
    }
    commitRenderTimestamp(impl, effectiveTimeStamp, inNumberFrames);
    // The ONLY legitimate post-render fail-closed-silent path: the wrapped
    // render itself failed, so its output is untrustworthy (already zeroed at
    // the `status != noErr` block above). Evaluate this FIRST so a genuine
    // render failure is never preserved as audio.
    if ((OSStatus)status != noErr) {
        return auv2FailClosedSilentNoErr(impl,
                                           ioActionFlags,
                                           output,
                                           inNumberFrames,
                                           renderedChannels,
                                           (OSStatus)status,
                                           &impl->wrappedRenderDiagnosticCount);
    }
    // Render SUCCEEDED and state has already advanced (commitRenderTimestamp).
    // A realtime-guard violation or a post-notify failure observed AFTER this
    // point must NOT silence already-valid audio — that would inject an audible
    // dropout and discard good samples for a deterministic bounce/replay over a
    // condition that did not corrupt the rendered block. Record telemetry only
    // and preserve the audio.
    //
    // The realtime-guard violation (if any) is counted by the
    // Auv2RealtimeGuardSliceAudit destructor at scope exit (markCounted() is NOT
    // called here, so the RAII path records it exactly once).
    if (postNotifyStatus != noErr) {
        impl->renderNotifyDiagnosticCount.fetch_add(1u, std::memory_order_relaxed);
    }
    auv2UpdateSilenceFlag(ioActionFlags, output, inNumberFrames, noErr, false);
    const OSStatus finalStatus = noErr;
    impl->lastRenderError.store(finalStatus, std::memory_order_release);

    // v600: push AUv2 host-layer diagnostic counters via raw atomics. No
    // Objective-C message dispatch is allowed from componentRender.
    {
        const auto diag = impl->publishedAuv2DiagTargets;
        if (diag.renderEpoch) diag.renderEpoch->store(impl->renderEpoch.currentForTesting(), std::memory_order_relaxed);
        if (diag.notifyCallbackViolationCount) diag.notifyCallbackViolationCount->store(impl->notifyCallbackViolationCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
        if (diag.renderDrainTimeoutCount) diag.renderDrainTimeoutCount->store(impl->renderDrainTimeoutCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
        if (diag.splitBrainDiagnosticCount) diag.splitBrainDiagnosticCount->store(impl->splitBrainDiagnosticCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
        if (diag.scratchUnderCapacityCountAuv2) diag.scratchUnderCapacityCountAuv2->store(impl->scratchUnderCapacityCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
        if (diag.preNotifyFailureCount) diag.preNotifyFailureCount->store(impl->preNotifyFailureCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
        if (diag.hostBufferScratchAttachCount) diag.hostBufferScratchAttachCount->store(impl->hostBufferScratchAttachCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
        if (diag.bridgeDivertedRenderCount) diag.bridgeDivertedRenderCount->store(impl->bridgeDivertedRenderCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
        if (diag.activityMutexRtViolations) diag.activityMutexRtViolations->store(impl->activityMutex.rtViolationCount(), std::memory_order_relaxed);
        if (diag.stateMutexRtViolations) diag.stateMutexRtViolations->store(impl->stateMutex.rtViolationCount(), std::memory_order_relaxed);
        if (diag.propListenerMutexRtViolations) diag.propListenerMutexRtViolations->store(impl->propertyListenerMutex.rtViolationCount(), std::memory_order_relaxed);
        if (diag.closeWaitMutexRtViolations) diag.closeWaitMutexRtViolations->store(impl->closeWaitMutex.rtViolationCount(), std::memory_order_relaxed);
    }

    return finalStatus;
}

static OSStatus componentReset(void* self, AudioUnitScope inScope, AudioUnitElement inElement) {
    (void)inScope;
    (void)inElement;
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl) return paramErr;
    suspendAuv2RenderingAndWait(impl);
    std::unique_lock<ArpSID::AuditedActivityMutex> activityLock(impl->activityMutex);
    ArpSIDDSPKernelAdapter* adapter = retainedAdapterForInstance(impl);
    if (!adapter) {
        // AUv2 v419: Reset is a lifecycle boundary, not a reason to poison
        // auval with -10867/-50 when the wrapper is otherwise open and has a
        // valid output contract. Keep the AUv2 wrapper initialized and let the
        // next render slice fail closed with silence/noErr while diagnostics
        // record the missing wrapped adapter.
        impl->lastRenderError.store(noErr, std::memory_order_release);
        impl->renderReadinessDiagnosticCount.fetch_add(1u, std::memory_order_relaxed);
        if (!impl->isClosing.load(std::memory_order_acquire)) {
            impl->initialized.store(true, std::memory_order_release);
            publishAuv2StateSnapshot(impl);
        }
        return noErr;
    }

    // Logic may call AudioUnitReset at transport boundaries. The reset-retention
    // snapshot must be taken from the live AUAudioUnit surface immediately before
    // reset, not from potentially stale AUv2 mirror storage. Program/BankSlot are
    // read through sticky preset authority, so host metadata cannot become preset
    // authority here.
    // Critical bridge fix: GUI preset selection happens on the wrapped AUv3 first.
    // Pull that sticky authority into the AUv2 mirror before taking the reset
    // snapshot; otherwise Logic Play/Reset can rebuild slot 0 while the GUI still
    // displays the correct patch.
    const bool wrappedHasStickyPreset = synchronizeAuv2PresetCacheFromWrappedAudioUnit(impl);
    refreshParameterCacheFromAudioUnit(impl);
    const Auv2ParameterSnapshot resetSnapshot = snapshotParameterCacheForReset(impl);
    const BOOL hasExplicitFactorySlot = (impl->hasExplicitPresetSelection || wrappedHasStickyPreset) ? YES : NO;
    const NSInteger explicitFactorySlot = hasExplicitFactorySlot
        ? cachedPinnedPresetSlotForInstance(impl)
        : -1;

    // AU Reset must preserve the currently audible preset root, not merely
    // restore a flat parameter cache after a destructive engine reset. SID, DrSID
    // and register backends may need the factory SidStateRoot re-projected before
    // edited host parameters are overlaid.
    if ([adapter respondsToSelector:@selector(resetPreservingParameterSnapshot:count:factorySlot:hasExplicitFactorySlot:)]) {
        [adapter resetPreservingParameterSnapshot:resetSnapshot.data()
                                            count:ArpSID::kNumParams
                                      factorySlot:explicitFactorySlot
                          hasExplicitFactorySlot:hasExplicitFactorySlot];
    } else {
        [adapter reset];
        restoreParameterCacheAfterReset(impl, adapter, resetSnapshot);
    }
    // Keep the AUv2 shim mirror synchronized with the same retained snapshot.
    // This mirror is read by GetParameter/GetProperty and must not be allowed to
    // drift back to default slot 0 after the DSP has been preserved.
    restoreParameterCacheAfterReset(impl, nil, resetSnapshot);
    resetPendingRenderState(impl);

    // Do not re-enter the configuration/allocation path from AudioUnitReset.
    // Reset has already preserved the audible preset; this stage only republishes
    // the already configured render entry points.
    ArpSIDAudioUnit* audioUnit = retainedAudioUnitForInstance(impl);
    if (!audioUnit) {
        impl->lastRenderError.store(noErr, std::memory_order_release);
        impl->renderReadinessDiagnosticCount.fetch_add(1u, std::memory_order_relaxed);
        if (!impl->isClosing.load(std::memory_order_acquire)) {
            impl->initialized.store(true, std::memory_order_release);
            publishAuv2StateSnapshot(impl);
        }
        return noErr;
    }
    ArpSIDDSPKernelAdapter* liveAdapter = (ArpSIDDSPKernelAdapter*)[audioUnit debugAdapter];
    ArpSID::ArpSIDDSPKernel* liveKernel = liveAdapter ? [liveAdapter kernelPtr] : nullptr;
    {
        std::lock_guard<ArpSID::AuditedStateMutex> lock(impl->stateMutex);
        impl->adapter = liveAdapter;
    }
    const bool hadRenderBlock = (loadPublishedRenderBlock(impl) != nullptr);
    const bool hadKernel = (loadPublishedKernelPointer(impl) != nullptr);
    if (liveKernel) {
        publishKernelPointer(impl, liveKernel);
    }
    publishAdapterPointer(impl, liveAdapter);  // v549
    AUInternalRenderBlock freshRenderBlock = [audioUnit internalRenderBlock];
    if (freshRenderBlock) {
        publishRenderBlock(impl, freshRenderBlock);
    }
    const bool ready = (loadPublishedRenderBlock(impl) && loadPublishedKernelPointer(impl));
    impl->lastRenderError.store(noErr, std::memory_order_release);
    if (!ready || (!freshRenderBlock && !hadRenderBlock) || (!liveKernel && !hadKernel)) {
        impl->renderReadinessDiagnosticCount.fetch_add(1u, std::memory_order_relaxed);
    }
    impl->renderConfigurationGeneration.fetch_add(1u, std::memory_order_acq_rel);
    impl->renderEpoch.bump();
    if (!impl->isClosing.load(std::memory_order_acquire)) {
        impl->initialized.store(true, std::memory_order_release);
        publishAuv2StateSnapshot(impl);
    }
    return noErr;
}

static OSStatus componentAddPropertyListener(void* self,
                                             AudioUnitPropertyID prop,
                                             AudioUnitPropertyListenerProc proc,
                                             void* userData) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl || !proc) return paramErr;
    std::lock_guard<ArpSID::AuditedPropertyListenerMutex> lock(impl->propertyListenerMutex);
    impl->propertyListeners.push_back({prop, proc, userData});
    return noErr;
}

static OSStatus componentRemovePropertyListener(void* self,
                                                AudioUnitPropertyID prop,
                                                AudioUnitPropertyListenerProc proc) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl || !proc) return paramErr;
    std::lock_guard<ArpSID::AuditedPropertyListenerMutex> lock(impl->propertyListenerMutex);
    impl->propertyListeners.erase(
        std::remove_if(impl->propertyListeners.begin(),
                       impl->propertyListeners.end(),
                       [&](const ArpSIDAUv2Instance::PropertyListenerEntry& entry) {
                           return entry.propertyID == prop && entry.proc == proc;
                       }),
        impl->propertyListeners.end());
    return noErr;
}

static OSStatus componentRemovePropertyListenerWithUserData(void* self,
                                                            AudioUnitPropertyID prop,
                                                            AudioUnitPropertyListenerProc proc,
                                                            void* userData) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl || !proc) return paramErr;
    std::lock_guard<ArpSID::AuditedPropertyListenerMutex> lock(impl->propertyListenerMutex);
    impl->propertyListeners.erase(
        std::remove_if(impl->propertyListeners.begin(),
                       impl->propertyListeners.end(),
                       [&](const ArpSIDAUv2Instance::PropertyListenerEntry& entry) {
                           return entry.propertyID == prop && entry.proc == proc && entry.userData == userData;
                       }),
        impl->propertyListeners.end());
    return noErr;
}

static OSStatus componentAddRenderNotify(void* self, AURenderCallback proc, void* userData) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl || !proc) return paramErr;
    std::lock_guard<ArpSID::AuditedPropertyListenerMutex> lock(impl->propertyListenerMutex);
    const auto existing = std::find_if(impl->renderNotifyListeners.begin(),
                                       impl->renderNotifyListeners.end(),
                                       [&](const ArpSIDAUv2Instance::RenderNotifyEntry& entry) {
                                           return entry.proc == proc && entry.userData == userData;
                                       });
    if (existing == impl->renderNotifyListeners.end()) {
        if (impl->renderNotifyListeners.size() >=
            ArpSIDAUv2Instance::PublishedRenderNotifyTable::kCapacity) {
            // Do not surface callback-table pressure to Logic as an AU failure.
            // The table is intentionally generous; if a pathological host
            // exceeds it, keep the component stable and retain existing
            // callbacks rather than triggering Logic's recovery warning.
            return noErr;
        }
        impl->renderNotifyListeners.push_back({proc, userData});
    }
    publishRenderNotifyTableLocked(impl);
    return noErr;
}

static OSStatus componentRemoveRenderNotify(void* self, AURenderCallback proc, void* userData) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl || !proc) return paramErr;
    std::lock_guard<ArpSID::AuditedPropertyListenerMutex> lock(impl->propertyListenerMutex);
    impl->renderNotifyListeners.erase(
        std::remove_if(impl->renderNotifyListeners.begin(),
                       impl->renderNotifyListeners.end(),
                       [&](const ArpSIDAUv2Instance::RenderNotifyEntry& entry) {
                           return entry.proc == proc && entry.userData == userData;
                       }),
        impl->renderNotifyListeners.end());
    publishRenderNotifyTableLocked(impl);
    return noErr;
}

static OSStatus componentMIDIEvent(void* self,
                                   UInt32 inStatus,
                                   UInt32 inData1,
                                   UInt32 inData2,
                                   UInt32 inOffsetSampleFrame) {
    ScopedInstanceUse use(self);
    ArpSIDAUv2Instance* impl = use.get();
    if (!impl) return paramErr;
    ArpSID::ArpSIDDSPKernel* kernel = loadPublishedKernelPointer(impl);
    if (!kernel) return kAudioUnitErr_Uninitialized;

    const uint8_t status = (uint8_t)(inStatus & 0xFFu);
    const uint8_t midiType = status & 0xF0u;
    uint8_t bytes[3] = {
        status,
        (uint8_t)(inData1 & 0x7Fu),
        (uint8_t)(inData2 & 0x7Fu)
    };
    uint8_t length = 3;
    if (midiType == 0xC0u || midiType == 0xD0u) {
        length = 2;
    } else if (status >= 0xF0u) {
        switch (status) {
            case 0xF2u: length = 3; break;
            case 0xF1u:
            case 0xF3u: length = 2; break;
            default:    length = 1; break;
        }
    }
    // v872: clamp the host sample offset before it enters mode-promotion / ingress.
    // A raw (int32_t) cast of a huge UInt32 wraps negative, injecting a bogus
    // negative offset into the kernel. Clamp to [0, maxFramesPerSlice-1] (falling
    // back to INT32_MAX when the max frame count is not yet known).
    const UInt32 offsetClampMax = impl->maxFramesPerSlice.load(std::memory_order_acquire);
    const UInt32 offsetCap = (offsetClampMax > 0u) ? (offsetClampMax - 1u) : (UInt32)0x7FFFFFFFu;
    const int32_t safeOffsetSampleFrame = (int32_t)std::min<UInt32>(inOffsetSampleFrame, offsetCap);
    promoteDrSidModeForIncomingGMNote(impl,
                                      status,
                                      bytes[1],
                                      (length >= 3u ? bytes[2] : 0u),
                                      (UInt32)safeOffsetSampleFrame);
    const bool queued = kernel->enqueueMidiIntent(bytes, length, safeOffsetSampleFrame, 0);
    if (!queued) {
        // A saturated MIDI ring is not a host-call failure. The kernel records
        // overflow telemetry and preserves release-critical note-offs/CC120/123
        // through its fallback ledgers; returning -10863 here makes Logic treat
        // a valid burst of MusicDeviceMIDIEvent calls as an AudioUnit failure.
    }
    return noErr;
}

static AudioComponentMethod lookupSelector(SInt16 selector) {
    switch (selector) {
        case kAudioUnitInitializeSelect: return (AudioComponentMethod)componentInitialize;
        case kAudioUnitUninitializeSelect: return (AudioComponentMethod)componentUninitialize;
        case kAudioUnitGetPropertyInfoSelect: return (AudioComponentMethod)componentGetPropertyInfo;
        case kAudioUnitGetPropertySelect: return (AudioComponentMethod)componentGetProperty;
        case kAudioUnitSetPropertySelect: return (AudioComponentMethod)componentSetProperty;
        case kAudioUnitAddPropertyListenerSelect: return (AudioComponentMethod)componentAddPropertyListener;
        case kAudioUnitRemovePropertyListenerSelect: return (AudioComponentMethod)componentRemovePropertyListener;
        case kAudioUnitRemovePropertyListenerWithUserDataSelect: return (AudioComponentMethod)componentRemovePropertyListenerWithUserData;
        case kAudioUnitAddRenderNotifySelect: return (AudioComponentMethod)componentAddRenderNotify;
        case kAudioUnitRemoveRenderNotifySelect: return (AudioComponentMethod)componentRemoveRenderNotify;
        case kAudioUnitGetParameterSelect: return (AudioComponentMethod)componentGetParameter;
        case kAudioUnitSetParameterSelect: return (AudioComponentMethod)componentSetParameter;
        case kAudioUnitScheduleParametersSelect: return (AudioComponentMethod)componentScheduleParameters;
        case kAudioUnitRenderSelect: return (AudioComponentMethod)componentRender;
        case kAudioUnitResetSelect: return (AudioComponentMethod)componentReset;
        case kMusicDeviceMIDIEventSelect: return (AudioComponentMethod)componentMIDIEvent;
        default: return nullptr;
    }
}

} // namespace

@interface ArpSIDAUv2ViewFactory : NSObject <AUCocoaUIBase>
@end

@implementation ArpSIDAUv2ViewFactory

- (unsigned)interfaceVersion {
    return 0;
}

- (NSString*)description {
    return @"ArpSID AUv2 View";
}

- (NSView*)uiViewForAudioUnit:(AudioUnit)inAudioUnit withSize:(NSSize)inPreferredSize {
    (void)inPreferredSize;
    if (inAudioUnit == NULL) return nil;

    @autoreleasepool {
        void* rawBridge = nullptr;
        UInt32 bridgeSize = (UInt32)sizeof(rawBridge);
        const OSStatus bridgeStatus = AudioUnitGetProperty(inAudioUnit,
                                                           kPropertyBridgeObject,
                                                           kAudioUnitScope_Global,
                                                           0,
                                                           &rawBridge,
                                                           &bridgeSize);
        if (bridgeStatus != noErr || bridgeSize != sizeof(rawBridge) || rawBridge == nullptr) {
            return nil;
        }

        ArpSIDAudioUnit* audioUnit = (__bridge ArpSIDAudioUnit*)rawBridge;
        if (!audioUnit || ![audioUnit isKindOfClass:[ArpSIDAudioUnit class]]) return nil;
        // v795: the AUv2 Cocoa view factory may be entered off-main and then queue
        // editor construction back to the main thread. Do not let those queued
        // blocks retain the AU object purely because view construction timed out
        // or the host tears the component down before main runs; weak-load it at
        // the point of use and fail/dispose best-effort if it has gone away.
        __weak ArpSIDAudioUnit* weakAudioUnit_v795 = audioUnit;

        NSString* contextLine = nil;
        CFStringRef contextName = nullptr;
        UInt32 contextSize = (UInt32)sizeof(contextName);
        if (AudioUnitGetProperty(inAudioUnit,
                                 kAudioUnitProperty_ContextName,
                                 kAudioUnitScope_Global,
                                 0,
                                 &contextName,
                                 &contextSize) == noErr && contextName) {
            contextLine = [(__bridge NSString*)contextName copy];
            CFRelease(contextName);
        }

        __block NSView* editorView = nil;
        void (^buildBlock)(void) = ^{
            @autoreleasepool {
                ArpSIDAudioUnit* strongAudioUnit_v795 = weakAudioUnit_v795;
                if (!strongAudioUnit_v795) return;
                @try {
                    // objc_getAssociatedObject returns an object at +0. Under ARC a strong
                    // local makes reuse lifetime explicit until the method returns; the
                    // association is refreshed before returning the view so host teardown or
                    // view reparenting cannot leave us handing out a stale weak-looking raw
                    // pointer. All association work is performed on the main thread.
                    __strong id associatedView = objc_getAssociatedObject(strongAudioUnit_v795, &kArpSIDAUv2AudioUnitViewAssociationKey);
                    NSView* existingView = [associatedView isKindOfClass:[NSView class]] ? (NSView*)associatedView : nil;
                    if (associatedView && !existingView) {
                        objc_setAssociatedObject(strongAudioUnit_v795,
                                                 &kArpSIDAUv2AudioUnitViewAssociationKey,
                                                 nil,
                                                 OBJC_ASSOCIATION_ASSIGN);
                    }
                    if (existingView && existingView.superview == nil) {
                        ArpSIDViewController* existingController =
                            associatedViewControllerForAudioUnit(strongAudioUnit_v795);
                        if (existingController) {
                            @try {
                                [existingController reconnectExistingEditorViewToAudioUnit:strongAudioUnit_v795
                                                                               contextLine:contextLine];
                            } @catch (NSException* __unused ex) {}
                        }
                        objc_setAssociatedObject(strongAudioUnit_v795,
                                                 &kArpSIDAUv2AudioUnitViewAssociationKey,
                                                 existingView,
                                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                        editorView = existingView;
                        return;
                    }

                    // audit P0-10: clean detach contract for repeated open / attached-
                    // view replacement. We reach here either with no existing view or
                    // with an existing view that is STILL ATTACHED to a host hierarchy
                    // (the detached-reuse branch above already returned). Disposing the
                    // outgoing controller while its view is still attached would leave
                    // the host displaying a view backed by a torn-down controller. So
                    // first remove the outgoing view from its superview, then dispose
                    // its controller, then clear the stale associations BEFORE building
                    // the replacement (so a failure below cannot leave the AU pointing
                    // at a half-torn-down editor).
                    if (existingView && existingView.superview != nil) {
                        [existingView removeFromSuperview];
                    }
                    ArpSIDViewController* oldController = objc_getAssociatedObject(strongAudioUnit_v795, &kArpSIDAUv2AudioUnitControllerAssociationKey);
                    if (oldController && [oldController respondsToSelector:@selector(prepareForFinalEditorDisposal)]) {
                        @try { [oldController prepareForFinalEditorDisposal]; } @catch (NSException* __unused ex) {}
                    }
                    objc_setAssociatedObject(strongAudioUnit_v795,
                                             &kArpSIDAUv2AudioUnitViewAssociationKey,
                                             nil,
                                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                    objc_setAssociatedObject(strongAudioUnit_v795,
                                             &kArpSIDAUv2AudioUnitControllerAssociationKey,
                                             nil,
                                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);

                    ArpSIDViewController* controller = [[ArpSIDViewController alloc] init];
                    if (!controller) return;
                    NSView* view = controller.view;
                    if (!view) return;
                    [controller connectAudioUnit:strongAudioUnit_v795];
                    if (contextLine.length > 0) {
                        [controller updatePresentationContext:contextLine];
                    }
                    objc_setAssociatedObject(view,
                                             &kArpSIDAUv2ViewControllerAssociationKey,
                                             controller,
                                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                    objc_setAssociatedObject(strongAudioUnit_v795,
                                             &kArpSIDAUv2AudioUnitControllerAssociationKey,
                                             controller,
                                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                    objc_setAssociatedObject(strongAudioUnit_v795,
                                             &kArpSIDAUv2AudioUnitViewAssociationKey,
                                             view,
                                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                    editorView = view;
                } @catch (NSException* ex) {
                    NSLog(@"[ArpSIDAUv2ViewFactory] EXCEPTION creating view: %@ — %@", ex.name, ex.reason);
                }
            }
        };

        if ([NSThread isMainThread]) {
            buildBlock();
        } else {
            // AUv2 hosts are not fully consistent about Cocoa UI thread entry.
            // Avoid an unbounded dispatch_sync deadlock if the main thread is
            // blocked by the host while requesting the view off-main.
            //
            // audit P0-9: on timeout we return nil, but the queued main-thread build
            // may still run later and would leave an ORPHAN editor (a controller/view
            // connected to the AU and retained via associations) the host never got.
            // stateLock makes the timeout decision and the build commit mutually
            // exclusive so exactly one side disposes the abandoned editor — never an
            // orphan, and never a dropped view that was actually built in time.
            dispatch_semaphore_t done = dispatch_semaphore_create(0);
            NSObject* stateLock = [[NSObject alloc] init];
            __block BOOL callerAbandoned = NO;
            __block BOOL builtOnMain = NO;
            dispatch_async(dispatch_get_main_queue(), ^{
                buildBlock();
                BOOL abandoned;
                @synchronized (stateLock) {
                    abandoned = callerAbandoned;
                    builtOnMain = YES;
                }
                if (abandoned) {
                    // Caller already timed out and returned nil — discard the editor
                    // we just built instead of leaving it connected to the AU.
                    ArpSIDAudioUnit* strongAudioUnit_v795 = weakAudioUnit_v795;
                    if (strongAudioUnit_v795) disposeAbandonedAUv2Editor(strongAudioUnit_v795);
                    editorView = nil;
                }
                dispatch_semaphore_signal(done);
            });
            const int64_t timeoutNs = 2LL * NSEC_PER_SEC;
            if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, timeoutNs)) != 0) {
                BOOL alreadyBuilt;
                @synchronized (stateLock) {
                    callerAbandoned = YES;
                    alreadyBuilt = builtOnMain;
                }
                if (alreadyBuilt) {
                    // The build finished in the race window just before we observed
                    // the timeout; the block did not see the abandonment, so dispose
                    // the orphan ourselves — on the main thread (AppKit teardown).
                    dispatch_async(dispatch_get_main_queue(), ^{
                        ArpSIDAudioUnit* strongAudioUnit_v795 = weakAudioUnit_v795;
                        if (strongAudioUnit_v795) disposeAbandonedAUv2Editor(strongAudioUnit_v795);
                    });
                }
                NSLog(@"[ArpSIDAUv2ViewFactory] Timed out creating AUv2 Cocoa view on main thread");
                return nil;
            }
        }
        return editorView;
    }
}
@end

extern "C" AudioComponentPlugInInterface* ArpSIDAUv2Factory(const AudioComponentDescription* inDesc) {
    if (!inDesc) return nullptr;
    if (!ArpSIDAUv2::isSupportedComponentDescription(*inDesc)) {
        return nullptr;
    }

    auto* wrapper = new (std::nothrow) ArpSIDAUv2Wrapper();
    if (!wrapper) return nullptr;
    auto* impl = new (std::nothrow) ArpSIDAUv2Instance();
    if (!impl) {
        delete wrapper;
        return nullptr;
    }

    impl->componentDescription = *inDesc;
    cachePresentPresetState(impl, auv2StartupFactorySlotForFlavor(componentFlavorForInstance(impl)), nil, false);
    wrapper->plugInInterface.Open = componentOpen;
    wrapper->plugInInterface.Close = componentClose;
    wrapper->plugInInterface.Lookup = componentLookup;
    wrapper->plugInInterface.reserved = nullptr;
    wrapper->impl = impl;

    NSError* error = nil;
    @autoreleasepool {
        impl->audioUnit = [[ArpSIDAudioUnit alloc] initWithComponentDescription:*inDesc options:0 error:&error];
        if (!impl->audioUnit || error) {
            delete impl;
            delete wrapper;
            return nullptr;
        }
        impl->adapter = (ArpSIDDSPKernelAdapter*)[impl->audioUnit debugAdapter];
        // adapter is required before Open/Initialize can publish render state.
        if (!impl->adapter) {
            impl->audioUnit = nil;
            delete impl;
            delete wrapper;
            return nullptr;
        }
    }
    // GUI 001-flicker fix (AUv2 mirror): seed the pinned bank/program caches from
    // the audioUnit's resolved current preset at creation, instead of leaving them
    // at the hardcoded slot-0 default (which made the patch readout flash "001"
    // until the first restore/selection populated the real slot). A later
    // setFullState restore still overrides this. Mirrors the AUv3 pin -1 sentinel.
    @autoreleasepool {
        AUAudioUnitPreset* seedPreset = impl->audioUnit.currentPreset;
        if (seedPreset) {
            cachePresentPresetState(impl, seedPreset.number, seedPreset.name, /*explicitSelection=*/false);
        }
    }
    publishKernelPointer(impl, nullptr);
    publishAdapterPointer(impl, nil);  // v549
    publishRenderBlock(impl, nil);

    return &wrapper->plugInInterface;
}
