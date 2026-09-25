#include "arpsid/core/sid_host_cycle_dispatcher.h"
#include <iostream>
#include <string>
#include <vector>

#define REQUIRE_TRUE(expr, msg) do { if (!(expr)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)

int main() {
    using namespace ArpSID;

    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};
    SidTimedEvent ev{};
    ev.type = SidTimedEventType::MidiNoteOn;
    ev.sample_offset = kSidUnresolvedSampleOffset;
    ev.cycle_offset = kSidUnresolvedCycleOffset;
    REQUIRE_TRUE(q.push(ev), "queue push must succeed");
    q.sort();

    SidCycleClockState clock{};
    clock.configureFixedCyclesPerSample(3u);

    std::vector<std::string> calls;
    int eventCalls = 0;
    int sliceCalls = 0;
    int subSpanCalls = 0;

    SidHostCycleDispatcher::dispatchBlock(
        q,
        4,
        clock,
        true,
        [&](const SidTimedEvent&) noexcept {
            ++eventCalls;
            calls.emplace_back("event");
        },
        [&](int sample, int frames) noexcept {
            ++sliceCalls;
            calls.emplace_back("slice:" + std::to_string(sample) + ":" + std::to_string(frames));
        },
        [&](int sample, uint16_t begin, uint16_t end) noexcept {
            ++subSpanCalls;
            calls.emplace_back("sub:" + std::to_string(sample) + ":" + std::to_string(begin) + ":" + std::to_string(end));
        },
        [&](int, uint16_t, uint16_t, uint16_t) noexcept {}
    );

    REQUIRE_TRUE(eventCalls == 1, "unresolved event must be applied exactly once");
    REQUIRE_TRUE(!calls.empty() && calls.front() == "event", "unresolved now-event must apply before first physical sub-sample span");
    REQUIRE_TRUE(subSpanCalls == 4, "fractional-capable unresolved-only block must use per-sample physical spans, not whole-block fast path");
    REQUIRE_TRUE(sliceCalls == 4, "fractional-capable unresolved-only block must finalize one slice per host sample");
    REQUIRE_TRUE(clock.fractionalQ32 == 0ull, "fixed 3 cycles/sample clock should end with zero fractional remainder after four samples");

    // Non-fractional backends keep the cheap legacy unresolved fast path.
    SidTimedEventQueue q2{SidTimedEventQueue::AllocateStorage{}};
    REQUIRE_TRUE(q2.push(ev), "queue2 push must succeed");
    q2.sort();
    SidCycleClockState clock2{};
    clock2.configureFixedCyclesPerSample(3u);
    int nfEvents = 0;
    int nfSlices = 0;
    int nfSubSpans = 0;
    bool nfWholeBlockSlice = true;
    SidHostCycleDispatcher::dispatchBlock(
        q2,
        4,
        clock2,
        false,
        [&](const SidTimedEvent&) noexcept { ++nfEvents; },
        [&](int, int frames) noexcept { ++nfSlices; nfWholeBlockSlice = nfWholeBlockSlice && (frames == 4); },
        [&](int, uint16_t, uint16_t) noexcept { ++nfSubSpans; },
        [&](int, uint16_t, uint16_t, uint16_t) noexcept {}
    );
    REQUIRE_TRUE(nfEvents == 1, "non-fractional unresolved event applies once");
    REQUIRE_TRUE(nfSlices == 1, "non-fractional unresolved fast path remains whole-block");
    REQUIRE_TRUE(nfWholeBlockSlice, "non-fractional fast path should render whole block once");
    REQUIRE_TRUE(nfSubSpans == 0, "non-fractional backend must not receive sub-sample spans");

    return 0;
}
