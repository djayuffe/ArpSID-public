#include "arpsid/core/psid_header.h"
#include "arpsid/core/c64_6510.h"
#include "arpsid/core/c64_bus.h"
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_psid_runtime.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int g_failures = 0;
static void check(bool cond, const char* msg) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}
template<typename T>
static void checkEq(T got, T expected, const char* msg) {
    if (got != expected) {
        std::fprintf(stderr, "FAIL: %s got=%lld expected=%lld\n", msg,
                     static_cast<long long>(got), static_cast<long long>(expected));
        ++g_failures;
    }
}

static void be16(std::vector<uint8_t>& b, size_t off, uint16_t v) { b[off]=uint8_t(v>>8); b[off+1]=uint8_t(v); }
static void be32(std::vector<uint8_t>& b, size_t off, uint32_t v) { b[off]=uint8_t(v>>24); b[off+1]=uint8_t(v>>16); b[off+2]=uint8_t(v>>8); b[off+3]=uint8_t(v); }
static void str32(std::vector<uint8_t>& b, size_t off, const char* s) { for (size_t i=0; i<32 && s[i]; ++i) b[off+i]=uint8_t(s[i]); }

static std::vector<uint8_t> buildPsid(bool rsid=false,
                                      uint8_t initVal=0x11,
                                      uint8_t playVal=0x22,
                                      uint16_t headerLoadAddr=0,
                                      uint16_t initAddr=0x0800,
                                      uint16_t playAddr=0x080A,
                                      uint16_t version=2) {
    const uint16_t dataOff = version >= 2 ? 0x7C : 0x76;
    std::vector<uint8_t> b(dataOff, 0);
    b[0] = rsid ? 'R' : 'P'; b[1]='S'; b[2]='I'; b[3]='D';
    be16(b, 4, version); be16(b, 6, dataOff); be16(b, 8, headerLoadAddr);
    be16(b, 0x0A, initAddr); be16(b, 0x0C, playAddr);
    be16(b, 0x0E, 1); be16(b, 0x10, 1); be32(b, 0x12, 0);
    str32(b, 0x16, "TestSID"); str32(b, 0x36, "TestAuthor"); str32(b, 0x56, "2024");
    // v893 refresh: RSID flag bit 1 ($0002) marks a C64 BASIC tune, which the
    // parser now honestly refuses (UnsupportedRsidBasic). These fixtures mean a
    // plain PAL RSID — use the video-standard bit ($0004) instead.
    if (version >= 2) { be16(b, 0x76, rsid ? 4 : 0); b[0x78]=0; b[0x79]=0; b[0x7A]=0; b[0x7B]=0; }
    const uint16_t loadAt = 0x0800;
    if (headerLoadAddr == 0) { b.push_back(uint8_t(loadAt)); b.push_back(uint8_t(loadAt >> 8)); }
    const size_t codeBase = b.size();
    const auto ensureOffset = [&](std::vector<uint8_t>& v, size_t off) { while (v.size() < codeBase + off) v.push_back(0xEA); };
    size_t initOff = size_t(initAddr - loadAt);
    ensureOffset(b, initOff);
    const uint8_t initCode[] = {0xA9,initVal,0x8D,0x00,0xD4,0x60};
    b.insert(b.end(), initCode, initCode + sizeof(initCode));
    if (playAddr != 0) {
        size_t playOff = size_t(playAddr - loadAt);
        ensureOffset(b, playOff);
        const uint8_t playCode[] = {0xA9,playVal,0x8D,0x01,0xD4,0x60};
        b.insert(b.end(), playCode, playCode + sizeof(playCode));
    }
    return b;
}

