// Copyright (C) 2024-2026 Ulf Bertilsson
// v898 end-to-end audibility closure.
//
// Pins the fix chain for the "projection/SynthMode instruments are silent and
// ignore MIDI" P0 (regressed between v875 and v892; v874 was the last build
// where SynthMode audibly played):
//  1. Host snapshot restores now sync the runtime model state root, so note
//     ROUTING (state-root mode authority) cannot split from the live render
//     params (engine-guard authority).
//  2. The fractional interval render path now consumes bank.sidWriteQueue
//     (runtimeTransferDueSynthWritesToBackend) — previously the only consumer
//     lived on the unreachable slice-fallback path, so note writes never
//     reached the engine.
//  3. SidRegisterEngine::renderIntervalAccurate applies queued writes whose
//     position falls in an interval-tiling gap (the transferred GATE-ON at
//     (cycle N, subphase 0) was skipped and wiped, leaving voices gate-off).
//  4. Host-provided timed note events now feed the raw-MIDI held-ingress
//     ledger, so the v869 orphan reconciler no longer hard-kills every voice
//     started from host (Logic region) events.
//
// The test drives the REAL ArpSIDDSPKernel with a note-on through
// processBlock in each render mode and requires audible output.

#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"
#include <array>
#include <cmath>
#include <cstdio>

using namespace ArpSID;

static float renderPeak(SidRuntimeRenderMode mode, int blocks, bool synthEnable) {
    ArpSIDDSPKernel k;
    k.setup(48000.0, 512);
    std::array<float, kNumParams> snap{};
    for (auto& v : snap) v = 0.0f;
    // Minimal audible baseline
    snap[(size_t)kParamMasterVolume] = 0.85f;
    snap[(size_t)kParamSustain] = 1.0f;
    snap[(size_t)kParamAttack] = 0.01f;
    snap[(size_t)kParamDecay] = 0.2f;
    snap[(size_t)kParamRelease] = 0.2f;
    snap[(size_t)kParamVCO1Waveform] = 0.45f; // pulse-ish
    snap[(size_t)kParamVCO1Level] = 1.0f;
    snap[(size_t)kParamVCO1PulseWidth] = 0.5f;
    snap[(size_t)kParamFilterCutoff] = 1.0f;
    snap[(size_t)kParamFilterMode] = 0.0f;
    snap[(size_t)kParamVirtualGate] = 1.0f;
    if (mode == SidRuntimeRenderMode::DrSid) {
        snap[(size_t)kParamDrSidEnable] = 1.0f;
        snap[(size_t)kParamSynthModeEnable] = 0.0f;
        snap[(size_t)kParamDrSidMachineModel] = 1.0f;
        snap[(size_t)kParamDrSidVolume] = 1.0f;
    } else {
        snap[(size_t)kParamDrSidEnable] = 0.0f;
        snap[(size_t)kParamSynthModeEnable] = synthEnable ? 1.0f : 0.0f;
    }
    k.restoreHostParameterSnapshotImmediate(snap.data(), kNumParams);

    constexpr int frames = 512;
    std::array<float, frames> l{}, r{};
    float* outs[2] = {l.data(), r.data()};
    TransportState t{};
    t.isPlaying = true;
    t.playStateKnown = true;
    t.sampleRate = 48000.0;
    t.frameCount = frames;

    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b) {
        l.fill(0.0f); r.fill(0.0f);
        if (b == 0) {
            TimedEvent ev{};
            ev.sampleOffset = 0;
            ev.kind = EventKind::NoteOn;
            ev.channel = (mode == SidRuntimeRenderMode::DrSid) ? 9 : 0;
            ev.pitch = (mode == SidRuntimeRenderMode::DrSid) ? 36 : 60;
            ev.value = 1.0f;
            k.processBlock(outs, 2, frames, &ev, 1, t);
        } else {
            k.processBlock(outs, 2, frames, nullptr, 0, t);
        }
        for (int i = 0; i < frames; ++i) {
            const float a = std::fabs(l[i]);
            if (a > peak) peak = a;
        }
    }
    return peak;
}

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "kernel_e2e_audibility_v898_tests FAIL: %s\n", msg);
        std::exit(1);
    }
}

int main() {
    ArpSID::prewarmAllSidTables();
    const float bp = renderPeak(SidRuntimeRenderMode::BitPerfect, 8, false);
    const float sm = renderPeak(SidRuntimeRenderMode::SidRegister, 8, true);
    const float dr = renderPeak(SidRuntimeRenderMode::DrSid, 8, false);
    std::printf("peaks: BitPerfect=%f SynthMode=%f DrSid=%f\n", bp, sm, dr);
    require(bp > 0.01f, "BitPerfect note-on must produce audible output");
    require(sm > 0.05f, "SynthMode/SidRegister note-on must produce audible output (v898 P0)");
    require(dr > 0.01f, "DrSid note-on must produce audible output");
    std::printf("kernel_e2e_audibility_v898_tests PASS\n");
    return 0;
}
