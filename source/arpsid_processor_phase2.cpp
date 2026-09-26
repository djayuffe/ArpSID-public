#include <vector>
#include "arpsid/core/sid_runtime_host_ops.h"
#include "arpsid/core/sid_variant_ops.h"
#include "arpsid/core/sid_runtime_midi_ops.h"
#include "arpsid/core/sid_runtime_mod_ops.h"
// arpsid_processor_phase2.cpp
// ArpSID — VST3 Processor Implementation (Phase 2 / Phase 4)
//
// Implements all pure-virtual methods declared in arpsid_processor_phase2.h:
// initialize, terminate, setBusArrangements, activateBus, setupProcessing,
// setActive, process, canProcessSampleSize, setState, getState,
// setProcessing, queryInterface, plus all private helpers.
//
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT

#include "arpsid_processor_phase2.h"
#include "gui/arpsid_vst_cocoa_bridge.h"
#include "arpsid/core/sid_mod_matrix_types.h"
#include "arpsid/core/sid_runtime_execution.h"
#include "arpsid/core/sid_runtime_audio_kernel.h"
#include "arpsid/core/sid_runtime_fractional_render.h"
#include "arpsid/core/sid_runtime_register_ops.h"
#include "arpsid/core/sid_runtime_synth_register_scheduler.h"
#include "arpsid/core/sid_runtime_synth_performance.h"
#include "arpsid/core/sid_runtime_backend_projection.h"
#include "arpsid/core/sid_runtime_host_policy.h"
#include "arpsid/core/sid_runtime_host_block.h"
#include "arpsid/core/sid_runtime_render_surface.h"
#include "arpsid/core/sid_realtime_guard.h"
#include "arpsid/core/sid_midi_cc_mapping.h"
#include "arpsid/core/sid_chip.h"
#include "arpsid_log.h"
#include "arpsid/version.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "factory_patch_params.h"
#include "au3/ArpSIDStateSerializer.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <functional>

