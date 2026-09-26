// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include "au3/ArpSIDCanonicalEvents.h"
#include "arpsid/core/sid_event_timing.h"
#include "arpsid/core/sid_runtime_synth_performance.h"
#include "arpsid/core/sid_runtime_model.h"
#include "arpsid/core/sid_postfx_timeline.h"
#include "arpsid/core/sid_render_pipeline_capabilities.h"
#include "arpsid/core/sid_runtime_execution.h"
#include "arpsid/core/sid_runtime_target_adapter.h"
#include "arpsid/core/sid_runtime_engine_ops.h"
#include "arpsid/core/sid_runtime_shared_kernel.h"
#include "arpsid/core/sid_runtime_kernel_dispatch.h"
#include "arpsid/core/sid_runtime_engine_bank.h"
#include "arpsid/core/sid_runtime_render_host.h"
#include "arpsid/core/sid_runtime_fractional_render.h"
#include "arpsid/core/sid_state_codec.h"
#include "arpsid/core/sid_ownership_mailbox.h"
#include "arpsid/core/sid_state_blob_slot.h"
#include "arpsid/core/sid_runtime_voice_policy.h"
#include "arpsid/core/sid_audio_processors.h"
#include "arpsid/core/sid_hifi_transcendence.h"
// Pass P: canonical policy files — VST wrapper must use these, not own the laws
#include "arpsid/core/sid_runtime_reset_policy.h"
#include "arpsid/core/sid_runtime_state_apply_policy.h"
#include "arpsid/core/sid_runtime_event_materializer.h"
#include "arpsid/core/sid_runtime_held_replay.h"
#include "arpsid/core/sid_runtime_mod_ops.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "parameter_ids.h"

#include "arpsid/engines/bitperfect_engine.h"
#include "arpsid/engines/arpeggiator.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/modulation/lfo.h"

#include <memory>
#include <mutex>
#include <cstdint>
#include <array>
#include <algorithm>
#include <cmath>
#include <vector>
#include <utility>
#include <atomic>
#include <cassert>
#include <functional>
#include "arpsid_telemetry_iface.h"
#include "gui/arpsid_vst_cocoa_bridge.h"

namespace ArpSID {

// Forward declaration
extern const Steinberg::FUID ProcessorUID;
extern const Steinberg::FUID ControllerUID;

// Total parameters for Phase 4

/**
 * ArpSID Processor - Phase 2
 * Complete polyphonic synthesizer with arpeggiator_() and drums
 */
class ArpSIDProcessorPhase2 : public Steinberg::Vst::AudioEffect, public IArpSIDTelemetryProvider {
public:
    ArpSIDProcessorPhase2();
    ~ArpSIDProcessorPhase2() override;
    
    // Factory method
    static Steinberg::FUnknown* createInstance(void*) {
        return (Steinberg::Vst::IAudioProcessor*)new ArpSIDProcessorPhase2;
    }
    
