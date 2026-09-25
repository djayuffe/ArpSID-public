#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/drum_engine_router.h"
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <cmath>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID;

    DrSidEngine drsid;
    Sid808Engine sid808;
    DrumEngineRouter router(drsid, sid808);
    router.prepare(48000.0);

    DrumKitIdentity id{};
    id.context = DrumContext::DrSID_C64Wavetable;
    router.setActiveIdentity(id);

    drsid.allNotesOff();
    float l[64], r[64];
    std::fill(std::begin(l), std::end(l), 0.75f);
    std::fill(std::begin(r), std::end(r), -0.50f);
    router.processBlock(l, r, 64);

    float sum = 0.0f;
    for (int i = 0; i < 64; ++i) sum += std::fabs(l[i]) + std::fabs(r[i]);
    require(sum < 1.0e-6f, "inactive DrSID router replacing render clears stale buffers");

    router.noteOn(SidGMDrumClass::Kick, 127, 36);
    router.processBlock(l, r, 64);
    sum = 0.0f;
    for (int i = 0; i < 64; ++i) sum += std::fabs(l[i]) + std::fabs(r[i]);
    require(sum > 1.0e-5f, "active DrSID router replacing render produces audio");

    std::fill(std::begin(l), std::end(l), 0.33f);
    float* mono[2]{l, nullptr};
    drsid.allNotesOff();
    drsid.processReplacingBlock(mono, 64);
    sum = 0.0f;
    for (float v : l) sum += std::fabs(v);
    require(sum < 1.0e-6f, "DrSID processReplacingBlock clears mono stale buffer when inactive");

    std::cout << "DrSidReplacingContractV625Tests PASS\n";
    return 0;
}
