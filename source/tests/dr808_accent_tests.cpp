#include "dr808_test_utils.h"
#include <iostream>

int main() {
    using namespace ArpSID::Tests;

    auto softEngine = makeAnalogDr808Engine();
    softEngine.setAccentAmount(1.0f);
    const auto softMono = renderMonoScript(softEngine, {{0, 36, 0.55f}}, 48000);
    const auto softStats = analyzeMono(softMono);

    auto hardEngine = makeAnalogDr808Engine();
    hardEngine.setAccentAmount(1.0f);
    const auto hardMono = renderMonoScript(hardEngine, {{0, 36, 1.00f}}, 48000);
    const auto hardStats = analyzeMono(hardMono);

    dr808Require(softStats.finite && hardStats.finite, "accent renders must stay finite");
    dr808Require(hardStats.peak > softStats.peak * 1.10f, "accented kick must peak above the lower-velocity hit");
    dr808Require(hardStats.peak < 0.98f, "accented kick must remain inside safe headroom");

    std::cout << "dr808_accent_tests PASS\n";
    return 0;
}