namespace ArpSID {

using namespace Steinberg;
using namespace Steinberg::Vst;

// ─── FUIDs (defined in plugin_ids.cpp) ──────────────────────────────────────
extern const FUID ProcessorUID;
extern const FUID ControllerUID;

static inline void sanitizeSynthModeQueuedShadowLocal_(
    ArpSID::SidRuntimeRegisterShadow& shadow,
    const float* paramValues,
    uint8_t sidSysByte) noexcept;

// ─── Constructor / Destructor ────────────────────────────────────────────────

ArpSIDProcessorPhase2::ArpSIDProcessorPhase2() {
    setControllerClass(ControllerUID);

    // Initialise all parameter values from defaults
    for (size_t i = 0; i < paramValues.size(); ++i)
        paramValues[i] = kParamInfos[i].defaultNorm;
    lastAppliedParamValues = paramValues;
    runtimeModel_.prepareRealtimeParameterStorage();
    runtimeExecutionOwner_ = std::make_unique<ArpSID::SidRuntimeExecutionOwner<ArpSIDProcessorPhase2>>(*this);
}

ArpSIDProcessorPhase2::~ArpSIDProcessorPhase2() = default;

// ─── IPluginBase ─────────────────────────────────────────────────────────────

tresult PLUGIN_API ArpSIDProcessorPhase2::initialize(FUnknown* context) {
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;

    // Audio buses: stereo output only (instrument)
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    // Event input bus (MIDI)
    addEventInput(STR16("MIDI In"), 1);

    // Create shared runtime engine bank
    engineBank_.create(44100.0);
    ArpSID::prewarmAllSidTables();
    ArpSID::arpsidSetActiveTelemetryProvider(this);

    ARPLOG("ArpSID %s processor initialized", ARPSID_PLUGIN_VERSION);
    return kResultOk;
}

tresult PLUGIN_API ArpSIDProcessorPhase2::terminate() {
    if (ArpSID::arpsidGetActiveTelemetryProvider() == this)
        ArpSID::arpsidSetActiveTelemetryProvider(nullptr);
    engineBank_.resetOwned();
    return AudioEffect::terminate();
}

// ─── queryInterface ──────────────────────────────────────────────────────────

tresult PLUGIN_API ArpSIDProcessorPhase2::queryInterface(const TUID _iid, void** obj) {
    return AudioEffect::queryInterface(_iid, obj);
}

// ─── IAudioProcessor ─────────────────────────────────────────────────────────

tresult PLUGIN_API ArpSIDProcessorPhase2::setBusArrangements(
        SpeakerArrangement* inputs,  int32 numIns,
        SpeakerArrangement* outputs, int32 numOuts) {
    // Instrument: zero audio inputs, stereo output
    if (numIns != 0) return kResultFalse;
    if (numOuts == 1 && (outputs[0] == SpeakerArr::kStereo ||
                         outputs[0] == SpeakerArr::kMono))
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API ArpSIDProcessorPhase2::activateBus(
        MediaType type, BusDirection dir, int32 index, TBool state) {
    return AudioEffect::activateBus(type, dir, index, state);
}

tresult PLUGIN_API ArpSIDProcessorPhase2::setupProcessing(ProcessSetup& setup) {
    tresult r = AudioEffect::setupProcessing(setup);
    if (r != kResultOk) return r;

    sampleRate    = setup.sampleRate;
    maxBlockSize  = std::max<Steinberg::int32>(1, setup.maxSamplesPerBlock);
    processingCapacity_ = std::max(maxBlockSize, kPhase2EmergencyBlockCapacity);

    if (!bitPerfectEngine_()) return kResultFalse;

    engineBank_.prepare(sampleRate);
    ArpSID::prewarmAllSidTables();

    // Preallocate scratch buffers
    tmpOutL.assign((size_t)processingCapacity_, 0.0f);
    tmpOutR.assign((size_t)processingCapacity_, 0.0f);
    runtimeFractionalAccumL_.assign((size_t)processingCapacity_, 0.0f);
    runtimeFractionalAccumR_.assign((size_t)processingCapacity_, 0.0f);
    runtimeFractionalWeight_.assign((size_t)processingCapacity_, 0u);
    runtimeFractionalActive_.assign((size_t)processingCapacity_, 0u);

    // Reverb
    reverb.init(sampleRate);

    // Limiter
    limiter.reset();
    limiter.setAttackMs(limiterAttackMs, sampleRate);
    limiter.setReleaseMs(limiterReleaseMs, sampleRate);
    hifiTranscendence_.reset();

    applyForensicConfig_();
    ARPLOG("setupProcessing SR=%.0f maxBlock=%d capacity=%d", sampleRate, maxBlockSize, processingCapacity_);
    synthVoicePolicy_().setSustainGateOffCallback([](void* ctx, int voiceIdx) noexcept {
        if (!ctx) return;
        auto* self = static_cast<ArpSIDProcessorPhase2*>(ctx);
        self->hardSynthModeVoiceOff_(voiceIdx, 0u, 0u, false);
    }, this);
    return kResultOk;
}

tresult PLUGIN_API ArpSIDProcessorPhase2::setActive(TBool state) {
    if (state) {
        if (!bitPerfectEngine_()) return kResultFalse;
        // Clear live MIDI state and canonical voice tokens before resetting
        // the engines. Without this, voice tokens, sustain state, pitch bend, and active
        // note identities from before deactivation survive and create zombie voices on
        // the next render block. Hosts that stop/start the plugin (e.g. during bypass or
        // transport-stop) will otherwise see stuck SID gates and identity mismatches.
        runtimeModel_.clearLiveMidiState();
        runtimeModel_.clearIdentityMirrors();
        runtimeModel_.clearTransientEvents();
        ArpSID::runtimeRenderHostResetEngines(*this);
        reverb.reset();
        limiter.reset();
        hifiTranscendence_.reset();
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API ArpSIDProcessorPhase2::canProcessSampleSize(int32 symbolicSampleSize) {
    // Support both 32-bit and 64-bit host buffers. The core render path remains
    // float-based; 64-bit buffers are rendered through the canonical float path
    // and converted on write-out at the host boundary.
    return (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API ArpSIDProcessorPhase2::setProcessing(TBool /*state*/) {
    return kResultOk;
}

// ─── State ───────────────────────────────────────────────────────────────────

tresult PLUGIN_API ArpSIDProcessorPhase2::setState(IBStream* state) {
    if (!state) return kResultFalse;
    IBStreamer s(state, kLittleEndian);
    uint32 version = 0;
    if (!s.readInt32u(version)) return kResultFalse;

    // Accept pre-v4 blobs via the migration decoder (decodeStateRootCanonical)
    // instead of hard-rejecting them. Projects saved by earlier versions (v1-v3 flat-param
    // format) are decoded by the compat path in ArpSIDStateSerializer.h.
    // The version field is still read to select the appropriate magic constant.
    const uint32_t stateMagic = (version >= 4u) ? ArpSID::kSidBinaryStateMagic
                                                 : ArpSID::kSidBinaryPatchStateMagic;

    // Read the blob as one canonical schema-root payload.
    int64 streamSize = 0;
    if (state->seek(0, IBStream::kIBSeekEnd, &streamSize) != kResultOk) return kResultFalse;
    state->seek(sizeof(uint32), IBStream::kIBSeekSet, nullptr);
    const size_t blobLen = (size_t)(streamSize - sizeof(uint32));
    if (blobLen < sizeof(ArpSID::SidBinaryStateHeader) || blobLen > stateIoScratch_.size()) return kResultFalse;
    int32 nRead = 0;
    if (state->read(stateIoScratch_.data(), (int32)blobLen, &nRead) != kResultOk || nRead <= 0) return kResultFalse;

    ArpSID::SidStateRootV1 root{};
    // Use the migration-capable decoder for all versions.
    if (!ArpSID::decodeStateToRoot(stateIoScratch_.data(), (size_t)nRead, root, stateMagic))
        return kResultFalse;
    ArpSID::sanitizePersistentStateRootForSerialization(root);
    if (!root.valid()) return kResultFalse;

    applyCanonicalStateRoot_(root);

    syncSerializableParamShadow();
    return kResultOk;
}

tresult PLUGIN_API ArpSIDProcessorPhase2::getState(IBStream* state) {
    if (!state) return kResultFalse;
    // Version 4: canonical schema-root only.
    IBStreamer s(state, kLittleEndian);
    s.writeInt32u(4u);

    buildSerializableStateRootFromShadow_(stateRootIoScratch_);
    const size_t encCap = ArpSID::encodedSidStateRootBinarySize(stateRootIoScratch_);
    if (encCap == 0 || encCap > stateIoScratch_.size()) return kResultFalse;
    const size_t encLen = ArpSID::encodeStateRoot(stateRootIoScratch_, ArpSID::kSidBinaryStateMagic, stateIoScratch_.data(), encCap);
    if (encLen == 0) return kResultFalse;
    return (state->write(stateIoScratch_.data(), (int32)encLen, nullptr) == kResultOk)
        ? kResultOk : kResultFalse;
}


void ArpSIDProcessorPhase2::updateSerializableStateTemplate_(const ArpSID::SidStateRootV1& root) noexcept {
    // Publish a sanitized state image through the ownership mailbox. This runs on
    // the non-RT path (called from applyCanonicalStateRoot_, which runs off the
    // audio thread in VST3), so the sanitization copy here is acceptable.
    ArpSID::SidStateRootV1 sanitized = root;
    ArpSID::sanitizePersistentStateRootForSerialization(sanitized);

    const size_t encCap = ArpSID::encodedSidStateRootBinarySize(sanitized);
    if (encCap > 0 && encCap <= kPendingBlobCapacity) {
        // Encode into the producer-owned buffer and publish ownership to the UI
        // consumer. The consumer's buffer is never touched here, so getState() can
        // decode without racing this writer.
        auto& slot = serializableBlobMailbox_.producerSlot();
        slot.len = ArpSID::encodeSidStateRootBinary(
            sanitized, slot.bytes.get(), kPendingBlobCapacity);
        serializableBlobMailbox_.publish();
    }
    // Never lock or copy the heavyweight SidStateRootV1 into serializableStateTemplate_
    // from this helper. getState() is the only place that takes
    // serializableStateTemplateMutex_ and drains the mailbox.
}

void ArpSIDProcessorPhase2::buildSerializableStateRootFromShadow_(ArpSID::SidStateRootV1& out) const noexcept {
    ArpSID::SidStateRootV1 templateRoot{};
    {
        ArpSID::sidRealtimeGuardForbidLock("VST serializableStateTemplateMutex_");
        std::lock_guard<std::mutex> lock(serializableStateTemplateMutex_);
        // Single-consumer drain (serialized by the mutex): take ownership of the
        // latest published blob, if any, and decode it from the consumer-owned
        // buffer. The producer never touches this buffer, so the decode can neither
        // race nor read torn bytes. If nothing new was published the cached
        // serializableStateTemplate_ is reused.
        if (auto* slot = serializableBlobMailbox_.tryConsume()) {
            if (slot->len > 0 && slot->len <= kPendingBlobCapacity) {
                ArpSID::SidStateRootV1 decoded{};
                if (ArpSID::decodeSidStateRootBinary(slot->bytes.get(), slot->len, decoded)) {
                    ArpSID::sanitizePersistentStateRootForSerialization(decoded);
                    if (decoded.valid())
                        serializableStateTemplate_ = std::move(decoded);
                }
            }
        }
        templateRoot = serializableStateTemplate_;
    }
    ArpSID::buildStateRootFromPresentationTemplate(templateRoot, out,
        [this](int i) noexcept -> float {
            float v = serializableParamShadow[(size_t)i].load(std::memory_order_relaxed);
            return std::isfinite(v) ? v : kParamInfos[(size_t)i].defaultNorm;
        });
}

void ArpSIDProcessorPhase2::applyCanonicalStateRoot_(const ArpSID::SidStateRootV1& root) noexcept {
    if (!root.valid()) return;

    ArpSID::SidStateRootV1 hydrated = root;
    ArpSID::sanitizePersistentStateRootForSerialization(hydrated);
    runtimeModel_.applyStateRoot(hydrated);
    updateSerializableStateTemplate_(hydrated);

    for (int i = 0; i < kNumParams; ++i) {
        float v = kParamInfos[(size_t)i].defaultNorm;
        if (!ArpSID::isRuntimeOnlyOrTransientParam(i))
            v = ArpSID::sidStateRootParamValue(hydrated, i);
        // v951: Phase2 state-root apply uses the shared param-specific sanitize
        // contract, not a generic 0..1 clamp. This keeps restored roots aligned
        // with live projected-parameter staging and AU3.
        const float clean = ArpSID::sanitizeNormalizedParamValue(
            i,
            v,
            ArpSID::defaultNormalizedParamValue(i));
        paramValues[(size_t)i] = clean;
        lastAppliedParamValues[(size_t)i] = clean;
        (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(i), clean);
    }

    // Reconcile persistent adapter-local policy through the same policy methods
    // as ordinary automation, without replaying transient actions such as Panic
    // or VirtualGate during project restore.
    ArpSID::runtimeReconcileStagedPersistentParameterPolicies(*this);
    // Backend projection now consumes the shared-core-owned voice policy/state from engineBank_.
    if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(true);
    syncSidSystemModelFromParams(true);
    resetRenderModeOutputNormalizer_();
    syncSerializableParamShadow();
}


// ─── Telemetry ───────────────────────────────────────────────────────────────

TBool PLUGIN_API ArpSIDProcessorPhase2::arpGetLatestSnapshot(MeterSnapshot& out) noexcept {
    return canonicalTelemetryRing_.consume(out) ? kResultTrue : kResultFalse;
}

TBool PLUGIN_API ArpSIDProcessorPhase2::arpGetTelemetryShadowSnapshot(MeterSnapshot& out) noexcept {
    return canonicalTelemetryRing_.loadLastConsumed(out) ? kResultTrue : kResultFalse;
}

TBool PLUGIN_API ArpSIDProcessorPhase2::arpGetLatestFullTelemetry(
        ArpSIDTelemetry& out, TBool includeScopes) noexcept {
    if (!fullTelemetryRing_.loadLatest(out)) return kResultFalse;
    if (!includeScopes) {
        std::memset(out.mainOscScope, 0, sizeof(out.mainOscScope));
        out.mainOscScopeCount = 0u;
        std::memset(out.voiceScope, 0, sizeof(out.voiceScope));
        std::memset(out.vcoScope, 0, sizeof(out.vcoScope));
        std::memset(out.filterScopeIn, 0, sizeof(out.filterScopeIn));
        std::memset(out.filterScopeOut, 0, sizeof(out.filterScopeOut));
        std::memset(out.digiScope, 0, sizeof(out.digiScope));
        std::memset(out.c64OpenBusScope, 0, sizeof(out.c64OpenBusScope));
        std::memset(out.c64SidBusScope, 0, sizeof(out.c64SidBusScope));
        std::memset(out.c64SidRegScope, 0, sizeof(out.c64SidRegScope));
        std::memset(out.c64SidValueScope, 0, sizeof(out.c64SidValueScope));
        std::memset(out.c64SidWritePulseScope, 0, sizeof(out.c64SidWritePulseScope));
        std::memset(out.c64Phi2Scope, 0, sizeof(out.c64Phi2Scope));
        std::memset(out.c64IrqDmaScope, 0, sizeof(out.c64IrqDmaScope));
        std::memset(out.c64ChipScope, 0, sizeof(out.c64ChipScope));
    }
    return kResultTrue;
}

bool ArpSIDProcessorPhase2::getLatestSnapshot(MeterSnapshot& out) const noexcept {
    return canonicalTelemetryRing_.loadLatest(out);
}

bool ArpSIDProcessorPhase2::getTelemetryShadowSnapshot(MeterSnapshot& out) const noexcept {
    return canonicalTelemetryRing_.loadLastConsumed(out);
}

bool ArpSIDProcessorPhase2::getLatestFullTelemetry(ArpSIDTelemetry& out) const noexcept {
    return fullTelemetryRing_.loadLatest(out);
}

void ArpSIDProcessorPhase2::publishLatestSnapshot_(const MeterSnapshot& snap) noexcept {
    canonicalTelemetryRing_.publish(snap);
}

// ─── Main render ─────────────────────────────────────────────────────────────


// ─── VST3 transport → canonical TransportState ───────────────────────────────
static ArpSID::TransportState buildVST3TransportState(
        const Steinberg::Vst::ProcessContext* ctx,
        double sampleRate, int frameCount) noexcept {
    ArpSID::TransportState ts{};
    ts.sampleRate  = sampleRate;
    ts.frameCount  = frameCount;
    if (!ctx) { ts.sanitize(); return ts; }
    if (ctx->state & Steinberg::Vst::ProcessContext::kTempoValid) {
        const double bpm = ctx->tempo;
        if (std::isfinite(bpm) && bpm > 1.0 && bpm < 1000.0) ts.bpm = bpm;
    }
    if (ctx->state & Steinberg::Vst::ProcessContext::kProjectTimeMusicValid) {
        const double bp = ctx->projectTimeMusic;
        if (std::isfinite(bp) && bp >= 0.0) ts.beatPosition = bp;
    }
    ts.isPlaying = (ctx->state & Steinberg::Vst::ProcessContext::kPlaying) != 0;
    ts.isLooping = (ctx->state & Steinberg::Vst::ProcessContext::kCycleActive) != 0;
    if (ts.isLooping &&
        (ctx->state & Steinberg::Vst::ProcessContext::kCycleValid)) {
        if (std::isfinite(ctx->cycleStartMusic) && ctx->cycleStartMusic >= 0.0)
            ts.loopStart = ctx->cycleStartMusic;
        if (std::isfinite(ctx->cycleEndMusic) && ctx->cycleEndMusic > ts.loopStart)
            ts.loopEnd = ctx->cycleEndMusic;
    }
    ts.sanitize();
    return ts;
}

void ArpSIDProcessorPhase2::setExternalCanonicalInputQueue(const ArpSID::SidTimedEventQueue& q, int frameCount) noexcept {
    externalCanonicalInputQueue_->reset();
    for (int i = 0; i < q.count; ++i) {
        ArpSID::SidTimedEvent ev = q.events[i];
        ev.sanitize(frameCount);
        if (!externalCanonicalInputQueue_->push(ev)) {
            break;
        }
    }
    externalCanonicalInputQueue_->sort();
}


void ArpSIDProcessorPhase2::kernelSetTransportPlaying(bool /*playing*/) noexcept {}
void ArpSIDProcessorPhase2::kernelSetHostTempo(float /*bpm*/) noexcept {}
void ArpSIDProcessorPhase2::kernelRewindTransport() noexcept { rewindArpPhase(); }
void ArpSIDProcessorPhase2::runtimeStageNormalizedParameterOnly(uint32_t target, float value) noexcept {
    if (!ArpSID::canonicalIsValidParamTarget(target, static_cast<uint32_t>(kNumParams))) return;
    const float clean = ArpSID::sanitizeNormalizedParamValue(static_cast<int>(target), value, ArpSID::defaultNormalizedParamValue(static_cast<int>(target)));
    paramValues[(size_t)static_cast<Steinberg::Vst::ParamID>(target)] = clean;
    lastAppliedParamValues[(size_t)static_cast<Steinberg::Vst::ParamID>(target)] = clean;
    // v943: match AU3 staging law. Direct/internal Phase2 parameter writes must
    // update the runtimeModel state-root or canonical MIDI routing can disagree
    // with the live VST render guards.
    (void)runtimeModel_.applyAutomationPoint(target, clean);
}

void ArpSIDProcessorPhase2::runtimeApplyNormalizedParameter(uint32_t target, float value) noexcept {
    if (!runtimeExecutionOwner_) return;
    runtimeExecutionOwner_->applyProjectedNormalizedParameter(target, value);
}

void ArpSIDProcessorPhase2::runtimeSetPitchBendRangeSemis(int channel, float semis) noexcept {
    const int ch = std::clamp(channel, 0, 15);
    const float clean = std::clamp(semis, 0.0f, 48.0f);
    runtimeModel_.setBendRangeSemis(ch, clean);
    if (bitPerfectEngine_()) bitPerfectEngine_()->setPitchBendRangeSemis(ch, clean);
}

void ArpSIDProcessorPhase2::runtimeImportSidRegisterNormalized(uint32_t target, float value) noexcept {
    if (target < (uint32_t)kParamSidRegD400 || target > (uint32_t)kParamSidRegD41D) return;
    const uint32_t reg = target - (uint32_t)kParamSidRegD400;
    if (reg >= 0x1Au) return;
    const float clean = ArpSID::sanitizeNormalizedParamValue(static_cast<int>(target), value, ArpSID::defaultNormalizedParamValue(static_cast<int>(target)));
    const uint8_t byteVal = static_cast<uint8_t>(std::clamp((int)std::lround(clean * 255.f), 0, 255));
    runtimeModel_.importRegisterWriteSnapshot(reg, byteVal);
    if (resolveTopLevelRenderMode_() == ArpSID::SidRuntimeRenderMode::SidRegister && reg < 0x19u) {
        sidRegEngine_().write(static_cast<uint8_t>(reg), byteVal);
    }
    if ((size_t)reg < sidQueuedShadow_().value.size()) {
        sidQueuedShadow_().value[(size_t)reg] = byteVal;
        sidQueuedShadow_().valid[(size_t)reg] = 1u;
        sidQueuedShadow_().sample[(size_t)reg] = 0u;
        sidQueuedShadow_().cycle[(size_t)reg] = 0u;
    }
}

void ArpSIDProcessorPhase2::kernelApplyNormalizedParameter(uint32_t target, float value) noexcept {
    const float v = ArpSID::sanitizeNormalizedParamValue(static_cast<int>(target), value, ArpSID::defaultNormalizedParamValue(static_cast<int>(target)));
    if (!ArpSID::runtimeApplyProjectedSpecialParameter(*this, target, v))
        runtimeApplyProjectedParameterBody(target, v);
}

void ArpSIDProcessorPhase2::runtimeApplyAllNotesOffPerformanceReset_(int channel, bool clearPhysicalPedals) noexcept {
    if (channel < 0) {
        runtimeModel_.clearLiveMidiState();
        if (clearPhysicalPedals) pedalState_().clear();
        runtimeModel_.clearIdentityMirrors();
        ArpSID::runtimeRenderHostAllNotesOff(*this);
    } else {
        runtimeModel_.clearLiveMidiChannel(channel, clearPhysicalPedals);
        if (clearPhysicalPedals) {
            pedalState_().setSustain(channel, false);
            pedalState_().setSostenuto(channel, false);
        }
        runtimeModel_.clearIdentityMirrors();
        ArpSID::runtimeRenderHostAllNotesOffChannel(*this, channel);
    }
}

void ArpSIDProcessorPhase2::runtimeApplyHardPanicPerformanceReset_() noexcept {
    runtimeModel_.clearTransientEvents();
    runtimeModel_.clearLiveMidiState();
    runtimeModel_.clearIdentityMirrors();
    ArpSID::runtimeRenderHostPanic(*this);
}

void ArpSIDProcessorPhase2::kernelPanic() noexcept { runtimeApplyHardPanicPerformanceReset_(); }
void ArpSIDProcessorPhase2::kernelAllNotesOff() noexcept { runtimeApplyAllNotesOffPerformanceReset_(-1, false); }
void ArpSIDProcessorPhase2::kernelAllNotesOffChannel(int channel) noexcept { runtimeApplyAllNotesOffPerformanceReset_(channel, false); }
void ArpSIDProcessorPhase2::kernelObserveNoteActivity(uint8_t note, float velocity) noexcept { lastNoteVelocity_ = velocity; lastMidiNote = note; midiActivityMeter = 1.0f; }
void ArpSIDProcessorPhase2::kernelTriggerDrumMidi(uint8_t note, float velocity) noexcept {
    ArpSID::canonicalTriggerDrumMidi(drSidEngine_(), note, velocity);
}
void ArpSIDProcessorPhase2::kernelArpNoteOn(uint8_t note, float velocity) noexcept { ArpSID::canonicalArpNoteOn(arpeggiator_(), note, velocity); }
void ArpSIDProcessorPhase2::kernelArpNoteOff(uint8_t note) noexcept { ArpSID::canonicalArpNoteOff(arpeggiator_(), note); }
void ArpSIDProcessorPhase2::kernelSynthNoteOn(const ArpSID::SidTimedEvent& e) noexcept {
    synthModeNoteOn(e.pitch, e.value, (Steinberg::int32)e.sample_offset, e.cycle_offset, e.channel, e.noteId);
}
void ArpSIDProcessorPhase2::kernelSynthNoteOff(const ArpSID::SidTimedEvent& e) noexcept {
    synthModeNoteOff(e.pitch, (Steinberg::int32)e.sample_offset, e.cycle_offset, e.channel, e.noteId);
}
void ArpSIDProcessorPhase2::kernelBitPerfectNoteOn(const ArpSID::SidTimedEvent& e) noexcept {
    if (resolveTopLevelRenderMode_() != ArpSID::SidRuntimeRenderMode::BitPerfect) return;
    ArpSID::canonicalBitPerfectNoteOn(bitPerfectEngine_(), e);
}
void ArpSIDProcessorPhase2::kernelBitPerfectNoteOff(const ArpSID::SidTimedEvent& e) noexcept {
    if (resolveTopLevelRenderMode_() != ArpSID::SidRuntimeRenderMode::BitPerfect) return;
    ArpSID::canonicalBitPerfectNoteOff(bitPerfectEngine_(), e);
}
void ArpSIDProcessorPhase2::kernelApplyPitchBend(const ArpSID::SidTimedEvent& e) noexcept { ArpSID::runtimeKernelDispatchPitchBend(*this, e); }
void ArpSIDProcessorPhase2::kernelApplyPolyPressure(const ArpSID::SidTimedEvent& e) noexcept { ArpSID::runtimeKernelDispatchPolyPressure(*this, e); }
void ArpSIDProcessorPhase2::kernelApplyChannelPressure(const ArpSID::SidTimedEvent& e) noexcept { ArpSID::runtimeKernelDispatchChannelPressure(*this, e); }
void ArpSIDProcessorPhase2::kernelApplyMidiCC(const ArpSID::SidTimedEvent& e) noexcept { ArpSID::runtimeKernelDispatchMidiCC(*this, e); }
void ArpSIDProcessorPhase2::kernelApplyVariantProfile(const ArpSID::SidVariantProfile& profile) noexcept {
    // Canonical runtime state was already mutated before concrete projection.
    const bool is8580 = (profile.family == ArpSID::SidFamily::MOS8580);
    const bool ntsc = ArpSID::sidVariantUsesNtscClock(profile);
    // kParamSidModel / kParamSidClockSystem are presentation/export mirrors only here.
    paramValues[(size_t)kParamSidModel] = is8580 ? 1.0f : 0.0f;
    paramValues[(size_t)kParamSidClockSystem] = ntsc ? 1.0f : 0.0f;
    serializableParamShadow[(size_t)kParamSidModel].store(paramValues[(size_t)kParamSidModel], std::memory_order_relaxed);
    serializableParamShadow[(size_t)kParamSidClockSystem].store(paramValues[(size_t)kParamSidClockSystem], std::memory_order_relaxed);
    if (bitPerfectEngine_()) {
        const SIDModel mdl = is8580 ? SIDModel::MOS8580 : SIDModel::MOS6581;
        bitPerfectEngine_()->setSIDModel(mdl);
        bitPerfectEngine_()->setClockFrequency(ArpSID::sidVariantClockHz(profile));
    }
    if (drSidEngine_()) {
        drSidEngine_()->setSIDModel(is8580 ? SIDModel::MOS8580 : SIDModel::MOS6581);
        drSidEngine_()->setClockFrequency(ArpSID::sidVariantClockHz(profile));
    }
    syncSidSystemModelFromParams(true);
}
void ArpSIDProcessorPhase2::kernelApplyProgramChange(uint8_t program) noexcept {
    // Incoming Program Change is GM/song metadata, not an ArpSID patch selector.
    (void)program;
}

void ArpSIDProcessorPhase2::runtimeHandleRenderModeTransition(ArpSID::SidRuntimeRenderMode oldMode, ArpSID::SidRuntimeRenderMode newMode) noexcept {
    ArpSID::runtimeRenderHostHandleModeTransition(*this, oldMode, newMode);
    performRenderModeTransition_(oldMode, newMode);
    // Re-apply Filter Drive after the local render mode changes. output_gain_bias
    // is stored in dB, so the DrSID 4.5 dB cap is projected immediately.
    runtimePolicySetFilterDrive(paramValues[(size_t)ArpSID::kParamFilterDrive]);
}

// isFactorySnapshotMetadataOrTransientParam_ is now inline in the header.

bool ArpSIDProcessorPhase2::applyFactoryPatchSnapshot_(int slot) noexcept {
    const ArpSID::SidStateRootV1 root = ArpSID::makeFactoryPatchStateRootForSlot(slot);
    if (!root.valid()) return false;
    applyCanonicalStateRoot_(root);
    return true;
}

static inline void assignApproxIntraSampleTiming_(ArpSID::SidTimedEvent& ev,
                                                  Steinberg::int32 sampleOffset,
                                                  uint32_t arrivalOrder,
                                                  uint8_t subphase,
                                                  double sampleRate,
                                                  double sidClockHz,
                                                  uint16_t explicitCycleOffset = ArpSID::kSidUnresolvedCycleOffset) noexcept {
    ArpSID::assignBestEffortIntraSampleTiming(ev,
                                              static_cast<int32_t>(sampleOffset),
                                              arrivalOrder,
                                              subphase,
                                              sampleRate,
                                              sidClockHz,
                                              explicitCycleOffset);
}

/* V66: VST3 no longer owns a duplicate SID-cycle stamp helper.
   Wrappers materialize sample offsets only; canonical runtime timing finalizes SID cycles. */

void ArpSIDProcessorPhase2::resetFractionalAccumulatorsForBlock_(int numSamples) noexcept {
    if (numSamples <= 0) return;
    const size_t n = static_cast<size_t>(numSamples);
    const size_t lrCount = std::min(n, std::min(runtimeFractionalAccumL_.size(), runtimeFractionalAccumR_.size()));
    std::fill_n(runtimeFractionalAccumL_.data(), lrCount, 0.0f);
    std::fill_n(runtimeFractionalAccumR_.data(), lrCount, 0.0f);
    const size_t wCount = std::min(lrCount, runtimeFractionalWeight_.size());
    std::fill_n(runtimeFractionalWeight_.data(), wCount, 0u);
    const size_t aCount = std::min(lrCount, runtimeFractionalActive_.size());
    std::fill_n(runtimeFractionalActive_.data(), aCount, uint8_t{0});
}

void ArpSIDProcessorPhase2::canonicalizeStructuralAuthorityAfterCanonicalBlock_(ArpSID::SidRuntimeRenderMode modeBeforeBlock) noexcept {
    const auto mode = resolveTopLevelRenderMode_();
    const bool structuralModeReturnToBitPerfect =
        modeBeforeBlock != mode &&
        mode == ArpSID::SidRuntimeRenderMode::BitPerfect &&
        (modeBeforeBlock == ArpSID::SidRuntimeRenderMode::SidRegister ||
         modeBeforeBlock == ArpSID::SidRuntimeRenderMode::DrSid);
    // v949: ARP is BitPerfect-only; SEQ is allowed in BitPerfect and
    // DrSID/SID808. Do not clear DrSID/SID808 drum sequencer authority just
    // because the resolved mode is DrSid. Same-block return to pure Classic
    // still clears both secondary authorities for the transition block.
    const bool modeBlocksArp = mode != ArpSID::SidRuntimeRenderMode::BitPerfect;
    const bool modeBlocksSeq = mode == ArpSID::SidRuntimeRenderMode::SidRegister;
    if (!modeBlocksArp && !modeBlocksSeq && !structuralModeReturnToBitPerfect) {
        return;
    }

    const bool hadArp = paramValues[(size_t)kParamArpEnable] > 0.5f ||
                        ArpSID::sidStateRootParamValue(runtimeModel_.stateRoot(), kParamArpEnable) > 0.5f;
    const bool hadSeq = paramValues[(size_t)kParamSeqEnable] > 0.5f ||
                        ArpSID::sidStateRootParamValue(runtimeModel_.stateRoot(), kParamSeqEnable) > 0.5f;
    if ((modeBlocksArp || structuralModeReturnToBitPerfect) && hadArp) {
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);
        if (arpeggiator_()) { arpeggiator_()->allNotesOff(); arpeggiator_()->setEnabled(false); }
        runtimeModel_.setArpActiveFlag(false);
    }
    if ((modeBlocksSeq || structuralModeReturnToBitPerfect) && hadSeq) {
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);
        runtimeModel_.setSeqLastNote(-1);
        runtimeModel_.setSeqSamplesUntilStep(-1.0);
        runtimeModel_.setSeqStep(0);
    }
}

void ArpSIDProcessorPhase2::processCanonicalBlockPhase2_(int numSamples,
                                                         float* outL,
                                                         float* outR,
                                                         int outChannels,
                                                         ArpSID::SidTimedEventQueue& canonicalQueue) noexcept {
    if (numSamples <= 0) return;
    resetFractionalAccumulatorsForBlock_(numSamples);

    struct SliceScope {
        ArpSIDProcessorPhase2& self;
        ~SliceScope() noexcept {
            self.runtimeSliceOutL_ = nullptr;
            self.runtimeSliceOutR_ = nullptr;
            self.runtimeSliceOutChannels_ = 0;
        }
    } scope{*this};

    runtimeSliceOutL_ = outL;
    runtimeSliceOutR_ = outR;
    runtimeSliceOutChannels_ = outChannels;

    // v906 Phase2/VST3 parity with AU3: glide writes are live render input,
    // not an offline helper, and future-block survivors must be rebased by the
    // same fractional consumer that owns block-local sample offsets.
    scheduleSynthModeGlideWrites_(numSamples);
    ArpSID::processCanonicalAudioBlockForTargetInto(runtimeModel_, *this, numSamples, canonicalQueue);
    ArpSID::runtimeEndFractionalBlock(*this, static_cast<uint32_t>(numSamples));
}


tresult PLUGIN_API ArpSIDProcessorPhase2::process(ProcessData& data) {
    auto zeroProcessOutputs = [&data]() noexcept {
        const int32 n = data.numSamples;
        if (n <= 0 || !data.outputs) return;
        for (int32 bus = 0; bus < data.numOutputs; ++bus) {
            AudioBusBuffers& outBus = data.outputs[bus];
            if (outBus.channelBuffers32) {
                for (int32 ch = 0; ch < outBus.numChannels; ++ch) {
                    float* buf = outBus.channelBuffers32[ch];
                    if (buf) std::memset(buf, 0, (size_t)n * sizeof(float));
                }
            }
            if (outBus.channelBuffers64) {
                for (int32 ch = 0; ch < outBus.numChannels; ++ch) {
                    double* buf = outBus.channelBuffers64[ch];
                    if (buf) std::fill(buf, buf + n, 0.0);
                }
            }
        }
    };
    if (!bitPerfectEngine_() || !runtimeExecutionOwner_ || !canonicalQueueScratch_) {
        zeroProcessOutputs();
        return kResultOk;
    }
    ArpSID::SidRealtimeScope arpsidRtScope_("ArpSIDProcessorPhase2::process");
    ArpSID::requireSidTablesPrewarmedForRealtime("ArpSIDProcessorPhase2::process missing SID table prewarm");

    const int32 numSamples = data.numSamples;
    if (numSamples <= 0) return kResultOk;

    // The host contract remains maxBlockSize, but bounded emergency headroom was
    // preallocated in setupProcessing(). Continue processing oversize transient
    // blocks so transport, delayed writes, FX and telemetry cannot freeze. Only a
    // block beyond the actual off-RT capacity is rejected safely.
    if (numSamples > processingCapacity_ ||
        (size_t)numSamples > tmpOutL.size() ||
        (size_t)numSamples > tmpOutR.size() ||
        (size_t)numSamples > runtimeFractionalAccumL_.size() ||
        (size_t)numSamples > runtimeFractionalAccumR_.size() ||
        (size_t)numSamples > runtimeFractionalWeight_.size() ||
        (size_t)numSamples > runtimeFractionalActive_.size()) {
        zeroProcessOutputs();
        canonicalQueueOverflowCount_ += 1u;
        return kResultOk;
    }

    currentProcessSamples_ = numSamples;

    // Capture the pre-event post-FX state. Canonical automation is replayed
    // against this snapshot at its exact sample offsets after core rendering.
    postFxBlockStart_ = ArpSID::sidCapturePostFxAutomationState(paramValues);

    // Build canonical TransportState for this block
    const ArpSID::TransportState vsT = buildVST3TransportState(
        data.processContext, sampleRate, (int)numSamples);
    if (vsT.isPlaying != runtimeModel_.seqHostWasPlaying()) {
        if (vsT.isPlaying) {
            // Transport Play is a hard runtime boundary, not a preset boundary.
            // Clear render-visible transient state while preserving current patch
            // and parameter authority.
            clearRuntimeStateForTransportStart_();
        }
        runtimeModel_.setSeqHostWasPlaying(vsT.isPlaying);
    }

    // Phase 3: canonical transport/tempo boundary law. VST no longer emits
    const ArpSID::SidCanonicalHostBlockState vstHostBlock =
        ArpSID::sidRuntimeBeginCanonicalHostBlock(runtimeModel_,
                                                  runtimeHostSurface(),
                                                  vsT,
                                                  sampleRate,
                                                  (int)numSamples,
                                                  0);
    (void)vstHostBlock;
    for (int i = 0; i < externalCanonicalInputQueue_->count; ++i)
        ArpSID::sidWrapperPushEvent(runtimeModel_, externalCanonicalInputQueue_->events[i], numSamples);
    externalCanonicalInputQueue_->reset();

    // 1. Process parameter changes from host
    processParameterChanges(data);

    // 2. Process MIDI events
    midiEvents.clear();
    processMIDIEvents(data);

    // 2b. Host context is already ingested exactly once through the canonical
    // VST ProcessContext -> TransportState -> SidCanonicalHostBlock path above.
    // Do not read ProcessContext again here: that created a second transport/tempo
    // authority that could disagree with buildVST3TransportState() sanitisation.
    runtimeModel_.setArpActiveFlag(ArpSID::sidEffectiveArpAuthorityFromLiveParams(paramValues));
    double telemetryHostTempo = runtimeModel_.hostTempoBpm();
    double telemetryHostBeat = runtimeModel_.hostProjectTimePPQ();
    bool telemetryHostPlaying = runtimeModel_.transportPlayingFlag();

    // 3. Sync tempo-linked controller rates and fire sequencer steps only after
    // host tempo/transport has been ingested for this block. LFO phase
    // advancement itself is slice-owned inside runtimeRenderCanonicalSlice().
    if (runtimeExecutionOwner_) runtimeExecutionOwner_->syncTempoLinkedControllers(numSamples);
    processLFOs(numSamples);
    processSequencer(numSamples);

    // 4. Finalize intra-sample timing and let the shared runtime own the block
    // execution ordering, dispatch timing, and slice-boundary render progression.
    const auto modeBeforeCanonicalBlock = resolveTopLevelRenderMode_();
    runtimeModel_.advanceRandomScope();
    finalizeIntraSampleTiming();
    applyModRoutesThisBlock_(); // apply live mod routes once per block, pre-render
    // Use the pre-allocated member to avoid a 1.31 MB stack allocation on the audio thread.
    ArpSID::SidTimedEventQueue& canonicalQueue = *canonicalQueueScratch_;
    canonicalQueue.reset();
    const bool hasOutputBus = (data.outputs && data.numOutputs > 0);
    const bool hasValidFloatOut = hasOutputBus && data.outputs[0].numChannels >= 1 &&
                                  data.outputs[0].channelBuffers32 && data.outputs[0].channelBuffers32[0];
    const bool hasValidDoubleOut = hasOutputBus && data.outputs[0].numChannels >= 1 &&
                                   data.outputs[0].channelBuffers64 && data.outputs[0].channelBuffers64[0];
    float telemetryPeakL = 0.0f;
    float telemetryPeakR = 0.0f;
    float telemetryRmsL = 0.0f;
    float telemetryRmsR = 0.0f;
    const float* telemetryOutL = nullptr;
    const float* telemetryOutR = nullptr;
    auto captureTelemetryMeters = [&](const float* l, const float* r) noexcept {
        if (!l || numSamples <= 0) return;
        double sumL = 0.0;
        double sumR = 0.0;
        float peakL = 0.0f;
        float peakR = 0.0f;
        const float* rr = r ? r : l;
        for (int i = 0; i < numSamples; ++i) {
            const float lv = std::isfinite(l[i]) ? l[i] : 0.0f;
            const float rv = std::isfinite(rr[i]) ? rr[i] : 0.0f;
            peakL = std::max(peakL, std::fabs(lv));
            peakR = std::max(peakR, std::fabs(rv));
            sumL += (double)lv * (double)lv;
            sumR += (double)rv * (double)rv;
        }
        telemetryPeakL = std::clamp(peakL, 0.0f, 1.0f);
        telemetryPeakR = std::clamp(peakR, 0.0f, 1.0f);
        telemetryRmsL = std::clamp((float)std::sqrt(sumL / (double)numSamples), 0.0f, 1.0f);
        telemetryRmsR = std::clamp((float)std::sqrt(sumR / (double)numSamples), 0.0f, 1.0f);
    };
    bool noOutputBusActive = false;
    if (hasValidFloatOut) {
        AudioBusBuffers& outBus = data.outputs[0];
        float* outL = outBus.channelBuffers32[0];
        float* outR = (outBus.numChannels >= 2 && outBus.channelBuffers32[1]) ? outBus.channelBuffers32[1] : outBus.channelBuffers32[0];
        std::memset(outL, 0, (size_t)numSamples * sizeof(float));
        if (outBus.numChannels >= 2 && outR && outR != outL) std::memset(outR, 0, (size_t)numSamples * sizeof(float));
        processCanonicalBlockPhase2_(numSamples, outL, outR, outBus.numChannels, canonicalQueue);
        applyRenderModeOutputNormalization_(outL, outR, numSamples, resolveTopLevelRenderMode_());
        // Apply post-FX (reverb + limiter) once after the canonical render pass.
        // Previously these were only applied inside renderAudio() which was never reached
        // for the standard 32-bit float output path, making reverb and limiter non-functional.
        applyOutputFX_(outL, outR, numSamples);
        captureTelemetryMeters(outL, outR);
        telemetryOutL = outL;
        telemetryOutR = outR;
    } else if (hasOutputBus && hasValidDoubleOut) {
        // Render once into float scratch, apply FX, then upcast to double output.
        // Previously renderAudio() was called after processCanonicalAudioBlockForTargetInto(),
        // causing all engine state (ADSR, oscillator phases, voice ages) to advance twice.
        AudioBusBuffers& outBus = data.outputs[0];
        double* outL64 = outBus.channelBuffers64[0];
        double* outR64 = (outBus.numChannels >= 2 && outBus.channelBuffers64[1]) ? outBus.channelBuffers64[1] : outBus.channelBuffers64[0];
        std::fill(outL64, outL64 + numSamples, 0.0);
        if (outBus.numChannels >= 2 && outR64 && outR64 != outL64) std::fill(outR64, outR64 + numSamples, 0.0);
        // Render into float scratch (avoids double-render)
        std::memset(tmpOutL.data(), 0, (size_t)numSamples * sizeof(float));
        std::memset(tmpOutR.data(), 0, (size_t)numSamples * sizeof(float));
        processCanonicalBlockPhase2_(numSamples, tmpOutL.data(), tmpOutR.data(), outBus.numChannels, canonicalQueue);
        if (outBus.numChannels >= 2) {
            applyRenderModeOutputNormalization_(tmpOutL.data(), tmpOutR.data(), numSamples, resolveTopLevelRenderMode_());
            applyOutputFX_(tmpOutL.data(), tmpOutR.data(), numSamples);
        } else {
            applyRenderModeOutputNormalization_(tmpOutL.data(), tmpOutL.data(), numSamples, resolveTopLevelRenderMode_());
            applyOutputFX_(tmpOutL.data(), tmpOutL.data(), numSamples);
        }
        // Upcast float scratch to double output
        for (int i = 0; i < numSamples; ++i) {
            outL64[i] = static_cast<double>(tmpOutL[i]);
            if (outBus.numChannels >= 2) outR64[i] = static_cast<double>(tmpOutR[i]);
        }
        captureTelemetryMeters(tmpOutL.data(), outBus.numChannels >= 2 ? tmpOutR.data() : tmpOutL.data());
        telemetryOutL = tmpOutL.data();
        telemetryOutR = outBus.numChannels >= 2 ? tmpOutR.data() : tmpOutL.data();
    } else {
        // No valid output bus — still run the SAME canonical/fractional block
        // contract into scratch buffers. This drains fractional finalizers,
        // advances slice-owned tempo controllers, schedules live glide writes,
        // and rebases future-block SID writes exactly like audible VST3/AU3.
        // v908: also run normalization + post-FX on scratch so HiFi/reverb/
        // limiter/dezipper state ages during host-inactive/no-output processing.
        // v909: capture meters/scope from the scratch render too — the engine
        // is actively advancing, so telemetry must not report silence. The
        // snapshot flags below make the no-output provenance explicit.
        noOutputBusActive = true;
        std::memset(tmpOutL.data(), 0, (size_t)numSamples * sizeof(float));
        std::memset(tmpOutR.data(), 0, (size_t)numSamples * sizeof(float));
        processCanonicalBlockPhase2_(numSamples, tmpOutL.data(), tmpOutR.data(), 2, canonicalQueue);
        applyRenderModeOutputNormalization_(tmpOutL.data(), tmpOutR.data(), numSamples, resolveTopLevelRenderMode_());
        applyOutputFX_(tmpOutL.data(), tmpOutR.data(), numSamples);
        captureTelemetryMeters(tmpOutL.data(), tmpOutR.data());
        telemetryOutL = tmpOutL.data();
        telemetryOutR = tmpOutR.data();
    }
    canonicalizeStructuralAuthorityAfterCanonicalBlock_(modeBeforeCanonicalBlock);
    appendCanonicalMidiEvents_(canonicalQueue);
    reconcileSynthModeUnheldVoices_();
    if (canonicalQueue.dropped > 0 || runtimeModel_.ingressDroppedCount() > 0) {
        canonicalQueueOverflowCount_ += canonicalQueue.dropped + runtimeModel_.ingressDroppedCount();
    }

    // 5. Publish meter snapshot
    {
        MeterSnapshot snap;
        snap.sampleRate    = (float)sampleRate;
        snap.bufferSize    = numSamples;
        snap.midiActivity  = midiActivityMeter > 0.01f;
        snap.lastMidiNote  = (lastMidiNote >= 0) ? (uint8_t)lastMidiNote : 0u;
        snap.peakL         = telemetryPeakL;
        snap.peakR         = telemetryPeakR;
        snap.rmsL          = telemetryRmsL;
        snap.rmsR          = telemetryRmsR;
        const ArpSID::SidRuntimeRenderMode telemetryMode = resolveTopLevelRenderMode_();
        if (telemetryMode == ArpSID::SidRuntimeRenderMode::DrSid && drSidEngine_()) {
            snap.activeVoices = drSidEngine_()->getActiveVoiceCount();
            const auto& regImage = drSidEngine_()->getRegisterImage();
            for (size_t i = 0; i < snap.sidRegs.size() && i < regImage.size(); ++i) snap.sidRegs[i] = regImage[i];
        } else if (telemetryMode == ArpSID::SidRuntimeRenderMode::SidRegister) {
            const auto& regImage = sidRegEngine_().getRegs().r;
            for (size_t i = 0; i < snap.sidRegs.size() && i < regImage.size(); ++i) snap.sidRegs[i] = regImage[i];
        } else if (bitPerfectEngine_()) {
            snap.activeVoices  = bitPerfectEngine_()->getActiveVoiceCount();
            const auto& regImage = bitPerfectEngine_()->getPrimaryRegImage();
            for (size_t i = 0; i < snap.sidRegs.size() && i < regImage.size(); ++i) snap.sidRegs[i] = regImage[i];
        }
        snap.arpPlaying    = arpeggiator_()->isEnabled();
        // Decay MIDI activity indicator
        midiActivityMeter  = std::max(0.0f, midiActivityMeter - (float)numSamples / (float)sampleRate);
        publishLatestSnapshot_(snap);

        ArpSIDTelemetry& full = fullTelemetryScratch_;
        full = ArpSIDTelemetry{};
        const uint64_t frameId = ++telemetryFrameCounter_;
        full.telemetryFrameId = frameId;
        full.scopeFrameId = frameId;
        full.digiFrameId = frameId;
        full.c64FrameId = frameId;
        full.mainOscFrameId = frameId;
        full.hostSampleStart = telemetryHostSampleCursor_;
        full.hostSampleEnd = telemetryHostSampleCursor_ + static_cast<uint64_t>(std::max(0, numSamples));
        telemetryHostSampleCursor_ = full.hostSampleEnd;
        full.peakL = snap.peakL;
        full.peakR = snap.peakR;
        full.rmsL = snap.rmsL;
        full.rmsR = snap.rmsR;
        full.activeVoices = snap.activeVoices;
        full.arpStep = arpeggiator_() ? arpeggiator_()->getCurrentStep() : 0;
        full.lastMidiNote = lastMidiNote;
        std::copy(snap.sidRegs.begin(), snap.sidRegs.end(), full.sidRegs);
        full.hostTempo = std::clamp(telemetryHostTempo, 1.0, 1000.0);
        full.hostBeat = std::max(0.0, telemetryHostBeat);
        full.hostPlaying = telemetryHostPlaying;
        full.renderMode = static_cast<int>(telemetryMode);
        full.sidModel = paramValues[(size_t)kParamSidModel] >= 0.5f ? 1 : 0;
        full.bankSlot = std::clamp(
            ArpSID::canonicalFactorySlotFromNormalizedBankSlot(paramValues[(size_t)kParamBankSlot]),
            0, ArpSID::kCanonicalFactoryPatchSlotMax);
        full.programNumber = full.bankSlot;
        full.voiceMode = std::clamp((int)std::lround(paramValues[(size_t)kParamVoiceMode] * 3.0f), 0, 3);
        full.synthMode = telemetryMode == ArpSID::SidRuntimeRenderMode::SidRegister;
        full.drSidMode = telemetryMode == ArpSID::SidRuntimeRenderMode::DrSid;
        full.psidActive = telemetryMode == ArpSID::SidRuntimeRenderMode::C64Psid;
        full.drSidPlaybackMode = drSidEngine_()
            ? static_cast<int>(drSidEngine_()->drSidPlaybackMode()) : 0;
        const bool fullEffectiveArpAuthority = arpeggiator_() &&
            ArpSID::sidEffectiveArpAuthorityFromLiveParams(paramValues);
        full.arpEnabled = fullEffectiveArpAuthority;
        full.arpFollowHost = runtimeModel_.followHostTempoArp();
        full.seqEnabled = ArpSID::sidEffectiveSeqAuthorityFromLiveParams(paramValues);
        full.seqFollowHost = runtimeModel_.followHostTempoSeq();
        full.seqStep = runtimeModel_.seqStep();
        full.seqTempoBpm = ArpSID_normToSeqTempoBpm(paramValues[(size_t)kParamSeqTempo]);

        full.drumVolume = paramValues[(size_t)kParamDrSidVolume];
        full.drumMachineModel = paramValues[(size_t)kParamDrSidMachineModel];
        full.drumAccentAmount = paramValues[(size_t)kParamDrSidAccentAmount];
        full.drumOutputDrive = paramValues[(size_t)kParamDrSidOutputDrive];
        full.drumHatMetal = paramValues[(size_t)kParamDrSidHatMetal];
        full.drumClapSpread = paramValues[(size_t)kParamDrSidClapSpread];
        full.drumKickTune = paramValues[(size_t)kParamDrSidKickTune];
        full.drumKickDecay = paramValues[(size_t)kParamDrSidKickDecay];
        full.drumSnareTone = paramValues[(size_t)kParamDrSidSnareTone];
        full.drumSnareSnap = paramValues[(size_t)kParamDrSidSnareSnap];
        full.drumHatTune = paramValues[(size_t)kParamDrSidHatTune];
        full.drumHatDecay = paramValues[(size_t)kParamDrSidHatDecay];
        full.drumCowbellTune = paramValues[(size_t)kParamDrSidCowbellTune];
        full.drumCowbellDecay = paramValues[(size_t)kParamDrSidCowbellDecay];
        full.drumTomTune = paramValues[(size_t)kParamDrSidTomTune];
        full.drumTomDecay = paramValues[(size_t)kParamDrSidTomDecay];
        if (drSidEngine_()) {
            drSidEngine_()->copyDrumLevels(full.drumLevel, 8);
            drSidEngine_()->copyVoiceLevels(full.drumVoiceLevel, 3);
            drSidEngine_()->copyGMDrumNoteLevels(full.gmDrumNoteLevel, 47);
            full.lastDrumNote = drSidEngine_()->lastGMDrumNote();
            full.lastDrumClass = drSidEngine_()->lastGMDrumClass();
            full.lastDrumVelocity = drSidEngine_()->lastGMDrumVelocity();
        } else {
            full.lastDrumNote = -1;
            full.lastDrumClass = 255;
        }

        if (telemetryOutL && numSamples > 0) {
            const int count = std::min(numSamples, 512);
            const int start = numSamples - count;
            const float* rr = telemetryOutR ? telemetryOutR : telemetryOutL;
            for (int i = 0; i < count; ++i) {
                const float mono = 0.5f * (telemetryOutL[start + i] + rr[start + i]);
                full.mainOscScope[i] = std::clamp(std::isfinite(mono) ? mono : 0.0f, -1.0f, 1.0f);
            }
            full.mainOscScopeCount = static_cast<uint32_t>(count);
        }

        float oscRaw[3][256]{};
        float filterRaw[2][256]{};
        float voiceRaw[8][256]{};
        uint8_t activeMask = 0u;
        uint32_t writePos = 0u;
        if (telemetryMode == ArpSID::SidRuntimeRenderMode::DrSid && drSidEngine_()) {
            drSidEngine_()->getScopeSnapshot(oscRaw, filterRaw, activeMask, writePos);
            for (int v = 0; v < 3; ++v)
                full.voiceEnvLevel[v] = drSidEngine_()->getVoiceEnvelopeLevel(v);
        } else if (telemetryMode == ArpSID::SidRuntimeRenderMode::SidRegister) {
            sidRegEngine_().getScopeSnapshot(oscRaw, filterRaw, activeMask, writePos);
            for (int v = 0; v < 3; ++v)
                full.voiceEnvLevel[v] = sidRegEngine_().getVoiceEnvelopeLevel(v);
        } else if (bitPerfectEngine_()) {
            bitPerfectEngine_()->getScopeSnapshot(voiceRaw, oscRaw, filterRaw, activeMask, writePos);
            for (int v = 0; v < 3; ++v)
                full.voiceEnvLevel[v] = bitPerfectEngine_()->getVoiceEnvelopeLevel(v);
        }
        const uint32_t scopeWp = writePos & 255u;
        for (int v = 0; v < 8; ++v)
            for (int i = 0; i < 256; ++i)
                full.voiceScope[v][i] = std::clamp(voiceRaw[v][(scopeWp + (uint32_t)i) & 255u], -1.0f, 1.0f);
        for (int v = 0; v < 3; ++v)
            for (int i = 0; i < 256; ++i)
                full.vcoScope[v][i] = std::clamp(oscRaw[v][(scopeWp + (uint32_t)i) & 255u], -1.0f, 1.0f);
        for (int i = 0; i < 256; ++i) {
            const uint32_t src = (scopeWp + (uint32_t)i) & 255u;
            full.filterScopeIn[i] = std::clamp(filterRaw[0][src], -1.0f, 1.0f);
            full.filterScopeOut[i] = std::clamp(filterRaw[1][src], -1.0f, 1.0f);
        }
        full.vcoActiveMask = static_cast<uint8_t>(activeMask & 0x07u);

        if (lfoBank_()) {
            for (int i = 0; i < 4; ++i) {
                full.lfoValue[i] = std::clamp(lfoBank_()->getLFO(i).getValue(), -1.0f, 1.0f);
                full.lfoPhase[i] = std::clamp((float)lfoBank_()->getLFO(i).getPhase(), 0.0f, 1.0f);
            }
        }
        full.lastNoteVelocity = std::clamp(lastNoteVelocity_, 0.0f, 1.0f);
        full.modWheelNorm = runtimeModel_.modWheelNorm();
        full.focusedPitchBend = std::clamp(runtimeModel_.focusedPitchBendNorm() * 2.0f - 1.0f, -1.0f, 1.0f);
        full.focusedChannelPressure =
            std::clamp(runtimeModel_.focusedChannelPressureBipolar() * 0.5f + 0.5f, 0.0f, 1.0f);
        full.focusedPolyPressure =
            std::clamp(runtimeModel_.focusedPolyPressureBipolar() * 0.5f + 0.5f, 0.0f, 1.0f);
        full.randomValue = runtimeModel_.randomBipolar();
        const ArpSIDForensicConfig forensic = buildForensicConfig_();
        full.forensicEnabled = forensic.enable;
        full.forensicIntensity = forensic.clampedIntensity();
        full.forensicClockJitter = forensic.clockJitterEnabled ? forensic.clockJitter : 0.0f;
        full.forensicSupplyRipple = forensic.supplyRippleEnabled ? forensic.supplyRipple : 0.0f;
        full.forensicThermalDrift = forensic.thermalDriftEnabled ? forensic.thermalDrift : 0.0f;
        full.forensicVoiceCrosstalk = forensic.voiceCrosstalkEnabled ? forensic.voiceCrosstalk : 0.0f;
        full.forensicExternalBleed = forensic.externalBleedEnabled ? forensic.externalBleed : 0.0f;
        full.forensicFilterOhmic = forensic.filterOhmic;
        full.forensicSystemNoise = forensic.systemNoise;
        full.forensicD418Asymmetry = forensic.d418Asymmetry;
        full.forensicEnvelopeTDM = forensic.envelopeTDM;
        full.forensicMotherboard = forensic.motherboard;
        full.forensicADCBleed = forensic.adcBleed;
        full.forensicBusCollision = forensic.busCollision;
        full.forensicPotInput = forensic.potInput;
        full.forensicDigifix8580 = forensic.digifix8580;
        full.forensicActivity = forensic.enable ? forensic.clampedIntensity() : 0.0f;
        full.hifiEnabled = lastHiFiConfig_.quality != ArpSID::HiFiQuality::PureEmulation;
        full.hifiQuality = static_cast<int>(lastHiFiConfig_.quality);
        full.hifiOversampling = std::clamp(lastHiFiConfig_.oversampling, 1,
                                           ArpSID::SidHiFiTranscendence::kMaxOversampling);
        full.hifiDryPeak = std::clamp(hifiTranscendence_.lastDryPeak(), 0.0f, 1.0f);
        full.hifiWetPeak = std::clamp(hifiTranscendence_.lastWetPeak(), 0.0f, 1.0f);
        full.hifiDeltaPeak = std::clamp(hifiTranscendence_.lastDeltaPeak(), 0.0f, 1.0f);
        full.hifiMonoCorrelation = std::clamp(hifiTranscendence_.lastMonoCorrelation(), -1.0f, 1.0f);
        full.hifiSafetyGain = std::clamp(hifiTranscendence_.lastSafetyGain(), 0.0f, 1.0f);
        full.c64PlatformEnabled = false;
        full.c64MirrorEnabled = false;
        full.c64PsidRuntimeActive = false;
        full.c64ProjectionOnly = false;
        full.c64BusScopeDecimation = 1u;
        full.c64BusScopeSourceLen = 128u;
        full.c64BusScopeSnapshotLen = 128u;
        // v909 telemetry-truth closure: Phase2/VST3 intentionally does not own
        // the AU3 C64 telemetry mirror (runtimeMirrorAppliedProjectionWrite is
        // a no-op here). Publish that explicitly so the UI can never imply
        // mirror accuracy, and flag no-output scratch-render provenance.
        full.projectionMirrorAvailable = false;
        full.projectionMirrorBackend = kArpSIDProjectionMirrorBackendUnavailablePhase2;
        full.noOutputBusActive = noOutputBusActive;
        full.telemetryRepresentsHostOutput = !noOutputBusActive;

        const uint64_t focusedToken = runtimeModel_.focusedVoiceToken();
        int tokenIndex = 0;
        int totalTokens = 0;
        runtimeModel_.forEachActiveTokenVoice([&](const auto& entry) noexcept {
            ++totalTokens;
            if (tokenIndex >= 8) return;
            ArpSIDTokenTelemetry& cell = full.tokens[tokenIndex++];
            cell.token = entry.token.token;
            cell.note = static_cast<uint8_t>(std::clamp<int>(entry.token.note, 0, 127));
            cell.channel = static_cast<uint8_t>(std::clamp<int>(entry.token.channel, 0, 15));
            cell.flags = 0u;
            if (entry.sustained) cell.flags |= 0x01u;
            if (entry.sostenutoLatched) cell.flags |= 0x02u;
            if (entry.token.token == focusedToken) cell.flags |= 0x04u;
            cell.velocity = std::clamp(entry.velocity, 0.0f, 1.0f);
            cell.polyPressure = std::clamp(entry.polyPressure, 0.0f, 1.0f);
        });
        full.activeTokenCount = static_cast<uint8_t>(std::min(tokenIndex, 8));
        full.totalActiveTokenCount = static_cast<uint8_t>(std::min(totalTokens, 64));
        fullTelemetryRing_.publish(full);
    }

    // setState: do NOT touch outputParameterChanges or numSamples here.
    // Those variables only exist in process() scope. Merely reload params and sync.
    ArpSID::sidRuntimeCommitCanonicalHostBlock(runtimeHostSurface(), vstHostBlock);
    syncSerializableParamShadow();
    pendingParams.clear();  // discard stale pending; process() will regenerate
    return kResultOk;
}

// ─── Parameter Changes ───────────────────────────────────────────────────────

void ArpSIDProcessorPhase2::processParameterChanges(ProcessData& data) {
    if (!data.inputParameterChanges) return;
    const int32 count = data.inputParameterChanges->getParameterCount();
    for (int32 pi = 0; pi < count; ++pi) {
        IParamValueQueue* queue = data.inputParameterChanges->getParameterData(pi);
        if (!queue) continue;
        const Steinberg::Vst::ParamID id = queue->getParameterId();
        if (id >= (Steinberg::Vst::ParamID)kNumParams) continue;
        // kParamProgram/kParamBankSlot are preset metadata/readback mirrors,
        // not render-thread factory-patch commands. Explicit factory loads enter
        // only through applyFactoryPatchSnapshot_()/applyCanonicalStateRoot_().
        if (id == (Steinberg::Vst::ParamID)kParamProgram ||
            id == (Steinberg::Vst::ParamID)kParamBankSlot) {
            continue;
        }

        // offset and value removed: getPoint uses sampleOff/ptVal instead
        const int32 n = queue->getPointCount();
        // Fix finding 17: read all automation points, apply in sample order.
        // Previously only the last point was read — full within-block automation
        // curves were collapsed to the final value. Now all points are scheduled.
        for (int32 pt = 0; pt < n; ++pt) {
            int32 sampleOff = 0;
            ParamValue ptVal = (pt == 0) ? (ParamValue)paramValues[(size_t)id] : 0.0;
            if (queue->getPoint(pt, sampleOff, ptVal) != kResultOk) continue;
            const float v = std::clamp((float)ptVal, 0.0f, 1.0f);
            {
                SidTimedEvent tev{};
                tev.type = SidTimedEventType::AutomationPoint;
                assignApproxIntraSampleTiming_(tev, sampleOff, 0u /*arrival_order set by merge*/, 1u, sampleRate, currentSidClockHz());
                tev.target = (uint32_t)id;
                tev.value = v;
                tev.value_f32 = v;
                if (id == kParamBankSlot)
                    tev.value_u32 = static_cast<uint32_t>(ArpSID::canonicalFactorySlotFromNormalizedBankSlot(static_cast<float>(v)));
                ArpSID::sidWrapperPushEvent(runtimeModel_, tev, currentProcessSamples_);
            }
            // Render-thread mutation remains owned by canonical timed dispatch.
            // Do not eagerly update paramValues here: same-block readers must not
            // observe future automation points before canonical dispatch reaches them.
        }
    }
}

// ─── MIDI Events ─────────────────────────────────────────────────────────────

void ArpSIDProcessorPhase2::mirrorVstMidiHeldIngress_(
        int channel, int note, int noteId, int velocity7, bool noteOn) noexcept {
    const uint8_t ch = static_cast<uint8_t>(std::clamp(channel, 0, 15));
    const uint8_t key = static_cast<uint8_t>(std::clamp(note, 0, 127));
    auto& hs = runtimeHostSurface();
    const uint32_t heldGen = hs.heldIngressChannelGeneration[(size_t)ch].load(std::memory_order_acquire);
    const bool heldCurrent =
        hs.heldIngressNoteGeneration[(size_t)ch][(size_t)key].load(std::memory_order_acquire) == heldGen;

    if (noteOn) {
        const uint8_t prevDepth = heldCurrent
            ? hs.heldIngressDepth[(size_t)ch][(size_t)key].load(std::memory_order_acquire)
            : 0u;
        const uint8_t nextDepth = static_cast<uint8_t>(std::min<int>(
            static_cast<int>(ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes),
            static_cast<int>(prevDepth) + 1));
        const uint8_t vel = static_cast<uint8_t>(std::clamp(velocity7, 1, 127));
        uint32_t serial = hs.heldIngressSerial.fetch_add(1u, std::memory_order_relaxed) + 1u;
        if (serial == 0u) serial = hs.heldIngressSerial.fetch_add(1u, std::memory_order_relaxed) + 1u;
        const int32_t replayNoteId = noteId >= 0
            ? static_cast<int32_t>(noteId)
            : ArpSID::SidRuntimeHostSurface::makeSyntheticAnonymousNoteId(serial);
        const size_t lane = std::min<size_t>(
            static_cast<size_t>(prevDepth),
            ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes - 1u);

        hs.heldIngressVelocity[(size_t)ch][(size_t)key].store(vel, std::memory_order_relaxed);
        hs.heldIngressLaneVelocity[(size_t)ch][(size_t)key][lane].store(vel, std::memory_order_relaxed);
        hs.heldIngressLaneNoteId[(size_t)ch][(size_t)key][lane].store(replayNoteId, std::memory_order_relaxed);
        hs.heldIngressLaneOrder[(size_t)ch][(size_t)key][lane].store(serial, std::memory_order_relaxed);
        hs.heldIngressNoteId[(size_t)ch][(size_t)key].store(replayNoteId, std::memory_order_relaxed);
        hs.heldIngressOrder[(size_t)ch][(size_t)key].store(serial, std::memory_order_relaxed);
        hs.heldIngressNoteGeneration[(size_t)ch][(size_t)key].store(heldGen, std::memory_order_release);
        hs.heldIngressDepth[(size_t)ch][(size_t)key].store(nextDepth, std::memory_order_release);
        return;
    }

    const uint8_t prevDepth = heldCurrent
        ? hs.heldIngressDepth[(size_t)ch][(size_t)key].load(std::memory_order_acquire)
        : 0u;
    if (prevDepth == 0u) {
        hs.heldIngressNoteGeneration[(size_t)ch][(size_t)key].store(heldGen, std::memory_order_release);
        return;
    }

    size_t clearLane = std::min<size_t>(
        static_cast<size_t>(prevDepth - 1u),
        ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes - 1u);
    if (noteId >= 0) {
        for (size_t lane = 0; lane < static_cast<size_t>(prevDepth) &&
                              lane < ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes; ++lane) {
            const int32_t laneId = hs.heldIngressLaneNoteId[(size_t)ch][(size_t)key][lane].load(std::memory_order_relaxed);
            if (laneId == noteId) {
                clearLane = lane;
                break;
            }
        }
    }

    const uint8_t nextDepth = static_cast<uint8_t>(prevDepth - 1u);
    for (size_t lane = clearLane; lane + 1u < static_cast<size_t>(prevDepth) &&
                                lane + 1u < ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes; ++lane) {
        hs.heldIngressLaneVelocity[(size_t)ch][(size_t)key][lane].store(
            hs.heldIngressLaneVelocity[(size_t)ch][(size_t)key][lane + 1u].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        hs.heldIngressLaneNoteId[(size_t)ch][(size_t)key][lane].store(
            hs.heldIngressLaneNoteId[(size_t)ch][(size_t)key][lane + 1u].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        hs.heldIngressLaneOrder[(size_t)ch][(size_t)key][lane].store(
            hs.heldIngressLaneOrder[(size_t)ch][(size_t)key][lane + 1u].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }

    const size_t tailLane = std::min<size_t>(
        static_cast<size_t>(prevDepth - 1u),
        ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes - 1u);
    hs.heldIngressLaneVelocity[(size_t)ch][(size_t)key][tailLane].store(0u, std::memory_order_relaxed);
    hs.heldIngressLaneNoteId[(size_t)ch][(size_t)key][tailLane].store(-1, std::memory_order_relaxed);
    hs.heldIngressLaneOrder[(size_t)ch][(size_t)key][tailLane].store(0u, std::memory_order_relaxed);

    if (nextDepth == 0u) {
        hs.heldIngressVelocity[(size_t)ch][(size_t)key].store(0u, std::memory_order_relaxed);
        hs.heldIngressNoteId[(size_t)ch][(size_t)key].store(-1, std::memory_order_relaxed);
        hs.heldIngressOrder[(size_t)ch][(size_t)key].store(0u, std::memory_order_relaxed);
    } else {
        const size_t topLane = std::min<size_t>(
            static_cast<size_t>(nextDepth - 1u),
            ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes - 1u);
        hs.heldIngressVelocity[(size_t)ch][(size_t)key].store(
            hs.heldIngressLaneVelocity[(size_t)ch][(size_t)key][topLane].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        hs.heldIngressNoteId[(size_t)ch][(size_t)key].store(
            hs.heldIngressLaneNoteId[(size_t)ch][(size_t)key][topLane].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        hs.heldIngressOrder[(size_t)ch][(size_t)key].store(
            hs.heldIngressLaneOrder[(size_t)ch][(size_t)key][topLane].load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }
    hs.heldIngressNoteGeneration[(size_t)ch][(size_t)key].store(heldGen, std::memory_order_release);
    hs.heldIngressDepth[(size_t)ch][(size_t)key].store(nextDepth, std::memory_order_release);
}

void ArpSIDProcessorPhase2::processMIDIEvents(ProcessData& data) {
    if (!data.inputEvents) return;
    const int32 count = data.inputEvents->getEventCount();
    const int32 maxSampleOffset = std::max<int32>(0, currentProcessSamples_ - 1);
    const auto maybePromoteDrSidForGMNote = [&](int channel, int midiNote) noexcept {
        if (!runtimeExecutionOwner_) return;
        // v909 Classic-mode authority: the VST3 Phase2 processor is the Hybrid
        // (Classic) flavor. GM channel-10 notes may only auto-promote into
        // DrSID when the user explicitly enabled kParamAutoGmDrumPromotion;
        // otherwise the selected Classic/BitPerfect mode remains authoritative.
        (void)ArpSID::sidCanonicalApplyGMDrSidPromotion(
            *this,
            static_cast<uint8_t>(std::clamp(channel, 0, 15)),
            static_cast<uint8_t>(std::clamp(midiNote, 0, 127)),
            drSidEngine_() != nullptr,
            resolveTopLevelRenderMode_() == ArpSID::SidRuntimeRenderMode::DrSid,
            ArpSID::sidCanonicalGMDrumAutoPromotionAllowed(
                false /* VST3 Phase2 is not a dedicated drum flavor */,
                true /* Hybrid (Classic) flavor */,
                paramValues[(size_t)kParamAutoGmDrumPromotion] > 0.5f));
    };
    for (int32 i = 0; i < count; ++i) {
        Event e{};
        if (data.inputEvents->getEvent(i, e) != kResultOk) continue;
        switch (e.type) {
            case Event::kNoteOnEvent: {
                maybePromoteDrSidForGMNote(e.noteOn.channel, e.noteOn.pitch);
                const float vel = std::clamp(e.noteOn.velocity, 0.0f, 1.0f);
                mirrorVstMidiHeldIngress_(e.noteOn.channel,
                                           e.noteOn.pitch,
                                           e.noteOn.noteId,
                                           std::clamp(static_cast<int>(std::lround(vel * 127.0f)), 1, 127),
                                           true);
                MidiEvent me;
                me.kind        = MidiKind::NoteOn;
                me.pitch       = e.noteOn.pitch;
                me.channel     = e.noteOn.channel;
                me.noteId      = e.noteOn.noteId;
                me.value       = vel;
                const int32 clampedSampleOffset = std::clamp<int32>(e.sampleOffset, 0, maxSampleOffset);
                me.sampleOffset= clampedSampleOffset;
                {
                    SidTimedEvent tev{};
                    tev.type = SidTimedEventType::MidiNoteOn;
                    assignApproxIntraSampleTiming_(tev, clampedSampleOffset, 0u /*arrival_order set by merge*/, 2u, sampleRate, currentSidClockHz());
                    tev.channel = (e.noteOn.channel >= 0)
                                      ? static_cast<uint8_t>(std::clamp<int16_t>(e.noteOn.channel, 0, 15))
                                      : ArpSID::kSidUnresolvedChannel;
                    tev.pitch = (int16_t)std::clamp<int16_t>(e.noteOn.pitch, 0, 127);
                    tev.noteId = e.noteOn.noteId;
                    tev.value = vel;
                    ArpSID::sidWrapperPushEvent(runtimeModel_, tev, currentProcessSamples_);
                }
                midiActivityMeter = 1.0f;
                lastMidiNote = e.noteOn.pitch;
                break;
            }
            case Event::kNoteOffEvent: {
                mirrorVstMidiHeldIngress_(e.noteOff.channel,
                                           e.noteOff.pitch,
                                           e.noteOff.noteId,
                                           0,
                                           false);
                MidiEvent me;
                me.kind        = MidiKind::NoteOff;
                me.pitch       = e.noteOff.pitch;
                me.channel     = e.noteOff.channel;
                me.noteId      = e.noteOff.noteId;
                const int32 clampedSampleOffset = std::clamp<int32>(e.sampleOffset, 0, maxSampleOffset);
                me.sampleOffset= clampedSampleOffset;
                {
                    SidTimedEvent tev{};
                    tev.type = SidTimedEventType::MidiNoteOff;
                    assignApproxIntraSampleTiming_(tev, clampedSampleOffset, 0u /*arrival_order set by merge*/, 2u, sampleRate, currentSidClockHz());
                    tev.channel = (e.noteOff.channel >= 0)
                                      ? static_cast<uint8_t>(std::clamp<int16_t>(e.noteOff.channel, 0, 15))
                                      : ArpSID::kSidUnresolvedChannel;
                    tev.pitch = (int16_t)std::clamp<int16_t>(e.noteOff.pitch, 0, 127);
                    tev.noteId = e.noteOff.noteId;
                    ArpSID::sidWrapperPushEvent(runtimeModel_, tev, currentProcessSamples_);
                }
                break;
            }
            case Event::kPolyPressureEvent: {
                MidiEvent me;
                me.kind        = MidiKind::PolyPressure;
                me.pitch       = e.polyPressure.pitch;
                me.channel     = e.polyPressure.channel;
                me.noteId      = e.polyPressure.noteId;
                me.value       = std::clamp(e.polyPressure.pressure, 0.0f, 1.0f);
                const int32 clampedSampleOffset = std::clamp<int32>(e.sampleOffset, 0, maxSampleOffset);
                me.sampleOffset= clampedSampleOffset;
                {
                    SidTimedEvent tev{};
                    tev.type = SidTimedEventType::PolyPressure;
                    assignApproxIntraSampleTiming_(tev, clampedSampleOffset, 0u /*arrival_order set by merge*/, 2u, sampleRate, currentSidClockHz());
                    tev.channel = (e.polyPressure.channel >= 0)
                                      ? static_cast<uint8_t>(std::clamp<int16_t>(e.polyPressure.channel, 0, 15))
                                      : ArpSID::kSidUnresolvedChannel;
                    tev.pitch = (int16_t)std::clamp<int16_t>(e.polyPressure.pitch, 0, 127);
                    tev.noteId = e.polyPressure.noteId;
                    tev.value = std::clamp(e.polyPressure.pressure, 0.0f, 1.0f);
                    ArpSID::sidWrapperPushEvent(runtimeModel_, tev, currentProcessSamples_);
                }
                break;
            }
            default: break;
        }
    }

    // Direct engine dispatch now happens from the canonical timed queue in process().
    // This keeps VST3 host MIDI, transport, automation mirrors, and sequencer output
    // on one shared ordering path before engine mutation.
}

void ArpSIDProcessorPhase2::appendCanonicalMidiEvents_(const ArpSID::SidTimedEventQueue& q) noexcept {
    const size_t droppedBefore = midiEvents.dropped();
    for (int i = 0; i < q.count; ++i) {
        const SidTimedEvent& ev = q.events[i];
        MidiEvent me{};
        me.sampleOffset = (Steinberg::int32)ev.sample_offset;
        me.cycleOffset = ev.cycle_offset;
        me.rawOrder = ev.arrival_order;
        me.channel = ev.channel;
        me.pitch = ev.pitch;
        me.noteId = ev.noteId;
        me.value = ev.value;
        me.data14 = ev.data14;
        me.ccNum = ev.ccNum;
        switch (ev.type) {
            case SidTimedEventType::MidiNoteOn:        me.kind = MidiKind::NoteOn; break;
            case SidTimedEventType::MidiNoteOff:       me.kind = MidiKind::NoteOff; break;
            case SidTimedEventType::PolyPressure:      me.kind = MidiKind::PolyPressure; break;
            case SidTimedEventType::MidiCC:            me.kind = MidiKind::ControlChange; break;
            case SidTimedEventType::PitchBend:         me.kind = MidiKind::PitchBend; break;
            case SidTimedEventType::ChannelPressure:   me.kind = MidiKind::ChannelPressure; break;
            case SidTimedEventType::AllNotesOff:       me.kind = MidiKind::AllNotesOff; break;
            case SidTimedEventType::AllSoundOff:       me.kind = MidiKind::AllSoundOff; break;
            default: continue;
        }
        me.provenance = debugProvenanceForMidiKind_(me.kind);
        midiEvents.push_back(me);
    }
    if (midiEvents.dropped() > droppedBefore) {
        ARPLOG("WARN: %zu MIDI event(s) dropped (midiEvents buffer full)",
               midiEvents.dropped() - droppedBefore);
    }
}

void ArpSIDProcessorPhase2::dispatchCanonicalTimedEvent_(const ArpSID::SidTimedEvent& ev) noexcept {
    ArpSID::dispatchCanonicalTimedEventToTarget(runtimeModel_, ev, *this);
}

void ArpSIDProcessorPhase2::dispatchCanonicalTimedQueue_(const ArpSID::SidTimedEventQueue& q) noexcept {
    ArpSID::dispatchCanonicalTimedQueueToTarget(runtimeModel_, q, *this);
}

// ─── Render Audio ─────────────────────────────────────────────────────────────

// v908: the old renderAudio(ProcessData&) entrypoint was removed. Standard
// Phase2/VST3 processing must go through processCanonicalBlockPhase2_() so
// fractional reset, live glide scheduling, future-write rebase, no-output
// scratch drain, mono folding, normalization, and post-FX state advancement
// stay under one timing/music contract.
void ArpSIDProcessorPhase2::renderAudioSlice_(float* outL, float* outR, int32 numOutChannels, int32 offset, int32 sliceFrames) noexcept {
    if (!outL || sliceFrames <= 0) return;
    if (offset < 0) return;
    float* writeR = (numOutChannels >= 2 && outR) ? outR : nullptr;
    if ((size_t)sliceFrames > tmpOutL.size() ||
        (size_t)sliceFrames > tmpOutR.size()) {
        for (int i = 0; i < sliceFrames; ++i) {
            outL[offset + i] = 0.0f;
            if (writeR) writeR[offset + i] = 0.0f;
        }
        return;
    }
    float* buf2[2] = { tmpOutL.data(), tmpOutR.data() };
    if (sliceFrames == 1 && offset >= 0 && (size_t)offset < runtimeFractionalActive_.size() && runtimeFractionalActive_[(size_t)offset]) {
        float finalL = 0.0f, finalR = 0.0f;
        const ArpSID::SidRuntimeRenderMode renderMode = resolveTopLevelRenderMode_();
        if (renderMode == ArpSID::SidRuntimeRenderMode::DrSid && drSidEngine_()) {
            drSidEngine_()->finalizeFractionalHostSample(finalL, finalR);
        } else if (renderMode == ArpSID::SidRuntimeRenderMode::BitPerfect && bitPerfectEngine_()) {
            bitPerfectEngine_()->finalizeFractionalHostSample(finalL, finalR);
        } else if (renderMode == ArpSID::SidRuntimeRenderMode::SidRegister) {
            sidRegEngine_().resetIntervalCursor();
        }
        if (runtimeFractionalActive_[(size_t)offset] == 2u) {
            const uint32_t weight = ((size_t)offset < runtimeFractionalWeight_.size()) ? runtimeFractionalWeight_[(size_t)offset] : 0u;
            const float invW = weight > 0u ? (1.0f / static_cast<float>(weight)) : 0.0f;
            buf2[0][0] = std::isfinite(runtimeFractionalAccumL_[(size_t)offset] * invW + finalL) ? (runtimeFractionalAccumL_[(size_t)offset] * invW + finalL) : 0.0f;
            buf2[1][0] = std::isfinite(runtimeFractionalAccumR_[(size_t)offset] * invW + finalR) ? (runtimeFractionalAccumR_[(size_t)offset] * invW + finalR) : 0.0f;
        } else {
            buf2[0][0] = std::isfinite(runtimeFractionalAccumL_[(size_t)offset] + finalL) ? (runtimeFractionalAccumL_[(size_t)offset] + finalL) : 0.0f;
            buf2[1][0] = std::isfinite(runtimeFractionalAccumR_[(size_t)offset] + finalR) ? (runtimeFractionalAccumR_[(size_t)offset] + finalR) : 0.0f;
        }
        runtimeFractionalAccumL_[(size_t)offset] = runtimeFractionalAccumR_[(size_t)offset] = 0.0f;
        if ((size_t)offset < runtimeFractionalWeight_.size()) runtimeFractionalWeight_[(size_t)offset] = 0u;
        runtimeFractionalActive_[(size_t)offset] = 0u;
    } else {
        runtimeCurrentRenderOffset_ = static_cast<int>(offset);
        ArpSID::renderCanonicalAudioForTarget(*this, buf2[0], buf2[1], sliceFrames);
        runtimeCurrentRenderOffset_ = 0;
    }

    for (auto& v : smVoices_()) { if (v.active) v.age += (float)sliceFrames; }

    // Write pre-FX samples directly into output buffers at correct offset.
    // Normalization/post-FX are applied once per block after canonical render,
    // including v908 no-output scratch blocks, so state is continuous regardless
    // of slice segmentation or host bus availability.
    for (int i = 0; i < sliceFrames; ++i) {
        const float l = std::isfinite(buf2[0][i]) ? buf2[0][i] : 0.0f;
        const float r = std::isfinite(buf2[1][i]) ? buf2[1][i] : 0.0f;
        if (writeR) {
            outL[offset + i] = l;
            writeR[offset + i] = r;
        } else {
            // v906 Phase2 mono parity: preserve stereo/fractional SID energy by
            // folding L/R, matching AU3/shared-output behavior.
            outL[offset + i] = 0.5f * (l + r);
        }
    }
}

// ─── Post-FX application ──────────────────────────────────────────────────────
// Applies reverb and peak limiter in-place to [outL, outR, n].
// Called once per block after the canonical render pass.
// Shared by both 32-bit and 64-bit output paths (P0-1 fix).
void ArpSIDProcessorPhase2::applyOutputFX_(float* outL, float* outR, int n) noexcept {
    if (!outL || n <= 0) return;
    float* writeR = outR ? outR : outL;
    const bool sharedOutputBus = writeR == outL;
    ArpSID::SidPostFxAutomationState state = postFxBlockStart_;
    limiter.setAttackMs(state.limiterAttackMs, sampleRate);
    limiter.setReleaseMs(state.limiterReleaseMs, sampleRate);
    const ArpSID::SidTimedEventQueue* queue = canonicalQueueScratch_.get();
    int eventIndex = 0;
    const uint32_t quietResetFrames = static_cast<uint32_t>(std::max(1.0, sampleRate * 0.25));
    float tailSumSq = 0.0f;
    for (int i = 0; i < n; ++i) {
        if (queue) while (eventIndex < queue->count &&
                          static_cast<int>(queue->events[(size_t)eventIndex].sample_offset) <= i) {
            const float oldAttack = state.limiterAttackMs;
            const float oldRelease = state.limiterReleaseMs;
            (void)ArpSID::sidApplyPostFxAutomationEvent(state, queue->events[(size_t)eventIndex]);
            if (state.limiterAttackMs != oldAttack) limiter.setAttackMs(state.limiterAttackMs, sampleRate);
            if (state.limiterReleaseMs != oldRelease) limiter.setReleaseMs(state.limiterReleaseMs, sampleRate);
            ++eventIndex;
        }
        float l = std::isfinite(outL[i]) ? outL[i] : 0.0f;
        float r = std::isfinite(writeR[i]) ? writeR[i] : l;
        const float dryPeak = std::max(std::fabs(l), std::fabs(r));
        if (state.reverbMix > 1.0e-4f) {
            float rl = 0.0f, rr = 0.0f;
            reverb.process(l, r, rl, rr);
            l += rl * state.reverbMix;
            r += rr * state.reverbMix;
            const float wet = 0.5f * (std::fabs(rl) + std::fabs(rr));
            tailSumSq += wet * wet;
        }
        if (state.limiterEnabled) limiter.processStereoSample(l, r, state.limiterThreshold);
        l = std::clamp(std::isfinite(l) ? l : 0.0f, -1.0f, 1.0f);
        r = std::clamp(std::isfinite(r) ? r : 0.0f, -1.0f, 1.0f);
        if (std::fabs(l) < 1.0e-10f) l = 0.0f;
        if (std::fabs(r) < 1.0e-10f) r = 0.0f;
        if (state.reverbMix > 1.0e-4f) {
            const float wetPeak = std::max(std::fabs(l), std::fabs(r));
            if (dryPeak < 1.0e-5f && wetPeak < 2.0e-5f) {
                if (++reverbSilentFrames >= quietResetFrames) {
                    reverb.reset(); reverbTailEnergy = 0.0f; reverbSilentFrames = 0u;
                }
            } else reverbSilentFrames = 0u;
        } else reverbSilentFrames = 0u;
        if (sharedOutputBus) outL[i] = 0.5f * (l + r);
        else { outL[i] = l; writeR[i] = r; }
    }
    reverbTailEnergy = state.reverbMix > 1.0e-4f
        ? std::sqrt(tailSumSq / static_cast<float>(std::max(1, n))) : 0.0f;

    const ArpSID::SidPostFxAutomationState finalDynamicsState = state;

    // HI-FI has internal state, so process contiguous segments and reconfigure
    // only at exact canonical automation boundaries.
    state = postFxBlockStart_;
    int cursor = 0;
    auto processHiFiSegment = [&](int end) noexcept {
        end = std::clamp(end, cursor, n);
        if (end <= cursor) return;
        lastHiFiConfig_ = ArpSID::sidHiFiConfigFromPostFxState(state);
        hifiTranscendence_.configure(sampleRate, buildForensicConfig_(), lastHiFiConfig_);
        hifiTranscendence_.processStereo(outL + cursor, writeR + cursor, end - cursor);
        cursor = end;
    };
    if (queue) for (int i = 0; i < queue->count; ++i) {
        const auto& ev = queue->events[(size_t)i];
        if (!ArpSID::sidPostFxEventTargetsHiFi(ev)) continue;
        processHiFiSegment(std::clamp(static_cast<int>(ev.sample_offset), 0, n));
        (void)ArpSID::sidApplyPostFxAutomationEvent(state, ev);
    }
    processHiFiSegment(n);
    // Preserve the exact final dynamics state from the first event walk. The
    // HI-FI pass intentionally replays only HI-FI targets from block-start
    // state, so its local state does not contain the final reverb/limiter values.
    reverbMix = finalDynamicsState.reverbMix;
    limiterEnabled = finalDynamicsState.limiterEnabled;
    limiterThreshold = finalDynamicsState.limiterThreshold;
    limiterAttackMs = finalDynamicsState.limiterAttackMs;
    limiterReleaseMs = finalDynamicsState.limiterReleaseMs;
}



void ArpSIDProcessorPhase2::runtimeRenderCanonicalSlice(int offset, int sliceFrames) noexcept {
    if (sliceFrames <= 0) return;
    if (!runtimeSliceOutL_ || runtimeSliceOutChannels_ <= 0) return;
    if (runtimeExecutionOwner_) runtimeExecutionOwner_->advanceTempoLinkedControllers(sliceFrames);
    renderAudioSlice_(runtimeSliceOutL_, runtimeSliceOutR_, static_cast<Steinberg::int32>(runtimeSliceOutChannels_),
                      static_cast<Steinberg::int32>(offset), static_cast<Steinberg::int32>(sliceFrames));
}

void ArpSIDProcessorPhase2::runtimeRenderCanonicalSubPhaseSpan(int sampleOffset, uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd) noexcept {
    ArpSID::runtimeDispatchSubPhaseIntervalToBackend(*this, sampleOffset, cycleIndex, subphaseStart, subphaseEnd);
}

void ArpSIDProcessorPhase2::runtimeRenderCanonicalSubSampleSpan(int sampleOffset, uint16_t cycleStart, uint16_t cycleEnd) noexcept {
    ArpSID::runtimeDispatchIntervalToBackend(*this, sampleOffset, cycleStart, cycleEnd);
}


// ─── applyParameter ──────────────────────────────────────────────────────────
// Routes a single normalized parameter value change to the correct engine API.


uint16_t ArpSIDProcessorPhase2::runtimeEstimatedCyclesPerHostSample() const noexcept { return ArpSID::estimateSidCyclesPerHostSample(sampleRate, currentSidClockHz()); }

double ArpSIDProcessorPhase2::runtimePhysicalSampleRateHz() const noexcept { return sampleRate; }
double ArpSIDProcessorPhase2::runtimePhysicalSidClockHz() const noexcept { return currentSidClockHz(); }

void ArpSIDProcessorPhase2::runtimePolicySetFilterDrive(float value) noexcept {
    // Filter Drive is wired: output_gain_bias drives output_drive scaling in
    // sid_static_params.h via: output_drive *= pow(10, output_gain_bias_dB / 20).
    // At value=1.0, normal synth/register mode still reaches 12 dB, but
    // DrSID is capped lower so forensic/filter drive cannot recreate the
    // drum-bus distortion stack closed in v232.
    driveAmount = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    const float maxDriveDb =
        (resolveTopLevelRenderMode_() == ArpSID::SidRuntimeRenderMode::DrSid)
            ? 4.5f : 12.0f;
    auto post = runtimeModel_.measuredPosterior();
    post.output_gain_bias = driveAmount * maxDriveDb;
    runtimeModel_.setMeasuredPosterior(post);
}

void ArpSIDProcessorPhase2::runtimePolicyHandleArpRate(float value) noexcept {
    runtimeModel_.setArpActiveFlag(ArpSID::sidEffectiveArpAuthorityFromLiveParams(paramValues));
    const bool oldFollowHost = runtimeModel_.followHostTempoArp();
    const bool newFollowHost = value < 0.005f;
    if (newFollowHost) {
        runtimeModel_.setFollowHostTempoArp(true);
        if (runtimeModel_.hostTempoBpm() > 1.0 && arpeggiator_())
            arpeggiator_()->setRateTempo(runtimeModel_.hostTempoBpm(), 1.0f);
    } else {
        runtimeModel_.setFollowHostTempoArp(false);
        if (arpeggiator_()) arpeggiator_()->setRate(value);
    }
    if (!oldFollowHost && newFollowHost) rewindArpPhase();
}

void ArpSIDProcessorPhase2::runtimePolicyHandleSeqEnable(float value) noexcept {
    runtimeModel_.setArpActiveFlag(ArpSID::sidEffectiveArpAuthorityFromLiveParams(paramValues));
    const bool requestedEnable = value > 0.5f;
    if (requestedEnable && !ArpSID::sidEffectiveSeqAuthorityFromLiveParams(paramValues)) {
        // v943: raw SeqEnable cannot arm dormant VST sequencer state under
        // SynthMode/DrSID. Canonicalize it off immediately.
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);
        runtimeModel_.setSeqLastNote(-1);
        runtimeModel_.setSeqSamplesUntilStep(-1.0);
        runtimeModel_.setSeqStep(0);
        return;
    }
    if (!requestedEnable && runtimeModel_.seqLastNote() >= 0) {
        const int note = runtimeModel_.seqLastNote();
        const auto mode = resolveTopLevelRenderMode_();
        if (mode == ArpSID::SidRuntimeRenderMode::DrSid) {
            runtimeReleaseDrSidNote(note);
        } else if (mode == ArpSID::SidRuntimeRenderMode::SidRegister) {
            synthModeNoteOff(note, 0, 0, 0, -1);
        } else if (bitPerfectEngine_()) {
            bitPerfectEngine_()->noteOff(note, 0, -1);
        }
        runtimeModel_.setSeqLastNote(-1);
        runtimeModel_.setSeqSamplesUntilStep(-1.0);
        runtimeModel_.setSeqStep(0);
    } else if (!requestedEnable) {
        runtimeModel_.setSeqLastNote(-1);
        runtimeModel_.setSeqSamplesUntilStep(-1.0);
        runtimeModel_.setSeqStep(0);
    }
}

void ArpSIDProcessorPhase2::runtimePolicyHandleSeqTempo(float value) noexcept {
    const bool oldFollowHost = runtimeModel_.followHostTempoSeq();
    const bool newFollowHost = value < 0.005f;
    runtimeModel_.setFollowHostTempoSeq(newFollowHost);
    if (oldFollowHost != newFollowHost) {
        runtimeModel_.setSeqSamplesUntilStep(-1.0);
        runtimeModel_.setSeqStep(0);
    }
}

void ArpSIDProcessorPhase2::runtimePolicyHandleVirtualGate(float value) noexcept {
    const bool gate = (value > 0.5f);
    const float vnorm = ArpSID::sanitizeNormalizedParamValue((int)kParamVirtualNote, paramValues[(size_t)kParamVirtualNote],
                                                       ArpSID::defaultNormalizedParamValue((int)kParamVirtualNote));
    const int note = std::clamp((int)std::lround(vnorm * 127.f), 0, 127);
    const ArpSID::SidRuntimeRenderMode renderMode = resolveTopLevelRenderMode_();
    const bool synthMode = renderMode == ArpSID::SidRuntimeRenderMode::SidRegister;
    const bool drMode = renderMode == ArpSID::SidRuntimeRenderMode::DrSid;
    const bool arpMode = ArpSID::sidEffectiveArpAuthorityFromLiveParams(paramValues);
    const int liveCh = (lastHostChannel_ >= 0 && lastHostChannel_ < 16) ? lastHostChannel_ : -1;
    const bool transportPlaying = runtimeModel_.seqHostWasPlaying();
    if (!transportPlaying) {
        if (virtualGateNoteId_ >= 0 && liveCh >= 0) {
            if (synthMode) {
                synthModeNoteOff(note, 0, 0, liveCh, virtualGateNoteId_);
            } else if (arpMode) {
                if (arpeggiator_()) arpeggiator_()->noteOff(note);
                // Virtual-gate NoteOn may have allocated a concrete BitPerfect voice
                // before arp became note authority. When arp is now enabled, the
                // matching NoteOff must still reach BPE for real noteId identities;
                // arp-generated voices remain anonymous (noteId < 0) and are not
                // affected by this safety-net.
                if (bitPerfectEngine_()) bitPerfectEngine_()->noteOff(note, liveCh, virtualGateNoteId_);
            } else if (!drMode && bitPerfectEngine_()) {
                bitPerfectEngine_()->noteOff(note, liveCh, virtualGateNoteId_);
            }
        }
        virtualGateNoteId_ = -1;
        return;
    }
    if (gate) {
        if (drMode) {
            if (drSidEngine_()) drSidEngine_()->triggerMidiNote(note, 1.0f);
        } else if (liveCh >= 0 && synthMode) {
            // v929: Phase2/VST virtual-gate uses the same render-mode authority
            // order as AU3 and canonical host MIDI. SynthMode/SID-register owns
            // playable note routing even if ArpEnable is stale/on.
            virtualGateNoteId_ = nextVirtualGateNoteId_++;
            synthModeNoteOn(note, 1.0f, 0, 0, liveCh, virtualGateNoteId_);
        } else if (arpMode) {
            if (arpeggiator_()) arpeggiator_()->noteOn(note, 1.0f);
        } else if (liveCh >= 0 && bitPerfectEngine_()) {
            virtualGateNoteId_ = nextVirtualGateNoteId_++;
            bitPerfectEngine_()->noteOn(note, 1.0f, liveCh, virtualGateNoteId_);
        }
    } else {
        if (liveCh >= 0 && synthMode) {
            synthModeNoteOff(note, 0, 0, liveCh, virtualGateNoteId_);
        } else if (arpMode) {
            if (arpeggiator_()) arpeggiator_()->noteOff(note);
            if (virtualGateNoteId_ >= 0 && liveCh >= 0 && bitPerfectEngine_()) {
                bitPerfectEngine_()->noteOff(note, liveCh, virtualGateNoteId_);
            }
        } else if (liveCh >= 0 && !drMode && bitPerfectEngine_()) {
            bitPerfectEngine_()->noteOff(note, liveCh, virtualGateNoteId_);
        }
        virtualGateNoteId_ = -1;
    }
}

void ArpSIDProcessorPhase2::runtimePolicyHandleSynthModeEnable(float value) noexcept {
    const bool nowOn = (value > 0.5f);
    if (nowOn) {
        // v945: do not bypass Phase2's canonical staging law. Direct paramValues
        // writes keep the mode mostly coherent today, but skip lastAppliedParamValues,
        // param-specific sanitize and future backend/policy side effects.
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);
        if (auto* arp = arpeggiator_()) {
            arp->allNotesOff();
            arp->setEnabled(false);
        }
        runtimePolicyHandleSeqEnable(0.0f);
        runtimeModel_.setArpActiveFlag(false);
        runtimeApplyAllNotesOffPerformanceReset_(-1, false);
        sidWriteQueue_().clear();
        syncSidSystemModelFromParams(true);
        syncSidQueuedShadowFromLive_();
        sanitizeSynthModeQueuedShadowLocal_(sidQueuedShadow_(), paramValues.data(), sidSysByteCached);
        // Seed full synth-mode voice truth at gate-off so first note startup never depends
        // on stale/poisoned queued shadow registers.
        for (int v = 0; v < 3; ++v)
            pushSynthModeVoiceSeedRealtime_(v, false, 0, 0);
        pushSynthModeFilterRegsRealtime_(0, 0);
    } else {
        for (auto& v : smVoices_()) {
            if (v.active) hardSynthModeVoiceOff_((int)(&v - smVoices_().data()), 0u, 0u, true);
        }
    }
}

void ArpSIDProcessorPhase2::runtimePolicySetLimiterEnabled(bool on) noexcept { limiterEnabled = on; }
void ArpSIDProcessorPhase2::runtimePolicySetLimiterThreshold(float value) noexcept { limiterThreshold = value; }
void ArpSIDProcessorPhase2::runtimePolicySetLimiterAttack(float value) noexcept { limiterAttackMs = value * 20.0f; limiter.setAttackMs(limiterAttackMs, sampleRate); }
void ArpSIDProcessorPhase2::runtimePolicySetLimiterRelease(float value) noexcept { limiterReleaseMs = 10.0f + value * 990.0f; limiter.setReleaseMs(limiterReleaseMs, sampleRate); }
void ArpSIDProcessorPhase2::runtimePolicySetReverbMix(float value) noexcept { reverbMix = value; }
void ArpSIDProcessorPhase2::runtimePolicyHandlePanic(float value) noexcept {
    if (value > 0.5f) {
        runtimeModel_.clearTransientEvents();
        externalCanonicalInputQueue_->reset();
        midiEvents.clear();
        // P0 FIX: When panic is triggered via the parameter path rather than via a
        // canonical Panic event, applyCanonicalEventToState() is bypassed. Explicitly
        // clear live MIDI mirrors (pitch bend, sustain, token voices, channel pressure)
        // so the model is consistent with the engine reset that follows.
        runtimeModel_.clearLiveMidiState();
        runtimeModel_.clearIdentityMirrors();
        ArpSID::runtimeRenderHostResetEngines(*this);
    }
}
void ArpSIDProcessorPhase2::runtimePolicyApplyForensicConfig() noexcept { applyForensicConfig_(); }

void ArpSIDProcessorPhase2::runtimeApplyProjectedParameterBody(uint32_t target, float value) noexcept {
    const Steinberg::Vst::ParamID id = static_cast<Steinberg::Vst::ParamID>(target);
    if (id >= (Steinberg::Vst::ParamID)kNumParams) return;
    if (!bitPerfectEngine_()) return;

    const int oldSynthVoiceMode = synthModeVoiceMode_();
    const float clean = ArpSID::sanitizeNormalizedParamValue(
        static_cast<int>(target),
        value,
        ArpSID::defaultNormalizedParamValue(static_cast<int>(target)));
    const float oldValue = paramValues[(size_t)id];

    // v947 guard: compare canonical/sanitized values, not the raw incoming value.
    // NaN/Inf/out-of-range automation should not create endless duplicate
    // backend/policy churn when it sanitizes to the already-applied value.
    if (std::fabs(lastAppliedParamValues[(size_t)id] - clean) < 1e-6f &&
        std::fabs(oldValue - clean) < 1e-6f && !auSafeInitInProgress_)
        return;

    runtimeStageNormalizedParameterOnly(target, value);
    const float stagedClean = paramValues[(size_t)id];

    // v947: backend/policy side effects consume the staged clean value, matching
    // AU3 and keeping engine internals synchronized with paramValues/runtimeModel_.
    const bool backendHandled = ArpSID::runtimeApplyProjectedBackendParameter(*this, target, stagedClean);
    const bool policyHandled = backendHandled ? false
                                              : ArpSID::runtimeApplyProjectedAdapterPolicyParameter(*this, target, stagedClean);
    (void)policyHandled;

    if (resolveTopLevelRenderMode_() == ArpSID::SidRuntimeRenderMode::SidRegister) {
        if (id == kParamVoiceMode || id == kParamVoiceSpread) {
            synthVoicePolicy_().setPlayMode(ArpSID::sidPlayModeFromCanonicalVoiceMode(synthModeVoiceMode_()));
            const int uniCount = ArpSID::requestedUnisonCountFromNormalizedSpread(paramValues[(size_t)kParamVoiceSpread]);
            synthVoicePolicy_().setUnisonCount(ArpSID::sidRegProjectedUnisonCount(uniCount));
            seedSynthHeldFromCurrentVoices_();
            sidWriteQueue_().clear();
            for (int i = 0; i < 3; ++i) {
                if (smVoices_()[(size_t)i].active)
                    hardSynthModeVoiceOff_(i, 0u, 0u, true);
            }
            if (const auto* top = synthVoicePolicy_().priorityHeldNote()) {
                synthModeNoteOn(top->identity.midi_note,
                                top->velocity,
                                0,
                                0u,
                                top->identity.channel,
                                top->identity.note_id);
            } else {
                reseedSynthModeRealtimeState_(0, 0u, false);
            }
        } else {
            reseedSynthModeRealtimeState_(0, 0u, true);
        }

        // Re-apply live performance state immediately in SID-register mode so GUI/automation,
        // host wheels, aftertouch and direct GUI knob edits are audible in the current block.
        applyLiveChannelPressureToSynthMode_(0u, 0u);
        bool activeChannels[16]{};
        for (const auto& sv : smVoices_()) {
            if (!sv.active) continue;
            const int ch = std::clamp<int>(sv.channel, 0, 15);
            activeChannels[ch] = true;
        }
        bool anyActive = false;
        for (int ch = 0; ch < 16; ++ch) {
            if (!activeChannels[ch]) continue;
            anyActive = true;
            ArpSID::applySynthModePitchBendToVoices(smVoices_(), sidWriteQueue_(), currentSidClockHz(), currentPitchBendSemis_(ch), ch, 0u, 0u);
        }
        if (!anyActive) {
            const int focusedChannel = std::clamp<int>(lastHostChannel_, 0, 15);
            ArpSID::applySynthModePitchBendToVoices(smVoices_(), sidWriteQueue_(), currentSidClockHz(), currentPitchBendSemis_(focusedChannel), focusedChannel, 0u, 0u);
        }
    }

    if (id == kParamVoiceMode && oldSynthVoiceMode != synthModeVoiceMode_()) {
        resetRenderModeOutputNormalizer_();
    }
}

// ─── Forensic Config ──────────────────────────────────────────────────────────

ArpSIDForensicConfig ArpSIDProcessorPhase2::buildForensicConfig_() const noexcept {
    return ArpSID::buildEffectiveForensicConfigFromParams(
        [this](ParamID pid) noexcept -> float { return paramValues[(size_t)pid]; },
        runtimeModel_.variantProfile(),
        runtimeModel_.staticParams());
}

void ArpSIDProcessorPhase2::applyForensicConfig_() noexcept {
    // Forensic parameter projection is now handled centrally in backend projection.
    if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(false);
}

// ─── SID System / Model sync ─────────────────────────────────────────────────

void ArpSIDProcessorPhase2::syncSidSystemModelFromParams(bool force) {
    const SidVariantProfile variant = runtimeModel_.variantProfile();
    const bool selected6581 = sidChipRevisionSelectorIs6581(paramValues[(size_t)kParamSidChipRevision]);
    const bool adsrBug = selected6581 || paramValues[(size_t)kParamSidAdsrBug6581] > 0.5f;
    uint8_t sysb = sidSystemByteFromVariantProfile(variant, adsrBug);
    if (selected6581) sysb &= static_cast<uint8_t>(~0x02u); else sysb |= 0x02u;
    if (force || sysb != sidSysByteCached) {
        sidSysByteCached = sysb;
        runtimeModel_.importPseudoSystemSnapshot(sysb);
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(false);
    }
}

// ─── SynthMode voice scheduling ──────────────────────────────────────────────
// Full implementation of the Synth Mode voice scheduling lives here.
// The SID has 3 voices — we map MIDI notes to SID voice registers directly.

void ArpSIDProcessorPhase2::synthModeNoteOn(
        int midiNote, float velocity, int sampleOffset,
        uint16_t cycleOffset, int channel, int noteId) {
    const int clampedSampleOffset = std::clamp(sampleOffset, 0, std::max(0, currentProcessSamples_ - 1));
    pushSynthModeFilterRegsRealtime_(clampedSampleOffset, cycleOffset);
    ArpSID::scheduleSynthModeNoteOn(
        synthVoicePolicy_(),
        smVoices_(),
        sidWriteQueue_(),
        runtimeModel_.patchStartPolicy(),
        paramValues.data(),
        sidQueuedShadow_().value.data(),
        sampleRate,
        currentSidClockHz(),
        synthModeVoiceMode_(),
        midiNote,
        velocity,
        clampedSampleOffset,
        cycleOffset,
        channel,
        noteId,
        [this](int voiceIdx, uint16_t off16, uint16_t cyc16, bool clearTracking) noexcept {
            hardSynthModeVoiceOff_(voiceIdx, off16, cyc16, clearTracking);
        });
}

void ArpSIDProcessorPhase2::synthModeNoteOff(
        int midiNote, int sampleOffset, uint16_t cycleOffset,
        int channel, int noteId) {
    const int clampedSampleOffset = std::clamp(sampleOffset, 0, std::max(0, currentProcessSamples_ - 1));
    const uint64_t voiceToken = runtimeModel_.resolveVoiceTokenForIdentity(channel, midiNote, noteId);
    ArpSID::scheduleSynthModeNoteOff(
        synthVoicePolicy_(),
        smVoices_(),
        sidWriteQueue_(),
        runtimeModel_.patchStartPolicy(),
        paramValues.data(),
        sidQueuedShadow_().value.data(),
        sampleRate,
        currentSidClockHz(),
        synthModeVoiceMode_(),
        midiNote,
        clampedSampleOffset,
        cycleOffset,
        channel,
        noteId,
        [this](int voiceIdx, uint16_t off16, uint16_t cyc16, bool clearTracking) noexcept {
            hardSynthModeVoiceOff_(voiceIdx, off16, cyc16, clearTracking);
        },
        voiceToken);
}
double ArpSIDProcessorPhase2::currentSidClockHz() const {
    return ArpSID::sidVariantClockHz(runtimeModel_.variantProfile());
}

// ─── Private helper implementations ──────────────────────────────────────────
// These helpers provide the concrete render-thread behavior used by the
// current Phase 4 pipeline.

void ArpSIDProcessorPhase2::seedZeroSnapshot_() noexcept {
    // Push an all-zero meter snapshot so the UI has a valid initial state
    // before the first real render block arrives.
    MeterSnapshot snap{};
    snap.sampleRate   = (float)sampleRate;
    snap.bufferSize   = 0;
    snap.midiActivity = false;
    snap.lastMidiNote = 0u;
    snap.activeVoices = 0;
    snap.arpPlaying   = false;
    publishLatestSnapshot_(snap);
}

void ArpSIDProcessorPhase2::invalidateSidQueuedShadow_() noexcept {
    ArpSID::invalidateRegisterShadow(sidQueuedShadow_());
}

void ArpSIDProcessorPhase2::syncSidQueuedShadowFromLive_() noexcept {
    ArpSID::syncRegisterShadowFromLive(sidRegEngine_(), sidQueuedShadow_());
    if ((size_t)0x19u < sidQueuedShadow_().value.size()) sidQueuedShadow_().value[0x19u] = sidSysByteCached;
}

static inline void sanitizeSynthModeQueuedShadowLocal_(ArpSID::SidRuntimeRegisterShadow& shadow,
                                                       const float* paramValues,
                                                       uint8_t sidSysByte) noexcept {
    for (int v = 0; v < 3; ++v) {
        const int base = v * 7;
        const bool gateOn = (shadow.value[(size_t)base + 4] & 0x01u) != 0u;
        shadow.value[(size_t)base + 4] = ArpSID::synthModeControlByteForVoice(paramValues, nullptr, v, gateOn);
    }
    if ((size_t)0x19u < shadow.value.size()) shadow.value[0x19u] = sidSysByte;
}

void ArpSIDProcessorPhase2::neutralizeTransportTransientHostControls_() noexcept {
    for (int ch = 0; ch < 16; ++ch) {
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlSustainBase) + ch), 0.0f);
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlSostenutoBase) + ch), 0.0f);
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlChannelPressureBase) + ch), 0.0f);
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlPitchBendBase) + ch), 0.5f);
        runtimeHostSurface().prevSustain[(size_t)ch] = 0.0f;
        runtimeHostSurface().prevChannelPressure[(size_t)ch] = 0.0f;
        runtimeHostSurface().prevPitchBend[(size_t)ch] = 0.5f;
    }
    runtimeHostSurface().channelPressure = 0.0f;
    runtimeHostSurface().pitchBendNorm = 0.5f;
}

