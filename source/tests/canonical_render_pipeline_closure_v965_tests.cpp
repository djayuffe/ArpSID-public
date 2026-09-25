// v965 canonical render-pipeline closure.
// Pins the end-to-end authority/timing repairs found by the full pipeline audit:
// one final event order, release reserve, exact sequencer/ARP boundaries,
// structural sample-boundary fallback, exact post-FX automation, AU no-output
// continuation, active PAL/NTSC DIGI timing, coherent telemetry publication,
// bounded Phase2 oversize headroom, and single-application VariantChange.

#include "runtime_test_common.h"
#include "arpsid/core/sid_postfx_timeline.h"
#include "arpsid/core/sid_render_pipeline_capabilities.h"
#include "arpsid/core/sid_runtime_engine_ops.h"
#include "au3/ArpSIDSequencerEngine.h"
#include "au3/ArpSIDDSPKernel.hpp"
#include "parameter_ids.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace ArpSID;
using namespace ArpSID::Tests;

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "canonical_render_pipeline_closure_v965_tests FAIL: %s\n", msg);
        std::exit(1);
    }
}

static std::string readText(const char* rel) {
#ifdef ARPSID_SOURCE_ROOT
    const std::string path = std::string(ARPSID_SOURCE_ROOT) + "/" + rel;
#else
    const std::string path = rel;
#endif
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), "required production source must be readable");
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

static std::string between(const std::string& s, const char* begin, const char* end) {
    const auto a = s.find(begin);
    require(a != std::string::npos, "source begin marker must exist");
    const auto b = s.find(end, a + 1);
    require(b != std::string::npos && b > a, "source end marker must exist");
    return s.substr(a, b - a);
}

static void testCanonicalOrderAndReleaseReserve() {
    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};
    auto tempo = makeEvent(SidTimedEventType::TempoChange, 4u, kSidUnresolvedCycleOffset, 0xFFu, 1u);
    auto note  = makeEvent(SidTimedEventType::MidiNoteOn, 4u, kSidUnresolvedCycleOffset, 0xFFu, 2u);
    auto autoP = makeEvent(SidTimedEventType::AutomationPoint, 4u, kSidUnresolvedCycleOffset, 0xFFu, 3u);
    require(q.push(autoP) && q.push(note) && q.push(tempo), "canonical order fixtures must enqueue");
    q.sort();
    require(q.events[0].type == SidTimedEventType::TempoChange,
            "transport/tempo source must remain first at same sample");
    require(q.events[1].type == SidTimedEventType::MidiNoteOn,
            "arrival/source order must keep MIDI before later automation");
    require(q.events[2].type == SidTimedEventType::AutomationPoint,
            "event-type priority must not overwrite canonical arrival order");

    SidTimedEventQueue full{SidTimedEventQueue::AllocateStorage{}};
    const int nonCriticalLimit = kMaxSidTimedEvents - SidTimedEventQueue::kReleaseReserve;
    for (int i = 0; i < nonCriticalLimit; ++i) {
        SidTimedEvent e{};
        e.type = SidTimedEventType::MidiNoteOn;
        e.sample_offset = static_cast<uint32_t>(i & 31);
        e.arrival_order = static_cast<uint32_t>(i + 1);
        e.pitch = static_cast<int16_t>(i & 127);
        require(full.push(e), "noncritical events must fill only their admission partition");
    }
    SidTimedEvent extraOn{};
    extraOn.type = SidTimedEventType::MidiNoteOn;
    extraOn.arrival_order = 0x7fffffffu;
    require(!full.push(extraOn), "noncritical traffic must not consume release reserve");
    SidTimedEvent off{};
    off.type = SidTimedEventType::MidiNoteOff;
    off.sample_offset = 7u;
    off.arrival_order = 0x80000000u;
    off.pitch = 60;
    require(full.push(off), "NoteOff must remain admissible after a dense NoteOn burst");
    require(full.count == nonCriticalLimit + 1, "release event must append into reserved headroom");

    SidTimedEventQueue flagQ{SidTimedEventQueue::AllocateStorage{}};
    for (int i = 0; i < nonCriticalLimit; ++i) {
        SidTimedEvent e{};
        e.type = SidTimedEventType::MidiNoteOn;
        e.sample_offset = 0u;
        e.arrival_order = static_cast<uint32_t>(i + 1);
        if (i == nonCriticalLimit - 1) e.cycle_offset = 3u;
        require(flagQ.push(e), "flag recomputation fixture must fill");
    }
    require(flagQ.hasAnyResolvedCycleTiming(), "fixture must contain one resolved-cycle event");
    SidTimedEvent better{};
    better.type = SidTimedEventType::AutomationPoint;
    better.sample_offset = 0u;
    better.arrival_order = 0u;
    require(flagQ.push(better), "higher-priority replacement must succeed");
    require(!flagQ.hasAnyResolvedCycleTiming(),
            "replacement must recompute and clear stale resolved-cycle metadata");
}

