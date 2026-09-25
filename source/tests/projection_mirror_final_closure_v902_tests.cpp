#include "arpsid/core/sid_event_timing.h"
#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/core/sid_runtime_fractional_render.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

struct MirrorCall {
    uint8_t reg = 0;
    uint8_t value = 0;
    uint32_t sample = 0;
    uint16_t cycle = 0;
};

struct TestBank {
    ArpSID::SidWriteQueue sidWriteQueue{};
    ArpSID::SidRegisterEngine sidRegister{};
};

struct TestTarget {
    TestBank bank{};
    std::vector<MirrorCall> mirror{};
    TestTarget() { bank.sidRegister.prepare(48000.0); }
    TestBank& runtimeEngineBank() noexcept { return bank; }
    void runtimeMirrorAppliedProjectionWrite(uint8_t reg, uint8_t value,
                                             uint32_t sampleOffset, uint16_t cycleOffset) noexcept {
        mirror.push_back(MirrorCall{reg, value, sampleOffset, cycleOffset});
    }
};

static void test_late_fractional_mirror_uses_audio_normalized_timing() {
    TestTarget t;
    require(t.bank.sidWriteQueue.push(4u, 0x11u, 1u, 99u), "push late write");
    ArpSID::runtimeTransferDueSynthWritesToBackend(t, 3);
    require(t.mirror.size() == 1u, "one mirror call for late write");
    require(t.mirror[0].sample == 3u, "late mirror sample normalized to current sample");
    require(t.mirror[0].cycle == 0u, "late mirror cycle normalized to zero");
    require(t.bank.sidRegister.hasPendingSubphaseWrite(0u, 0u, 4u, 0x11u),
            "audio engine queued late write at current-sample cycle zero");
}

static void test_unresolved_fractional_mirror_uses_cycle_zero() {
    TestTarget t;
    require(t.bank.sidWriteQueue.push(4u, 0x22u, 2u, ArpSID::kSidUnresolvedCycleOffset),
            "push unresolved write");
    ArpSID::runtimeTransferDueSynthWritesToBackend(t, 2);
    require(t.mirror.size() == 1u, "one mirror call for unresolved write");
    require(t.mirror[0].sample == 2u, "unresolved mirror sample remains resolved sample");
    require(t.mirror[0].cycle == 0u, "unresolved mirror cycle normalized to zero");
    require(t.bank.sidRegister.hasPendingSubphaseWrite(0u, 0u, 4u, 0x22u),
            "audio engine queued unresolved write at cycle zero");
}

static void test_subphase_overflow_telemetry_survives_cursor_reset() {
    ArpSID::SidRegisterEngine e;
    e.prepare(48000.0);
    for (int i = 0; i < ArpSID::SidRegisterEngine::kMaxSubphaseWrites; ++i) {
        e.queueSubphaseWrite(static_cast<uint32_t>(i), 0u, 5u, static_cast<uint8_t>(i));
    }
    e.queueSubphaseWrite(999u, 0u, 6u, 0xAAu);
    require(e.subphaseWriteOverflowCount() == 1u, "overflow telemetry incremented");
    e.resetIntervalCursor();
    require(e.subphaseWriteOverflowCount() == 1u,
            "cursor reset must not clear overflow telemetry per fractional sample");
    e.resetSubphaseWriteTelemetryForBlock();
    require(e.subphaseWriteOverflowCount() == 0u, "explicit block telemetry reset clears counter");
}

static void test_equal_priority_same_register_newer_replaces_older_under_pressure() {
    ArpSID::SidRegisterEngine e;
    e.prepare(48000.0);
    for (int i = 0; i < ArpSID::SidRegisterEngine::kMaxSubphaseWrites; ++i) {
        e.queueSubphaseWrite(static_cast<uint32_t>(i), 0u, 5u, static_cast<uint8_t>(i & 0x7Fu));
    }
    e.queueSubphaseWrite(999u, 0u, 5u, 0xCCu);
    require(e.subphaseWriteOverflowCount() == 1u, "same-register pressure overflow counted");
    require(e.hasPendingSubphaseWrite(999u, 0u, 5u, 0xCCu),
            "newer same-register equal-priority write retained");
    require(!e.hasPendingSubphaseWrite(0u, 0u, 5u, 0x00u),
            "oldest same-register equal-priority write retired");
}

static void test_floor_timestamp_policy() {
    require(ArpSID::canonicalHostSampleOffsetFromSeconds(0.49 / 48000.0, 48000.0, 64) == 0,
            "0.49 sample maps to sample 0");
    require(ArpSID::canonicalHostSampleOffsetFromSeconds(0.51 / 48000.0, 48000.0, 64) == 0,
            "0.51 sample must not round into future sample");
    require(ArpSID::canonicalHostSampleOffsetFromSeconds(1.0 / 48000.0, 48000.0, 64) == 1,
            "exact one-sample boundary maps to sample 1");
}

static void test_source_shape_guards() {
#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif
    const std::string kernel = readFile(ARPSID_SOURCE_ROOT "/source/au3/ArpSIDDSPKernel.hpp");
    require(kernel.find("if (safeFrames == 1 && !sharedOutputBus") == std::string::npos,
            "mono/shared output must not bypass fractional finalizer");
    require(kernel.find("float fracL = 0.0f;") != std::string::npos &&
            kernel.find("0.5f * (fracL + fracR)") != std::string::npos,
            "mono fractional path finalizes into local stereo then folds to mono");

    const std::string frac = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/core/sid_runtime_fractional_render.h");
    require(frac.find("const uint32_t mirrorSample = late ? static_cast<uint32_t>(sampleOffset) : w->sampleOffset") != std::string::npos,
            "fractional mirror sample must use normalized late timing");
    require(frac.find("const uint16_t mirrorCycle = (late || w->cycleOffset == kSidUnresolvedCycleOffset)") != std::string::npos,
            "fractional mirror cycle must use normalized audio timing");

    const std::string preflight = readFile(ARPSID_SOURCE_ROOT "/scripts/run_full_ctest_preflight.sh");
    const std::string closure = readFile(ARPSID_SOURCE_ROOT "/scripts/run_full_closure_validation.sh");
    require(preflight.find("-DARPSID_BUILD_TESTS=ON") != std::string::npos &&
            closure.find("-DARPSID_BUILD_TESTS=ON") != std::string::npos,
            "full validation scripts must enable tests explicitly");
    require(preflight.find("--no-tests=error") != std::string::npos &&
            closure.find("--no-tests=error") != std::string::npos,
            "full validation scripts must fail when ctest discovers zero tests");
}

int main() {
    test_late_fractional_mirror_uses_audio_normalized_timing();
    test_unresolved_fractional_mirror_uses_cycle_zero();
    test_subphase_overflow_telemetry_survives_cursor_reset();
    test_equal_priority_same_register_newer_replaces_older_under_pressure();
    test_floor_timestamp_policy();
    test_source_shape_guards();
    std::cout << "projection_mirror_final_closure_v902_tests PASS\n";
    return 0;
}
