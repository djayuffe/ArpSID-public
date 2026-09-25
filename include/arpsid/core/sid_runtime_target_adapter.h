#pragma once
#include "sid_runtime_execution.h"
#include "sid_runtime_audio_kernel.h"
#include "sid_runtime_backend.h"
#include "sid_runtime_backend_impl.h"
#include "sid_runtime_shared_kernel.h"
#include "sid_runtime_backend_projection.h"
#include "sid_runtime_parameter_services.h"
#include "sid_runtime_sidreg_queue_render.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ArpSID {

template <class Target>
struct CanonicalRuntimeBackend final : SidRuntimeBackendImpl {
    Target* target{};
    bool arpTimelineMaterialized_ = false;
    bool arpTimelineFinalStateValid_ = false;
    Arpeggiator arpTimelineFinalState_{};
    static void setTransportPlaying_cb(void* u, bool playing) noexcept { static_cast<Target*>(u)->kernelSetTransportPlaying(playing); }
    static void setHostTempo_cb(void* u, float bpm) noexcept { static_cast<Target*>(u)->kernelSetHostTempo(bpm); }
    static void rewindTransport_cb(void* u) noexcept { static_cast<Target*>(u)->kernelRewindTransport(); }
    static void applyNormalizedParameter_cb(void* u, uint32_t targetId, float value) noexcept { static_cast<Target*>(u)->runtimeExecutionOwner().applyProjectedNormalizedParameter(targetId, value); }
    static void panic_cb(void* u) noexcept { static_cast<Target*>(u)->kernelPanic(); }
    static void allNotesOff_cb(void* u) noexcept { static_cast<Target*>(u)->kernelAllNotesOff(); }
    static void observeNoteActivity_cb(void* u, uint8_t note, float velocity) noexcept { static_cast<Target*>(u)->kernelObserveNoteActivity(note, velocity); }
    static void synthNoteOn_cb(void* u, const SidTimedEvent& ev) noexcept { static_cast<Target*>(u)->kernelSynthNoteOn(ev); }
    static void synthNoteOff_cb(void* u, const SidTimedEvent& ev) noexcept { static_cast<Target*>(u)->kernelSynthNoteOff(ev); }
    static void pitchBend_cb(void* u, const SidTimedEvent& ev) noexcept { static_cast<Target*>(u)->kernelApplyPitchBend(ev); }
    static void polyPressure_cb(void* u, const SidTimedEvent& ev) noexcept { static_cast<Target*>(u)->kernelApplyPolyPressure(ev); }
    static void channelPressure_cb(void* u, const SidTimedEvent& ev) noexcept { static_cast<Target*>(u)->kernelApplyChannelPressure(ev); }
    static void midiCC_cb(void* u, const SidTimedEvent& ev) noexcept { static_cast<Target*>(u)->kernelApplyMidiCC(ev); }
    static void variantProfile_cb(void* u, const SidVariantProfile& profile) noexcept { static_cast<Target*>(u)->kernelApplyVariantProfile(profile); }
    static void programChange_cb(void* u, uint8_t program) noexcept { static_cast<Target*>(u)->kernelApplyProgramChange(program); }
    static void renderSlice_cb(void* u, int offset, int frames) noexcept { static_cast<Target*>(u)->runtimeRenderCanonicalSlice(offset, frames); }
    static void renderSubSampleSpan_cb(void* u, int sampleOffset, uint16_t cycleStart, uint16_t cycleEnd) noexcept { static_cast<Target*>(u)->runtimeRenderCanonicalSubSampleSpan(sampleOffset, cycleStart, cycleEnd); }
    static void renderSubPhaseSpan_cb(void* u, int sampleOffset, uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd) noexcept { static_cast<Target*>(u)->runtimeRenderCanonicalSubPhaseSpan(sampleOffset, cycleIndex, subphaseStart, subphaseEnd); }
    static uint16_t estimateCyclesPerHostSample_cb(void* u) noexcept { return static_cast<Target*>(u)->runtimeEstimatedCyclesPerHostSample(); }
    static double physicalSampleRateHz_cb(void* u) noexcept { return static_cast<Target*>(u)->runtimePhysicalSampleRateHz(); }
    static double physicalSidClockHz_cb(void* u) noexcept { return static_cast<Target*>(u)->runtimePhysicalSidClockHz(); }
    explicit CanonicalRuntimeBackend(SidRuntimeModel& rt, Target& tgt) noexcept {
        runtime = &rt;
        target = &tgt;
        user = &tgt;
        bindEngines(tgt.runtimeBitPerfectEngine(), tgt.runtimeDrSidEngine(), tgt.runtimeArpeggiator(), tgt.runtimeSidRegisterEngine());
        set_transport_playing = &setTransportPlaying_cb;
        set_host_tempo = &setHostTempo_cb;
        rewind_transport = &rewindTransport_cb;
        apply_normalized_parameter = &applyNormalizedParameter_cb;
        panic_fn = &panic_cb;
        all_notes_off_fn = &allNotesOff_cb;
        observe_note_activity = &observeNoteActivity_cb;
        synth_note_on_fn = &synthNoteOn_cb;
        synth_note_off_fn = &synthNoteOff_cb;
        pitch_bend_fn = &pitchBend_cb;
        poly_pressure_fn = &polyPressure_cb;
        channel_pressure_fn = &channelPressure_cb;
        midi_cc_fn = &midiCC_cb;
        variant_profile_fn = &variantProfile_cb;
        program_change_fn = &programChange_cb;
        render_slice_fn = &renderSlice_cb;
        render_subsample_span_fn = &renderSubSampleSpan_cb;
        render_subphase_span_fn = &renderSubPhaseSpan_cb;
        estimate_cycles_per_host_sample_fn = &estimateCyclesPerHostSample_cb;
        physical_sample_rate_hz_fn = &physicalSampleRateHz_cb;
        physical_sid_clock_hz_fn = &physicalSidClockHz_cb;
    }

