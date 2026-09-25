// ArpSIDAudioUnit.mm
// ArpSID AUv3 — Full AUAudioUnit Implementation (v1.5.1)
//
// AUv3 compliance matrix (Apple TN3102 / Logic Pro 10.8+):
//
// ✓ AUAudioUnitV3 internalRenderBlock (no-ObjC, no-malloc, no-lock)
// ✓ AURenderEventMIDI + AURenderEventMIDIEventList (UMP / Logic 10.8+)
// ✓ Parameter ramps — handled inside kernel processBlock (no ObjC on render path)
// ✓ musicalContextBlock — captured outside render; render path uses sanitized transport snapshot
// ✓ transportStateBlock — never called from render path
// ✓ AUParameterTree (kNumParams parameters, grouped, automatable subset)
// ✓ Full state save/restore (fullState / fullStateForDocument)
// ✓ Preset save/restore (factory + user, negative user preset numbers)
// ✓ Stereo output bus, no input bus, channelCapabilities @[@0, @2]
// ✓ shouldChangeToFormat: — float32 non-interleaved mono/stereo
// ✓ latency: constant 5 ms wall-clock (not sample-rate-dependent)
// ✓ tailTime: 8 s
// ✓ supportsMPE: NO | virtualMIDICableCount: 1
// ✓ MIDIOutputEventListBlock: nil (instrument — no MIDI output)
// supportsUserPresets: NO (canonical fullState only)
// ✓ Transient params excluded from state blobs
// ✓ Non-nil originator sentinel breaks observer loop during preset load
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#import "ArpSIDAudioUnit.h"
#include <array>
#include "arpsid/core/sid_runtime_forensic_config.h"
#include "arpsid/core/sid_parameter_presentation.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "ArpSIDAUEventTranslator.h"
#include "ArpSIDStateSerializer.h"
#include "ArpSIDComponentFlavor.h"
#import "ArpSIDDSPKernelAdapter.h"
#import "ArpSIDAUParameters.h"
#import "ArpSIDViewController.h"

// Full kernel definition required here — internalRenderBlock calls kernel methods
// directly on the render thread (no ObjC message sends on the hot path).
#include "ArpSIDDSPKernel.hpp"
#include "parameter_ids.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid/engines/sid_register_engine.h"

#include <AudioToolbox/AudioToolbox.h>
#include <AVFoundation/AVFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>
#include <cstring>

#ifndef ARPSID_UI_TRACE
#define ARPSID_UI_TRACE 0
#endif
#if ARPSID_UI_TRACE
#define ARPSID_UI_LOG(...) NSLog(__VA_ARGS__)
#else
#define ARPSID_UI_LOG(...) do {} while (0)
#endif

// ─── Constants ───────────────────────────────────────────────────────────────

static NSString * const kArpSIDStateKey_RootBlob = @"ArpSIDStateRoot";
static NSString * const kArpSIDStateKey_Version = @"ArpSIDStateVersion";
static NSString * const kArpSIDStateKey_HostTempoHint = @"ArpSIDHostTempoHint";
static NSString * const kArpSIDStateKey_HostBeatHint = @"ArpSIDHostBeatHint";
static NSString * const kArpSIDStateKey_HostPlayingHint = @"ArpSIDHostPlayingHint";
static NSString * const kArpSIDStateKey_HostLoopingHint = @"ArpSIDHostLoopingHint";
static NSString * const kArpSIDStateKey_HostLoopStartHint = @"ArpSIDHostLoopStartHint";
static NSString * const kArpSIDStateKey_HostLoopEndHint = @"ArpSIDHostLoopEndHint";

static inline void ArpSIDZeroRenderedAuv3Buffers(AudioBufferList* outputData,
                                                 AVAudioFrameCount frameCount,
                                                 UInt32 renderedChannels) noexcept {
    if (!outputData) return;
    const size_t frames = static_cast<size_t>(frameCount);
    if (outputData->mNumberBuffers == 1) {
        AudioBuffer& b = outputData->mBuffers[0];
        if (!b.mData) return;
        const UInt32 ch = std::min<UInt32>(std::max<UInt32>(1u, b.mNumberChannels),
                                           std::max<UInt32>(1u, renderedChannels));
        const size_t bytes = std::min<size_t>(static_cast<size_t>(b.mDataByteSize),
                                              frames * sizeof(float) * static_cast<size_t>(ch));
        std::memset(b.mData, 0, bytes);
        return;
    }
    const UInt32 n = std::min<UInt32>(outputData->mNumberBuffers, std::max<UInt32>(1u, renderedChannels));
    for (UInt32 i = 0; i < n; ++i) {
        AudioBuffer& b = outputData->mBuffers[i];
        if (!b.mData) continue;
        const size_t bytes = std::min<size_t>(static_cast<size_t>(b.mDataByteSize), frames * sizeof(float));
        std::memset(b.mData, 0, bytes);
    }
}

static inline bool ArpSIDReadHostTransportSnapshotCoherent(const std::atomic<uint64_t>* generation,
                                                           const std::atomic<double>* bpm,
                                                           const std::atomic<double>* beat,
                                                           const std::atomic<double>* sampleRate,
                                                           const std::atomic<double>* loopStart,
                                                           const std::atomic<double>* loopEnd,
                                                           const std::atomic<int>* frameCount,
                                                           const std::atomic<uint32_t>* flags,
                                                           ArpSID::TransportState& out) noexcept {
    if (!generation || !bpm || !beat || !sampleRate || !loopStart || !loopEnd || !frameCount || !flags) return false;
    for (int retry = 0; retry < 8; ++retry) {
        const uint64_t before = generation->load(std::memory_order_acquire);
        if (before == 0u || (before & 1u) != 0u) continue;
        ArpSID::TransportState candidate{};
        candidate.bpm = bpm->load(std::memory_order_relaxed);
        candidate.beatPosition = beat->load(std::memory_order_relaxed);
        candidate.sampleRate = sampleRate->load(std::memory_order_relaxed);
        candidate.frameCount = frameCount->load(std::memory_order_relaxed);
        const uint32_t f = flags->load(std::memory_order_relaxed);
        candidate.playStateKnown = (f & 1u) != 0u;
        candidate.isPlaying = (f & 2u) != 0u;
        candidate.isLooping = (f & 4u) != 0u;
        candidate.loopStart = loopStart->load(std::memory_order_relaxed);
        candidate.loopEnd = loopEnd->load(std::memory_order_relaxed);
        // Acquire fence guarantees the relaxed loads above are not reordered
        // past the trailing generation acquire-load, closing the textbook
        // seqlock data-read race on weakly-ordered targets.
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint64_t after = generation->load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            out = candidate;
            return true;
        }
    }
    return false;
}

static NSString * const kArpSIDStateKey_HostTimeSigNumHint = @"ArpSIDHostTimeSigNumHint";
static NSString * const kArpSIDStateKey_HostTimeSigDenHint = @"ArpSIDHostTimeSigDenHint";
static NSString * const kArpSIDStateKey_HostMeasureDownbeatHint = @"ArpSIDHostMeasureDownbeatHint";
static NSString * const kArpSIDStateKey_SeqTempoNorm = @"ArpSIDSeqTempoNorm";
static NSString * const kArpSIDStateKey_ArpTransposeNorm = @"ArpSIDArpTransposeNorm";
static NSString * const kArpSIDStateKey_ProgramNorm = @"ArpSIDProgramNorm";
static NSString * const kArpSIDStateKey_BankSlotNorm = @"ArpSIDBankSlotNorm";
static NSString * const kArpSIDStateKey_ArpEnableNorm = @"ArpSIDArpEnableNorm";
static NSString * const kArpSIDStateKey_ArpModeNorm = @"ArpSIDArpModeNorm";
static NSString * const kArpSIDStateKey_ArpRateNorm = @"ArpSIDArpRateNorm";
static NSString * const kArpSIDStateKey_ArpOctavesNorm = @"ArpSIDArpOctavesNorm";
static NSString * const kArpSIDStateKey_ArpSwingNorm = @"ArpSIDArpSwingNorm";
static NSString * const kArpSIDStateKey_ArpGateNorm = @"ArpSIDArpGateNorm";
static NSString * const kArpSIDStateKey_ArpHoldNorm = @"ArpSIDArpHoldNorm";
static NSString * const kArpSIDStateKey_ArpLatchNorm = @"ArpSIDArpLatchNorm";
static NSString * const kArpSIDStateKey_ArpRandomNorm = @"ArpSIDArpRandomNorm";
static NSString * const kArpSIDStateKey_ArpPatternLengthNorm = @"ArpSIDArpPatternLengthNorm";
static NSString * const kArpSIDStateKey_SeqEnableNorm = @"ArpSIDSeqEnableNorm";
static NSString * const kArpSIDStateKey_SeqSwingNorm = @"ArpSIDSeqSwingNorm";
static NSString * const kArpSIDStateKey_SeqModeNorm = @"ArpSIDSeqModeNorm";
static NSString * const kArpSIDStateKey_SeqLengthNorm = @"ArpSIDSeqLengthNorm";
static NSString * const kArpSIDStateKey_SidChipRevisionNorm = @"ArpSIDChipRevisionNorm";
static NSString * const kArpSIDStateKey_SidChipSelectionIndex = @"ArpSIDSidChipSelectionIndex"; // explicit persisted GUI selector index 0..3
static NSString * const kArpSIDStateKey_SidChipFamilyNorm = @"ArpSIDSidChipFamilyNorm";       // coherent kParamSidModel mirror
static NSString * const kArpSIDStateKey_SidExternalRcEnableNorm = @"ArpSIDExternalRcEnableNorm";
static NSString * const kArpSIDStateKey_SidOversamplingFactorNorm = @"ArpSIDOversamplingFactorNorm";
static NSString * const kArpSIDStateKey_PureSid1Q1OutputMode = @"ArpSIDPureSid1Q1OutputMode"; // bypass AU post-FX/HiFi and emit SID engine output directly
static const UInt32     kArpSIDStateVersion      = 8; // v8 persists explicit SID chip selector index + family mirror

static ArpSID::ComponentFlavor ArpSIDResolveComponentFlavor(const AudioComponentDescription& desc) noexcept {
    return ArpSID::componentFlavorFromDescription(desc);
}

static inline void ArpSIDApplyComponentFlavorPolicyToStateRoot(ArpSID::ComponentFlavor flavor,
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

static inline bool ArpSIDShouldRebuildStateRootForFlavor(ArpSID::ComponentFlavor flavor,
                                                         NSInteger requestedSlot,
                                                         NSInteger resolvedSlot) noexcept {
    if (!ArpSID::componentFlavorIsDedicatedDrum(flavor)) return false;
    if (resolvedSlot < 0) return true;
    if (requestedSlot < 0) return true;
    return requestedSlot != resolvedSlot;
}

static NSString* ArpSIDFactoryPresetTitleForFlavor(ArpSID::ComponentFlavor flavor,
                                                   NSInteger slot,
                                                   NSString* baseName) {
    const NSInteger clampedSlot = std::clamp(slot, (NSInteger)0, (NSInteger)ArpSID::kCanonicalFactoryPatchSlotMax);
    NSString* resolvedName = baseName.length > 0 ? baseName : @"Factory Patch";
    if (flavor == ArpSID::ComponentFlavor::Sid808) {
        return [NSString stringWithFormat:@"808 K%03ld · %@", (long)(clampedSlot + 1), resolvedName];
    }
    if (ArpSID::componentFlavorIsDedicatedDrum(flavor)) {
        return [NSString stringWithFormat:@"K%03ld · %@", (long)(clampedSlot + 1), resolvedName];
    }
    return [NSString stringWithFormat:@"P%03ld · %@", (long)(clampedSlot + 1), resolvedName];
}

static bool ArpSIDFactoryDefinitionIsDrumAuthored(const ArpSID::PatchDefinition& def) noexcept;

static NSInteger ArpSIDStartupFactorySlotForFlavor(ArpSID::ComponentFlavor flavor) noexcept {
    if (flavor == ArpSID::ComponentFlavor::C64SidPlayer) return 0;
    if (flavor == ArpSID::ComponentFlavor::Sid808) {
        const auto& defs = ArpSID::getFactoryPatchDefinitions();
        if (defs.size() > 120u && ArpSIDFactoryDefinitionIsDrumAuthored(defs[120])) return 120;
    }
    if (!ArpSID::componentFlavorIsDedicatedDrum(flavor)) return 0;
    const auto& defs = ArpSID::getFactoryPatchDefinitions();
    for (size_t i = 0; i < defs.size(); ++i) {
        if (defs[i].usage.role == ArpSID::PatchRole::Drum ||
            defs[i].usage.family == ArpSID::HistoricalFamilyId::DrumKit) {
            return (NSInteger)i;
        }
    }
    return 0;
}

static bool ArpSIDFactoryDefinitionIsDrumAuthored(const ArpSID::PatchDefinition& def) noexcept {
    return def.usage.role == ArpSID::PatchRole::Drum ||
           def.usage.family == ArpSID::HistoricalFamilyId::DrumKit;
}

static bool ArpSIDComponentFlavorAllowsFactorySlot(ArpSID::ComponentFlavor flavor, NSInteger slot) noexcept {
    const auto& defs = ArpSID::getFactoryPatchDefinitions();
    if (defs.empty()) return slot == 0;
    const NSInteger normalized = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)slot);
    if ((size_t)normalized >= defs.size()) return false;
    const bool isDrum = ArpSIDFactoryDefinitionIsDrumAuthored(defs[(size_t)normalized]);
    switch (flavor) {
        case ArpSID::ComponentFlavor::Hybrid:
            return true;
        case ArpSID::ComponentFlavor::Instrument:
            return !isDrum;
        case ArpSID::ComponentFlavor::C64SidPlayer:
            return normalized == 0;
        case ArpSID::ComponentFlavor::Sid808:
            return ArpSID::isSid808FactorySlot((int)normalized);
        case ArpSID::ComponentFlavor::DrumMachine:
            return isDrum;
        default:
            return true;
    }
}

static NSInteger ArpSIDResolveFactorySlotForFlavor(ArpSID::ComponentFlavor flavor, NSInteger requestedSlot) noexcept {
    const NSInteger normalized = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)requestedSlot);
    if (ArpSIDComponentFlavorAllowsFactorySlot(flavor, normalized)) return normalized;
    return ArpSIDStartupFactorySlotForFlavor(flavor);
}

static inline void ArpSIDAppendOrderedFactorySlotIfAllowed(std::vector<NSInteger>& orderedSlots,
                                                           std::vector<bool>& seenSlots,
                                                           ArpSID::ComponentFlavor flavor,
                                                           NSInteger slot) noexcept {
    const NSInteger normalized = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)slot);
    if (normalized < 0 || normalized >= (NSInteger)seenSlots.size()) return;
    if (!ArpSIDComponentFlavorAllowsFactorySlot(flavor, normalized)) return;
    if (seenSlots[(size_t)normalized]) return;
    orderedSlots.push_back(normalized);
    seenSlots[(size_t)normalized] = true;
}

static std::vector<NSInteger> ArpSIDFactorySlotOrderForFlavor(ArpSID::ComponentFlavor flavor) {
    const auto& defs = ArpSID::getFactoryPatchDefinitions();
    std::vector<NSInteger> orderedSlots;
    orderedSlots.reserve(defs.size());
    std::vector<bool> seenSlots(defs.size(), false);
    if (flavor == ArpSID::ComponentFlavor::C64SidPlayer) {
        ArpSIDAppendOrderedFactorySlotIfAllowed(orderedSlots, seenSlots, flavor, 0);
        return orderedSlots;
    }
    if (flavor == ArpSID::ComponentFlavor::Sid808) {
        for (NSInteger slot = 120; slot <= 149; ++slot) {
            ArpSIDAppendOrderedFactorySlotIfAllowed(orderedSlots, seenSlots, flavor, slot);
        }
        static constexpr NSInteger kSid808LegacyCompatibilitySlots[] = {
            118, 119, 116, 117, 127, 47
        };
        for (NSInteger slot : kSid808LegacyCompatibilitySlots) {
            ArpSIDAppendOrderedFactorySlotIfAllowed(orderedSlots, seenSlots, flavor, slot);
        }
    }
    for (NSInteger slot = 0; slot < (NSInteger)defs.size(); ++slot) {
        ArpSIDAppendOrderedFactorySlotIfAllowed(orderedSlots, seenSlots, flavor, slot);
    }
    return orderedSlots;
}

static constexpr AUAudioFrameCount kArpSIDDefaultMaxFrames = 8192;
static constexpr AUAudioFrameCount kArpSIDMinMaxFrames = 64;
static constexpr AUAudioFrameCount kArpSIDHardMaxFrames = 65536;
static constexpr AUAudioFrameCount kArpSIDScratchHeadroomFrames = 2048;

