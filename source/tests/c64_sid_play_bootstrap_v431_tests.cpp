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

static std::vector<uint8_t> buildStatefulPlayPsid() {
    const uint16_t dataOff = 0x7C;
    std::vector<uint8_t> b(dataOff, 0);
    b[0]='P'; b[1]='S'; b[2]='I'; b[3]='D';
    be16(b,0x04,2); be16(b,0x06,dataOff); be16(b,0x08,0); be16(b,0x0A,0x0800); be16(b,0x0C,0x0840);
    be16(b,0x0E,1); be16(b,0x10,1); be32(b,0x12,0);
    b.push_back(0x00); b.push_back(0x08);
    const uint8_t init[] = {
        0xA9,0x00,0x85,0x02,       // LDA #0; STA $02 frame counter
        0xA9,0xAA,0x85,0x10,       // LDA #$AA; STA $10 state byte
        0xA9,0x11,0x8D,0x00,0xD4, // STA $D400 prove init ran
        0x60
    };
    b.insert(b.end(), init, init + sizeof(init));
    while (b.size() < dataOff + 2 + 0x40) b.push_back(0xEA);
    const uint8_t play[] = {
        0xE6,0x02,                 // INC $02; state must persist across play calls
        0xA5,0x02,                 // LDA $02
        0x8D,0x05,0xD4,           // STA $D405 frame count to SID
        0xA5,0x10,                 // LDA $10
        0x8D,0x06,0xD4,           // STA $D406 prove init-created RAM state still exists
        0xA5,0x01,                 // LDA $0001
        0x8D,0x07,0xD4,           // STA $D407 prove processor port not reset by play
        0xBA,                       // TSX
        0x8E,0x08,0xD4,           // STX $D408 prove play call used JSR stack, not direct PC
        0x60
    };
    b.insert(b.end(), play, play + sizeof(play));
    return b;
}

int main() {
    using namespace ArpSID::C64;
    const auto psid = buildStatefulPlayPsid();
    C64Runtime rt;
    rt.reset(true);
    require(rt.loadPsid(psid.data(), psid.size()), "PSID loads");
    require(!rt.runPlay(64), "play is refused until init completed");
    require(rt.runInit(1, 256), "init completes through boot bootstrap");
    require(rt.sidSink().regs[0] == 0x11, "init wrote SID register");
    require(rt.platform().peekMemory(0x0002) == 0x00, "init frame counter starts at zero");
    require(rt.platform().peekMemory(0x0010) == 0xAA, "init state byte installed");

    require(rt.runPlay(128), "first play dispatch completes");
    const auto& b1 = rt.platform().bootState();
    require(b1.sidPlayBootstrapInstalled, "play bootstrap installed");
    require(b1.sidPlayDispatched, "play dispatched");
    require(b1.sidPlayCompleted, "play completed");
    require(b1.playBootstrapAddress == 0x0350, "play bootstrap lives after init bootstrap");
    require(b1.playInstructionsExecuted > 0, "play instruction count tracked");
    require(b1.playEndPhi2 >= b1.playStartPhi2, "play PHI2 interval tracked");
    require(b1.playCallCount == 1, "first play call counted");
    require(rt.sidSink().regs[5] == 0x01, "first play sees persisted frame counter = 1");
    require(rt.sidSink().regs[6] == 0xAA, "play sees init-created RAM state");
    require(rt.sidSink().regs[7] == 0xF7, "play sees effective processor port, not direct reset garbage");
    require(rt.sidSink().regs[8] == 0xFD, "play routine is called by JSR and sees pushed return address");

    require(rt.runPlay(128), "second play dispatch completes");
    const auto& b2 = rt.platform().bootState();
    require(b2.playCallCount == 2, "second play call counted");
    require(rt.sidSink().regs[5] == 0x02, "second play preserves and increments state across frames");
    require(rt.platform().peekMemory(0x0002) == 0x02, "RAM frame counter persists after two plays");

    C64ChipSnapshot snap = c64BuildTelemetrySnapshot(rt.platform(), true, 99, b2.playCallCount, 50.0f,
                                                     "PLAYBOOT", "TEST", "2026", rt.image().header.songs, b2.currentSubtune);
    require(snap.sidPlayBootstrapInstalled, "telemetry exposes play bootstrap installed");
    require(snap.sidPlayDispatched, "telemetry exposes play dispatched");
    require(snap.sidPlayCompleted, "telemetry exposes play completed");
    require(snap.sidPlayBootstrapAddress == 0x0350, "telemetry exposes play bootstrap address");
    require(snap.sidPlayInstructionsExecuted == b2.playInstructionsExecuted, "telemetry exposes play instruction count");
    require(snap.sidPlayCallCount == 2, "telemetry exposes play call count");

    std::puts("C64SidPlayBootstrapV431 tests passed");
    return 0;
}
