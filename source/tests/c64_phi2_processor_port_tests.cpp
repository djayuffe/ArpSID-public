#include "arpsid/core/c64_memory_matrix.h"
#include "arpsid/core/c64_processor_port.h"

#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID::C64;

    ProcessorPort6510 p;
    p.powerOn();
    require(p.loram() && p.hiram() && p.charen(), "power-on processor port exposes normal C64 ROM/I/O map");

    p.write(0x0000u, 0x07u);
    p.write(0x0001u, 0x05u);
    require(p.read(0x0000u, 0xFFu) == 0x07u, "$0000 reads DDR latch");
    require((p.read(0x0001u, 0xA0u) & 0x07u) == 0x05u, "$0001 output bits read from data latch");
    require(p.loram(), "LORAM follows processor port bit 0");
    require(!p.hiram(), "HIRAM follows processor port bit 1");
    require(p.charen(), "CHAREN follows processor port bit 2");

    p.write(0x0000u, 0x00u);
    p.write(0x0001u, 0x00u);
    require(p.read(0x0001u, 0xA5u) == 0xFFu,
            "$0001 input bits use C64 pullups and high bits stay high when DDR bit is input");
    require(p.loram() && p.hiram() && p.charen(),
            "banking input bits use C64 pullups when DDR exposes inputs");

    const PlaState visible = plaFromPortCart(p);
    require(decodeCpuRead(0xD400u, visible) == ReadTarget::Sid, "PLA maps D400 to SID when I/O visible");

    p.write(0x0000u, 0x07u);
    p.write(0x0001u, 0x00u);
    const PlaState hidden = plaFromPortCart(p);
    require(decodeCpuRead(0xD400u, hidden) == ReadTarget::Ram, "PLA maps D400 to RAM when I/O hidden");
    require(decodeCpuWrite(0xD400u, hidden) == WriteTarget::Ram, "hidden I/O SID write targets RAM");

    std::cout << "c64_phi2_processor_port_tests PASS\n";
    return 0;
}

