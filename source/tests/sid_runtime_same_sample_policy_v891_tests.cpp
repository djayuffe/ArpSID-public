#include "arpsid/core/sid_host_cycle_dispatcher.h"
#include <iostream>
#include <vector>

#define REQUIRE_TRUE(expr, msg) do { if (!(expr)) { std::cerr << "FAIL: " << msg << "\n"; return 1; } } while (0)

int main() {
    using namespace ArpSID;
    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};

    SidTimedEvent write{};
    write.type = SidTimedEventType::SidRegisterWrite;
    write.sample_offset = 0u; write.cycle_offset = 2u; write.subphase = 0u;
    write.arrival_order = 1u; write.value_u32 = 0x2222u;
    REQUIRE_TRUE(q.push(write), "cycle write push");

    SidTimedEvent panic{};
    panic.type = SidTimedEventType::Panic;
    panic.sample_offset = 0u; panic.cycle_offset = kSidUnresolvedCycleOffset; panic.subphase = 0xFFu;
    panic.arrival_order = 2u; panic.value_u32 = 0xAAAAu;
    REQUIRE_TRUE(q.push(panic), "panic push");

    SidTimedEvent off{};
    off.type = SidTimedEventType::AllSoundOff;
    off.sample_offset = 0u; off.cycle_offset = kSidUnresolvedCycleOffset; off.subphase = 0xFFu;
    off.arrival_order = 3u; off.value_u32 = 0xBBBBu;
    REQUIRE_TRUE(q.push(off), "all sound off push");
    q.sort();

    SidCycleClockState clock{}; clock.configureFixedCyclesPerSample(4u);
    std::vector<uint32_t> order;
    SidHostCycleDispatcher::dispatchBlock(
        q, 1, clock, true,
        [&](const SidTimedEvent& ev) noexcept { order.push_back(ev.value_u32); },
        [&](int, int) noexcept {},
        [&](int, uint16_t, uint16_t) noexcept {},
        [&](int, uint16_t, uint16_t, uint16_t) noexcept {});

    REQUIRE_TRUE(order.size() == 3u, "all same-sample events applied");
    REQUIRE_TRUE(order[0] == 0xAAAAu, "sample-only Panic preempts same-sample cycle-stamped write");
    REQUIRE_TRUE(order[1] == 0xBBBBu, "sample-only AllSoundOff preempts same-sample cycle-stamped write after Panic priority");
    REQUIRE_TRUE(order[2] == 0x2222u, "cycle-stamped write remains after emergency sample-boundary controls");
    return 0;
}