static constexpr AUAudioFrameCount ArpSIDRoundUpFrameQuantum(AUAudioFrameCount frames) noexcept {
    AUAudioFrameCount v = std::max<AUAudioFrameCount>(kArpSIDMinMaxFrames, frames);
    v -= 1;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

static constexpr AUAudioFrameCount ArpSIDSanitizeHostMaxFrames(AUAudioFrameCount frames) noexcept {
    const AUAudioFrameCount clamped = std::min<AUAudioFrameCount>(std::max<AUAudioFrameCount>(kArpSIDMinMaxFrames, frames), kArpSIDHardMaxFrames);
    return std::min<AUAudioFrameCount>(ArpSIDRoundUpFrameQuantum(clamped), kArpSIDHardMaxFrames);
}

static inline AVAudioChannelCount ArpSIDSanitizeOutputChannels(AVAudioChannelCount ch) noexcept {
    return (ch == 1 || ch == 2) ? ch : 2;
}

static constexpr size_t ArpSIDScratchReserveFramesForMaxFrames(AUAudioFrameCount frames) noexcept {
    const AUAudioFrameCount safe = ArpSIDSanitizeHostMaxFrames(frames > 0 ? frames : kArpSIDDefaultMaxFrames);
    const AUAudioFrameCount baseline = std::max<AUAudioFrameCount>(safe * 2u + (kArpSIDScratchHeadroomFrames / 2u),
                                                                   safe + kArpSIDScratchHeadroomFrames * 2u);
    return (size_t)ArpSIDRoundUpFrameQuantum(baseline);
}

static constexpr size_t ArpSIDScratchStableRenderFrames() noexcept {
    return ArpSIDScratchReserveFramesForMaxFrames(kArpSIDHardMaxFrames);
}

// audit #6 (AUv3 scratch pointer stability): the planar render pointers
// (_renderPlanarL/R) captured by the internalRenderBlock are only safe to use
// without re-validation because the scratch is reserved ONCE at the hard frame
// ceiling during init and never grown while a render block is live. That whole
// argument collapses if the once-reserved size could ever be smaller than a
// frame count a host may legitimately request. Pin the invariant at COMPILE time
// so the discipline cannot silently regress: the stable reserve must cover the
// largest sanitized render request (kArpSIDHardMaxFrames). The per-render
// `planarCap >= frameCount` check then only ever fails if a host violates the
// hard max, in which case render fails closed (silent) rather than out-of-bounds.
static_assert(ArpSIDScratchStableRenderFrames() >= (size_t)kArpSIDHardMaxFrames,
              "AUv3 stable scratch reserve must cover the hard max render frame count "
              "(pointer-stability invariant for the captured planar render pointers)");
static_assert(ArpSIDScratchReserveFramesForMaxFrames(kArpSIDHardMaxFrames)
                  >= ArpSIDScratchReserveFramesForMaxFrames(kArpSIDDefaultMaxFrames),
              "stable (hard-max) reserve must dominate any per-host reserve");

static inline void ArpSIDEnsureFloatScratchCapacity(std::vector<float>& scratch, size_t needed) {
    const size_t target = (size_t)ArpSIDRoundUpFrameQuantum((AUAudioFrameCount)std::max<size_t>(needed, (size_t)kArpSIDMinMaxFrames));
    if (scratch.size() >= target) return;
    const size_t oldSize = scratch.size();
    scratch.resize(target);
    std::fill(scratch.begin() + static_cast<ptrdiff_t>(oldSize), scratch.end(), 0.0f);
}

static inline bool auv3ParamIsAutomatable(int pid) {
    if (pid < 0 || pid >= ArpSID::kNumParams) return false;
    if (ArpSID::isHostMidiBridgeParam(pid))   return false;
    // Program/BankSlot are AU preset identity mirrors, not automatable DSP
    // controls. If they are exposed as writable AUParameterTree nodes, Logic can
    // replay stale project/GM metadata at Stop->Play and repaint/reassert an old
    // patch despite PresentPreset/currentPreset guards. They remain readable via
    // getParameterValue()/AUv2 GetParameter, but are not host-writable params.
    if (pid == ArpSID::kParamProgram || pid == ArpSID::kParamBankSlot) return false;
    return ArpSID::kParamInfos[(size_t)pid].automatable;
}

[[maybe_unused]] static inline bool auv3ParamIsPersistent(int pid) {
    return ArpSID::isPatchPersistentParam(pid);
}

struct ArpSIDAuxParamBinding {
    NSString* const* key;
    int paramID;
};

static const ArpSIDAuxParamBinding kArpSIDAuxParamBindings[] = {
    {&kArpSIDStateKey_SeqTempoNorm, ArpSID::kParamSeqTempo},
    {&kArpSIDStateKey_ArpTransposeNorm, ArpSID::kParamArpTranspose},
    {&kArpSIDStateKey_ProgramNorm, ArpSID::kParamProgram},
    {&kArpSIDStateKey_BankSlotNorm, ArpSID::kParamBankSlot},
    {&kArpSIDStateKey_ArpEnableNorm, ArpSID::kParamArpEnable},
    {&kArpSIDStateKey_ArpModeNorm, ArpSID::kParamArpMode},
    {&kArpSIDStateKey_ArpRateNorm, ArpSID::kParamArpRate},
    {&kArpSIDStateKey_ArpOctavesNorm, ArpSID::kParamArpOctaves},
    {&kArpSIDStateKey_ArpSwingNorm, ArpSID::kParamArpSwing},
    {&kArpSIDStateKey_ArpGateNorm, ArpSID::kParamArpGate},
    {&kArpSIDStateKey_ArpHoldNorm, ArpSID::kParamArpHold},
    {&kArpSIDStateKey_ArpLatchNorm, ArpSID::kParamArpLatch},
    {&kArpSIDStateKey_ArpRandomNorm, ArpSID::kParamArpRandom},
    {&kArpSIDStateKey_ArpPatternLengthNorm, ArpSID::kParamArpPatternLength},
    {&kArpSIDStateKey_SeqEnableNorm, ArpSID::kParamSeqEnable},
    {&kArpSIDStateKey_SeqSwingNorm, ArpSID::kParamSeqSwing},
    {&kArpSIDStateKey_SeqModeNorm, ArpSID::kParamSeqMode},
    {&kArpSIDStateKey_SeqLengthNorm, ArpSID::kParamSeqLength},
    {&kArpSIDStateKey_SidChipRevisionNorm, ArpSID::kParamSidChipRevision},
    {&kArpSIDStateKey_SidExternalRcEnableNorm, ArpSID::kParamSidExternalRcEnable},
    {&kArpSIDStateKey_SidOversamplingFactorNorm, ArpSID::kParamSidOversamplingFactor},
};

// v966: AUv3 display text delegates to the shared parameter-ID-aware
// presentation authority (identical laws and labels as AUv2 and VST3). The
// old fallback here rendered the raw normalized value with a unit suffix
// (e.g. LFO rate "0.200 Hz" for an actual 0.29 Hz exponential-law rate).
static NSString* ArpSIDParameterDisplayString(int pid, float value) {
    char buf[64] = {};
    if (!ArpSID::SidParameterPresentation::formatNormalized(pid, value, buf, sizeof(buf)))
        return @"0.0000";
    NSString* s = [NSString stringWithUTF8String:buf];
    return s ? s : @"0.0000";
}


// ─── Implementation ──────────────────────────────────────────────────────────

static constexpr size_t kArpSIDStateBlobCap = (ArpSID::kStateBufferSize + 65536);

@interface ArpSIDAudioUnit () {
    ArpSIDDSPKernelAdapter*             _adapter;
    AUAudioUnitBusArray*                _inputBusArray;
    AUAudioUnitBusArray*                _outputBusArray;
    AUParameterTree*                    _parameterTree;
    AVAudioFormat*                      _outputFormat;
    AudioComponentDescription           _componentDescription;
    ArpSID::ComponentFlavor             _componentFlavor;
    NSInteger                           _selectedUserPresetNumber;
    BOOL                                _isSetup;
    std::array<uint8_t, kArpSIDStateBlobCap> _stateIoScratch;
    NSArray<AUAudioUnitPreset*>*        _factoryPresetCache;
    AUAudioUnitPreset*                  _currentPresetMetadata;
    NSInteger                           _currentFactoryPresetNumber;
    NSInteger                           _pinnedFactoryPresetNumber;
    NSInteger                           _pinnedBankSlotNumber;
    NSInteger                           _restoredStateGeneration;
    std::atomic<bool>                   _presetApplyInProgress;
    std::vector<float>                  _renderPlanarL;
    std::vector<float>                  _renderPlanarR;
    // Audit #51 — scratch-resize race guard. AUv3 planar scratch is reserved
    // at the hard frame ceiling during init and every lifecycle refresh, so
    // host max-frame changes do not normally reallocate under a returned
    // render block. The lock/epoch protocol remains as a fail-closed guard for
    // any future path that would mutate _renderPlanarL/_renderPlanarR.
    NSLock*                             _renderScratchMutex;
    std::atomic<uint64_t>               _renderScratchEpoch;
    // audit P1.9 — AU-owned stable render-kernel slot. The internalRenderBlock
    // used to snapshot a raw kernel pointer once at block-creation time, which is
    // a dangling pointer if the kernel/adapter is ever reset or replaced during a
    // lifecycle transition. This atomic is published by allocateRenderResources
    // and cleared by deallocateRenderResources/dealloc; the render block loads it
    // every invocation and fails closed (silent) when it is null. Its address is
    // an AU ivar, so it is valid for exactly the AU's lifetime (the same lifetime
    // the AUAudioUnit render-block contract already guarantees).
    std::atomic<ArpSID::ArpSIDDSPKernel*> _renderKernelPtr;
    // audit P2.14 — render-format channel snapshot. The internalRenderBlock used
    // to capture _outputFormat.channelCount ONCE at block-creation time; if the
    // host changes/reallocates the format without reacquiring the block, that
    // capture is stale. allocateRenderResources publishes the negotiated channel
    // count here and the render block loads it every invocation.
    // ObjC ivars cannot use a C++ brace-initializer; zero-filled by the runtime
    // and explicitly published by allocateRenderResources before any render call.
    std::atomic<uint32_t>                 _renderOutputChannels;
    // Audit #52 — AUv3 scratch under-capacity counter (mirror of the AUv2
    // counter from skive 5). Incremented when render observes the scratch
    // can't hold the requested frame count.
    std::atomic<uint64_t>               _scratchUnderCapacityCount;
    // audit #15 — defensive render reentrancy guard. The AUAudioUnit contract
    // guarantees internalRenderBlock is never reentered, but a misbehaving host
    // could violate it. The render block CAS-claims this flag at entry and
    // releases it (RAII) at every exit; a failed claim renders silence instead
    // of letting two invocations stomp the shared scratch/event buffers.
    std::atomic<bool>                   _renderReentryGuard;
    // audit #16 — count parameter writes dropped because the adapter was nil
    // (teardown window). Diagnostics only; surfaced for host tooling parity with
    // the other dropped-write counters.
    std::atomic<uint64_t>               _droppedParameterWrites;
    ArpSID::EventBuffer                 _renderEventScratch;
    ArpSID::EventBuffer                 _renderChunkEventScratch;
    std::atomic<uint64_t>                _hostTransportSnapshotGeneration;
    std::atomic<double>                  _hostTransportSnapshotBpm;
    std::atomic<double>                  _hostTransportSnapshotBeat;
    std::atomic<double>                  _hostTransportSnapshotSampleRate;
    std::atomic<double>                  _hostTransportSnapshotLoopStart;
    std::atomic<double>                  _hostTransportSnapshotLoopEnd;
    std::atomic<int>                     _hostTransportSnapshotFrameCount;
    std::atomic<uint32_t>                _hostTransportSnapshotFlags;
}
- (BOOL)_setCurrentFactoryPresetMetadataOnlyForSlot:(NSInteger)slot
                                 assumeGuardActive:(BOOL)assumeGuardActive
                              updateAdapterMirrors:(BOOL)updateAdapterMirrors
                                 notifySuperPreset:(BOOL)notifySuperPreset;
- (BOOL)_setCurrentFactoryPresetMetadataOnlyForSlot:(NSInteger)slot assumeGuardActive:(BOOL)assumeGuardActive;
- (void)_setCurrentFactoryPresetMetadataOnlyForSlot:(NSInteger)slot;
- (void)_restorePinnedPresetMetadataFromDocumentDictionary:(NSDictionary<NSString*,id>*)state updateAdapterMirrors:(BOOL)updateMirrors;
- (float)_pinnedBankSlotNormalizedValue;
- (float)_pinnedProgramNormalizedValue;
- (NSInteger)_serializationPresetSlot;
- (BOOL)setRestoredFactoryPresetMetadataOnlyForSlot:(NSInteger)slot;
- (NSInteger)stickyFactoryPresetNumber;
- (void)_publishHostTransportSnapshotNonRealtimeWithFrameCount:(AUAudioFrameCount)frames;
- (BOOL)hasStickyUserFactoryPresetSelection;
@end

@implementation ArpSIDAudioUnit


// ─── Init ─────────────────────────────────────────────────────────────────────

- (instancetype)initWithComponentDescription:(AudioComponentDescription)componentDescription
                                      options:(AudioComponentInstantiationOptions)options
                                        error:(NSError* __autoreleasing*)outError {
    self = [super initWithComponentDescription:componentDescription options:options error:outError];
    if (!self) return nil;

    _componentDescription       = componentDescription;
    _componentFlavor            = ArpSIDResolveComponentFlavor(componentDescription);
    // Audit #51 — initialize the scratch-resize lock and epoch BEFORE any
    // setMaximumFramesToRender / scratch reservation can be triggered.
    _renderScratchMutex         = [[NSLock alloc] init];
    _renderScratchEpoch.store(0u, std::memory_order_release);
    _scratchUnderCapacityCount.store(0u, std::memory_order_release);
    // audit #15/#16 — initialize defensive render-reentry guard and dropped-write
    // counter before any render block can be handed out or any parameter observed.
    _renderReentryGuard.store(false, std::memory_order_release);
    _droppedParameterWrites.store(0u, std::memory_order_release);
    ArpSIDEnsureFloatScratchCapacity(_renderPlanarL, ArpSIDScratchStableRenderFrames());
    ArpSIDEnsureFloatScratchCapacity(_renderPlanarR, ArpSIDScratchStableRenderFrames());
    _adapter                  = [[ArpSIDDSPKernelAdapter alloc] init];
    [_adapter setComponentFlavor:(NSInteger)_componentFlavor];
    // audit P0.1 (corrected): the kernel is owned by the adapter for the AU's whole
    // life — it is NOT freed by deallocateRenderResources. So _renderKernelPtr must
    // track the KERNEL's lifetime: publish it as soon as the kernel exists and clear
    // it only at dealloc. (It is re-published in allocateRenderResources too.) This
    // keeps the stale-call safety while never going silent merely because render
    // resources were deallocated/reallocated.
    _renderKernelPtr.store([_adapter kernelPtr], std::memory_order_release);
    _selectedUserPresetNumber = -1;
    _currentFactoryPresetNumber = 0;
    // GUI 001-flicker fix: the pinned bank/factory slots are the highest-priority
    // source in _resolvedCurrentFactoryPresetSlot. Initialising them to 0 made the
    // patch display assert slot 0 ("001") on every refresh BEFORE the host's state
    // restore (or an explicit selection) had populated the real slot — producing
    // the visible 001<->correct-patch flicker. Initialise to the -1 "unknown"
    // sentinel instead, so until a real value is pinned the resolver falls through
    // to the kernel state-root (the actual loaded patch) rather than forcing 001.
    // This is NOT the strict flavor-filtering that previously caused the patch-reset
    // loop; it only changes which source wins while the pin is still unknown.
    _pinnedFactoryPresetNumber = -1;
    _pinnedBankSlotNumber = -1;
    _restoredStateGeneration = 0;
    _currentPresetMetadata = nil;
    _presetApplyInProgress.store(false, std::memory_order_release);
    _hostTransportSnapshotGeneration.store(0, std::memory_order_relaxed);
    _hostTransportSnapshotBpm.store(120.0, std::memory_order_relaxed);
    _hostTransportSnapshotBeat.store(0.0, std::memory_order_relaxed);
    _hostTransportSnapshotSampleRate.store(kArpSIDDefaultSampleRate, std::memory_order_relaxed);
    _hostTransportSnapshotLoopStart.store(0.0, std::memory_order_relaxed);
    _hostTransportSnapshotLoopEnd.store(0.0, std::memory_order_relaxed);
    _hostTransportSnapshotFrameCount.store((int)kArpSIDDefaultMaxFrames, std::memory_order_relaxed);
    _hostTransportSnapshotFlags.store(0u, std::memory_order_relaxed);
    _factoryPresetCache = nil;
    _isSetup                  = NO;

    [self _setupBusses];
    [self _buildParameterTree];
    [self _connectParameterTreeCallbacks];

    // AUv2 validation paths repeatedly cold-open and destroy the component
    // without ever asking for a custom view. Keep init strictly headless and
    // avoid any editor/controller dependency on the classic component path.
    // Logic and other AU hosts may negotiate larger realtime blocks than 512.
    // Use a safer default to avoid undersized first-pass setup.
    self.maximumFramesToRender = kArpSIDDefaultMaxFrames;

    // Force a deterministic startup preset per advertised AU flavor.
    // Hybrid/instrument variants start at slot 0, while the dedicated drum
    // variant boots on the first authored drum-kit slot.
    const NSInteger startupSlot = ArpSIDStartupFactorySlotForFlavor(_componentFlavor);
    [self _setCurrentFactoryPresetMetadataOnlyForSlot:startupSlot];
    {
        ArpSID::SidStateRootV1 startupRoot = ArpSID::makeFactoryPatchStateRootForSlot((int)startupSlot);
        ArpSIDApplyComponentFlavorPolicyToStateRoot(_componentFlavor, startupRoot);
        [self _syncParameterTreeFromStateRoot:startupRoot];
        if (ArpSID::ArpSIDDSPKernel* k = [_adapter kernelPtr]) {
            k->schedulePendingStateRestore(startupRoot);
        }
    }
    return self;
}

- (NSInteger)componentFlavor {
    return (NSInteger)_componentFlavor;
}


- (void)dealloc {
    // audit P1.9: ensure the render-kernel slot is null before the adapter (and
    // therefore the kernel) is torn down.
    _renderKernelPtr.store(nullptr, std::memory_order_release);
    if (self.renderResourcesAllocated) {
        [self deallocateRenderResources];
    } else if (_adapter) {
        [_adapter allNotesOff];
        [_adapter reset];
        [self _publishHostTransportSnapshotNonRealtimeWithFrameCount:self.maximumFramesToRender];
    }
    _parameterTree = nil;
    _inputBusArray = nil;
    _outputBusArray = nil;
    _outputFormat = nil;
    _factoryPresetCache = nil;
    _currentPresetMetadata = nil;
    _adapter = nil;
}

// ─── Bus Configuration ────────────────────────────────────────────────────────

- (void)_setupBusses {
    _outputFormat = [[AVAudioFormat alloc]
        initWithCommonFormat:AVAudioPCMFormatFloat32
                  sampleRate:kArpSIDDefaultSampleRate
                    channels:2
                 interleaved:NO];

    _inputBusArray = nil;

    AUAudioUnitBus* outputBus = [[AUAudioUnitBus alloc]
        initWithFormat:_outputFormat error:nil];
    _outputBusArray = [[AUAudioUnitBusArray alloc]
        initWithAudioUnit:self busType:AUAudioUnitBusTypeOutput busses:@[outputBus]];
}

- (AUAudioUnitBusArray*)inputBusses  { return nil;  }
- (AUAudioUnitBusArray*)outputBusses { return _outputBusArray; }

- (NSArray<NSNumber*>*)channelCapabilities {
    // Output-only instrument: explicitly advertise both mono and stereo.
    // The AUv2 bridge and auval can probe mono first; advertising only 0/2
    // causes avoidable initialization failures.
    return @[ @0, @1, @0, @2 ];
}

- (BOOL)shouldChangeToFormat:(AVAudioFormat*)format forBus:(AUAudioUnitBus*)bus {
    if (!format || !bus) return NO;
    if (bus.busType == AUAudioUnitBusTypeInput) return NO;
    if (!ArpSIDIsCanonicalHostSampleRate(format.sampleRate)) return NO;
    const AVAudioChannelCount ch = format.channelCount;
    if (!(ch == 1 || ch == 2)) return NO;
    if (format.commonFormat != AVAudioPCMFormatFloat32) return NO;
    // Accept either interleaved or deinterleaved float32 so host negotiation
    // can complete and the render path can fan out or zero-fill as required.
    return YES;
}

- (void)setMaximumFramesToRender:(AUAudioFrameCount)maximumFramesToRender {
    const AUAudioFrameCount clamped = ArpSIDSanitizeHostMaxFrames(maximumFramesToRender);
    [super setMaximumFramesToRender:clamped];
    if (!self.renderResourcesAllocated) {
        // Audit #51: gate scratch resize through the lock so it cannot race
        // a concurrent allocateRenderResourcesAndReturnError / deallocate.
        // The render path never takes this lock; it consults
        // `_renderScratchEpoch` instead. Scratch is held at the hard ceiling;
        // this refresh is normally a no-op and exists as a defensive repair
        // path if a future lifecycle change ever leaves the vectors undersized.
        [_renderScratchMutex lock];
        _renderScratchEpoch.fetch_add(1u, std::memory_order_acq_rel);
        if (!self.renderResourcesAllocated) {
            (void)clamped;
            const size_t reserveFrames = ArpSIDScratchStableRenderFrames();
            ArpSIDEnsureFloatScratchCapacity(_renderPlanarL, reserveFrames);
            ArpSIDEnsureFloatScratchCapacity(_renderPlanarR, reserveFrames);
        }
        _renderScratchEpoch.fetch_add(1u, std::memory_order_acq_rel);
        [_renderScratchMutex unlock];
    }
}


- (BOOL)canProcessInPlace {
    return YES;
}

- (void)reset {
    if (_adapter) {
        [_adapter reset];
        [self _publishHostTransportSnapshotNonRealtimeWithFrameCount:self.maximumFramesToRender];
    }
}

// ─── Parameter helper ─────────────────────────────────────────────────────────
- (void)_publishHostTransportSnapshotNonRealtimeWithFrameCount:(AUAudioFrameCount)frames {
    double hostTempo = 120.0;
    double hostBeat = 0.0;
    double hostLoopStart = 0.0;
    double hostLoopEnd = 0.0;
    uint32_t flagsOut = 0u;
    AUHostMusicalContextBlock musCtx = self.musicalContextBlock;
    if (musCtx) {
        double bpm = 0.0, num = 0.0, beatPos = 0.0, measureDownbeat = 0.0;
        NSInteger den = 4;
        NSInteger sampleOffsetToNextBeat = 0;
        if (musCtx(&bpm, &num, &den, &beatPos, &sampleOffsetToNextBeat, &measureDownbeat)) {
            if (std::isfinite(bpm) && bpm >= 1.0 && bpm <= 1000.0) hostTempo = bpm;
            if (std::isfinite(beatPos) && beatPos >= 0.0) hostBeat = beatPos;
        }
    }
    AUHostTransportStateBlock transp = self.transportStateBlock;
    if (transp) {
        AUHostTransportStateFlags flags = 0;
        double samplePos = 0.0, cycStart = 0.0, cycEnd = 0.0;
        if (transp(&flags, &samplePos, &cycStart, &cycEnd)) {
            flagsOut |= 1u;
            if ((flags & AUHostTransportStateMoving) != 0) flagsOut |= 2u;
            if ((flags & AUHostTransportStateCycling) != 0) flagsOut |= 4u;
            if (std::isfinite(cycStart)) hostLoopStart = std::max(0.0, cycStart);
            if (std::isfinite(cycEnd)) hostLoopEnd = std::max(hostLoopStart, cycEnd);
        }
    }
    const double sr = ArpSIDSanitizeHostSampleRate([self currentSampleRate]);
    // Publish as an odd/even seqlock-style snapshot. Render accepts only a
    // non-zero even generation whose before/after reads match, so it can never
    // observe mixed BPM/beat/loop/flag fields from two host polls.
    uint64_t seq = _hostTransportSnapshotGeneration.load(std::memory_order_relaxed);
    if ((seq & 1u) != 0u) ++seq;
    _hostTransportSnapshotGeneration.store(seq + 1u, std::memory_order_release);
    _hostTransportSnapshotBpm.store(hostTempo, std::memory_order_relaxed);
    _hostTransportSnapshotBeat.store(hostBeat, std::memory_order_relaxed);
    _hostTransportSnapshotSampleRate.store(sr, std::memory_order_relaxed);
    _hostTransportSnapshotLoopStart.store(hostLoopStart, std::memory_order_relaxed);
    _hostTransportSnapshotLoopEnd.store(hostLoopEnd, std::memory_order_relaxed);
    _hostTransportSnapshotFrameCount.store((int)frames, std::memory_order_relaxed);
    _hostTransportSnapshotFlags.store(flagsOut, std::memory_order_relaxed);
    _hostTransportSnapshotGeneration.store(seq + 2u, std::memory_order_release);
}

- (void)refreshHostTransportSnapshotForFrameCount:(AUAudioFrameCount)frames {
    [self _publishHostTransportSnapshotNonRealtimeWithFrameCount:frames];
}

#ifdef __cplusplus
- (ArpSIDHostTransportSnapshotAccess)hostTransportSnapshotAccess {
    ArpSIDHostTransportSnapshotAccess access;
    access.generation = &_hostTransportSnapshotGeneration;
    access.bpm = &_hostTransportSnapshotBpm;
    access.beat = &_hostTransportSnapshotBeat;
    access.sampleRate = &_hostTransportSnapshotSampleRate;
    access.loopStart = &_hostTransportSnapshotLoopStart;
    access.loopEnd = &_hostTransportSnapshotLoopEnd;
    access.frameCount = &_hostTransportSnapshotFrameCount;
    access.flags = &_hostTransportSnapshotFlags;
    return access;
}
#endif


- (BOOL)_hostTransportIsMovingNonRealtime {
    [self _publishHostTransportSnapshotNonRealtimeWithFrameCount:self.maximumFramesToRender];
    AUHostTransportStateBlock block = self.transportStateBlock;
    if (!block) return NO;
    AUHostTransportStateFlags flags = 0;
    double sample = 0.0, start = 0.0, end = 0.0;
    if (!block(&flags, &sample, &start, &end)) return NO;
    return (flags & AUHostTransportStateMoving) != 0;
}

- (void)_applyParameterValue:(float)value forID:(int)paramID updateTree:(BOOL)updateTree {
    // Guard: reject NaN, Inf, out-of-range before applying
    const float clamped = ArpSID::sanitizeNormalizedParamValue(paramID, value, ArpSID::defaultNormalizedParamValue(paramID));

    // kParamProgram and kParamBankSlot are not ordinary
    // host-write parameters. Logic can replay GM/preset metadata when transport
    // starts, and even if currentPreset is protected, writing these params still
    // changes the visible patch number because UI/telemetry reads BankSlot.
    // Only explicit preset apply / project restore may alter these mirrors.
    if ((paramID == ArpSID::kParamProgram || paramID == ArpSID::kParamBankSlot) &&
        ![self _presetApplyGuardActive]) {
        return;
    }

    // ordinary parameter writes are never preset-selection; only explicit preset APIs own factory slot changes.
    [_adapter setParameterID:paramID value:clamped];

    if (updateTree) {
        AUParameter* param = [_parameterTree parameterWithAddress:(AUParameterAddress)paramID];
        if (param) {
            // Non-nil sentinel originator: tree updates cached value but does NOT
            // re-notify observers — breaks double-write during bulk preset load.
            [param setValue:clamped originator:(__bridge void*)self];  // ARC: void* sentinel, no ownership transfer
        }
    }
}

// ─── Parameter Tree ───────────────────────────────────────────────────────────

- (void)_buildParameterTree {
    NSMutableArray<AUParameterNode*>* allGroups = [NSMutableArray array];

    AUParameter* (^makeParam)(int) = ^AUParameter*(int pid) {
        if (!auv3ParamIsAutomatable(pid)) return nil;
        const ArpSID::ParamInfo& info = ArpSID::kParamInfos[(size_t)pid];
        // Choose a semantically correct unit from the ParamInfo.unit hint
        AudioUnitParameterUnit auUnit = kAudioUnitParameterUnit_Generic;
        NSString* unitName = nil;
        const char* u = info.unit;
        if      (strcmp(u,"s")   ==0) auUnit = kAudioUnitParameterUnit_Seconds;
        else if (strcmp(u,"semi")==0) auUnit = kAudioUnitParameterUnit_RelativeSemiTones;
        else if (strcmp(u,"norm")==0) auUnit = kAudioUnitParameterUnit_LinearGain;
        else if (strcmp(u,"Hz")  ==0) auUnit = kAudioUnitParameterUnit_Hertz;
        else if (strcmp(u,"dB")  ==0) auUnit = kAudioUnitParameterUnit_Decibels;
        else if (strcmp(u,"pct") ==0) auUnit = kAudioUnitParameterUnit_Percent;
        else if (strcmp(u,"bpm") ==0) auUnit = kAudioUnitParameterUnit_BPM;
        else if (strcmp(u,"ms")  ==0) auUnit = kAudioUnitParameterUnit_Milliseconds;
        else if (strcmp(u,"ct")  ==0) auUnit = kAudioUnitParameterUnit_Cents;
        // Boolean semantics come from the shared parameter contract; display
        // names are presentation and are not a reliable type system.
        else if (ArpSID::isBooleanNormalizedParam(pid)) {
            auUnit = kAudioUnitParameterUnit_Boolean;
        }
        return [AUParameterTree createParameterWithIdentifier:
                    [NSString stringWithFormat:@"arpsid_%d", pid]
                                                         name:[NSString stringWithUTF8String:info.name]
                                                      address:(AUParameterAddress)pid
                                                          min:0.0f max:1.0f
                                                         unit:auUnit
                                                     unitName:unitName
                                                        flags:kAudioUnitParameterFlag_IsWritable |
                                                               kAudioUnitParameterFlag_IsReadable
                                                 valueStrings:nil dependentParameters:nil];
    };

    AUParameterGroup* (^makeGroup)(NSString*, NSArray<NSNumber*>*) =
        ^AUParameterGroup*(NSString* name, NSArray<NSNumber*>* pids) {
            NSMutableArray* nodes = [NSMutableArray array];
            for (NSNumber* n in pids) { AUParameter* p = makeParam(n.intValue); if (p) [nodes addObject:p]; }
            return [AUParameterTree createGroupWithIdentifier:
                        [NSString stringWithFormat:@"grp_%@", [[name stringByReplacingOccurrencesOfString:@" " withString:@"_"] stringByReplacingOccurrencesOfString:@"/" withString:@"_"]] name:name children:nodes];
        };

    [allGroups addObject:makeGroup(@"Master", @[@(ArpSID::kParamMasterVolume),@(ArpSID::kParamMasterTune),
        @(ArpSID::kParamPortamentoTime),@(ArpSID::kParamVoiceMode),@(ArpSID::kParamVoiceSpread)])];
    [allGroups addObject:makeGroup(@"VCO 1", @[@(ArpSID::kParamVCO1Waveform),@(ArpSID::kParamVCO1PulseWidth),
        @(ArpSID::kParamVCO1Detune),@(ArpSID::kParamVCO1Level),@(ArpSID::kParamVCO1LowFreqMode),
        @(ArpSID::kParamVCO1PWMDepth),@(ArpSID::kParamVCO1SyncEnable),@(ArpSID::kParamVCO1RingModEnable)])];
    [allGroups addObject:makeGroup(@"VCO 2", @[@(ArpSID::kParamVCO2Waveform),@(ArpSID::kParamVCO2PulseWidth),
        @(ArpSID::kParamVCO2Detune),@(ArpSID::kParamVCO2Level),@(ArpSID::kParamVCO2LowFreqMode),
        @(ArpSID::kParamVCO2PWMDepth),@(ArpSID::kParamVCO2SyncEnable),@(ArpSID::kParamVCO2RingModEnable)])];
    [allGroups addObject:makeGroup(@"VCO 3", @[@(ArpSID::kParamVCO3Waveform),@(ArpSID::kParamVCO3PulseWidth),
        @(ArpSID::kParamVCO3Detune),@(ArpSID::kParamVCO3Level),@(ArpSID::kParamVCO3LowFreqMode),
        @(ArpSID::kParamVCO3PWMDepth),@(ArpSID::kParamVCO3SyncEnable),@(ArpSID::kParamVCO3RingModEnable)])];
    [allGroups addObject:makeGroup(@"Filter", @[@(ArpSID::kParamFilterCutoff),@(ArpSID::kParamFilterResonance),
        @(ArpSID::kParamFilterMode),@(ArpSID::kParamFilterEnvAmount),@(ArpSID::kParamFilterLFOAmount),
        @(ArpSID::kParamFilterKeyTrack),@(ArpSID::kParamFilterDrive)])];
    [allGroups addObject:makeGroup(@"ADSR", @[@(ArpSID::kParamAttack),@(ArpSID::kParamDecay),
        @(ArpSID::kParamSustain),@(ArpSID::kParamRelease)])];
    [allGroups addObject:makeGroup(@"LFO 1", @[@(ArpSID::kParamLFORate),@(ArpSID::kParamLFODepth),
        @(ArpSID::kParamLFOShape),@(ArpSID::kParamLFOSync)])];
    [allGroups addObject:makeGroup(@"LFO 2", @[@(ArpSID::kParamLFO2Rate),@(ArpSID::kParamLFO2Depth),
        @(ArpSID::kParamLFO2Shape),@(ArpSID::kParamLFO2Sync)])];
    [allGroups addObject:makeGroup(@"LFO 3", @[@(ArpSID::kParamLFO3Rate),@(ArpSID::kParamLFO3Depth),
        @(ArpSID::kParamLFO3Shape),@(ArpSID::kParamLFO3Sync)])];
    [allGroups addObject:makeGroup(@"LFO 4", @[@(ArpSID::kParamLFO4Rate),@(ArpSID::kParamLFO4Depth),
        @(ArpSID::kParamLFO4Shape),@(ArpSID::kParamLFO4Sync)])];
    [allGroups addObject:makeGroup(@"Arpeggiator", @[@(ArpSID::kParamArpEnable),@(ArpSID::kParamArpMode),
        @(ArpSID::kParamArpRate),@(ArpSID::kParamArpOctaves),@(ArpSID::kParamArpSwing),
        @(ArpSID::kParamArpGate),@(ArpSID::kParamArpHold),@(ArpSID::kParamArpLatch),
        @(ArpSID::kParamArpTranspose),@(ArpSID::kParamArpRandom),@(ArpSID::kParamArpPatternLength)])];
    [allGroups addObject:makeGroup(@"DrSID Drums", @[@(ArpSID::kParamDrSidEnable),@(ArpSID::kParamDrSidKickTune),
        @(ArpSID::kParamDrSidKickDecay),@(ArpSID::kParamDrSidSnareTone),@(ArpSID::kParamDrSidSnareSnap),
        @(ArpSID::kParamDrSidHatTune),@(ArpSID::kParamDrSidHatDecay),@(ArpSID::kParamDrSidClapDecay),
        @(ArpSID::kParamDrSidCowbellTune),@(ArpSID::kParamDrSidCowbellDecay),
        @(ArpSID::kParamDrSidTomTune),@(ArpSID::kParamDrSidTomDecay),@(ArpSID::kParamDrSidVolume),
        @(ArpSID::kParamDrSidMachineModel),@(ArpSID::kParamDrSidAccentAmount),
        @(ArpSID::kParamDrSidOutputDrive),@(ArpSID::kParamDrSidHatMetal),
        @(ArpSID::kParamDrSidClapSpread),
        // v910: Classic-mode authority opt-in — allow GM channel-10 notes to
        // auto-arm DrSID in the Hybrid flavor (default OFF; dedicated drum
        // flavors always allow, Instrument/C64 flavors never).
        @(ArpSID::kParamAutoGmDrumPromotion)])];
    [allGroups addObject:makeGroup(@"Sequencer", @[@(ArpSID::kParamSeqEnable),@(ArpSID::kParamSeqTempo),
        @(ArpSID::kParamSeqSwing),@(ArpSID::kParamSeqMode),@(ArpSID::kParamSeqLength)])];
    {
        NSMutableArray* sp = [NSMutableArray array];
        for (int s = 0; s < 32; ++s) {
            int b = (int)ArpSID::kParamSeqStep1Note + s*3;
            [sp addObject:@(b)]; [sp addObject:@(b+1)]; [sp addObject:@(b+2)];
        }
        [allGroups addObject:makeGroup(@"Sequencer Steps", sp)];
    }
    [allGroups addObject:makeGroup(@"Macros", @[@(ArpSID::kParamMacro1),@(ArpSID::kParamMacro2),
        @(ArpSID::kParamMacro3),@(ArpSID::kParamMacro4),@(ArpSID::kParamMacro5),
        @(ArpSID::kParamMacro6),@(ArpSID::kParamMacro7),@(ArpSID::kParamMacro8)])];
    [allGroups addObject:makeGroup(@"Output / FX", @[@(ArpSID::kParamOutputLimiter),
        @(ArpSID::kParamLimiterThreshold),@(ArpSID::kParamLimiterAttack),
        @(ArpSID::kParamLimiterRelease),@(ArpSID::kParamReverbMix)])];
    [allGroups addObject:makeGroup(@"Modulation Matrix", @[
        @(ArpSID::kParamModVCFCutoffSource),@(ArpSID::kParamModVCFCutoffDepth),
        @(ArpSID::kParamModVCFResonanceSource),@(ArpSID::kParamModVCFResonanceDepth),
        @(ArpSID::kParamModVCO1FreqSource),@(ArpSID::kParamModVCO1FreqDepth),
        @(ArpSID::kParamModVCO1PWSource),@(ArpSID::kParamModVCO1PWDepth),
        @(ArpSID::kParamModVCO2FreqSource),@(ArpSID::kParamModVCO2FreqDepth),
        @(ArpSID::kParamModVCO2PWSource),@(ArpSID::kParamModVCO2PWDepth),
        @(ArpSID::kParamModVCO3FreqSource),@(ArpSID::kParamModVCO3FreqDepth),
        @(ArpSID::kParamModVCO3PWSource),@(ArpSID::kParamModVCO3PWDepth),
        @(ArpSID::kParamModMasterVolumeSource),@(ArpSID::kParamModMasterVolumeDepth)])];
    {
        NSMutableArray* sp = [NSMutableArray array];
        [sp addObject:@(ArpSID::kParamSynthModeEnable)];
        [sp addObject:@(ArpSID::kParamSidChipRevision)];
        [sp addObject:@(ArpSID::kParamSidExternalRcEnable)];
        [sp addObject:@(ArpSID::kParamSidOversamplingFactor)];
        [sp addObject:@(ArpSID::kParamSidAdsrBug6581)];
        // kParamSidModel / kParamSidClockSystem are legacy presentation mirrors only.
        // They must not be exposed as first-class editable SID REG controls in the canonical AU group.
        for (int r = 0; r < ArpSID::kSidRegCount; ++r) [sp addObject:@((int)ArpSID::kParamSidRegD400+r)];
        [allGroups addObject:makeGroup(@"Synth Mode (SID Regs)", sp)];
    }
    [allGroups addObject:makeGroup(@"Forensic", @[@(ArpSID::kParamForensicEnable),
        @(ArpSID::kParamForensicStartupRandom),
        @(ArpSID::kParamForensicTemp),@(ArpSID::kParamForensicSupply),
        @(ArpSID::kParamForensicRevision),@(ArpSID::kParamForensicChipSeed),
        @(ArpSID::kParamForensicClockJitterEnable),@(ArpSID::kParamForensicClockJitter),
        @(ArpSID::kParamForensicSupplyRippleEnable),@(ArpSID::kParamForensicSupplyRipple),
        @(ArpSID::kParamForensicThermalDriftEnable),@(ArpSID::kParamForensicThermalDrift),
        @(ArpSID::kParamForensicVoiceCrosstalkEnable),@(ArpSID::kParamForensicVoiceCrosstalk),
        @(ArpSID::kParamForensicExternalBleedEnable),@(ArpSID::kParamForensicExternalBleed),
        @(ArpSID::kParamForensicDigifix8580),@(ArpSID::kParamForensicIntensity),
        @(ArpSID::kParamForensicEnvelopeTDM),@(ArpSID::kParamForensicD418Asymmetry),
        @(ArpSID::kParamForensicFilterOhmic),@(ArpSID::kParamForensicSystemNoise),
        @(ArpSID::kParamForensicMotherboard),@(ArpSID::kParamForensicADCBleed),
        @(ArpSID::kParamForensicBusCollision),@(ArpSID::kParamForensicPOTInput)])];
    // Program/BankSlot are not editable AUParameterTree controls. They are
    // sticky preset readback mirrors only; preset selection uses factory/current
    // preset APIs or applyUserFactoryPresetNumber:. Exposing them here lets Logic
    // replay stale metadata on Play.
    [allGroups addObject:makeGroup(@"Control", @[@(ArpSID::kParamVirtualNote),
        @(ArpSID::kParamVirtualGate)])];

    _parameterTree = [AUParameterTree createTreeWithChildren:allGroups];
}

// ─── Parameter Tree Callbacks ────────────────────────────────────────────────

- (void)_connectParameterTreeCallbacks {
    ArpSIDAudioUnit* __weak weakSelf = self;

    _parameterTree.implementorValueObserver = ^(AUParameter* param, AUValue value) {
        ArpSIDAudioUnit* s = weakSelf; if (!s) return;
        const int pid = (int)param.address;
        const float clamped = ArpSID::sanitizeNormalizedParamValue(pid, value, ArpSID::defaultNormalizedParamValue(pid));
        // never let host automation/MIDI metadata replay write Program or
        // BankSlot into the DSP param mirror. These are preset identity mirrors,
        // not transport-time automation targets.
        if ((pid == ArpSID::kParamProgram || pid == ArpSID::kParamBankSlot) &&
            ![s _presetApplyGuardActive]) {
            return;
        }
        // audit #16: explicit null-guard. Sending to a nil adapter is a silent
        // ObjC no-op, which would hide writes lost during the teardown window;
        // count them instead so the loss is observable in diagnostics.
        if (!s->_adapter) { s->_droppedParameterWrites.fetch_add(1u, std::memory_order_relaxed); return; }
        [s->_adapter setParameterID:pid value:clamped];
    };
    _parameterTree.implementorValueProvider = ^AUValue(AUParameter* param) {
        ArpSIDAudioUnit* s = weakSelf; if (!s) return 0.f;
        const int pid = (int)param.address;
        // AUParameterTree readback must use the same sticky preset authority
        // as AUv2 GetParameter / public getParameterValue. If the tree provider
        // reads the adapter shadow for Program/BankSlot, Logic can observe a stale
        // piano/GM slot and repaint/reassert patch 0 when transport starts.
        // (These pinned readbacks don't touch the adapter, so they stay valid even
        // during the teardown window.)
        if (pid == ArpSID::kParamBankSlot) return [s _pinnedBankSlotNormalizedValue];
        if (pid == ArpSID::kParamProgram) return [s _pinnedProgramNormalizedValue];
        // audit #16: null-guard the adapter readback during teardown.
        if (!s->_adapter) return ArpSID::defaultNormalizedParamValue(pid);
        return [s->_adapter getParameterID:pid];
    };
    _parameterTree.implementorStringFromValueCallback =
        ^NSString*(AUParameter* param, const AUValue* __nullable value) {
            AUValue v = value ? *value : param.value;
            return ArpSIDParameterDisplayString((int)param.address, v);
        };
    // v966: text entry inverts the same shared presentation laws that
    // produced the display string, instead of the AUParameter default
    // (which interprets typed text as a raw normalized float).
    _parameterTree.implementorValueFromStringCallback =
        ^AUValue(AUParameter* param, NSString* string) {
            float normalized = 0.0f;
            if (string && ArpSID::SidParameterPresentation::parseToNormalized(
                    (int)param.address, string.UTF8String, normalized)) {
                return normalized;
            }
            return param.value;
        };
}

- (BOOL)_applyOutputBusFormatIfNeeded:(AVAudioFormat*)format error:(NSError* __autoreleasing*)outError {
    if (!format || _outputBusArray.count == 0) return YES;
    AUAudioUnitBus* outBus = _outputBusArray[0];
    NSError* fmtError = nil;
    if (![outBus setFormat:format error:&fmtError]) {
        if (outError) *outError = fmtError;
        return NO;
    }
    _outputFormat = format;
    return YES;
}

- (void)_negotiateInitialRenderingState {
    AUAudioUnitBus* outBus = (self.outputBusses.count > 0) ? self.outputBusses[0] : nil;
    AVAudioFormat* busFormat = outBus ? outBus.format : _outputFormat;
    if (!busFormat) {
        busFormat = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                                      sampleRate:kArpSIDDefaultSampleRate
                                                        channels:2
                                                     interleaved:NO];
    }
    (void)[self _applyOutputBusFormatIfNeeded:busFormat error:nil];
    if (self.maximumFramesToRender < 1) {
        self.maximumFramesToRender = kArpSIDDefaultMaxFrames;
    }
}

