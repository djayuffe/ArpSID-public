// Copyright (C) 2024-2026 Ulf Bertilsson
// sid808_snare_body_routing_v869_tests.cpp
//
// v869 pins the deeper SID808 snare/body and shared-voice routing fix:
// - the delayed snare body stage must be audible, not just register-visible;
// - Snare/Clap/Rim share SID voice 1 but filter routing follows the active
//   drum owning that voice, not the factory table row for some other drum.

#include "arpsid/engines/sid808_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "sid808_snare_body_routing_v869_tests FAIL: "
                  << message << "\n";
        std::exit(1);
    }
}

std::string readSourceFile(const char* relativePath) {
#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif
    std::ifstream in(std::string(ARPSID_SOURCE_DIR) + "/" + relativePath);
    require(in.good(), "source file must be readable");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

float rmsRegion(const std::vector<float>& l,
                const std::vector<float>& r,
                int begin,
                int end) {
    begin = std::clamp(begin, 0, static_cast<int>(l.size()));
    end = std::clamp(end, begin, static_cast<int>(l.size()));
    double sum = 0.0;
    int count = 0;
    for (int i = begin; i < end; ++i) {
        const float mono = 0.5f * ((std::isfinite(l[(size_t)i]) ? l[(size_t)i] : 0.0f) +
                                   (std::isfinite(r[(size_t)i]) ? r[(size_t)i] : 0.0f));
        sum += static_cast<double>(mono) * static_cast<double>(mono);
        ++count;
    }
    return count > 0 ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
}

float peakRegion(const std::vector<float>& l,
                 const std::vector<float>& r,
                 int begin,
                 int end) {
    begin = std::clamp(begin, 0, static_cast<int>(l.size()));
    end = std::clamp(end, begin, static_cast<int>(l.size()));
    float peak = 0.0f;
    for (int i = begin; i < end; ++i) {
        const float lv = std::isfinite(l[(size_t)i]) ? l[(size_t)i] : 0.0f;
        const float rv = std::isfinite(r[(size_t)i]) ? r[(size_t)i] : 0.0f;
        peak = std::max(peak, std::max(std::fabs(lv), std::fabs(rv)));
    }
    return peak;
}

void testSnareBodyAudibleRms() {
    using namespace ArpSID;
    constexpr int sr = 48000;
    Sid808Engine engine;
    engine.prepare(static_cast<double>(sr));
    engine.noteOn(Sid808Drum::Snare, 120u, 38u);

    std::vector<float> l((size_t)sr, 0.0f);
    std::vector<float> r((size_t)sr, 0.0f);
    engine.processBlock(l.data(), r.data(), sr);

    const int snapEnd = static_cast<int>(std::lround(sr * 0.0075));
    const int bodyEnd = static_cast<int>(std::lround(sr * 0.025));
    const float snapRms = rmsRegion(l, r, 0, snapEnd);
    const float bodyRms = rmsRegion(l, r, snapEnd, bodyEnd);
    const float bodyPeak = peakRegion(l, r, snapEnd, bodyEnd);

    require(snapRms > 0.015f, "snare snap must produce audible RMS");
    require(bodyRms > 0.006f, "snare body window must produce audible RMS");
    require(bodyRms > snapRms * 0.08f,
            "snare body RMS must be a meaningful fraction of snap RMS");
    require(bodyPeak > 0.02f, "snare body must have non-trivial peak energy");
    require(engine.lastSnareSnapRms() > 0.015f,
            "engine telemetry must publish audible snare snap RMS");
    require(engine.lastSnareBodyRms() > 0.006f,
            "engine telemetry must publish audible snare body RMS");
    require(engine.lastSnareBodyPeak() > 0.02f,
            "engine telemetry must publish snare body peak");
    require(engine.snareMicroStageAppliedCount() > 0u,
            "engine telemetry must count applied snare body stage");
    require(engine.snareMicroStageLateCount() == 0u,
            "snare body stage should land on scheduled render chunk boundary");
}

void testSharedVoiceFilterRoutingUsesActiveDrum() {
    using namespace ArpSID;
    Sid808Engine engine;
    engine.prepare(48000.0);

    engine.noteOn(Sid808Drum::Snare, 120u, 38u);
    require(engine.activeDrumForVoice(1) == Sid808Drum::Snare,
            "snare must own shared voice 1 after snare hit");
    require(engine.activeFilterRoutedForVoice(1),
            "snare must actively route shared voice 1 through filter");
    require(engine.activeFilterModeForVoice(1) == FilterMode::BpHp,
            "snare must own BP+HP filter mode while active");

    engine.noteOn(Sid808Drum::Clap, 120u, 39u);
    require(engine.activeDrumForVoice(1) == Sid808Drum::Clap,
            "clap must replace snare as active owner of shared voice 1");
    require(!engine.activeFilterRoutedForVoice(1),
            "clap must not inherit snare filter route on shared voice 1");
    require(engine.activeFilterModeForVoice(1) == FilterMode::None,
            "clap default must clear shared voice 1 filter mode");

    engine.noteOn(Sid808Drum::Rim, 120u, 37u);
    require(engine.activeDrumForVoice(1) == Sid808Drum::Rim,
            "rim must replace clap as active owner of shared voice 1");
    require(!engine.activeFilterRoutedForVoice(1),
            "rim must not inherit snare filter route on shared voice 1");
    require(engine.activeFilterModeForVoice(1) == FilterMode::None,
            "rim default must clear shared voice 1 filter mode");
}

void testSourceGuards() {
    const std::string sid808 = readSourceFile("include/arpsid/engines/sid808_engine.h");
    require(contains(sid808, "activeDrumByVoice_"),
            "SID808 must track active drum ownership per SID voice");
    require(contains(sid808, "activeFilterModeByVoice_"),
            "SID808 must track active filter mode per SID voice");
    require(!contains(sid808, "drumConfigs_[di].flags"),
            "SID808 filter routing must not scan factory table flags for inactive drums");
    require(contains(sid808, "applySid808VoiceConfigToFixedVoice_(static_cast<std::uint8_t>(voice), drum, cfg, level, true)"),
            "snare body micro-stage must retrigger with a real gate edge");
    require(contains(sid808, "lastSnareBodyRms"),
            "SID808 must expose snare body RMS telemetry");
    require(contains(sid808, "snareMicroStageAppliedCount"),
            "SID808 must expose snare micro-stage counters");
}

} // namespace

int main() {
    testSnareBodyAudibleRms();
    testSharedVoiceFilterRoutingUsesActiveDrum();
    testSourceGuards();
    std::cout << "sid808_snare_body_routing_v869_tests PASS\n";
    return 0;
}
