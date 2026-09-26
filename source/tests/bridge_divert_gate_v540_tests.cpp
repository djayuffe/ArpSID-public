// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// bridge_divert_gate_v540_tests.cpp
//
// Pins the standalone engine-split bridge gate semantics. Production AUv2
// rendering stays in the canonical kernel because this independent bridge
// does not own the wrapper's ordered MIDI/automation/sequencer stream.
//
// divert ⇔ bridge.activeIdentity().context == SID-808
//
// Component flavor is checked by the kernel caller. Within the bridge path,
// active identity is the sole authority; no mutable router-enable fork exists.
//
// Test surface:
// A. Default context None → no divert
// B. No identity loaded → no divert
// C. SID-808 context is sufficient — divert engages
// D. Audio comes from bridge
// E. Mode switch back to DrSID context — divert disengages
// F. Bridge audio is finite and bounded in divert-on state

#include "arpsid/engines/drum_engine_host_bridge.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

// Stand-in for `componentRender`'s divert-gate logic.
// Returns:
// true → bridge would render the block
// false → legacy renderBlock would be called
bool divertGateActive(const ArpSID::DrumKitIdentity& activeIdentity) noexcept {
    return activeIdentity.context == ArpSID::DrumContext::SID808_AnalogProjection;
}

// Mirror the actual componentRender's behavior: if gate is on, render via
// bridge; if not, return "legacy render would happen here".
struct DivertSimulation {
    std::uint64_t bridgeRenderCount = 0;
    std::uint64_t legacyRenderCount = 0;
};

void simulateOneBlock(DivertSimulation& sim,
                      ArpSID::DrumEngineHostBridge& bridge,
                      float* outL, float* outR, int numSamples) {
    if (divertGateActive(bridge.activeIdentity())) {
        bridge.processBlock(outL, outR, numSamples);
        ++sim.bridgeRenderCount;
    } else {
        // Legacy path — we model with silence (in the real wrapper this
        // would be `renderBlock(...)` producing the synth+drum output).
        for (int i = 0; i < numSamples; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }
        ++sim.legacyRenderCount;
    }
}

} // namespace

int main() {
    using namespace ArpSID;

    // ── A. Default state — context None → no divert ──────────────────────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        require(!divertGateActive(bridge.activeIdentity()),
                "default state: divert gate OFF until a SID-808 identity is loaded");
    }

    // ── B. No identity loaded — divert blocked ───────────────────────────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        require(bridge.activeIdentity().context == DrumContext::None,
                "no slot loaded → context is None");
        require(!divertGateActive(bridge.activeIdentity()),
                "bridge needs a SID-808 identity");
    }

    // ── C. SID-808 context is the complete bridge divert authority ───────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        bridge.loadFactorySlot(123); // Hard kit (SID-808)
        require(bridge.activeIdentity().context == DrumContext::SID808_AnalogProjection,
                "slot 123 → SID-808 context");
        require(divertGateActive(bridge.activeIdentity()),
                "SID-808 context engages without a second mutable enable flag");
    }

    // ── D. SID-808 identity diverts audio ────────────────────────────────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        bridge.loadFactorySlot(120); // Classic kit (SID-808)
        require(divertGateActive(bridge.activeIdentity()),
                "audit-#39 divert gate engages from SID-808 context");

        // Simulate 10 blocks of render. All 10 must go through bridge.
        bridge.noteOn(SidGMDrumClass::Kick, 110);
        DivertSimulation sim;
        constexpr int N = 256;
        std::vector<float> outL(N), outR(N);
        for (int i = 0; i < 10; ++i) {
            simulateOneBlock(sim, bridge, outL.data(), outR.data(), N);
        }
        require(sim.bridgeRenderCount == 10,
                "all 10 blocks diverted through bridge");
        require(sim.legacyRenderCount == 0,
                "no blocks fell through to legacy renderBlock path");
    }

    // ── E. Mode switch — load DrSID slot → divert disengages ─────────────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        bridge.loadFactorySlot(125); // SID-808 (canonical)
        require(divertGateActive(bridge.activeIdentity()),
                "SID-808 slot — divert engaged");
        // Switch to a DrSID slot.
        bridge.loadFactorySlot(47); // DrSID legacy
        require(bridge.activeIdentity().context == DrumContext::DrSID_C64Wavetable,
                "switch to slot 47 → DrSID context");
        require(!divertGateActive(bridge.activeIdentity()),
                "divert disengages when context switches to DrSID");
    }

    // ── F. Bridge audio in divert-on state is finite + bounded ───────────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        bridge.setClockFrequency(PAL_CLOCK_FREQ);
        bridge.loadFactorySlot(121); // Punch kit
        bridge.noteOn(SidGMDrumClass::Kick, 120);
        bridge.noteOn(SidGMDrumClass::Snare, 100);

        DivertSimulation sim;
        constexpr int N = 1024;
        std::vector<float> outL(N), outR(N);
        simulateOneBlock(sim, bridge, outL.data(), outR.data(), N);
        require(sim.bridgeRenderCount == 1, "block diverted to bridge");

        bool anyNonZero = false;
        for (int i = 0; i < N; ++i) {
            require(std::isfinite(outL[i]) && std::isfinite(outR[i]),
                    "divert audio is finite");
            require(outL[i] >= -1.5f && outL[i] <= 1.5f &&
                    outR[i] >= -1.5f && outR[i] <= 1.5f,
                    "divert audio bounded ±1.5 (post-master-clamp envelope)");
            if (std::abs(outL[i]) > 1e-5f || std::abs(outR[i]) > 1e-5f) {
                anyNonZero = true;
            }
        }
        require(anyNonZero, "bridge produced non-trivial audio under divert");
    }

    // ── G. Switching identity mid-session toggles divert correctly ───────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        bridge.loadFactorySlot(47); // DrSID context
        bridge.noteOn(SidGMDrumClass::Kick, 100);
        DivertSimulation sim;
        constexpr int N = 128;
        std::vector<float> outL(N), outR(N);

        for (int i = 0; i < 3; ++i) {
            simulateOneBlock(sim, bridge, outL.data(), outR.data(), N);
        }
        require(sim.legacyRenderCount == 3 && sim.bridgeRenderCount == 0,
                "3 canonical blocks while DrSID identity is active");

        bridge.loadFactorySlot(122); // Lo-Fi (SID-808)
        bridge.noteOn(SidGMDrumClass::Kick, 100);
        for (int i = 0; i < 3; ++i) {
            simulateOneBlock(sim, bridge, outL.data(), outR.data(), N);
        }
        require(sim.legacyRenderCount == 3,
                "canonical counter unchanged after SID-808 identity");
        require(sim.bridgeRenderCount == 3,
                "3 bridge blocks after SID-808 identity switch");
    }

    std::cout << "bridge_divert_gate_v540_tests: standalone engine-split bridge semantics pinned\n";
    return 0;
}
