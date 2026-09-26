// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/sid_runtime_backend_projection.h"
#include "arpsid/core/sid_runtime_engine_bank.h"
#include "arpsid/core/sid_runtime_model.h"
#include "parameter_ids.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace ArpSID;

static std::array<float, static_cast<size_t>(kNumParams)> defaultParams() {
    std::array<float, static_cast<size_t>(kNumParams)> p{};
    for (int i = 0; i < kNumParams; ++i) p[(size_t)i] = kParamInfos[(size_t)i].defaultNorm;
    return p;
}

static uint8_t byteNorm(float v) {
    return static_cast<uint8_t>(std::clamp((int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f), 0, 255));
}

static void project(SidRuntimeModel& model,
                    SidRuntimeEngineBank& bank,
                    std::array<float, static_cast<size_t>(kNumParams)>& params,
                    bool force,
                    bool& firstApply,
                    SidRuntimeRenderMode& lastMode) {
    SidFamily lastFamily = SidFamily::MOS8580;
    float lastA = 0.0f, lastD = 0.267f, lastS = 0.533f, lastR = 0.2f;
    float lastFC = 0.72f, lastFR = 0.35f;
    projectRuntimeStateToBackends(model, bank, params, &bank.voicePolicy, 120.0, force,
                                  firstApply, lastMode, lastFamily,
                                  lastA, lastD, lastS, lastR, lastFC, lastFR);
}

static void test_bitperfect_projection_does_not_write_sid_register_engine_from_param_mirror() {
    SidRuntimeModel model{};
    SidRuntimeEngineBank bank{};
    bank.create(44100.0);

    auto params = defaultParams();
    params[(size_t)kParamSynthModeEnable] = 0.0f; // BitPerfect mode, not SID-register authority mode.
    params[(size_t)kParamDrSidEnable] = 0.0f;
    params[(size_t)kParamSidRegD400] = 0x11 / 255.0f;

    bank.sidRegister.write(0x00u, 0xAAu);
    bool firstApply = true;
    SidRuntimeRenderMode lastMode = SidRuntimeRenderMode::BitPerfect;
    project(model, bank, params, true, firstApply, lastMode);

    assert(bank.sidRegister.getRegs().r[0x00] == 0xAAu &&
           "BitPerfect/DrSid projection must not back-write SidRegisterEngine from register parameter mirrors");
}

static void test_sidregister_projection_seeds_live_engine_once_on_mode_entry_or_restore() {
    SidRuntimeModel model{};
    SidRuntimeEngineBank bank{};
    bank.create(44100.0);

    auto params = defaultParams();
    params[(size_t)kParamSynthModeEnable] = 1.0f; // SidRegister mode.
    params[(size_t)kParamDrSidEnable] = 0.0f;
    params[(size_t)kParamSidRegD400] = 0x34 / 255.0f;
    params[(size_t)kParamSidRegD401] = 0x12 / 255.0f;
    params[(size_t)kParamSidRegD418] = 0x0F / 255.0f;

    bank.sidRegister.write(0x00u, 0xAAu);
    bank.sidWriteQueue.push(0x00u, 0x55u, 0u, 0u);

    bool firstApply = true;
    SidRuntimeRenderMode lastMode = SidRuntimeRenderMode::BitPerfect;
    project(model, bank, params, true, firstApply, lastMode);

    assert(bank.sidRegister.getRegs().r[0x00] == 0x34u);
    assert(bank.sidRegister.getRegs().r[0x01] == 0x12u);
    assert(bank.sidRegister.getRegs().r[0x18] == 0x0Fu);
    assert(bank.sidWriteQueue.size() == 0u &&
           "SID-register restore/mode-entry seed must amputate stale queued synth writes");

    bank.sidRegister.write(0x00u, 0x77u);
    project(model, bank, params, false, firstApply, lastMode);
    assert(bank.sidRegister.getRegs().r[0x00] == 0x77u &&
           "steady-state projection must not repeatedly stomp live SidRegisterEngine authority");
}

int main() {
    test_bitperfect_projection_does_not_write_sid_register_engine_from_param_mirror();
    test_sidregister_projection_seeds_live_engine_once_on_mode_entry_or_restore();
    std::puts("register_authority_phase1_tests: PASS");
    return 0;
}