- (AVAudioFormat*)_resolvedOutputFormatForAllocation {
    AUAudioUnitBus* outBus = (self.outputBusses.count > 0) ? self.outputBusses[0] : nil;
    AVAudioFormat* fmt = outBus ? outBus.format : _outputFormat;
    const AVAudioChannelCount ch = ArpSIDSanitizeOutputChannels(fmt ? fmt.channelCount : 2);
    const double sr = ArpSIDSanitizeHostSampleRate(fmt ? fmt.sampleRate : kArpSIDDefaultSampleRate);
    const AVAudioCommonFormat cf = AVAudioPCMFormatFloat32;
    const BOOL interleaved = (fmt != nil) ? fmt.isInterleaved : NO;
    AVAudioFormat* resolved = [[AVAudioFormat alloc] initWithCommonFormat:cf sampleRate:sr channels:ch interleaved:interleaved];
    return resolved ? resolved : [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                                                   sampleRate:kArpSIDDefaultSampleRate
                                                                     channels:2
                                                                  interleaved:NO];
}

// ─── Resource Allocation ─────────────────────────────────────────────────────

- (BOOL)allocateRenderResourcesAndReturnError:(NSError* __autoreleasing*)outError {
    [self _negotiateInitialRenderingState];
    if (_isSetup) {
        [self deallocateRenderResources];
    }

    AVAudioFormat* resolvedFormat = [self _resolvedOutputFormatForAllocation];
    if (![self _applyOutputBusFormatIfNeeded:resolvedFormat error:outError]) return NO;

    if (![super allocateRenderResourcesAndReturnError:outError]) return NO;

    const double safeSR = ArpSIDSanitizeHostSampleRate(resolvedFormat ? resolvedFormat.sampleRate : kArpSIDDefaultSampleRate);
    const AVAudioChannelCount safeCh = ArpSIDSanitizeOutputChannels(resolvedFormat ? resolvedFormat.channelCount : 2);
    const int maxFrames = (int)ArpSIDSanitizeHostMaxFrames(self.maximumFramesToRender);
    const size_t allocFrames = ArpSIDScratchStableRenderFrames();

    _outputFormat = resolvedFormat;
    // Audit #51: same lock + epoch protocol as setMaximumFramesToRender so
    // resize-on-allocate cannot race a concurrent setMaximumFramesToRender.
    [_renderScratchMutex lock];
    _renderScratchEpoch.fetch_add(1u, std::memory_order_acq_rel);
    ArpSIDEnsureFloatScratchCapacity(_renderPlanarL, allocFrames);
    ArpSIDEnsureFloatScratchCapacity(_renderPlanarR, allocFrames);
    _renderScratchEpoch.fetch_add(1u, std::memory_order_acq_rel);
    [_renderScratchMutex unlock];

    // Allocation applies the negotiated CoreAudio format to the kernel without
    // destructively resetting audible preset/parameter authority. Logic may
    // reallocate resources at transport/configuration boundaries; this must not
    // behave like a preset reset.
    [_adapter setupPreservingAudioStateWithSampleRate:safeSR maxFrames:maxFrames];
    [self _publishHostTransportSnapshotNonRealtimeWithFrameCount:self.maximumFramesToRender];

    // Prime a known-safe output topology so repeated cold-open validation sees a
    // stable mono/stereo negotiation surface before first render.
    AUAudioUnitBus* outBus = (self.outputBusses.count > 0) ? self.outputBusses[0] : nil;
        if (outBus && outBus.format != resolvedFormat) {
            NSError* fmtError = nil;
            if (![outBus setFormat:resolvedFormat error:&fmtError]) {
                if (outError) *outError = fmtError;
                return NO;
            }
        }
    (void)safeSR;
    (void)safeCh;

    // audit P1.9: publish the kernel pointer for the render block to load each
    // invocation, now that render resources (and the kernel's setup) are valid.
    _renderKernelPtr.store([_adapter kernelPtr], std::memory_order_release);
    // audit P2.14: publish the negotiated channel count for the render block.
    _renderOutputChannels.store(
        (_outputFormat && (_outputFormat.channelCount == 1 || _outputFormat.channelCount == 2))
            ? (uint32_t)_outputFormat.channelCount : 2u,
        std::memory_order_release);

    _isSetup = YES;
    return YES;
}

