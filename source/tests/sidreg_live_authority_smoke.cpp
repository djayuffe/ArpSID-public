// Copyright (C) 2024-2026 Ulf Bertilsson
#include "runtime_test_common.h"

#include <algorithm>
#include <thread>
#include <vector>

using namespace ArpSID;
using namespace ArpSID::Tests;

static void testConcurrentIngressDrainsAllEvents() {
    SidRuntimeModel runtime;
    constexpr int kPerThread = 64;

    auto producer = [&](SidTimedEventType type, SidIngressSourcePriority prio, uint32_t baseTarget) {
        for (int i = 0; i < kPerThread; ++i) {
            SidTimedEvent ev{};
            ev.type = type;
            ev.sample_offset = static_cast<uint32_t>(i % 8);
            ev.cycle_offset = 0u;
            ev.subphase = 0u;
            ev.target = baseTarget + static_cast<uint32_t>(i);
            ev.value_u32 = static_cast<uint32_t>(i);
            const bool ok = runtime.pushToLane(ev, prio, 8);
            ARPSID_TEST_EXPECT_MSG(ok, "concurrent ingress push failed");
        }
    };

    std::thread a(producer, SidTimedEventType::TempoChange, SidIngressSourcePriority::TransportTempo, 1000u);
    std::thread b(producer, SidTimedEventType::MidiCC, SidIngressSourcePriority::MidiNoteControl, 2000u);
    a.join();
    b.join();

    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};
    runtime.consumePendingEventsInto(8, q);
    ARPSID_TEST_EXPECT(q.count == kPerThread * 2);
    ARPSID_TEST_EXPECT(q.dropped == 0u);
    ARPSID_TEST_EXPECT(std::count_if(q.begin(), q.end(), [](const SidTimedEvent& ev) {
        return ev.type == SidTimedEventType::TempoChange;
    }) == kPerThread);
    ARPSID_TEST_EXPECT(std::count_if(q.begin(), q.end(), [](const SidTimedEvent& ev) {
        return ev.type == SidTimedEventType::MidiCC;
    }) == kPerThread);
}

static void testLiveRegisterWritesUpdateRuntimeSnapshot() {
    SidRuntimeModel runtime;
    TraceBackend backend;
    runtime.bindBackend(&backend);

    SidTimedEvent regA = makeEvent(SidTimedEventType::SidRegisterWrite, 0u, 0u, 0u, 1u);
    regA.target = 0x15u;
    regA.value_u32 = 0x5Au;
    SidTimedEvent regB = makeEvent(SidTimedEventType::SidRegisterWrite, 0u, 0u, 1u, 2u);
    regB.target = 0x18u;
    regB.value_u32 = 0x0Fu;

    ARPSID_TEST_EXPECT(runtime.pushToLane(regA, SidIngressSourcePriority::AutomationRefinement, 1));
    ARPSID_TEST_EXPECT(runtime.pushToLane(regB, SidIngressSourcePriority::AutomationRefinement, 1));

    SidTimedEventQueue consumed{SidTimedEventQueue::AllocateStorage{}};
    runtime.processBoundCanonicalBlockInto(1, consumed);
    ARPSID_TEST_EXPECT(consumed.count == 2);
    ARPSID_TEST_EXPECT(runtime.registerImage().reg[0x15] == 0x5Au);
    ARPSID_TEST_EXPECT(runtime.registerImage().reg[0x18] == 0x0Fu);
    ARPSID_TEST_EXPECT(backend.dispatched.size() == 2u);
}

int main() {
    testConcurrentIngressDrainsAllEvents();
    testLiveRegisterWritesUpdateRuntimeSnapshot();
    std::puts("sidreg_live_authority_smoke: PASS");
    return 0;
}
