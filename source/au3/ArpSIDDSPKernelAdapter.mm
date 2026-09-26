// ArpSIDDSPKernelAdapter.mm
// ArpSID AUv3 — ObjC++ Bridge Implementation
//
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT

#import "ArpSIDDSPKernelAdapter.h"
#import "ArpSIDDSPKernel.hpp"
#include "arpsid/gui/digi_record_limits.h"

#include <memory>
#include <vector>
#include <array>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cstring>
#include <mutex>
#include <mach/mach_time.h>

namespace {
static inline float ArpSIDSanitizeUnitFloat(float v, int pid = -1) noexcept {
    return ArpSID::sanitizeNormalizedParamValue(pid, v, ArpSID::defaultNormalizedParamValue(pid));
}

static inline double ArpSIDSanitizeFiniteDouble(double v, double fallback = 0.0) noexcept {
    return std::isfinite(v) ? v : fallback;
}

static inline int ArpSIDSanitizeRangeInt(int v, int lo, int hi, int fallback) noexcept {
    if (v < lo || v > hi) return fallback;
    return v;
}

static inline float ArpSIDSanitizeScopeSample(float v) noexcept {
    if (!std::isfinite(v)) return 0.0f;
    return std::clamp(v, -1.25f, 1.25f);
}

static inline float ArpSIDSanitizeBipolarFloat(float v, float fallback = 0.0f) noexcept {
    if (!std::isfinite(v)) return fallback;
    return std::clamp(v, -1.0f, 1.0f);
}

static constexpr int kArpSIDAdapterMinMaxFrames = 64;
static constexpr int kArpSIDAdapterHardMaxFrames = 65536;
static constexpr int kArpSIDAdapterScratchHeadroomFrames = 2048;

static inline size_t ArpSIDRoundUpAdapterFrames(size_t frames) noexcept {
    size_t v = std::max<size_t>((size_t)kArpSIDAdapterMinMaxFrames, frames);
    v -= 1u;
    v |= v >> 1u;
    v |= v >> 2u;
    v |= v >> 4u;
    v |= v >> 8u;
    v |= v >> 16u;
    if constexpr (sizeof(size_t) > sizeof(uint32_t)) v |= v >> 32u;
    return v + 1u;
}

static inline size_t ArpSIDAdapterScratchReserveFrames(int maxFrames) noexcept {
    const int safeMaxFrames = std::clamp(maxFrames > 0 ? maxFrames : kArpSIDAdapterMinMaxFrames,
                                         kArpSIDAdapterMinMaxFrames,
                                         kArpSIDAdapterHardMaxFrames);
    const size_t baseline = std::max<size_t>((size_t)safeMaxFrames * 2u + (size_t)kArpSIDAdapterScratchHeadroomFrames / 2u,
                                             (size_t)safeMaxFrames + (size_t)kArpSIDAdapterScratchHeadroomFrames * 2u);
    return ArpSIDRoundUpAdapterFrames(baseline);
}

static inline void ArpSIDEnsurePlanarScratchCapacity(std::vector<float>& scratch, size_t needed) {
    const size_t target = ArpSIDRoundUpAdapterFrames(needed);
    if (scratch.size() >= target) return;
    const size_t oldSize = scratch.size();
    scratch.resize(target);
    std::fill(scratch.begin() + static_cast<ptrdiff_t>(oldSize), scratch.end(), 0.0f);
}
} // namespace



namespace {
static inline bool ArpSIDBufferHasUsableBytes(const AudioBuffer& b, AVAudioFrameCount frames) noexcept {
    return b.mData != nullptr && b.mDataByteSize >= frames * sizeof(float) * std::max<UInt32>(1u, b.mNumberChannels);
}

static inline void ArpSIDZeroAudioBufferList(AudioBufferList* abl, AVAudioFrameCount frames) noexcept {
    if (!abl) return;
    for (UInt32 i = 0; i < abl->mNumberBuffers; ++i) {
        AudioBuffer& b = abl->mBuffers[i];
        if (!b.mData) continue;
        const size_t bytes = std::min<size_t>((size_t)b.mDataByteSize, (size_t)frames * sizeof(float) * std::max<UInt32>(1u, b.mNumberChannels));
        std::memset(b.mData, 0, bytes);
    }
}

struct DigiModelBankPair_v596 {
    ArpSID::GUI::DigiPanelModel model = ArpSID::GUI::makeDefaultDigiPanelModel();
    ArpSID::GUI::DigiSampleBankBlob bank{};

    DigiModelBankPair_v596() noexcept {
        ArpSID::GUI::resetDigiSampleBankBlob(bank);
    }
};
}