- (void)deallocateRenderResources {
    // audit P0.1 (corrected): do NOT null _renderKernelPtr here. The kernel is owned
    // by the adapter and is NOT freed by render-resource deallocation, so it remains
    // valid; nulling it caused total silence when a host renders after deallocate or
    // across a deallocate/reallocate cycle. The slot is cleared only at dealloc, when
    // the adapter (and thus the kernel) is actually torn down.
    // Resource deallocation is a CoreAudio lifecycle boundary, not an audible
    // preset authority boundary. Do not reset the adapter/kernel here; Logic can
    // deallocate/reallocate during plugin scanning or graph changes, and a
    // destructive reset makes the next allocation sound like factory slot 0 while
    // the GUI/preset identity still shows the selected patch.
    _isSetup = NO;
    if (self.renderResourcesAllocated) {
        [super deallocateRenderResources];
    }
}

/// Direct MIDI injection — safe to call from ANY thread (CoreMIDI read proc, UI thread, etc.)
/// Enqueues into a lock-free ring buffer; drained at the start of each render block.
- (void)injectMIDIBytes:(const uint8_t*)data length:(uint32_t)length {
    if (!data || length < 1 || length > 4) return;
    if (!_adapter || !_isSetup) return;
    (void)[_adapter enqueueMIDIBytes:data length:length hostTime:mach_absolute_time()];
}

