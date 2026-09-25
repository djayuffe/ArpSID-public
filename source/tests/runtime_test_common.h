#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "arpsid/core/sid_runtime_model.h"
#include "arpsid/core/sid_runtime_backend.h"

namespace ArpSID::Tests {

inline void fail(const char* expr, const char* file, int line, const std::string& detail = {}) {
    std::fprintf(stderr, "TEST FAILURE: %s (%s:%d)\n", expr, file, line);
    if (!detail.empty()) std::fprintf(stderr, "  detail: %s\n", detail.c_str());
    std::fflush(stderr);
    std::abort();
}

inline void expect(bool cond, const char* expr, const char* file, int line, const std::string& detail = {}) {
    if (!cond) fail(expr, file, line, detail);
}

inline bool nearlyEqual(float a, float b, float eps = 1.0e-6f) {
    return std::fabs(a - b) <= eps;
}

#define ARPSID_TEST_EXPECT(expr) ::ArpSID::Tests::expect((expr), #expr, __FILE__, __LINE__)
#define ARPSID_TEST_EXPECT_MSG(expr, msg) ::ArpSID::Tests::expect((expr), #expr, __FILE__, __LINE__, (msg))
#define ARPSID_TEST_EXPECT_NEAR(a, b, eps) \
    ::ArpSID::Tests::expect(::ArpSID::Tests::nearlyEqual((a), (b), (eps)), #a " ~= " #b, __FILE__, __LINE__)

struct TraceBackend final : public SidRuntimeBackend {
    std::vector<std::string> trace;
    std::vector<SidTimedEvent> dispatched;
    std::vector<std::pair<int, int>> renderedSlices;
    uint16_t cyclesPerSample = 4u;
    bool fractional = true;

    void append(const std::string& s) { trace.push_back(s); }

    void dispatchCanonicalEvent(const SidTimedEvent& ev) noexcept override {
        dispatched.push_back(ev);
        std::ostringstream os;
        os << "event:" << static_cast<int>(ev.type)
           << ":sample=" << ev.sample_offset
           << ":cycle=" << ev.cycle_offset
           << ":sub=" << static_cast<int>(ev.subphase)
           << ":target=" << ev.target
           << ":value=" << ev.value_u32;
        append(os.str());
    }

    void renderCanonicalSlice(int offset, int frames) noexcept override {
        renderedSlices.emplace_back(offset, frames);
        std::ostringstream os;
        os << "slice:" << offset << ":" << frames;
        append(os.str());
    }

    bool supportsFractionalSubSampleSpans() const noexcept override { return fractional; }

    void renderCanonicalSubSampleSpan(int sampleOffset, uint16_t cycleStart, uint16_t cycleEnd) noexcept override {
        std::ostringstream os;
        os << "subsample:" << sampleOffset << ":" << cycleStart << "->" << cycleEnd;
        append(os.str());
    }

    void renderCanonicalSubPhaseSpan(int sampleOffset, uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd) noexcept override {
        std::ostringstream os;
        os << "subphase:" << sampleOffset << ":" << cycleIndex << ":"
           << static_cast<int>(subphaseStart) << "->" << static_cast<int>(subphaseEnd);
        append(os.str());
    }

    uint16_t estimatedCyclesPerHostSample() const noexcept override { return cyclesPerSample; }

    void setTransportPlaying(bool) noexcept override {}
    void setHostTempo(float) noexcept override {}
    void rewindTransport() noexcept override {}
    void applyNormalizedParameter(uint32_t, float) noexcept override {}
    void panic() noexcept override {}
    void allNotesOff() noexcept override {}
    void observeNoteActivity(uint8_t, float) noexcept override {}
    void triggerDrumMidi(uint8_t, float) noexcept override {}
    void arpNoteOn(uint8_t, float) noexcept override {}
    void arpNoteOff(uint8_t) noexcept override {}
    void synthNoteOn(const SidTimedEvent&) noexcept override {}
    void synthNoteOff(const SidTimedEvent&) noexcept override {}
    void bitPerfectNoteOn(const SidTimedEvent&) noexcept override {}
    void bitPerfectNoteOff(const SidTimedEvent&) noexcept override {}
    void applyPitchBend(const SidTimedEvent&) noexcept override {}
    void applyPolyPressure(const SidTimedEvent&) noexcept override {}
    void applyChannelPressure(const SidTimedEvent&) noexcept override {}
    void applyMidiCC(const SidTimedEvent&) noexcept override {}
    void applyVariantProfile(const SidVariantProfile&) noexcept override {}
    void applyProgramChange(uint8_t) noexcept override {}
    void renderSidRegister(float*, float*, int) noexcept override {}
    void renderDrSid(float*, float*, int) noexcept override {}
    void emitArpTimedEvents(int) noexcept override {}
    void renderBitPerfect(float*, float*, int) noexcept override {}
};

inline SidTimedEvent makeEvent(SidTimedEventType type,
                               uint32_t sample,
                               uint16_t cycle,
                               uint8_t subphase,
                               uint32_t arrival = 0u) {
    SidTimedEvent ev{};
    ev.type = type;
    ev.sample_offset = sample;
    ev.cycle_offset = cycle;
    ev.subphase = subphase;
    ev.arrival_order = arrival;
    ev.channel = 0;
    return ev;
}

inline std::vector<std::string> runCanonicalTrace(const std::vector<SidTimedEvent>& script,
                                                  int frameCount,
                                                  uint16_t cyclesPerSample = 4u) {
    SidRuntimeModel runtime;
    TraceBackend backend;
    backend.cyclesPerSample = cyclesPerSample;
    runtime.bindBackend(&backend);

    uint32_t order = 1u;
    for (auto ev : script) {
        if (ev.arrival_order == 0u) ev.arrival_order = order++;
        const auto priority = (ev.type == SidTimedEventType::TempoChange || ev.type == SidTimedEventType::TransportChange)
            ? SidIngressSourcePriority::TransportTempo
            : SidIngressSourcePriority::MidiNoteControl;
        const bool ok = runtime.pushToLane(ev, priority, frameCount);
        if (!ok) fail("runtime.pushToLane", __FILE__, __LINE__, "trace script push failed");
    }

    SidTimedEventQueue consumed{SidTimedEventQueue::AllocateStorage{}};
    runtime.processBoundCanonicalBlockInto(frameCount, consumed);
    return backend.trace;
}

} // namespace ArpSID::Tests
