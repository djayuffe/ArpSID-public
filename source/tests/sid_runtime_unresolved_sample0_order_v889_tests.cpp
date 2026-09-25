#include "arpsid/core/sid_host_cycle_dispatcher.h"
#include <iostream>
#include <string>
#include <vector>

#define REQUIRE_TRUE(expr, msg) do { if (!(expr)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)

int main() {
    using namespace ArpSID;

    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};

    SidTimedEvent resolvedPanic{};
    resolvedPanic.type = SidTimedEventType::Panic;
    resolvedPanic.sample_offset = 0u;
    resolvedPanic.cycle_offset = 0u;
    resolvedPanic.subphase = 0u;
    resolvedPanic.arrival_order = 2u;
    resolvedPanic.value_u32 = 0x1111u;
    REQUIRE_TRUE(q.push(resolvedPanic), "resolved sample-0 panic push must succeed");

    SidTimedEvent unresolvedNote{};
    unresolvedNote.type = SidTimedEventType::MidiNoteOn;
    unresolvedNote.sample_offset = kSidUnresolvedSampleOffset;
    unresolvedNote.cycle_offset = kSidUnresolvedCycleOffset;
    unresolvedNote.subphase = 0xFFu;
    unresolvedNote.arrival_order = 1u;
    unresolvedNote.value_u32 = 0x2222u;
    REQUIRE_TRUE(q.push(unresolvedNote), "unresolved sample-now note push must succeed");

    SidTimedEvent resolvedLater{};
    resolvedLater.type = SidTimedEventType::SidRegisterWrite;
    resolvedLater.sample_offset = 2u;
    resolvedLater.cycle_offset = 1u;
    resolvedLater.subphase = 0u;
    resolvedLater.arrival_order = 3u;
    resolvedLater.value_u32 = 0x3333u;
    REQUIRE_TRUE(q.push(resolvedLater), "resolved later write push must succeed");

    q.sort();
    REQUIRE_TRUE(q.count == 3, "queue must contain three events");
    REQUIRE_TRUE(q.events[2].sample_offset == kSidUnresolvedSampleOffset,
                 "pre-dispatch queue keeps unresolved event in tail before v889 materialisation");

    SidCycleClockState clock{};
    clock.configureFixedCyclesPerSample(3u);

    std::vector<uint32_t> eventValues;
    std::vector<std::string> calls;

    SidHostCycleDispatcher::dispatchBlock(
        q,
        4,
        clock,
        true,
        [&](const SidTimedEvent& ev) noexcept {
            eventValues.push_back(ev.value_u32);
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

    REQUIRE_TRUE(eventValues.size() == 3u, "all three events must apply exactly once");
    REQUIRE_TRUE(eventValues[0] == 0x1111u,
                 "resolved sample-0 Panic priority must remain before materialised unresolved NoteOn");
    REQUIRE_TRUE(eventValues[1] == 0x2222u,
                 "unresolved NoteOn must materialise at sample 0 after higher-priority sample-0 event");
    REQUIRE_TRUE(eventValues[2] == 0x3333u,
                 "later resolved event must remain at its resolved physical position");

    REQUIRE_TRUE(calls.size() > 2 && calls[0] == "event:4369:sample:0" && calls[1] == "event:8738:sample:0",
                 "sample-0 events must apply before the first physical sub-sample span");

    bool sawSample2SpanBeforeLater = false;
    bool laterAfterSample2Span = false;
    for (const auto& c : calls) {
        if (c.rfind("sub:2:", 0) == 0) sawSample2SpanBeforeLater = true;
        if (c == "event:13107:sample:2") {
            laterAfterSample2Span = sawSample2SpanBeforeLater;
            break;
        }
    }
    REQUIRE_TRUE(laterAfterSample2Span,
                 "resolved later event must still be interleaved by physical cycle spans");

    return 0;
}