    void renderSidRegister(float* left, float* right, int n) noexcept override {
        if (!left || !right || n <= 0) return;
        if (!target) {
            SidRuntimeBackendImpl::renderSidRegister(left, right, n);
            return;
        }

        const bool synthMode = sidResolveRenderModeFromLiveParams(target->runtimeParameterValues()) == SidRuntimeRenderMode::SidRegister;
        if (!synthMode || !sid_register) {
            SidRuntimeBackendImpl::renderSidRegister(left, right, n);
            return;
        }

        auto& bank = target->runtimeEngineBank();

        // In SID-register mode the SidRegisterEngine is the live audio authority.
        // Do not push the parameter-derived canonical register image back into it here,
        // otherwise live register writes from the queue/editor are overwritten before render.

        // v874 audit (scoped mirror completeness): mirror EVERY write the render actually
        // applies (not just the idempotent seed subset) into the C64 telemetry bus, so the
        // cockpit reflects exactly what was played. The observer is internally gated by the
        // target on cockpit-demand, so this is a cheap early-out when nothing is watching.
        Target* const mirrorTarget = target;
        renderSidRegisterQueueToStereo(*sid_register,
                                       bank.sidWriteQueue,
                                       left,
                                       right,
                                       n,
                                       target->runtimeCurrentRenderOffset(),
                                       [mirrorTarget](const SidWrite& w) noexcept {
                                           mirrorTarget->runtimeMirrorAppliedProjectionWrite(
                                               w.regIndex, w.value, w.sampleOffset, w.cycleOffset);
                                       });
    }

    void bitPerfectNoteOn(const SidTimedEvent& ev) noexcept override {
        if (!bit_perfect || !target) return;
        if (sidResolveRenderModeFromLiveParams(target->runtimeParameterValues()) != SidRuntimeRenderMode::BitPerfect) return;
        canonicalBitPerfectNoteOn(bit_perfect, ev);
    }