static void testPsidHeaderCompatibilityParser() {
    auto buf = buildPsid();
    ArpSID::PsidHeader hdr{};
    check(ArpSID::psidParse(buf.data(), uint32_t(buf.size()), hdr) == ArpSID::PsidParseResult::OK, "psidParse PSID OK");
    check(!hdr.isRsid, "PSID isRsid false");
    checkEq(hdr.version, uint16_t{2}, "version 2");
    checkEq(hdr.dataOffset, uint16_t{0x7C}, "dataOffset v2");
    checkEq(hdr.loadAddr, uint16_t{0}, "embedded load header zero");
    checkEq(hdr.effectiveLoadAddr, uint16_t{0x0800}, "embedded effective load $0800");
    checkEq(hdr.initAddr, uint16_t{0x0800}, "init addr big-endian");
    checkEq(hdr.playAddr, uint16_t{0x080A}, "play addr big-endian");
    checkEq(hdr.songs, uint16_t{1}, "songs big-endian");
    checkEq(hdr.startSong, uint16_t{1}, "start song big-endian");
    checkEq(hdr.speed, uint32_t{0}, "speed big-endian");
    check(std::strcmp(hdr.name, "TestSID") == 0, "name metadata");
    check(std::strcmp(hdr.author, "TestAuthor") == 0, "author metadata");
    check(std::strcmp(hdr.released, "2024") == 0, "released metadata");
    uint8_t ram[65536]{};
    ArpSID::psidLoadIntoRam(hdr, ram);
    checkEq(ram[0x0800], uint8_t{0xA9}, "RAM loaded init opcode");
    checkEq(ram[0x080A], uint8_t{0xA9}, "RAM loaded play opcode");

    auto rsid = buildPsid(true, 0x11, 0x22, 0, 0x0800, 0x0000);
    check(ArpSID::psidParse(rsid.data(), uint32_t(rsid.size()), hdr) == ArpSID::PsidParseResult::OK && hdr.isRsid, "RSID magic OK with C64-scheduled play vector");
    auto badRsid = buildPsid(true, 0x11, 0x22, 0, 0x0800, 0x080A);
    check(ArpSID::psidParse(badRsid.data(), uint32_t(badRsid.size()), hdr) == ArpSID::PsidParseResult::BadRsidHeader, "RSID direct play address rejected");
    rsid[0] = 'X';
    check(ArpSID::psidParse(rsid.data(), uint32_t(rsid.size()), hdr) == ArpSID::PsidParseResult::BadMagic, "bad magic rejected");
    auto v3 = buildPsid(false,0x11,0x22,0,0x0800,0x080A,3);
    check(ArpSID::psidParse(v3.data(), uint32_t(v3.size()), hdr) == ArpSID::PsidParseResult::OK, "PSID version 3 accepted");
}

static void testExplicitLoadAndRuntimeRtsTrampoline() {
    using namespace ArpSID::C64;
    auto explicitLoad = buildPsid(false, 0x55, 0x66, 0x0800);
    const PsidImage img = PsidParser::parse(explicitLoad.data(), explicitLoad.size());
    check(img.header.valid, "C64::PsidParser explicit load valid");
    checkEq(img.header.loadAddress, uint16_t{0x0800}, "C64::PsidParser header load");
    checkEq(img.effectiveLoadAddress, uint16_t{0x0800}, "C64::PsidParser effective explicit load");

    C64Runtime rt; rt.reset(true);
    check(rt.loadPsid(explicitLoad.data(), explicitLoad.size()), "C64Runtime loads explicit PSID");
    checkEq(rt.platform().peekMemory(0x0800), uint8_t{0xA9}, "runtime RAM has init opcode");
    check(rt.runInit(1), "runInit RTS trampoline returns via halt sentinel");
    checkEq(rt.platform().sidRegisterImage()[0], uint8_t{0x55}, "runInit wrote $55 to $D400 through bus");
    check(rt.runPlay(), "runPlay RTS trampoline returns via halt sentinel");
    checkEq(rt.platform().sidRegisterImage()[1], uint8_t{0x66}, "runPlay wrote $66 to $D401 through bus");
    check(rt.phi2InitUsed(), "PSID init/play state is owned by PHI2 CPU");
    check(!rt.platform().cpu().state().jammed,
          "inspection CPU mirror is not misreported as a live jammed authority");
}

static void testRsidRuntimeUsesMachineBootstrapWithoutOverwritingLoad() {
    using namespace ArpSID::C64;
    auto rsid = buildPsid(true, 0x44, 0x55, 0, 0x0800, 0x0000);
    C64Runtime rt; rt.reset(true);
    check(rt.loadPsid(rsid.data(), rsid.size()), "C64Runtime loads RSID");
    checkEq(rt.platform().peekMemory(0x0800), uint8_t{0xA9}, "RSID payload present before bootstrap");
    check(rt.runInit(1, 512), "RSID init runs through machine bootstrap");
    checkEq(rt.platform().peekMemory(0x0800), uint8_t{0xA9}, "RSID bootstrap does not overwrite common $0800 load address");
    checkEq(rt.platform().sidRegisterImage()[0], uint8_t{0x44}, "RSID init wrote through SID bus");
    check(rt.platform().bootState().mode == C64BootMode::RsidMachine, "RSID runtime reports machine boot mode");
    check(!rt.platform().trapBrkAsJam(), "RSID runtime leaves BRK as real IRQ service");
}

static void testPlatformSidBusAndReadOnlyUpperWindow() {
    using namespace ArpSID::C64;
    struct Sink final : SidRegisterSink {
        uint8_t reg = 0xFF; uint8_t val = 0xFF; uint32_t count = 0;
        void sidWrite(uint8_t r, uint8_t v, uint64_t) noexcept override { reg=r; val=v; ++count; }
        uint8_t sidRead(uint8_t, uint64_t) noexcept override { return 0; }
    } sink;
    C64Platform p; p.reset(true); p.attachSid(&sink);
    p.cpuWrite(0x0200, 0xAB); checkEq(p.cpuRead(0x0200), uint8_t{0xAB}, "RAM write/read roundtrip");
    p.cpuWrite(0xD400, 0x77); checkEq(sink.reg, uint8_t{0}, "SID reg 0 from $D400"); checkEq(sink.val, uint8_t{0x77}, "SID val $77");
    p.cpuWrite(0xD420, 0x88); checkEq(sink.reg, uint8_t{0}, "SID mirror reg 0 from $D420"); checkEq(sink.val, uint8_t{0x88}, "SID mirror val $88");
    const uint32_t before = sink.count;
    p.cpuWrite(0xD419, 0x99); p.cpuWrite(0xD41A, 0x99); p.cpuWrite(0xD41B, 0x99);
    checkEq(sink.count, before, "$D419-$D41B read-only writes rejected");
    p.cpuWrite(0xD41C, 0x1C); p.cpuWrite(0xD41F, 0x1F);
    checkEq(p.sidRegisterImage()[0x1C], uint8_t{0x00}, "$D41C ENV3 read-only deterministic mirror");
    checkEq(p.sidRegisterImage()[0x1F], uint8_t{0x00}, "$D41F unmapped SID hole rejected");
}