@implementation ArpSIDDSPKernelAdapter {
    std::unique_ptr<ArpSID::ArpSIDDSPKernel> _kernel;
    std::vector<float> _planarL;
    std::vector<float> _planarR;

    // v549 — AUv2/AUv3 host-layer diagnostic counter mirrors.
    // Written by storeAuv2DiagCounters: (render thread, relaxed store).
    // Read by readDiagnosticCounters: (GUI thread, acquire load).
    // Note: std::atomic does not support brace-initialization in ObjC ivar
    // blocks — initialized to 0 via default constructor (zero-initialized).
    std::atomic<uint64_t> _diagRenderEpoch;
    std::atomic<uint64_t> _diagNotifyCallbackViolationCount;
    std::atomic<uint64_t> _diagRenderDrainTimeoutCount;
    std::atomic<uint64_t> _diagSplitBrainDiagnosticCount;
    std::atomic<uint64_t> _diagScratchUnderCapacityCountAuv2;
    std::atomic<uint64_t> _diagPreNotifyFailureCount;
    std::atomic<uint64_t> _diagHostBufferScratchAttachCount;
    std::atomic<uint64_t> _diagBridgeDivertedRenderCount;
    std::atomic<uint64_t> _diagActivityMutexRtViolations;
    std::atomic<uint64_t> _diagStateMutexRtViolations;
    std::atomic<uint64_t> _diagPropListenerMutexRtViolations;
    std::atomic<uint64_t> _diagCloseWaitMutexRtViolations;
    std::atomic<uint64_t> _diagRenderScratchEpochAuv3;
    std::atomic<uint64_t> _diagScratchUnderCapacityCountAuv3;

    // v551 — SETTINGS persistence model. GUI/main thread only; no synchronization
    // needed. Written by setSettingsModel: (on connect/restore) and by ViewController
    // action handlers (on user change). Read by getSettingsModel: and serialized into
    // the AU state dict by fullState / wrapClassInfoDictionary.
    ArpSID::GUI::SettingsPanelModel _settingsModel;

    // v561 — MIX state model. GUI/main thread only. Written by setMixModel:
    // (on connect/restore and on every user edit in the MIX tab) and read by
    // getMixModel: for serialization into the AU state dict under @"ArpSIDMix_v561".
    ArpSID::GUI::MixPanelModel _mixModel_v561_;

    // v560 — KIT state blob. GUI/main thread only. Written by setKitStateBlob:
    // (on connect/restore and on every user edit) and read by getKitStateBlob:
    // for serialization into the AU state dict under @"ArpSIDKit_v560".
    ArpSID::GUI::KitStateBlob _kitStateBlob_v560_;

    // v565/v596 — DIGI model + sample bank are one mutex-protected authority.
    // The model can contain handles into the bank, so they must never be read,
    // written, sanitized, or published as independent halves.
    std::mutex _digiPairMutex_v596_;
    DigiModelBankPair_v596 _digiPair_v596_;
}


- (void)_processPlanarLeft:(float*)left right:(float*)right frameCount:(AVAudioFrameCount)frameCount timestamp:(const AudioTimeStamp*)timestamp {
    if (!_kernel || !left || frameCount == 0) return;
    if (timestamp) _kernel->setRenderHostTime(timestamp->mHostTime);
    // Route through processBlock() — same path as the AUv3 internalRenderBlock.
    // This ensures the canonical seqEngine_ sequencer is used for both the
    // standalone host and AUv3 (previously the standalone called process() which
    // used the parallel appendSequencerCanonicalEvents_ path, causing sequencer
    // state divergence between the two entry points).
    ArpSID::TransportState transport{};
    transport.frameCount = (int)frameCount;
    transport.sampleRate = _kernel->sampleRate();
    transport.bpm        = _kernel->hostTempo();
    transport.beatPosition = _kernel->hostBeatPosition();
    transport.isPlaying  = _kernel->transportPlaying();
    if (right) {
        float* chunkPtrs[2] = { left, right };
        // audit P0.5: pass an explicit channel count (stereo) so the kernel never
        // indexes outputs[1] past a caller-supplied array.
        _kernel->processBlock(chunkPtrs, 2, (int)frameCount, nullptr, 0, transport);
    } else {
        _kernel->processBlockMono(left, (int)frameCount, nullptr, 0, transport);
    }
}


- (BOOL)drainQueuedDrumBridgeSlotLoadNonRealtime {
    return _kernel ? _kernel->drainQueuedDrumBridgeSlotLoadNonRealtime() : NO;
}

- (void)_publishSanitizedGuiRealtimeModels_v150IncludeDigiSampleBank:(BOOL)includeDigiSampleBank {
    // Every non-DIGI adapter setter publishes the full GUI tuple. Keep the
    // DIGI half of that tuple repaired immediately before publication so even
    // compatibility read paths or corrupted/incomplete state cannot leak stale
    // user-sample handles through a later MIX/KIT/setup publish.
    std::lock_guard<std::mutex> lock(_digiPairMutex_v596_);
    ArpSID::GUI::sanitizeDigiSampleBankBlob(_digiPair_v596_.bank);
    ArpSID::GUI::sanitizeDigiPanelModel(_digiPair_v596_.model);
    ArpSID::GUI::digiRepairUserSampleReferences(_digiPair_v596_.model, _digiPair_v596_.bank);
    if (_kernel) {
        _kernel->publishGuiRealtimeModels(&_mixModel_v561_,
                                          &_kitStateBlob_v560_,
                                          &_digiPair_v596_.model,
                                          includeDigiSampleBank ? &_digiPair_v596_.bank : nullptr);
    }
}

- (void)_publishSanitizedGuiRealtimeModels_v150 {
    [self _publishSanitizedGuiRealtimeModels_v150IncludeDigiSampleBank:NO];
}

- (instancetype)init {
    self = [super init];
    if (self) {
        // Logic AUv2/AUv3 bridge stability: keep the DSP kernel in the known-good
        // single-owner shape. The simple 64-byte alignment theory was disproven;
        // the risky regression was shared kernel ownership plus async shared capture
        // during AU host probe/teardown.
        _kernel = std::make_unique<ArpSID::ArpSIDDSPKernel>();
        // v551: initialize settings to canonical defaults.
        _settingsModel = ArpSID::GUI::makeDefaultSettings();
        // v561: initialize MIX model to canonical defaults.
        _mixModel_v561_ = ArpSID::GUI::makeDefaultMixModel();
        // v560: initialize KIT state blob to canonical defaults.
        _kitStateBlob_v560_ = ArpSID::GUI::makeDefaultKitStateBlob();
        _digiPair_v596_.model = ArpSID::GUI::makeDefaultDigiPanelModel();
        ArpSID::GUI::resetDigiSampleBankBlob(_digiPair_v596_.bank);
        [self _publishSanitizedGuiRealtimeModels_v150IncludeDigiSampleBank:YES];
        // v549: explicitly zero-initialize diagnostic atomics.
        _diagRenderEpoch.store(0u, std::memory_order_relaxed);
        _diagNotifyCallbackViolationCount.store(0u, std::memory_order_relaxed);
        _diagRenderDrainTimeoutCount.store(0u, std::memory_order_relaxed);
        _diagSplitBrainDiagnosticCount.store(0u, std::memory_order_relaxed);
        _diagScratchUnderCapacityCountAuv2.store(0u, std::memory_order_relaxed);
        _diagPreNotifyFailureCount.store(0u, std::memory_order_relaxed);
        _diagHostBufferScratchAttachCount.store(0u, std::memory_order_relaxed);
        _diagBridgeDivertedRenderCount.store(0u, std::memory_order_relaxed);
        _diagActivityMutexRtViolations.store(0u, std::memory_order_relaxed);
        _diagStateMutexRtViolations.store(0u, std::memory_order_relaxed);
        _diagPropListenerMutexRtViolations.store(0u, std::memory_order_relaxed);
        _diagCloseWaitMutexRtViolations.store(0u, std::memory_order_relaxed);
        _diagRenderScratchEpochAuv3.store(0u, std::memory_order_relaxed);
        _diagScratchUnderCapacityCountAuv3.store(0u, std::memory_order_relaxed);
    }
    return self;
}

