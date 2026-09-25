#include "dr808_test_utils.h"
#include <array>
#include <iostream>

int main() {
    using namespace ArpSID::Tests;

    const std::array<int, 11> notes{{36, 38, 42, 46, 39, 56, 45, 37, 49, 70, 75}};
    for (int note : notes) {
        auto engine = makeAnalogDr808Engine();
        const auto mono = renderMonoScript(engine, {{0, note, 0.88f}}, 96000);
        const auto stats = analyzeMono(mono);
        dr808Require(stats.finite, "analog drum voice render must stay finite");
        dr808Require(stats.peak > 1.0e-4f, "analog drum voice must produce audible output");
        dr808Require(stats.peak < 0.98f, "analog drum voice must stay under headroom ceiling");
        dr808Require(stats.energy > 1.0e-2f, "analog drum voice must accumulate non-trivial energy");
        dr808Require(stats.tailRms < 0.08f, "analog drum voice must decay back toward silence");
        dr808Require(engine.lastGMDrumNote() == note, "exact GM note telemetry must survive analog-mode triggering");
    }

    std::cout << "dr808_voice_smoke_tests PASS\n";
    return 0;
}
