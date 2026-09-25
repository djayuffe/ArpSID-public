#include "arpsid/core/sid_event_queue.h"
#include "arpsid/core/sid_runtime_register_ops.h"
#include "arpsid/engines/bitperfect_engine.h"
#include "arpsid/engines/sid_register_engine.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

int main() {
    ArpSID::SidTimedEvent ev{};
    ev.target = 0x1Du;
    ev.value_u32 = 0x03u; // NTSC + 8580 system byte

    ArpSID::SidRegisterEngine sreg;
    sreg.reset();
    sreg.writeSystemByte(0x00u);
    ArpSID::canonicalSidRegisterWrite(&sreg, ev);
    require(sreg.currentSystemByte() == 0x03u,
            "canonical SidRegisterEngine write must route D41D to writeSystemByte");

    ArpSID::BitPerfectEngine bpe;
    bpe.reset();
    bpe.rawSidRegisterEngine().writeSystemByte(0x00u);
    ArpSID::canonicalSidRegisterWrite(&bpe, ev);
    require(bpe.rawSidRegisterEngine().currentSystemByte() == 0x03u,
            "canonical BitPerfectEngine write must route D41D to writeSystemByte");

    std::array<uint8_t, 0x20> image{};
    image[0x1D] = 0x02u;
    bpe.rawSidRegisterEngine().writeSystemByte(0x00u);
    bpe.applySidRegisterImage(image);
    require(bpe.rawSidRegisterEngine().currentSystemByte() == 0x02u,
            "BitPerfectEngine register image apply must preserve D41D system byte");

    std::cout << "SidD41dSystemByteRoutingV809Tests PASS\n";
    return 0;
}