// ─── Render Block ─────────────────────────────────────────────────────────────
//
// HOT PATH — zero ObjC messages, zero allocation, zero locks.
//
// Host musical/transport Objective-C blocks are intentionally NOT called from
// the returned render block. Runtime receives sanitized render-local transport
// unless a non-RT host snapshot explicitly publishes newer transport.
//
- (AUInternalRenderBlock)internalRenderBlock {
    // Use __unsafe_unretained instead of __weak.
    // __weak requires an atomic retain/release on every block invocation and creates
    // a window where the object could be released between the nil-check and member
    // access. AUAudioUnit's lifetime contract guarantees the render block cannot
    // outlive the AUAudioUnit instance, so __unsafe_unretained is both correct
    // and required here (no heap allocation, no ObjC message on the hot path).
    ArpSIDDSPKernelAdapter* __unsafe_unretained adapter = _adapter;
    (void)adapter;
    // audit P0.1 (lifecycle): load the AU-owned atomic kernel slot per render
    // invocation and FAIL CLOSED (silence) when it is null. allocateRenderResources
    // publishes it and deallocate/dealloc clear it, so a host that calls a retained
    // render block after render resources were torn down renders silence instead of
    // dereferencing a stale kernel. There is intentionally NO captured-raw-pointer
    // fallback — a null slot means "not valid for render right now".
    std::atomic<ArpSID::ArpSIDDSPKernel*>* __block renderKernelSlot = &_renderKernelPtr;
    // audit P2.14: per-invocation channel-count atomic, with the captured-once
    // negotiated format as the fallback when the atomic is not yet published.
    const AVAudioChannelCount capturedOutputChannels =
        (_outputFormat && (_outputFormat.channelCount == 1 || _outputFormat.channelCount == 2))
            ? _outputFormat.channelCount : 2;
    std::atomic<uint32_t>* __block renderOutputChannelsSlot = &_renderOutputChannels;

    // Pre-capture raw planar buffer pointers. v591 reserves these vectors at
    // the hard frame ceiling before the render block is handed out, and every
    // later lifecycle refresh requests the same stable capacity. The epoch is
    // still checked by scratch-using paths so any future resize path fails
    // closed instead of touching stale pointers.
    float* __block planarL = _renderPlanarL.empty() ? nullptr : _renderPlanarL.data();
    float* __block planarR = _renderPlanarR.empty() ? nullptr : _renderPlanarR.data();
    AVAudioFrameCount __block planarCap = (AVAudioFrameCount)_renderPlanarL.size();
    // audit P0.2: no creation-time epoch capture — scratch is validated by
    // entry/exit stability inside the render block instead.
    ArpSID::EventBuffer* __block renderEventScratch = &_renderEventScratch;
    ArpSID::EventBuffer* __block renderChunkEventScratch = &_renderChunkEventScratch;
    // Audit #51/#52: capture pointers to the scratch epoch and counter for
    // the render closure. The closure reads the epoch at entry and exit; a
    // mid-render change indicates a concurrent setMaximumFramesToRender /
    // allocateRenderResources, in which case we fail-closed silent.
    std::atomic<uint64_t>* __block scratchEpoch    = &_renderScratchEpoch;
    std::atomic<uint64_t>* __block scratchUnderCount = &_scratchUnderCapacityCount;
    // audit #15: capture the defensive reentrancy flag (see CAS+RAII at body top).
    std::atomic<bool>* __block renderReentryFlag = &_renderReentryGuard;
    const ArpSID::GUI::Auv3ScratchCounterAtomicTargets scratchDiag =
        adapter ? [adapter auv3ScratchCounterAtomicTargets] : ArpSID::GUI::Auv3ScratchCounterAtomicTargets{};
    std::atomic<uint64_t>* __block diagScratchEpoch = scratchDiag.renderScratchEpoch;
    std::atomic<uint64_t>* __block diagScratchUnderCount = scratchDiag.scratchUnderCapacity;

    const std::atomic<uint64_t>* __block hostSnapGeneration = &_hostTransportSnapshotGeneration;
    const std::atomic<double>* __block hostSnapBpm = &_hostTransportSnapshotBpm;
    const std::atomic<double>* __block hostSnapBeat = &_hostTransportSnapshotBeat;
    const std::atomic<double>* __block hostSnapSampleRate = &_hostTransportSnapshotSampleRate;
    const std::atomic<double>* __block hostSnapLoopStart = &_hostTransportSnapshotLoopStart;
    const std::atomic<double>* __block hostSnapLoopEnd = &_hostTransportSnapshotLoopEnd;
    const std::atomic<int>* __block hostSnapFrameCount = &_hostTransportSnapshotFrameCount;
    const std::atomic<uint32_t>* __block hostSnapFlags = &_hostTransportSnapshotFlags;

    __block uint32_t orderCounter = 0u;

    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags* actionFlags,
                              const AudioTimeStamp*        timestamp,
                              AVAudioFrameCount             frameCount,
                              NSInteger                     outputBusNumber,
                              AudioBufferList*              outputData,
                              const AURenderEvent*          realtimeEventListHead,
                              AURenderPullInputBlock __nullable pullInputBlock) {
        (void)pullInputBlock;
        // audit #15: defensive non-reentrancy guard. AU render is contractually
        // non-reentrant; if a host ever violates that, claim the flag and fail
        // closed (silent) instead of letting two invocations corrupt the shared
        // render scratch / event buffers. The RAII releaser clears the claim on
        // EVERY exit path below (including the early returns).
        bool reentryExpected = false;
        if (!renderReentryFlag->compare_exchange_strong(reentryExpected, true,
                                                        std::memory_order_acquire,
                                                        std::memory_order_relaxed)) {
            ArpSIDZeroRenderedAuv3Buffers(outputData, frameCount, 2u);
            if (actionFlags) *actionFlags |= kAudioUnitRenderAction_OutputIsSilence;
            return noErr;
        }
        struct ArpSIDRenderReentryRelease {
            std::atomic<bool>* flag;
            ~ArpSIDRenderReentryRelease() { flag->store(false, std::memory_order_release); }
        } reentryRelease{renderReentryFlag};
        // audit P0.1: load the current kernel each invocation. No stale fallback.
        ArpSID::ArpSIDDSPKernel* kernel = renderKernelSlot->load(std::memory_order_acquire);
        if (frameCount == 0) return noErr;
        if (outputBusNumber != 0) {
            ArpSIDZeroRenderedAuv3Buffers(outputData, frameCount, 2u);
            if (actionFlags) *actionFlags |= kAudioUnitRenderAction_OutputIsSilence;
            return kAudioUnitErr_InvalidElement;
        }
        // audit P0.1: kernel slot null => render resources are not currently valid
        // (e.g. block called after deallocateRenderResources). Fail closed with
        // silence and noErr — safer and more host-compatible than a stale call or
        // a hard error.
        if (!kernel || !outputData) {
            ArpSIDZeroRenderedAuv3Buffers(outputData, frameCount, 2u);
            if (actionFlags) *actionFlags |= kAudioUnitRenderAction_OutputIsSilence;
            return noErr;
        }
        if (timestamp) kernel->setRenderHostTime(timestamp->mHostTime);
        if (outputData->mNumberBuffers < 1) return kAudioUnitErr_InvalidPropertyValue;
        // _ensureScratchFrames: removed from render path — it calls
        // std::vector::resize() which allocates heap memory (hard RT violation).
        // Scratch buffers are pre-allocated with sufficient size in
        // allocateRenderResourcesAndReturnError:. If somehow frameCount exceeds
        // the pre-allocated capacity (should never happen under normal AUv3 contract),
        // fall back to error rather than allocating on the audio thread.

        const UInt32 nbuf = outputData->mNumberBuffers;
        const uint32_t publishedCh = renderOutputChannelsSlot->load(std::memory_order_acquire);
        const AVAudioChannelCount negotiatedCh = ArpSIDSanitizeOutputChannels(
            publishedCh != 0u ? (AVAudioChannelCount)publishedCh : capturedOutputChannels);
        const bool interleavedStereo = (nbuf == 1 && outputData->mBuffers[0].mNumberChannels >= 2);
        const bool monoSingleBuffer  = (nbuf == 1 && outputData->mBuffers[0].mNumberChannels <= 1);
        const size_t frames = (size_t)frameCount;
        // audit P0.2: validate scratch by STABILITY during this render call, not by
        // equality to the block-creation-time epoch. The correct invariant is:
        // epoch even at entry (no resize in progress), pointers valid, capacity
        // sufficient, and the epoch unchanged from entry to exit (checked below).
        // Comparing to the creation-time epoch wrongly went permanently silent
        // whenever allocateRenderResources bumped the epoch after the block existed.
        const uint64_t scratchEpochAtEntry = scratchEpoch->load(std::memory_order_acquire);
        const bool scratchValidAtEntry = ((scratchEpochAtEntry & 1u) == 0u) &&
                                         planarL && planarR && planarCap >= frameCount;
        if (interleavedStereo) {
            const AudioBuffer& b = outputData->mBuffers[0];
            const size_t requiredBytes = frames * sizeof(float) * (size_t)std::max<UInt32>(1u, b.mNumberChannels);
            if (!b.mData || (size_t)b.mDataByteSize < requiredBytes) {
                ArpSIDZeroRenderedAuv3Buffers(outputData, frameCount, negotiatedCh);
                if (actionFlags) *actionFlags |= kAudioUnitRenderAction_OutputIsSilence;
                return kAudioUnitErr_TooManyFramesToProcess;
            }
        }

        if (interleavedStereo && !scratchValidAtEntry) {
            // Audit #52: pre-allocated scratch is insufficient. The legacy
            // path returned `kAudioUnitErr_TooManyFramesToProcess`, which
            // hosts (auval, Logic) can interpret as a hard error and refuse
            // to play further. AUv2 already does silent bounded fallback            // do the same here for parity: zero the output, set
            // OutputIsSilence, increment the scratch-under-capacity counter
            // for diagnostics, and return noErr so the host treats this as
            // a legitimately silent block instead of a render failure.
            ArpSIDZeroRenderedAuv3Buffers(outputData, frameCount, negotiatedCh);
            if (actionFlags) *actionFlags |= kAudioUnitRenderAction_OutputIsSilence;
            const uint64_t under = scratchUnderCount->fetch_add(1u, std::memory_order_relaxed) + 1u;
            if (diagScratchEpoch) diagScratchEpoch->store(scratchEpochAtEntry, std::memory_order_relaxed);
            if (diagScratchUnderCount) diagScratchUnderCount->store(under, std::memory_order_relaxed);
            return noErr;
        }

        float* outputs[8] = {};
        int numCh = 0;
        if (!interleavedStereo) {
            const UInt32 renderedChannels = monoSingleBuffer ? 1u : std::min<UInt32>(std::max<UInt32>(1u, negotiatedCh), 2u);
            if (nbuf < renderedChannels) {
                ArpSIDZeroRenderedAuv3Buffers(outputData, frameCount, renderedChannels);
                if (actionFlags) *actionFlags |= kAudioUnitRenderAction_OutputIsSilence;
                return kAudioUnitErr_TooManyFramesToProcess;
            }
            numCh = (int)renderedChannels;
            for (int i = 0; i < numCh; ++i) {
                AudioBuffer& b = outputData->mBuffers[(UInt32)i];
                outputs[i] = (float*)b.mData;
                const size_t requiredBytes = frames * sizeof(float);
                if (!outputs[i] || (size_t)b.mDataByteSize < requiredBytes) {
                    ArpSIDZeroRenderedAuv3Buffers(outputData, frameCount, renderedChannels);
                    if (actionFlags) *actionFlags |= kAudioUnitRenderAction_OutputIsSilence;
                    return kAudioUnitErr_TooManyFramesToProcess;
                }
                const size_t zeroBytes = std::min<size_t>((size_t)b.mDataByteSize, requiredBytes);
                std::memset(outputs[i], 0, zeroBytes);
            }
            if (numCh == 1 && negotiatedCh >= 2 &&
                scratchValidAtEntry) {
                std::fill(planarL, planarL + (ptrdiff_t)frames, 0.0f);
                std::fill(planarR, planarR + (ptrdiff_t)frames, 0.0f);
            }
            for (int i = numCh; i < 8; ++i) outputs[i] = outputs[0];
            for (UInt32 i = (UInt32)numCh; i < nbuf; ++i) {
                AudioBuffer& b = outputData->mBuffers[i];
                if (!b.mData) continue;
                const size_t zeroBytes = std::min<size_t>((size_t)b.mDataByteSize, frames * sizeof(float));
                std::memset(b.mData, 0, zeroBytes);
            }
        } else {
            std::fill(planarL, planarL + (ptrdiff_t)frames, 0.0f);
            std::fill(planarR, planarR + (ptrdiff_t)frames, 0.0f);
            outputs[0] = planarL;
            outputs[1] = planarR;
            for (int i = 2; i < 8; ++i) outputs[i] = outputs[0];
            numCh = 2;
        }

        // AUv3 render reads only a pre-published atomic POD transport snapshot.
        // No Objective-C host musical/transport blocks are called from this path.
        ArpSID::TransportState transport{};
        if (!ArpSIDReadHostTransportSnapshotCoherent(hostSnapGeneration, hostSnapBpm, hostSnapBeat,
                                                     hostSnapSampleRate, hostSnapLoopStart, hostSnapLoopEnd,
                                                     hostSnapFrameCount, hostSnapFlags, transport)) {
            transport.sampleRate = kernel->sampleRate();
            transport.frameCount = (int)frameCount;
        } else {
            transport.frameCount = (int)frameCount;
        }
        transport.sanitize();
        // Render negotiated sample-rate is owned by the AU/kernel bus format.
        // Host transport snapshots can contain stale/bogus rates and must never
        // retune the DSP or beat/sample math during render.
        transport.sampleRate = kernel->sampleRate();

        renderEventScratch->reset();
        ArpSID::EventBuffer& evBuf = *renderEventScratch;
        orderCounter = 0;
        ArpSID::translateAUEvents(realtimeEventListHead, (int)frameCount, orderCounter, evBuf);
        evBuf.sort();

        const int totalFrames = (int)frameCount;
        int firstResolvedEvent = 0;
        while (firstResolvedEvent < evBuf.count && evBuf.events[firstResolvedEvent].sampleOffset < 0)
            ++firstResolvedEvent;
        int nextResolvedEvent = firstResolvedEvent;
        for (int chunkStart = 0; chunkStart < totalFrames; chunkStart += ArpSID::ArpSIDDSPKernel::kMaxFramesPerBlock) {
            const int chunkFrames = std::min(ArpSID::ArpSIDDSPKernel::kMaxFramesPerBlock, totalFrames - chunkStart);
            const int chunkEnd = chunkStart + chunkFrames;
            renderChunkEventScratch->reset();
            ArpSID::EventBuffer& chunkEvBuf = *renderChunkEventScratch;
            if (chunkStart == 0) {
                for (int ei = 0; ei < firstResolvedEvent; ++ei) {
                    auto localEv = evBuf.events[ei];
                    localEv.sampleOffset = -1;
                    chunkEvBuf.push(localEv);
                }
            }
            while (nextResolvedEvent < evBuf.count && evBuf.events[nextResolvedEvent].sampleOffset < chunkStart)
                ++nextResolvedEvent;
            int scanEvent = nextResolvedEvent;
            while (scanEvent < evBuf.count && evBuf.events[scanEvent].sampleOffset < chunkEnd) {
                auto localEv = evBuf.events[scanEvent];
                localEv.sampleOffset -= chunkStart;
                chunkEvBuf.push(localEv);
                ++scanEvent;
            }
            nextResolvedEvent = scanEvent;
            // evBuf is globally sorted; slicing by monotonic cursor preserves order.

            ArpSID::TransportState chunkTransport = transport;
            chunkTransport.frameCount = chunkFrames;
            if (chunkTransport.sampleRate > 0.0 && chunkTransport.bpm > 0.0) {
                const double beatsPerSample = chunkTransport.bpm / (chunkTransport.sampleRate * 60.0);
                chunkTransport.beatPosition = transport.beatPosition + (double)chunkStart * beatsPerSample;
            }

            float* chunkOutputs[8] = {};
            for (int i = 0; i < numCh; ++i)
                chunkOutputs[i] = outputs[i] + chunkStart;
            for (int i = numCh; i < 8; ++i)
                chunkOutputs[i] = chunkOutputs[0];

            if (numCh >= 2) {
                // audit P0.5: pass the explicit channel count so the kernel never
                // dereferences outputs[1] beyond the supplied channel array.
                kernel->processBlock(chunkOutputs, numCh, chunkFrames,
                                     chunkEvBuf.begin(), chunkEvBuf.count, chunkTransport);
            } else {
                kernel->processBlockMono(chunkOutputs[0], chunkFrames,
                                         chunkEvBuf.begin(), chunkEvBuf.count, chunkTransport);
            }
        }

        if (interleavedStereo) {
            // audit P0.2: scratch is safe iff its epoch did not change during this
            // render call (no concurrent resize) and is even at exit. Compared to
            // entry, NOT to the block-creation epoch.
            const uint64_t scratchEpochAtExit = scratchEpoch->load(std::memory_order_acquire);
            if (scratchEpochAtExit != scratchEpochAtEntry || (scratchEpochAtExit & 1u) != 0u) {
                ArpSIDZeroRenderedAuv3Buffers(outputData, frameCount, negotiatedCh);
                if (actionFlags) *actionFlags |= kAudioUnitRenderAction_OutputIsSilence;
                const uint64_t under = scratchUnderCount->fetch_add(1u, std::memory_order_relaxed) + 1u;
                if (diagScratchEpoch) diagScratchEpoch->store(scratchEpochAtExit, std::memory_order_relaxed);
                if (diagScratchUnderCount) diagScratchUnderCount->store(under, std::memory_order_relaxed);
                return noErr;
            }
            float* raw = reinterpret_cast<float*>(outputData->mBuffers[0].mData);
            const UInt32 outCh = outputData->mBuffers[0].mNumberChannels;
            for (AVAudioFrameCount i = 0; i < frameCount; ++i) {
                raw[(size_t)i * outCh + 0] = planarL[(size_t)i];
                raw[(size_t)i * outCh + 1] = planarR[(size_t)i];
                for (UInt32 ch = 2; ch < outCh; ++ch) raw[(size_t)i * outCh + ch] = 0.0f;
            }
        } else if (monoSingleBuffer) {
            // already rendered mono in-place
        } else {
            for (int i = 2; i < numCh; ++i)
                std::memset(outputData->mBuffers[i].mData, 0, frameCount * sizeof(float));
        }

        if (diagScratchEpoch) diagScratchEpoch->store(scratchEpoch->load(std::memory_order_relaxed), std::memory_order_relaxed);
        if (diagScratchUnderCount) diagScratchUnderCount->store(scratchUnderCount->load(std::memory_order_relaxed), std::memory_order_relaxed);

        return noErr;
    };
}


// ─── MIDI Output ──────────────────────────────────────────────────────────────

- (AUMIDIEventListBlock)MIDIOutputEventListBlock { return nil; }

// ─── Factory Presets ──────────────────────────────────────────────────────────
// NOT refined for Swift — NS_REFINED_FOR_SWIFT hides the method from ObjC
// callers including Logic Pro's preset scanner.

- (NSArray<AUAudioUnitPreset*>*)factoryPresets {
    if (_factoryPresetCache) return _factoryPresetCache;

    NSMutableArray<AUAudioUnitPreset*>* arr = [NSMutableArray array];
    const auto& defs = ArpSID::getFactoryPatchDefinitions();
    const std::vector<NSInteger> orderedSlots = ArpSIDFactorySlotOrderForFlavor(_componentFlavor);
    for (NSInteger slot : orderedSlots) {
        if (slot < 0 || (size_t)slot >= defs.size()) continue;
        AUAudioUnitPreset* p = [[AUAudioUnitPreset alloc] init];
        p.number = slot;
        NSString* baseName = [NSString stringWithUTF8String:defs[(size_t)slot].displayName.c_str()];
        p.name = ArpSIDFactoryPresetTitleForFlavor(_componentFlavor, slot, baseName);
        [arr addObject:p];
    }
    _factoryPresetCache = [arr copy];
    return _factoryPresetCache;
}

- (AUAudioUnitPreset*)_factoryPresetForNumber:(NSInteger)number {
    NSArray<AUAudioUnitPreset*>* presets = [self factoryPresets];
    if (number < 0) return nil;
    const NSUInteger idx = (NSUInteger)number;
    if (idx < presets.count) {
        AUAudioUnitPreset* preset = presets[idx];
        if (preset.number == number) return preset;
    }
    for (AUAudioUnitPreset* p in presets) {
        if (p.number == number) return p;
    }
    return nil;
}

- (AUAudioUnitPreset*)currentPreset {
    AUAudioUnitPreset* preset = _currentPresetMetadata;
    if (preset) return preset;

    const NSInteger resolvedSlot = [self _resolvedCurrentFactoryPresetSlot];
    preset = [self _factoryPresetForNumber:resolvedSlot];
    if (!preset) {
        AUAudioUnitPreset* fallback = [[AUAudioUnitPreset alloc] init];
        fallback.number = resolvedSlot;
        fallback.name = @"Factory Patch";
        preset = fallback;
    }
    _currentPresetMetadata = preset;
    return preset;
}

- (BOOL)_tryBeginPresetApplyGuard {
    bool expected = false;
    const bool accepted = _presetApplyInProgress.compare_exchange_strong(expected,
                                                                         true,
                                                                         std::memory_order_acq_rel,
                                                                         std::memory_order_acquire);
    if (!accepted) {
        NSLog(@"[ArpSIDAudioUnit] preset apply skipped because another preset apply is already in progress");
    }
    return accepted;
}

- (void)_endPresetApplyGuard {
    _presetApplyInProgress.store(false, std::memory_order_release);
    _hostTransportSnapshotGeneration.store(0, std::memory_order_relaxed);
    _hostTransportSnapshotBpm.store(120.0, std::memory_order_relaxed);
    _hostTransportSnapshotBeat.store(0.0, std::memory_order_relaxed);
    _hostTransportSnapshotSampleRate.store(kArpSIDDefaultSampleRate, std::memory_order_relaxed);
    _hostTransportSnapshotLoopStart.store(0.0, std::memory_order_relaxed);
    _hostTransportSnapshotLoopEnd.store(0.0, std::memory_order_relaxed);
    _hostTransportSnapshotFrameCount.store((int)kArpSIDDefaultMaxFrames, std::memory_order_relaxed);
    _hostTransportSnapshotFlags.store(0u, std::memory_order_relaxed);
}

- (BOOL)_presetApplyGuardActive {
    return _presetApplyInProgress.load(std::memory_order_acquire);
}

- (float)_pinnedBankSlotNormalizedValue {
    const NSInteger slot = [self _resolvedCurrentFactoryPresetSlot];
    return ArpSID::canonicalNormalizedBankSlotValue((int)slot);
}

- (float)_pinnedProgramNormalizedValue {
    const NSInteger slot = [self _resolvedCurrentFactoryPresetSlot];
    return ArpSID::canonicalNormalizedFactoryProgramValue((int)slot);
}

- (NSInteger)_serializationPresetSlot {
    if (_pinnedBankSlotNumber >= 0) return ArpSIDResolveFactorySlotForFlavor(_componentFlavor, _pinnedBankSlotNumber);
    if (_pinnedFactoryPresetNumber >= 0) return ArpSIDResolveFactorySlotForFlavor(_componentFlavor, _pinnedFactoryPresetNumber);
    if (_currentFactoryPresetNumber >= 0) return ArpSIDResolveFactorySlotForFlavor(_componentFlavor, _currentFactoryPresetNumber);
    return ArpSIDStartupFactorySlotForFlavor(_componentFlavor);
}

- (NSInteger)stickyFactoryPresetNumber {
    return [self _resolvedCurrentFactoryPresetSlot];
}

- (BOOL)hasStickyUserFactoryPresetSelection {
    return _selectedUserPresetNumber >= 0;
}

