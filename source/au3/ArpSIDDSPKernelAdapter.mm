// ArpSIDDSPKernelAdapter.mm
// ArpSID AUv3 — ObjC++ Bridge Implementation
//
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT

#import "ArpSIDDSPKernelAdapter.h"
#import "ArpSIDDSPKernel.hpp"
#include "ArpSIDKernelTelemetryFill.h"
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
static inline float ArpSIDSanitizeScopeSample(float v) noexcept {
    if (!std::isfinite(v)) return 0.0f;
    return std::clamp(v, -1.25f, 1.25f);
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
    if (!_kernel) {
        memset(out, 0, sizeof(ArpSIDTelemetry));
        out->lastMidiNote = -1;
        out->hostTempo    = 120.0;
        return;
    }
    ArpSID::TelemetryFill::fillTelemetryFromKernel(*_kernel, out, includeScopes ? true : false,
                                                   includeC64Snapshot ? true : false);
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
