#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/core/drum_context.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static float energy(const std::vector<float>& v) {
    float e = 0.0f;
    for (float x : v) e += std::fabs(x);
    return e;
}

int main() {
    using namespace ArpSID;

    constexpr int n = 512;
    DrSidEngine canonicalDrSid;
    DrumEngineHostBridge bridge(canonicalDrSid);
    bridge.prepare(48000.0);

    require(bridge.loadFactorySlot(static_cast<int>(kSid808NewFactoryRange.first)),
            "SID808 factory slot loads into bridge");
    require(bridge.activeIdentity().context == DrumContext::SID808_AnalogProjection,
            "bridge active context is SID808");

    std::vector<float> l(static_cast<std::size_t>(n), 0.75f);
    std::vector<float> r(static_cast<std::size_t>(n), -0.75f);
    bridge.noteOn(SidGMDrumClass::Kick, 120u, 36u);
    bridge.processBlock(l.data(), r.data(), n);

    require(energy(l) > 0.001f || energy(r) > 0.001f,
            "SID808 bridge render produces audio after noteOn");
    require(std::fabs(l[0] - 0.75f) > 1.0e-6f && std::fabs(r[0] + 0.75f) > 1.0e-6f,
            "bridge render is replacing/authoritative, not additive over stale host buffer");

    bridge.allNotesOff();
    std::fill(l.begin(), l.end(), 0.25f);
    std::fill(r.begin(), r.end(), -0.25f);
    bridge.processBlock(l.data(), r.data(), n);
    require(std::fabs(l[0] - 0.25f) > 1.0e-6f && std::fabs(r[0] + 0.25f) > 1.0e-6f,
            "silent bridge render also clears stale host buffer");

    bridge.loadFactorySlot(47);
    require(bridge.activeIdentity().context == DrumContext::DrSID_C64Wavetable,
            "bridge can switch to DrSID context");
    std::fill(l.begin(), l.end(), 0.5f);
    std::fill(r.begin(), r.end(), -0.5f);
    bridge.noteOn(SidGMDrumClass::Snare, 110u, 38u);
    bridge.processBlock(l.data(), r.data(), n);
    require(std::fabs(l[0] - 0.5f) > 1.0e-6f && std::fabs(r[0] + 0.5f) > 1.0e-6f,
            "DrSID bridge context also uses replacing render semantics");

    std::cout << "DrumBridgeRuntimeAuthorityV644Tests PASS\n";
    return 0;
}