static void testStandardOpcodeSubsetAndRmwHelper() {
    using namespace ArpSID::C64;
    C64Platform p; p.reset(true);
    // LDA #$42; STA $0300; LDX #$99; STX $0301; LDY #$77; STY $0302; JSR $0220; BRK
    const uint8_t prog[] = {0xA9,0x42,0x8D,0x00,0x03,0xA2,0x99,0x8E,0x01,0x03,0xA0,0x77,0x8C,0x02,0x03,0x20,0x20,0x02,0x00};
    for (size_t i=0;i<sizeof(prog);++i) p.pokeMemory(uint16_t(0x0200+i), prog[i]);
    // subroutine: INC $0300; DEC $0301; RTS
    const uint8_t sub[] = {0xEE,0x00,0x03,0xCE,0x01,0x03,0x60};
    for (size_t i=0;i<sizeof(sub);++i) p.pokeMemory(uint16_t(0x0220+i), sub[i]);
    p.cpu().state().pc = 0x0200;
    for (int i=0; i<16 && !p.cpu().state().jammed; ++i) p.executeInstruction();
    checkEq(p.peekMemory(0x0300), uint8_t{0x43}, "INC abs official opcode executed");
    checkEq(p.peekMemory(0x0301), uint8_t{0x98}, "DEC abs official opcode executed");
    checkEq(p.peekMemory(0x0302), uint8_t{0x77}, "STY abs official opcode executed");
    check(c64Mos6510OpcodeIsReadModifyWrite(0xC6) && c64Mos6510OpcodeIsReadModifyWrite(0xCE) &&
          c64Mos6510OpcodeIsReadModifyWrite(0xD6) && c64Mos6510OpcodeIsReadModifyWrite(0xDE) &&
          c64Mos6510OpcodeIsReadModifyWrite(0xE6) && c64Mos6510OpcodeIsReadModifyWrite(0xEE) &&
          c64Mos6510OpcodeIsReadModifyWrite(0xF6) && c64Mos6510OpcodeIsReadModifyWrite(0xFE),
          "RMW helper covers official INC/DEC low nibble $6/$E opcodes");
    check(!c64Mos6510OpcodeIsReadModifyWrite(0xEA) && !c64Mos6510OpcodeIsReadModifyWrite(0xA9), "RMW helper rejects non-RMW opcodes");
}

static void testSidBusQueueReclaimsConsumedSlots() {
    using namespace ArpSID::C64;
    SidBusQueue q;
    for (size_t i = 0; i < SidBusQueue::kCapacity; ++i) {
        check(q.push(BusCycle{0xD400u, uint8_t(i), false, static_cast<uint64_t>(i), 0u}), "SID bus queue accepts capacity events");
    }
    for (size_t i = 0; i < SidBusQueue::kCapacity; ++i) {
        const SidBusEvent* ev = q.nextBeforeOrAt(static_cast<uint64_t>(i));
        check(ev != nullptr && ev->cycle.data == uint8_t(i), "SID bus queue drains events in PHI2 order");
    }
    check(q.empty(), "SID bus queue empty after drain");
    check(q.push(BusCycle{0xD401u, 0x7Fu, false, 2048u, 0u}), "SID bus queue reclaims drained storage without allocation");
    const SidBusEvent* ev = q.nextBeforeOrAt(2048u);
    check(ev != nullptr && ev->cycle.address == 0xD401u && ev->cycle.data == 0x7Fu, "SID bus queue returns event after compaction");
}

int main() {
    testPsidHeaderCompatibilityParser();
    testExplicitLoadAndRuntimeRtsTrampoline();
    testRsidRuntimeUsesMachineBootstrapWithoutOverwritingLoad();
    testPlatformSidBusAndReadOnlyUpperWindow();
    testSidBusQueueReclaimsConsumedSlots();
    testStandardOpcodeSubsetAndRmwHelper();
    if (g_failures == 0) { std::printf("PsidRsidRuntimeComprehensiveV265Tests PASS\n"); return 0; }
    std::fprintf(stderr, "%d test(s) FAILED\n", g_failures);
    return 1;
}