static void testStructuralBoundaryDisablesFractionalClock() {
    SidRuntimeModel runtime;
    TraceBackend backend;
    backend.fractional = true;
    backend.cyclesPerSample = 20u;
    runtime.bindBackend(&backend);

    SidTimedEvent ev{};
    ev.type = SidTimedEventType::AutomationPoint;
    ev.target = static_cast<uint32_t>(kParamSidClockSystem);
    ev.value = 1.0f;
    ev.sample_offset = 3u;
    ev.arrival_order = 1u;
    require(runtime.pushToLane(ev, SidIngressSourcePriority::AutomationRefinement, 16),
            "structural automation must enqueue");
    SidTimedEventQueue consumed{SidTimedEventQueue::AllocateStorage{}};
    runtime.processBoundCanonicalBlockInto(16, consumed);

    require(!backend.renderedSlices.empty(),
            "structural block must use sample-boundary slice rendering");
    for (const auto& line : backend.trace) {
        require(line.rfind("subsample:", 0) != 0 && line.rfind("subphase:", 0) != 0,
                "mode/clock transition block must not use stale fractional clock spans");
    }
}

static void testArpAndSequencerExactTimelines() {
    Arpeggiator arp;
    arp.setSampleRate(48000.0);
    arp.setEnabled(true);
    arp.setRate(0.4f);
    arp.setGateLength(0.5f);
    arp.noteOn(60, 1.0f);
    arp.noteOn(64, 0.8f);
    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};
    const int generated = canonicalAppendArpTimedEvents(&arp, q, 48000);
    require(generated >= 2, "ARP must materialize sample-stamped gate events");
    q.sort();
    bool sawOn = false, sawOff = false;
    uint32_t firstOn = 0u, firstOff = 0u;
    for (int i = 0; i < q.count; ++i) {
        const auto& e = q.events[(size_t)i];
        require(sidTimedEventIsInternalArpGenerated(e),
                "materialized ARP queue must contain only internally marked gates");
        if (!sawOn && e.type == SidTimedEventType::MidiNoteOn) { sawOn = true; firstOn = e.sample_offset; }
        if (!sawOff && e.type == SidTimedEventType::MidiNoteOff) { sawOff = true; firstOff = e.sample_offset; }
    }
    require(sawOn && sawOff && firstOff > firstOn,
            "ARP gate-off must retain its exact later sample position");

    SequencerEngine seq;
    seq.setEnabled(true);
    seq.setSampleRate(48000.0);
    seq.setTempo(240.0);
    seq.setStepsPerBeat(8.0f);
    auto events = std::make_unique<EventBuffer>();
    events->count = 0;
    std::array<SequencerEngine::StepBoundary, 64> boundaries{};
    int boundaryCount = 0;
    seq.advanceWindow(0.0, 2.0, 48000, *events,
                      boundaries.data(), static_cast<int>(boundaries.size()), &boundaryCount);
    require(boundaryCount >= 8, "one block spanning many steps must publish every boundary");
    require(boundaries[0].sampleOffset == 0, "first sequencer boundary must be at block start");
    for (int i = 1; i < boundaryCount; ++i) {
        require(boundaries[(size_t)i].sampleOffset > boundaries[(size_t)i - 1].sampleOffset,
                "sequencer boundaries must be exact and strictly ordered");
    }
}

