#include "dr808_test_utils.h"
#include <iostream>
#include <vector>

int main() {
    using namespace ArpSID::Tests;

    auto engine = makeAnalogDr808Engine();
    engine.setAccentAmount(1.0f);
    engine.setOutputDrive(0.54f);

    std::vector<DrumEvent> script;
    for (int step = 0; step < 32; ++step) {
        const int sample = step * 900;
        script.push_back({sample, 36, (step % 4 == 0) ? 1.0f : 0.88f});
        if ((step & 1) == 0) script.push_back({sample, 42, 0.74f});
        if ((step % 4) == 2) script.push_back({sample, 38, 0.92f});
        if ((step % 8) == 4) script.push_back({sample, 46, 0.86f});
        if ((step % 8) == 6) script.push_back({sample, 39, 0.84f});
        if ((step % 16) == 8) script.push_back({sample, 49, 0.82f});
    }

    const auto mono = renderMonoScript(engine, script, 32000);
    const auto stats = analyzeMono(mono);
    dr808Require(stats.finite, "dense analog kit render must stay finite");
    dr808Require(stats.peak < 0.98f, "dense analog kit render must stay under the safety ceiling");
    dr808Require(stats.energy > 0.5f, "dense analog kit render must remain materially audible");

    std::cout << "dr808_peak_headroom_tests PASS\n";
    return 0;
}
