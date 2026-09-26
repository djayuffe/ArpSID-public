// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_sid_bridge.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

static void req(bool b, const char* msg) { if (!b) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); } }

static void put_be16(std::vector<uint8_t>& v, size_t off, uint16_t x) { v[off] = uint8_t(x >> 8); v[off+1] = uint8_t(x); }
static void put_be32(std::vector<uint8_t>& v, size_t off, uint32_t x) { v[off] = uint8_t(x >> 24); v[off+1] = uint8_t(x >> 16); v[off+2] = uint8_t(x >> 8); v[off+3] = uint8_t(x); }
static void put_text(std::vector<uint8_t>& v, size_t off, const char* s) { for (size_t i=0; s[i] && i<32; ++i) v[off+i] = uint8_t(s[i]); }

static std::vector<uint8_t> make_psid(bool rsid, uint16_t load, uint16_t init, uint16_t play, bool embeddedLoad) {
    std::vector<uint8_t> v(0x7C, 0);
    v[0] = rsid ? 'R' : 'P'; v[1] = 'S'; v[2] = 'I'; v[3] = 'D';
    put_be16(v, 4, 2); put_be16(v, 6, 0x7C); put_be16(v, 8, embeddedLoad ? 0 : load);
    put_be16(v, 10, init); put_be16(v, 12, play); put_be16(v, 14, 1); put_be16(v, 16, 1); put_be32(v, 18, 0);
    put_text(v, 22, "ArpSID PSID Runtime Test"); put_text(v, 54, "Ulf/ArpSID"); put_text(v, 86, "2026");
    // v893 refresh: RSID flag bit 1 ($0002) marks a C64 BASIC tune, which the
    // parser now honestly refuses (UnsupportedRsidBasic). This fixture means a
    // plain PAL RSID — use the video-standard bit ($0004) instead.
    put_be16(v, 0x76, rsid ? 0x0004 : 0);
    if (embeddedLoad) { v.push_back(uint8_t(load)); v.push_back(uint8_t(load >> 8)); }
    // init @ load: LDA #$11; STA $D400; BRK
    v.push_back(0xA9); v.push_back(0x11); v.push_back(0x8D); v.push_back(0x00); v.push_back(0xD4); v.push_back(0x00);
    while ((load + (v.size() - 0x7C - (embeddedLoad ? 2 : 0))) < play) v.push_back(0xEA);
    // play @ play: LDA #$22; STA $D401; BRK
    v.push_back(0xA9); v.push_back(0x22); v.push_back(0x8D); v.push_back(0x01); v.push_back(0xD4); v.push_back(0x00);
    return v;
}