void ArpSIDProcessorPhase2::flushStuckNoteState_(bool, bool) noexcept {
    runtimeModel_.clearTransientEvents();
    midiEvents.clear();
    if (externalCanonicalInputQueue_) externalCanonicalInputQueue_->reset();
    ArpSID::runtimeRenderHostResetEngines(*this);
    sidWriteQueue_().clear();
    for (auto& v : smVoices_()) v.reset();
    limiter.reset();
    reverb.reset();
    hifiTranscendence_.reset();
    reverbTailEnergy = 0.0f;
    reverbSilentFrames = 0u;
    const size_t fracCount = std::min(runtimeFractionalAccumL_.size(), runtimeFractionalAccumR_.size());
    std::fill_n(runtimeFractionalAccumL_.data(), fracCount, 0.0f);
    std::fill_n(runtimeFractionalAccumR_.data(), fracCount, 0.0f);
    std::fill_n(runtimeFractionalWeight_.data(), std::min<size_t>(fracCount, runtimeFractionalWeight_.size()), 0u);
    std::fill(runtimeFractionalActive_.begin(), runtimeFractionalActive_.end(), uint8_t{0});
    syncSidSystemModelFromParams(true);
    syncSidQueuedShadowFromLive_();
    sanitizeSynthModeQueuedShadowLocal_(sidQueuedShadow_(), paramValues.data(), sidSysByteCached);
}