- (void)setupWithSampleRate:(double)sampleRate maxFrames:(int)maxFrames {
    const size_t scratchFrames = ArpSIDAdapterScratchReserveFrames(maxFrames);
    ArpSIDEnsurePlanarScratchCapacity(_planarL, scratchFrames);
    ArpSIDEnsurePlanarScratchCapacity(_planarR, scratchFrames);
    _kernel->requestAudioEngineMode(
        _settingsModel.audioEngineMode == ArpSID::GUI::AudioEngineMode::SingleSid3Voice ? 1u : 0u);
    _kernel->setup(sampleRate, maxFrames);
    [self _publishSanitizedGuiRealtimeModels_v150IncludeDigiSampleBank:YES];
}

- (void)setupPreservingAudioStateWithSampleRate:(double)sampleRate maxFrames:(int)maxFrames {
    if (!_kernel) return;
    std::array<float, ArpSID::kNumParams> snapshot{};
    for (int i = 0; i < ArpSID::kNumParams; ++i) snapshot[(size_t)i] = _kernel->getParameter(i);
    const int stickySlot = _kernel->stickyPresetDisplaySlot();
    const size_t scratchFrames = ArpSIDAdapterScratchReserveFrames(maxFrames);
    ArpSIDEnsurePlanarScratchCapacity(_planarL, scratchFrames);
    ArpSIDEnsurePlanarScratchCapacity(_planarR, scratchFrames);
    _kernel->requestAudioEngineMode(
        _settingsModel.audioEngineMode == ArpSID::GUI::AudioEngineMode::SingleSid3Voice ? 1u : 0u);
    _kernel->setup(sampleRate, maxFrames);
    _kernel->restoreHostParameterSnapshotImmediate(snapshot.data(), ArpSID::kNumParams);
    _kernel->setStickyPresetDisplaySlot(stickySlot);
    [self _publishSanitizedGuiRealtimeModels_v150IncludeDigiSampleBank:YES];
}

- (void)reset {
    _kernel->reset();
    std::fill(_planarL.begin(), _planarL.end(), 0.0f);
    std::fill(_planarR.begin(), _planarR.end(), 0.0f);
}

- (void)setParameterID:(int)paramID value:(float)value {
    if (!_kernel) return;
    const uint64_t hostTime = mach_absolute_time();
    _kernel->enqueueParameterIntent(paramID, value, -1, hostTime);
}

- (float)getParameterID:(int)paramID {
    return _kernel->getParameter(paramID);
}

- (void)setStickyPresetDisplaySlot:(NSInteger)slot {
    if (!_kernel) return;
    _kernel->setStickyPresetDisplaySlot((int)slot);
}

- (void)restoreParameterSnapshot:(const float*)values count:(int)count {
    if (!_kernel || !values || count <= 0) return;
    _kernel->restoreHostParameterSnapshotImmediate(values, count);
}

- (void)setComponentFlavor:(NSInteger)flavor {
    if (!_kernel) return;
    _kernel->setComponentFlavor((int)flavor);
}

- (void)performC64ControlHubCommand:(NSInteger)command {
    if (!_kernel) return;
    using Cmd = ArpSID::ArpSIDDSPKernel::C64ControlHubCommand;
    switch (command) {
        case 1: _kernel->performC64ControlHubCommand(Cmd::Boot); break;
        case 2: _kernel->performC64ControlHubCommand(Cmd::Start); break;
        case 3: _kernel->performC64ControlHubCommand(Cmd::Stop); break;
        case 4: _kernel->performC64ControlHubCommand(Cmd::Reset); break;
        case 5: _kernel->performC64ControlHubCommand(Cmd::LoadProjectionBootstrap); break;
        case 6: _kernel->setC64VicFast(true);  break;  // v838: VIC-II fast ON
        case 7: _kernel->setC64VicFast(false); break;  // v838: VIC-II fast OFF (accurate)
        case 8: _kernel->setC64CpuFast(true);  break;  // v839: 6510 fast ON
        case 9: _kernel->setC64CpuFast(false); break;  // v839: 6510 fast OFF (accurate)
        default: break;
    }
}

- (void)setC64VicFast:(BOOL)on { if (_kernel) _kernel->setC64VicFast(on ? true : false); }
- (BOOL)c64VicFast { return (_kernel && _kernel->c64VicFast()) ? YES : NO; }
- (void)setC64CpuFast:(BOOL)on { if (_kernel) _kernel->setC64CpuFast(on ? true : false); }
- (BOOL)c64CpuFast { return (_kernel && _kernel->c64CpuFast()) ? YES : NO; }

- (void)resetPreservingParameterSnapshot:(const float*)values
                                   count:(int)count
                             factorySlot:(NSInteger)factorySlot
                  hasExplicitFactorySlot:(BOOL)hasExplicitFactorySlot {
    if (!_kernel) return;
    _kernel->resetPreservingHostParameterSnapshot(values,
                                                  count,
                                                  (int)factorySlot,
                                                  hasExplicitFactorySlot ? true : false);
    std::fill(_planarL.begin(), _planarL.end(), 0.0f);
    std::fill(_planarR.begin(), _planarR.end(), 0.0f);
}

