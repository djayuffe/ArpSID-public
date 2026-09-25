#include "arpsid/core/c64_platform.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

using namespace ArpSID::C64;

static void require(bool cond, const char* msg) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); std::abort(); }
}

int main() {
    {
        C64Platform c64;
        c64.reset(true);
        // Custom ROM loading and PLA visibility proof.
        std::array<uint8_t, 0x2000> basic{};
        std::array<uint8_t, 0x2000> kernal{};
        std::array<uint8_t, 0x1000> chargen{};
        basic.fill(0xBA); kernal.fill(0xEA); chargen.fill(0xCC);
        require(c64.loadBasicRom(basic.data(), basic.size()), "load BASIC ROM");
        require(c64.loadKernalRom(kernal.data(), kernal.size()), "load KERNAL ROM");
        require(c64.loadCharacterRom(chargen.data(), chargen.size()), "load CHARGEN ROM");
        c64.cpu().state().portDirection = 0x07;
        c64.cpu().state().portData = 0x07;
        require(c64.cpuRead(0xA000) == 0xBA, "BASIC ROM visible through PLA");
        c64.cpu().state().portData = 0x03; // CHAREN low -> character ROM at D000
        require(c64.cpuRead(0xD000) == 0xCC, "character ROM visible when CHAREN low");
    }

    {
        C64Platform c64;
        c64.reset(true);
        // Banked cartridge proof.
        std::array<uint8_t, 0x2000> bank0{};
        std::array<uint8_t, 0x2000> bank1{};
        bank0.fill(0x10); bank1.fill(0x20);
        require(c64.cartridge().loadRomLBank(0, bank0.data(), bank0.size()), "load cart bank 0");
        require(c64.cartridge().loadRomLBank(1, bank1.data(), bank1.size()), "load cart bank 1");
        c64.attachCartridge(C64CartridgeMode::EightK);
        c64.setCartridgeBank(0);
        require(c64.cpuRead(0x8000) == 0x10, "cart bank 0 visible");
        c64.setCartridgeBank(1);
        require(c64.cpuRead(0x8000) == 0x20, "cart bank 1 visible");
    }

    {
        C64Platform c64;
        c64.reset(true);
        // IEC open-collector law and CIA2 input wiring.
        c64.iec().setLine(C64IecBus::Drive8, false, true, false);
        c64.runCycles(1);
        const uint8_t pa = c64.cia2().read(0x00);
        require((pa & 0x40u) == 0u, "IEC CLK low reflected on CIA2 PA6");
        require((pa & 0x80u) != 0u, "IEC DATA high reflected on CIA2 PA7");

        // Tape input surface.
        c64.tape().setSense(false);
        c64.tape().injectReadPulse(false);
        c64.runCycles(1);
        const uint8_t pb = c64.cia1().read(0x01);
        require((pb & 0x10u) == 0u, "tape sense reflected on CIA1 PB4");
        require((pb & 0x20u) == 0u, "tape read reflected on CIA1 PB5");
        require(c64.tape().pulseCount() == 1u, "tape pulse counter");
    }

    {
        // Half-cycle VIC contention surface proof.
        VicII vic;
        vic.reset(true);
        vic.write(0x11, 0x10); // display enabled, yscroll=0 => badline at $30
        while (vic.rasterLine() < 0x30u) vic.step(63);
        uint32_t stolen = vic.stepHalfCycles(2u * 64u);
        require(stolen > 0u, "half-cycle VIC contention reports stolen half cycles");
    }

    {
        // Synthetic RSID corpus runner: small program BRK-vector reaches deterministic execution.
        C64Platform c64;
        c64.reset(true);
        c64.pokeMemory(0x0801, 0xEA); // NOP
        c64.pokeMemory(0x0802, 0x00); // BRK
        RselCorpusCase cases[] = {{"synthetic-rsid-brk", 0x0801, 4}};
        auto r = c64.runRsidCorpus(cases, 1);
        require(r.complete && r.casesRun == 1 && r.casesPassed == 1, "synthetic RSID corpus runner");
    }

    std::puts("C64FullSystemSurfaceV415Tests passed");
    return 0;
}