void ArpSIDProcessorPhase2::clearRuntimeStateForTransportStart_() noexcept {
    runtimeApplyAllNotesOffPerformanceReset_(-1, true);
    neutralizeTransportTransientHostControls_();
    flushStuckNoteState_(true, true);
    rewindArpPhase();
}

void ArpSIDProcessorPhase2::performRenderModeTransition_(
        ArpSID::SidRuntimeRenderMode oldMode,
        ArpSID::SidRuntimeRenderMode newMode) noexcept {
    // Controlled mode transition: gate off SID-register voices before engine reset,
    // flush pending writes, and rebuild queued-shadow truth from the live engine.
    // This is the safety barrier that prevents stale queued gates/register writes from
    // bleeding across mode swaps as stuck mono notes or messy hybrid output.
    if (oldMode == ArpSID::SidRuntimeRenderMode::SidRegister && newMode != oldMode) {
        for (int i = 0; i < 3; ++i)
            if (smVoices_()[(size_t)i].active)
                hardSynthModeVoiceOff_(i, 0u, 0u, true);
        sidWriteQueue_().sortStable();
    }
    resetRenderModeOutputNormalizer_();
    // v942: Phase2 mode transitions must not destroy DrSID/SID808 kit state.
    // The shared transition helper already silences all note authorities; keep
    // this local cleanup to queues/FX/normalizer/canonical mirrors only.
    ArpSID::runtimeRenderHostAllNotesOff(*this);
    sidWriteQueue_().clear();
    for (auto& v : smVoices_()) v.reset();
    runtimeModel_.clearTransientEvents();
    runtimeModel_.setSeqLastNote(-1);
    runtimeModel_.setSeqSamplesUntilStep(-1.0);
    runtimeModel_.setSeqStep(0);
    virtualGateNoteId_ = -1;
    limiter.reset();
    reverb.reset();
    hifiTranscendence_.reset();
    reverbTailEnergy = 0.0f;
    reverbSilentFrames = 0u;
    syncSidSystemModelFromParams(true);
    syncSidQueuedShadowFromLive_();
    sanitizeSynthModeQueuedShadowLocal_(sidQueuedShadow_(), paramValues.data(), sidSysByteCached);
}