    // IPluginBase
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid, void** obj) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    
    // IAudioProcessor
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs,
        Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs,
        Steinberg::int32 numOuts) override;

    Steinberg::tresult PLUGIN_API activateBus(
        Steinberg::Vst::MediaType type,
        Steinberg::Vst::BusDirection dir,
        Steinberg::int32 index,
        Steinberg::TBool state) override;
    
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& setup) override;
    
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    
    Steinberg::tresult PLUGIN_API process(
        Steinberg::Vst::ProcessData& data) override;
    
    
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
// IComponent - State management
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    
    // FIX BUG-0005: Report the SIDChip/SidRegisterEngine lookahead limiter delay.
    // The internal limiters use a ~5 ms lookahead buffer (sampleRate * 0.005 samples).
    // Without reporting this, host latency compensation is broken and all ArpSID
    // tracks are ~5 ms late relative to other instruments in a mix.
    Steinberg::uint32 PLUGIN_API getLatencySamples() override {
        const double sr = (std::isfinite(sampleRate) && sampleRate > 1000.0) ? sampleRate : 44100.0;
        return static_cast<Steinberg::uint32>(std::clamp(
            static_cast<int>(std::lround(sr * 0.005)), 1, 1023));
    }

    // getTailSamples: report a conservative release+FX tail so hosts do not
    // truncate SynthMode, BitPerfect, or DrSID note releases during bounce/freeze.
    Steinberg::uint32 PLUGIN_API getTailSamples() override {
        const double sr = (std::isfinite(sampleRate) && sampleRate > 1000.0) ? sampleRate : 44100.0;
        const double scale = sr / 44100.0;

        // Conservative release estimate for all musical modes.
        // In mirrored SynthMode we use the top-level release param; in direct-register
        // patches we also scan the raw SID SR registers so hosts do not cut tails just
        // because the high-level ADSR controls are no longer authoritative.
        const float releaseNorm = std::clamp(paramValues[kParamRelease], 0.0f, 1.0f);
        uint8_t directReleaseNib = 0u;
        for (int pid : { (int)kParamSidRegD406, (int)kParamSidRegD40D, (int)kParamSidRegD414 }) {
            const float n = std::clamp(paramValues[(size_t)pid], 0.0f, 1.0f);
            const uint8_t srByte = (uint8_t)std::clamp((int)std::lround(n * 255.0f), 0, 255);
            directReleaseNib = std::max<uint8_t>(directReleaseNib, (uint8_t)(srByte & 0x0Fu));
        }
        const double directReleaseSec = 0.050 + std::exp(std::log(std::max(1.0e-9, (double)directReleaseNib / 15.0)) * 1.7) * 7.5;
        const double synthReleaseSec = std::max(0.080 + std::exp(std::log(std::max(1.0e-9, (double)releaseNorm)) * 1.7) * 7.5,
                                                directReleaseSec);
        const double drumTailSec = (paramValues[kParamDrSidEnable] > 0.5f) ? 2.5 : 0.0;

        double reverbTailSec = 0.0;
        if (reverbMix > 1.0e-4f) {
            const int combMax = static_cast<int>(std::lround(1379.0 * scale));
            const int apMax   = static_cast<int>(std::lround((579 + 464) * scale));
            reverbTailSec = (double)((combMax + apMax) * 6) / sr;
        }

        const double totalSec = std::max({synthReleaseSec, drumTailSec, reverbTailSec, 0.050});
        const uint64_t totalSamples = static_cast<uint64_t>(std::llround(totalSec * sr));
        return static_cast<Steinberg::uint32>(std::min<uint64_t>(totalSamples, 0x7fffffffu));
    }

    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) override;

    // Explicit telemetry provider interface; coherent snapshots only.
    Steinberg::TBool PLUGIN_API arpGetLatestSnapshot(MeterSnapshot& out) noexcept override;
    Steinberg::TBool PLUGIN_API arpGetTelemetryShadowSnapshot(MeterSnapshot& out) noexcept override;
    Steinberg::TBool PLUGIN_API
    arpGetLatestFullTelemetry(ArpSIDTelemetry& out,
                              Steinberg::TBool includeScopes) noexcept override;

    // GUI polling must use coherent snapshots, never the raw ring.
    bool getLatestSnapshot(MeterSnapshot& out) const noexcept;
    bool getTelemetryShadowSnapshot(MeterSnapshot& out) const noexcept;
    bool getLatestFullTelemetry(ArpSIDTelemetry& out) const noexcept;

    ArpSID::SidRuntimeModel& runtimeModel() noexcept { return runtimeCore_.runtimeModel; }
    const ArpSID::SidRuntimeModel& runtimeModel() const noexcept { return runtimeCore_.runtimeModel; }
    ArpSID::SidRuntimeHostSurface& runtimeHostSurface() noexcept { return engineBank_.hostSurface; }
    const ArpSID::SidRuntimeHostSurface& runtimeHostSurface() const noexcept { return engineBank_.hostSurface; }
    bool runtimeHasBitPerfectEngine() const noexcept { return engineBank_.bitPerfect.get() != nullptr; }
    void runtimeObserveDefaultEventChannel(int channel) noexcept {
        if (channel >= 0 && channel < 16) lastHostChannel_ = channel;
    }
    void runtimeBitPerfectSetPitchBend14(int channel, int raw14) noexcept {
        if (engineBank_.bitPerfect)
            engineBank_.bitPerfect->setPitchBend14(std::clamp(channel, 0, 15),
                                                   std::clamp(raw14, 0, 16383));
    }
    void runtimeAllNotesOff() noexcept { kernelAllNotesOff(); }
    void runtimeAllNotesOffChannel(int channel) noexcept { kernelAllNotesOffChannel(channel); }
    // Canonical drum dispatch surface (see CanonicalRuntimeBackend::triggerDrumMidi).
    // Phase2 is the Hybrid flavor: the canonical DrSID engine is the sole drum
    // audio authority (no SID-808 bridge replacement path exists here).
    void runtimeTriggerDrSidNote(int note, float velocity) noexcept {
        ArpSID::canonicalTriggerDrumMidi(engineBank_.drSid.get(),
                                         static_cast<uint8_t>(std::clamp(note, 0, 127)),
                                         velocity);
    }
    void runtimeReleaseDrSidNote(int note) noexcept {
        if (engineBank_.drSid) engineBank_.drSid->noteOffMidi(std::clamp(note, 0, 127));
    }
    void runtimeRenderCanonicalSlice(int offset, int sliceFrames) noexcept;
    void runtimeRenderCanonicalSubSampleSpan(int sampleOffset, uint16_t cycleStart, uint16_t cycleEnd) noexcept;
    uint16_t runtimeEstimatedCyclesPerHostSample() const noexcept;
    double runtimePhysicalSampleRateHz() const noexcept;
    double runtimePhysicalSidClockHz() const noexcept;
    void runtimeRenderCanonicalSubPhaseSpan(int sampleOffset, uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd) noexcept;
    ArpSID::BitPerfectEngine* runtimeBitPerfectEngine() noexcept { return engineBank_.bitPerfect.get(); }
    ArpSID::DrSidEngine* runtimeDrSidEngine() noexcept { return engineBank_.drSid.get(); }
    ArpSID::Arpeggiator* runtimeArpeggiator() noexcept { return engineBank_.arp.get(); }
    ArpSID::SidRegisterEngine* runtimeSidRegisterEngine() noexcept { return &engineBank_.sidRegister; }
    const ArpSID::BitPerfectEngine* runtimeBitPerfectEngine() const noexcept { return engineBank_.bitPerfect.get(); }
    const ArpSID::DrSidEngine* runtimeDrSidEngine() const noexcept { return engineBank_.drSid.get(); }
    const ArpSID::SidRegisterEngine* runtimeSidRegisterEngine() const noexcept { return &engineBank_.sidRegister; }
    ArpSID::SidRuntimeEngineBank& runtimeEngineBank() noexcept { return engineBank_; }
    const ArpSID::SidRuntimeEngineBank& runtimeEngineBank() const noexcept { return engineBank_; }
    std::array<float, kNumParams>& runtimeParameterValues() noexcept { return paramValues; }
    const std::array<float, kNumParams>& runtimeParameterValues() const noexcept { return paramValues; }
    ArpSID::VoiceAllocator* runtimeVoicePolicy() noexcept { return &synthVoicePolicy_(); }
    const ArpSID::VoiceAllocator* runtimeVoicePolicy() const noexcept { return &synthVoicePolicy_(); }
    double runtimeProjectionHostTempo() const noexcept { return runtimeModel_.hostTempoBpm(); }
    ArpSID::SidRuntimeProjectionState& runtimeProjectionState() noexcept { return runtimeCore_.projection; }
    const ArpSID::SidRuntimeProjectionState& runtimeProjectionState() const noexcept { return runtimeCore_.projection; }
    ArpSID::SidRuntimeExecutionOwner<ArpSIDProcessorPhase2>& runtimeExecutionOwner() noexcept { return *runtimeExecutionOwner_; }
    const ArpSID::SidRuntimeExecutionOwner<ArpSIDProcessorPhase2>& runtimeExecutionOwner() const noexcept { return *runtimeExecutionOwner_; }
    float* runtimeFractionalAccumLData() noexcept { return runtimeFractionalAccumL_.data(); }
    float* runtimeFractionalAccumRData() noexcept { return runtimeFractionalAccumR_.data(); }
    uint32_t* runtimeFractionalWeightData() noexcept { return runtimeFractionalWeight_.data(); }
    uint8_t* runtimeFractionalActiveData() noexcept { return runtimeFractionalActive_.data(); }
    size_t runtimeFractionalAccumCapacity() const noexcept { return std::min(runtimeFractionalAccumL_.size(), runtimeFractionalAccumR_.size()); }
    int runtimeCurrentRenderOffset() const noexcept { return runtimeCurrentRenderOffset_; }
    // v875 audit closure: Phase2 does not own the AU3 C64 telemetry mirror, but
    // the canonical SID-register backend has an applied-write observer contract.
    // Keep the target API complete so Phase2 builds through the same backend.
    // v909: this no-op is now published explicitly in telemetry
    // (projectionMirrorAvailable=false, projectionMirrorBackend=
    // kArpSIDProjectionMirrorBackendUnavailablePhase2) so audio and cockpit
    // mirror state can never silently diverge without the UI knowing.
    void runtimeMirrorAppliedProjectionWrite(uint8_t, uint8_t, uint32_t, uint16_t) noexcept {}
    void runtimeStageNormalizedParameterOnly(uint32_t target, float value) noexcept;
    void runtimeApplyNormalizedParameter(uint32_t target, float value) noexcept;
    void runtimeApplyProjectedParameterBody(uint32_t target, float value) noexcept;
    void runtimeImportSidRegisterNormalized(uint32_t target, float value) noexcept;
    void runtimeSetPitchBendRangeSemis(int channel, float semis) noexcept;
    void runtimePolicySetFilterDrive(float value) noexcept;
    void runtimePolicyHandleArpRate(float value) noexcept;
    void runtimePolicyHandleSeqEnable(float value) noexcept;
    void runtimePolicyHandleSeqTempo(float value) noexcept;
    void runtimePolicyHandleVirtualGate(float value) noexcept;
    void runtimePolicyHandleSynthModeEnable(float value) noexcept;
    void runtimePolicySetLimiterEnabled(bool on) noexcept;
    void runtimePolicySetLimiterThreshold(float value) noexcept;
    void runtimePolicySetLimiterAttack(float value) noexcept;
    void runtimePolicySetLimiterRelease(float value) noexcept;
    void runtimePolicySetReverbMix(float value) noexcept;
    void runtimePolicyHandlePanic(float value) noexcept;
    void runtimePolicyApplyForensicConfig() noexcept;

    void setExternalCanonicalInputQueue(const ArpSID::SidTimedEventQueue& q, int frameCount) noexcept;
    void applyCanonicalStateRoot_(const ArpSID::SidStateRootV1& root) noexcept;
    void updateSerializableStateTemplate_(const ArpSID::SidStateRootV1& root) noexcept;
    void buildSerializableStateRootFromShadow_(ArpSID::SidStateRootV1& out) const noexcept;

    // Shared canonical target-adapter surface

    void kernelSetTransportPlaying(bool playing) noexcept;
    void kernelSetHostTempo(float bpm) noexcept;
    void kernelRewindTransport() noexcept;
    void kernelApplyNormalizedParameter(uint32_t target, float value) noexcept;
    void kernelPanic() noexcept;
    void kernelAllNotesOff() noexcept;
    void kernelAllNotesOffChannel(int channel) noexcept;
    void kernelObserveNoteActivity(uint8_t note, float velocity) noexcept;
    void kernelTriggerDrumMidi(uint8_t note, float velocity) noexcept;
    void kernelArpNoteOn(uint8_t note, float velocity) noexcept;
    void kernelArpNoteOff(uint8_t note) noexcept;
    void kernelSynthNoteOn(const ArpSID::SidTimedEvent& ev) noexcept;
    void kernelSynthNoteOff(const ArpSID::SidTimedEvent& ev) noexcept;
    void kernelBitPerfectNoteOn(const ArpSID::SidTimedEvent& ev) noexcept;
    void kernelBitPerfectNoteOff(const ArpSID::SidTimedEvent& ev) noexcept;
    void kernelApplyPitchBend(const ArpSID::SidTimedEvent& ev) noexcept;
    void kernelApplyPolyPressure(const ArpSID::SidTimedEvent& ev) noexcept;
    void kernelApplyChannelPressure(const ArpSID::SidTimedEvent& ev) noexcept;
    void kernelApplyMidiCC(const ArpSID::SidTimedEvent& ev) noexcept;
    void kernelApplyVariantProfile(const ArpSID::SidVariantProfile& profile) noexcept;
    void kernelApplyProgramChange(uint8_t program) noexcept;
    void runtimeHandleRenderModeTransition(ArpSID::SidRuntimeRenderMode oldMode, ArpSID::SidRuntimeRenderMode newMode) noexcept;

    void runtimeDispatchPitchBend14(int channel, int raw14) noexcept;
    void runtimeDispatchPitchBendEvent(const ArpSID::SidTimedEvent& ev) noexcept;
    void runtimeDispatchPolyPressure(int channel, int pitch, int value7) noexcept;
    void runtimeDispatchPolyPressureEvent(const ArpSID::SidTimedEvent& ev) noexcept;
    void runtimeDispatchChannelPressure(int channel, int value7) noexcept;
    void runtimeDispatchChannelPressureEvent(const ArpSID::SidTimedEvent& ev) noexcept;
    void runtimeDispatchMidiCC(int channel, int cc, int value7) noexcept;
    void runtimeBitPerfectSetSustainPedal(int channel, bool on) noexcept;
    void runtimeBitPerfectSetSostenutoPedal(int channel, bool on) noexcept;
    void runtimeSetSustainState(int channel, bool on) noexcept;
    void runtimeSetSostenutoState(int channel, bool on) noexcept;
    void runtimeVoicePolicySetSustain(int channel, bool on) noexcept;
    void runtimeVoicePolicySetSostenuto(int channel, bool on) noexcept;
    void runtimeHardSynthAllNotesOffChannel(int channel) noexcept;
    void runtimeApplyAllNotesOffPerformanceReset_(int channel = -1, bool clearPhysicalPedals = false) noexcept;
    void runtimeApplyHardPanicPerformanceReset_() noexcept;
    void runtimeApplySoftPedal(int channel, bool on) noexcept;
    bool isFactorySnapshotMetadataOrTransientParam_(Steinberg::Vst::ParamID pid) noexcept {
        return ArpSID::isFactorySnapshotMetadataOrTransientParam(static_cast<int>(pid));
    }
    bool applyFactoryPatchSnapshot_(int slot) noexcept;
    void renderAudioSlice_(float* outL, float* outR, Steinberg::int32 numOutChannels, Steinberg::int32 offset, Steinberg::int32 sliceFrames) noexcept;
    // P0-FIX: Apply reverb and limiter post-FX in-place over [outL, outR, n].
    // Called once per block after the canonical render pass writes raw audio.
    // Shared by both 32-bit float and 64-bit float output paths.
    void applyOutputFX_(float* outL, float* outR, int n) noexcept;