static void testArpSameBlockHostIngress() {
    prewarmAllSidTables();
    auto kernel = std::make_unique<ArpSIDDSPKernel>();
    constexpr int F = 512;
    constexpr int noteOffset = 96;
    kernel->setup(48000.0, F);
    kernel->setParameter(kParamArpEnable, 1.0f);
    kernel->setParameter(kParamArpRate, 0.40f);
    kernel->setParameter(kParamArpGate, 0.50f);

    TransportState t{};
    t.isPlaying = true;
    t.playStateKnown = true;
    t.bpm = 120.0;
    t.sampleRate = 48000.0;
    t.frameCount = F;
    std::array<float, F> l{}, r{};
    float* out[2] = {l.data(), r.data()};
    // Apply ARP parameter intents before the measured host-note block.
    kernel->processBlock(out, 2, F, nullptr, 0, t);

    l.fill(0.0f); r.fill(0.0f);
    TimedEvent on{};
    on.kind = EventKind::NoteOn;
    on.sampleOffset = noteOffset;
    on.channel = 0;
    on.pitch = 60;
    on.value = 1.0f;
    on.rawOrder = 1u;
    kernel->processBlock(out, 2, F, &on, 1, t);

    float before = 0.0f, after = 0.0f;
    for (int i = 0; i < noteOffset; ++i) before = std::max(before, std::fabs(l[(size_t)i]));
    for (int i = noteOffset; i < F; ++i) after = std::max(after, std::fabs(l[(size_t)i]));
    require(before < 1.0e-6f,
            "same-block ARP materialization must not backdate host NoteOn audio");
    require(after > 1.0e-5f,
            "host NoteOn must create an audible ARP gate in the same block");
}

static void testPostFxExactOffsetState() {
    std::array<float, kNumParams> params{};
    params[(size_t)kParamOutputLimiter] = 1.0f;
    params[(size_t)kParamLimiterThreshold] = 0.94f;
    params[(size_t)kParamLimiterAttack] = 0.08f;
    params[(size_t)kParamLimiterRelease] = 0.35f;
    auto state = sidCapturePostFxAutomationState(params);
    require(std::fabs(state.limiterAttackMs - 1.6f) < 1.0e-5f,
            "limiter attack mapping must match DSP law");
    require(std::fabs(state.limiterReleaseMs - 356.5f) < 1.0e-4f,
            "limiter release mapping must match DSP law");

    SidTimedEvent change{};
    change.type = SidTimedEventType::AutomationPoint;
    change.target = static_cast<uint32_t>(kParamReverbMix);
    change.value = 1.0f;
    change.sample_offset = 64u;
    std::array<float, 128> applied{};
    for (int i = 0; i < 128; ++i) {
        if (i == static_cast<int>(change.sample_offset))
            require(sidApplyPostFxAutomationEvent(state, change), "post-FX event must apply");
        applied[(size_t)i] = state.reverbMix;
    }
    for (int i = 0; i < 64; ++i) require(applied[(size_t)i] == 0.0f,
                                         "post-FX final value must not leak backward");
    for (int i = 64; i < 128; ++i) require(applied[(size_t)i] == 1.0f,
                                           "post-FX event must affect its sample and later");
}

static void testAuNoOutputContinuesRuntime() {
    prewarmAllSidTables();
    auto kernel = std::make_unique<ArpSIDDSPKernel>();
    constexpr int F = 256;
    kernel->setup(48000.0, F);
    TransportState t{};
    t.isPlaying = true;
    t.playStateKnown = true;
    t.bpm = 120.0;
    t.sampleRate = 48000.0;
    t.frameCount = F;

    std::array<float, F> l{}, r{};
    float* out[2] = {l.data(), r.data()};
    kernel->processBlock(out, 2, F, nullptr, 0, t);
    const auto before = kernel->readTelemetry(false);

    TimedEvent on{};
    on.kind = EventKind::NoteOn;
    on.sampleOffset = 64;
    on.channel = 0;
    on.pitch = 60;
    on.value = 1.0f;
    kernel->processBlock(nullptr, 0, F, &on, 1, t);
    const auto after = kernel->readTelemetry(false);
    require(after.telemetryFrameId > before.telemetryFrameId,
            "AU no-output block must publish a later render frame");
    require(after.hostSampleEnd > before.hostSampleEnd,
            "AU no-output block must advance canonical host time");

    l.fill(0.0f); r.fill(0.0f);
    kernel->processBlock(out, 2, F, nullptr, 0, t);
    float peak = 0.0f;
    for (float v : l) peak = std::max(peak, std::fabs(v));
    require(peak > 1.0e-5f,
            "NoteOn consumed during no-output processing must remain audible afterward");
}

