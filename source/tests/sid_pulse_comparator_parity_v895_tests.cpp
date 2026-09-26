// Copyright (C) 2024-2026 Ulf Bertilsson
// v895 SID-core split-brain closure tests.
//
// The audit found THREE separately maintained copies of the SID pulse
// comparator law. SIDVoice and the register engine agreed; SidReadbackModel
// had silently dropped the 6581 comparator bias (PW<=$020 → +1, PW>=$F00 →
// +2), so $D41B OSC3 polling of a 6581 pulse at extreme widths disagreed
// with the rendered audio comparator. All three now delegate to the single
// canonical sidPulseComparator12() in sid_combined_wave_model.h; the dead
// local copies of the combined-wave helpers (SIDVoice privates, register
// engine statics, legacy non-Ultra sidAnalogCombined12) were removed.

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "arpsid/core/c64_sid_readback.h"
#include "arpsid/core/sid_chip.h"
#include "arpsid/core/sid_combined_wave_model.h"
#include "arpsid/engines/sid_register_engine.h"

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "sid_pulse_comparator_parity_v895_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

// Canonical-law truth used to check every engine surface.
uint16_t expected(uint16_t top12, uint16_t pw, bool is6581) {
    return ArpSID::sidPulseComparator12(top12, pw, is6581);
}

void canonicalLawEdgeCases() {
    using ArpSID::sidPulseComparator12;
    // PW=$000 constant high on both models, bias never applied.
    require(sidPulseComparator12(0x000u, 0x000u, true) == 0x0FFFu, "PW=$000 high (6581)");
    require(sidPulseComparator12(0xFFFu, 0x000u, false) == 0x0FFFu, "PW=$000 high (8580)");
    // PW=$FFF spike train: high only at top12 == $FFF on 8580; the 6581 +2
    // bias clamps to $FFF so behavior is identical there.
    require(sidPulseComparator12(0xFFEu, 0xFFFu, false) == 0x0000u, "PW=$FFF low below top");
    require(sidPulseComparator12(0xFFFu, 0xFFFu, false) == 0x0FFFu, "PW=$FFF spike at top");
    require(sidPulseComparator12(0xFFFu, 0xFFFu, true) == 0x0FFFu, "PW=$FFF spike at top (6581)");
    // 6581 low-width bias: PW=$010 behaves as $011.
    require(sidPulseComparator12(0x010u, 0x010u, true) == 0x0000u, "6581 low-width bias (+1)");
    require(sidPulseComparator12(0x011u, 0x010u, true) == 0x0FFFu, "6581 low-width bias edge");
    require(sidPulseComparator12(0x010u, 0x010u, false) == 0x0FFFu, "8580 has no bias");
    // 6581 high-width bias: PW=$F00 behaves as $F02.
    require(sidPulseComparator12(0xF00u, 0xF00u, true) == 0x0000u, "6581 high-width bias (+2)");
    require(sidPulseComparator12(0xF01u, 0xF00u, true) == 0x0000u, "6581 high-width bias (+2) mid");
    require(sidPulseComparator12(0xF02u, 0xF00u, true) == 0x0FFFu, "6581 high-width bias edge");
    require(sidPulseComparator12(0xF00u, 0xF00u, false) == 0x0FFFu, "8580 high width unbiased");
    // TEST forces high regardless of width/phase.
    require(sidPulseComparator12(0x000u, 0x800u, true, true) == 0x0FFFu, "TEST forces high");
}

void registerEngineWrapperDelegates() {
    using namespace ArpSID;
    const uint16_t pws[] = {0x000u, 0x001u, 0x010u, 0x020u, 0x021u, 0x7FFu,
                            0x800u, 0xEFFu, 0xF00u, 0xF01u, 0xFFEu, 0xFFFu};
    for (uint16_t pw : pws) {
        for (uint32_t t = 0u; t < 0x1000u; t += 0x0Fu) {
            const uint16_t top = static_cast<uint16_t>(t & 0x0FFFu);
            require(sidRegisterPulseComparator12(top, pw, SIDModel::MOS6581) ==
                        expected(top, pw, true),
                    "register-engine comparator matches canonical law (6581)");
            require(sidRegisterPulseComparator12(top, pw, SIDModel::MOS8580) ==
                        expected(top, pw, false),
                    "register-engine comparator matches canonical law (8580)");
        }
    }
}

