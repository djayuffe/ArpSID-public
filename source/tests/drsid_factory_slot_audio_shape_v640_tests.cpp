// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/core/drum_context.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static float renderEnergy(ArpSID::DrSidEngine& dr, int n = 512) {
    float e = 0.0f;
    std::vector<float> l(static_cast<std::size_t>(n), 0.0f);
    std::vector<float> r(static_cast<std::size_t>(n), 0.0f);
    float* outs[2] = { l.data(), r.data() };
    dr.processReplacingBlock(outs, n);
    for (int i = 0; i < n; ++i) {
        e += std::fabs(l[static_cast<std::size_t>(i)]) + std::fabs(r[static_cast<std::size_t>(i)]);
    }
    return e;
}

int main() {
    using namespace ArpSID;
    const auto slotA = static_cast<std::uint16_t>(kDrSidNewFactoryRange.first + 2u);
    const auto slotB = static_cast<std::uint16_t>(kDrSidNewFactoryRange.first + 11u);

    DrSidEngine a;
    DrSidEngine b;
    a.setSampleRate(48000.0);
    b.setSampleRate(48000.0);

    a.triggerKitMidiNote(36, 0.82f, slotA, true, 0x00u, 0x00u, 0x00u, 0x0000u, 0x00u, 0x00u);
    b.triggerKitMidiNote(36, 0.82f, slotB, true, 0x00u, 0x00u, 0x00u, 0x0000u, 0x00u, 0x00u);

    require(a.lastKitSelectedFactorySlot() == slotA, "slot A recorded");
    require(b.lastKitSelectedFactorySlot() == slotB, "slot B recorded");

    const float ea = renderEnergy(a);
    const float eb = renderEnergy(b);
    require(std::fabs(ea - eb) > 1.0e-4f,
            "different DrSID factory slots alter actual rendered energy/signature");

    DrSidEngine base;
    base.setSampleRate(48000.0);
    base.triggerKitMidiNote(36, 0.82f, 0u, false, 0, 0, 0, 0, 0, 0);
    const float ebase = renderEnergy(base);
    require(std::fabs(ea - ebase) > 1.0e-4f || std::fabs(eb - ebase) > 1.0e-4f,
            "selected DrSID factory slot differs from no-slot canonical trigger");

    std::cout << "DrSidFactorySlotAudioShapeV640Tests PASS\n";
    return 0;
}
