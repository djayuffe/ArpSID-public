// Copyright (C) 2024-2026 Ulf Bertilsson
// sid_envelope_dac_6581_v607_tests.cpp
// Regression for the "timing/sound wrong for all non-8580" bug.
//
// The 8-bit envelope counter drives the envelope DAC directly on BOTH the
// 6581 and the 8580 — the counter value IS the volume multiplier (0..255).
// A prior revision bit-reversed the 6581 counter in dacOutput(), scrambling
// every amplitude level (quiet attacks rendered loud, sustain levels jumbled)
// and breaking the sound + perceived envelope timing of every 6581 voice.
//
// These tests pin the correct behavior:
// I. dacOutput() == envCounter for every counter value, on both models.
// II. The 6581 envelope DAC is strictly monotonic in the counter.
// III. 6581 and 8580 produce identical envelope output (no model bit-reverse).

#include "arpsid/core/sid_envelope_core.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using ArpSID::Sid6581Envelope;

    Sid6581Envelope e6581{}; e6581.is6581 = true;
    Sid6581Envelope e8580{}; e8580.is6581 = false;

    // I. Direct counter→DAC mapping on both models.
    for (int c = 0; c <= 255; ++c) {
        e6581.envCounter = static_cast<uint8_t>(c);
        e8580.envCounter = static_cast<uint8_t>(c);
        require(e6581.dacOutput() == static_cast<uint8_t>(c),
                "6581 dacOutput must equal the raw envelope counter");
        require(e8580.dacOutput() == static_cast<uint8_t>(c),
                "8580 dacOutput must equal the raw envelope counter");
    }

    // II. Strict monotonicity for 6581 (no bit-reversal scrambling).
    for (int c = 1; c <= 255; ++c) {
        e6581.envCounter = static_cast<uint8_t>(c);
        const uint8_t hi = e6581.dacOutput();
        e6581.envCounter = static_cast<uint8_t>(c - 1);
        const uint8_t lo = e6581.dacOutput();
        require(hi > lo, "6581 envelope DAC must be strictly monotonically increasing");
    }

    // III. Identical 6581 vs 8580 envelope output (model must not reorder levels).
    for (int c = 0; c <= 255; ++c) {
        e6581.envCounter = static_cast<uint8_t>(c);
        e8580.envCounter = static_cast<uint8_t>(c);
        require(e6581.dacOutput() == e8580.dacOutput(),
                "6581 and 8580 envelope DAC outputs must match for every counter value");
    }

    // Spot-checks that would FAIL under the old bit-reversed behavior:
    // bitReverse8(0x01) == 0x80, bitReverse8(0x40) == 0x02.
    e6581.envCounter = 0x01u;
    require(e6581.dacOutput() == 0x01u, "6581 quiet level $01 must stay $01 (not $80)");
    e6581.envCounter = 0x40u;
    require(e6581.dacOutput() == 0x40u, "6581 level $40 must stay $40 (not $02)");

    std::cout << "SidEnvelopeDac6581V607Tests PASS\n";
    return 0;
}