- (void)processWithOutputBufferList:(AudioBufferList*)outputBufferList
                         frameCount:(AVAudioFrameCount)frameCount
                          timestamp:(const AudioTimeStamp*)timestamp {
    if (!_kernel || !outputBufferList || frameCount == 0 || outputBufferList->mNumberBuffers == 0) return;

    const UInt32 nbuf = outputBufferList->mNumberBuffers;
    AudioBuffer& b0 = outputBufferList->mBuffers[0];
    if (!ArpSIDBufferHasUsableBytes(b0, frameCount)) {
        ArpSIDZeroAudioBufferList(outputBufferList, frameCount);
        return;
    }

    if (nbuf >= 2) {
        AudioBuffer& b1 = outputBufferList->mBuffers[1];
        if (!ArpSIDBufferHasUsableBytes(b1, frameCount)) {
            ArpSIDZeroAudioBufferList(outputBufferList, frameCount);
            return;
        }
        float* left = reinterpret_cast<float*>(b0.mData);
        float* right = reinterpret_cast<float*>(b1.mData);
        [self _processPlanarLeft:left right:right frameCount:frameCount timestamp:timestamp];
        for (UInt32 i = 2; i < nbuf; ++i) {
            if (outputBufferList->mBuffers[i].mData) {
                std::memset(outputBufferList->mBuffers[i].mData, 0, std::min<size_t>((size_t)outputBufferList->mBuffers[i].mDataByteSize, (size_t)frameCount * sizeof(float)));
            }
        }
        return;
    }

    const UInt32 chans = std::max<UInt32>(1u, b0.mNumberChannels);
    float* raw = reinterpret_cast<float*>(b0.mData);
    if (chans <= 1) {
        [self _processPlanarLeft:raw right:nullptr frameCount:frameCount timestamp:timestamp];
        return;
    }

    if (_planarL.size() < frameCount || _planarR.size() < frameCount) {
        ArpSIDZeroAudioBufferList(outputBufferList, frameCount);
        return;
    }
    std::fill(_planarL.begin(), _planarL.begin() + static_cast<ptrdiff_t>(frameCount), 0.0f);
    std::fill(_planarR.begin(), _planarR.begin() + static_cast<ptrdiff_t>(frameCount), 0.0f);
    [self _processPlanarLeft:_planarL.data() right:_planarR.data() frameCount:frameCount timestamp:timestamp];
    for (AVAudioFrameCount i = 0; i < frameCount; ++i) {
        raw[(size_t)i * chans + 0] = _planarL[i];
        raw[(size_t)i * chans + 1] = _planarR[i];
        for (UInt32 ch = 2; ch < chans; ++ch) raw[(size_t)i * chans + ch] = 0.0f;
    }
}

- (void)handleNoteOn:(uint8_t)note channel:(uint8_t)channel velocity:(uint8_t)velocity {
    if (!_kernel) return;
    const uint8_t status = (uint8_t)(0x90u | (channel & 0x0Fu));
    const uint64_t hostTime = mach_absolute_time();
    const uint8_t msg[3] = { status, note, velocity };
    _kernel->pushMidi(msg, 3, hostTime);
}

- (void)handleNoteOff:(uint8_t)note channel:(uint8_t)channel velocity:(uint8_t)velocity {
    if (!_kernel) return;
    const uint8_t status = (uint8_t)(0x80u | (channel & 0x0Fu));
    const uint64_t hostTime = mach_absolute_time();
    const uint8_t msg[3] = { status, note, velocity };
    _kernel->pushMidi(msg, 3, hostTime);
}

- (void)handleCC:(uint8_t)cc channel:(uint8_t)channel value:(uint8_t)value {
    if (!_kernel) return;
    const uint8_t status = (uint8_t)(0xB0u | (channel & 0x0Fu));
    const uint64_t hostTime = mach_absolute_time();
    const uint8_t msg[3] = { status, cc, value };
    _kernel->pushMidi(msg, 3, hostTime);
}

- (void)handlePitchBend:(uint16_t)bendValue channel:(uint8_t)channel {
    if (!_kernel) return;
    const int16_t signed14 = (int16_t)((int)bendValue - 8192);
    const uint16_t raw14 = (uint16_t)std::clamp((int)signed14 + 8192, 0, 16383);
    const uint8_t status = (uint8_t)(0xE0u | (channel & 0x0Fu));
    const uint8_t lo = (uint8_t)(raw14 & 0x7Fu);
    const uint8_t hi = (uint8_t)((raw14 >> 7) & 0x7Fu);
    const uint64_t hostTime = mach_absolute_time();
    const uint8_t msg[3] = { status, lo, hi };
    _kernel->pushMidi(msg, 3, hostTime);
}

- (void)handleAftertouch:(uint8_t)pressure channel:(uint8_t)channel {
    if (!_kernel) return;
    const uint8_t status = (uint8_t)(0xD0u | (channel & 0x0Fu));
    const uint64_t hostTime = mach_absolute_time();
    const uint8_t msg[2] = { status, pressure };
    _kernel->pushMidi(msg, 2, hostTime);
}

- (void)allNotesOff {
    if (!_kernel) return;
    const uint64_t hostTime = mach_absolute_time();
    const uint8_t msg[3] = { 0xB0u, 123u, 0u };
    _kernel->pushMidi(msg, 3, hostTime);
}

- (BOOL)enqueueMIDIBytes:(const uint8_t*)data length:(uint32_t)length hostTime:(uint64_t)hostTime {
    if (!_kernel || !data || length < 1 || length > 4) return NO;
    const uint8_t b0 = data[0];
    const uint8_t b1 = length > 1 ? data[1] : 0;
    const uint8_t b2 = length > 2 ? data[2] : 0;
    const uint8_t b3 = length > 3 ? data[3] : 0;
    const uint8_t bytes[4] = { b0, b1, b2, b3 };
    _kernel->pushMidi(bytes, (uint8_t)length, hostTime);
    return YES;
}

