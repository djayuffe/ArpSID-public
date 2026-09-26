// Copyright (C) 2024-2026 Ulf Bertilsson
// v873 P0-12: init-time I/O map. A PSID init routine in RAM under ROM must have
// the shadowing ROM banked out (via $01) so its code actually runs, while common
// tunes in normal RAM keep the full $37 map unchanged.
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_psid_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using ArpSID::C64::C64Platform;
using ArpSID::C64::C64Runtime;

namespace {

int failures = 0;
void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "PsidInitIomapV873Tests FAIL: %s\n", msg); ++failures; }
}

// The $01 immediate byte the init bootstrap emits lives at entry+9:
//   +0 SEI  +1 LDX #$FF  +3 TXS  +4 LDA #$2F  +6 STA $00  +8 LDA #imm  +9 <imm>
uint8_t emittedInitIomap(uint16_t initAddr) {
    C64Platform p;
    p.reset(true);
    const uint16_t entry = p.installPsidInitBootstrap(initAddr, 0u, 0x0334u, false);
    return p.peekMemory(static_cast<uint16_t>(entry + 9u));
}

} // namespace

int main() {
    using C = C64Platform;

    // Pure helper: only RAM-under-ROM regions deviate from $37.
    require(C::psidInitIomapForAddress(0x1000) == 0x37, "normal RAM $1000 -> $37");
    require(C::psidInitIomapForAddress(0x9FFF) == 0x37, "top of low RAM $9FFF -> $37");
    require(C::psidInitIomapForAddress(0xA000) == 0x36, "$A000 (under BASIC) -> $36");
    require(C::psidInitIomapForAddress(0xBFFF) == 0x36, "$BFFF (under BASIC) -> $36");
    require(C::psidInitIomapForAddress(0xC000) == 0x37, "$C000 (RAM) -> $37");
    require(C::psidInitIomapForAddress(0xD000) == 0x37, "$D000 (I/O) left at $37");
    require(C::psidInitIomapForAddress(0xE000) == 0x35, "$E000 (under KERNAL) -> $35");
    require(C::psidInitIomapForAddress(0xFFFF) == 0x35, "$FFFF (under KERNAL) -> $35");

    // Integration: the emitted bootstrap $01 matches the helper.
    require(emittedInitIomap(0x1000) == 0x37, "init $1000 emits $01=$37 (unchanged)");
    require(emittedInitIomap(0xA000) == 0x36, "init $A000 emits $01=$36 (RAM under BASIC)");
    require(emittedInitIomap(0xE000) == 0x35, "init $E000 emits $01=$35 (RAM under KERNAL)");

    // End-to-end: a PSID whose init routine lives in RAM under BASIC ($A000) must
    // actually execute. The PHI2 machine's BASIC ROM is 0xEA-filled, so with the old
    // $01=$37 map the CPU would run NOPs from ROM and never touch the SID; only the
    // corrected $36 map exposes the RAM code that writes $D418.
    {
        auto be16 = [](std::vector<uint8_t>& v, size_t off, uint16_t val) {
            v[off] = uint8_t(val >> 8); v[off + 1] = uint8_t(val & 0xFF);
        };
        std::vector<uint8_t> psid(0x7C, 0);
        psid[0]='P'; psid[1]='S'; psid[2]='I'; psid[3]='D';
        be16(psid, 0x04, 2);       // version 2
        be16(psid, 0x06, 0x7C);    // dataOffset
        be16(psid, 0x08, 0xA000);  // loadAddr = RAM under BASIC
        be16(psid, 0x0A, 0xA000);  // initAddr
        be16(psid, 0x0C, 0x0000);  // playAddr (none)
        be16(psid, 0x0E, 1);       // songs
        be16(psid, 0x10, 1);       // startSong
        // init code at $A000: LDA #$0F; STA $D418; RTS
        const uint8_t code[] = {0xA9u, 0x0Fu, 0x8Du, 0x18u, 0xD4u, 0x60u};
        for (uint8_t b : code) psid.push_back(b);

        C64Runtime rt;
        require(rt.loadPsid(psid.data(), psid.size()), "PSID with $A000 init loads");
        require(rt.runInit(0, 4096), "RAM-under-BASIC init runs to completion");
        require(rt.sidSink().regsByChip[0][0x18] == 0x0Fu,
                "init code in RAM under BASIC executed and wrote $D418=$0F (was shadowed by ROM before P0-12)");
    }

    std::printf(failures == 0 ? "PsidInitIomapV873Tests PASS\n" : "PsidInitIomapV873Tests FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
