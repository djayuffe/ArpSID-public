// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_bus.h"

#include <cstdlib>
#include <iostream>

using ArpSID::C64::BusCycle;
using ArpSID::C64::SidBusQueue;

static void req(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static BusCycle ordinary(uint16_t address, uint64_t cycle = 50) {
    BusCycle c{};
    c.address = address;
    c.data = static_cast<uint8_t>(address & 0xffu);
    c.rw = false;
    c.phi2Cycle = cycle;
    c.projectionMirror = false;
    return c;
}

int main() {
    SidBusQueue q;
    req(q.push(ordinary(0xD400)), "push A");
    req(q.push(ordinary(0xD401)), "push B");
    req(q.push(ordinary(0xD402)), "push C");

    const auto* first = q.nextBeforeOrAt(50);
    req(first && first->cycle.address == 0xD400, "first event consumed");
    req(q.cursor() == 1u, "cursor marks one consumed event");

    // Fill the fixed queue to capacity while one consumed event remains at the
    // front. The next push must compact the consumed prefix, sort/rebase the
    // surviving pending range, and append the new event after all survivors.
    for (size_t i = q.size(); i < SidBusQueue::kCapacity; ++i) {
        req(q.push(ordinary(static_cast<uint16_t>(0xE000u + i))), "fill to capacity");
    }
    req(q.size() == SidBusQueue::kCapacity, "queue reached capacity");
    req(q.cursor() == 1u, "cursor still points past consumed prefix");

    req(q.push(ordinary(0xD4FF)), "capacity push compacts consumed prefix and succeeds");
    req(q.size() == SidBusQueue::kCapacity, "queue remains at capacity after compact+push");
    req(q.cursor() == 0u, "compaction resets cursor to pending-range start");

    q.sortStable();
    req(q.at(0).cycle.address == 0xD401, "surviving B is first after consumed A removed");
    req(q.at(1).cycle.address == 0xD402, "surviving C follows B");
    req(q.at(SidBusQueue::kCapacity - 1).cycle.address == 0xD4FF,
        "new same-cycle event sorts after all rebased survivors");

    for (size_t i = 0; i < q.size(); ++i) {
        req(q.at(i).cycle.order == static_cast<uint32_t>(i),
            "pending event order rebased densely after ordinary compaction");
    }

    std::cout << "PASS c64_projection_mirror_queue_compaction_v883_tests\n";
    return 0;
}