- (ArpSID::ArpSIDDSPKernel*)kernelPtr {
    return _kernel.get();
}

@end

// ─── Telemetry Category ───────────────────────────────────────────────────────

@implementation ArpSIDDSPKernelAdapter (Telemetry)

- (void)readTelemetry:(ArpSIDTelemetry*)out {
    [self readTelemetry:out includeScopes:YES includeC64Snapshot:YES];
}

- (void)readTelemetry:(ArpSIDTelemetry*)out includeScopes:(BOOL)includeScopes {
    [self readTelemetry:out includeScopes:includeScopes includeC64Snapshot:YES];
}

- (void)readTelemetry:(ArpSIDTelemetry*)out includeScopes:(BOOL)includeScopes includeC64Snapshot:(BOOL)includeC64Snapshot {
    if (!out) return;
    memset(out, 0, sizeof(ArpSIDTelemetry));
    out->lastMidiNote = -1;
    out->hostTempo    = 120.0;
    if (!_kernel) {
        return;
    }

    // Visible C64 panes are the demand signal for heavy C64/PSID snapshot work.
    // The audio thread keeps scalar meters fresh always, but only builds disassembly/
    // chip snapshots while a C64-facing GUI view is actively polling.
    if (includeC64Snapshot) {
        _kernel->noteC64TelemetryRequest();
    }
    if (includeScopes) {
        _kernel->notePresentationScopeRequest();
    }
    const auto t = _kernel->readTelemetry(includeScopes);
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
    out->digiGuiPadLastAccepted = t.digiGuiPadLastAccepted ? YES : NO;
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
    const bool hasC64Snapshot = includeC64Snapshot && _kernel->readC64Telemetry(c64Snapshot);
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
        std::strncpy(out->psidTitle, c64Snapshot.psidTitle.data(), sizeof(out->psidTitle) - 1u);
        out->psidTitle[sizeof(out->psidTitle) - 1u] = '\0';
        std::strncpy(out->psidAuthor, c64Snapshot.psidAuthor.data(), sizeof(out->psidAuthor) - 1u);
        out->psidAuthor[sizeof(out->psidAuthor) - 1u] = '\0';
        std::strncpy(out->psidReleased, c64Snapshot.psidReleased.data(), sizeof(out->psidReleased) - 1u);
        out->psidReleased[sizeof(out->psidReleased) - 1u] = '\0';
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
        _kernel->getPresentationFullScopeSnapshot(
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
            _kernel->readOscilloscope(out->mainOscScope, 512));
    }
}

- (int)readOscilloscope:(float*)buf maxSamples:(int)maxSamples {
    if (!buf || maxSamples <= 0 || !_kernel) return 0;
    const int n = _kernel->readOscilloscope(buf, maxSamples);
    for (int i = 0; i < n; ++i) {
        buf[i] = ArpSIDSanitizeScopeSample(buf[i]);
    }
    return n;
}

- (void)readVCOScope:(float*)vco0 vco1:(float*)vco1 vco2:(float*)vco2 {
    static const int kScope = 256;
    if (!_kernel) {
        if (vco0) memset(vco0, 0, kScope*sizeof(float));
        if (vco1) memset(vco1, 0, kScope*sizeof(float));
        if (vco2) memset(vco2, 0, kScope*sizeof(float));
        return;
    }
    _kernel->notePresentationScopeRequest();
    static thread_local float oscBuf[3][kScope];
    uint8_t activeMask = 0;
    uint32_t writePos = 0;
    _kernel->getPresentationOscScopeSnapshot(oscBuf, activeMask, writePos);
    const uint32_t wp = writePos & 255u;
    (void)activeMask;
    if (vco0) for (int i = 0; i < kScope; ++i) vco0[i] = ArpSIDSanitizeScopeSample(oscBuf[0][(wp + (uint32_t)i) & 255u]);
    if (vco1) for (int i = 0; i < kScope; ++i) vco1[i] = ArpSIDSanitizeScopeSample(oscBuf[1][(wp + (uint32_t)i) & 255u]);
    if (vco2) for (int i = 0; i < kScope; ++i) vco2[i] = ArpSIDSanitizeScopeSample(oscBuf[2][(wp + (uint32_t)i) & 255u]);
}

@end


@implementation ArpSIDDSPKernelAdapter (C64SidPlayerLoad)
- (BOOL)loadSidFileData:(NSData*)data {
    return [self loadSidFileData:data subtune:0u];
}

- (BOOL)loadSidFileData:(NSData*)data subtune:(uint16_t)subtune {
    if (!_kernel || !data || [data length] == 0) return NO;
    const void* bytes = [data bytes];
    const NSUInteger length = [data length];
    if (!bytes || length == 0 || length > (NSUInteger)std::numeric_limits<size_t>::max()) return NO;

    return _kernel->loadPsidData(bytes,
                                 static_cast<size_t>(length),
                                 static_cast<uint16_t>(subtune)) ? YES : NO;
}

- (void)unloadSidFile {
    if (_kernel) _kernel->unloadPsid();
}

- (void)resetC64SidPlayerForEject {
    if (_kernel) _kernel->resetC64SidPlayerForEject();
}
@end

// ─── DiagnosticCounters (v549) ────────────────────────────────────────────────

@implementation ArpSIDDSPKernelAdapter (DiagnosticCounters)

