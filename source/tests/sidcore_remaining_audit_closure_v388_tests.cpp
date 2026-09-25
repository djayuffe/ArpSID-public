
#include <iostream>
#include "arpsid/core/sid_envelope_core.h"
#include "arpsid/core/sid_combined_wave_model.h"
#include "arpsid/core/math_utils.h"
#include "arpsid/core/sid_chip.h"

int main() {
    static_assert(sizeof(ArpSID::Fixed64_32) == 8, "Q32 must be 64-bit");
    ArpSID::Sid6581Envelope e; e.setSustainNibble(10); e.stage = ArpSID::Sid6581Envelope::Stage::Decay; e.envCounter = 100; e.setSustainNibble(12);
    if (e.stage != ArpSID::Sid6581Envelope::Stage::Sustain || e.envCounter != 204) { std::cerr << "sustain decay closure failed\n"; return 1; }
    e.is6581 = true; e.performHardRestart(); if (e.hardRestartWindowCycles == 0 || e.stage != ArpSID::Sid6581Envelope::Stage::Release || e.envCounter != 0) { std::cerr << "hard restart closure failed\n"; return 2; }
    uint32_t seed = 0x12345678u, seedBefore = seed;
    const auto a = ArpSID::sidAnalogCombined12_Ultra(0x0fff,0x0ffe,0x0fff,0,false,false,true,false,true,0x0fff,35.0f,5.0f,3,seed);
    const auto b = ArpSID::sidAnalogCombined12_Ultra(0x0fff,0x0ffe,0x0fff,0,false,false,true,false,true,0x0fff,35.0f,5.0f,3,seed);
    if (seed != seedBefore || a != b || a > 0x0fff) { std::cerr << "combined determinism/sag closure failed\n"; return 3; }
    ArpSID::ArpSIDForensicConfig fc; fc.bitPerfectMode = true; if (fc.active(1.0f) != 0.0f || !fc.frozen()) { std::cerr << "forensic freeze closure failed\n"; return 4; }
    std::cout << "SIDCORE v388 remaining audit closure passed\n";
    return 0;
}