uint8_t ArpSIDProcessorPhase2::synthModeVoiceCtrlNoGate_(int voiceIndex) const noexcept {
    return ArpSID::synthModeControlByteForVoice(paramValues.data(), sidQueuedShadow_().value.data(), voiceIndex, false);
}

void ArpSIDProcessorPhase2::clearQueuedSidWritesForVoice_(
        int voiceIndex, uint16_t /*baseSample*/, uint16_t /*baseCycle*/) noexcept {
    ArpSID::clearQueuedSidWritesForVoice(sidWriteQueue_(), voiceIndex);
}

void ArpSIDProcessorPhase2::hardSynthModeVoiceOff_(
        int voiceIndex, uint16_t sampleOff, uint16_t cycleOff, bool clearTracking) noexcept {
    if (clearTracking) smVoices_()[(size_t)voiceIndex].reset();
    const int base = voiceIndex * 7;
    const uint8_t ctrl = synthModeVoiceCtrlNoGate_(voiceIndex);
    pushSidWriteTimed((uint8_t)(base + 4), ctrl, sampleOff, cycleOff);
}

void ArpSIDProcessorPhase2::reconcileSynthModeUnheldVoices_() noexcept {
    const bool directSynth =
        resolveTopLevelRenderMode_() == ArpSID::SidRuntimeRenderMode::SidRegister;
    if (!directSynth) {
        synthVoicePolicy_().resetOrphanReconcile();
        return;
    }

    auto& hs = runtimeHostSurface();
    (void)synthVoicePolicy_().reconcileUnheldVoices(
        [&hs](int ch, int note) noexcept -> bool {
            if (ch < 0 || ch > 15 || note < 0 || note > 127) return true;
            const uint32_t cg = hs.heldIngressChannelGeneration[(size_t)ch].load(std::memory_order_acquire);
            const uint32_t ng = hs.heldIngressNoteGeneration[(size_t)ch][(size_t)note].load(std::memory_order_acquire);
            const uint8_t d = hs.heldIngressDepth[(size_t)ch][(size_t)note].load(std::memory_order_acquire);
            return (ng == cg) && d > 0u;
        },
        [this](int ch) noexcept -> bool {
            if (ch < 0 || ch > 15) return false;
            return pedalState_().sustainDown(ch) ||
                   pedalState_().sostenutoByChannel[(size_t)ch] != 0u;
        },
        [this](int voiceIdx) noexcept {
            if (voiceIdx >= 0 && voiceIdx < static_cast<int>(smVoices_().size())) {
                auto& sv = smVoices_()[(size_t)voiceIdx];
                sv.keyDown = false;
                sv.sustained = false;
                sv.sostenutoLatched = false;
            }
            hardSynthModeVoiceOff_(voiceIdx, 0u, 0u, false);
        });
}