- (void)storeAuv2DiagCounters:(const ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot*)partial {
    if (!partial) return;
    // RT-safe: relaxed stores only — no allocation, no lock.
    _diagRenderEpoch.store(partial->renderEpoch, std::memory_order_relaxed);
    _diagNotifyCallbackViolationCount.store(partial->notifyCallbackViolationCount, std::memory_order_relaxed);
    _diagRenderDrainTimeoutCount.store(partial->renderDrainTimeoutCount, std::memory_order_relaxed);
    _diagSplitBrainDiagnosticCount.store(partial->splitBrainDiagnosticCount, std::memory_order_relaxed);
    _diagScratchUnderCapacityCountAuv2.store(partial->scratchUnderCapacityCountAuv2, std::memory_order_relaxed);
    _diagPreNotifyFailureCount.store(partial->preNotifyFailureCount, std::memory_order_relaxed);
    _diagHostBufferScratchAttachCount.store(partial->hostBufferScratchAttachCount, std::memory_order_relaxed);
    _diagBridgeDivertedRenderCount.store(partial->bridgeDivertedRenderCount, std::memory_order_relaxed);
    _diagActivityMutexRtViolations.store(partial->activityMutexRtViolations, std::memory_order_relaxed);
    _diagStateMutexRtViolations.store(partial->stateMutexRtViolations, std::memory_order_relaxed);
    _diagPropListenerMutexRtViolations.store(partial->propListenerMutexRtViolations, std::memory_order_relaxed);
    _diagCloseWaitMutexRtViolations.store(partial->closeWaitMutexRtViolations, std::memory_order_relaxed);
    _diagRenderScratchEpochAuv3.store(partial->renderScratchEpochAuv3, std::memory_order_relaxed);
    _diagScratchUnderCapacityCountAuv3.store(partial->scratchUnderCapacityCountAuv3, std::memory_order_relaxed);
}

- (void)storeAuv3ScratchEpoch:(uint64_t)epoch underCapacity:(uint64_t)underCapacity {
    _diagRenderScratchEpochAuv3.store(epoch, std::memory_order_relaxed);
    _diagScratchUnderCapacityCountAuv3.store(underCapacity, std::memory_order_relaxed);
}

- (ArpSID::GUI::Auv3ScratchCounterAtomicTargets)auv3ScratchCounterAtomicTargets {
    return { &_diagRenderScratchEpochAuv3, &_diagScratchUnderCapacityCountAuv3 };
}

- (ArpSID::GUI::Auv2DiagnosticCounterAtomicTargets)auv2DiagnosticCounterAtomicTargets {
    return { &_diagRenderEpoch,
             &_diagNotifyCallbackViolationCount,
             &_diagRenderDrainTimeoutCount,
             &_diagSplitBrainDiagnosticCount,
             &_diagScratchUnderCapacityCountAuv2,
             &_diagPreNotifyFailureCount,
             &_diagHostBufferScratchAttachCount,
             &_diagBridgeDivertedRenderCount,
             &_diagActivityMutexRtViolations,
             &_diagStateMutexRtViolations,
             &_diagPropListenerMutexRtViolations,
             &_diagCloseWaitMutexRtViolations };
}

- (void)readDiagnosticCounters:(ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot*)out {
    if (!out) return;
    *out = ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot{};
    // Pull kernel-accessible counters (audit #33, #36, #38, #45, #24, #25, #65, #66).
    if (_kernel) _kernel->collectDiagnosticCounters(*out);
    // Overlay AUv2/AUv3 host-layer counters stored by storeAuv2DiagCounters:.
    // acquire loads pair with the render-thread relaxed stores.
    out->renderEpoch                     = _diagRenderEpoch.load(std::memory_order_acquire);
    out->notifyCallbackViolationCount    = _diagNotifyCallbackViolationCount.load(std::memory_order_acquire);
    out->renderDrainTimeoutCount         = _diagRenderDrainTimeoutCount.load(std::memory_order_acquire);
    out->splitBrainDiagnosticCount       = _diagSplitBrainDiagnosticCount.load(std::memory_order_acquire);
    out->scratchUnderCapacityCountAuv2   = _diagScratchUnderCapacityCountAuv2.load(std::memory_order_acquire);
    out->preNotifyFailureCount           = _diagPreNotifyFailureCount.load(std::memory_order_acquire);
    out->hostBufferScratchAttachCount    = _diagHostBufferScratchAttachCount.load(std::memory_order_acquire);
    out->bridgeDivertedRenderCount       = _diagBridgeDivertedRenderCount.load(std::memory_order_acquire);
    out->activityMutexRtViolations       = _diagActivityMutexRtViolations.load(std::memory_order_acquire);
    out->stateMutexRtViolations          = _diagStateMutexRtViolations.load(std::memory_order_acquire);
    out->propListenerMutexRtViolations   = _diagPropListenerMutexRtViolations.load(std::memory_order_acquire);
    out->closeWaitMutexRtViolations      = _diagCloseWaitMutexRtViolations.load(std::memory_order_acquire);
    out->renderScratchEpochAuv3          = _diagRenderScratchEpochAuv3.load(std::memory_order_acquire);
    out->scratchUnderCapacityCountAuv3   = _diagScratchUnderCapacityCountAuv3.load(std::memory_order_acquire);
}

@end  // ArpSIDDSPKernelAdapter (DiagnosticCounters)

// ─── SIDCORE Panel Model Bridge (v550) ───────────────────────────────────────