void sidVoiceOscReadbackMatchesCanonicalLaw() {
    using namespace ArpSID;
    SIDVoice::initTablesOnce();
    ArpSIDForensicConfig fc{};
    fc.enable = false;
    fc.bitPerfectMode = true;

    const uint16_t pws[] = {0x010u, 0xF00u, 0xFFFu};
    const uint16_t tops[] = {0x00Fu, 0x010u, 0x011u, 0xF00u, 0xF01u, 0xF02u, 0xFFEu, 0xFFFu};
    for (bool is6581 : {true, false}) {
        SIDVoice v;
        v.reset();
        v.setModel(is6581 ? SIDModel::MOS6581 : SIDModel::MOS8580);
        v.setWaveform(0x40u); // pulse only
        for (uint16_t pw : pws) {
            v.setPulseWidth(pw);
            for (uint16_t top : tops) {
                auto s = v.snapshot();
                s.phase = static_cast<uint32_t>(top) << 12u;
                v.restore(s);
                (void)v.renderFromPhase(fc);
                const uint8_t want = static_cast<uint8_t>(expected(top, pw, is6581) >> 4u);
                require(v.readOscillatorByte() == want,
                        "SIDVoice pulse OSC readback matches canonical comparator law");
            }
        }
    }
}

void readbackModelNowAppliesThe6581Bias() {
    using namespace ArpSID::C64;
    // Voice 3, freq $1000: phase advances $1000/cycle, so top12 == elapsed cycles.
    // pw=$F00 on a 6581: the +2 bias means top12 $F00/$F01 read LOW and $F02
    // reads HIGH. The pre-v895 readback (raw comparator) read HIGH at $F00 —
    // the split-brain this closure removes.
    auto oscAtTop = [](bool mos6581, uint16_t pw, uint16_t top) -> uint8_t {
        SidReadbackModel rb;
        rb.reset(mos6581);
        uint64_t phi2 = 10u;
        rb.write(phi2, 0x0Eu, 0x00u);                        // v3 freq lo
        rb.write(phi2, 0x0Fu, 0x10u);                        // v3 freq hi → $1000/cycle
        rb.write(phi2, 0x10u, static_cast<uint8_t>(pw & 0xFFu));        // v3 pw lo
        rb.write(phi2, 0x11u, static_cast<uint8_t>((pw >> 8u) & 0x0Fu)); // v3 pw hi
        rb.write(phi2, 0x12u, 0x40u);                        // v3 control: pulse
        return rb.read(phi2 + top, 0x1Bu);
    };
    require(oscAtTop(true, 0xF00u, 0xF00u) == 0x00u,
            "6581 OSC3 readback applies the +2 high-width comparator bias at $F00");
    require(oscAtTop(true, 0xF00u, 0xF01u) == 0x00u,
            "6581 OSC3 readback applies the +2 high-width comparator bias at $F01");
    require(oscAtTop(true, 0xF00u, 0xF02u) == 0xFFu,
            "6581 OSC3 readback goes high exactly at the biased edge $F02");
    require(oscAtTop(false, 0xF00u, 0xF00u) == 0xFFu,
            "8580 OSC3 readback stays unbiased at $F00");
    require(oscAtTop(true, 0x010u, 0x010u) == 0x00u,
            "6581 OSC3 readback applies the +1 low-width comparator bias");
    require(oscAtTop(false, 0x010u, 0x010u) == 0xFFu,
            "8580 OSC3 low-width readback stays unbiased");
}

} // namespace

int main() {
    canonicalLawEdgeCases();
    registerEngineWrapperDelegates();
    sidVoiceOscReadbackMatchesCanonicalLaw();
    readbackModelNowAppliesThe6581Bias();
    std::cout << "sid_pulse_comparator_parity_v895_tests PASS\n";
    return 0;
}
