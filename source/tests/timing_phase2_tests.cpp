// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/sid_event_timing.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

using namespace ArpSID;

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "TimingPhase2Tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static uint64_t runClock(double sr, double clk, int samples) {
    SidCycleClockState s{};
    s.configure(sr, clk);
    uint64_t total = 0;
    for (int i = 0; i < samples; ++i) total += s.cyclesForNextHostSample();
    return total;
}

static void test_pal_44100_one_second_exact_bounded() {
    const uint64_t total = runClock(44100.0, PAL_CLOCK_FREQ, 44100);
    const uint64_t expected = static_cast<uint64_t>(std::llround(PAL_CLOCK_FREQ));
    require((total == expected || total + 1 == expected || total == expected + 1),
            "PAL 44.1 kHz one-second SID cycle total must carry fractional remainder within one cycle");
}

static void test_ntsc_48000_one_second_exact_bounded() {
    const uint64_t total = runClock(48000.0, NTSC_CLOCK_FREQ, 48000);
    const uint64_t expected = static_cast<uint64_t>(std::llround(NTSC_CLOCK_FREQ));
    require((total == expected || total + 1 == expected || total == expected + 1),
            "NTSC 48 kHz one-second SID cycle total must carry fractional remainder within one cycle");
}

static void test_rounded_estimate_is_not_render_authority() {
    const uint16_t legacy = estimateSidCyclesPerHostSample(44100.0, PAL_CLOCK_FREQ);
    require(legacy == 23u,
            "legacy presentation estimate is conservative ceil, not rounded physical authority");
    SidCycleClockState s{};
    s.configure(44100.0, PAL_CLOCK_FREQ);
    bool saw22 = false;
    bool saw23 = false;
    for (int i = 0; i < 4096; ++i) {
        const uint16_t c = s.cyclesForNextHostSample();
        saw22 = saw22 || (c == 22u);
        saw23 = saw23 || (c == 23u);
    }
    require(saw22 && saw23,
            "physical planner must distribute 22/23 cycle samples instead of constant rounded count");
}

static void test_subphase_maps_inside_actual_sample_width() {
    require(cycleOffsetFromSubphaseForCyclesInSample(0u, 22u) == 0u,
            "subphase 0 maps to cycle 0");
    require(cycleOffsetFromSubphaseForCyclesInSample(128u, 22u) == 11u,
            "subphase 128 maps inside 22-cycle sample");
    require(cycleOffsetFromSubphaseForCyclesInSample(255u, 22u) == 21u,
            "subphase 255 maps to final 22-cycle slot");
    require(cycleOffsetFromSubphaseForCyclesInSample(255u, 23u) == 22u,
            "subphase 255 maps to final 23-cycle slot");
}

int main() {
    test_pal_44100_one_second_exact_bounded();
    test_ntsc_48000_one_second_exact_bounded();
    test_rounded_estimate_is_not_render_authority();
    test_subphase_maps_inside_actual_sample_width();
    std::cout << "TimingPhase2Tests PASS\n";
    return 0;
}
