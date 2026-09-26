// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/sid_runtime_fractional_render.h"
#include "arpsid/core/sid_runtime_synth_register_scheduler.h"
#include "arpsid/engines/sid_register_engine.h"

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

static bool queueHasWrite(const ArpSID::SidWriteQueue& q, uint8_t reg, uint8_t value,
                          uint32_t sample, uint16_t cycle) {
    const auto view = q.data();
    for (const ArpSID::SidWrite* w = view.begin(); w != view.end(); ++w) {
        if (w->regIndex == reg && w->value == value && w->sampleOffset == sample && w->cycleOffset == cycle)
            return true;
    }
    return false;
}

static void test_future_block_sid_writes_rebase_and_consume_next_block() {
    TestTarget t;
    require(t.bank.sidWriteQueue.push(4u, 0x08u, 512u, 0u), "push block-tail delayed TEST-off");
    require(t.bank.sidWriteQueue.push(4u, 0x09u, 513u, 1u), "push block-tail delayed gate-on");

    // Nothing is due in the old block at sample 511.
    ArpSID::runtimeTransferDueSynthWritesToBackend(t, 511);
    require(t.mirror.empty(), "future-block writes must not consume before block end");

    // Fractional consumer owns the queue lifetime: rebase survivors into next block.
    ArpSID::runtimeEndFractionalBlock(t, 512u);
    require(queueHasWrite(t.bank.sidWriteQueue, 4u, 0x08u, 0u, 0u),
            "sample 512 survivor rebased to next-block sample 0");
    require(queueHasWrite(t.bank.sidWriteQueue, 4u, 0x09u, 1u, 1u),
            "sample 513 survivor rebased to next-block sample 1");

    ArpSID::runtimeTransferDueSynthWritesToBackend(t, 0);
    require(t.mirror.size() == 1u, "rebased sample-0 write consumed in next block");
    require(t.mirror[0].sample == 0u && t.mirror[0].cycle == 0u,
            "next-block sample-0 mirror timing is exact");
    require(t.bank.sidRegister.hasPendingSubphaseWrite(0u, 0u, 4u, 0x08u),
            "audio queued rebased TEST-off at cycle zero");

    ArpSID::runtimeTransferDueSynthWritesToBackend(t, 1);
    require(t.mirror.size() == 2u, "rebased sample-1 write consumed in next block");
    require(t.mirror[1].sample == 1u && t.mirror[1].cycle == 1u,
            "next-block sample-1 mirror timing is exact");
}

static void test_runtime_end_fractional_block_does_not_touch_empty_or_zero() {
    TestTarget t;
    ArpSID::runtimeEndFractionalBlock(t, 0u);
    require(t.bank.sidWriteQueue.size() == 0u, "zero-frame end block is no-op");
    require(t.bank.sidWriteQueue.push(1u, 0xAAu, 7u, 3u), "push write");
    ArpSID::runtimeEndFractionalBlock(t, 0u);
    require(queueHasWrite(t.bank.sidWriteQueue, 1u, 0xAAu, 7u, 3u),
            "zero-frame end block does not corrupt write offsets");
}

static void test_glide_scheduler_emits_frequency_writes() {
    ArpSID::SynthModeVoices3 voices{};
    ArpSID::SidWriteQueue q{};
    auto& v = voices[0];
    v.active = true;
    v.currentSidFreqReg = 1000u;
    v.targetSidFreqReg = 1010u;
    v.glide = ArpSID::makeDiscreteRegisterGlide(1000u, 1010u, 0.01f,
                                                48000.0, 985248.0,
                                                ArpSID::PortamentoStyle::C64FixedDelta,
                                                1u, 50.0);
    require(v.glide.active, "test glide is active");
    ArpSID::scheduleSynthModeGlideWrites(voices, q, 1024, 48000.0, 985248.0);
    require(q.size() > 0u, "glide scheduler emitted SID frequency writes");
    require(v.currentSidFreqReg != 1000u, "glide advanced current SID frequency");
    bool hasFreqLo = false;
    bool hasFreqHi = false;
    const auto view = q.data();
    for (const ArpSID::SidWrite* w = view.begin(); w != view.end(); ++w) {
        hasFreqLo = hasFreqLo || (w->regIndex == 0u);
        hasFreqHi = hasFreqHi || (w->regIndex == 1u);
    }
    require(hasFreqLo && hasFreqHi, "glide emitted voice frequency low/high register writes");
}

static void test_source_shape_guards_v903() {
#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif
    const std::string frac = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/core/sid_runtime_fractional_render.h");
    require(frac.find("inline void runtimeEndFractionalBlock(Target& target, uint32_t frameCount) noexcept") != std::string::npos,
            "fractional consumer must expose block-end rebase hook");
    require(frac.find("q.rebaseAfterBlock(frameCount);") != std::string::npos,
            "fractional block-end hook must rebase SidWriteQueue survivors");

    const std::string kernel = readFile(ARPSID_SOURCE_ROOT "/source/au3/ArpSIDDSPKernel.hpp");
    require(kernel.find("ArpSID::scheduleSynthModeGlideWrites(engineBank_.synthVoices") != std::string::npos,
            "AU3 live render path must wire synth-mode glide scheduler before canonical render");
    require(kernel.find("ArpSID::runtimeEndFractionalBlock(*this, static_cast<uint32_t>(numFrames));") != std::string::npos,
            "AU3 live render path must rebase fractional SID-write survivors after canonical render");
    require(kernel.find("staleOrInvalidHostTime && te.kind == EventKind::NoteOff") != std::string::npos,
            "stale note-off policy must apply old note-offs at sample zero");

    const std::string chip = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/core/sid_chip.h");
    require(chip.find("const double budget = cycleFrac + clockFrequency / std::max(1.0, sampleRate);") != std::string::npos,
            "SIDChip cycle budget must use dispatcher-compatible nominal clock law");
    require(chip.find("effectiveClock / std::max(1.0, sampleRate)") == std::string::npos,
            "SIDChip must not own a second forensic-modulated cycle budget");

    const std::string drsid = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/engines/drsid_engine.h");
    require(drsid.find("tickWavetableRunnersForCycles_(wholeCycleEnd - wholeCycleStart);") != std::string::npos,
            "DrSID whole-cycle interval WT runner must use shared tick helper");
    require(drsid.find("if (subEnd >= kSidSubcycleBoundary) tickWavetableRunnersForCycles_(1u);") != std::string::npos,
            "DrSID split leading subphase that completes a cycle must tick WT runner");
}

int main() {
    test_future_block_sid_writes_rebase_and_consume_next_block();
    test_runtime_end_fractional_block_does_not_touch_empty_or_zero();
    test_glide_scheduler_emits_frequency_writes();
    test_source_shape_guards_v903();
    std::cout << "projection_mirror_final_closure_v903_tests PASS\n";
    return 0;
}