private:
    struct RuntimeBridgeCore {
        std::unique_ptr<ArpSID::SidRuntimeExecutionOwner<ArpSIDProcessorPhase2>> executionOwner;
        SidRuntimeEngineBank engineBank{};
        ArpSID::SidRuntimeModel runtimeModel{};
        ArpSID::SidRuntimeProjectionState projection{};
    } runtimeCore_{};
    void publishLatestSnapshot_(const MeterSnapshot& snap) noexcept;
    void seedZeroSnapshot_() noexcept;
    bool auSafeInitInProgress_ = false;
    std::unique_ptr<ArpSID::SidRuntimeExecutionOwner<ArpSIDProcessorPhase2>>& runtimeExecutionOwner_ = runtimeCore_.executionOwner;
    // Shared runtime-owned engine bank. The VST3 processor is now a host adapter
    // over a single shared engine bundle rather than directly owning the engines.
    SidRuntimeEngineBank& engineBank_ = runtimeCore_.engineBank;
    BitPerfectEngine* bitPerfectEngine_() noexcept { return engineBank_.bitPerfect.get(); }
    const BitPerfectEngine* bitPerfectEngine_() const noexcept { return engineBank_.bitPerfect.get(); }
    Arpeggiator* arpeggiator_() noexcept { return engineBank_.arp.get(); }
    const Arpeggiator* arpeggiator_() const noexcept { return engineBank_.arp.get(); }
    // Prevent feeding the arpeggiator_() twice in the same audio block when both
    // multiple host ingress paths are active.

    // When true, incoming MIDI note on/off events for this block were already
    // fed into the arpeggiator_() before render. This prevents double-feeding the
    // same events again at sample time while still allowing immediate arp
    // response on very short note taps.
    DrSidEngine* drSidEngine_() noexcept { return engineBank_.drSid.get(); }
    const DrSidEngine* drSidEngine_() const noexcept { return engineBank_.drSid.get(); }
    LFOBank* lfoBank_() noexcept { return engineBank_.lfo.get(); }
    const LFOBank* lfoBank_() const noexcept { return engineBank_.lfo.get(); }

    // Synth Mode: register-driven SID interface
    SidRegisterEngine& sidRegEngine_() noexcept { return engineBank_.sidRegister; }
    const SidRegisterEngine& sidRegEngine_() const noexcept { return engineBank_.sidRegister; }
    SidWriteQueue& sidWriteQueue_() noexcept { return engineBank_.sidWriteQueue; }
    const SidWriteQueue& sidWriteQueue_() const noexcept { return engineBank_.sidWriteQueue; }
    ArpSID::SidRuntimeRegisterShadow& sidQueuedShadow_() noexcept { return engineBank_.registerShadow; }
    const ArpSID::SidRuntimeRegisterShadow& sidQueuedShadow_() const noexcept { return engineBank_.registerShadow; }
    void invalidateSidQueuedShadow_() noexcept;
    void syncSidQueuedShadowFromLive_() noexcept;
    void flushStuckNoteState_(bool resetSequencerPhase, bool resetArpTransport) noexcept;
    void clearRuntimeStateForTransportStart_() noexcept;
    void neutralizeTransportTransientHostControls_() noexcept;
    void performRenderModeTransition_(ArpSID::SidRuntimeRenderMode oldMode,
                                      ArpSID::SidRuntimeRenderMode newMode) noexcept;
    uint8_t synthModeVoiceCtrlNoGate_(int voiceIndex) const noexcept;
    void clearQueuedSidWritesForVoice_(int voiceIndex, uint16_t fromSampleOffset, uint16_t fromCycleOffset) noexcept;
    void hardSynthModeVoiceOff_(int voiceIndex, uint16_t sampleOffset, uint16_t cycleOffset, bool clearTracking) noexcept;
    void reconcileSynthModeUnheldVoices_() noexcept;
    void mirrorVstMidiHeldIngress_(int channel, int note, int noteId, int velocity7, bool noteOn) noexcept;
    ArpSIDForensicConfig buildForensicConfig_() const noexcept;
    void applyForensicConfig_() noexcept;

    // Cached SID SYSTEM/MODEL pseudo-register ($D41D) byte after applying helper params
    uint8_t sidSysByteCached = 0x02; // default: PAL + MOS8580 + ADSRbug off ($D41D bit 0x02 = 8580)
    void syncSidSystemModelFromParams(bool force=false);

    // ----------------------------------------------------------------------
    // MIDI sample-sliced event queue (ordered by sampleOffset each block; cycle offsets are best-effort ordering hints, not true per-cycle host timing)
    // Render path consumes these by splitting the block into segments.
    // MUST be declared before FixedVec instantiations that reference it.
    // ----------------------------------------------------------------------
    enum class MidiKind : uint8_t {
        NoteOn,
        NoteOff,
        PolyPressure,
        ControlChange,
        Sustain,
        Sostenuto,
        AllNotesOff,
        AllSoundOff,
        ChannelPressure,
        PitchBend,
    };

    enum class EventProvenance : uint8_t {
        InternalGenerated,
        HostNoteOn,
        HostNoteOff,
        HostPolyPressure,
        HostControl,
    };

    struct MidiEvent {
        Steinberg::int32 sampleOffset = -1;  // required at ingress; -1 means unresolved until normalized
        uint16_t cycleOffset = kSidUnresolvedCycleOffset; // optional intra-sample ordering hint; unresolved for normal host ingress
        EventProvenance provenance = EventProvenance::InternalGenerated; // debug/adapter provenance only
        MidiKind kind = MidiKind::NoteOn;    // canonical engine-facing semantic
        Steinberg::int16 pitch = 0;          // 0..127
        Steinberg::int16 channel = -1;       // 0..15 when known, -1 unresolved
        Steinberg::int32 noteId = -1;        // VST3 note identity when known
        float value = 0.0f;                  // velocity / pressure / normalized CC value
        uint16_t data14 = 0;                 // 14-bit MIDI payload for pitch bend / NRPN / RPN data when needed
        uint8_t ccNum = 0;                   // MIDI CC number for ControlChange events
        double ppqPosition = -1.0;           // musical position in quarter notes (-1=unknown)
        uint32_t rawOrder = 0;               // host/input encounter order within block
    };


    // midiKindPriority_, isNoteOnMidiEvent_, isNoteOffMidiEvent_, isDirectNoteMidiEvent_,
    // isControlMidiEvent_ REMOVED — superseded by sidTimedEventPriority() in canonical queue.
    // debugProvenanceForMidiKind_ kept as it is still used in appendCanonicalMidiEvents_.

    static inline EventProvenance debugProvenanceForMidiKind_(MidiKind kind) noexcept {
        switch (kind) {
            case MidiKind::NoteOff:         return EventProvenance::HostNoteOff;
            case MidiKind::NoteOn:          return EventProvenance::HostNoteOn;
            case MidiKind::PolyPressure:    return EventProvenance::HostPolyPressure;
            case MidiKind::ControlChange:
            case MidiKind::Sustain:
            case MidiKind::Sostenuto:
            case MidiKind::AllNotesOff:
            case MidiKind::AllSoundOff:
            case MidiKind::ChannelPressure:
            case MidiKind::PitchBend:
            default:                        return EventProvenance::HostControl;
        }
    }

    // Realtime-safe temp storage for parameter coalescing
    struct PendingParam { Steinberg::Vst::ParamID id; float v; };

    struct TimedParamChange {
        Steinberg::int32 sampleOffset = -1;
        uint16_t cycleOffset = kSidUnresolvedCycleOffset;
        Steinberg::Vst::ParamID id = 0;
        float value = 0.0f;
        uint32_t rawOrder = 0;
    };

    // ----------------------------------------------------------------------
    // FixedVec: fixed-capacity vector replacement. Zero allocation,
    // stack-resident, all methods noexcept. Safe to use on audio thread.
    // ----------------------------------------------------------------------
    template<typename T, size_t N>
    struct FixedVec {
        using value_type = T;
        void  clear()             noexcept { n_ = 0; dropped_ = 0; overflowed_ = false; }
        bool  empty()       const noexcept { return n_ == 0; }
        size_t size()       const noexcept { return n_; }
        void  resize(size_t n)    noexcept {
            if (n > N) {
                dropped_ += (n - N);
                overflowed_ = true;
            }
            n_ = (n < N) ? n : N;
        }
        static constexpr size_t capacity() noexcept { return N; }
        size_t dropped() const noexcept { return dropped_; }
        bool overflowed() const noexcept { return overflowed_; }

        bool push_back(const T& v) noexcept {
            if (n_ >= N) {
                ++dropped_;
                overflowed_ = true;
                return false;
            }
            data_[n_++] = v;
            return true;
        }
        // FIX BUG-0012: Added assert for debug builds to catch out-of-bounds access.
        T&       operator[](size_t i)       noexcept { assert(i < n_ && "FixedVec OOB"); return data_[i]; }
        const T& operator[](size_t i) const noexcept { assert(i < n_ && "FixedVec OOB"); return data_[i]; }
        T*       begin()       noexcept { return data_; }
        T*       end()         noexcept { return data_ + n_; }
        const T* begin() const noexcept { return data_; }
        const T* end()   const noexcept { return data_ + n_; }
        T*       data()        noexcept { return data_; }
        const T* data()  const noexcept { return data_; }
    private:
        T      data_[N];
        size_t n_ = 0;
        size_t dropped_ = 0;
        bool overflowed_ = false;
    };

    // MIDI/automation queues are fixed-capacity and audio-thread safe.
    // midiEvents: populated from canonical output queue for GUI telemetry (read-only consumers).
    // synthModeInternalEvents: internal arp/seq events fed into SynthMode path.
    FixedVec<MidiEvent, 512> midiEvents;                  // telemetry only — not an engine dispatch source
    // hostNeutralControlEvents_, internalRenderMidiEvents_, synthModeMergedEvents_,
    // timedParamChanges, virtualGatePtsBlock, virtualNotePtsBlock: REMOVED (migration-incomplete
    // dead queues from pre-canonical event path; no readers exist in the canonical architecture).
    void enqueueInternalRenderMidiEvent_(MidiKind kind,
                                         int pitch,
                                         float value,
                                         Steinberg::int32 sampleOffset = -1,
                                         uint16_t cycleOffset = kSidUnresolvedCycleOffset,
                                         int channel = -1,
                                         int noteId = -1) noexcept;
    void appendCanonicalMidiEvents_(const ArpSID::SidTimedEventQueue& q) noexcept;
    void dispatchCanonicalTimedEvent_(const ArpSID::SidTimedEvent& ev) noexcept;
    void dispatchCanonicalTimedQueue_(const ArpSID::SidTimedEventQueue& q) noexcept;
    // (synthModeMergedEvents_, timedParamChanges, virtualGatePtsBlock, virtualNotePtsBlock
    // REMOVED — migration-complete dead queues from pre-canonical path; no consumers.)

    // Automation parameter coalescing: unique params per block (used by reportRawMidiTouchedParam).
    FixedVec<PendingParam, 256> pendingParams;

    // ----------------------------------------------------------------------
    // TRUE Modulation Matrix (Phase 4)
    // One source per destination:
    // src param: 0=None, 1..8=Source
    // Destinations: 9 (Cutoff, Res, VCO1 Freq/PW, VCO2 Freq/PW, VCO3 Freq/PW, Volume)
    // ----------------------------------------------------------------------
    static constexpr int kNumModDests = 9;
    std::array<int, kNumModDests> modSrc{};      // 0..8
    std::array<float, kNumModDests> modDepth{};  // 0..1

    // Modulation sources (normalized)
    float srcVelocity = 0.0f;   // 0..1 (last note)
    float srcModWheel = 0.0f;   // 0..1 live wheel source used by mod source 4
    bool  srcModWheelFromCC_ = false; // true once hardware CC1 has taken ownership
    float srcAftertouch = 0.0f; // 0..1 (poly pressure)
    float srcKeyTrack = 0.0f;   // 0..1 (last note pitch)
    float srcRandom = 0.0f;     // 0..1 (smoothed random)
    float randomTarget = 0.5f;
    int   randomCountdown = 0;

    // Simple global ADSR envelope follower for modulation (0..1)
    struct ModADSR {
        float value = 0.0f;
        bool  gate  = false;
        int   heldCount = 0;  // BUG-B29-07: count held notes for poly correctness
        void reset() { value = 0.0f; gate = false; heldCount = 0; }
        void noteOn()  { ++heldCount; gate = true; }
        void noteOff() {
            if (--heldCount <= 0) {
                heldCount = 0;
                gate = false;
            }
            // While any note is still held, gate stays high — envelope keeps going.
        }
        void process(int n, double sr, float a, float d, float s, float r);
    } modEnv;
    
    // Parameter storage
    std::array<float, kNumParams> paramValues;
    // FIX NEW-BUG-0001: Preallocated fixed buffer — no heap allocation on audio thread.
    // Sized to processingCapacity_ in setupProcessing(); process() only overwrites
    // the first currentProcessSamples_ entries without container mutation.
    static constexpr int kMaxSidCyclesCacheSize = 8192; // generous ceiling
    std::array<int, kMaxSidCyclesCacheSize> sidCyclesPerSampleCache_{};
    int sidCyclesPerSampleCacheLen_ = 0;
    // Thread-safe serialization snapshot for getState()/setState().
    // Audio thread updates this once per block (and after explicit state loads),
    // so the UI thread can serialize without racing directly on paramValues[].
    std::array<std::atomic<float>, kNumParams> serializableParamShadow{};
    mutable std::mutex serializableStateTemplateMutex_{};
    mutable ArpSID::SidStateRootV1 serializableStateTemplate_{};
    // audit P0-2 FIX: was a versioned two-slot seqlock whose getState() reader could
    // decode from a slot the writer concurrently overwrote (a data race on the blob
    // bytes even though the post-decode version recheck rejected torn results).
    // Replaced with an OwnershipMailbox so the consumer owns its buffer outright and
    // the producer can never touch it mid-decode. serializableStateTemplateMutex_
    // still serializes the (possibly multiple) non-RT consumers and guards
    // serializableStateTemplate_. See sid_ownership_mailbox.h / sid_state_blob_slot.h.
    static constexpr size_t kPendingBlobCapacity = 1024u * 1024u + 65536u;
    mutable ArpSID::OwnershipMailbox<ArpSID::FixedBlobSlot<kPendingBlobCapacity>>
        serializableBlobMailbox_{};
    ArpSID::SidStateRootV1 stateRootIoScratch_{};
    static constexpr size_t kStateIoScratchSize = sizeof(ArpSID::SidBinaryStateHeader) + (size_t)kNumParams * sizeof(uint32_t) + 64 + 65536;
    std::array<uint8_t, kStateIoScratchSize> stateIoScratch_{};

    inline void syncSerializableParamShadow() noexcept {
        for (size_t i = 0; i < paramValues.size(); ++i) {
            const int pid = static_cast<int>(i);
            const float v = ArpSID::sanitizeNormalizedParamValue(pid, paramValues[i], ArpSID::defaultNormalizedParamValue(pid));
            serializableParamShadow[i].store(v, std::memory_order_relaxed);
        }
    }

    // Safe parameter read helper (normalized 0..1). Never returns NaN.
    inline float getParam(Steinberg::Vst::ParamID pid) const {
        if (pid < 0 || pid >= static_cast<Steinberg::Vst::ParamID>(paramValues.size()))
            return 0.0f;
        return ArpSID::sanitizeNormalizedParamValue(static_cast<int>(pid), paramValues[static_cast<size_t>(pid)],
                                                   ArpSID::defaultNormalizedParamValue(static_cast<int>(pid)));
    }
    
    // Audio state
    double sampleRate = 44100.0;
    Steinberg::int32 maxBlockSize = 512;
    // Hosts should respect maxBlockSize, but some produce a larger transient
    // block during graph/bus reconfiguration. Preallocate bounded emergency
    // headroom off the audio thread so such blocks still advance canonical
    // runtime, FX and telemetry instead of being silently dropped.
    static constexpr Steinberg::int32 kPhase2EmergencyBlockCapacity = 65536;
    Steinberg::int32 processingCapacity_ = 512;
    Steinberg::int32 currentProcessSamples_ = 0;
    bool collectingInputMidiEvents_ = false;
    bool suppressImmediateRealtimeMidiApply_ = false;
    double sidBlockStartCycleFrac_ = 0.0;
    float masterTuneSemis_ = 0.0f;
    std::array<float, 3> synthModeModPitchSemis_{{0.0f, 0.0f, 0.0f}};

    // GUI/HUD telemetry + virtual keyboard state
    float midiActivityMeter = 0.0f;          // decays to 0
    int   lastMidiNote = -1;                 // 0..127
    float lastNoteVelocity_ = 0.0f;          // most recent note-on velocity 0..1

    // Coalesce projected parameter application work: only touch backends when value meaningfully changes.
    // (Hosts may deliver many automation points per block while dragging.)
    std::array<float, kNumParams> lastAppliedParamValues{};

    // Shared canonical runtime bridge for VST3, matching the AU-side continuation pass.
    ArpSID::SidRuntimeModel& runtimeModel_ = runtimeCore_.runtimeModel;
    std::unique_ptr<ArpSID::SidTimedEventQueue> externalCanonicalInputQueue_{std::make_unique<ArpSID::SidTimedEventQueue>(ArpSID::SidTimedEventQueue::AllocateStorage{})};
    // Pass P: arrival_order removed from wrapper. Merge (sid_ingress_merge.h) is the
    // sole authority for arrival_order assignment. [compat-only counter removed]

    

    // ----------------------------------------------------------------------
    // Sample64 support: render in float, then convert to double outputs.
    // Preallocated to processingCapacity_ (no allocations in audio thread).
    // ----------------------------------------------------------------------
    std::vector<float> tmpOutL;
    std::vector<float> tmpOutR;
    std::vector<float> runtimeFractionalAccumL_;
    std::vector<float> runtimeFractionalAccumR_;
    std::vector<uint32_t> runtimeFractionalWeight_;
    std::vector<uint8_t> runtimeFractionalActive_;

