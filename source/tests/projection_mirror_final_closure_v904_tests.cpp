// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/sid_runtime_fractional_render.h"
#include "arpsid/core/sid_runtime_synth_register_scheduler.h"
#include "arpsid/engines/sid_register_engine.h"

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

struct V904Bank {
    ArpSID::SidWriteQueue sidWriteQueue{};
    ArpSID::SidRegisterEngine sidRegister{};
};

struct V904Target {
    V904Bank bank{};
    int mirrorCalls = 0;
    V904Target() { bank.sidRegister.prepare(48000.0); }
    V904Bank& runtimeEngineBank() noexcept { return bank; }
    void runtimeMirrorAppliedProjectionWrite(uint8_t, uint8_t, uint32_t, uint16_t) noexcept { ++mirrorCalls; }
};

static bool queueHasSample(const ArpSID::SidWriteQueue& q, uint32_t sample) {
    const auto view = q.data();
    for (const ArpSID::SidWrite* w = view.begin(); w != view.end(); ++w)
        if (w->sampleOffset == sample) return true;
    return false;
}

static void test_rebase_can_span_multiple_blocks_without_stuck_write() {
    V904Target t;
    require(t.bank.sidWriteQueue.push(4u, 0x09u, 1025u, 2u), "push two-block future write");
    ArpSID::runtimeEndFractionalBlock(t, 512u);
    require(queueHasSample(t.bank.sidWriteQueue, 513u), "first rebase preserves remaining one-block-plus delay");
    ArpSID::runtimeTransferDueSynthWritesToBackend(t, 511);
    require(t.mirrorCalls == 0, "write is still not due before second block end");
    ArpSID::runtimeEndFractionalBlock(t, 512u);
    require(queueHasSample(t.bank.sidWriteQueue, 1u), "second rebase moves write into next local block");
    ArpSID::runtimeTransferDueSynthWritesToBackend(t, 1);
    require(t.mirrorCalls == 1, "multi-block future write eventually consumes exactly once");
    require(t.bank.sidWriteQueue.size() == 0u, "consumed multi-block write is erased");
}

static void test_rebase_preserves_same_sample_order_after_clamp() {
    V904Target t;
    require(t.bank.sidWriteQueue.push(0u, 0x11u, 512u, 1u), "push first survivor");
    require(t.bank.sidWriteQueue.push(1u, 0x22u, 512u, 2u), "push second survivor");
    ArpSID::runtimeEndFractionalBlock(t, 512u);
    ArpSID::runtimeTransferDueSynthWritesToBackend(t, 0);
    require(t.mirrorCalls == 2, "same-sample rebased survivors both consume");
    require(t.bank.sidRegister.hasPendingSubphaseWrite(1u, 0u, 0u, 0x11u), "first survivor cycle retained");
    require(t.bank.sidRegister.hasPendingSubphaseWrite(2u, 0u, 1u, 0x22u), "second survivor cycle retained");
}

static void test_release_contract_source_shape_v904() {
#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

    const std::string pre = readFile(ARPSID_SOURCE_ROOT "/scripts/run_full_ctest_preflight.sh");
    require(pre.find("-DARPSID_BUILD_TESTS=ON") != std::string::npos,
            "preflight must enable tests");
    require(pre.find("--no-tests=error") != std::string::npos,
            "preflight must fail on zero tests");

    const std::string closure = readFile(ARPSID_SOURCE_ROOT "/scripts/run_full_closure_validation.sh");
    require(closure.find("-DARPSID_BUILD_TESTS=ON") != std::string::npos,
            "closure validation must enable tests");
    require(closure.find("--no-tests=error") != std::string::npos,
            "closure validation must fail on zero tests");
}

int main() {
    test_rebase_can_span_multiple_blocks_without_stuck_write();
    test_rebase_preserves_same_sample_order_after_clamp();
    test_release_contract_source_shape_v904();
    std::cout << "projection_mirror_final_closure_v904_tests PASS\n";
    return 0;
}
