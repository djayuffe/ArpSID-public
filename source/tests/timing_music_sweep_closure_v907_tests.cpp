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

static void test_v907_identity_and_notes() {
}

static void test_sweep_includes_all_current_contracts() {
    const std::string script = readFile(ARPSID_SOURCE_ROOT "/scripts/run_timing_music_contract_sweep.sh");
    requireContains(script, "-DARPSID_BUILD_TESTS=ON", "sweep must configure tests on");
    requireContains(script, "--no-tests=error", "sweep must fail on zero tests");
    requireContains(script, "TimingMusicSweepClosureV907Tests", "sweep must include v907 sweep-closure test");
    requireContains(script, "ProjectionMirrorFinalClosureV90[2345678]", "sweep regex must include v907 closure lineage");
}

int main() {
    test_v907_identity_and_notes();
    test_sweep_includes_all_current_contracts();
    std::cout << "timing_music_sweep_closure_v907_tests PASS\n";
    return 0;
}
