// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void requireContains(const std::string& s, const char* needle, const char* msg) {
    require(s.find(needle) != std::string::npos, msg);
}

static void test_v906_release_identity() {
}

static void test_phase2_has_one_canonical_block_contract() {
    const std::string cpp = readFile(ARPSID_SOURCE_ROOT "/source/arpsid_processor_phase2.cpp");
    requireContains(cpp, "processCanonicalBlockPhase2_(int numSamples",
                    "Phase2 must centralize canonical block processing in one helper");
    requireContains(cpp, "resetFractionalAccumulatorsForBlock_(numSamples);",
                    "Phase2 standard process must reset fractional accumulators per block");
    requireContains(cpp, "scheduleSynthModeGlideWrites_(numSamples);",
                    "Phase2 canonical block helper must schedule live glide writes");
    requireContains(cpp, "ArpSID::runtimeEndFractionalBlock(*this, static_cast<uint32_t>(numSamples));",
                    "Phase2 canonical block helper must rebase future-block SID writes");
    requireContains(cpp, "processCanonicalBlockPhase2_(numSamples, tmpOutL.data(), tmpOutR.data(), 2, canonicalQueue);",
                    "Phase2 no-output path must render into scratch and drain fractional finalizers");
    requireContains(cpp, "std::fill_n(runtimeFractionalActive_.data(), aCount, uint8_t{0});",
                    "Phase2 reset helper must clear fractional active flags");
}

static void test_phase2_mono_and_double_contracts() {
    const std::string cpp = readFile(ARPSID_SOURCE_ROOT "/source/arpsid_processor_phase2.cpp");
    requireContains(cpp, "outL[offset + i] = 0.5f * (l + r);",
                    "Phase2 mono renderAudioSlice_ must fold stereo/fractional L/R to mono");
    requireContains(cpp, "applyOutputFX_(tmpOutL.data(), tmpOutL.data(), numSamples);",
                    "Phase2 double mono path must run FX as shared mono, not fake silent stereo");
    requireContains(cpp, "captureTelemetryMeters(tmpOutL.data(), outBus.numChannels >= 2 ? tmpOutR.data() : tmpOutL.data());",
                    "Phase2 double mono telemetry must read folded mono, not silent R scratch");
    requireContains(cpp, "zeroProcessOutputs();\n        return kResultOk;",
                    "Phase2 early precondition return must clear host outputs");
}

static void test_v906_sweep_includes_phase2_contracts() {
    const std::string script = readFile(ARPSID_SOURCE_ROOT "/scripts/run_timing_music_contract_sweep.sh");
    requireContains(script, "Phase2TimingMusicClosureV906Tests",
                    "timing/music sweep must build and run v906 Phase2 closure tests");
    requireContains(script, "ProjectionMirrorFinalClosureV90[2345678]",
                    "timing/music sweep regex must include v906 closure lineage");
}

int main() {
    test_v906_release_identity();
    test_phase2_has_one_canonical_block_contract();
    test_phase2_mono_and_double_contracts();
    test_v906_sweep_includes_phase2_contracts();
    std::cout << "phase2_timing_music_closure_v906_tests PASS\n";
    return 0;
}
