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

static void requireNotContains(const std::string& s, const char* needle, const char* msg) {
    require(s.find(needle) == std::string::npos, msg);
}

static void test_v908_identity() {
}

static void test_nooutput_runs_full_postfx_contract() {
    const std::string cpp = readFile(ARPSID_SOURCE_ROOT "/source/arpsid_processor_phase2.cpp");
    requireContains(cpp,
                    "processCanonicalBlockPhase2_(numSamples, tmpOutL.data(), tmpOutR.data(), 2, canonicalQueue);\n"
                    "        applyRenderModeOutputNormalization_(tmpOutL.data(), tmpOutR.data(), numSamples, resolveTopLevelRenderMode_());\n"
                    "        applyOutputFX_(tmpOutL.data(), tmpOutR.data(), numSamples);",
                    "Phase2 no-output branch must run scratch canonical render, normalization, and post-FX in that order");
    requireContains(cpp, "v908: also run normalization + post-FX on scratch",
                    "Phase2 no-output branch must explain v908 FX state-aging contract");
}

static void test_stale_render_audio_entrypoint_removed() {
    const std::string cpp = readFile(ARPSID_SOURCE_ROOT "/source/arpsid_processor_phase2.cpp");
    const std::string h = readFile(ARPSID_SOURCE_ROOT "/source/arpsid_processor_phase2.h");
    requireNotContains(cpp, "void ArpSIDProcessorPhase2::renderAudio(ProcessData& data)",
                       "stale renderAudio(ProcessData&) definition must not remain as alternate timing path");
    requireNotContains(h, "void renderAudio(Steinberg::Vst::ProcessData& data);",
                       "stale renderAudio(ProcessData&) declaration must not remain callable");
    requireContains(cpp, "the old renderAudio(ProcessData&) entrypoint was removed",
                    "source must document why the stale alternate render path is removed");
}

static void test_sweep_includes_v908_contract() {
    const std::string script = readFile(ARPSID_SOURCE_ROOT "/scripts/run_timing_music_contract_sweep.sh");
    requireContains(script, "Phase2NoOutputFxClosureV908Tests",
                    "timing/music sweep must build and run v908 Phase2 no-output FX closure tests");
    requireContains(script, "ProjectionMirrorFinalClosureV90[2345678]",
                    "timing/music sweep regex must include v908 closure lineage");
}

int main() {
    test_v908_identity();
    test_nooutput_runs_full_postfx_contract();
    test_stale_render_audio_entrypoint_removed();
    test_sweep_includes_v908_contract();
    std::cout << "phase2_nooutput_fx_closure_v908_tests PASS\n";
    return 0;
}