- (BOOL)_setCurrentFactoryPresetMetadataOnlyForSlot:(NSInteger)slot
                                 assumeGuardActive:(BOOL)assumeGuardActive
                              updateAdapterMirrors:(BOOL)updateAdapterMirrors
                                 notifySuperPreset:(BOOL)notifySuperPreset {
    const NSInteger normalizedSlot = ArpSIDResolveFactorySlotForFlavor(_componentFlavor, slot);
    AUAudioUnitPreset* preset = [self _factoryPresetForNumber:normalizedSlot];
    if (!preset) return NO;
    AUAudioUnitPreset* current = _currentPresetMetadata;
    _currentPresetMetadata = preset;
    _currentFactoryPresetNumber = preset.number;
    _pinnedFactoryPresetNumber = preset.number;
    _pinnedBankSlotNumber = normalizedSlot;
    if (_adapter && [_adapter respondsToSelector:@selector(setStickyPresetDisplaySlot:)]) {
        [_adapter setStickyPresetDisplaySlot:normalizedSlot];
    }
    if (updateAdapterMirrors) {
        const float bankNorm = ArpSID::canonicalNormalizedBankSlotValue((int)normalizedSlot);
        const float progNorm = ArpSID::canonicalNormalizedFactoryProgramValue((int)normalizedSlot);
        if (_adapter) {
            [_adapter setParameterID:ArpSID::kParamBankSlot value:bankNorm];
            [_adapter setParameterID:ArpSID::kParamProgram value:progNorm];
        }
        if (AUParameter* bankParam = [_parameterTree parameterWithAddress:(AUParameterAddress)ArpSID::kParamBankSlot]) {
            [bankParam setValue:bankNorm originator:(__bridge void*)self];
        }
        if (AUParameter* progParam = [_parameterTree parameterWithAddress:(AUParameterAddress)ArpSID::kParamProgram]) {
            [progParam setValue:progNorm originator:(__bridge void*)self];
        }
    }
    if (notifySuperPreset && !(current && current.number == preset.number)) {
        const BOOL acquired = assumeGuardActive ? NO : [self _tryBeginPresetApplyGuard];
        if (!assumeGuardActive && !acquired) return NO;
        @try {
            [super setCurrentPreset:preset];
        } @finally {
            if (acquired) [self _endPresetApplyGuard];
        }
    }
    return YES;
}

- (BOOL)_setCurrentFactoryPresetMetadataOnlyForSlot:(NSInteger)slot assumeGuardActive:(BOOL)assumeGuardActive {
    return [self _setCurrentFactoryPresetMetadataOnlyForSlot:slot
                                           assumeGuardActive:assumeGuardActive
                                        updateAdapterMirrors:YES
                                           notifySuperPreset:YES];
}
- (void)_setCurrentFactoryPresetMetadataOnlyForSlot:(NSInteger)slot {
    (void)[self _setCurrentFactoryPresetMetadataOnlyForSlot:slot
                                          assumeGuardActive:[self _presetApplyGuardActive]
                                       updateAdapterMirrors:NO
                                          notifySuperPreset:NO];
}

- (BOOL)setCurrentFactoryPresetMetadataOnlyForSlot:(NSInteger)slot {
    const BOOL alreadyActive = [self _presetApplyGuardActive];
    const BOOL acquired = alreadyActive ? NO : [self _tryBeginPresetApplyGuard];
    if (!alreadyActive && !acquired) return NO;
    BOOL ok = NO;
    @try {
        ok = [self _setCurrentFactoryPresetMetadataOnlyForSlot:slot
                                             assumeGuardActive:YES
                                          updateAdapterMirrors:NO
                                             notifySuperPreset:NO];
    } @finally {
        if (acquired) [self _endPresetApplyGuard];
    }
    return ok;
}

- (BOOL)setRestoredFactoryPresetMetadataOnlyForSlot:(NSInteger)slot {
    const BOOL alreadyActive = [self _presetApplyGuardActive];
    const BOOL acquired = alreadyActive ? NO : [self _tryBeginPresetApplyGuard];
    if (!alreadyActive && !acquired) return NO;
    BOOL ok = NO;
    @try {
        const NSInteger normalized = ArpSIDResolveFactorySlotForFlavor(_componentFlavor, slot);
        ok = [self _setCurrentFactoryPresetMetadataOnlyForSlot:normalized
                                             assumeGuardActive:YES
                                          updateAdapterMirrors:NO
                                             notifySuperPreset:NO];
        if (ok) {
            _selectedUserPresetNumber = normalized;
            _pinnedFactoryPresetNumber = normalized;
            _pinnedBankSlotNumber = normalized;
        }
    } @finally {
        if (acquired) [self _endPresetApplyGuard];
    }
    return ok;
}

- (NSInteger)_bankSlotFromStateRoot:(const ArpSID::SidStateRootV1&)root {
    float bankNorm = ArpSID::defaultNormalizedParamValue(ArpSID::kParamBankSlot);
    bool found = false;
    for (const auto& e : root.patch.parameters.semantic_entries) {
        if ((int)e.param_id == ArpSID::kParamBankSlot) {
            bankNorm = e.value;
            found = true;
            break;
        }
    }
    if (!found && root.patch.parameters.values.size() > (size_t)ArpSID::kParamBankSlot) {
        bankNorm = root.patch.parameters.values[(size_t)ArpSID::kParamBankSlot];
        found = true;
    }
    if (!found || !std::isfinite(bankNorm)) {
        return -1;
    }
    const int bankSlot = std::clamp((int)ArpSID::canonicalFactorySlotFromNormalizedBankSlot(bankNorm), 0, ArpSID::kCanonicalFactoryPatchSlotMax);
    return (NSInteger)ArpSID::canonicalFactorySlotForRoot(bankSlot);
}

- (NSInteger)_factorySlotFromStateRoot:(const ArpSID::SidStateRootV1&)root {
    const NSInteger bankSlot = [self _bankSlotFromStateRoot:root];
    if (bankSlot >= 0) return ArpSIDResolveFactorySlotForFlavor(_componentFlavor, bankSlot);
    // kParamProgram is GM/presentation metadata only. Never derive ArpSID preset
    // identity from it, otherwise piano MIDI/program 0 can reset the selected patch
    // on transport start. Missing BankSlot means use existing pinned/current preset.
    return -1;
}

- (void)_pinFactoryPresetMetadataFromStateRoot:(const ArpSID::SidStateRootV1&)root {
    const NSInteger slot = [self _factorySlotFromStateRoot:root];
    if (slot < 0) return;
    _selectedUserPresetNumber = slot;
    _pinnedFactoryPresetNumber = slot;
    _pinnedBankSlotNumber = slot;
    [self _setCurrentFactoryPresetMetadataOnlyForSlot:slot
                                      assumeGuardActive:[self _presetApplyGuardActive]
                                   updateAdapterMirrors:NO
                                      notifySuperPreset:NO];
}

- (NSInteger)_resolvedCurrentFactoryPresetSlot {
    if (_selectedUserPresetNumber >= 0) {
        const NSInteger slot = ArpSIDResolveFactorySlotForFlavor(_componentFlavor, _selectedUserPresetNumber);
        _pinnedFactoryPresetNumber = slot;
        _pinnedBankSlotNumber = slot;
        return slot;
    }
    // explicit user preset is the highest authority.
    if (_pinnedBankSlotNumber >= 0) {
        return ArpSIDResolveFactorySlotForFlavor(_componentFlavor, _pinnedBankSlotNumber);
    }
    if (_pinnedFactoryPresetNumber >= 0) {
        return ArpSIDResolveFactorySlotForFlavor(_componentFlavor, _pinnedFactoryPresetNumber);
    }
    ArpSID::SidStateRootV1 root{};
    if (ArpSID::ArpSIDDSPKernel* k = [_adapter kernelPtr]) {
        k->buildSerializableStateRootFromShadow(root);
        if (root.valid()) {
            const NSInteger slot = [self _factorySlotFromStateRoot:root];
            if (slot >= 0) {
                _pinnedFactoryPresetNumber = slot;
                _pinnedBankSlotNumber = slot;
                return slot;
            }
        }
    }
    if (_currentFactoryPresetNumber >= 0) {
        const NSInteger slot = ArpSIDResolveFactorySlotForFlavor(_componentFlavor, _currentFactoryPresetNumber);
        _pinnedFactoryPresetNumber = slot;
        _pinnedBankSlotNumber = slot;
        return slot;
    }
    const NSInteger startupSlot = ArpSIDStartupFactorySlotForFlavor(_componentFlavor);
    _pinnedFactoryPresetNumber = startupSlot;
    _pinnedBankSlotNumber = startupSlot;
    return startupSlot;
}


- (float)_normalizedParamValue:(int)paramID fromStateRoot:(const ArpSID::SidStateRootV1&)root fallback:(float)fallback {
    float value = fallback;
    bool found = false;
    for (const auto& e : root.patch.parameters.semantic_entries) {
        if ((int)e.param_id == paramID) {
            value = e.value;
            found = true;
            break;
        }
    }
    if (!found && paramID >= 0 && root.patch.parameters.values.size() > (size_t)paramID) {
        value = root.patch.parameters.values[(size_t)paramID];
        found = true;
    }
    if (!std::isfinite(value)) value = fallback;
    return std::clamp(value, 0.0f, 1.0f);
}

- (float)_currentNormalizedValueForParam:(int)paramID fallback:(float)fallback {
    if (paramID == ArpSID::kParamBankSlot) return [self _pinnedBankSlotNormalizedValue];
    if (paramID == ArpSID::kParamProgram) return [self _pinnedProgramNormalizedValue];
    if (paramID < 0 || paramID >= ArpSID::kNumParams) return fallback;
    if (AUParameter* param = [_parameterTree parameterWithAddress:(AUParameterAddress)paramID]) {
        // AUParameter.value is documented as a thread-safe read. This path only
        // needs a weakly-consistent snapshot for state export/UI recovery.
        const float v = param.value;
        if (std::isfinite(v)) return std::clamp(v, 0.0f, 1.0f);
    }
    if (_adapter) {
        const float v = [_adapter getParameterID:paramID];
        if (std::isfinite(v)) return std::clamp(v, 0.0f, 1.0f);
    }
    return fallback;
}

- (NSDictionary<NSString*, id>*)_auxStateSnapshotFromRoot:(const ArpSID::SidStateRootV1&)root {
    NSMutableDictionary<NSString*, id>* d = [NSMutableDictionary dictionary];
    for (const auto& binding : kArpSIDAuxParamBindings) {
        if (binding.paramID == ArpSID::kParamBankSlot) {
            d[*binding.key] = @([self _pinnedBankSlotNormalizedValue]);
            continue;
        }
        if (binding.paramID == ArpSID::kParamProgram) {
            d[*binding.key] = @([self _pinnedProgramNormalizedValue]);
            continue;
        }
        const float fallback = [self _currentNormalizedValueForParam:binding.paramID fallback:ArpSID::defaultNormalizedParamValue(binding.paramID)];
        // the audible SID chip selector is explicit user/UI state.
        // Do not let a stale factory/root blob reassert a patch-authored chip
        // over the currently selected MOS 6581/8580 revision while saving.
        const float value = (binding.paramID == ArpSID::kParamSidChipRevision)
            ? fallback
            : [self _normalizedParamValue:binding.paramID fromStateRoot:root fallback:fallback];
        d[*binding.key] = @(value);
    }

    // store a stable discrete selector index in addition to the
    // normalized AU parameter. This makes project/user-preset restore robust
    // against floating point round-trip drift and keeps kParamSidModel coherent.
    const float chipNorm228 = [self _currentNormalizedValueForParam:ArpSID::kParamSidChipRevision
                                                          fallback:ArpSID::sidChipRevisionIndexToNormalized(ArpSID::kSidChipRevisionDefaultIndex)];
    const NSInteger chipIndex228 = std::clamp((NSInteger)ArpSID::sidChipRevisionIndexFromNormalized(chipNorm228),
                                             (NSInteger)0, (NSInteger)3);
    d[kArpSIDStateKey_SidChipRevisionNorm] = @(ArpSID::sidChipRevisionIndexToNormalized((int)chipIndex228));
    d[kArpSIDStateKey_SidChipSelectionIndex] = @(chipIndex228);
    d[kArpSIDStateKey_SidChipFamilyNorm] = @(chipIndex228 >= 3 ? 1.0f : 0.0f);

    double hostTempo = 120.0;
    double hostBeat = 0.0;
    BOOL hostPlaying = NO;
    BOOL hostLooping = NO;
    double hostLoopStart = 0.0;
    double hostLoopEnd = 0.0;
    double hostTimeSigNum = 4.0;
    NSInteger hostTimeSigDen = 4;
    double hostMeasureDownbeat = 0.0;
    AUHostMusicalContextBlock musCtx = self.musicalContextBlock;
    if (musCtx) {
        double bpm = 0.0, num = 0.0, beatPos = 0.0, measureDownbeat = 0.0;
        NSInteger den = 4;
        NSInteger sampleOffsetToNextBeat = 0;
        if (musCtx(&bpm, &num, &den, &beatPos, &sampleOffsetToNextBeat, &measureDownbeat)) {
            if (std::isfinite(bpm) && bpm >= 1.0 && bpm <= 1000.0) hostTempo = bpm;
            if (std::isfinite(beatPos) && beatPos >= 0.0) hostBeat = beatPos;
            if (std::isfinite(num) && num >= 1.0 && num <= 64.0) hostTimeSigNum = num;
            if (den >= 1 && den <= 64) hostTimeSigDen = den;
            if (std::isfinite(measureDownbeat) && measureDownbeat >= 0.0) hostMeasureDownbeat = measureDownbeat;
        }
    }
    AUHostTransportStateBlock transp = self.transportStateBlock;
    if (transp) {
        AUHostTransportStateFlags flags = 0;
        double samplePos = 0.0, cycStart = 0.0, cycEnd = 0.0;
        if (transp(&flags, &samplePos, &cycStart, &cycEnd)) {
            hostPlaying = (flags & AUHostTransportStateMoving) != 0;
            hostLooping = (flags & AUHostTransportStateCycling) != 0;
            if (std::isfinite(cycStart)) hostLoopStart = std::max(0.0, cycStart);
            if (std::isfinite(cycEnd)) hostLoopEnd = std::max(hostLoopStart, cycEnd);
        }
    }
    if (ArpSID::ArpSIDDSPKernel* k = [_adapter kernelPtr]) {
        const double bpm = k->hostTempo();
        const double beat = k->hostBeatPosition();
        if (std::isfinite(bpm) && bpm >= 1.0 && bpm <= 1000.0) hostTempo = bpm;
        if (std::isfinite(beat) && beat >= 0.0) hostBeat = beat;
        hostPlaying = k->transportPlaying() ? YES : hostPlaying;
    }
    d[kArpSIDStateKey_HostTempoHint] = @(std::isfinite(hostTempo) && hostTempo >= 1.0 && hostTempo <= 1000.0 ? hostTempo : 120.0);
    d[kArpSIDStateKey_HostBeatHint] = @(std::isfinite(hostBeat) && hostBeat >= 0.0 ? hostBeat : 0.0);
    d[kArpSIDStateKey_HostPlayingHint] = @(hostPlaying);
    d[kArpSIDStateKey_HostLoopingHint] = @(hostLooping);
    d[kArpSIDStateKey_HostLoopStartHint] = @(std::isfinite(hostLoopStart) && hostLoopStart >= 0.0 ? hostLoopStart : 0.0);
    d[kArpSIDStateKey_HostLoopEndHint] = @(std::isfinite(hostLoopEnd) && hostLoopEnd >= hostLoopStart ? hostLoopEnd : hostLoopStart);
    d[kArpSIDStateKey_HostTimeSigNumHint] = @(std::isfinite(hostTimeSigNum) && hostTimeSigNum >= 1.0 ? hostTimeSigNum : 4.0);
    d[kArpSIDStateKey_HostTimeSigDenHint] = @(hostTimeSigDen >= 1 ? hostTimeSigDen : 4);
    d[kArpSIDStateKey_HostMeasureDownbeatHint] = @(std::isfinite(hostMeasureDownbeat) && hostMeasureDownbeat >= 0.0 ? hostMeasureDownbeat : 0.0);
    return d;
}

- (void)_applyAuxStateSnapshot:(NSDictionary<NSString*, id>*)state {
    if (!state) return;
    for (const auto& binding : kArpSIDAuxParamBindings) {
        if (binding.paramID == ArpSID::kParamProgram || binding.paramID == ArpSID::kParamBankSlot) {
            continue; // patch identity is restored only from explicit pinned preset metadata
        }
        NSNumber* value = state[*binding.key];
        if ([value isKindOfClass:[NSNumber class]]) {
            [self _applyParameterValue:(float)std::clamp(value.floatValue, 0.0f, 1.0f) forID:binding.paramID updateTree:YES];
        }
    }

    // restore the selected SID chip as a discrete GUI selector and
    // immediately repair the legacy family mirror. This runs after root restore
    // and after the generic aux loop, so explicit project keys win over factory
    // patch defaults and stale Logic replay.
    NSNumber* chipIndexNumber228 = state[kArpSIDStateKey_SidChipSelectionIndex];
    NSNumber* chipNormNumber228 = state[kArpSIDStateKey_SidChipRevisionNorm];
    NSInteger chipIndex228 = -1;
    if ([chipIndexNumber228 isKindOfClass:[NSNumber class]]) {
        chipIndex228 = std::clamp((NSInteger)chipIndexNumber228.integerValue, (NSInteger)0, (NSInteger)3);
    } else if ([chipNormNumber228 isKindOfClass:[NSNumber class]]) {
        chipIndex228 = std::clamp((NSInteger)ArpSID::sidChipRevisionIndexFromNormalized(chipNormNumber228.floatValue),
                                  (NSInteger)0, (NSInteger)3);
    }
    if (chipIndex228 >= 0) {
        const float revisionNorm228 = ArpSID::sidChipRevisionIndexToNormalized((int)chipIndex228);
        const float familyNorm228 = chipIndex228 >= 3 ? 1.0f : 0.0f;
        [self _applyParameterValue:revisionNorm228 forID:ArpSID::kParamSidChipRevision updateTree:YES];
        [self _applyParameterValue:familyNorm228 forID:ArpSID::kParamSidModel updateTree:YES];
    }

    if (ArpSID::ArpSIDDSPKernel* k = [_adapter kernelPtr]) {
        NSNumber* hostTempoHint = state[kArpSIDStateKey_HostTempoHint];
        NSNumber* hostBeatHint = state[kArpSIDStateKey_HostBeatHint];
        NSNumber* hostPlayingHint = state[kArpSIDStateKey_HostPlayingHint];
        if ([hostTempoHint isKindOfClass:[NSNumber class]]) {
            const double bpm = hostTempoHint.doubleValue;
            if (std::isfinite(bpm) && bpm >= 1.0 && bpm <= 1000.0) k->setHostTempo(bpm);
        }
        if ([hostBeatHint isKindOfClass:[NSNumber class]]) {
            const double beat = hostBeatHint.doubleValue;
            if (std::isfinite(beat) && beat >= 0.0) k->setTransportBeatPosition(beat);
        }
        if ([hostPlayingHint isKindOfClass:[NSNumber class]]) {
            k->setTransportPlaying(hostPlayingHint.boolValue);
        }
    }
}

