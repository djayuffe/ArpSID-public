// Copyright (C) 2024-2026 Ulf Bertilsson
// Regression guard for the v840 C64P render-path stall instrumentation.
//
// Measurement (v839) proved the cycle-accurate emulation runs ~12x realtime, so
// the C64P "play-stop-play-stop" choppiness is a periodic render-path stall, not
// compute. This instrumentation publishes RT-safe maxes — render-block µs,
// overrun count, host-delivery gap, continuous catch-up burst size, and leftover
// passive-cycle debt — so the stall can be SEEN instead of guessed. This guard
// locks that the signal is wired end-to-end and stays render-thread-safe.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readText(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!stream) { std::cerr << "FAIL: missing " << rel << '\n'; std::exit(1); }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

int main() {
    // 1. Snapshot schema carries the split stall counters, layout pinned, schema bumped.
    const std::string snap = readText("include/arpsid/gui/diagnostic_snapshot.h");
    require(snap.find("kDiagSchemaVersion = 8u;") != std::string::npos,
            "diagnostic snapshot schema must be bumped to 8 for the split stall counters");
    for (const char* f : {"c64CallbackLastBlockMicros", "c64CallbackMaxBlockMicros",
                          "c64CallbackOverrunCount", "c64PreC64LastBlockMicros",
                          "c64RenderLastBlockMicros", "c64RenderMaxBlockMicros",
                          "c64PostC64LastBlockMicros", "c64TelemetryLastBlockMicros",
                          "c64MaxHostGapMicros", "c64MaxContinuousCatchupCycles",
                          "c64MaxCiaCatchupCycles", "c64MaxVbiCatchupCycles",
                          "c64LastPassiveDebtCycles"}) {
        require(snap.find(std::string("std::uint64_t ") + f) != std::string::npos,
                "diagnostic snapshot must declare the stall counter field");
    }
    require(snap.find("sizeof(std::uint64_t) * 56") != std::string::npos,
            "diagnostic snapshot size static_assert must be updated to 56 uint64 fields");

    // 2. Kernel: atomics, RT-safe max helpers, timing capture, catch-up capture,
    //    reset-on-load, and the GUI-thread collector fill.
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");
    require(kernel.find("std::atomic<uint32_t> c64RenderMaxBlockMicros_{0};") != std::string::npos,
            "kernel must hold the render-µs max atomic");
    require(kernel.find("std::atomic<uint32_t> c64CallbackMaxBlockMicros_{0};") != std::string::npos,
            "kernel must hold the full callback max atomic");
    require(kernel.find("std::atomic<uint64_t> c64MaxContinuousCatchupCycles_{0};") != std::string::npos,
            "kernel must hold the continuous catch-up burst max atomic");
    require(kernel.find("void noteC64RenderTiming_(") != std::string::npos,
            "kernel must have the render-timing capture helper");
    require(kernel.find("void noteC64CallbackTiming_(") != std::string::npos,
            "kernel must have the full callback timing helper");
    require(kernel.find("updateC64SidplayTelemetryLight_(outputs, numFrames);")
                != std::string::npos,
            "C64 path must use the light telemetry updater");
    require(kernel.find("publishHiFiBypassTelemetry_();") != std::string::npos,
            "C64 player path must bypass the HiFi post-chain while publishing explicit bypass telemetry");
    require(kernel.find("const uint64_t continuousAudioBlockCycles") != std::string::npos &&
            kernel.find("std::min<uint64_t>(debtBeforeContinuousRun, continuousAudioBlockCycles)") != std::string::npos,
            "continuous C64 catch-up must be capped to the current audio-block PHI2 span");
    require(kernel.find("const bool continuousBacklog = debtBeforeContinuousRun > continuousAudioBlockCycles;") != std::string::npos,
            "continuous C64 backlog drop must be explicitly gated to debt beyond the current buffer span");
    require(kernel.find("future\" writes clamp to the last sample") != std::string::npos,
            "continuous C64 catch-up comment must document the block-edge burst failure mode");
    require(kernel.find("const uint64_t catchup = c64PsidPassiveCycleDebt_;") == std::string::npos,
            "continuous C64 catch-up must not consume the full stale debt in one render block");
    require(kernel.find("if (rsidResult.completedCycleBudget && continuousBacklog)") != std::string::npos,
            "continuous backlog drop must run only on genuine buffer-span backlog");
    require(kernel.find("renderC64SidplayPathIfActive_(outputs, numFrames,") != std::string::npos,
            "processBlock must take the C64 player branch through the early helper");
    require(kernel.find("then take the player branch before host-block/event-intent/MIDI/seq work") != std::string::npos,
            "processBlock must branch into the C64 player path before normal host/event work");
    require(kernel.find("noteC64Catchup_(C64CatchupPath_::Continuous, debtBeforeContinuousRun);") != std::string::npos,
            "the continuous catch-up site must record true pre-cap debt so backlog remains visible");
    require(kernel.find("noteC64Catchup_(C64CatchupPath_::Continuous, catchup);") == std::string::npos,
            "the continuous catch-up site must not hide backlog by publishing only capped run budget");
    require(kernel.find("noteC64Catchup_(C64CatchupPath_::Cia, catchup);") != std::string::npos,
            "the CIA catch-up site must record its burst size");
    require(kernel.find("noteC64Catchup_(C64CatchupPath_::Vbi, catchup);") != std::string::npos,
            "the VBI catch-up site must record its burst size");
    require(kernel.find("void clearC64BypassedPerformanceState_()") != std::string::npos &&
            kernel.find("clearC64BypassedPerformanceState_();") != std::string::npos,
            "C64 player branch must clear bypassed synth/DIGI note state without entering the normal render path");
    require(kernel.find("bool c64BypassedPerformanceStateCleared_ = false;") != std::string::npos &&
            kernel.find("if (!c64BypassedPerformanceStateCleared_)") != std::string::npos &&
            kernel.find("c64BypassedPerformanceStateCleared_ = true;") != std::string::npos &&
            kernel.find("c64BypassedPerformanceStateCleared_ = false;") != std::string::npos,
            "C64 player bypass-state cleanup must be edge-triggered, not repeated every block");
    require(kernel.find("resetC64SampleCursorsForHandoff_();") != std::string::npos,
            "C64 handoff paths must reset sample cursors explicitly");
    require(kernel.find("resetC64RenderInstrumentation_();") != std::string::npos,
            "instrumentation must reset on tune load");
    require(kernel.find("out.c64RenderMaxBlockMicros") != std::string::npos &&
            kernel.find("c64RenderMaxBlockMicros_.load") != std::string::npos,
            "collectDiagnosticCounters must publish the render-µs max");
    require(kernel.find("out.c64MaxContinuousCatchupCycles") != std::string::npos &&
            kernel.find("c64MaxContinuousCatchupCycles_.load") != std::string::npos,
            "collectDiagnosticCounters must publish the continuous catch-up burst max");
    // The cycle counter publish must use relaxed atomics (RT-safe, no locks).
    require(kernel.find("atomicMaxU32_(std::atomic<uint32_t>& a, uint32_t v)") != std::string::npos,
            "kernel must use a lock-free max helper for RT safety");

    // 3. GUI surfaces it both in the 55-row grid and the always-visible readout.
    const std::string vc = readText("source/au3/ArpSIDViewController.mm");
    require(vc.find("kC64DiagValueCount_v302 = 55u;") != std::string::npos,
            "C64 STATE diagnostic grid must grow to 55 rows");
    require(vc.find("snap.c64CallbackMaxBlockMicros") != std::string::npos,
            "C64 STATE grid must display the callback-µs max");
    require(vc.find("CATCHUP cont/CIA/VBI") != std::string::npos,
            "C64 cockpit must show the split catch-up readout");

    std::cout << "C64RenderStallInstrumentationV840Tests PASS\n";
    return 0;
}
