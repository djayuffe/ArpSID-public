// Copyright (C) 2024-2026 Ulf Bertilsson
#include "ArpSIDDSPKernel.hpp"
#include <iostream>

int main() {
    // This test intentionally only includes the AU DSP kernel header.
    // It catches field-contract drift in header-only/templated/inline code that
    // normal small unit tests may miss but AUv2/AUv3 translation units compile.
    std::cout << "DspKernelIncludeSmokeV633Tests PASS\n";
    return 0;
}
