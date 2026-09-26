// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/psid_header.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); }
}
static void be16(std::vector<uint8_t>& b, size_t off, uint16_t v) { b[off]=uint8_t(v>>8); b[off+1]=uint8_t(v); }
static void be32(std::vector<uint8_t>& b, size_t off, uint32_t v) { b[off]=uint8_t(v>>24); b[off+1]=uint8_t(v>>16); b[off+2]=uint8_t(v>>8); b[off+3]=uint8_t(v); }

static std::vector<uint8_t> buildStereoPsid() {
    const uint16_t dataOff = 0x7C;
    std::vector<uint8_t> b(dataOff, 0);
    b[0]='P'; b[1]='S'; b[2]='I'; b[3]='D';
    // v873: the second-SID selector ($7A) is a PSID v3 field, so a stereo header
    // must declare version 3 (a v2 header leaves $7A reserved and is mono).
    be16(b,0x04,3); be16(b,0x06,dataOff); be16(b,0x08,0); be16(b,0x0A,0x0800); be16(b,0x0C,0x0830);
    be16(b,0x0E,3); be16(b,0x10,2); be32(b,0x12,0x00000002u); // song 2 uses CIA timing
    b[0x7A] = 0x42; // second SID at $D420
    b.push_back(0x00); b.push_back(0x08);
    const uint8_t init[] = {
        0x8D,0x02,0xD4,       // STA $D402 = subtune-1
        0xA9,0x11,0x8D,0x00,0xD4, // chip0 reg0
        0xA9,0x22,0x8D,0x20,0xD4, // chip1 reg0 at $D420
        0xA9,0x77,0x8D,0x00,0x20, // regular RAM write via CPU/memory path
        0x60
    };
    b.insert(b.end(), init, init + sizeof(init));
    while (b.size() < dataOff + 2 + 0x30) b.push_back(0xEA);
    const uint8_t play[] = {
        0xEE,0x00,0x20,       // INC $2000, proves play writes through memory and preserves state
        0xAD,0x00,0x20,       // LDA $2000
        0x8D,0x01,0xD4,       // STA $D401
        0xA9,0x33,0x8D,0x21,0xD4, // chip1 reg1
        0x60
    };
    b.insert(b.end(), play, play + sizeof(play));
    return b;
}

int main() {
    using namespace ArpSID::C64;
    auto psid = buildStereoPsid();

    ArpSID::PsidHeader core{};
    require(ArpSID::psidParse(psid.data(), static_cast<uint32_t>(psid.size()), core) == ArpSID::PsidParseResult::OK,
            "PSID v2NG stereo header parses instead of being rejected");
    require(ArpSID::psidSidChipCount(core) == 2, "PSID reports two SID chips");
    require(ArpSID::psidSidBaseForChip(core, 0) == 0xD400, "chip0 base is $D400");
    require(ArpSID::psidSidBaseForChip(core, 1) == 0xD420, "chip1 base is $D420");
    require(ArpSID::psidUsesCiaTimingForSong(core, 2), "PSID speed bit reports CIA timing for song 2");

    C64Runtime rt;
    rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "runtime loads stereo PSID");
    require(rt.platform().sidChipCount() == 2, "platform configured for two SID banks");
    require(rt.platform().sidBase(1) == 0xD420, "platform second SID base installed");
    require(rt.platform().bootState().sidPayloadBytesLoaded > 0, "payload load count recorded");
    require(rt.platform().bootState().sidPayloadBytesTruncated == 0, "payload did not truncate");
    require(rt.platform().bootState().sidUsesCiaTiming, "boot state records selected-song CIA timing");

    require(rt.runInit(0, 256), "init runs");
    require(rt.sidSink().regsByChip[0][0] == 0x11, "chip0 SID write routed through CPU IO memory map");
    require(rt.sidSink().regsByChip[1][0] == 0x22, "chip1 SID write routed to second SID bank");
    require(rt.sidSink().regsByChip[0][2] == 0x01, "init got selected startSong-1 in A");
    require(rt.platform().peekMemory(0x2000) == 0x77, "init RAM store went through C64 memory path");

    require(rt.runPlay(256), "play runs");
    require(rt.platform().peekMemory(0x2000) == 0x78, "play INC wrote through C64 memory path and preserved init state");
    require(rt.sidSink().regsByChip[0][1] == 0x78, "play RAM value was written to chip0 SID register");
    require(rt.sidSink().regsByChip[1][1] == 0x33, "play wrote second SID register through configured base");
    require(rt.platform().lastSidChip() == 1, "platform tracks last SID chip");

    // Verify payload boundary is fail-safe and never wraps to zero page.
    std::vector<uint8_t> nearEnd(0x7C, 0);
    nearEnd[0]='P'; nearEnd[1]='S'; nearEnd[2]='I'; nearEnd[3]='D';
    be16(nearEnd,0x04,2); be16(nearEnd,0x06,0x7C); be16(nearEnd,0x08,0); be16(nearEnd,0x0A,0xFFFE); be16(nearEnd,0x0C,0);
    be16(nearEnd,0x0E,1); be16(nearEnd,0x10,1); be32(nearEnd,0x12,0);
    nearEnd.push_back(0xFE); nearEnd.push_back(0xFF); nearEnd.push_back(0xAA); nearEnd.push_back(0xBB); nearEnd.push_back(0xCC);
    C64Runtime rt2; rt2.reset(true);
    require(rt2.loadPsid(nearEnd.data(), nearEnd.size()), "near-end PSID loads");
    require(rt2.platform().peekMemory(0xFFFE) == 0xAA && rt2.platform().peekMemory(0xFFFF) == 0xBB,
            "near-end payload writes final bytes");
    require(rt2.platform().peekMemory(0x0000) == 0x2F, "near-end payload did not wrap into zero page");
    require(rt2.platform().bootState().sidPayloadBytesTruncated == 1, "near-end truncation is recorded");

    std::puts("C64PsidFullMemoryRuntimeV435 tests passed");
    return 0;
}