static void testProductionSourceContracts() {
    require(sidPipelineHas(kAuRenderPipelineCapabilities, SidRenderPipelineCapability::KitSequencerOverlay) &&
            sidPipelineHas(kAuRenderPipelineCapabilities, SidRenderPipelineCapability::DigiOverlay) &&
            sidPipelineHas(kAuRenderPipelineCapabilities, SidRenderPipelineCapability::MixFx),
            "AU capability contract must declare its overlay/MIX layers");
    require(!sidPipelineHas(kPhase2RenderPipelineCapabilities, SidRenderPipelineCapability::KitSequencerOverlay) &&
            !sidPipelineHas(kPhase2RenderPipelineCapabilities, SidRenderPipelineCapability::DigiOverlay) &&
            !sidPipelineHas(kPhase2RenderPipelineCapabilities, SidRenderPipelineCapability::MixFx),
            "Phase2 must not claim overlay capabilities it does not instantiate");
    require(sidPipelineHas(kPhase2RenderPipelineCapabilities, SidRenderPipelineCapability::ExactPostFxTimeline) &&
            sidPipelineHas(kPhase2RenderPipelineCapabilities, SidRenderPipelineCapability::NoOutputContinuation),
            "Phase2 capability contract must declare repaired core/post-FX behavior");
    const std::string au = readText("source/au3/ArpSIDDSPKernel.hpp");
    const std::string phase2 = readText("source/arpsid_processor_phase2.cpp");
    const std::string phase2h = readText("source/arpsid_processor_phase2.h");
    const std::string exec = readText("include/arpsid/core/sid_runtime_execution.h");

    const std::string telemetry = between(au, "Telemetry readTelemetry(bool includeScopes)",
                                          "void getScopeSnapshot");
    require(telemetry.find("renderParams_") == std::string::npos,
            "GUI telemetry reader must not access live renderParams_");
    require(telemetry.find("drs_()->drSidPlaybackMode") == std::string::npos,
            "GUI telemetry reader must not access live DrSID engine state");
    require(au.find("sequencerStepTriggerOffsetForBlock_") == std::string::npos,
            "legacy countdown-derived KIT/DIGI trigger authority must be removed");
    require(au.find("phi2PerFrameForDigi = currentSidClockHz_()") != std::string::npos,
            "authentic DIGI must use active PAL/NTSC SID clock");
    require(au.find("digiD418_.updateTimingPreserveVoices") != std::string::npos,
            "live variant changes must retime authentic DIGI without killing voices");
    require(au.find("applyAuReverbLimiterTimeline_") != std::string::npos &&
            au.find("applyAuHiFiTimeline_") != std::string::npos,
            "AU post-FX must consume canonical automation timeline");
    require(phase2.find("numSamples > maxBlockSize") == std::string::npos,
            "Phase2 must not drop every host block larger than declared maxBlockSize");
    require(phase2.find("numSamples > processingCapacity_") != std::string::npos &&
            phase2h.find("kPhase2EmergencyBlockCapacity") != std::string::npos,
            "Phase2 must use bounded off-RT emergency capacity");
    require(exec.find("runtime.applyVariantChange(canonicalVariantProfileFromEvent") == std::string::npos,
            "VariantChange dispatch must not mutate canonical runtime twice");
    const std::string auVariant = between(au, "void kernelApplyVariantProfile", "void kernelApplyProgramChange");
    require(auVariant.find("runtimeModel_.applyVariantChange") == std::string::npos,
            "AU variant projection sink must not mutate canonical runtime twice");
    const std::string phaseVariant = between(phase2, "void ArpSIDProcessorPhase2::kernelApplyVariantProfile",
                                             "void ArpSIDProcessorPhase2::kernelApplyProgramChange");
    require(phaseVariant.find("runtimeModel_.applyVariantChange") == std::string::npos,
            "Phase2 variant projection sink must not mutate canonical runtime twice");
}

int main() {
    testCanonicalOrderAndReleaseReserve();
    testStructuralBoundaryDisablesFractionalClock();
    testArpAndSequencerExactTimelines();
    testArpSameBlockHostIngress();
    testPostFxExactOffsetState();
    testAuNoOutputContinuesRuntime();
    testProductionSourceContracts();
    std::printf("canonical_render_pipeline_closure_v965_tests PASS\n");
    return 0;
}