ArpSID::SidRuntimeRenderMode ArpSIDProcessorPhase2::resolveTopLevelRenderMode_() const noexcept {
    return ArpSID::sidResolveRenderModeFromLiveParams(paramValues);
}

void ArpSIDProcessorPhase2::resetRenderModeOutputNormalizer_() noexcept {
    // Reset per-mode post-gain state on mode transitions and state restores.
    renderModeOutputGain_ = {1.0f, 1.0f, 1.0f};
    reverbMix = paramValues[(size_t)kParamReverbMix];
    // FIX v573: Re-project limiter state from paramValues[] on state restore /
    // mode transition. Previously limiterEnabled, limiterThreshold,
    // limiterAttackMs, and limiterReleaseMs (plus the underlying SimpleLimiter
    // object time constants) were NOT refreshed here. After setState() the
    // plugin silently used C++ field defaults (threshold=0.97, attack=0.5 ms,
    // release=200 ms) instead of the saved preset values — even though
    // paramValues[] were correctly populated from the state blob.
    // Transformations match runtimePolicySetLimiter* in
    // sid_runtime_parameter_services.h exactly.
    limiterEnabled   = paramValues[(size_t)kParamOutputLimiter] > 0.5f;
    limiterThreshold = std::clamp(paramValues[(size_t)kParamLimiterThreshold], 0.5f, 1.0f);
    limiterAttackMs  = std::clamp(paramValues[(size_t)kParamLimiterAttack], 0.0f, 1.0f) * 20.0f;
    limiterReleaseMs = 10.0f + std::clamp(paramValues[(size_t)kParamLimiterRelease], 0.0f, 1.0f) * 990.0f;
    limiter.setAttackMs(limiterAttackMs, sampleRate);
    limiter.setReleaseMs(limiterReleaseMs, sampleRate);
}

void ArpSIDProcessorPhase2::applyRenderModeOutputNormalization_(
        float* outL, float* outR, int numSamples,
        ArpSID::SidRuntimeRenderMode mode) noexcept {
    const SidVariantProfile variant = runtimeModel_.variantProfile();
    float trim = 1.0f;
    switch (variant.output_stage) {
        case SidOutputStageProfile::StockC64_6581: trim = 0.92f; break;
        case SidOutputStageProfile::StockC64_8580: trim = 1.00f; break;
        case SidOutputStageProfile::C64C_Modified: trim = 0.98f; break;
        case SidOutputStageProfile::DirectLineOut: trim = 1.00f; break;
        case SidOutputStageProfile::StudioCapture: trim = 1.00f; break;
        case SidOutputStageProfile::Custom:
        case SidOutputStageProfile::Unknown:
        default: trim = variant.is6581() ? 0.92f : 1.0f; break;
    }
    size_t modeIndex = 0;
    switch (mode) {
        case ArpSID::SidRuntimeRenderMode::SidRegister: modeIndex = 1; break;
        case ArpSID::SidRuntimeRenderMode::DrSid:       modeIndex = 2; break;
        case ArpSID::SidRuntimeRenderMode::BitPerfect:
        default: modeIndex = 0; break;
    }
    const float modeGain = renderModeOutputGain_[modeIndex];
    const float gain = trim * (std::isfinite(modeGain) ? modeGain : 1.0f);
    if (std::fabs(gain - 1.0f) > 1.0e-4f && outL) {
        const bool sharedOutputBus = (!outR || outR == outL);
        for (int i = 0; i < numSamples; ++i) {
            outL[i] *= gain;
            if (!sharedOutputBus) outR[i] *= gain;
        }
    }
}

// v910: the legacy double/floor timing helper cluster
// (sidCyclesPerSampleFloor/Max, absoluteSidCycleAtSampleStart_,
// clampActualSidCycleOffset_, mapCycleOffsetToActualSample_,
// absoluteSidCycleAtSampleOffset_, absoluteSidCycleToSampleOffset_,
// pushSidWriteDelayed) was REMOVED. It had zero production callers and
// carried a second double/floor timing law parallel to the canonical Q32
// runtime clock (sid_event_timing.h). Any future delayed-write need must
// go through the canonical fractional/Q32 helpers, never a re-derived
// floor(cycles-per-sample) law.
void ArpSIDProcessorPhase2::pushSidWriteTimed(
        uint8_t reg, uint8_t value, uint16_t sampleOff, uint16_t cycleOff) {
    ArpSID::pushRegisterShadowWrite(sidWriteQueue_(), sidQueuedShadow_(), reg, value, sampleOff, cycleOff);
}

void ArpSIDProcessorPhase2::pushSidWriteTimedIfChanged_(
        uint8_t reg, uint8_t value, uint16_t sampleOff, uint16_t cycleOff) {
    (void)ArpSID::pushRegisterShadowWriteIfChanged(sidWriteQueue_(), sidQueuedShadow_(), reg, value, sampleOff, cycleOff);
}

uint8_t ArpSIDProcessorPhase2::currentOrQueuedSidReg_(uint8_t reg) const noexcept {
    return ArpSID::currentOrShadowedSidReg(sidRegEngine_(), sidQueuedShadow_(), reg);
}

// Misc private helpers
// These helpers delegate to existing engine methods
void ArpSIDProcessorPhase2::recomputeGlobalAftertouch_() noexcept {
    float maxPressure = 0.0f;
    for (int __ch = 0; __ch < 16; ++__ch) {
        const float p = runtimeModel_.channelPressureNorm(__ch);
        maxPressure = std::max(maxPressure, p);
    }
    globalAftertouch_ = maxPressure;
}
float ArpSIDProcessorPhase2::notePressureForVoice_(int voiceIdx, int /*channel*/) const noexcept {
    // Return per-voice pressure from SynthMode voice state
    if (voiceIdx >= 0 && voiceIdx < (int)smVoices_().size()) {
        return std::clamp(smVoices_()[(size_t)voiceIdx].pressure, 0.0f, 1.0f);
    }
    return 0.0f;
}
bool  ArpSIDProcessorPhase2::notePressureExplicitForVoice_(int voiceIdx, int /*channel*/) const noexcept {
    if (voiceIdx >= 0 && voiceIdx < (int)smVoices_().size())
        return smVoices_()[(size_t)voiceIdx].pressureExplicit;
    return false;
}
void ArpSIDProcessorPhase2::clearPolyPressureForVoice_(int channel, int noteId) noexcept {
    for (auto& v : smVoices_()) {
        const bool chOk = (channel < 0) ? (v.channel < 0) : (v.channel == channel);
        const bool nidOk = (noteId < 0) ? (v.noteId < 0) : (v.noteId == noteId);
        if (v.active && chOk && nidOk) {
            v.pressure = 0.0f;
            v.pressureExplicit = false;
        }
    }
}
float ArpSIDProcessorPhase2::currentPitchBendSemis_() const noexcept {
    float maxAbs = 0.0f;
    float chosen = 0.0f;
    for (int ch = 0; ch < 16; ++ch) {
        const float v = currentPitchBendSemis_(ch);
        if (std::fabs(v) >= maxAbs) { maxAbs = std::fabs(v); chosen = v; }
    }
    return chosen;
}
float ArpSIDProcessorPhase2::currentPitchBendSemis_(int ch) const noexcept {
    if (ch < 0 || ch >= 16) return 0.0f;
    return runtimeModel_.pitchBendNorm(ch) * runtimeModel_.bendRangeSemis(ch);
}
void ArpSIDProcessorPhase2::applyLivePitchBend_(uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
    for (int ch = 0; ch < 16; ++ch)
        applyLivePitchBend_(ch, sampleOffset, cycleOffset);
}
void ArpSIDProcessorPhase2::applyLivePitchBend_(int channel, uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
    const int clampedChannel = std::clamp(channel, 0, 15);
    const float normBend = runtimeModel_.pitchBendNorm(clampedChannel);
    if (!std::isfinite(normBend)) return;
    const int raw14 = (int)std::lround((normBend * 0.5f + 0.5f) * 16383.0f);
    SidTimedEvent tev{};
    tev.type = SidTimedEventType::PitchBend;
    tev.arrival_order = 0u /*arrival_order set by merge*/;
    assignApproxIntraSampleTiming_(tev, static_cast<Steinberg::int32>(sampleOffset), tev.arrival_order, 0u, sampleRate, currentSidClockHz(), cycleOffset);
    tev.channel = static_cast<uint8_t>(clampedChannel);
    tev.data14 = static_cast<uint16_t>(std::clamp(raw14, 0, 16383));
    tev.value = normBend;
    tev.value_f32 = normBend;
    ArpSID::sidWrapperPushEvent(runtimeModel_, tev, std::max(1, currentProcessSamples_));
}
void ArpSIDProcessorPhase2::applyRealtimeSynthModeModState_(uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
    for (int ch = 0; ch < 16; ++ch) {
        applyLivePitchBend_(ch, sampleOffset, cycleOffset);
        applyLiveChannelPressureToSynthMode_(ch, sampleOffset, cycleOffset);
    }
}
void ArpSIDProcessorPhase2::fanoutChannelPressure_(float pressure) noexcept {
    const float clamped = std::clamp(pressure, 0.0f, 1.0f);
    globalAftertouch_ = clamped;
    for (int ch = 0; ch < 16; ++ch)
        runtimeModel_.setChannelPressureNorm(ch, clamped);
}

void ArpSIDProcessorPhase2::applyChannelPressure_(int channel, float pressure) noexcept {
    const float clamped = std::clamp(pressure, 0.0f, 1.0f);
    if (channel >= 0 && channel < 16)
        runtimeModel_.setChannelPressureNorm(channel, clamped);
    recomputeGlobalAftertouch_();
}
void ArpSIDProcessorPhase2::applyLiveChannelPressureToSynthMode_() noexcept {
    for (int ch = 0; ch < 16; ++ch)
        applyLiveChannelPressureToSynthMode_(ch, 0, 0);
}
void ArpSIDProcessorPhase2::applyLiveChannelPressureToSynthMode_(uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
    for (int ch = 0; ch < 16; ++ch)
        applyLiveChannelPressureToSynthMode_(ch, sampleOffset, cycleOffset);
}
void ArpSIDProcessorPhase2::applyLiveChannelPressureToSynthMode_(int channel, uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
    (void)channel;
    recomputeGlobalAftertouch_();
    const int clampedChannel = std::clamp(channel, 0, 15);
    ArpSID::applySynthModeChannelPressureToVoices(
        smVoices_(),
        sidWriteQueue_(),
        clampedChannel,
        runtimeModel_.channelPressureNorm(clampedChannel),
        sampleOffset,
        cycleOffset);
}
void ArpSIDProcessorPhase2::applyLiveDrSidPerformanceState_(int channel) noexcept {
    // F44: DrSID pitch bend — apply via engine API (was pending, now wired).
    const float bendSemis = currentPitchBendSemis_(channel);
    if (drSidEngine_() && std::isfinite(bendSemis)) {
        // DrSidEngine::setGlobalPitchBend if available, else adjust global tune
        // Use a ±2 semitone coarse shift clamped to the DrSID range
        const float clampedSemis = std::clamp(bendSemis, -12.f, 12.f);
        drSidEngine_()->setPerformancePitchBendSemis(clampedSemis);  // semitones relative to nominal
    }
}
void ArpSIDProcessorPhase2::fanoutPolyPressure_(int pitch, float pressure) noexcept {
    applyPolyPressure_(-1, pitch, -1, pressure);
}
void ArpSIDProcessorPhase2::applyPolyPressure_(int channel, int pitch, int noteId, float pressure) noexcept {
    // Token-first: update canonical token pressure registry.
    // This keeps tokenVoices[].polyPressure = focusedTokenPolyPressure() authoritative.
    {
        const uint64_t tok = runtimeModel_.resolveVoiceTokenForIdentity(
            static_cast<int16_t>(std::clamp(channel, 0, 15)),
            static_cast<int16_t>(std::clamp(pitch, 0, 127)),
            noteId);
        if (tok != 0) {
            runtimeModel_.bindPolyPressureToToken(tok, std::clamp(pressure, 0.0f, 1.0f));
        }
    }
    // Compat: also drive synth-mode voice pressure for register writes.
    // Pass the resolved token to the voice scan so it uses token-first path.
    const uint64_t voiceTok = runtimeModel_.resolveVoiceTokenForIdentity(
        static_cast<int16_t>(std::clamp(channel, 0, 15)),
        static_cast<int16_t>(std::clamp(pitch, 0, 127)), noteId);
    if (voiceTok != 0) {
        ArpSID::applySynthModePolyPressureToVoices(smVoices_(), sidWriteQueue_(),
            channel, pitch, noteId, pressure, 0u, 0u, voiceTok);
    }
}
void ArpSIDProcessorPhase2::fanoutPitchBend14_(int raw14) noexcept {
    for (int ch = 0; ch < 16; ++ch)
        bitPerfectEngine_()->setPitchBend14(ch, raw14);
}
void ArpSIDProcessorPhase2::applyPitchBend14_(int ch, int raw14) noexcept {
    bitPerfectEngine_()->setPitchBend14(ch, raw14);
}
void ArpSIDProcessorPhase2::applyRPNDataEntry_() noexcept {
    for (int ch = 0; ch < 16; ++ch)
        applyRPNDataEntry_(ch, 0, 0);
}
void ArpSIDProcessorPhase2::applyRPNDataEntry_(int channel) noexcept {
    applyRPNDataEntry_(channel, 0, 0);
}
void ArpSIDProcessorPhase2::applyRPNDataEntry_(int channel, uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
    const int clampedChannel = std::clamp(channel,0,15);
    if (runtimeModel_.pnNumber(clampedChannel) < 0) return;
    if (runtimeModel_.pnActive(clampedChannel) == 1 && runtimeModel_.pnNumber(clampedChannel) == 0 && runtimeModel_.pnDataMsb(clampedChannel) <= 48) {
        const float semis = (float)std::clamp(runtimeModel_.pnDataMsb(clampedChannel), 0, 48);
        SidTimedEvent rangeEv{};
        rangeEv.type = SidTimedEventType::AutomationPoint;
        rangeEv.arrival_order = 0u /*arrival_order set by merge*/;
        assignApproxIntraSampleTiming_(rangeEv, static_cast<Steinberg::int32>(sampleOffset), rangeEv.arrival_order, 0u, sampleRate, currentSidClockHz(), cycleOffset);
        rangeEv.target = static_cast<uint32_t>((int)kParamHostCtrlRpnMsbBase + clampedChannel);
        rangeEv.value = std::clamp(semis / 48.0f, 0.0f, 1.0f);
        rangeEv.value_f32 = rangeEv.value;
        ArpSID::sidWrapperPushEvent(runtimeModel_, rangeEv, std::max(1, currentProcessSamples_));
        applyLivePitchBend_(clampedChannel, sampleOffset, cycleOffset);
    }
}
void ArpSIDProcessorPhase2::synthModeSetSustainPedal_(int channel, bool on) noexcept {
    synthModeSetSustainPedal_(channel, on, 0, 0);
}

