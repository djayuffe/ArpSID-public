// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/engines/drum_stem_mixer.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static bool near(float a, float b) {
    const float d = a - b;
    return d > -0.00001f && d < 0.00001f;
}

int main() {
    using namespace ArpSID;
    DrumStemFrame f{};
    f.mainL = 0.10f; f.mainR = 0.11f;
    f.drsidL = 0.20f; f.drsidR = 0.21f;
    f.sid808L = 0.30f; f.sid808R = 0.31f;
    f.digiL = 0.40f; f.digiR = 0.41f;
    f.useDrsid = true;
    f.useSid808 = true;
    f.useDigi = true;

    float l = 0.0f, r = 0.0f;
    mixDrumStemFrame(f, DrumStemMixPolicy::ReplaceWithSid808, l, r);
    require(near(l, 0.30f) && near(r, 0.31f), "SID808 replace policy uses only SID808 stem");

    f.useSid808 = false;
    mixDrumStemFrame(f, DrumStemMixPolicy::ReplaceWithSid808, l, r);
    require(near(l, 0.0f) && near(r, 0.0f), "SID808 replace policy clears output when SID808 inactive");

    f.useSid808 = true;
    f.useDigi = false;
    mixDrumStemFrame(f, DrumStemMixPolicy::ExplicitSelectedStem, l, r);
    require(near(l, 0.60f) && near(r, 0.63f), "explicit selected stem adds only enabled stems plus main");

    f.useDigi = true;
    mixDrumStemFrame(f, DrumStemMixPolicy::AdditiveDrumMachine, l, r);
    require(near(l, 1.0f) && near(r, 1.0f), "additive DrumMachine sums and clamps all stems");

    std::cout << "DrumStemMixerV651Tests PASS\n";
    return 0;
}
