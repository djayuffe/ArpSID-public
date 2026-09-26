// Copyright (C) 2024-2026 Ulf Bertilsson
#include "dr808_test_utils.h"
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace ArpSID::Tests;

    const std::vector<DrumEvent> script{
        {0, 36, 1.00f}, {2400, 42, 0.74f}, {4800, 38, 0.92f}, {7200, 46, 0.88f},
        {9600, 39, 0.84f}, {12000, 56, 0.82f}, {14400, 49, 0.86f}, {16800, 37, 0.78f}
    };

    auto a = makeAnalogDr808Engine();
    auto b = makeAnalogDr808Engine();
    const auto left = renderMonoScript(a, script, 24000);
    const auto right = renderMonoScript(b, script, 24000);

    dr808Require(left.size() == right.size(), "determinism buffers must match length");
    for (size_t i = 0; i < left.size(); ++i) {
        dr808Require(std::fabs(left[i] - right[i]) <= 1.0e-7f, "analog drum render must be deterministic from identical state and seed");
    }

    std::cout << "dr808_determinism_tests PASS\n";
    return 0;
}
