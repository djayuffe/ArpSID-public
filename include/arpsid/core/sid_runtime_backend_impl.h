// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include "sid_runtime_backend.h"
#include "sid_runtime_shared_kernel.h"
#include "sid_runtime_execution.h"
#include "sid_runtime_engine_ops.h"
#include "sid_runtime_register_ops.h"
#include "arpsid/engines/bitperfect_engine.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/arpeggiator.h"
#include "arpsid/engines/sid_register_engine.h"
#include <utility>

namespace ArpSID {

class SidRuntimeBackendImpl : public SidRuntimeBackend {
public:
    using BoolFn = void(*)(void*, bool) noexcept;
    using FloatFn = void(*)(void*, float) noexcept;
    using ParamFn = void(*)(void*, uint32_t, float) noexcept;
    using PanicFn = void(*)(void*) noexcept;
    using NoteObsFn = void(*)(void*, uint8_t, float) noexcept;
    using NoteOnOffFn = void(*)(void*, const SidTimedEvent&) noexcept;
    using MidiEventFn = void(*)(void*, const SidTimedEvent&) noexcept;
    using VariantFn = void(*)(void*, const SidVariantProfile&) noexcept;
    using ProgramFn = void(*)(void*, uint8_t) noexcept;
    using RenderSliceFn = void(*)(void*, int, int) noexcept;
    using RenderSubSampleSpanFn = void(*)(void*, int, uint16_t, uint16_t) noexcept;
    using RenderSubPhaseSpanFn = void(*)(void*, int, uint16_t, uint16_t, uint16_t) noexcept;
    using EstimateCyclesFn = uint16_t(*)(void*) noexcept;

    void* user = nullptr;
    BitPerfectEngine* bit_perfect = nullptr;
    DrSidEngine* drsid = nullptr;
    Arpeggiator* arp = nullptr;
    SidRegisterEngine* sid_register = nullptr;


    void bindEngines(BitPerfectEngine* bp, DrSidEngine* ds, Arpeggiator* a, SidRegisterEngine* sr) noexcept {
        bit_perfect = bp;
        drsid = ds;
        arp = a;
        // SID-register authority must be explicitly provided.
        // Do not alias it to BitPerfect's embedded raw SID engine here,
        // otherwise render-mode ownership silently collapses across engines.
        sid_register = sr;
    }

    BoolFn set_transport_playing = nullptr;
    FloatFn set_host_tempo = nullptr;
    PanicFn rewind_transport = nullptr;
    ParamFn apply_normalized_parameter = nullptr;
    PanicFn panic_fn = nullptr;
    PanicFn all_notes_off_fn = nullptr;
    NoteObsFn observe_note_activity = nullptr;
    NoteOnOffFn synth_note_on_fn = nullptr;
    NoteOnOffFn synth_note_off_fn = nullptr;
    MidiEventFn pitch_bend_fn = nullptr;
    MidiEventFn poly_pressure_fn = nullptr;
    MidiEventFn channel_pressure_fn = nullptr;
    MidiEventFn midi_cc_fn = nullptr;
    VariantFn variant_profile_fn = nullptr;
    ProgramFn program_change_fn = nullptr;
    RenderSliceFn render_slice_fn = nullptr;
    RenderSubSampleSpanFn render_subsample_span_fn = nullptr;
    RenderSubPhaseSpanFn render_subphase_span_fn = nullptr;
    EstimateCyclesFn estimate_cycles_per_host_sample_fn = nullptr;
    using PhysicalDoubleFn = double (*)(void*) noexcept;
    PhysicalDoubleFn physical_sample_rate_hz_fn = nullptr;
    PhysicalDoubleFn physical_sid_clock_hz_fn = nullptr;

