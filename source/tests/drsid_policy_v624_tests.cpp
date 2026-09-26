// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/engines/drsid_engine.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID;

    DrSidEngine e;
    e.setSampleRate(48000.0);
    e.setAllowUnsupportedMidiFallback(false);
    require(!e.allowUnsupportedMidiFallback(), "unsupported MIDI fallback defaults controllable/off");
    e.triggerMidiNote(1, 1.0f); // unsupported note
    require(e.lastGMDrumClass() == static_cast<int>(SidGMDrumClass::Unsupported),
            "unsupported MIDI note is reported as unsupported");
    require(e.drumEnvelopeLevel(DrSidEngine::DrumType::Kick) <= 0.0f,
            "unsupported MIDI note does not register a fallback kick when strict");

    e.setAllowUnsupportedMidiFallback(true);
    e.triggerMidiNote(1, 1.0f);
    require(e.drumEnvelopeLevel(DrSidEngine::DrumType::Kick) > 0.0f,
            "unsupported MIDI fallback can be explicitly enabled and registers fallback kick");

    // Rim is no longer part of the semantic hat choke family.
    e.allNotesOff();
    e.triggerMidiNote(46, 1.0f); // open hat
    require(e.gmNoteLevel(46) > 0.0f, "open hat GM level active");
    e.triggerMidiNote(37, 1.0f); // rim
    require(e.gmNoteLevel(46) > 0.0f, "rim does not semantically clear open-hat GM ledger");
    require(e.gmNoteLevel(37) > 0.0f, "rim GM level active");

    std::cout << "DrSidPolicyV624Tests PASS\n";
    return 0;
}
