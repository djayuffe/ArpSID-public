// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

static void requireContains(const std::string& haystack, const char* needle, const char* msg) {
    require(haystack.find(needle) != std::string::npos, msg);
}

static void test_v905_release_identity() {
#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

}

static void test_timing_music_sweep_builds_then_ctests() {
#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif
    const std::string script = readFile(ARPSID_SOURCE_ROOT "/scripts/run_timing_music_contract_sweep.sh");
    requireContains(script, "-DARPSID_BUILD_TESTS=ON",
                    "timing/music sweep must configure tests on");
    requireContains(script, "cmake --build \"${BUILD}\" --target",
                    "timing/music sweep must build selected targets before CTest");
    requireContains(script, "arpsid_release_gate_regression_tests",
                    "timing/music sweep must build ReleaseGateRegression executable target");
    requireContains(script, "C64ProjectionMirrorBehaviorV899Tests",
                    "timing/music sweep must build v899 mirror behavior test");
    requireContains(script, "ProjectionMirrorFinalClosureV905Tests",
                    "timing/music sweep must build v905 closure test");
    requireContains(script, "--no-tests=error",
                    "timing/music sweep must fail on zero tests");
    requireContains(script, "ProjectionMirrorFinalClosureV90[2345678]",
                    "timing/music sweep regex must include v902-v908 closure tests");
}

int main() {
    test_v905_release_identity();
    test_timing_music_sweep_builds_then_ctests();
    std::cout << "projection_mirror_final_closure_v905_tests PASS\n";
    return 0;
}
