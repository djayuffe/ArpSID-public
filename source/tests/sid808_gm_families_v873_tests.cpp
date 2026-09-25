// v873: ClosedHat/Cowbell/Rim-derived GM percussion now consume their profiles
// (distinct, audible, non-clamped voices) instead of collapsing to the generic
// voice for their engine drum.
#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/engines/sid808_gm_projection.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "Sid808GMFamiliesV873Tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

float peakOf(const float* l, const float* r, int n) {
    float p = 0.0f;
    for (int i = 0; i < n; ++i) {
        p = std::max(p, std::fabs(l[i]));
        p = std::max(p, std::fabs(r[i]));
    }
    return p;
}

} // namespace

int main() {
    using namespace ArpSID;

    DrumEngineHostBridge bridge;
    bridge.prepare(48000.0);
    require(bridge.loadFactorySlot(120), "test bridge loads SID808 factory slot");

    struct Hit { const char* name; SidGMDrumClass cls; std::uint8_t note; Sid808Drum drum; Sid808PercProfile prof; };
    const Hit hits[] = {
        {"Pedal Hi-Hat",   SidGMDrumClass::ClosedHat, 44, Sid808Drum::ClosedHat, Sid808PercProfile::PedalHat},
        {"Tambourine",     SidGMDrumClass::ClosedHat, 54, Sid808Drum::ClosedHat, Sid808PercProfile::Tambourine},
        {"Cabasa",         SidGMDrumClass::ClosedHat, 69, Sid808Drum::ClosedHat, Sid808PercProfile::Shaker},
        {"Short Guiro",    SidGMDrumClass::ClosedHat, 73, Sid808Drum::ClosedHat, Sid808PercProfile::GuiroShort},
        {"Ride Bell",      SidGMDrumClass::Cowbell,   53, Sid808Drum::Cowbell,   Sid808PercProfile::RideBell},
        {"High Agogo",     SidGMDrumClass::Cowbell,   67, Sid808Drum::Cowbell,   Sid808PercProfile::AgogoHigh},
        {"Low Agogo",      SidGMDrumClass::Cowbell,   68, Sid808Drum::Cowbell,   Sid808PercProfile::AgogoLow},
        {"Mute Triangle",  SidGMDrumClass::Cowbell,   80, Sid808Drum::Cowbell,   Sid808PercProfile::TriangleMute},
        {"Vibraslap",      SidGMDrumClass::Rim,       58, Sid808Drum::Rim,       Sid808PercProfile::Vibraslap},
        {"Claves",         SidGMDrumClass::Rim,       75, Sid808Drum::Rim,       Sid808PercProfile::Claves},
        {"High Wood Block",SidGMDrumClass::Rim,       76, Sid808Drum::Rim,       Sid808PercProfile::WoodBlockHigh},
        {"Low Wood Block", SidGMDrumClass::Rim,       77, Sid808Drum::Rim,       Sid808PercProfile::WoodBlockLow},
    };

    float l[16000]{};
    float r[16000]{};
    for (const auto& h : hits) {
        for (auto& x : l) x = 0.0f;
        for (auto& x : r) x = 0.0f;
        bridge.noteOn(h.cls, 100u, h.note);
        bridge.processBlock(l, r, 12000);
        const auto& e = bridge.sid808Engine();
        require(e.lastPercProfile() == h.prof, h.name);            // profile selected
        require(peakOf(l, r, 12000) > 1e-5f, h.name);              // audible
        require(e.lastInitialFreqForDrum(h.drum) < 65535u, h.name); // not SID-max clamped
    }

    // Rim-family instruments were previously identical max-freq clicks; verify pitch distinctness.
    bridge.noteOn(SidGMDrumClass::Rim, 100u, 75u);
    bridge.processBlock(l, r, 8000);
    const auto clavesFreq = bridge.sid808Engine().lastInitialFreqForDrum(Sid808Drum::Rim);
    bridge.noteOn(SidGMDrumClass::Rim, 100u, 77u);
    bridge.processBlock(l, r, 8000);
    const auto woodLowFreq = bridge.sid808Engine().lastInitialFreqForDrum(Sid808Drum::Rim);
    require(clavesFreq != woodLowFreq, "Claves and Low Wood Block are pitch-distinct");

    // Plain cowbell (note 56, no family profile) keeps the generic cowbell voice.
    bridge.noteOn(SidGMDrumClass::Cowbell, 100u, 56u);
    bridge.processBlock(l, r, 8000);
    require(bridge.sid808Engine().lastPercProfile() == Sid808PercProfile::Default,
            "plain cowbell keeps generic voice");

    // Class-mismatch guard: a Crash note (49) forced onto ClosedHat must not leak a profile.
    Sid808VoiceConfig base{};
    base.freq = 4000u;
    const auto bad = sid808GMProjectionSpecForNote(49u, SidGMDrumClass::ClosedHat);
    const auto ov = sid808GMNoteOverride(bad, base, Sid808HitOverride{});
    require(!ov.hasProfile && !ov.hasFreq, "class mismatch does not project a profile/frequency");

    std::cout << "Sid808GMFamiliesV873Tests PASS\n";
    return 0;
}