// ─── Pure 1:1 SID engine output mode ───────────────────────────────
@implementation ArpSIDDSPKernelAdapter (PureSid1Q1Output)
- (void)setPureSid1Q1OutputMode:(BOOL)enabled {
    if (_kernel) _kernel->setPureSid1Q1OutputMode(enabled == YES);
}
- (BOOL)pureSid1Q1OutputMode {
    return _kernel ? (_kernel->pureSid1Q1OutputModeEnabled() ? YES : NO) : NO;
}
- (BOOL)startPureSid1Q1RecordCapture:(NSUInteger)maxFrames {
    if (!_kernel) return NO;
    // P0-1: propagate fail-closed result so callers learn the capture buffer was
    // NOT armed (e.g. a render-thread capture did not drain) instead of assuming
    // success and later reading an unarmed/empty buffer.
    return _kernel->startPureSid1Q1RecordCapture(static_cast<std::uint32_t>(std::min<NSUInteger>(maxFrames, ArpSID::GUI::kDigiRecordCaptureMaxFrames))) ? YES : NO;
}
- (BOOL)copyAndStopPureSid1Q1RecordCapture:(float*)dst
                                  maxFrames:(NSUInteger)maxFrames
                                 frameCount:(NSUInteger*)frameCount
                                 sampleRate:(double*)sampleRate
                              droppedFrames:(NSUInteger*)droppedFrames {
    if (!_kernel) return NO;
    std::uint32_t frames = 0u;
    std::uint32_t drops = 0u;
    double sr = 44100.0;
    const BOOL ok = _kernel->copyAndStopPureSid1Q1RecordCapture(dst,
                                                                static_cast<std::uint32_t>(std::min<NSUInteger>(maxFrames, ArpSID::GUI::kDigiRecordCaptureMaxFrames)),
                                                                &frames,
                                                                &sr,
                                                                &drops) ? YES : NO;
    if (frameCount) *frameCount = (NSUInteger)frames;
    if (sampleRate) *sampleRate = sr;
    if (droppedFrames) *droppedFrames = (NSUInteger)drops;
    return ok;
}
- (BOOL)pureSid1Q1RecordCaptureStatusFrames:(NSUInteger*)frameCount
                                  sampleRate:(double*)sampleRate
                               droppedFrames:(NSUInteger*)droppedFrames
                                        peak:(float*)peak
                                         rms:(float*)rms {
    if (!_kernel) return NO;
    std::uint32_t frames = 0u;
    std::uint32_t drops = 0u;
    double sr = 44100.0;
    float p = 0.0f;
    float r = 0.0f;
    _kernel->pureSid1Q1RecordCaptureStatus(&frames, &sr, &drops, &p, &r);
    if (frameCount) *frameCount = (NSUInteger)frames;
    if (sampleRate) *sampleRate = sr;
    if (droppedFrames) *droppedFrames = (NSUInteger)drops;
    if (peak) *peak = p;
    if (rms) *rms = r;
    return YES;
}
@end

@implementation ArpSIDDSPKernelAdapter (SidCorePanel)

- (void)setSidCorePanelModel:(ArpSID::GUI::SidCorePanelModel*)model {
    if (_kernel) _kernel->setSidCorePanelModel(model);
}

@end  // ArpSIDDSPKernelAdapter (SidCorePanel)

// ─── Settings Persistence Bridge (v551) ──────────────────────────────────────
@implementation ArpSIDDSPKernelAdapter (SettingsPersistence)

- (void)getSettingsModel:(ArpSID::GUI::SettingsPanelModel*)out {
    if (out) *out = _settingsModel;
}

- (void)setSettingsModel:(const ArpSID::GUI::SettingsPanelModel*)m {
    if (!m) return;
    _settingsModel = ArpSID::GUI::sanitizeSettings(*m);
    if (_kernel) {
        _kernel->requestAudioEngineMode(
            _settingsModel.audioEngineMode == ArpSID::GUI::AudioEngineMode::SingleSid3Voice ? 1u : 0u);
    }
}

@end  // ArpSIDDSPKernelAdapter (SettingsPersistence)

// ─── v560: KIT State Persistence ─────────────────────────────────────────────

@implementation ArpSIDDSPKernelAdapter (KitStatePersistence)

- (void)getKitStateBlob:(ArpSID::GUI::KitStateBlob*)out {
    if (out) *out = _kitStateBlob_v560_;
}

- (void)setKitStateBlob:(const ArpSID::GUI::KitStateBlob*)b {
    if (!b) return;
    _kitStateBlob_v560_ = *b;
    ArpSID::GUI::kitStateBlobSanitize(_kitStateBlob_v560_);
    [self _publishSanitizedGuiRealtimeModels_v150];
}

@end  // ArpSIDDSPKernelAdapter (KitStatePersistence)

// ─── v561: MIX State Persistence ─────────────────────────────────────────────

@implementation ArpSIDDSPKernelAdapter (MixStatePersistence)

- (void)getMixModel:(ArpSID::GUI::MixPanelModel*)out {
    if (out) *out = _mixModel_v561_;
}

- (void)setMixModel:(const ArpSID::GUI::MixPanelModel*)m {
    if (!m) return;
    _mixModel_v561_ = *m;
    ArpSID::GUI::sanitizeMixModel(_mixModel_v561_);
    [self _publishSanitizedGuiRealtimeModels_v150];
}

@end  // ArpSIDDSPKernelAdapter (MixStatePersistence)

// ─── v565: DIGI State Persistence ────────────────────────────────────────────

@implementation ArpSIDDSPKernelAdapter (DigiStatePersistence)

- (void)getDigiModel:(ArpSID::GUI::DigiPanelModel*)out {
    if (!out) return;
    // Legacy split getter: fail closed with an empty/default model. Returning
    // the live half of the DIGI pair is unsafe because old callers can combine
    // it with a separately-read bank across an atomic update boundary. New
    // callers must use getDigiModel:sampleBank:.
    *out = ArpSID::GUI::makeDefaultDigiPanelModel();
}

- (void)setDigiModel:(const ArpSID::GUI::DigiPanelModel*)m {
    (void)m;
    // Legacy split setter: fail closed. Even shadow-only mutation is unsafe
    // because a later MIX/KIT publish sends the full GUI realtime tuple and
    // would indirectly publish a half-updated DIGI pair. New callers must use
    // setDigiModel:sampleBank:.
}

- (void)getDigiSampleBank:(ArpSID::GUI::DigiSampleBankBlob*)out {
    if (!out) return;
    // Legacy split getter: fail closed with an empty/default bank. Returning
    // the live half of the DIGI pair is unsafe because old callers can combine
    // it with a separately-read model across an atomic update boundary. New
    // callers must use getDigiModel:sampleBank:.
    ArpSID::GUI::resetDigiSampleBankBlob(*out);
}


