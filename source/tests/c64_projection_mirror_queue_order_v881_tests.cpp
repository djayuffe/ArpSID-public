#include "arpsid/core/c64_bus.h"
#include <cstdint>
#include <iostream>

static void req(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "C64ProjectionMirrorQueueOrderV881Tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static ArpSID::C64::BusCycle wr(uint16_t addr, uint8_t value, uint64_t cycle, bool projection) {
    ArpSID::C64::BusCycle c{};
    c.address = addr;
    c.data = value;
    c.rw = false;
    c.phi2Cycle = cycle;
    c.projectionMirror = projection;
    return c;
}

int main() {
    using ArpSID::C64::SidBusQueue;

    SidBusQueue q;
    req(q.push(wr(0xD418, 0x01, 10, false)), "push ordinary A");
    req(q.push(wr(0xD418, 0x02, 10, true)),  "push projection to drop");
    req(q.push(wr(0xD418, 0x03, 10, false)), "push ordinary B");

    q.clearPendingProjectionMirrorEvents();
    req(q.size() == 2, "selective clear keeps only ordinary events");
    req(q.cursor() == 0, "cursor reset after selective clear");
    req(q.at(0).cycle.data == 0x01 && q.at(1).cycle.data == 0x03,
        "ordinary same-cycle relative order preserved");
    req(q.at(0).cycle.order == 0 && q.at(1).cycle.order == 1,
        "surviving pending order rebased to compact range");

    req(q.push(wr(0xD418, 0x04, 10, false)), "push future same-cycle ordinary");
    const auto* e0 = q.nextBeforeOrAt(10);
    const auto* e1 = q.nextBeforeOrAt(10);
    const auto* e2 = q.nextBeforeOrAt(10);
    req(e0 && e0->cycle.data == 0x01, "rebased A sorts first");
    req(e1 && e1->cycle.data == 0x03, "rebased B sorts second");
    req(e2 && e2->cycle.data == 0x04, "new future same-cycle event sorts after pending survivors");

    SidBusQueue emptyAfterConsumed;
    req(emptyAfterConsumed.push(wr(0xD418, 0x11, 1, false)), "push consumed-only event");
    req(emptyAfterConsumed.nextBeforeOrAt(1) != nullptr, "consume only event");
    emptyAfterConsumed.clearPendingProjectionMirrorEvents();
    req(emptyAfterConsumed.size() == 0 && emptyAfterConsumed.cursor() == 0,
        "compaction clears consumed-only queue");
    req(emptyAfterConsumed.push(wr(0xD418, 0x12, 1, false)), "push after consumed-only compaction");
    req(emptyAfterConsumed.at(0).cycle.order == 0,
        "order counter resets after queue becomes empty through compaction");

    std::cout << "C64ProjectionMirrorQueueOrderV881Tests PASS\n";
    return 0;
}
