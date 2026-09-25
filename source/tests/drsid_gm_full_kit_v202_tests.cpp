#include "arpsid/core/sid_gm_drum_kit.h"
#include "arpsid/engines/drsid_engine.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); }
}

int main() {
    using namespace ArpSID;

    std::array<bool, 8> seen{};
    for (int note = 35; note <= 81; ++note) {
        const auto spec = sidGMDrumSpecForNote(static_cast<uint8_t>(note));
        require(spec.note == note, "GM spec preserves note number");
        require(spec.drumClass != SidGMDrumClass::Unsupported, "every GM percussion note 35..81 has a concrete DrSID class");
        require(spec.name && spec.name[0], "every GM note has a full display name");
        require(spec.shortName && spec.shortName[0], "every GM note has a HUD abbreviation");
        require(spec.velocityScale > 0.0f && spec.velocityScale <= 1.10f, "velocity scale is sane");
        require(spec.decayScale > 0.0f && spec.decayScale <= 1.80f, "decay scale is sane");
        seen[static_cast<size_t>(DrSidEngine::drumTypeForGMClass(spec.drumClass))] = true;
    }
    for (size_t i = 0; i < seen.size(); ++i) require(seen[i], "GM table must exercise every DrSID drum family");

    DrSidEngine e;
    e.setSampleRate(48000.0);
    e.setClockFrequency(PAL_CLOCK_FREQ);
    for (int note = 35; note <= 81; ++note) {
        e.allNotesOff();
        e.triggerMidiNote(note, 0.75f);
        require(e.lastGMDrumNote() == note, "DrSID trigger stores the exact GM note");
        require(e.lastGMDrumClass() == static_cast<int>(sidGMDrumClassForNote(static_cast<uint8_t>(note))), "DrSID trigger class matches canonical GM table");
        require(e.lastGMDrumVelocity() > 0.0f, "DrSID trigger stores nonzero velocity");
        require(e.getActiveVoiceCount() > 0, "DrSID trigger arms at least one SID/overlay voice");
        float l[64]{}; float r[64]{}; float* out[2] = {l, r};
        e.processBlock(out, 64);
        float energy = 0.0f;
        for (int i = 0; i < 64; ++i) energy += std::fabs(l[i]) + std::fabs(r[i]);
        require(energy > 1.0e-5f, "every GM percussion note produces audio energy");
    }

    e.allNotesOff();
    require(e.lastGMDrumNote() == -1, "allNotesOff clears last GM note telemetry");
    require(e.lastGMDrumClass() == static_cast<int>(SidGMDrumClass::Unsupported), "allNotesOff clears GM class telemetry");
    std::cout << "drsid_gm_full_kit_v202_tests PASS\n";
    return 0;
}
