// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/sid_host_cycle_dispatcher.h"
#include <iostream>
#include <string>
#include <vector>

#define REQUIRE_TRUE(expr, msg) do { if (!(expr)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)

int main() {
    using namespace ArpSID;
    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};

    SidTimedEvent sampleOnly{};
    sampleOnly.type = SidTimedEventType::MidiNoteOn;
    sampleOnly.sample_offset = 0u;
    sampleOnly.cycle_offset = kSidUnresolvedCycleOffset;
    sampleOnly.subphase = 0xFFu;
    sampleOnly.arrival_order = 1u;
    sampleOnly.value_u32 = 0x1111u;
    REQUIRE_TRUE(q.push(sampleOnly), "sample-only push");
    q.sort();

    SidCycleClockState clock{};
    clock.configureFixedCyclesPerSample(4u);

    std::vector<std::string> calls;
    std::vector<std::string> subphaseSpans;
    SidHostCycleDispatcher::dispatchBlock(
        q, 1, clock, true,
        [&](const SidTimedEvent& ev) noexcept { calls.emplace_back("event:" + std::to_string(ev.value_u32)); },
        [&](int sample, int frames) noexcept { calls.emplace_back("slice:" + std::to_string(sample) + ":" + std::to_string(frames)); },
        [&](int sample, uint16_t begin, uint16_t end) noexcept { calls.emplace_back("sub:" + std::to_string(sample) + ":" + std::to_string(begin) + ":" + std::to_string(end)); },
        [&](int sample, uint16_t cycle, uint16_t begin, uint16_t end) noexcept {
            std::string s = "phase:" + std::to_string(sample) + ":" + std::to_string(cycle) + ":" + std::to_string(begin) + ":" + std::to_string(end);
            calls.push_back(s); subphaseSpans.push_back(s);
        });

    REQUIRE_TRUE(!calls.empty() && calls.front() == "event:4369", "sample-only event must apply before any render span");
    for (const auto& s : subphaseSpans) {
        REQUIRE_TRUE(s != "phase:0:0:0:255" && s != "phase:0:0:0:256",
                     "sample-only 0xFF sentinel must not render subphase 0..255/256 before event");
    }
    return 0;
}