void ArpSIDProcessorPhase2::synthModeSetSustainPedal_(
        int channel, bool on, uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
    ArpSID::applySynthModeSustainPedal(
        synthVoicePolicy_(),
        smVoices_(),
        std::clamp(channel, 0, 15),
        on,
        sampleOffset,
        cycleOffset,
        [this](int voiceIdx, uint16_t off16, uint16_t cyc16, bool clearTracking) noexcept {
            hardSynthModeVoiceOff_(voiceIdx, off16, cyc16, clearTracking);
        });
}

void ArpSIDProcessorPhase2::synthModeSetSostenutoPedal_(
        int channel, bool on, uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
    ArpSID::applySynthModeSostenutoPedal(
        synthVoicePolicy_(),
        smVoices_(),
        std::clamp(channel, 0, 15),
        on,
        sampleOffset,
        cycleOffset,
        [this](int voiceIdx, uint16_t off16, uint16_t cyc16, bool clearTracking) noexcept {
            hardSynthModeVoiceOff_(voiceIdx, off16, cyc16, clearTracking);
        });
}

void ArpSIDProcessorPhase2::enqueueInternalRenderMidiEvent_(
        MidiKind kind, int pitch, float value,
        int32 sampleOffset, uint16_t cycleOffset,
        int channel, int noteId) noexcept {
    // FIX: internalRenderMidiEvents_ was removed as part of migration to canonical event path.
    // Route through the canonical SidTimedEvent ingress instead so internal synth-mode events
    // (e.g. from enqueueSynthModeInternalEvent) are dispatched correctly.
    ArpSID::SidTimedEvent ev{};
    ev.sample_offset = (sampleOffset >= 0)
        ? static_cast<uint32_t>(std::clamp<int32>(sampleOffset, 0, 65535))
        : ArpSID::kSidUnresolvedSampleOffset;
    ev.cycle_offset  = cycleOffset;
    ev.subphase      = 0;
    // Map MidiKind to SidTimedEvent type
    switch (kind) {
        case MidiKind::NoteOn:
            ev.type    = ArpSID::SidTimedEventType::MidiNoteOn;
            ev.pitch   = static_cast<int16_t>(std::clamp(pitch, 0, 127));
            ev.value   = std::clamp(value, 0.0f, 1.0f);
            ev.channel = (channel < 0)
                ? ArpSID::kSidUnresolvedChannel
                : static_cast<uint8_t>(std::clamp(channel, 0, 15));
            ev.noteId  = noteId;
            break;
        case MidiKind::NoteOff:
            ev.type    = ArpSID::SidTimedEventType::MidiNoteOff;
            ev.pitch   = static_cast<int16_t>(std::clamp(pitch, 0, 127));
            ev.value   = std::clamp(value, 0.0f, 1.0f);
            ev.channel = (channel < 0)
                ? ArpSID::kSidUnresolvedChannel
                : static_cast<uint8_t>(std::clamp(channel, 0, 15));
            ev.noteId  = noteId;
            break;
        default:
            // Non-note events: store as CC or ignore
            ev.type    = ArpSID::SidTimedEventType::MidiCC;
            ev.ccNum   = static_cast<uint8_t>(std::clamp(pitch, 0, 127));
            ev.value   = std::clamp(value, 0.0f, 1.0f);
            ev.channel = static_cast<uint8_t>(std::clamp(channel, 0, 15));
            break;
    }
    dispatchCanonicalTimedEvent_(ev);
}

void ArpSIDProcessorPhase2::enqueueSynthModeInternalEvent(
        MidiKind kind, int pitch, float value,
        int32 sampleOffset, uint16_t cycleOffset,
        int channel, int noteId) noexcept {
    enqueueInternalRenderMidiEvent_(kind, pitch, value, sampleOffset, cycleOffset, channel, noteId);
}

// Panic/reset helpers.
void ArpSIDProcessorPhase2::performFullPanicReset(bool) {
    ArpSID::runtimeRenderHostResetEngines(*this);
}
// ─── processLFOs ─────────────────────────────────────────────────────────────
// Advance all 4 LFOs by numSamples and cache their end-of-block values for the
// mod matrix. Called once per block from process() before canonical render.

void ArpSIDProcessorPhase2::processLFOs(int numSamples) {
    (void)numSamples;
    // Tempo-linked controller sync is owned centrally from process() through the
    // shared execution owner. Keep this leaf empty so the block is not advanced twice.
}

// Called once per block after LFOs are advanced and before renderAudio.
// Applies all live mod routes from the patch to the BitPerfect engine.
// This is the canonical mod-route application point for the VST path.
void ArpSIDProcessorPhase2::applyModRoutesThisBlock_() noexcept {
    if (bitPerfectEngine_()) {
        runtimeModel_.resolveRandomForCurrentScope();
        ArpSID::applyTypedModRoutesToBitPerfect(runtimeModel_, bitPerfectEngine_(), paramValues, lfoBank_());
        ArpSID::applyParamMatrixToBitPerfect(runtimeModel_, bitPerfectEngine_(), paramValues);
    }
    if (drSidEngine_()) {
        runtimeModel_.resolveRandomForCurrentScope();
        ArpSID::applyParamMatrixToDrSid(runtimeModel_, drSidEngine_(), paramValues);
    }
}


// ─── processSequencer ────────────────────────────────────────────────────────
// 32-step sequencer — fires note events from kParamSeqStep*Note/Vel/Gate
// into the active engine. Advances by step-clock derived from seqBpm.

void ArpSIDProcessorPhase2::processSequencer(int numSamples) {
    if (numSamples <= 0) return;
    const bool seqEnabled = ArpSID::sidEffectiveSeqAuthorityFromLiveParams(paramValues);
    const bool transportPlaying = runtimeModel_.seqHostWasPlaying();
    const bool followHostTempo = runtimeModel_.followHostTempoSeq();
    const int seqLength = ArpSID_normToSeqSteps(paramValues[(size_t)kParamSeqLength]);
    const float rawSeqSwing = paramValues[(size_t)kParamSeqSwing];
    const float seqSwing = std::clamp(std::isfinite(rawSeqSwing) ? rawSeqSwing : 0.0f, 0.0f, 1.0f);
    const float seqModeNorm = std::isfinite(paramValues[(size_t)kParamSeqMode]) ? paramValues[(size_t)kParamSeqMode] : 0.f;
    const int seqMode = std::clamp((int)std::lround(seqModeNorm * 3.f), 0, 3);
    const float rawSeqTempo = paramValues[(size_t)kParamSeqTempo];
    const float seqTempoParam = std::clamp(std::isfinite(rawSeqTempo) ? rawSeqTempo : 0.0f, 0.0f, 1.0f);
    if (!seqEnabled || (followHostTempo && !transportPlaying)) return;
    if (seqLength < 1) return;

    const double seqBpm = (double)ArpSID_normToSeqTempoBpm(seqTempoParam);
    const double effectiveBpm = (followHostTempo && runtimeModel_.hostTempoBpm() > 1.0f)
                                ? runtimeModel_.hostTempoBpm() : seqBpm;
    const double samplesPerStep = sampleRate * 60.0 / (effectiveBpm * 4.0);

    auto stepDuration = [&](int step) -> double {
        // FIX v571: Symmetric swing — odd+even pair must sum to 2×samplesPerStep
        // so the overall sequencer tempo is preserved at any swing amount.
        // OLD (wrong): even used * 0.5 * 0.5 = * 0.25, giving pair sum of
        // (1 + s*0.5) + (1 - s*0.25) = 2 + 0.25s (drifts slow at s>0).
        // NEW (correct): even mirrors odd with the same 0.5 coefficient, so
        // (1 + s*0.5) + (1 - s*0.5) = 2.0 ✓ at all swing values.
        const bool isOdd = (step & 1) != 0;
        const double swingFactor = (double)seqSwing * 0.5;
        return isOdd ? samplesPerStep * (1.0 + swingFactor)
                     : samplesPerStep * std::max(0.1, 1.0 - swingFactor);
    };

    if (!std::isfinite(runtimeModel_.seqSamplesUntilStep()) ||
        runtimeModel_.seqSamplesUntilStep() < 0.0) {
        runtimeModel_.setSeqSamplesUntilStep(0.0);
        runtimeModel_.setSeqSamplesUntilStep(0); // compat reset of sequencer countdown authority
    }

    int samplesRemaining = numSamples;
    while (samplesRemaining > 0) {
        if (runtimeModel_.seqSamplesUntilStep() <= 0.0) {
            const int stepOffset = numSamples - samplesRemaining;
            const int base = (int)kParamSeqStep1Note + runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step * 3;
            const float rawNoteNorm = paramValues[(size_t)base];
            const float rawVelNorm  = paramValues[(size_t)base + 1];
            const float rawGate     = paramValues[(size_t)base + 2];
            const float noteNorm = std::clamp(std::isfinite(rawNoteNorm) ? rawNoteNorm : 0.0f, 0.0f, 1.0f);
            const float velNorm = std::clamp(std::isfinite(rawVelNorm) ? rawVelNorm : 0.0f, 0.0f, 1.0f);
            const float gate = std::clamp(std::isfinite(rawGate) ? rawGate : 0.0f, 0.0f, 1.0f);

            if (runtimeModel_.seqLastNote() >= 0) {
                SidTimedEvent off{};
                off.type = SidTimedEventType::MidiNoteOff;
                assignApproxIntraSampleTiming_(off, stepOffset, 0u /*arrival_order set by merge*/, 3u, sampleRate, currentSidClockHz());
                off.pitch = (int16_t)std::clamp(runtimeModel_.seqLastNote(), 0, 127);
                ArpSID::sidWrapperPushEvent(runtimeModel_, off, numSamples);
                runtimeModel_.setSeqLastNote(-1);
            }

            if (gate > 0.5f) {
                // FIX v572: Canonical note formula — matches DSPKernel (AUv3) and UI.
                // OLD (wrong): noteNorm × 48 + 36 → range [36, 84] (4-octave C2-C6).
                // At default param 0.5 → MIDI 60 (Middle C). At param 0 → MIDI 36 (C2).
                // NEW (correct): noteNorm × 127 → full range [0, 127].
                // DSPKernel and UI both use round(param × 127). At param 0.5 → MIDI 64
                // (E4); Middle C (MIDI 60) maps to param 60/127 ≈ 0.4724.
                const int midiNote = std::clamp((int)std::lround(noteNorm * 127.0f), 0, 127);
                const float vel = std::clamp(velNorm, 0.0f, 1.0f);
                SidTimedEvent on{};
                on.type = SidTimedEventType::MidiNoteOn;
                assignApproxIntraSampleTiming_(on, stepOffset, 0u /*arrival_order set by merge*/, 3u, sampleRate, currentSidClockHz());
                on.pitch = (int16_t)midiNote;
                on.value = vel;
                ArpSID::sidWrapperPushEvent(runtimeModel_, on, numSamples);
                runtimeModel_.setSeqLastNote(midiNote);
            }

            const int len = std::clamp(seqLength, 1, 32);
            switch (seqMode) {
                case 0:
                    runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step = (runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step + 1) % len;
                    break;
                case 1:
                    runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step = (runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step - 1 + len) % len;
                    break;
                case 2:
                    runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step += runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_ping_dir;
                    if (runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step >= len)  { runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step = len - 2; runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_ping_dir = -1; }
                    if (runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step < 0)     { runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step = 1;       runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_ping_dir =  1; }
                    runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step = std::clamp(runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step, 0, len - 1);
                    break;
                case 3:
                    runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_rng_state ^= runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_rng_state << 13;
                    runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_rng_state ^= runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_rng_state >> 17;
                    runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_rng_state ^= runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_rng_state << 5;
                    runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step = (int)(runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_rng_state % (uint32_t)len);
                    break;
                default:
                    runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step = (runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step + 1) % len;
                    break;
            }
            runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_samples_until_step += stepDuration(runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().seq_step);
        }

        const int advance = std::min(samplesRemaining,
                                     (int)std::ceil(runtimeModel_.seqSamplesUntilStep()));
        runtimeModel_.setSeqSamplesUntilStep(runtimeModel_.seqSamplesUntilStep() - (double)advance);
        samplesRemaining    -= advance;
    }
}

bool ArpSIDProcessorPhase2::isTimingCriticalParam(Steinberg::Vst::ParamID id) const {
    return (id >= kParamSidRegD400 && id <= kParamSidRegD41D);
}
int  ArpSIDProcessorPhase2::timingPriorityForParam(Steinberg::Vst::ParamID id) const {
    if (id >= kParamSidRegD400 && id <= kParamSidRegD41D) return 3;
    if (id == kParamFilterCutoff || id == kParamFilterResonance ||
        id == kParamMasterVolume || id == kParamFilterMode) return 2;
    return 1;
}
int  ArpSIDProcessorPhase2::timingPriorityForMidi(const MidiEvent& me) const {
    // Note-offs before note-ons at same sample (prevents stuck notes on retrigger)
    if (me.kind == MidiKind::NoteOff)  return 4;
    if (me.kind == MidiKind::NoteOn)   return 3;
    if (me.kind == MidiKind::ControlChange)       return 2;
    if (me.kind == MidiKind::PolyPressure) return 1;
    return 0;
}
void ArpSIDProcessorPhase2::finalizeIntraSampleTiming() {
    sidWriteQueue_().sortStable();
}
void ArpSIDProcessorPhase2::applyTimedParamEvent(const TimedParamChange& ev) {
    if (runtimeExecutionOwner_) runtimeExecutionOwner_->applyProjectedNormalizedParameter(static_cast<uint32_t>(ev.id), ev.value);
}

void ArpSIDProcessorPhase2::pushSynthModeVoiceSeedRealtime_(int voiceIndex, bool gateOn, int sampleOffset, uint16_t cycleOffset) {
    if (voiceIndex < 0 || voiceIndex >= 3) return;
    const int base = voiceIndex * 7;
    const auto& sv = smVoices_()[(size_t)voiceIndex];
    const uint8_t ctrlNoGate = ArpSID::resolveSynthModeControlNoGate(paramValues.data(), sidQueuedShadow_().value.data(), voiceIndex);
    const uint8_t ctrl = static_cast<uint8_t>(ctrlNoGate | (gateOn ? 0x01u : 0x00u));
    const uint16_t freqReg = (sv.currentSidFreqReg != 0u) ? sv.currentSidFreqReg : sv.targetSidFreqReg;
    if (freqReg != 0u) {
        pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 0), static_cast<uint8_t>(freqReg & 0xFFu), static_cast<uint16_t>(sampleOffset), cycleOffset);
        pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 1), static_cast<uint8_t>(freqReg >> 8), static_cast<uint16_t>(sampleOffset), cycleOffset);
    }
    const int pwPid = (voiceIndex == 0) ? kParamVCO1PulseWidth
                   : (voiceIndex == 1) ? kParamVCO2PulseWidth
                                       : kParamVCO3PulseWidth;
    const float pwNorm = std::isfinite(paramValues[(size_t)pwPid])
        ? std::clamp(paramValues[(size_t)pwPid], 0.0f, 1.0f)
        : 0.5f;
    const uint16_t pw12 = static_cast<uint16_t>(std::clamp((int)std::lround(pwNorm * 4095.0f), 0, 4095));
    pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 2), static_cast<uint8_t>(pw12 & 0xFFu), static_cast<uint16_t>(sampleOffset), cycleOffset);
    pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 3), static_cast<uint8_t>((pw12 >> 8) & 0x0Fu), static_cast<uint16_t>(sampleOffset), cycleOffset);

    const uint8_t attNib = (uint8_t)std::clamp((int)std::lround(std::isfinite(paramValues[(size_t)kParamAttack])  ? paramValues[(size_t)kParamAttack]  * 15.f : 0.f), 0, 15);
    const uint8_t decNib = (uint8_t)std::clamp((int)std::lround(std::isfinite(paramValues[(size_t)kParamDecay])   ? paramValues[(size_t)kParamDecay]   * 15.f : 0.f), 0, 15);
    const uint8_t susNib = (uint8_t)std::clamp((int)std::lround(std::isfinite(paramValues[(size_t)kParamSustain]) ? paramValues[(size_t)kParamSustain] * 15.f : 0.f), 0, 15);
    const uint8_t relNib = (uint8_t)std::clamp((int)std::lround(std::isfinite(paramValues[(size_t)kParamRelease]) ? paramValues[(size_t)kParamRelease] * 15.f : 0.f), 0, 15);
    pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 5), static_cast<uint8_t>((attNib << 4) | decNib), static_cast<uint16_t>(sampleOffset), cycleOffset);
    pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 6), static_cast<uint8_t>((susNib << 4) | relNib), static_cast<uint16_t>(sampleOffset), cycleOffset);
    pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 4), ctrl, static_cast<uint16_t>(sampleOffset), cycleOffset);
}

uint8_t ArpSIDProcessorPhase2::computeSynthModeFilterRouteLowNibble_() const noexcept { return runtimeModel_.synthFilterRouteLowNibble(); }
void ArpSIDProcessorPhase2::pushSynthModeFilterRegsRealtime_(int sampleOffset, uint16_t cycleOffset) {
    const auto normOrZero = [](float v) noexcept -> float {
        return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
    };
    const float cutNorm = normOrZero(paramValues[(size_t)kParamFilterCutoff]);
    const float resNorm = normOrZero(paramValues[(size_t)kParamFilterResonance]);
    const float volNorm = normOrZero(paramValues[(size_t)kParamMasterVolume]);
    const float modNorm = normOrZero(paramValues[(size_t)kParamFilterMode]);
    const uint16_t fc   = (uint16_t)std::clamp((int)std::lround(cutNorm * 2047.f), 0, 2047);
    pushSidWriteTimedIfChanged_(0x15, (uint8_t)(fc & 0x07u),          (uint16_t)sampleOffset, cycleOffset);
    pushSidWriteTimedIfChanged_(0x16, (uint8_t)((fc >> 3) & 0xFFu),   (uint16_t)sampleOffset, cycleOffset);
    const uint8_t resNib  = (uint8_t)std::clamp((int)std::lround(resNorm * 15.f), 0, 15);
    pushSidWriteTimedIfChanged_(0x17, (uint8_t)((resNib << 4) | computeSynthModeFilterRouteLowNibble_()),(uint16_t)sampleOffset, cycleOffset);
    const uint8_t volNib  = (uint8_t)std::clamp((int)std::lround(volNorm * 15.f), 0, 15);
    const int modeIdx = (int)std::lround(modNorm * 2.f);
    const uint8_t modeBits = (modeIdx==0)?0x10u:(modeIdx==1)?0x20u:0x40u;
    pushSidWriteTimedIfChanged_(0x18, (uint8_t)(modeBits | volNib),    (uint16_t)sampleOffset, cycleOffset);
}
void ArpSIDProcessorPhase2::applyTargetedSynthModeParamToSidRegs_(
        Steinberg::Vst::ParamID pid, int sampleOffset, uint16_t cycleOffset) {
    auto pushVoiceSeed = [this, sampleOffset, cycleOffset](int voiceIndex) noexcept {
        const bool gateOn = (sidQueuedShadow_().value[(size_t)(voiceIndex * 7 + 4)] & 0x01u) != 0u;
        pushSynthModeVoiceSeedRealtime_(voiceIndex, gateOn, sampleOffset, cycleOffset);
    };
    switch (pid) {
        case kParamFilterCutoff: case kParamFilterResonance:
        case kParamFilterMode:   case kParamMasterVolume:
            pushSynthModeFilterRegsRealtime_(sampleOffset, cycleOffset); break;
        case kParamVCO1Waveform:
        case kParamVCO1RingModEnable:
        case kParamVCO1PulseWidth:
            pushVoiceSeed(0); break;
        case kParamVCO2Waveform:
        case kParamVCO2SyncEnable:
        case kParamVCO2RingModEnable:
        case kParamVCO2PulseWidth:
            pushVoiceSeed(1); break;
        case kParamVCO3Waveform:
        case kParamVCO3PulseWidth:
            pushVoiceSeed(2); break;
        case kParamAttack: case kParamDecay: case kParamSustain: case kParamRelease:
            pushVoiceSeed(0); pushVoiceSeed(1); pushVoiceSeed(2); break;
        default: break;
    }
}

void ArpSIDProcessorPhase2::reseedSynthModeRealtimeState_(int sampleOffset, uint16_t cycleOffset, bool activeOnly) noexcept {
    syncSidSystemModelFromParams(false);
    syncSidQueuedShadowFromLive_();
    sanitizeSynthModeQueuedShadowLocal_(sidQueuedShadow_(), paramValues.data(), sidSysByteCached);
    pushSynthModeFilterRegsRealtime_(sampleOffset, cycleOffset);
    for (int v = 0; v < 3; ++v) {
        const auto& sv = smVoices_()[(size_t)v];
        if (activeOnly && !sv.active) continue;
        const bool gateOn = activeOnly ? (sv.active && (sv.keyDown || sv.sustained || sv.sostenutoLatched)) : false;
        pushSynthModeVoiceSeedRealtime_(v, gateOn, sampleOffset, cycleOffset);
    }
}

