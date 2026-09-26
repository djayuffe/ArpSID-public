// Copyright (C) 2024-2026 Ulf Bertilsson
#include "resid-fp/SID.h"
#include <cstdlib>
#include <iostream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace reSIDfp;
    SID sid;
    require(SID::is_deterministic_fallback(), "external reSID-fp compatibility layer exposes deterministic fallback diagnostic");
    require(std::string(SID::implementation_name()).find("deterministic") != std::string::npos,
            "fallback implementation name is explicit");
    sid.set_chip_model(MOS8580);
    sid.set_sampling_parameters(985248.0, RESAMPLE_INTERPOLATE, 48000.0);
    sid.write(0, 0x80);
    sid.write(1, 0x20);
    sid.write(4, 0x21);
    sid.write(24, 0x0f);
    sid.clock(16);
    (void)sid.output();
    std::cout << "ResidFallbackContractV630Tests PASS\n";
    return 0;
}
