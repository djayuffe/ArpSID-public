// sid_core_exactness_v854_tests.cpp
//
// Ultra-low-level SID core exactness guard. Pins the three v854 hardware fixes
// plus the surrounding invariants they live in:
//
//  1. Envelope rate 15 (8 s) period is the die-measured 31251 cycles (was 31250).
//  2. The exponential-decay divisor latches when the counter REACHES a boundary,
//     so the decrement FROM level 93/54/26/14/6 already uses the slower divisor
//     ([94..255]→1, [55..93]→2, [27..54]→4, [15..26]→8, [7..14]→16, [0..6]→30).
//  3. Pulse width $FFF is a 1/4096-duty spike train (comparator t >= $FFF), not
//     constant silence; $000 remains constant high.
//
// All engines (BitPerfect, SidRegisterEngine, DrSID) share this envelope core,
// so these guards cover every render path.

#include "arpsid/core/sid_chip.h"
#include "arpsid/core/sid_envelope_core.h"

#include <cstdio>

using namespace ArpSID;

static int g_failures = 0;
static void require(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++g_failures; }
}

// Ticks needed for the envelope to leave `level` in Release at rate-0 (period 9).
static int ticksToLeaveLevel(Sid6581Envelope& e, uint8_t level, int cap = 4000) {
    int t = 0;
    while (e.envCounter == level && t < cap) { e.tick(); ++t; }
    return t;
}

int main() {
    // ── 1. Rate table: attack rate 15 steps after exactly 31251 cycles. ─────
    {
        require(Sid6581Envelope::kRatePeriods[15] == 31251u,
                "rate 15 period is the die-measured 31251");
        Sid6581Envelope e{};
        e.reset(); e.is6581 = false; e.attack = 15;
        e.gateOn();
        int ticks = 0;
        while (e.envCounter == 0u && ticks < 40000) { e.tick(); ++ticks; }
        require(ticks == 31251, "first attack-15 step lands on cycle 31251");
    }

    // ── 2. Exponential divisor boundaries (Release, rate 0 → period 9). ─────
    {
        struct B { uint8_t level; int events; };
        // events = expo divisor charged for the decrement FROM this level.
        const B cases[] = {
            {94, 1}, {93, 2},   // 1|2 boundary
            {55, 2}, {54, 4},   // 2|4 boundary
            {27, 4}, {26, 8},   // 4|8 boundary
            {15, 8}, {14, 16},  // 8|16 boundary
            {7, 16}, {6, 30},   // 16|30 boundary
        };
        for (const B& c : cases) {
            Sid6581Envelope e{};
            e.reset(); e.is6581 = false; e.release = 0; e.sustain = 0;
            e.envCounter = c.level;
            e.stage = Sid6581Envelope::Stage::Release;
            e.rateCounter = 0; e.expoCounter = 0;
            const int ticks = ticksToLeaveLevel(e, c.level);
            if (ticks != c.events * 9) {
                std::printf("EXPO: level %u took %d ticks, expected %d (div %d)\n",
                            c.level, ticks, c.events * 9, c.events);
                ++g_failures;
            }
        }
    }

    // ── 3. Pulse comparator edges via SIDVoice's public surface ONLY. ───────
    // Scope note (v855): this section exercises the SIDVoice waveform core, NOT
    // the C64-facing bus readback (C64::SidReadbackModel / $D41B). That surface
    // has its own guard in C64PlayBridgeRoutingV855Tests — the two are separate
    // implementations and one passing does not prove the other.
    {
        auto countHighs = [](uint16_t pw) -> int {
            SIDVoice v;
            v.reset();
            v.setModel(SIDModel::MOS8580);
            v.setWaveform(0x04u);            // pulse select (waveform nibble bit 2)
            v.setPulseWidth(pw);
            v.setFrequency(0x1000u);         // top12 advances by exactly 1 per cycle
            ArpSIDForensicConfig fc{};
            int highs = 0;
            for (int i = 0; i < 4096; ++i) {
                v.stepCycle();
                (void)v.renderFromPhase(fc);
                if (v.readOscillatorByte() == 0xFFu) ++highs;
            }
            return highs;
        };
        require(countHighs(0x000u) == 4096, "PW=$000 is constant high (t >= 0 always true)");
        require(countHighs(0x800u) == 2048, "PW=$800 is exactly 50% duty");
        require(countHighs(0xFFFu) == 1,    "PW=$FFF is a 1/4096-duty spike, not silence");
    }

    // ── 4. Surrounding invariants: sync topology + noise dead low nibble. ───
    {
        require(kSidHardSyncSourceOf[0] == 2 && kSidHardSyncSourceOf[1] == 0 &&
                kSidHardSyncSourceOf[2] == 1,
                "hard-sync/ring source topology is V1<-V3, V2<-V1, V3<-V2");
        SIDVoice v;
        v.reset();
        v.setModel(SIDModel::MOS8580);
        v.setWaveform(0x08u);                // noise select
        v.setFrequency(0xFFFFu);
        ArpSIDForensicConfig fc{};
        for (int i = 0; i < 512; ++i) {
            v.stepCycle();
            (void)v.renderFromPhase(fc);
        }
        // The OSC3 read byte for noise is the top 8 LFSR-mapped bits; the 12-bit
        // wave's low nibble is dead on hardware, which the readback can't show —
        // instead pin that the readback changes over time (LFSR is clocking).
        uint8_t first = v.readOscillatorByte();
        bool changed = false;
        for (int i = 0; i < 512 && !changed; ++i) {
            v.stepCycle();
            (void)v.renderFromPhase(fc);
            changed = (v.readOscillatorByte() != first);
        }
        require(changed, "noise LFSR clocks and reaches the OSC readback");
    }

    if (g_failures == 0) { std::printf("sid_core_exactness_v854_tests: PASS\n"); return 0; }
    std::printf("sid_core_exactness_v854_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