    void setTransportPlaying(bool playing) noexcept override {
        if (set_transport_playing) set_transport_playing(user, playing);
    }
    void setHostTempo(float bpm) noexcept override {
        if (set_host_tempo) set_host_tempo(user, bpm);
    }
    void rewindTransport() noexcept override {
        if (rewind_transport) rewind_transport(user);
    }
    void applyNormalizedParameter(uint32_t target, float value) noexcept override {
        if (apply_normalized_parameter) apply_normalized_parameter(user, target, value);
    }
    void panic() noexcept override {
        if (panic_fn) panic_fn(user);
    }
    void allNotesOff() noexcept override {
        if (all_notes_off_fn) all_notes_off_fn(user);
    }
    void allNotesOffChannel(int channel) noexcept override {
        // Channel-scoped release must also clear global/channel-less arp state;
        // otherwise MIDI CC123/120 on channel 0 can leave arp-owned voices alive.
        // BitPerfect/DrSID handle the channel filter internally; arp is global, so
        // clear it conservatively until arp input ownership is per-channel.
        if (channel < 0) { allNotesOff(); return; }
        if (bit_perfect) bit_perfect->allNotesOffChannel(channel);
        if (drsid) drsid->allNotesOffChannel(channel);
        if (arp) arp->allNotesOff();
    }
    void observeNoteActivity(uint8_t note, float velocity) noexcept override {
        if (observe_note_activity) observe_note_activity(user, note, velocity);
    }
    void triggerDrumMidi(uint8_t note, float velocity) noexcept override {
        canonicalTriggerDrumMidi(drsid, note, velocity);
    }
    void releaseDrumMidi(uint8_t note) noexcept override {
        if (drsid) drsid->noteOffMidi(static_cast<int>(note));
    }
    void arpNoteOn(uint8_t note, float velocity) noexcept override {
        canonicalArpNoteOn(arp, note, velocity);
    }
    void arpNoteOff(uint8_t note) noexcept override {
        canonicalArpNoteOff(arp, note);
    }
    void synthNoteOn(const SidTimedEvent& ev) noexcept override {
        if (synth_note_on_fn) synth_note_on_fn(user, ev);
    }
    void synthNoteOff(const SidTimedEvent& ev) noexcept override {
        if (synth_note_off_fn) synth_note_off_fn(user, ev);
    }
    void bitPerfectNoteOn(const SidTimedEvent& ev) noexcept override {
        if (!acceptsBitPerfectVoiceEvents_()) return;
        canonicalBitPerfectNoteOn(bit_perfect, ev);
    }
    void bitPerfectNoteOnWithToken(const SidTimedEvent& ev, uint64_t voiceToken) noexcept override {
        if (!acceptsBitPerfectVoiceEvents_()) return;
        if (bit_perfect) bit_perfect->noteOn(ev.pitch, ev.value, ev.channel, ev.noteId, voiceToken);
    }
    void bitPerfectNoteOff(const SidTimedEvent& ev) noexcept override {
        if (!acceptsBitPerfectVoiceEvents_()) return;
        canonicalBitPerfectNoteOff(bit_perfect, ev);
    }
    void applyPitchBend(const SidTimedEvent& ev) noexcept override {
        if (pitch_bend_fn) pitch_bend_fn(user, ev);
    }
    void applyPolyPressure(const SidTimedEvent& ev) noexcept override {
        if (poly_pressure_fn) poly_pressure_fn(user, ev);
    }
    void applyChannelPressure(const SidTimedEvent& ev) noexcept override {
        if (channel_pressure_fn) channel_pressure_fn(user, ev);
    }
    void applyMidiCC(const SidTimedEvent& ev) noexcept override {
        if (midi_cc_fn) midi_cc_fn(user, ev);
    }
    void applyVariantProfile(const SidVariantProfile& profile) noexcept override {
        if (variant_profile_fn) variant_profile_fn(user, profile);
    }
    void applyProgramChange(uint8_t program) noexcept override {
        (void)program;
        // Safety net: ProgramChange must not select ArpSID factory patches.
    }
    void renderSidRegister(float* left, float* right, int n) noexcept override {
        // P1 authority fix: never push runtime->registerImage() into the live
        // SidRegisterEngine from the render path. registerImage() is a derived
        // presentation/readback snapshot; the live register engine and its
        // timestamped write queue are the only runtime register authority.
        canonicalRenderSidRegister(bit_perfect, sid_register, left, right, n);
    }
    void renderDrSid(float* left, float* right, int n) noexcept override {
        canonicalRenderDrSid(drsid, left, right, n);
    }
    void emitArpTimedEvents(int n) noexcept override {
        canonicalEmitArpTimedEvents(arp, bit_perfect, n);
    }
    void renderBitPerfect(float* left, float* right, int n) noexcept override {
        // BitPerfect voice/runtime state is render-owned truth in BitPerfect mode.
        // Do NOT push runtime->registerImage() back into the engine every block,
        // otherwise live note/gate state and canonical voice events fight a stale
        // parameter-derived register image.
        canonicalRenderBitPerfect(bit_perfect, left, right, n);
    }
    void renderBitPerfectWithArp(float* left, float* right, int n) noexcept override {
        // Interleave arp gate transitions with audio render sub-blocks so sampleOffset
        // is honored. collectTimedEvents() is still always called to flush pending
        // gate-offs after arp disable/rewind.
        canonicalRenderBitPerfectWithArp(arp, bit_perfect, left, right, n);
    }
    void dispatchCanonicalEvent(const SidTimedEvent& ev) noexcept override {
        if (!user) return;
        // Dispatch through the shared kernel using this backend as the primitive surface.
        if (runtime) { auto s = sink(); dispatchCanonicalTimedEvent(*runtime, ev, s); }
    }
    void renderCanonicalSlice(int offset, int frames) noexcept override {
        if (render_slice_fn) render_slice_fn(user, offset, frames);
    }
    uint16_t estimatedCyclesPerHostSample() const noexcept override {
        return estimate_cycles_per_host_sample_fn ? estimate_cycles_per_host_sample_fn(user) : 0u;
    }
    double physicalSampleRateHz() const noexcept override {
        return physical_sample_rate_hz_fn ? physical_sample_rate_hz_fn(user) : 0.0;
    }
    double physicalSidClockHz() const noexcept override {
        return physical_sid_clock_hz_fn ? physical_sid_clock_hz_fn(user) : 0.0;
    }
    bool supportsFractionalSubSampleSpans() const noexcept override {
        if (!render_subsample_span_fn || !runtime) return false;
        const auto mode = runtime->resolveRenderMode();
        return mode == SidRuntimeRenderMode::BitPerfect ||
               mode == SidRuntimeRenderMode::DrSid ||
               mode == SidRuntimeRenderMode::SidRegister;
    }
    void renderCanonicalSubSampleSpan(int sampleOffset, uint16_t cycleStart, uint16_t cycleEnd) noexcept override {
        if (render_subsample_span_fn) render_subsample_span_fn(user, sampleOffset, cycleStart, cycleEnd);
    }
    void renderCanonicalSubPhaseSpan(int sampleOffset, uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd) noexcept override {
        if (render_subphase_span_fn) render_subphase_span_fn(user, sampleOffset, cycleIndex, subphaseStart, subphaseEnd);
    }

