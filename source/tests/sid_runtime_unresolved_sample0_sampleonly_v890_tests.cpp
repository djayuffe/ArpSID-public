// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/sid_host_cycle_dispatcher.h"
#include <iostream>
#include <string>
#include <vector>

#define REQUIRE_TRUE(expr, msg) do { if (!(expr)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)

int main() {
    using namespace ArpSID;

    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};

    // This is the v889 gap: a resolved sample-0 event that intentionally has no
    // cycle metadata must not be outranked merely because an unresolved host-now
    // event was materialised as cycle=0. Sample-0 priority/order must decide.
    SidTimedEvent resolvedSampleOnlyPanic{};
    resolvedSampleOnlyPanic.type = SidTimedEventType::Panic;
    resolvedSampleOnlyPanic.sample_offset = 0u;
    resolvedSampleOnlyPanic.cycle_offset = kSidUnresolvedCycleOffset;
    resolvedSampleOnlyPanic.subphase = 0xFFu;
    resolvedSampleOnlyPanic.arrival_order = 2u;
    resolvedSampleOnlyPanic.value_u32 = 0xAAAAu;
    REQUIRE_TRUE(q.push(resolvedSampleOnlyPanic), "resolved sample-only panic push must succeed");

    SidTimedEvent unresolvedNote{};
    unresolvedNote.type = SidTimedEventType::MidiNoteOn;
    unresolvedNote.sample_offset = kSidUnresolvedSampleOffset;
    unresolvedNote.cycle_offset = kSidUnresolvedCycleOffset;
    unresolvedNote.subphase = 0xFFu;
    unresolvedNote.arrival_order = 1u;
    unresolvedNote.value_u32 = 0xBBBBu;
    REQUIRE_TRUE(q.push(unresolvedNote), "unresolved note push must succeed");

    SidTimedEvent resolvedCycleLater{};
    resolvedCycleLater.type = SidTimedEventType::SidRegisterWrite;
    resolvedCycleLater.sample_offset = 1u;
    resolvedCycleLater.cycle_offset = 1u;
    resolvedCycleLater.subphase = 0u;
    resolvedCycleLater.arrival_order = 3u;
    resolvedCycleLater.value_u32 = 0xCCCCu;
    REQUIRE_TRUE(q.push(resolvedCycleLater), "resolved later cycle event push must succeed");

    q.sort();

    SidCycleClockState clock{};
    clock.configureFixedCyclesPerSample(3u);

    std::vector<uint32_t> events;
    std::vector<std::string> calls;
    std::vector<uint16_t> eventCycles;
    std::vector<uint8_t> eventSubphases;

    SidHostCycleDispatcher::dispatchBlock(
        q,
        3,
        clock,
        true,
        [&](const SidTimedEvent& ev) noexcept {
            events.push_back(ev.value_u32);
            eventCycles.push_back(ev.cycle_offset);
            eventSubphases.push_back(ev.subphase);
            calls.emplace_back("event:" + std::to_string(ev.value_u32) + ":sample:" + std::to_string(ev.sample_offset));
        },
        [&](int sample, int frames) noexcept {
            calls.emplace_back("slice:" + std::to_string(sample) + ":" + std::to_string(frames));
        },
        [&](int sample, uint16_t begin, uint16_t end) noexcept {
            calls.emplace_back("sub:" + std::to_string(sample) + ":" + std::to_string(begin) + ":" + std::to_string(end));
        },
        [&](int, uint16_t, uint16_t, uint16_t) noexcept {}
    );

    REQUIRE_TRUE(events.size() == 3u, "all events must apply exactly once");
    REQUIRE_TRUE(events[0] == 0xAAAAu, "resolved sample-only Panic must remain before materialised unresolved NoteOn");
    REQUIRE_TRUE(events[1] == 0xBBBBu, "unresolved NoteOn must be materialised as sample-0 after Panic priority");
    REQUIRE_TRUE(events[2] == 0xCCCCu, "later resolved cycle event must remain later");

    REQUIRE_TRUE(eventCycles[1] == kSidUnresolvedCycleOffset,
                 "materialised unresolved event must remain sample-only, not cycle-stamped");
    REQUIRE_TRUE(eventSubphases[1] == 0xFFu,
                 "materialised unresolved event must keep unresolved subphase sentinel");

    REQUIRE_TRUE(calls.size() >= 2 && calls[0] == "event:43690:sample:0" && calls[1] == "event:48059:sample:0",
                 "sample-0 sample-only events must apply before first physical span and in priority order");

    bool sawSample1SpanBeforeLater = false;
    bool laterAfterSample1Span = false;
    for (const auto& c : calls) {
        if (c.rfind("sub:1:", 0) == 0) sawSample1SpanBeforeLater = true;
        if (c == "event:52428:sample:1") {
            laterAfterSample1Span = sawSample1SpanBeforeLater;
            break;
        }
    }
    REQUIRE_TRUE(laterAfterSample1Span, "resolved later cycle event must still be interleaved by physical cycle spans");

    return 0;
}