- (void)_syncParameterTreeFromStateRoot:(const ArpSID::SidStateRootV1&)root {
    float params[ArpSID::kNumParams];
    ArpSID::exportPersistentPresentationParamsFromStateRoot(root, params, ArpSID::kNumParams);
    for (int i = 0; i < ArpSID::kNumParams; ++i) {
        float fv = params[i];
        // state-root parameter mirrors are not preset identity authority.
        // Keep Program/BankSlot synchronized from sticky pinned preset metadata
        // only, otherwise project/host state with stale Program 0 can repaint
        // the visible patch at transport start.
        if (i == ArpSID::kParamBankSlot) fv = [self _pinnedBankSlotNormalizedValue];
        else if (i == ArpSID::kParamProgram) fv = [self _pinnedProgramNormalizedValue];
        AUParameter* param = [_parameterTree parameterWithAddress:(AUParameterAddress)i];
        if (param) [param setValue:fv originator:(__bridge void*)self];
    }
}

// ─── Restore path A: factory preset — apply PatchStaticState only ────────────
- (void)_applyFactoryPresetMetadataOnly:(AUAudioUnitPreset*)preset {
    if (!preset || preset.number < 0) return;
    (void)[self _setCurrentFactoryPresetMetadataOnlyForSlot:preset.number
                                          assumeGuardActive:[self _presetApplyGuardActive]
                                       updateAdapterMirrors:NO
                                          notifySuperPreset:NO];
}

// ─── Sticky preset metadata restore ───────────────────────────────────────────
- (void)_restorePinnedPresetMetadataFromDocumentDictionary:(NSDictionary<NSString*,id>*)state updateAdapterMirrors:(BOOL)updateMirrors {
    if (!state) return;
    NSNumber* selectedUser = state[@"ArpSIDDocumentSelectedUserPreset"];
    NSNumber* pinnedBank = state[@"ArpSIDDocumentPinnedBankSlot"];
    NSNumber* pinned = state[@"ArpSIDDocumentPinnedFactoryPreset"];
    NSNumber* current = state[@"ArpSIDDocumentCurrentPreset"];
    NSInteger targetNum = -1;
    if ([selectedUser isKindOfClass:[NSNumber class]] && selectedUser.integerValue >= 0) {
        targetNum = selectedUser.integerValue;
    } else if ([pinnedBank isKindOfClass:[NSNumber class]]) {
        targetNum = pinnedBank.integerValue;
    } else if ([pinned isKindOfClass:[NSNumber class]]) {
        targetNum = pinned.integerValue;
    } else if ([current isKindOfClass:[NSNumber class]]) {
        targetNum = current.integerValue;
    }
    if (targetNum < 0) return;
    NSInteger normalized = ArpSIDResolveFactorySlotForFlavor(_componentFlavor, targetNum);

    // ClassInfo/fullState replay at Logic transport start can carry stale
    // preset metadata. This helper runs before _restoreProjectState(); accepting
    // a stale incoming value here would overwrite sticky GUI authority and make
    // the later root guard compare against the wrong slot. Fail closed: preserve
    // sticky authority for every mismatch, including an explicitly selected slot 0.
    if (_selectedUserPresetNumber >= 0) {
        const NSInteger sticky = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)_selectedUserPresetNumber);
        if (normalized != sticky) {
            (void)[self _setCurrentFactoryPresetMetadataOnlyForSlot:sticky
                                              assumeGuardActive:YES
                                           updateAdapterMirrors:updateMirrors
                                              notifySuperPreset:NO];
            return;
        }
    }

    _selectedUserPresetNumber = normalized;
    _pinnedFactoryPresetNumber = normalized;
    _pinnedBankSlotNumber = normalized;
    (void)[self _setCurrentFactoryPresetMetadataOnlyForSlot:normalized
                                          assumeGuardActive:YES
                                       updateAdapterMirrors:updateMirrors
                                          notifySuperPreset:NO];
}

// ─── Restore path B: full project state from document ─────────────────────────
- (void)_restoreProjectState:(NSDictionary<NSString*,id>*)state allowRootPresetPin:(BOOL)allowRootPresetPin {
    if (!state) return;
    NSData* blob = state[kArpSIDStateKey_RootBlob];
    if (!blob || blob.length < sizeof(ArpSID::SidBinaryStateHeader) || blob.length > kArpSIDStateBlobCap) return;
    ArpSID::SidStateRootV1 root{};
    if (!ArpSID::decodeStateToRoot((const uint8_t*)blob.bytes, (size_t)blob.length, root,
                                  ArpSID::kProjectStateMagic)) {
        ARPSID_UI_LOG(@"[ArpSID AU] _restoreProjectState: schema decode failed; refusing invalid/corrupt state blob");
        return;
    }
    ArpSID::sanitizePersistentStateRootForSerialization(root);
    const NSInteger requestedSlotBeforePolicy = [self _bankSlotFromStateRoot:root];
    ArpSIDApplyComponentFlavorPolicyToStateRoot(_componentFlavor, root);
    const NSInteger resolvedFlavorSlot = [self _factorySlotFromStateRoot:root];
    if (ArpSIDShouldRebuildStateRootForFlavor(_componentFlavor, requestedSlotBeforePolicy, resolvedFlavorSlot)) {
        ArpSID::SidStateRootV1 rebuilt = ArpSID::makeFactoryPatchStateRootForSlot((int)ArpSIDResolveFactorySlotForFlavor(_componentFlavor, resolvedFlavorSlot >= 0 ? resolvedFlavorSlot : ArpSIDStartupFactorySlotForFlavor(_componentFlavor)));
        if (rebuilt.valid()) {
            root = std::move(rebuilt);
            ArpSIDApplyComponentFlavorPolicyToStateRoot(_componentFlavor, root);
        }
    }
    if (!root.valid()) {
        ARPSID_UI_LOG(@"[ArpSID AU] _restoreProjectState: decoded root invalid after sanitation; refusing state blob");
        return;
    }

    // Logic can replay ClassInfo/fullState/currentPreset at the Stop/Play
    // boundary. The replayed blob can be older than the user's current explicit
    // preset, including an explicitly selected slot 0.
    // Once a GUI/user sticky slot exists, reject every incoming root whose factory
    // slot does not match that authority before scheduling audio state.
    if (!allowRootPresetPin && _selectedUserPresetNumber >= 0) {
        const NSInteger sticky = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)_selectedUserPresetNumber);
        const NSInteger rootSlot = [self _factorySlotFromStateRoot:root];
        if (rootSlot != sticky) {
            (void)[self _setCurrentFactoryPresetMetadataOnlyForSlot:sticky
                                                  assumeGuardActive:[self _presetApplyGuardActive]
                                               updateAdapterMirrors:YES
                                                  notifySuperPreset:NO];
            return;
        }
    }

    // Use schedulePendingStateRestore instead of applyStateRootCanonical.
    // applyStateRootCanonical resets engine state (voice tokens, SID write queue, voice
    // policy) which may race with an in-progress render block. schedulePendingStateRestore
    // stores the root atomically and applies it at the top of the next processBlock().
    if (ArpSID::ArpSIDDSPKernel* k = [_adapter kernelPtr]) k->schedulePendingStateRestore(root);
    ++_restoredStateGeneration;
    if (allowRootPresetPin) {
        [self _pinFactoryPresetMetadataFromStateRoot:root];
    }
    [self _syncParameterTreeFromStateRoot:root];
}





- (BOOL)_buildFactoryStateRootForSlot:(NSInteger)slot outRoot:(ArpSID::SidStateRootV1&)outRoot resolvedSlot:(NSInteger*)resolvedSlot {
    const int normalizedSlot = (int)ArpSIDResolveFactorySlotForFlavor(_componentFlavor, slot);
    ArpSID::SidStateRootV1 root = ArpSID::makeFactoryPatchStateRootForSlot(normalizedSlot);
    if (!root.valid()) {
        const NSInteger startupSlot = ArpSIDStartupFactorySlotForFlavor(_componentFlavor);
        root = ArpSID::makeFactoryPatchStateRootForSlot((int)startupSlot);
        if (!root.valid()) return NO;
        if (resolvedSlot) *resolvedSlot = startupSlot;
    } else if (resolvedSlot) {
        *resolvedSlot = normalizedSlot;
    }
    ArpSIDApplyComponentFlavorPolicyToStateRoot(_componentFlavor, root);
    outRoot = std::move(root);
    return YES;
}

- (BOOL)applyUserFactoryPresetNumber:(NSInteger)slot {
    if (slot < 0) return NO;
    if (![self _tryBeginPresetApplyGuard]) return NO;
    BOOL ok = NO;
    @try {
        @autoreleasepool {
            NSInteger resolvedSlot = 0;
            ArpSID::SidStateRootV1 root{};
            if (![self _buildFactoryStateRootForSlot:slot outRoot:root resolvedSlot:&resolvedSlot]) {
                if (![self _buildFactoryStateRootForSlot:0 outRoot:root resolvedSlot:&resolvedSlot]) return NO;
            }
            ok = [self _setCurrentFactoryPresetMetadataOnlyForSlot:resolvedSlot
                                      assumeGuardActive:YES
                                   updateAdapterMirrors:YES
                                      notifySuperPreset:YES];
            if (!ok) return NO;
            _selectedUserPresetNumber = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)resolvedSlot);
            _pinnedFactoryPresetNumber = _selectedUserPresetNumber;
            _pinnedBankSlotNumber = _selectedUserPresetNumber;
            ++_restoredStateGeneration;
            if (ArpSID::ArpSIDDSPKernel* k = [_adapter kernelPtr]) { k->schedulePendingStateRestore(root); }
            [self _syncParameterTreeFromStateRoot:root];
            [self _applyAuxStateSnapshot:[self _auxStateSnapshotFromRoot:root]];
        }
    } @finally {
        [self _endPresetApplyGuard];
    }
    return ok;
}

- (void)setCurrentPreset:(AUAudioUnitPreset*)currentPreset {
    if (!currentPreset) return;
    if (currentPreset.number < 0) return;

    // _selectedUserPresetNumber is only set by explicit preset selection / pinned
    // metadata restore. Once it is >= 0, that slot is authoritative even if it is 0.
    const BOOL guardActive = [self _presetApplyGuardActive];
    if (!guardActive && _selectedUserPresetNumber >= 0) {
        const NSInteger sticky = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)_selectedUserPresetNumber);
        const NSInteger incoming = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)currentPreset.number);
        if (incoming == sticky) {
            [self _setCurrentFactoryPresetMetadataOnlyForSlot:sticky
                                      assumeGuardActive:NO
                                   updateAdapterMirrors:NO
                                      notifySuperPreset:NO];
            return;
        }
        if (incoming != sticky) {
            [self _setCurrentFactoryPresetMetadataOnlyForSlot:sticky
                                      assumeGuardActive:NO
                                   updateAdapterMirrors:YES
                                      notifySuperPreset:NO];
            return;
        }
    }

    // During actual transport motion, currentPreset writes are replay, not explicit
    // user intent. Stop-boundary writes are handled by the explicit-sticky gate above.
    if ([self _hostTransportIsMovingNonRealtime]) return;

    if (![self _tryBeginPresetApplyGuard]) return;

    @try {
        @autoreleasepool {
            try {
                NSInteger resolvedSlot = 0;
                ArpSID::SidStateRootV1 root{};
                if (![self _buildFactoryStateRootForSlot:currentPreset.number outRoot:root resolvedSlot:&resolvedSlot]) {
                    if (![self _buildFactoryStateRootForSlot:0 outRoot:root resolvedSlot:&resolvedSlot]) return;
                }

                (void)[self _setCurrentFactoryPresetMetadataOnlyForSlot:resolvedSlot
                                          assumeGuardActive:YES
                                       updateAdapterMirrors:YES
                                          notifySuperPreset:YES];
                _selectedUserPresetNumber = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)resolvedSlot);
                _pinnedFactoryPresetNumber = _selectedUserPresetNumber;
                _pinnedBankSlotNumber = _selectedUserPresetNumber;
                ++_restoredStateGeneration;

                if (ArpSID::ArpSIDDSPKernel* k = [_adapter kernelPtr])
                    k->schedulePendingStateRestore(root);
                [self _syncParameterTreeFromStateRoot:root];
                [self _applyAuxStateSnapshot:[self _auxStateSnapshotFromRoot:root]];
            } catch (const std::exception&) {
                NSInteger resolvedSlot = 0;
                ArpSID::SidStateRootV1 fallback{};
                if (![self _buildFactoryStateRootForSlot:0 outRoot:fallback resolvedSlot:&resolvedSlot]) return;
                (void)[self _setCurrentFactoryPresetMetadataOnlyForSlot:resolvedSlot
                                          assumeGuardActive:YES
                                       updateAdapterMirrors:YES
                                          notifySuperPreset:YES];
                _selectedUserPresetNumber = (NSInteger)ArpSID::normalizeFactoryPatchSlot((int)resolvedSlot);
                _pinnedFactoryPresetNumber = _selectedUserPresetNumber;
                _pinnedBankSlotNumber = _selectedUserPresetNumber;
                if (ArpSID::ArpSIDDSPKernel* k = [_adapter kernelPtr])
                    k->schedulePendingStateRestore(fallback);
                [self _syncParameterTreeFromStateRoot:fallback];
                [self _applyAuxStateSnapshot:[self _auxStateSnapshotFromRoot:fallback]];
            }
        }
    } @finally {
        [self _endPresetApplyGuard];
    }
}


// ─── User Presets ─────────────────────────────────────────────────────────────

- (BOOL)supportsUserPresets               { return NO; }
- (NSArray<AUAudioUnitPreset*>*)userPresets { return @[]; }

- (BOOL)saveUserPreset:(AUAudioUnitPreset*)p error:(NSError* __autoreleasing*)err {
    (void)p;
    if (err) *err = [NSError errorWithDomain:NSOSStatusErrorDomain code:unimpErr userInfo:nil];
    return NO;
}


- (BOOL)deleteUserPreset:(AUAudioUnitPreset*)p error:(NSError* __autoreleasing*)err {
    (void)p;
    if (err) *err = [NSError errorWithDomain:NSOSStatusErrorDomain code:unimpErr userInfo:nil];
    return NO;
}

// ─── State ────────────────────────────────────────────────────────────────────

// v551: extract and push SettingsPanelModel from a state dict into the adapter.
// Shared by setFullState: and setFullStateForDocument:.
- (void)_restoreSettingsFromStateDictionary:(NSDictionary<NSString*,id>*)state {
    if (!state) return;
    NSData* settingsData = state[@"ArpSIDSettings_v1"];
    if (![settingsData isKindOfClass:[NSData class]] || settingsData.length != 32) return;
    if (![_adapter respondsToSelector:@selector(setSettingsModel:)]) return;
    std::array<std::uint8_t, 32> bytes{};
    std::memcpy(bytes.data(), settingsData.bytes, 32);
    ArpSID::GUI::SettingsPanelModel settings = ArpSID::GUI::deserializeSettings(bytes);
    [_adapter setSettingsModel:&settings];
}

// v561: Restore MIX model from state dictionary. Shared by setFullState: and
// setFullStateForDocument:. Silently no-ops if the key is absent or the NSData
// length doesn't match sizeof(MixPanelModel).
- (void)_restoreMixStateFromStateDictionary:(NSDictionary<NSString*,id>*)state {
    if (!state) return;
    NSData* mixData = state[@"ArpSIDMix_v561"];
    if (![mixData isKindOfClass:[NSData class]] ||
        mixData.length != sizeof(ArpSID::GUI::MixPanelModel)) return;
    if (![_adapter respondsToSelector:@selector(setMixModel:)]) return;
    ArpSID::GUI::MixPanelModel model{};
    std::memcpy(&model, mixData.bytes, sizeof(ArpSID::GUI::MixPanelModel));
    // setMixModel: internally calls sanitizeMixModel.
    [_adapter setMixModel:&model];
}

// v560: Restore KIT state blob from state dictionary. Shared by setFullState:
// and setFullStateForDocument:. Silently no-ops if the key is absent or the
// NSData length doesn't match the pinned struct size.
- (void)_restoreKitStateFromStateDictionary:(NSDictionary<NSString*,id>*)state {
    if (!state) return;
    NSData* kitData = state[@"ArpSIDKit_v560"];
    if (![kitData isKindOfClass:[NSData class]]) return;
    if (![_adapter respondsToSelector:@selector(setKitStateBlob:)]) return;
    ArpSID::GUI::KitStateBlob blob{};
    if (!ArpSID::GUI::kitStateBlobDeserialize(kitData.bytes,
                                              static_cast<std::size_t>(kitData.length),
                                              blob)) return;
    // setKitStateBlob: internally calls kitStateBlobSanitize.
    [_adapter setKitStateBlob:&blob];
}

