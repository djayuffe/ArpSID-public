// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/sid_host_cycle_dispatcher.h"
#include <iostream>
#include <string>
#include <vector>

#define REQUIRE_TRUE(expr, msg) do { if (!(expr)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)

int main() {
    using namespace ArpSID;

    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};

    SidTimedEvent resolved{};
    resolved.type = SidTimedEventType::SidRegisterWrite;
    resolved.sample_offset = 2u;
    resolved.cycle_offset = 1u;
    resolved.subphase = 0u;
    resolved.arrival_order = 1u;
    resolved.value_u32 = 0xA5u;
    REQUIRE_TRUE(q.push(resolved), "resolved event push must succeed");

    SidTimedEvent unresolved{};
    unresolved.type = SidTimedEventType::MidiNoteOn;
    unresolved.sample_offset = kSidUnresolvedSampleOffset;
    unresolved.cycle_offset = kSidUnresolvedCycleOffset;
    unresolved.subphase = 0xFFu;
    unresolved.arrival_order = 2u;
    unresolved.value_u32 = 0x5Au;
    REQUIRE_TRUE(q.push(unresolved), "unresolved event push must succeed");
    q.sort();

    REQUIRE_TRUE(q.count == 2, "queue must contain two events");
    REQUIRE_TRUE(q.events[0].hasResolvedTiming(), "sort places resolved events before unresolved tail");
    REQUIRE_TRUE(!q.events[1].hasResolvedTiming(), "unresolved event is sorted to tail and must be preconsumed");

    SidCycleClockState clock{};
    clock.configureFixedCyclesPerSample(3u);

    std::vector<std::string> calls;
    int eventCalls = 0;
    int resolvedEventCalls = 0;
    int unresolvedEventCalls = 0;

    SidHostCycleDispatcher::dispatchBlock(
        q,
        4,
        clock,
        true,
        [&](const SidTimedEvent& ev) noexcept {
            ++eventCalls;
            if (ev.value_u32 == 0xA5u) {
                ++resolvedEventCalls;
                calls.emplace_back("event:resolved:" + std::to_string(ev.sample_offset));
            } else if (ev.value_u32 == 0x5Au) {
                ++unresolvedEventCalls;
                calls.emplace_back("event:unresolved-now:" + std::to_string(ev.sample_offset));
            }
        },
        [&](int sample, int frames) noexcept {
            calls.emplace_back("slice:" + std::to_string(sample) + ":" + std::to_string(frames));
        },
        [&](int sample, uint16_t begin, uint16_t end) noexcept {
            calls.emplace_back("sub:" + std::to_string(sample) + ":" + std::to_string(begin) + ":" + std::to_string(end));
        },
        [&](int, uint16_t, uint16_t, uint16_t) noexcept {}
    );

    REQUIRE_TRUE(eventCalls == 2, "mixed unresolved/resolved block must apply exactly two events");
    REQUIRE_TRUE(unresolvedEventCalls == 1, "unresolved tail must be applied exactly once");
    REQUIRE_TRUE(resolvedEventCalls == 1, "resolved event must still be applied exactly once");
    REQUIRE_TRUE(!calls.empty() && calls.front() == "event:unresolved-now:0",
                 "unresolved now-event must materialise at sample 0 before first physical SID-cycle span even in mixed queue");

    bool resolvedAfterSample2Span = false;
    bool sawSample2SpanBeforeResolved = false;
    for (const auto& c : calls) {
        if (c.rfind("sub:2:", 0) == 0) sawSample2SpanBeforeResolved = true;
        if (c == "event:resolved:2") {
            resolvedAfterSample2Span = sawSample2SpanBeforeResolved;
            break;
        }
    }
    REQUIRE_TRUE(resolvedAfterSample2Span,
                 "resolved sample-2 cycle event must still be interleaved through the physical sub-sample law");

    return 0;
}