- (void)getDigiModel:(ArpSID::GUI::DigiPanelModel*)model
          sampleBank:(ArpSID::GUI::DigiSampleBankBlob*)bank {
    if (!model || !bank) {
        if (model) *model = ArpSID::GUI::makeDefaultDigiPanelModel();
        if (bank) ArpSID::GUI::resetDigiSampleBankBlob(*bank);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(_digiPairMutex_v596_);
        *model = _digiPair_v596_.model;
        *bank = _digiPair_v596_.bank;
    }
    ArpSID::GUI::sanitizeDigiSampleBankBlob(*bank);
    ArpSID::GUI::sanitizeDigiPanelModel(*model);
    ArpSID::GUI::digiRepairUserSampleReferences(*model, *bank);
}

- (void)setDigiSampleBank:(const ArpSID::GUI::DigiSampleBankBlob*)bank {
    (void)bank;
    // Legacy split setter: fail closed. Even shadow-only mutation is unsafe
    // because a later MIX/KIT publish sends the full GUI realtime tuple and
    // would indirectly publish a half-updated DIGI pair. New callers must use
    // setDigiModel:sampleBank:.
}

- (void)setDigiModel:(const ArpSID::GUI::DigiPanelModel*)model
          sampleBank:(const ArpSID::GUI::DigiSampleBankBlob*)bank {
    if (!model || !bank) return;
    {
        std::lock_guard<std::mutex> lock(_digiPairMutex_v596_);
        _digiPair_v596_.model = *model;
        _digiPair_v596_.bank = *bank;
        ArpSID::GUI::sanitizeDigiSampleBankBlob(_digiPair_v596_.bank);
        ArpSID::GUI::sanitizeDigiPanelModel(_digiPair_v596_.model);
        ArpSID::GUI::digiRepairUserSampleReferences(_digiPair_v596_.model, _digiPair_v596_.bank);
    }
    [self _publishSanitizedGuiRealtimeModels_v150IncludeDigiSampleBank:YES];
}

- (BOOL)setDigiUserSampleForSlot:(NSInteger)slot
                         samples:(const float*)samples
                      frameCount:(NSUInteger)frameCount
                      sampleRate:(double)sampleRate
                            name:(NSString*)name {
    if (slot < 0 || slot >= ArpSID::GUI::kDigiActiveSlotCount || !samples || frameCount == 0) {
        return NO;
    }
    const NSUInteger cappedFrames = std::min<NSUInteger>(
        frameCount, static_cast<NSUInteger>(std::numeric_limits<std::uint32_t>::max()));
    const std::uint32_t sampleRateHz = static_cast<std::uint32_t>(
        std::clamp<double>(std::isfinite(sampleRate) ? std::round(sampleRate) : 44100.0, 1000.0, 384000.0));
    const char* utf8 = name ? [name UTF8String] : nullptr;
    const std::uint32_t nameLen = utf8
        ? static_cast<std::uint32_t>(std::min<std::size_t>(std::strlen(utf8), std::numeric_limits<std::uint32_t>::max()))
        : 0u;
    const std::uint8_t slotIndex = static_cast<std::uint8_t>(slot);
    {
        std::lock_guard<std::mutex> lock(_digiPairMutex_v596_);
        const bool ok = ArpSID::GUI::digiLoadUserSampleFromFloatMono(_digiPair_v596_.bank,
                                                                      slotIndex,
                                                                      samples,
                                                                      static_cast<std::uint32_t>(cappedFrames),
                                                                      sampleRateHz,
                                                                      utf8,
                                                                      nameLen);
        if (!ok) return NO;

        const ArpSID::GUI::DigiUserSampleClip& clip = _digiPair_v596_.bank.clips[slotIndex];
        ArpSID::GUI::digiSetUserSampleSlot(_digiPair_v596_.model.slots[slotIndex], slotIndex, clip.handle);
        _digiPair_v596_.model.activeSlot = slotIndex;
        ArpSID::GUI::sanitizeDigiPanelModel(_digiPair_v596_.model);
        ArpSID::GUI::digiRepairUserSampleReferences(_digiPair_v596_.model, _digiPair_v596_.bank);
    }
    [self _publishSanitizedGuiRealtimeModels_v150IncludeDigiSampleBank:YES];
    return YES;
}

@end  // ArpSIDDSPKernelAdapter (DigiStatePersistence)

@implementation ArpSIDDSPKernelAdapter (DigiD418RuntimePolicyPass104)

- (void)setDigiD418RuntimeMode:(uint8_t)mode rateHz:(uint32_t)rateHz {
    if (!_kernel) return;
    _kernel->setDigiD418RuntimePolicy(mode, rateHz);
}

- (void)getDigiD418RuntimeMode:(uint8_t*)mode rateHz:(uint32_t*)rateHz {
    if (mode) *mode = _kernel ? _kernel->digiD418RuntimeMode() : 0u;
    if (rateHz) *rateHz = _kernel ? _kernel->digiD418RuntimeRateHz() : 8000u;
}

- (void)clearDigiD418RuntimeTelemetry {
    if (_kernel) _kernel->clearDigiD418RuntimeTelemetryForGui();
}

- (void)triggerDigiPadSlot:(uint8_t)slot velocity:(uint8_t)velocity {
    if (_kernel) _kernel->triggerDigiPadForGui(slot, velocity);
}

- (void)setDigiMidiPadRootNote:(uint8_t)rootNote channelFilter:(uint8_t)channelFilter {
    if (_kernel) _kernel->setDigiMidiPadMapping(rootNote, channelFilter);
}

- (void)getDigiMidiPadRootNote:(uint8_t*)rootNote channelFilter:(uint8_t*)channelFilter {
    if (rootNote) *rootNote = _kernel ? _kernel->digiMidiRootNote() : ArpSID::DigiD418StreamEngine::kDefaultMidiRootNote;
    if (channelFilter) *channelFilter = _kernel ? _kernel->digiMidiChannelFilter() : 16u;
}

@end
