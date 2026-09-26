// Copyright (C) 2024-2026 Ulf Bertilsson
// v873 audit P0-10: the init/play/CIA bootstraps live in a fixed $0334..$037C
// window. A tune that loads over that window must NOT have its bytes overwritten by
// the bootstrap — the bootstrap block relocates to a free page instead. Common tunes
// (no overlap) keep the $0334 base unchanged.
#include "arpsid/core/c64_psid_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using ArpSID::C64::C64Runtime;

namespace {

int failures = 0;
void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "PsidBootstrapRelocationV873Tests FAIL: %s\n", msg); ++failures; }
}

void be16(std::vector<uint8_t>& v, size_t off, uint16_t val) {
    v[off] = uint8_t(val >> 8); v[off + 1] = uint8_t(val & 0xFF);
}

} // namespace

int main() {
    // ---- Collision case: tune loads $0300..$03FF, covering the $0334 bootstrap ----
    // init at $0300: LDA $0334 ; STA $D418 ; RTS. A marker byte $0A sits at $0334.
    // If the bootstrap is (wrongly) placed at $0334 it overwrites the marker with its
    // own opcode; if it relocates, the marker survives and reaches $D418.
    {
        std::vector<uint8_t> psid(0x7C, 0);
        psid[0]='P'; psid[1]='S'; psid[2]='I'; psid[3]='D';
        be16(psid, 0x04, 2);        // version 2
        be16(psid, 0x06, 0x7C);     // dataOffset
        be16(psid, 0x08, 0x0300);   // loadAddr — low, overlaps $0334
        be16(psid, 0x0A, 0x0300);   // initAddr
        be16(psid, 0x0C, 0x0000);   // playAddr (none)
        be16(psid, 0x0E, 1);
        be16(psid, 0x10, 1);
        std::vector<uint8_t> payload(0x100, 0x00);          // $0300..$03FF
        const uint8_t code[] = {0xADu, 0x34u, 0x03u,        // LDA $0334
                                0x8Du, 0x18u, 0xD4u,        // STA $D418
                                0x60u};                     // RTS
        for (size_t i = 0; i < sizeof(code); ++i) payload[i] = code[i];
        payload[0x34] = 0x0Au;                              // marker at $0334
        psid.insert(psid.end(), payload.begin(), payload.end());

        C64Runtime rt;
        require(rt.loadPsid(psid.data(), psid.size()), "low-loading PSID loads");
        require(rt.runInit(0, 4096), "init runs to completion with relocated bootstrap");
        require(rt.platform().peekMemory(0x0334u) == 0x0Au,
                "tune byte at $0334 survived (bootstrap relocated off the tune)");
        require(rt.sidSink().regsByChip[0][0x18] == 0x0Au,
                "init read the intact $0334 marker and wrote it to $D418");
    }

    // ---- Control: a normal tune ($1000) does not collide; bootstrap stays at $0334 ----
    {
        std::vector<uint8_t> psid(0x7C, 0);
        psid[0]='P'; psid[1]='S'; psid[2]='I'; psid[3]='D';
        be16(psid, 0x04, 2);
        be16(psid, 0x06, 0x7C);
        be16(psid, 0x08, 0x1000);   // loadAddr — normal, no overlap
        be16(psid, 0x0A, 0x1000);
        be16(psid, 0x0C, 0x0000);
        be16(psid, 0x0E, 1);
        be16(psid, 0x10, 1);
        const uint8_t code[] = {0xA9u, 0x07u, 0x8Du, 0x18u, 0xD4u, 0x60u}; // LDA #$07; STA $D418; RTS
        psid.insert(psid.end(), code, code + sizeof(code));

        C64Runtime rt;
        require(rt.loadPsid(psid.data(), psid.size()), "normal PSID loads");
        require(rt.runInit(0, 4096), "normal init runs");
        require(rt.sidSink().regsByChip[0][0x18] == 0x07u, "normal tune plays (bootstrap at $0334)");
    }

    std::printf(failures == 0 ? "PsidBootstrapRelocationV873Tests PASS\n"
                              : "PsidBootstrapRelocationV873Tests FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
