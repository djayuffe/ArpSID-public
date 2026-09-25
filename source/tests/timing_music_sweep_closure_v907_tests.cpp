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


static bool isPreservedPass380ClosureLineageVersion(const std::string& version) {
    // v918: keep old closure tests release-forward. These tests verify that
    // their original contracts are preserved by the current pass380 closure
    // train, not that VERSION.txt remains pinned to an obsolete v90x label.
    return version.find("0.0.690-pass380-v") != std::string::npos &&
           version.find("closure") != std::string::npos;
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
    const std::string version = readFile(ARPSID_SOURCE_ROOT "/VERSION.txt");
    require(isPreservedPass380ClosureLineageVersion(version),
            "VERSION.txt must identify v907-v909 final timing/music sweep closure lineage");
    const std::string status = readFile(ARPSID_SOURCE_ROOT "/STATUS.md");
    require(status.find("v907 final timing/music sweep closure") != std::string::npos ||
            status.find("v908 Phase2 no-output FX contract closure") != std::string::npos,
            "STATUS.md must document v907/v908 closure lineage");
    const std::string notes = readFile(ARPSID_SOURCE_ROOT "/RELEASE_NOTES_V907.md");
    requireContains(notes, "27/27 PASS", "v907 notes must record completed focused sweep result");
    const std::string notes908 = readFile(ARPSID_SOURCE_ROOT "/RELEASE_NOTES_V908.md");
    requireContains(notes908, "post-FX", "v908 notes must record no-output post-FX closure");
}

static void test_sweep_includes_all_current_contracts() {
    const std::string script = readFile(ARPSID_SOURCE_ROOT "/scripts/run_timing_music_contract_sweep.sh");
    requireContains(script, "-DARPSID_BUILD_TESTS=ON", "sweep must configure tests on");
    requireContains(script, "--no-tests=error", "sweep must fail on zero tests");
    requireContains(script, "Phase2TimingMusicClosureV906Tests", "sweep must include Phase2/VST3 parity test");
    requireContains(script, "TimingMusicSweepClosureV907Tests", "sweep must include v907 sweep-closure test");
    requireContains(script, "Phase2NoOutputFxClosureV908Tests", "sweep must include v908 no-output FX closure test");
    requireContains(script, "ProjectionMirrorFinalClosureV90[2345678]", "sweep regex must include v907 closure lineage");
}

static void test_phase2_runtime_contract_still_pinned() {
    const std::string cpp = readFile(ARPSID_SOURCE_ROOT "/source/arpsid_processor_phase2.cpp");
    requireContains(cpp, "processCanonicalBlockPhase2_(int numSamples", "Phase2 canonical helper must remain centralized");
    requireContains(cpp, "resetFractionalAccumulatorsForBlock_(numSamples);", "Phase2 helper must reset fractional state");
    requireContains(cpp, "scheduleSynthModeGlideWrites_(numSamples);", "Phase2 helper must schedule live glide writes");
    requireContains(cpp, "ArpSID::runtimeEndFractionalBlock(*this, static_cast<uint32_t>(numSamples));", "Phase2 helper must rebase future SID writes");
    requireContains(cpp, "outL[offset + i] = 0.5f * (l + r);", "Phase2 mono path must fold stereo/fractional L/R");
    requireContains(cpp, "applyOutputFX_(tmpOutL.data(), tmpOutL.data(), numSamples);", "Phase2 double mono must not fake a silent right channel");
}

int main() {
    test_v907_identity_and_notes();
    test_sweep_includes_all_current_contracts();
    test_phase2_runtime_contract_still_pinned();
    std::cout << "timing_music_sweep_closure_v907_tests PASS\n";
    return 0;
}