int main() {
    using namespace ArpSID::C64;
    auto psid = make_psid(false, 0x1000, 0x1000, 0x1010, false);
    auto img = PsidParser::parse(psid.data(), psid.size());
    req(img.header.valid && !img.header.rsid, "PSID header parses");
    req(img.header.version == 2 && img.header.dataOffset == 0x7C, "big-endian version/dataOffset parse");
    req(img.header.loadAddress == 0x1000 && img.header.initAddress == 0x1000 && img.header.playAddress == 0x1010, "big-endian address parse");
    req(img.header.songs == 1 && img.header.startSong == 1, "big-endian song metadata parse");
    req(std::strcmp(img.header.name, "ArpSID PSID Runtime Test") == 0, "metadata strings parse");
    req(img.effectiveLoadAddress == 0x1000 && img.payloadSize > 8, "explicit load address handling");

    auto rsid = make_psid(true, 0x2000, 0x2000, 0x0000, true);
    auto rimg = PsidParser::parse(rsid.data(), rsid.size());
    req(rimg.header.valid && rimg.header.rsid, "RSID header parses");
    req(rimg.header.loadAddress == 0 && rimg.effectiveLoadAddress == 0x2000, "embedded little-endian load address handling");
    req(rimg.payload[0] == 0xA9 && rimg.payload[1] == 0x11, "embedded load address stripped from payload");

    C64Runtime rt; rt.reset(true);
    req(rt.loadPsid(psid.data(), psid.size()), "C64 RAM loading accepts PSID");
    req(rt.platform().peekMemory(0x1000) == 0xA9 && rt.platform().peekMemory(0x1004) == 0xD4, "payload loaded into C64 RAM");
    req(rt.runInit(), "init trampoline executes through C64Runtime CPU");
    req(rt.platform().sidRegisterImage()[0] == 0x11, "init SID write reaches real $D400 bus path");
    req(rt.sidSink().regs[0] == 0x11 && rt.sidSink().writeCount >= 1, "attached SID sink observes init bus write");
    req(rt.runPlay(), "play trampoline executes through C64Runtime CPU");
    req(rt.platform().sidRegisterImage()[1] == 0x22, "play SID write reaches real $D401 bus path");
    req(rt.sidSink().regs[1] == 0x22 && rt.sidSink().lastReg == 1 && rt.sidSink().lastValue == 0x22, "play sink mirrors last SID write");

    C64SidBridgeState bridge;
    bridge.sidWrite(static_cast<uint8_t>(32u + 4u), 0x55u, 123u);
    req(bridge.lastChip == 1u && bridge.lastReg == 4u && bridge.lastValue == 0x55u,
        "C64 SID bridge preserves encoded multi-SID chip metadata");
    req(bridge.regsByChip[1][4] == 0x55u && bridge.regs[4] == 0u,
        "C64 SID bridge keeps non-primary SID writes out of chip0 mirror");
    req(bridge.timedWriteCount == 1u &&
        bridge.timedWrites[0].chip == 1u &&
        bridge.timedWrites[0].reg == 4u &&
        bridge.timedWrites[0].value == 0x55u,
        "C64 SID bridge timed writes retain chip index and local register");
    bridge.sidWrite(4u, 0x66u, 124u);
    req(bridge.regsByChip[0][4] == 0x66u && bridge.regs[4] == 0x66u,
        "C64 SID bridge chip0 writes still update legacy single-SID mirror");

    C64Runtime rt2; rt2.reset(true);
    rt2.setRsidPlaybackMode(RsidPlaybackMode::Compatible);
    req(rt2.loadPsid(rsid.data(), rsid.size()), "C64 RAM loading accepts RSID with embedded load address");
    req(rt2.platform().peekMemory(0x2000) == 0xA9, "RSID payload loaded at embedded little-endian address");
    req(rt2.runInit() && rt2.platform().sidRegisterImage()[0] == 0x11, "RSID init executes via CPU/SID bus");
    req(!rt2.runPlay(), "RSID direct play trampoline is unavailable when play address is zero");
    req(!rt2.phi2MachineEnabled(), "PHI2 runtime starts behind explicit feature flag");

    rt2.enablePhi2Machine(true);
    req(rt2.phi2MachineEnabled() && rt2.phi2MachineReady(), "PHI2 runtime flag syncs post-init RSID state");
    req(rt2.phi2Machine().memory().peekRam(0x2000) == 0xA9, "PHI2 runtime owns mirrored PSID payload RAM");

    rt2.phi2Machine().memory().pokeRam(0x3000, 0xA9); // LDA #$44
    rt2.phi2Machine().memory().pokeRam(0x3001, 0x44);
    rt2.phi2Machine().memory().pokeRam(0x3002, 0x8D); // STA $D401
    rt2.phi2Machine().memory().pokeRam(0x3003, 0x01);
    rt2.phi2Machine().memory().pokeRam(0x3004, 0xD4);
    rt2.phi2Machine().memory().pokeRam(0x3005, 0xEA); // NOP
    rt2.phi2Machine().cpu().setPc(0x3000);
    const auto phi2Ok = rt2.runRsidMachineCycles(6, 16, nullptr);
    req(phi2Ok.completedCycleBudget && !phi2Ok.cpuJammed && !phi2Ok.instructionBudgetHit,
        "PHI2 RSID route completes supported LDA/STA sequence");
    req(rt2.sidSink().regs[1] == 0x44 && rt2.sidSink().lastReg == 1,
        "PHI2 RSID route forwards SID write to runtime sink");
    req(rt2.phi2Diagnostics().sidWrites >= 1,
        "PHI2 diagnostics count production-routed SID writes");
    req(rt2.phi2UnsupportedOpcodeCount() == 0,
        "supported PHI2 sequence has no unsupported opcode diagnostics");

    // v605: full micro CPU implements all 151 official opcodes including CLI (0x58).
    // Verify CLI executes without jamming.
    rt2.phi2Machine().memory().pokeRam(0x3100, 0x58); // CLI — now fully supported
    rt2.phi2Machine().memory().pokeRam(0x3101, 0xEA); // NOP
    rt2.phi2Machine().cpu().setPc(0x3100);
    const auto phi2Cli = rt2.runRsidMachineCycles(4, 16, nullptr);
    req(!phi2Cli.cpuJammed,
        "v605 micro CPU executes official CLI opcode without jamming");
    req(rt2.phi2Machine().cpu().unsupportedOpcodeCount(0x58) == 0,
        "CLI opcode has zero unsupported-opcode count in v605 micro CPU");
    // KIL/JAM opcode (0x02) must still jam the CPU unconditionally.
    rt2.phi2Machine().memory().pokeRam(0x3200, 0x02); // KIL
    rt2.phi2Machine().cpu().setPc(0x3200);
    const auto phi2Kil = rt2.runRsidMachineCycles(4, 16, nullptr);
    req(phi2Kil.cpuJammed,
        "KIL opcode jams PHI2 CPU in v605 micro CPU");

    std::cout << "PsidRsidC64RuntimeV261Tests PASS\n";
    return 0;
}