// v565/v596: Restore DIGI model and saved user-sample payloads from the
// state dictionary. Accepts the legacy 328-byte v1 model blob and the current
// 360-byte handle-aware blob. When both model and sample-bank are present,
// publish them through the combined adapter selector so the kernel never sees
// a transient restored bank with the old model, or the restored model with the
// old bank.
- (void)_restoreDigiStateAndSampleBankFromStateDictionary:(NSDictionary<NSString*,id>*)state {
    if (!state) return;

    NSData* digiData = state[@"ArpSIDDigi_v565"];
    NSData* bankData = state[@"ArpSIDDigiSamples_v596"];

    bool haveModel = false;
    ArpSID::GUI::DigiPanelModel model{};
    if ([digiData isKindOfClass:[NSData class]]) {
        haveModel = ArpSID::GUI::deserializeDigiPanelModel(digiData.bytes,
                                                           static_cast<std::size_t>(digiData.length),
                                                           model);
    }

    bool haveBank = false;
    std::unique_ptr<ArpSID::GUI::DigiSampleBankBlob> bank;
    if ([bankData isKindOfClass:[NSData class]] &&
        bankData.length == sizeof(ArpSID::GUI::DigiSampleBankBlob)) {
        bank = std::make_unique<ArpSID::GUI::DigiSampleBankBlob>();
        std::memcpy(bank.get(), bankData.bytes, sizeof(ArpSID::GUI::DigiSampleBankBlob));
        haveBank = true;
    }

    if (haveModel && haveBank) {
        if ([_adapter respondsToSelector:@selector(setDigiModel:sampleBank:)]) {
            // setDigiModel:sampleBank: internally sanitizes both blobs and publishes
            // exactly one matched snapshot to the realtime kernel.
            [_adapter setDigiModel:&model sampleBank:bank.get()];
        }
        // Do not fall back to two separate publishes when both blobs are present:
        // that would recreate the restored-model/restored-bank split-brain window
        // closed by the atomic restore path. Older adapters without the combined selector
        // must leave DIGI state unchanged rather than publish a mismatched pair.
        return;
    }

    // No partial DIGI restore: model and sample-bank are a matched persistence
    // unit. Restoring only one side can resurrect stale user-sample handles or
    // publish a bank whose handles no current model references. Leave the
    // current adapter DIGI state unchanged unless both blobs can be restored
    // atomically.
    (void)haveModel;
    (void)haveBank;
}

- (NSDictionary<NSString*,id>*)fullState {
    NSMutableDictionary* d = [NSMutableDictionary dictionaryWithDictionary:(([super fullState] != nil) ? [super fullState] : @{})];
    ArpSID::SidStateRootV1 root{};
    if (ArpSID::ArpSIDDSPKernel* k = [_adapter kernelPtr]) k->buildSerializableStateRootFromShadow(root);
    const size_t encCap = ArpSID::encodedSidStateRootBinarySize(root);
    NSData* blob = [NSData data];
    if (encCap > 0 && encCap <= _stateIoScratch.size()) {
        const size_t encLen = ArpSID::encodeSidStateRootBinary(root, _stateIoScratch.data(), encCap);
        if (encLen > 0) blob = [NSData dataWithBytes:_stateIoScratch.data() length:encLen];
    }
    d[kArpSIDStateKey_RootBlob] = blob;
    d[kArpSIDStateKey_Version] = @(kArpSIDStateVersion);
    // v551: embed the 32-byte settings model under a stable key.
    // The adapter stores whatever the ViewController last pushed; on a fresh
    // instance it holds the canonical defaults (schema v1).
    if ([_adapter respondsToSelector:@selector(getSettingsModel:)]) {
        ArpSID::GUI::SettingsPanelModel settings{};
        [_adapter getSettingsModel:&settings];
        std::array<std::uint8_t, 32> settingsBytes{};
        ArpSID::GUI::serializeSettings(settings, settingsBytes);
        d[@"ArpSIDSettings_v1"] = [NSData dataWithBytes:settingsBytes.data() length:32];
    }
    // v565/v596: embed a matched DIGI model+sample-bank snapshot.     // Do not serialize DIGI via separate getters: AU state must either contain a
    // pair-consistent model+bank snapshot or omit DIGI state rather than saving
    // a mismatched pair during concurrent GUI/import activity.
    if ([_adapter respondsToSelector:@selector(getDigiModel:sampleBank:)]) {
        ArpSID::GUI::DigiPanelModel digiModel{};
        auto bank = std::make_unique<ArpSID::GUI::DigiSampleBankBlob>();
        [_adapter getDigiModel:&digiModel sampleBank:bank.get()];
        d[@"ArpSIDDigi_v565"] = [NSData dataWithBytes:&digiModel
                                               length:sizeof(ArpSID::GUI::DigiPanelModel)];
        d[@"ArpSIDDigiSamples_v596"] = [NSData dataWithBytes:bank.get()
                                                      length:sizeof(ArpSID::GUI::DigiSampleBankBlob)];
    }
    // v561: embed the 1264-byte MIX model under a stable key.
    if ([_adapter respondsToSelector:@selector(getMixModel:)]) {
        ArpSID::GUI::MixPanelModel mixModel{};
        [_adapter getMixModel:&mixModel];
        d[@"ArpSIDMix_v561"] = [NSData dataWithBytes:&mixModel
                                              length:sizeof(ArpSID::GUI::MixPanelModel)];
    }
    // v560: embed the 1388-byte KIT state blob under a stable key.
    if ([_adapter respondsToSelector:@selector(getKitStateBlob:)]) {
        ArpSID::GUI::KitStateBlob kitBlob{};
        [_adapter getKitStateBlob:&kitBlob];
        d[@"ArpSIDKit_v560"] = [NSData dataWithBytes:&kitBlob
                                              length:sizeof(ArpSID::GUI::KitStateBlob)];
    }
    // Phase 4: serialization is read-only. Derive document preset metadata
    // from the canonical snapshot instead of mutating AU preset pins.
    const NSInteger resolvedPresetSlot = [self _serializationPresetSlot];
    d[@"ArpSIDDocumentSelectedUserPreset"] = @(resolvedPresetSlot);
    d[@"ArpSIDDocumentStateGeneration"] = @(_restoredStateGeneration);
    [d addEntriesFromDictionary:[self _auxStateSnapshotFromRoot:root]];
    if ([_adapter respondsToSelector:@selector(pureSid1Q1OutputMode)]) {
        d[kArpSIDStateKey_PureSid1Q1OutputMode] = @([(id)_adapter pureSid1Q1OutputMode] ? YES : NO);
    }
    return d;
}

- (void)setFullState:(NSDictionary<NSString*,id>*)state {
    if (![self _tryBeginPresetApplyGuard]) return;
    @try {
        if (!state) return;
        NSNumber* ver = state[kArpSIDStateKey_Version];
        if (ver && ver.integerValue > kArpSIDStateVersion) {
            ARPSID_UI_LOG(@"[ArpSID AU] setFullState: attempting forward-compatible restore from version %ld (current %d)",
                  (long)ver.integerValue, kArpSIDStateVersion);
        }
        [self _restorePinnedPresetMetadataFromDocumentDictionary:state updateAdapterMirrors:NO];
        [self _restoreProjectState:state allowRootPresetPin:NO];
        // fullState (not only fullStateForDocument) may carry forward-compatible
        // aux keys for first-class SID analogue options. Apply them after root
        // restore so explicit project keys win even when an older/corrupt root
        // blob is absent or lacks the newer parameter entries.
        [self _applyAuxStateSnapshot:state];
        NSNumber* pureSid1Q1 = state[kArpSIDStateKey_PureSid1Q1OutputMode];
        if ([pureSid1Q1 isKindOfClass:[NSNumber class]] && [_adapter respondsToSelector:@selector(setPureSid1Q1OutputMode:)]) {
            [(id)_adapter setPureSid1Q1OutputMode:pureSid1Q1.boolValue ? YES : NO];
        }
        // v551: restore settings model if present in state dict.
        [self _restoreSettingsFromStateDictionary:state];
        // v565/v596: restore DIGI model and saved sample payloads atomically.
        [self _restoreDigiStateAndSampleBankFromStateDictionary:state];
        // v561: restore MIX model if present in state dict.
        [self _restoreMixStateFromStateDictionary:state];
        // v560: restore KIT state blob if present in state dict.
        [self _restoreKitStateFromStateDictionary:state];
    } @finally {
        [self _endPresetApplyGuard];
    }
}

- (NSDictionary<NSString*,id>*)fullStateForDocument {
    NSMutableDictionary* d = [[self fullState] mutableCopy];
    ArpSID::SidStateRootV1 auxRoot{};
    if (ArpSID::ArpSIDDSPKernel* k = [_adapter kernelPtr]) k->buildSerializableStateRootFromShadow(auxRoot);
    // Phase 4: document serialization is read-only. Do not pin or change
    // the current preset while the host is merely asking for state.
    const NSInteger resolvedPresetSlot = [self _serializationPresetSlot];
    d[@"ArpSIDDocumentSelectedUserPreset"] = @(resolvedPresetSlot);
    d[@"ArpSIDDocumentStateGeneration"]    = @(_restoredStateGeneration);
    d[@"ArpSIDDocumentSampleRateHint"]     = @([self currentSampleRate]);
    [d addEntriesFromDictionary:[self _auxStateSnapshotFromRoot:auxRoot]];
    return d;
}

- (void)setFullStateForDocument:(NSDictionary<NSString*,id>*)state {
    if (![self _tryBeginPresetApplyGuard]) return;
    @try {
        // Sticky document/ClassInfo preset metadata is authority. Restore it
        // before root sync so stale root BankSlot/Program mirrors cannot repaint
        // slot 0 or GM piano metadata even transiently.
        [self _restorePinnedPresetMetadataFromDocumentDictionary:state updateAdapterMirrors:NO];
        [self _restoreProjectState:state allowRootPresetPin:NO];

        NSNumber* generation = state[@"ArpSIDDocumentStateGeneration"];
        if ([generation isKindOfClass:[NSNumber class]]) {
            _restoredStateGeneration = generation.integerValue;
        }

        [self _applyAuxStateSnapshot:state];
        NSNumber* pureSid1Q1 = state[kArpSIDStateKey_PureSid1Q1OutputMode];
        if ([pureSid1Q1 isKindOfClass:[NSNumber class]] && [_adapter respondsToSelector:@selector(setPureSid1Q1OutputMode:)]) {
            [(id)_adapter setPureSid1Q1OutputMode:pureSid1Q1.boolValue ? YES : NO];
        }
        // v551: restore settings model from document state.
        [self _restoreSettingsFromStateDictionary:state];
        // v565/v596: restore DIGI model and saved sample payloads atomically.
        [self _restoreDigiStateAndSampleBankFromStateDictionary:state];
        // v561: restore MIX model from document state.
        [self _restoreMixStateFromStateDictionary:state];
        // v560: restore KIT state blob from document state.
        [self _restoreKitStateFromStateDictionary:state];

        NSNumber* srHint = state[@"ArpSIDDocumentSampleRateHint"];
        if ([srHint isKindOfClass:[NSNumber class]]) {
            const double hintSR = srHint.doubleValue;
            const double curSR  = [self currentSampleRate];
            if (std::fabs(hintSR - curSR) > 1.0 && hintSR > 0.0)
                ARPSID_UI_LOG(@"[ArpSID] Document SR hint %.0f vs current %.0f — may need rate-match", hintSR, curSR);
        }
    } @finally {
        [self _endPresetApplyGuard];
    }
}

// ─── Latency / Tail ───────────────────────────────────────────────────────────

- (NSTimeInterval)latency  { return 0.005; } // fixed 5 ms product latency contract
- (NSTimeInterval)tailTime { return 8.0;   } // conservative long release/reverb tail

// ─── MIDI / Capability Surface ────────────────────────────────────────────────

- (BOOL)supportsMPE                    { return NO; }
- (NSInteger)virtualMIDICableCount     { return 1;  }
- (NSArray<NSString*>*)MIDIOutputNames { return @[]; }

// ─── Convenience ─────────────────────────────────────────────────────────────

- (float)getParameterValue:(int)pid {
    if (pid == ArpSID::kParamBankSlot) return [self _pinnedBankSlotNormalizedValue];
    if (pid == ArpSID::kParamProgram) return [self _pinnedProgramNormalizedValue];
    return [_adapter getParameterID:pid];
}
- (void)setParameterValue:(float)v forID:(int)pid { [self _applyParameterValue:v forID:pid updateTree:YES]; }
- (void)writeSIDRegister:(uint8_t)regIndex value:(uint8_t)value {
    if (regIndex > 0x18u) return; // writable SID hardware regs only; reject RO/system pseudo regs
    const int pid = (int)ArpSID::kParamSidRegD400 + (int)regIndex;
    if (pid < 0 || pid >= ArpSID::kNumParams) return;
    [self _applyParameterValue:((float)value / 255.0f) forID:pid updateTree:YES];
}
- (void)performC64ControlHubCommand:(NSInteger)command {
    [_adapter performC64ControlHubCommand:command];
}
- (BOOL)loadSidFileData:(NSData*)data {
    return [_adapter loadSidFileData:data];
}
- (BOOL)loadSidFileData:(NSData*)data subtune:(uint16_t)subtune {
    return [_adapter loadSidFileData:data subtune:subtune];
}
- (void)unloadSidFile {
    [_adapter unloadSidFile];
}
- (void)resetC64SidPlayerForEject {
    [_adapter resetC64SidPlayerForEject];
}
- (double)currentSampleRate {
    AUAudioUnitBus* outBus = (self.outputBusses.count > 0) ? self.outputBusses[0] : nil;
    AVAudioFormat* fmt = outBus ? outBus.format : _outputFormat;
    return ArpSIDSanitizeHostSampleRate(fmt ? fmt.sampleRate : kArpSIDDefaultSampleRate);
}


- (void)prepareStandaloneWithSampleRate:(double)sampleRate maxFrames:(AVAudioFrameCount)maxFrames {
    const double safeSR = ArpSIDSanitizeHostSampleRate(sampleRate);
    const AUAudioFrameCount safeMaxFrames = ArpSIDSanitizeHostMaxFrames(maxFrames > 0 ? maxFrames : kArpSIDDefaultMaxFrames);
    const BOOL wasSetup = _isSetup;
    const double oldSR = _outputFormat ? ArpSIDSanitizeHostSampleRate(_outputFormat.sampleRate) : 0.0;
    const AUAudioFrameCount oldMaxFrames = self.maximumFramesToRender;
    const BOOL configurationChanged = (!wasSetup) ||
        (oldSR < safeSR - 0.5 || oldSR > safeSR + 0.5) ||
        (oldMaxFrames != safeMaxFrames);

    self.maximumFramesToRender = safeMaxFrames;

    AVAudioFormat* standaloneFormat = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatFloat32
                                                                        sampleRate:safeSR
                                                                          channels:2
                                                                       interleaved:NO];
    if (standaloneFormat) {
        (void)[self _applyOutputBusFormatIfNeeded:standaloneFormat error:nil];
        _outputFormat = standaloneFormat;
    }

    const size_t allocFrames = ArpSIDScratchStableRenderFrames();
    // Audit #51: serialize standalone-path scratch resize through the same lock.
    [_renderScratchMutex lock];
    _renderScratchEpoch.fetch_add(1u, std::memory_order_acq_rel);
    ArpSIDEnsureFloatScratchCapacity(_renderPlanarL, allocFrames);
    ArpSIDEnsureFloatScratchCapacity(_renderPlanarR, allocFrames);
    _renderScratchEpoch.fetch_add(1u, std::memory_order_acq_rel);
    [_renderScratchMutex unlock];

    // this method is reached from AUv2 configuration sync, not only from
    // first allocation. It must therefore be idempotent. Re-running adapter
    // setup/allNotesOff/reset when sample-rate/maxFrames are unchanged destroys
    // the audible factory state at Logic transport-start after AUv2 has already
    // preserved it. Only a first setup or real render-configuration change may
    // perform the destructive adapter setup/reset sequence.
    if (configurationChanged) {
        [_adapter setupWithSampleRate:safeSR maxFrames:(int)safeMaxFrames];
        [_adapter allNotesOff];
        [_adapter reset];
        [self _publishHostTransportSnapshotNonRealtimeWithFrameCount:self.maximumFramesToRender];
    }
    _isSetup = YES;
    ARPSID_UI_LOG(@"[ArpSIDAU] prepareStandalone sampleRate=%f maxFrames=%u destructive=%d", safeSR, (unsigned)safeMaxFrames, configurationChanged ? 1 : 0);
}

- (id)debugAdapter {
    return _adapter;
}

// requestViewControllerWithCompletionHandler: is required for ALL AU formats on
// macOS 10.13+. Logic Pro (and GarageBand, MainStage) use this path for both
// AUv3 extensions AND AUv2 components bridged through AUAudioUnit. Guarding it
// behind ARPSID_AUV3_EXTENSION only left the AUv2 component with a permanently
// blank/missing UI in Logic.
//
// AUv3 path: use ArpSIDAUExtensionViewController (the .appex principal class,
// which holds the extension lifecycle boilerplate).
// AUv2 path: use ArpSIDViewController directly — no .appex bundle involved, so
// ArpSIDAUExtensionViewController is not compiled into the component.
#if defined(ARPSID_AUV3_EXTENSION) || defined(ARPSID_AUV2_COMPONENT)
- (void)requestViewControllerWithCompletionHandler:(void (^)(AUViewControllerBase * _Nullable viewController))completionHandler API_AVAILABLE(macos(10.13)) {
    if (!completionHandler) return;

    // v791: this request may enter off-main and bounce to the main queue. Do not
    // let the queued editor-construction block retain the AU object across host
    // teardown; strong-load only when the block actually runs.
    __weak ArpSIDAudioUnit* weakAudioUnit_v791 = self;
    void (^buildControllerOnMain)(void) = ^{
        @autoreleasepool {
            ArpSIDAudioUnit* strongAudioUnit_v791 = weakAudioUnit_v791;
            if (!strongAudioUnit_v791) {
                completionHandler(nil);
                return;
            }
#if defined(ARPSID_AUV3_EXTENSION)
            Class controllerClass = NSClassFromString(@"ArpSIDAUExtensionViewController");
            if (controllerClass == Nil) {
                completionHandler(nil);
                return;
            }
            id controller = [[controllerClass alloc] init];
            if (!controller) {
                completionHandler(nil);
                return;
            }
            if ([controller respondsToSelector:@selector(view)]) {
                (void)[controller view];
            }
            @try {
                if ([controller respondsToSelector:@selector(setExtensionAudioUnit:)]) {
                    [controller setValue:strongAudioUnit_v791 forKey:@"extensionAudioUnit"];
                } else {
                    completionHandler(nil);
                    return;
                }
            } @catch (NSException* __unused ex) {
                completionHandler(nil);
                return;
            }
            completionHandler(controller);
#else
            ArpSIDViewController* controller = [[ArpSIDViewController alloc] init];
            if (!controller) {
                completionHandler(nil);
                return;
            }
            (void)[controller view];
            @try {
                [controller connectAudioUnit:strongAudioUnit_v791];
            } @catch (NSException* __unused ex) {
                completionHandler(nil);
                return;
            }
            completionHandler(controller);
#endif
        }
    };

    if ([NSThread isMainThread]) buildControllerOnMain();
    else dispatch_async(dispatch_get_main_queue(), buildControllerOnMain);
}
#endif

@end
