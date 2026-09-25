#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_telemetry.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); }
}
static void be16(std::vector<uint8_t>& b, size_t off, uint16_t v) { b[off]=uint8_t(v>>8); b[off+1]=uint8_t(v); }
static void be32(std::vector<uint8_t>& b, size_t off, uint32_t v) { b[off]=uint8_t(v>>24); b[off+1]=uint8_t(v>>16); b[off+2]=uint8_t(v>>8); b[off+3]=uint8_t(v); }

static std::vector<uint8_t> buildBootProbePsid() {
    const uint16_t dataOff = 0x7C;
    std::vector<uint8_t> b(dataOff, 0);
    b[0]='P'; b[1]='S'; b[2]='I'; b[3]='D';
    be16(b,0x04,2); be16(b,0x06,dataOff); be16(b,0x08,0); be16(b,0x0A,0x0800); be16(b,0x0C,0x0820);
    be16(b,0x0E,3); be16(b,0x10,2); be32(b,0x12,0);
    b.push_back(0x00); b.push_back(0x08);
    const uint8_t init[] = {
        0x8D,0x02,0xD4,       // STA $D402; proves A=startSong-1 from bootstrap
        0xA5,0x01,             // LDA $0001
        0x8D,0x00,0xD4,       // STA $D400; proves processor port booted to $37
        0xA5,0x00,             // LDA $0000
        0x8D,0x01,0xD4,       // STA $D401; proves DDR booted to $2F
        0xBA,                  // TSX
        0x8E,0x03,0xD4,       // STX $D403; proves stack was initialized by bootstrap
        0x60                   // RTS back to bootstrap JMP $FFFF
    };
    b.insert(b.end(), init, init + sizeof(init));
    while (b.size() < dataOff + 2 + 0x20) b.push_back(0xEA);
    const uint8_t play[] = { 0xA9,0x66,0x8D,0x04,0xD4,0x60 };
    b.insert(b.end(), play, play + sizeof(play));
    return b;
}

int main() {
    using namespace ArpSID::C64;
    auto psid = buildBootProbePsid();
    C64Runtime rt;
    rt.reset(true);
    require(!rt.platform().bootState().coldBootComplete, "plain reset has not run SID load boot sequence");
    require(rt.loadPsid(psid.data(), psid.size()), "PSID loads");
    const auto& loaded = rt.platform().bootState();
    require(loaded.coldBootComplete, "loadPsid performs deterministic C64 cold boot before payload load");
    require(loaded.sidImageLoaded, "loadPsid records SID image loaded state");
    require(rt.platform().peekMemory(0x0000) == 0x2F, "$0000 DDR initialized before SID init");
    require(rt.platform().peekMemory(0x0001) == 0x37, "$0001 processor port initialized before SID init");
    require(rt.platform().effectiveProcessorPort() == 0xF7, "effective processor port reflects DDR outputs and pulled-up inputs");
    require(rt.platform().peekMemory(0x0400) == 0x20, "screen RAM initialized to spaces");
    require((rt.platform().colorRam()[0] & 0x0F) == 0x0E, "color RAM initialized deterministically");

    require(rt.runInit(0, 128), "SID init runs through reset-vector bootstrap");
    const auto& boot = rt.platform().bootState();
    require(boot.sidInitBootstrapInstalled, "init bootstrap installed");
    require(boot.sidInitDispatched, "init dispatched through boot sequence");
    require(boot.sidInitCompleted, "init completed");
    require(boot.sidPlayReady, "play routine marked ready after init");
    require(boot.bootstrapAddress == 0x0334, "bootstrap lives in cassette-buffer area, not over $0801 SID payload");
    require(boot.initInstructionsExecuted > 0, "init instruction count tracked");
    require(boot.initEndPhi2 >= boot.initStartPhi2, "init PHI2 interval tracked");
    require(rt.sidSink().regs[2] == 0x01, "init received startSong-1 in A");
    require(rt.sidSink().regs[0] == 0xF7, "init saw effective booted $0001 processor port");
    require(rt.sidSink().regs[1] == 0x2F, "init saw booted $0000 port DDR");
    require(rt.sidSink().regs[3] == 0xFD, "init saw stack after bootstrap JSR return-address push");
    require(rt.runPlay(64), "play trampoline still runs after booted init");
    require(rt.sidSink().regs[4] == 0x66, "play wrote SID register after proper init");

    C64ChipSnapshot snap = c64BuildTelemetrySnapshot(rt.platform(), true, 77, 2, 50.0f, "BOOT", "TEST", "2026", rt.image().header.songs, boot.currentSubtune);
    require(snap.bootColdComplete, "telemetry exposes cold boot completion");
    require(snap.sidImageLoaded, "telemetry exposes SID image loaded");
    require(snap.sidInitCompleted, "telemetry exposes SID init completion");
    require(snap.sidPlayReady, "telemetry exposes play-ready state");
    require(snap.sidBootstrapAddress == 0x0334, "telemetry exposes bootstrap address");
    require(snap.sidInitInstructionsExecuted == boot.initInstructionsExecuted, "telemetry exposes init instruction count");

    std::puts("C64BootSequenceSidInitV430 tests passed");
    return 0;
}