    SidRuntimeModel* runtime = nullptr;
private:
    bool acceptsBitPerfectVoiceEvents_() const noexcept {
        return bit_perfect && (!runtime || runtime->resolveRenderMode() == SidRuntimeRenderMode::BitPerfect);
    }
    struct DirectSink {
        SidRuntimeModel* runtime{};
        SidRuntimePrimitiveSurface* surface{};
        void onTransportChange(const SidTimedEvent& ev) noexcept { runtimeKernelOnTransportChange(*runtime, *surface, ev); }
        void onAutomationPoint(const SidTimedEvent& ev) noexcept { runtimeKernelOnAutomationPoint(*runtime, *surface, ev); }
        void onPanic(const SidTimedEvent& ev) noexcept { runtimeKernelOnPanic(*runtime, *surface, ev); }
        void onAllSoundOff(const SidTimedEvent& ev) noexcept { runtimeKernelOnAllSoundOff(*runtime, *surface, ev); }
        void onAllNotesOff(const SidTimedEvent& ev) noexcept { runtimeKernelOnAllNotesOff(*runtime, *surface, ev); }
        void onMidiNoteOn(const SidTimedEvent& ev) noexcept { runtimeKernelOnMidiNoteOn(*runtime, *surface, ev); }
        void onMidiNoteOff(const SidTimedEvent& ev) noexcept { runtimeKernelOnMidiNoteOff(*runtime, *surface, ev); }
        void onPitchBend(const SidTimedEvent& ev) noexcept { runtimeKernelOnPitchBend(*runtime, *surface, ev); }
        void onPolyPressure(const SidTimedEvent& ev) noexcept { runtimeKernelOnPolyPressure(*runtime, *surface, ev); }
        void onChannelPressure(const SidTimedEvent& ev) noexcept { runtimeKernelOnChannelPressure(*runtime, *surface, ev); }
        void onMidiCC(const SidTimedEvent& ev) noexcept { runtimeKernelOnMidiCC(*runtime, *surface, ev); }
        void onVariantChange(const SidTimedEvent& ev) noexcept { runtimeKernelOnVariantChange(*runtime, *surface, ev); }
        void onProgramChange(const SidTimedEvent& ev) noexcept { runtimeKernelOnProgramChange(*runtime, *surface, ev); }
        void onTempoChange(const SidTimedEvent& ev) noexcept { runtimeKernelOnTempoChange(*runtime, *surface, ev); }
    };
    DirectSink sink() noexcept { return DirectSink{runtime, this}; }
};

} // namespace ArpSID
