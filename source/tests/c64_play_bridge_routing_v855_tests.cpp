// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_play_bridge_routing_v855_tests.cpp
//
// v855 P0 timing-fix guards. The audio renderer consumes SID writes from
// C64SidBridgeState::timedWrites; before v855 the discrete PSID VBI and PSID-CIA
// play paths attached only the internal runtime sink, so play-routine SID writes
// never reached the audio bridge — audible updates were quantized to block edges
// (up to ~12-23 ms displacement). These tests prove:
//
//   A. VBI play-routine SID writes arrive in the external timed-write bridge
//      with PHI2 cycle stamps, and bridge/sink write deltas agree.
//   B. PSID-CIA play-routine SID writes also arrive in the external bridge and
//      the service reports full completion (serviceComplete).
//   C. The C64-facing SidReadbackModel renders PW=$FFF as the hardware 1/4096
//      spike on $D41B OSC3 readback (parity with the v854 SIDVoice fix).

#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/core/c64_sid_readback.h"

#include <cstdio>
#include <vector>

using namespace ArpSID::C64;

static int g_failures = 0;
static void require(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++g_failures; }
}

// ── Minimal PSID v2 builder (same layout as the v574 fixture) ────────────────
static void be16(std::vector<uint8_t>& b, size_t off, uint16_t v) {
    b[off] = uint8_t(v >> 8); b[off + 1] = uint8_t(v);
}
static void be32(std::vector<uint8_t>& b, size_t off, uint32_t v) {
    b[off] = uint8_t(v >> 24); b[off+1] = uint8_t(v >> 16);
    b[off+2] = uint8_t(v >> 8); b[off+3] = uint8_t(v);
}
static std::vector<uint8_t> buildPsid(uint16_t loadAddr, uint16_t initAddr, uint16_t playAddr,
                                      uint32_t speed,
                                      const std::vector<uint8_t>& initCode,
                                      const std::vector<uint8_t>& playCode) {
    const uint16_t dataOff = 0x7C;
    std::vector<uint8_t> b(dataOff, 0);
    b[0]='P'; b[1]='S'; b[2]='I'; b[3]='D';
    be16(b, 4, 2); be16(b, 6, dataOff); be16(b, 8, 0);
    be16(b, 0x0A, initAddr); be16(b, 0x0C, playAddr);
    be16(b, 0x0E, 1); be16(b, 0x10, 1);
    be32(b, 0x12, speed);
    b.push_back(uint8_t(loadAddr)); b.push_back(uint8_t(loadAddr >> 8));
    const size_t codeBase = b.size();
    const uint16_t highAddr = static_cast<uint16_t>(
        std::max<int>(int(initAddr) + int(initCode.size()), int(playAddr) + int(playCode.size())));
    b.resize(codeBase + size_t(highAddr - loadAddr), 0xEA);
    for (size_t i = 0; i < initCode.size(); ++i) b[codeBase + size_t(initAddr - loadAddr) + i] = initCode[i];
    for (size_t i = 0; i < playCode.size(); ++i) b[codeBase + size_t(playAddr - loadAddr) + i] = playCode[i];
    return b;
}

// init: LDA #$00 / RTS.  play: LDA #$21 / STA $D404 / LDA #$42 / STA $D401 / RTS
static const std::vector<uint8_t> kInit = { 0xA9, 0x00, 0x60 };
static const std::vector<uint8_t> kPlay = { 0xA9, 0x21, 0x8D, 0x04, 0xD4,
                                            0xA9, 0x42, 0x8D, 0x01, 0xD4, 0x60 };