    void bitPerfectNoteOnWithToken(const SidTimedEvent& ev, uint64_t voiceToken) noexcept override {
        if (!bit_perfect || !target) return;
        if (sidResolveRenderModeFromLiveParams(target->runtimeParameterValues()) != SidRuntimeRenderMode::BitPerfect) return;
        bit_perfect->noteOn(ev.pitch, ev.value, ev.channel, ev.noteId, voiceToken);
    }

    void bitPerfectNoteOff(const SidTimedEvent& ev) noexcept override {
        if (!bit_perfect || !target) return;
        if (sidResolveRenderModeFromLiveParams(target->runtimeParameterValues()) != SidRuntimeRenderMode::BitPerfect) return;
        canonicalBitPerfectNoteOff(bit_perfect, ev);
    }

    void renderBitPerfectWithArp(float* left, float* right, int n) noexcept override {
        if (arpTimelineMaterialized_) canonicalRenderBitPerfect(bit_perfect, left, right, n);
        else SidRuntimeBackendImpl::renderBitPerfectWithArp(left, right, n);
    }

    template <class Params>
    void projectArpSimulationParams_(Arpeggiator& sim, const Params& params) noexcept {
        const bool effective = sidEffectiveArpAuthorityFromLiveParams(params);
        const bool rawEnable = params[(size_t)kParamArpEnable] > 0.5f;
        if (rawEnable && !effective) {
            // Match the production fail-closed law: an ARP request under a
            // non-BitPerfect owner must not leave a hidden buffered arp alive.
            sim.allNotesOff();
        }
        sim.setEnabled(effective);
        sim.setMode(params[(size_t)kParamArpMode]);
        const float rate = std::clamp(params[(size_t)kParamArpRate], 0.0f, 1.0f);
        if (rate < 0.005f) {
            const double tempo = target ? target->runtimeProjectionHostTempo() : 120.0;
            sim.setRateTempo((float)((tempo > 1.0 && tempo < 1000.0) ? tempo : 120.0), 1.0f);
        } else sim.setRate(std::clamp(rate, 0.005f, 1.0f));
        sim.setOctaves(params[(size_t)kParamArpOctaves]);
        sim.setSwing(params[(size_t)kParamArpSwing]);
        sim.setGateLength(params[(size_t)kParamArpGate]);
        sim.setHold(params[(size_t)kParamArpHold] > 0.5f);
        sim.setLatch(params[(size_t)kParamArpLatch] > 0.5f);
        sim.setTranspose(params[(size_t)kParamArpTranspose]);
        sim.setPatternLength(params[(size_t)kParamArpPatternLength]);
        sim.setRandomAmount(params[(size_t)kParamArpRandom]);
        const bool glide = params[(size_t)kParamPortamentoArpGlide] > 0.5f;
        const int voiceMode = std::clamp((int)std::lround(
            std::clamp(params[(size_t)kParamVoiceMode], 0.0f, 1.0f) * 3.0f), 0, 3);
        sim.setPortamentoArpGlide(glide && voiceMode != 0);
    }

