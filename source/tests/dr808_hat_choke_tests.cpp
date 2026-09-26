// Copyright (C) 2024-2026 Ulf Bertilsson
#include "dr808_test_utils.h"
#include <iostream>

int main() {
    using namespace ArpSID::Tests;

    auto engine = makeAnalogDr808Engine();
    const auto mono = renderMonoScript(engine, {{0, 46, 0.90f}, {4800, 42, 0.86f}}, 12000);
    (void)mono;

    auto preEngine = makeAnalogDr808Engine();
    renderMonoScript(preEngine, {{0, 46, 0.90f}}, 640);
    const float openBeforeChoke = preEngine.gmDrumNoteLevel(46);
    dr808Require(openBeforeChoke > 0.002f, "open hat telemetry must still be alive before choke");

    renderMonoScript(preEngine, {{0, 42, 0.86f}}, 256);
    const float openAfterChoke = preEngine.gmDrumNoteLevel(46);
    dr808Require(openAfterChoke < openBeforeChoke * 0.2f, "closed hat must choke the open-hat telemetry level");
    dr808Require(preEngine.gmDrumNoteLevel(42) > 0.02f, "closed hat must replace the audible family after choke");

    std::cout << "dr808_hat_choke_tests PASS\n";
    return 0;
}