int main() {
    // ── A. VBI play writes reach the external audio bridge. ─────────────────
    {
        const auto psid = buildPsid(0x1000, 0x1000, 0x1010, /*speed VBI*/0u, kInit, kPlay);
        C64Runtime rt;
        rt.reset(true);
        require(rt.loadPsid(psid.data(), psid.size()), "VBI fixture loads");
        require(rt.runInit(), "VBI fixture init completes");

        C64SidBridgeState bridge;
        bridge.reset();
        const uint64_t sinkBefore = rt.sidSink().writeCount;
        require(rt.runPlay(4096, &bridge), "VBI play completes");
        const uint64_t sinkDelta = rt.sidSink().writeCount - sinkBefore;

        require(bridge.timedWriteCount >= 2u,
                "VBI play SID writes arrive in the external timed-write bridge");
        require(uint64_t(bridge.timedWriteCount) == sinkDelta,
                "bridge and internal sink observe the same play write count");
        bool sawD404 = false, sawD401 = false, stamped = true;
        for (uint32_t i = 0; i < bridge.timedWriteCount; ++i) {
            const auto& w = bridge.timedWrites[i];
            if (w.reg == 0x04u && w.value == 0x21u) sawD404 = true;
            if (w.reg == 0x01u && w.value == 0x42u) sawD401 = true;
            if (i > 0 && bridge.timedWrites[i].phi2Cycle < bridge.timedWrites[i-1].phi2Cycle) stamped = false;
        }
        require(sawD404 && sawD401, "both play-routine register writes are present");
        require(stamped, "bridge writes carry monotonic PHI2 cycle stamps");

        // Without an external sink the bridge must remain untouched (baseline).
        C64SidBridgeState idle;
        idle.reset();
        require(rt.runPlay(4096), "play without sink still completes");
        require(idle.timedWriteCount == 0u, "unattached bridge receives nothing");
    }

    // ── B. PSID-CIA play writes reach the external bridge + serviceComplete. ─
    {
        const auto psid = buildPsid(0x1000, 0x1000, 0x1010, /*speed CIA*/1u, kInit, kPlay);
        C64Runtime rt;
        rt.reset(true);
        require(rt.loadPsid(psid.data(), psid.size()), "CIA fixture loads");
        require(rt.runInit(), "CIA fixture init completes");

        C64SidBridgeState bridge;
        bridge.reset();
        const auto service = rt.runPsidCiaPlaybackServiceTicks(262144u, &bridge);
        require(service.playAddressEntered, "CIA service entered the play routine");
        require(service.serviceComplete, "CIA service reports FULL completion (idle-return proof)");
        require(bridge.timedWriteCount >= 2u,
                "CIA play SID writes arrive in the external timed-write bridge");
    }

    // ── C. $D41B OSC3 readback: PW=$FFF is a 1/4096 spike, not silence. ─────
    {
        SidReadbackModel rb;
        rb.reset(false); // 8580
        uint64_t phi2 = 100;
        rb.write(phi2, 0x0Eu, 0x00u); // V3 freq lo
        rb.write(phi2, 0x0Fu, 0x10u); // V3 freq hi → $1000: top12 +1 per cycle
        rb.write(phi2, 0x10u, 0xFFu); // V3 PW lo
        rb.write(phi2, 0x11u, 0x0Fu); // V3 PW hi → $FFF
        rb.write(phi2, 0x12u, 0x41u); // V3 control: pulse | gate
        int highs = 0;
        for (int i = 0; i < 4096; ++i) {
            ++phi2;
            if (rb.read(phi2, 0x1Bu) == 0xFFu) ++highs;
        }
        require(highs == 1, "$D41B pulse readback at PW=$FFF is a 1/4096-duty spike");

        rb.write(phi2, 0x10u, 0x00u); rb.write(phi2, 0x11u, 0x00u); // PW=$000
        int highs0 = 0;
        for (int i = 0; i < 4096; ++i) {
            ++phi2;
            if (rb.read(phi2, 0x1Bu) == 0xFFu) ++highs0;
        }
        require(highs0 == 4096, "$D41B pulse readback at PW=$000 is constant high");
    }

    // ── D. v856: RTI-exiting play routines complete instead of jamming. ─────
    // A large PSID class exits play with RTI (written for IRQ-entry players).
    // Under the JSR dispatch that consumed our 2-byte frame + 1 garbage byte
    // and previously jammed/rolled back EVERY frame (audibly choppy/silent).
    {
        // play: LDA #$37 / STA $D404 / RTI
        const std::vector<uint8_t> rtiPlay = { 0xA9, 0x37, 0x8D, 0x04, 0xD4, 0x40 };
        const auto psid = buildPsid(0x1000, 0x1000, 0x1010, /*VBI*/0u, kInit, rtiPlay);
        C64Runtime rt;
        rt.reset(true);
        require(rt.loadPsid(psid.data(), psid.size()), "RTI fixture loads");
        require(rt.runInit(), "RTI fixture init completes");
        C64SidBridgeState bridge;
        bridge.reset();
        require(rt.runPlay(4096, &bridge), "RTI-exiting play frame is accepted as complete");
        require(bridge.timedWriteCount >= 1u, "RTI-exit play write reached the audio bridge");
        // And it stays stable across repeated frames (the choppy symptom was
        // per-frame rollback), including writes on every subsequent frame.
        for (int f = 0; f < 8; ++f) {
            const uint32_t before = bridge.timedWriteCount;
            require(rt.runPlay(4096, &bridge), "repeated RTI play frames all complete");
            require(bridge.timedWriteCount > before, "each RTI frame delivers its SID write");
        }
    }

    // ── E. v856: tune-programmed CIA tempo is preserved (multi-speed). ──────
    // init programs Timer A to 9852 ($267C ≈ 100 Hz PAL, a 2x multi-speed tune)
    // then RTS. The CIA bootstrap install must NOT clobber it back to the
    // 50 Hz default (19705).
    {
        const std::vector<uint8_t> tempoInit = {
            0xA9, 0x7C, 0x8D, 0x04, 0xDC,   // LDA #$7C / STA $DC04
            0xA9, 0x26, 0x8D, 0x05, 0xDC,   // LDA #$26 / STA $DC05
            0x60                            // RTS
        };
        const auto psid = buildPsid(0x1000, 0x1000, 0x1020, /*CIA*/1u, tempoInit, kPlay);
        C64Runtime rt;
        rt.reset(true);
        require(rt.loadPsid(psid.data(), psid.size()), "tempo fixture loads");
        require(rt.runInit(), "tempo fixture init completes");
        require(rt.platform().cia1().latchA() == 0x267Cu,
                "tune-programmed CIA Timer A latch (100 Hz) survives bootstrap install");

        // Control: a CIA tune whose init does NOT touch the timer still gets
        // the 50 Hz default.
        const auto psidDefault = buildPsid(0x1000, 0x1000, 0x1020, 1u, kInit, kPlay);
        C64Runtime rtDefault;
        rtDefault.reset(true);
        require(rtDefault.loadPsid(psidDefault.data(), psidDefault.size()), "default fixture loads");
        require(rtDefault.runInit(), "default fixture init completes");
        require(rtDefault.platform().cia1().latchA() == 19705u,
                "untouched timer gets the PAL 50 Hz default latch");
    }

    if (g_failures == 0) { std::printf("c64_play_bridge_routing_v855_tests: PASS\n"); return 0; }
    std::printf("c64_play_bridge_routing_v855_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