    void appendGeneratedTimedEvents(int frameCount, SidTimedEventQueue& queue) noexcept override {
        arpTimelineMaterialized_ = false;
        arpTimelineFinalStateValid_ = false;
        if (!target || !arp || frameCount <= 0) return;

        // Generate against a render-local value copy. Host MIDI/automation is
        // replayed into this simulation at exact sample offsets, so a NoteOn at
        // sample N can produce the first arp gate at N rather than one block late.
        // The live arp is still used by normal canonical dispatch for observers;
        // finishGeneratedTimedEvents() installs this exact final simulation state
        // after dispatch, avoiding double advancement or double note accounting.
        auto simulatedParams = target->runtimeParameterValues();
        arpTimelineFinalState_ = *arp;
        projectArpSimulationParams_(arpTimelineFinalState_, simulatedParams);

        const int hostEventCount = queue.count;
        uint32_t nextArrival = 1u;
        for (int i = 0; i < hostEventCount; ++i) {
            const uint32_t arrival = queue.events[(size_t)i].arrival_order;
            if (arrival != UINT32_MAX) nextArrival = std::max(nextArrival, arrival + 1u);
        }

        int cursor = 0;
        bool simulatedPlaying = target->runtimeModel().transportPlayingFlag();
        for (int i = 0; i < hostEventCount; ++i) {
            const SidTimedEvent& ev = queue.events[(size_t)i];
            const int offset = std::clamp((int)ev.sample_offset, 0, frameCount - 1);
            if (offset > cursor) {
                (void)canonicalAppendArpTimedEventsSegment(&arpTimelineFinalState_, queue,
                                                           offset - cursor, cursor,
                                                           frameCount, nextArrival);
                cursor = offset;
            }

            switch (ev.type) {
                case SidTimedEventType::MidiNoteOn:
                    if (sidEffectiveArpAuthorityFromLiveParams(simulatedParams))
                        arpTimelineFinalState_.noteOn((int)(ev.pitch & 0x7F), ev.value);
                    break;
                case SidTimedEventType::MidiNoteOff:
                    if (sidEffectiveArpAuthorityFromLiveParams(simulatedParams))
                        arpTimelineFinalState_.noteOff((int)(ev.pitch & 0x7F));
                    break;
                case SidTimedEventType::Panic:
                case SidTimedEventType::AllSoundOff:
                case SidTimedEventType::AllNotesOff:
                    arpTimelineFinalState_.allNotesOff();
                    break;
                case SidTimedEventType::TransportChange: {
                    const bool nowPlaying = ev.value > 0.5f;
                    if (!nowPlaying) arpTimelineFinalState_.allNotesOff();
                    else if (!simulatedPlaying) arpTimelineFinalState_.rewindPhase(true);
                    simulatedPlaying = nowPlaying;
                    break;
                }
                case SidTimedEventType::TempoChange:
                    if (std::isfinite(ev.value_f32) && ev.value_f32 > 0.0f &&
                        simulatedParams[(size_t)kParamArpRate] < 0.005f)
                        arpTimelineFinalState_.setRateTempo(
                            std::clamp(ev.value_f32, 1.0f, 400.0f), 1.0f);
                    break;
                case SidTimedEventType::AutomationPoint:
                    if (ev.target < simulatedParams.size()) {
                        simulatedParams[(size_t)ev.target] =
                            std::clamp(std::isfinite(ev.value) ? ev.value : 0.0f, 0.0f, 1.0f);
                        // Re-project only when the event can alter ARP authority,
                        // timing, pattern, gate shape, or glide voice policy.
                        switch ((int)ev.target) {
                            case kParamArpEnable: case kParamArpMode: case kParamArpRate:
                            case kParamArpOctaves: case kParamArpSwing: case kParamArpGate:
                            case kParamArpHold: case kParamArpLatch: case kParamArpTranspose:
                            case kParamArpPatternLength: case kParamArpRandom:
                            case kParamPortamentoArpGlide: case kParamVoiceMode:
                            case kParamDrSidEnable: case kParamSynthModeEnable:
                                projectArpSimulationParams_(arpTimelineFinalState_, simulatedParams);
                                break;
                            default: break;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        if (cursor < frameCount)
            (void)canonicalAppendArpTimedEventsSegment(&arpTimelineFinalState_, queue,
                                                       frameCount - cursor, cursor,
                                                       frameCount, nextArrival);

        arpTimelineMaterialized_ = true;
        arpTimelineFinalStateValid_ = true;
    }

    void finishGeneratedTimedEvents() noexcept override {
        if (arpTimelineFinalStateValid_ && arp) *arp = arpTimelineFinalState_;
        arpTimelineFinalStateValid_ = false;
        arpTimelineMaterialized_ = false;
    }

    bool supportsFractionalSubSampleSpans() const noexcept override {
        if (!render_subsample_span_fn || !target) return false;
        const auto mode = sidResolveRenderModeFromLiveParams(target->runtimeParameterValues());
        return mode == SidRuntimeRenderMode::BitPerfect ||
               mode == SidRuntimeRenderMode::DrSid ||
               mode == SidRuntimeRenderMode::SidRegister;
    }

    void triggerDrumMidi(uint8_t note, float velocity) noexcept override {
        if (target) {
            target->runtimeTriggerDrSidNote(
                static_cast<int>(note & 0x7Fu),
                std::clamp(std::isfinite(velocity) ? velocity : 0.0f, 0.0f, 1.0f));
            return;
        }
        SidRuntimeBackendImpl::triggerDrumMidi(note, velocity);
    }

    void releaseDrumMidi(uint8_t note) noexcept override {
        if (target) {
            target->runtimeReleaseDrSidNote(static_cast<int>(note & 0x7Fu));
            return;
        }
        SidRuntimeBackendImpl::releaseDrumMidi(note);
    }
};

struct SidRuntimeProjectionState {
    bool firstApply = true;
    SidRuntimeRenderMode lastMode = SidRuntimeRenderMode::BitPerfect;
    SidFamily lastFamily = SidFamily::MOS8580;
    float lastA = 0.0f, lastD = 0.267f, lastS = 0.533f, lastR = 0.2f;
    float lastFC = 0.72f, lastFR = 0.35f;
};

template <class Target>
class SidRuntimeExecutionOwner final {
public:
    explicit SidRuntimeExecutionOwner(Target& target) noexcept
    : target_(target), backend_(target.runtimeModel(), target) {}

    CanonicalRuntimeBackend<Target>& backend() noexcept {
        backend_.runtime = &target_.runtimeModel();
        backend_.target = &target_;
        backend_.user = &target_;
        backend_.bindEngines(target_.runtimeBitPerfectEngine(),
                             target_.runtimeDrSidEngine(),
                             target_.runtimeArpeggiator(),
                             target_.runtimeSidRegisterEngine());
        return backend_;
    }

    void dispatch(const SidTimedEvent& ev) noexcept {
        backend().dispatchCanonicalEvent(ev);
    }

    void dispatchQueue(const SidTimedEventQueue& q) noexcept {
        auto& b = backend();
        for (int i = 0; i < q.count; ++i) b.dispatchCanonicalEvent(q.events[i]);
    }

    void render(float* left, float* right, int frames) noexcept {
        SidRuntimeModel& runtime = target_.runtimeModel();
        SidRuntimeBackend* const previousBackend = runtime.boundBackend();
        auto& b = backend();
        runtime.bindBackend(&b);
        if (left && right && frames > 0) {
            std::memset(left, 0, (size_t)frames * sizeof(float));
            std::memset(right, 0, (size_t)frames * sizeof(float));
            const auto mode = sidResolveRenderModeFromLiveParams(target_.runtimeParameterValues());
            switch (mode) {
                case SidRuntimeRenderMode::SidRegister:
                    b.renderSidRegister(left, right, frames);
                    break;
                case SidRuntimeRenderMode::DrSid:
                    b.renderDrSid(left, right, frames);
                    break;
                case SidRuntimeRenderMode::BitPerfect:
                default:
                    // Combined arp+BitPerfect render honors TimedArpEvent::sampleOffset
                    // while still running collection after disable/rewind to flush gate-off.
                    b.renderBitPerfectWithArp(left, right, frames);
                    break;
            }
        }
        runtime.bindBackend(previousBackend);
    }

    void processBlockInto(int frameCount, SidTimedEventQueue& out) noexcept(noexcept(target_.runtimeRenderCanonicalSlice(0, 0))) {
        SidRuntimeModel& runtime = target_.runtimeModel();
        SidRuntimeBackend* const previousBackend = runtime.boundBackend();
        auto& b = backend();
        runtime.bindBackend(&b);
        runtime.processBoundCanonicalBlockInto(frameCount, out);
        b.finishGeneratedTimedEvents();
        runtime.bindBackend(previousBackend);
        // v897 HOTFIX: the v894 per-block SidWriteQueue::rebaseAfterBlock()
        // call that lived here was REVERTED. It assumed the queue is fully
        // consumed by renderSidRegisterQueueToStereo() within each block, but
        // the fractional sub-span render path (the live stereo path in the AU)
        // short-circuits the queue-render slice fallback, so pending write
        // offsets have a longer-than-one-block lifetime there. Rebasing them
        // every block corrupted live synth/projection write timing (reported
        // as "projection instruments don't play"). The block-tail stuck-write
        // issue v894 targeted must instead be fixed co-located with the
        // actual queue consumer, where its consumption lifetime is known.
        // SidWriteQueue::rebaseAfterBlock() itself is retained (with tests)
        // for that future properly-scoped fix.
    }

    // audit P0.3: by-value processBlock() removed (render-path allocation trap).
    // Use processBlockInto() with caller-owned storage.

    void projectStateToBackends(bool force = false) noexcept {
        auto& ps = target_.runtimeProjectionState();
        projectRuntimeStateToBackends(target_.runtimeModel(),
                                      target_.runtimeEngineBank(),
                                      target_.runtimeParameterValues(),
                                      target_.runtimeVoicePolicy(),
                                      target_.runtimeProjectionHostTempo(),
                                      force,
                                      ps.firstApply,
                                      ps.lastMode,
                                      ps.lastFamily,
                                      ps.lastA,
                                      ps.lastD,
                                      ps.lastS,
                                      ps.lastR,
                                      ps.lastFC,
                                      ps.lastFR);
    }

    void syncTempoLinkedControllers(int frames) noexcept {
        (void)frames;
        syncTempoLinkedRuntimeControllers(target_.runtimeEngineBank(),
                                          target_.runtimeParameterValues(),
                                          target_.runtimeProjectionHostTempo());
    }

    void advanceTempoLinkedControllers(int frames) noexcept {
        advanceTempoLinkedRuntimeControllers(target_.runtimeEngineBank(), frames);
    }

    void applyProjectedNormalizedParameter(uint32_t targetId, float value) noexcept {
        const auto oldMode = sidResolveRenderModeFromLiveParams(target_.runtimeParameterValues());
        if (!runtimeApplyProjectedSpecialParameter(target_, targetId, value))
            // v946: pass the raw normalized value into the target body. The target
            // stages through runtimeStageNormalizedParameterOnly(), which performs
            // param-specific sanitization. Pre-clamping here would turn NaN into
            // 0.0 before the param default can be applied.
            target_.runtimeApplyProjectedParameterBody(targetId, value);
        const auto newMode = sidResolveRenderModeFromLiveParams(target_.runtimeParameterValues());
        if (oldMode != newMode) target_.runtimeHandleRenderModeTransition(oldMode, newMode);
        projectStateToBackends(false);
    }

private:
    Target& target_;
    CanonicalRuntimeBackend<Target> backend_;
};

template <class Target>
inline void dispatchCanonicalTimedEventToTarget(SidRuntimeModel&, const SidTimedEvent& ev, Target& target) noexcept {
    target.runtimeExecutionOwner().dispatch(ev);
}

template <class Target>
inline void dispatchCanonicalTimedQueueToTarget(SidRuntimeModel&, const SidTimedEventQueue& q, Target& target) noexcept {
    target.runtimeExecutionOwner().dispatchQueue(q);
}

template <class Target>
inline void renderCanonicalAudioForTarget(Target& target, float* left, float* right, int frames) noexcept {
    target.runtimeExecutionOwner().render(left, right, frames);
}

// audit P0.3: by-value processCanonicalAudioBlockForTarget() removed (render-path
// allocation trap). Use processCanonicalAudioBlockForTargetInto() below.

// In-place variant: writes result into caller-provided queue.
// Preferred over the return-value version to avoid RVO dependency in hot paths.
template <class Target>
inline void processCanonicalAudioBlockForTargetInto(SidRuntimeModel&, 
                                                    Target& target,
                                                    int frameCount,
                                                    SidTimedEventQueue& outQueue) noexcept(noexcept(target.runtimeRenderCanonicalSlice(0, 0))) {
    target.runtimeExecutionOwner().processBlockInto(frameCount, outQueue);
}

} // namespace ArpSID