void ArpSIDProcessorPhase2::resetSynthHeldState_() noexcept {
    synthVoicePolicy_().clearHeldNotes();
}

void ArpSIDProcessorPhase2::synthHeldNoteOn_(int note, float vel, int ch, int nid) noexcept {
    synthVoicePolicy_().trackHeldNoteOn(note, vel, ch, nid);
}

void ArpSIDProcessorPhase2::synthHeldNoteOff_(int note, int ch, int noteId) noexcept {
    synthVoicePolicy_().trackHeldNoteOff(note, ch, noteId);
}

int ArpSIDProcessorPhase2::synthHeldTopNote_() const noexcept {
    return synthVoicePolicy_().topHeldNote();
}

void ArpSIDProcessorPhase2::seedSynthHeldFromCurrentVoices_() noexcept {
    synthVoicePolicy_().rebuildHeldFromVoices(synthModeVoiceMode_(), smVoices_());
}

void ArpSIDProcessorPhase2::synthModeResolveForcedStack_(uint16_t off16, uint16_t cyc16, bool retrig) noexcept {
    const auto* topEntry = synthVoicePolicy_().priorityHeldNote();
    if (!topEntry) return;

    const int note = topEntry->identity.midi_note;
    const float vel = topEntry->velocity;
    const int channel = topEntry->identity.channel;
    const int noteId = topEntry->identity.note_id;

    if (retrig || (smVoices_()[0].midiNote != note) || (smVoices_()[0].channel != channel) || (smVoices_()[0].noteId != noteId)) {
        synthModeNoteOn(note, vel, static_cast<int>(off16), cyc16, channel, noteId);
        return;
    }

    const uint16_t freq = ArpSID::canonicalSidFrequencyRegisterForMidiNote(static_cast<double>(note), currentSidClockHz());
    pushSidWriteTimed(static_cast<uint8_t>(0), static_cast<uint8_t>(freq & 0xFFu), off16, cyc16);
    pushSidWriteTimed(static_cast<uint8_t>(1), static_cast<uint8_t>((freq >> 8) & 0xFFu), off16, cyc16);
    smVoices_()[0].midiNote = note;
    smVoices_()[0].channel = channel;
    smVoices_()[0].noteId = noteId;
}

void ArpSIDProcessorPhase2::scheduleSynthModeGlideWrites_(int numSamples) noexcept {
    ArpSID::scheduleSynthModeGlideWrites(
        smVoices_(), sidWriteQueue_(), numSamples, sampleRate, currentSidClockHz());
}
bool ArpSIDProcessorPhase2::sidShadowHasValueAtOrBefore_(
        uint8_t regIndex, uint8_t expectedValue,
        uint16_t sampleOffset, uint16_t cycleOffset) const noexcept {
    return ArpSID::registerShadowMatchesAtOrBefore(sidQueuedShadow_(), regIndex, expectedValue, sampleOffset, cycleOffset);
}

void ArpSIDProcessorPhase2::applyNRPN(int nrpnNum, int raw14) {
    applyNRPN(0, nrpnNum, raw14, 0, 0);
}
void ArpSIDProcessorPhase2::applyNRPN(int channel, int nrpnNum, int raw14,
                                       uint16_t sampleOffset, uint16_t cycleOffset) {
    const float norm = std::clamp((float)raw14 / 16383.0f, 0.f, 1.f);
    if (nrpnNum >= 1 && nrpnNum <= kNumParams) {
        const auto pid = (Steinberg::Vst::ParamID)(nrpnNum - 1);
        SidTimedEvent tev{};
        tev.type = SidTimedEventType::AutomationPoint;
        tev.arrival_order = 0u /*arrival_order set by merge*/;
        assignApproxIntraSampleTiming_(tev, static_cast<Steinberg::int32>(sampleOffset), tev.arrival_order, 0u, sampleRate, currentSidClockHz(), cycleOffset);
        tev.target = static_cast<uint32_t>(pid);
        tev.value = norm;
        tev.value_f32 = norm;
        ArpSID::sidWrapperPushEvent(runtimeModel_, tev, std::max(1, currentProcessSamples_));
        reportRawMidiTouchedParam(pid, norm);
        if (nrpnNum == 1 && raw14 <= 48*128)
            runtimeModel_.setBendRangeSemis(std::clamp(channel,0,15), (float)(raw14 >> 7));
    }
}
void ArpSIDProcessorPhase2::reportRawMidiTouchedParam(Steinberg::Vst::ParamID pid, float value) {
    // Mark parameter as MIDI-touched for host recording/automation capture without
    // mutating the live normalized parameter array outside the canonical timed path.
    if (pid >= (Steinberg::Vst::ParamID)kNumParams) return;
    const float clamped = ArpSID::sanitizeNormalizedParamValue(static_cast<int>(pid), value, ArpSID::defaultNormalizedParamValue(static_cast<int>(pid)));
    if (pendingParams.size() < pendingParams.capacity())
        pendingParams.push_back({pid, clamped});
}
void ArpSIDProcessorPhase2::queueMappedParamFromRawCC(
        Steinberg::Vst::ParamID pid, float value, Steinberg::int32 sampleOffset, uint32_t rawOrder) {
    if (pid >= (Steinberg::Vst::ParamID)kNumParams) return;
    const float clamped = ArpSID::sanitizeNormalizedParamValue(static_cast<int>(pid), value, ArpSID::defaultNormalizedParamValue(static_cast<int>(pid)));
    SidTimedEvent tev{};
    tev.type = SidTimedEventType::AutomationPoint;
    assignApproxIntraSampleTiming_(tev, sampleOffset, rawOrder, 1u, sampleRate, currentSidClockHz());
    tev.target = static_cast<uint32_t>(pid);
    tev.value = clamped;
    tev.value_f32 = clamped;
    ArpSID::sidWrapperPushEvent(runtimeModel_, tev, std::max(1, currentProcessSamples_));
    reportRawMidiTouchedParam(pid, clamped);
}
void ArpSIDProcessorPhase2::toggleMappedParamFromRawCC(
        Steinberg::Vst::ParamID pid, Steinberg::int32 sampleOffset, uint32_t rawOrder) {
    if (pid >= (Steinberg::Vst::ParamID)kNumParams) return;
    const float toggled = (paramValues[(size_t)pid] > 0.5f) ? 0.0f : 1.0f;
    queueMappedParamFromRawCC(pid, toggled, sampleOffset, rawOrder);
}
void ArpSIDProcessorPhase2::handleMappedRawCC(
        uint8_t ccNum, uint8_t val, Steinberg::int32 sampleOffset, uint32_t& rawOrderCounter,
        int channel) {
    // F40: add per-channel offset so CC data from different channels is kept separate
    const int ch = std::clamp(channel, 0, 15);
    const float norm = (float)val / 127.0f;
    const uint32_t order = rawOrderCounter++;

    switch (ccNum) {
        case 1:  queueMappedParamFromRawCC((ParamID)((int)kParamHostCtrlModWheelBase  + ch), norm, sampleOffset, order); break;
        case 2:  queueMappedParamFromRawCC((ParamID)((int)kParamHostCtrlBreathBase    + ch), norm, sampleOffset, order); break;
        case 4:
        case 11: queueMappedParamFromRawCC((ParamID)((int)kParamHostCtrlExpressionBase+ ch), norm, sampleOffset, order); break;
        case 7:  queueMappedParamFromRawCC(kParamMasterVolume,                              norm, sampleOffset, order); break;
        case 64: queueMappedParamFromRawCC((ParamID)((int)kParamHostCtrlSustainBase   + ch), norm, sampleOffset, order); break;
        case 65: // portamento on/off
            queueMappedParamFromRawCC(kParamPortamentoTime, (val >= 64) ? 0.3f : 0.0f, sampleOffset, order); break;
        case 66: queueMappedParamFromRawCC((ParamID)((int)kParamHostCtrlSostenutoBase + ch), norm, sampleOffset, order); break;
        case 67: // soft pedal → filter duck
            queueMappedParamFromRawCC(kParamFilterCutoff,
                std::clamp(paramValues[(size_t)kParamFilterCutoff] * (1.0f - norm * 0.4f), 0.0f, 1.0f),
                sampleOffset, order); break;
        case 70: case 71: case 72: case 73: case 74: case 75: case 76: case 77:
            queueMappedParamFromRawCC(ArpSID::sidMappedRealtimeCcParam(static_cast<uint8_t>(ccNum)), norm, sampleOffset, order);
            break;
        case 120: case 123: // all sound off / all notes off
            kernelAllNotesOff();
            for (int i=0;i<3;++i) if(smVoices_()[(size_t)i].active)
                hardSynthModeVoiceOff_(i,0u,0u,true);
            break;
        default: {
            // NRPN accumulation: CC 99=NRPN MSB, CC 98=NRPN LSB, CC 6=data MSB, CC 38=data LSB
            const int curNum = runtimeModel_.pnNumber(ch);
            const int curActive = runtimeModel_.pnActive(ch);
            const int curMsb = runtimeModel_.pnDataMsb(ch);
            const int curLsb = runtimeModel_.pnDataLsb(ch);
            if (ccNum == 99) { runtimeModel_.setPnState(ch, 2, val << 7, curMsb, curLsb); }
            else if (ccNum == 98) { runtimeModel_.setPnState(ch, curActive, (curNum & ~0x7F) | val, curMsb, curLsb); }
            else if (ccNum == 101) { runtimeModel_.setPnState(ch, 1, val << 7, curMsb, curLsb); }
            else if (ccNum == 100) { runtimeModel_.setPnState(ch, curActive, (curNum & ~0x7F) | val, curMsb, curLsb); }
            else if (ccNum == 6)  { runtimeModel_.setPnState(ch, curActive, curNum, val, curLsb); }
            else if (ccNum == 38) {
                runtimeModel_.setPnState(ch, curActive, curNum, curMsb, val);
                const int raw14 = (runtimeModel_.pnDataMsb(ch) << 7) | runtimeModel_.pnDataLsb(ch);
                if (runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().pn_active[(size_t)ch] == 2)
                    applyNRPN(ch, runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().pn_number[(size_t)ch], raw14, (uint16_t)sampleOffset, 0);
                else if (runtimeModel_.dynamicStateInternalForCanonicalRuntimeOnly().pn_active[(size_t)ch] == 1)
                    applyRPNDataEntry_(ch, (uint16_t)sampleOffset, 0);
            }
            break;
        }
    }
}

int ArpSIDProcessorPhase2::synthModeVoiceMode_() const noexcept {
    return ArpSID::canonicalVoiceModeIndexFromNormalized(paramValues[(size_t)kParamVoiceMode]);
}

} // namespace ArpSID

namespace ArpSID {

void ArpSIDProcessorPhase2::runtimeDispatchPitchBend14(int channel, int raw14) noexcept {
    ArpSID::SidTimedEvent ev{};
    ev.type = SidTimedEventType::PitchBend;
    ev.channel = static_cast<uint8_t>(std::clamp(channel, 0, 15));
    ev.data14 = static_cast<uint16_t>(std::clamp(raw14, 0, 16383));
    ev.value = std::clamp((static_cast<float>(ev.data14) - 8192.0f) / 8192.0f, -1.0f, 1.0f);
    ev.value_f32 = ev.value;
    runtimeDispatchPitchBendEvent(ev);
}

void ArpSIDProcessorPhase2::runtimeDispatchPitchBendEvent(const ArpSID::SidTimedEvent& ev) noexcept {
    const int channel = std::clamp<int>(ev.channel, 0, 15);
    const int raw14 = ArpSID::canonicalPitchBend14FromEvent(ev);
    lastHostChannel_ = static_cast<uint8_t>(channel);
    ArpSID::runtimeHandleRenderedPitchBend(*this,
                                           static_cast<uint8_t>(channel),
                                           static_cast<int16_t>(std::clamp(raw14 - 8192, -8192, 8191)));
    const float bendSemis = currentPitchBendSemis_(channel);
    bool anySynthVoiceForChannel = false;
    for (const auto& sv : smVoices_()) {
        if (sv.active && std::clamp<int>(sv.channel, 0, 15) == channel) {
            anySynthVoiceForChannel = true;
            break;
        }
    }
    if (anySynthVoiceForChannel) {
        ArpSID::applySynthModePitchBendToVoices(smVoices_(), sidWriteQueue_(), currentSidClockHz(), bendSemis, channel, 0u, 0u);
    } else if (resolveTopLevelRenderMode_() == ArpSID::SidRuntimeRenderMode::SidRegister) {
        const double bendMul = std::exp2(static_cast<double>(bendSemis) / 12.0);
        for (int voice = 0; voice < 3; ++voice) {
            const size_t base = static_cast<size_t>(voice * 7);
            const uint8_t curLo = ArpSID::currentOrShadowedSidReg(sidRegEngine_(), sidQueuedShadow_(), static_cast<uint8_t>(base + 0));
            const uint8_t curHi = ArpSID::currentOrShadowedSidReg(sidRegEngine_(), sidQueuedShadow_(), static_cast<uint8_t>(base + 1));
            const uint16_t unbent = static_cast<uint16_t>(curLo | (static_cast<uint16_t>(curHi) << 8));
            const uint16_t bent = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(std::lround(static_cast<double>(unbent) * bendMul)), 0, 65535));
            const uint8_t lo = static_cast<uint8_t>(bent & 0xFFu);
            const uint8_t hi = static_cast<uint8_t>((bent >> 8) & 0xFFu);
            sidRegEngine_().write(static_cast<uint8_t>(base + 0), lo);
            sidRegEngine_().write(static_cast<uint8_t>(base + 1), hi);
            sidQueuedShadow_().value[base + 0] = lo;
            sidQueuedShadow_().value[base + 1] = hi;
            sidQueuedShadow_().valid[base + 0] = sidQueuedShadow_().valid[base + 1] = 1u;
            sidQueuedShadow_().sample[base + 0] = sidQueuedShadow_().sample[base + 1] = 0u;
            sidQueuedShadow_().cycle[base + 0] = sidQueuedShadow_().cycle[base + 1] = 0u;
        }
    }
    applyLiveDrSidPerformanceState_(channel);
}

void ArpSIDProcessorPhase2::runtimeDispatchPolyPressure(int channel, int pitch, int value7) noexcept {
    ArpSID::SidTimedEvent ev{};
    ev.type = SidTimedEventType::PolyPressure;
    ev.channel = static_cast<uint8_t>(std::clamp(channel, 0, 15));
    ev.pitch = static_cast<int16_t>(std::clamp(pitch, 0, 127));
    ev.value = std::clamp((float)value7 / 127.0f, 0.0f, 1.0f);
    runtimeDispatchPolyPressureEvent(ev);
}

void ArpSIDProcessorPhase2::runtimeDispatchPolyPressureEvent(const ArpSID::SidTimedEvent& ev) noexcept {
    const int channel = std::clamp<int>(ev.channel, 0, 15);
    const int pitch = std::clamp<int>(ev.pitch, 0, 127);
    lastHostChannel_ = static_cast<uint8_t>(channel);
    const float pressure = std::clamp(std::isfinite(ev.value) ? ev.value
                                                          : static_cast<float>(ArpSID::canonicalMidi7FromEventValue(ev)) / 127.0f,
                                      0.0f,
                                      1.0f);
    runtimeModel_.setLastPolyPressureNote(channel, pitch);
    runtimeModel_.setLastNoteState(pitch, channel, runtimeModel_.lastNoteVelocity());
    applyPolyPressure_(channel, pitch, ev.noteId, pressure);
}

void ArpSIDProcessorPhase2::runtimeDispatchChannelPressure(int channel, int value7) noexcept {
    ArpSID::SidTimedEvent ev{};
    ev.type = SidTimedEventType::ChannelPressure;
    ev.channel = static_cast<uint8_t>(std::clamp(channel, 0, 15));
    ev.value = std::clamp((float)value7 / 127.0f, 0.0f, 1.0f);
    runtimeDispatchChannelPressureEvent(ev);
}

void ArpSIDProcessorPhase2::runtimeDispatchChannelPressureEvent(const ArpSID::SidTimedEvent& ev) noexcept {
    const int channel = std::clamp<int>(ev.channel, 0, 15);
    lastHostChannel_ = static_cast<uint8_t>(channel);
    const uint8_t pressure7 = static_cast<uint8_t>(ArpSID::canonicalMidi7FromEventValue(ev));
    ArpSID::runtimeHandleRenderedChannelPressure(*this, static_cast<uint8_t>(channel), pressure7);
    applyChannelPressure_(channel, runtimeModel_.channelPressureNorm(channel));
    if (resolveTopLevelRenderMode_() == ArpSID::SidRuntimeRenderMode::SidRegister)
        applyLiveChannelPressureToSynthMode_(channel, 0u, 0u);
    if (drSidEngine_()) drSidEngine_()->setPerformancePressure(runtimeModel_.channelPressureNorm(channel));
}

void ArpSIDProcessorPhase2::runtimeBitPerfectSetSustainPedal(int channel, bool on) noexcept {
    if (bitPerfectEngine_()) bitPerfectEngine_()->setSustainPedal(std::clamp(channel, 0, 15), on);
}

void ArpSIDProcessorPhase2::runtimeBitPerfectSetSostenutoPedal(int channel, bool on) noexcept {
    if (bitPerfectEngine_()) bitPerfectEngine_()->setSostenutoPedal(std::clamp(channel, 0, 15), on);
}

void ArpSIDProcessorPhase2::runtimeSetSustainState(int channel, bool on) noexcept {
    const int ch = std::clamp(channel, 0, 15);
    runtimeModel_.setSustainState(ch, on);
    pedalState_().setSustain(ch, on);
    runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlSustainBase) + ch), on ? 1.0f : 0.0f);
}

void ArpSIDProcessorPhase2::runtimeSetSostenutoState(int channel, bool on) noexcept {
    const int ch = std::clamp(channel, 0, 15);
    runtimeModel_.setSostenutoState(ch, on);
    pedalState_().setSostenuto(ch, on);
    runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlSostenutoBase) + ch), on ? 1.0f : 0.0f);
}

void ArpSIDProcessorPhase2::runtimeVoicePolicySetSustain(int channel, bool on) noexcept {
    synthModeSetSustainPedal_(std::clamp(channel, 0, 15), on, 0, 0);
}

void ArpSIDProcessorPhase2::runtimeVoicePolicySetSostenuto(int channel, bool on) noexcept {
    synthModeSetSostenutoPedal_(std::clamp(channel, 0, 15), on, 0, 0);
}

void ArpSIDProcessorPhase2::runtimeHardSynthAllNotesOffChannel(int channel) noexcept {
    if (auto* vp = runtimeVoicePolicy()) {
        if (channel < 0) {
            vp->clearAllNotesOffStateOnly();
        } else {
            vp->allNotesOffChannel(std::clamp(channel, 0, 15));
        }
    }
    for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
        const auto& v = smVoices_()[(size_t)i];
        if (!v.active) continue;
        if (channel >= 0 && v.channel >= 0 && v.channel != channel) continue;
        hardSynthModeVoiceOff_(i, 0u, 0u, true);
    }
}

void ArpSIDProcessorPhase2::runtimeApplySoftPedal(int channel, bool on) noexcept {
    ArpSID::runtimeRenderHostApplySoftPedal(*this, channel, on, engineBank_.hostSurface.softPedalSavedCutoff);
}

void ArpSIDProcessorPhase2::runtimeDispatchMidiCC(int channel, int cc, int value7) noexcept {
    if (channel < 16 && channel >= 0) lastHostChannel_ = static_cast<uint8_t>(channel);
    const int ch = std::clamp(channel, 0, 15);
    const int v7 = std::clamp(value7, 0, 127);
    ArpSID::runtimeHandleRenderedCC(*this,
                                    static_cast<uint8_t>(ch),
                                    static_cast<uint8_t>(std::clamp(cc, 0, 127)),
                                    static_cast<uint8_t>(v7));
    const float norm = std::clamp((float)v7 / 127.0f, 0.0f, 1.0f);
    const auto mappedRealtimeCcParam = [](int ccNum) noexcept -> ParamID {
        return ArpSID::sidMappedRealtimeCcParam(static_cast<uint8_t>(std::clamp(ccNum, 0, 127)));
    };
    if (cc == 1) {
        srcModWheel = norm;
        srcModWheelFromCC_ = true;
        if (drSidEngine_()) drSidEngine_()->setPerformanceModWheel(srcModWheel);
    } else if (cc == 11) {
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->applyProjectedNormalizedParameter(kParamMasterVolume, norm);
    } else {
        const ParamID mapped = mappedRealtimeCcParam(cc);
        if (mapped != kNumParams && runtimeExecutionOwner_) runtimeExecutionOwner_->applyProjectedNormalizedParameter(mapped, norm);
    }
    const auto ccRequiresImmediateProjection = [&](int ccNum) noexcept {
        return ArpSID::sidCcRequiresImmediateAudioProjection(static_cast<uint8_t>(std::clamp(ccNum, 0, 127)));
    };
    if (ccRequiresImmediateProjection(cc)) {
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(false);
    }
    if (resolveTopLevelRenderMode_() == ArpSID::SidRuntimeRenderMode::SidRegister) {
        switch (cc) {
            case 1:
            case 2:
            case 7:
            case 11:
            case 64:
            case 66:
                reseedSynthModeRealtimeState_(0, 0u, true);
                applyLiveChannelPressureToSynthMode_(0u, 0u);
                {
                    bool activeChannels[16]{};
                    for (const auto& sv : smVoices_()) {
                        if (!sv.active) continue;
                        activeChannels[std::clamp<int>(sv.channel, 0, 15)] = true;
                    }
                    bool anyActive = false;
                    for (int bendCh = 0; bendCh < 16; ++bendCh) {
                        if (!activeChannels[bendCh]) continue;
                        anyActive = true;
                        ArpSID::applySynthModePitchBendToVoices(smVoices_(), sidWriteQueue_(), currentSidClockHz(), currentPitchBendSemis_(bendCh), bendCh, 0u, 0u);
                    }
                    if (!anyActive)
                        ArpSID::applySynthModePitchBendToVoices(smVoices_(), sidWriteQueue_(), currentSidClockHz(), currentPitchBendSemis_(ch), ch, 0u, 0u);
                }
                break;
            default:
                break;
        }
    }
}

} // namespace ArpSID
