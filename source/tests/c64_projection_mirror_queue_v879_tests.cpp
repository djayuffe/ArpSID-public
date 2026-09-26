// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_bus.h"
#include <cstdlib>
#include <iostream>

static void req(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID::C64;

    SidBusQueue q;
    req(q.push(BusCycle{0xD418, 0x11, false, 10, 0, true}), "push consumed projection");
    req(q.push(BusCycle{0xD400, 0x22, false, 20, 0, false}), "push consumed ordinary");
    const SidBusEvent* e = q.nextBeforeOrAt(20);
    req(e && e->cycle.address == 0xD418 && e->cycle.projectionMirror, "consume first projection");
    e = q.nextBeforeOrAt(20);
    req(e && e->cycle.address == 0xD400 && !e->cycle.projectionMirror, "consume ordinary");

    req(q.push(BusCycle{0xD401, 0x33, false, 30, 0, false}), "push pending ordinary");
    req(q.push(BusCycle{0xD402, 0x44, false, 30, 0, true}), "push pending projection");
    q.clearPendingProjectionMirrorEvents();

    req(q.cursor() == 0, "clear must compact consumed events and reset cursor");
    req(q.size() == 1, "clear must drop only pending projection and compact consumed events");
    e = q.nextBeforeOrAt(30);
    req(e && e->cycle.address == 0xD401 && !e->cycle.projectionMirror,
        "pending ordinary write survives selective projection clear");
    req(q.nextBeforeOrAt(30) == nullptr, "pending projection write was discarded");

    SidBusQueue sameCycle;
    req(sameCycle.push(BusCycle{0xD410, 0x01, false, 100, 0, false}), "same ordinary push");
    req(sameCycle.push(BusCycle{0xD411, 0x02, false, 100, 0, true}), "same projection push");
    sameCycle.clearPendingProjectionMirrorEvents();
    req(sameCycle.size() == 1, "same-cycle clear leaves one ordinary event");
    e = sameCycle.nextBeforeOrAt(100);
    req(e && e->cycle.address == 0xD410 && e->cycle.data == 0x01, "same-cycle ordinary survives");

    std::cout << "C64ProjectionMirrorQueueV879Tests PASS\n";
    return 0;
}