// ----------------------------------------------------------------------
// Phase 4 Output FX (realtime-safe, preallocated)
    // - Drive: simple tanh waveshaper
    // - Reverb: lightweight Schroeder (4 comb + 2 allpass per channel)
    // - Limiter: soft peak limiter w/ release
    // ----------------------------------------------------------------------
    float driveAmount = 0.0f;     // 0..1
    float reverbMix = 0.0f;       // 0..1
    bool  limiterEnabled = true;

    // SynthMode MIDI voice tracking (3 SID voices, hardware max).
    // Tracks which SID voice holds which MIDI note for sample-sliced
    ArpSID::SynthModeVoices3& smVoices_() noexcept { return engineBank_.synthVoices; }
    const ArpSID::SynthModeVoices3& smVoices_() const noexcept { return engineBank_.synthVoices; }
    ArpSID::VoiceAllocator& synthVoicePolicy_() noexcept { return engineBank_.voicePolicy; }
    const ArpSID::VoiceAllocator& synthVoicePolicy_() const noexcept { return engineBank_.voicePolicy; }

    // Expressive/performance truth is accessed through runtimeModel_ accessors/bridges.
    // Wrapper-local fields below are host-bridge only and do not own synth truth.
    int lastHostChannel_ = 0;                  // host-local only: last incoming channel hint; default 0 so virtual gate works before MIDI
    int virtualGateNoteId_ = -1;               // host-local only: transient virtual-note bridge id
    int nextVirtualGateNoteId_ = 1;            // host-local only: transient virtual-note id generator
    float globalAftertouch_ = 0.0f;            // derived cache from canonical channel pressure for quick fanout
    ArpSID::SidRuntimePedalState& pedalState_() noexcept { return engineBank_.pedalState; }
    const ArpSID::SidRuntimePedalState& pedalState_() const noexcept { return engineBank_.pedalState; }
    void recomputeGlobalAftertouch_() noexcept;
    float notePressureForVoice_(int channel, int midiNote) const noexcept;
    bool notePressureExplicitForVoice_(int channel, int midiNote) const noexcept;
    void clearPolyPressureForVoice_(int channel, int midiNote) noexcept;
    float currentPitchBendSemis_() const noexcept;
    float currentPitchBendSemis_(int channel) const noexcept;
    void applyLivePitchBend_(uint16_t sampleOffset, uint16_t cycleOffset) noexcept;
    void applyLivePitchBend_(int channel, uint16_t sampleOffset, uint16_t cycleOffset) noexcept;
    void applyRealtimeSynthModeModState_(uint16_t sampleOffset, uint16_t cycleOffset) noexcept;
    void fanoutChannelPressure_(float value) noexcept;
    void applyChannelPressure_(int channel, float value) noexcept;
    void applyLiveChannelPressureToSynthMode_() noexcept;
    void applyLiveChannelPressureToSynthMode_(uint16_t sampleOffset, uint16_t cycleOffset) noexcept;
    void applyLiveChannelPressureToSynthMode_(int channel, uint16_t sampleOffset, uint16_t cycleOffset) noexcept;
    void applyLiveDrSidPerformanceState_(int channel) noexcept;
    void fanoutPolyPressure_(int midiNote, float value) noexcept;
    void applyPolyPressure_(int midiNote, int channel, int noteId, float value) noexcept;
    void fanoutPitchBend14_(int raw14) noexcept;
    void applyPitchBend14_(int channel, int raw14) noexcept;
    void applyRPNDataEntry_() noexcept;
    void applyRPNDataEntry_(int channel) noexcept;
    void applyRPNDataEntry_(int channel, uint16_t sampleOffset, uint16_t cycleOffset) noexcept;
    void synthModeSetSustainPedal_(int channel, bool down) noexcept;
    void synthModeSetSustainPedal_(int channel, bool down, uint16_t sampleOffset, uint16_t cycleOffset) noexcept;
    void synthModeSetSostenutoPedal_(int channel, bool down, uint16_t sampleOffset, uint16_t cycleOffset) noexcept;

    // The live parameter array is the sole render-mode authority. Do not cache a
    // second enum: state restore and same-block automation can otherwise leave the
    // cache one transition behind the engine selected by the canonical resolver.
    ArpSID::SidRuntimeRenderMode resolveTopLevelRenderMode_() const noexcept;
    void resetRenderModeOutputNormalizer_() noexcept;
    void applyRenderModeOutputNormalization_(float* left, float* right, int numSamples,
                                             ArpSID::SidRuntimeRenderMode mode) noexcept;

    // Internal note events generated by arp / sequencer while Synth Mode is enabled.
    // These are folded into the same SID-register render path as external MIDI.
    FixedVec<MidiEvent, 2048> synthModeInternalEvents;
    uint32_t synthModeInternalRawOrderCounter_ = 0;
    void enqueueSynthModeInternalEvent(MidiKind kind,
                                       int pitch,
                                       float value,
                                       Steinberg::int32 sampleOffset = -1,
                                       uint16_t cycleOffset = kSidUnresolvedCycleOffset,
                                       int channel = -1,
                                       int noteId = -1) noexcept;

    // SynthMode voice-mode / held-note helpers
    int synthModeVoiceMode_() const noexcept;
    void resetSynthHeldState_() noexcept;
    void synthHeldNoteOn_(int midiNote, float velocity, int channel = -1, int noteId = -1) noexcept;
    void synthHeldNoteOff_(int midiNote, int channel = -1, int noteId = -1) noexcept;
    int synthHeldTopNote_() const noexcept;
    void seedSynthHeldFromCurrentVoices_() noexcept;
    void synthModeResolveForcedStack_(uint16_t off16, uint16_t cyc16,
                                      bool retrigger) noexcept;
    void scheduleSynthModeGlideWrites_(int numSamples) noexcept;
    void resetFractionalAccumulatorsForBlock_(int numSamples) noexcept;
    void processCanonicalBlockPhase2_(int numSamples,
                                      float* outL,
                                      float* outR,
                                      int outChannels,
                                      ArpSID::SidTimedEventQueue& canonicalQueue) noexcept;
    void canonicalizeStructuralAuthorityAfterCanonicalBlock_(ArpSID::SidRuntimeRenderMode modeBeforeBlock) noexcept;

    // Helpers — called from the processCanonicalBlockPhase2_() SynthMode path
    uint8_t currentOrQueuedSidReg_(uint8_t reg) const noexcept;
    bool sidShadowHasValueAtOrBefore_(uint8_t reg, uint8_t value, uint16_t sampleOffset, uint16_t cycleOffset) const noexcept;
    uint8_t computeSynthModeFilterRouteLowNibble_() const noexcept;
    void pushSynthModeFilterRegsRealtime_(int sampleOffset, uint16_t cycleOffset = kSidUnresolvedCycleOffset);
    void pushSynthModeVoiceSeedRealtime_(int voiceIndex, bool gateOn, int sampleOffset, uint16_t cycleOffset = kSidUnresolvedCycleOffset);
    void reseedSynthModeRealtimeState_(int sampleOffset, uint16_t cycleOffset, bool activeOnly) noexcept;
    void applyTargetedSynthModeParamToSidRegs_(Steinberg::Vst::ParamID pid, int sampleOffset, uint16_t cycleOffset = kSidUnresolvedCycleOffset);
    void synthModeNoteOn (int midiNote, float velocity, int sampleOffset, uint16_t cycleOffset = kSidUnresolvedCycleOffset,
                          int channel = -1, int noteId = -1);
    void synthModeNoteOff(int midiNote, int sampleOffset, uint16_t cycleOffset = kSidUnresolvedCycleOffset,
                          int channel = -1, int noteId = -1);
    double currentSidClockHz() const;
    // v910: the legacy double/floor timing helper cluster (cycles-per-sample
    // floor/max, absolute-cycle mapping, pushSidWriteDelayed) was removed —
    // zero production callers and a second timing law parallel to the
    // canonical Q32 runtime clock. Use sid_event_timing.h helpers instead.
    void pushSidWriteTimed(uint8_t reg, uint8_t value, uint16_t sampleOffset, uint16_t cycleOffset = kSidUnresolvedCycleOffset);
    void pushSidWriteTimedIfChanged_(uint8_t reg, uint8_t value, uint16_t sampleOffset, uint16_t cycleOffset = kSidUnresolvedCycleOffset);
    // Running estimate of reverb tail energy (used for silence detection).
    // Decays each block; silence optimisation waits until this drops below threshold.
    ArpSID::SidPostFxAutomationState postFxBlockStart_{};
    float reverbTailEnergy = 0.0f;
    uint32_t reverbSilentFrames = 0u;

    // FIX BUG-0009 (documentation): Dual limiter architecture rationale.
    // Per-chip LookaheadLimiter (~-0.1 dBFS, 5ms delay) prevents intra-chip clipping.
    // This processor-level SimpleLimiter (zero-latency envelope follower) handles
    // inter-voice peak accumulation in polyphonic mode where the post-sum signal may
    // exceed the per-chip threshold. The SimpleLimiter's threshold should be set
    // slightly below 1.0 to provide headroom without double-compressing.
    // Shared with AU — see include/arpsid/core/sid_audio_processors.h
    ArpSID::SimpleLimiter limiter;
    float limiterThreshold = 0.97f;
    float limiterAttackMs = 0.5f;
    float limiterReleaseMs = 200.0f;

    // Cross-mode output normalization.
    // Keeps perceived level closer between BitPerfect, Synth/SID-register,
    // and DrSID render paths without overriding the user's master-volume intent.
    std::array<float, 3> renderModeOutputGain_{{1.00f, 1.00f, 1.00f}};
    float* runtimeSliceOutL_ = nullptr;
    float* runtimeSliceOutR_ = nullptr;
    int runtimeSliceOutChannels_ = 0;
    int runtimeCurrentRenderOffset_ = 0;
    uint64_t canonicalQueueOverflowCount_ = 0;
    std::unique_ptr<ArpSID::SidTimedEventQueue> canonicalQueueScratch_{std::make_unique<ArpSID::SidTimedEventQueue>(ArpSID::SidTimedEventQueue::AllocateStorage{})};

    // Lock-free ring buffer: audio thread → GUI meter updates (NOT via param queue)
    CanonicalTelemetryRing canonicalTelemetryRing_;
    FullTelemetryRing fullTelemetryRing_;
    ArpSIDTelemetry fullTelemetryScratch_{};
    uint64_t telemetryFrameCounter_ = 0u;
    uint64_t telemetryHostSampleCursor_ = 0u;

    // Shared with AU — see include/arpsid/core/sid_audio_processors.h
    ArpSID::SchroederReverb reverb;

    // Shared post-SID HI-FI authority. VST and AU must feed the same normalized
    // controls and effective forensic profile into the same realtime-safe
    // processor so the audible result and GUI telemetry cannot diverge.
    ArpSID::SidHiFiTranscendence hifiTranscendence_{};
    ArpSID::SidHiFiConfig lastHiFiConfig_{};
    
    // Arpeggiator runtime truth lives in the arpeggiator engine plus canonical host tempo flags.
    // Remaining wrapper fields here are bridge-local scheduling/plumbing only.

    // Sequencer runtime truth is accessed through runtimeModel_ accessors/bridges.

    // Host tempo/transport snapshot fields remain adapter-local by design.
    ArpSID::SidRuntimeProjectionState& runtimeProjectionState_ = runtimeCore_.projection;
    inline void rewindArpPhase() noexcept { if (arpeggiator_()) arpeggiator_()->rewindPhase(true); }

    // Full processContext state (updated each block from host's ProcessContext)

    // Realtime-safe RNG (xorshift32) for sequencer random mode
    std::array<uint8_t, 128> rawCcLastValues_{};
    FixedVec<Steinberg::Vst::ParamID, 128> rawMidiTouchedParams;
    void applyNRPN(int nrpnNum, int raw14);
    void applyNRPN(int channel, int nrpnNum, int raw14, uint16_t sampleOffset, uint16_t cycleOffset);
    void reportRawMidiTouchedParam(Steinberg::Vst::ParamID pid, float value);
    void queueMappedParamFromRawCC(Steinberg::Vst::ParamID pid, float value, Steinberg::int32 sampleOffset, uint32_t rawOrder);
    void toggleMappedParamFromRawCC(Steinberg::Vst::ParamID pid, Steinberg::int32 sampleOffset, uint32_t rawOrder);
    void handleMappedRawCC(uint8_t ccNum, uint8_t val, Steinberg::int32 sampleOffset, uint32_t& rawOrderCounter, int channel);

    
    // Processing methods
    void processParameterChanges(Steinberg::Vst::ProcessData& data);
    void processMIDIEvents(Steinberg::Vst::ProcessData& data);
    bool isTimingCriticalParam(Steinberg::Vst::ParamID id) const;
    int  timingPriorityForParam(Steinberg::Vst::ParamID id) const;
    int  timingPriorityForMidi(const MidiEvent& e) const;
    void finalizeIntraSampleTiming();
    void applyTimedParamEvent(const TimedParamChange& ev);
    void performFullPanicReset(bool clearHoldAndSustain);
    void processLFOs(int numSamples);
    void processSequencer(int numSamples);
    void applyModRoutesThisBlock_() noexcept; // applies live mod routes once per block
    // renderAudio(ProcessData&) was removed in v908: all Phase2/VST3 audio now
    // flows through processCanonicalBlockPhase2_() from process().

    // Canonical telemetry is owned exclusively by canonicalTelemetryRing_.
    // Dead double-buffer/shadow fields were removed so no second snapshot
    // authority can be reintroduced accidentally.
    bool firstProcessLogged_ = false;
};

} // namespace ArpSID
