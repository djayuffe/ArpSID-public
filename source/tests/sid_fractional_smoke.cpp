// Copyright (C) 2024-2026 Ulf Bertilsson
#include "runtime_test_common.h"

#include <algorithm>
#include <vector>

using namespace ArpSID;
using namespace ArpSID::Tests;

static void testFractionalOrderingAndSpans() {
    SidRuntimeModel runtime;
    TraceBackend backend;
    backend.cyclesPerSample = 4u;
    runtime.bindBackend(&backend);

    SidTimedEvent a = makeEvent(SidTimedEventType::SidRegisterWrite, 1u, 0u, 1u, 1u);
    a.target = 0x00u;
    a.value_u32 = 0x11u;
    SidTimedEvent b = makeEvent(SidTimedEventType::SidRegisterWrite, 1u, 0u, 3u, 2u);
    b.target = 0x01u;
    b.value_u32 = 0x22u;
    SidTimedEvent c = makeEvent(SidTimedEventType::SidRegisterWrite, 1u, 2u, 0u, 3u);
    c.target = 0x02u;
    c.value_u32 = 0x33u;

    ARPSID_TEST_EXPECT(runtime.pushToLane(c, SidIngressSourcePriority::AutomationRefinement, 4));
    ARPSID_TEST_EXPECT(runtime.pushToLane(a, SidIngressSourcePriority::AutomationRefinement, 4));
    ARPSID_TEST_EXPECT(runtime.pushToLane(b, SidIngressSourcePriority::AutomationRefinement, 4));

    SidTimedEventQueue consumed{SidTimedEventQueue::AllocateStorage{}};
    runtime.processBoundCanonicalBlockInto(4, consumed);

    ARPSID_TEST_EXPECT(consumed.count == 3);
    ARPSID_TEST_EXPECT(consumed.events[0].target == 0x00u);
    ARPSID_TEST_EXPECT(consumed.events[1].target == 0x01u);
    ARPSID_TEST_EXPECT(consumed.events[2].target == 0x02u);
    ARPSID_TEST_EXPECT(runtime.registerImage().reg[0x00] == 0x11u);
    ARPSID_TEST_EXPECT(runtime.registerImage().reg[0x01] == 0x22u);
    ARPSID_TEST_EXPECT(runtime.registerImage().reg[0x02] == 0x33u);

    const auto hasTrace = [&](const std::string& needle) {
        return std::find(backend.trace.begin(), backend.trace.end(), needle) != backend.trace.end();
    };
    ARPSID_TEST_EXPECT(hasTrace("subphase:1:0:0->1"));
    ARPSID_TEST_EXPECT(hasTrace("subphase:1:0:1->3"));
    ARPSID_TEST_EXPECT(hasTrace("subphase:1:0:3->256"));
    ARPSID_TEST_EXPECT(hasTrace("subsample:1:1->2"));
    ARPSID_TEST_EXPECT(hasTrace("subsample:1:2->4"));
}

static void testOutOfBudgetCycleIsClamped() {
    SidRuntimeModel runtime;
    TraceBackend backend;
    backend.cyclesPerSample = 4u;
    runtime.bindBackend(&backend);

    SidTimedEvent ev = makeEvent(SidTimedEventType::SidRegisterWrite, 0u, 99u, 200u, 1u);
    ev.target = 0x05u;
    ev.value_u32 = 0x7Fu;
    ARPSID_TEST_EXPECT(runtime.pushToLane(ev, SidIngressSourcePriority::AutomationRefinement, 1));

    SidTimedEventQueue consumed{SidTimedEventQueue::AllocateStorage{}};
    runtime.processBoundCanonicalBlockInto(1, consumed);
    ARPSID_TEST_EXPECT(consumed.count == 1);
    ARPSID_TEST_EXPECT(consumed.events[0].cycle_offset == 3u);
    ARPSID_TEST_EXPECT(std::find(backend.trace.begin(), backend.trace.end(), "subsample:0:0->3") != backend.trace.end());
    ARPSID_TEST_EXPECT(std::find(backend.trace.begin(), backend.trace.end(), "subsample:0:4->99") == backend.trace.end());
}

static void testMergePriorityOrdering() {
    std::array<SidMergeEventLane, kMergeLaneCount> lanes{};
    SidTimedEventQueue pending{SidTimedEventQueue::AllocateStorage{}};
    std::atomic<uint32_t> arrival{1u};
    std::atomic<uint64_t> overflow{0u};   // audit #12: sidIngressMerge telemetry is now 64-bit
    std::array<SidMergeCandidate, kMergeScratchMax> scratch{};

    SidTimedEvent noteOn = makeEvent(SidTimedEventType::MidiNoteOn, 0u, 0u, 0u, 1u);
    noteOn.pitch = 60;
    noteOn.noteId = 77;
    noteOn.value = 0.8f;

    SidTimedEvent tempo = makeEvent(SidTimedEventType::TempoChange, 0u, 0u, 0u, 2u);
    tempo.value_f32 = 138.0f;

    ARPSID_TEST_EXPECT(sidIngressPushToLane(lanes, noteOn, SidIngressSourcePriority::MidiNoteControl));
    ARPSID_TEST_EXPECT(sidIngressPushToLane(lanes, tempo, SidIngressSourcePriority::TransportTempo));

    const int merged = sidIngressMerge(lanes, pending, arrival, 1, overflow, scratch.data(), scratch.size());
    ARPSID_TEST_EXPECT(merged == 2);
    ARPSID_TEST_EXPECT(pending.count == 2);
    ARPSID_TEST_EXPECT(pending.events[0].type == SidTimedEventType::TempoChange);
    ARPSID_TEST_EXPECT(pending.events[1].type == SidTimedEventType::MidiNoteOn);
}

int main() {
    testFractionalOrderingAndSpans();
    testOutOfBudgetCycleIsClamped();
    testMergePriorityOrdering();
    std::puts("sid_fractional_smoke: PASS");
    return 0;
}
