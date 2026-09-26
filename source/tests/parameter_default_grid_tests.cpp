// Copyright (C) 2024-2026 Ulf Bertilsson
// Every declared parameter default must be a fixed point of the canonical
// sanitize law. Hosts publish the declared default, set it, and read it back
// (auval: "Parameter did not retain default value when set"); a stepped
// default that sits between grid points would come back snapped.
//
// The 0.9.1 grid defaults must also decode to exactly what the previous
// off-grid defaults decoded to, so moving them is sound-neutral.
#include "parameter_ids.h"
#include "arpsid/core/math_utils.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace ArpSID;

static int failures = 0;
static void check(bool ok, const char* what, int id) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s (param %d)\n", what, id);
        ++failures;
    }
}

int main() {
    for (int id = 0; id < kNumParams; ++id) {
        const float def = defaultNormalizedParamValue(id);
        check(std::isfinite(def) && def >= 0.0f && def <= 1.0f, "default in [0,1]", id);
        check(sanitizeNormalizedParamValue(id, def, def) == def,
              "declared default is on the sanitize grid", id);
    }

    // Waveform/filter selectors decode floor(v*8) into 8 bins.
    const auto waveIdx = [](float v) { return (int)std::floor(v * 8.0f); };
    check(waveIdx(defaultNormalizedParamValue(kParamVCO1Waveform)) == waveIdx(0.25f), "VCO1 waveform index unchanged", kParamVCO1Waveform);
    check(waveIdx(defaultNormalizedParamValue(kParamVCO2Waveform)) == waveIdx(0.125f), "VCO2 waveform index unchanged", kParamVCO2Waveform);
    check(waveIdx(defaultNormalizedParamValue(kParamVCO3Waveform)) == waveIdx(0.125f), "VCO3 waveform index unchanged", kParamVCO3Waveform);

    // Arp pattern and sequencer lengths decode 1 + lround(v*31).
    check(ArpSID_normToSeqSteps(defaultNormalizedParamValue(kParamSeqLength)) == ArpSID_normToSeqSteps(0.5f),
          "seq length unchanged", kParamSeqLength);
    check(1 + std::lround(defaultNormalizedParamValue(kParamArpPatternLength) * 31.0f) == 1 + std::lround(0.5f * 31.0f),
          "arp pattern length unchanged", kParamArpPatternLength);

    // Step notes decode lround(v*127); pitch bend decodes lround(v*16383).
    for (int step = 0; step < 32; ++step) {
        const int id = (int)kParamSeqStep1Note + step * 3;
        check(std::lround(defaultNormalizedParamValue(id) * 127.0f) == std::lround(0.5f * 127.0f),
              "seq step note unchanged", id);
    }
    for (int ch = 0; ch < 16; ++ch) {
        const int id = (int)kParamHostCtrlPitchBendBase + ch;
        check(std::lround(defaultNormalizedParamValue(id) * 16383.0f) == 8192,
              "pitch bend default is centre (8192)", id);
    }

    if (failures) return 1;
    std::puts("ParameterDefaultGridTests PASS");
    return 0;
}
