// Copyright (C) 2024-2026 Ulf Bertilsson
// sid808_transport_integration_v506_tests.cpp
//
// Regression coverage for the SID-808 / transport / BPM / ticks / Logic
// integration. Pins:
// 1) A stale AUv2 transport snapshot must retain the already-advanced
// internal block-start beat. Time advances exactly once at block end.
// 2) When the host beat IS fresh (publisher updated since last block), the
// runtime must adopt the new published beat as ground truth (slow drift
// correction).
// 3) playStateKnown still gates whether host.transportPlaying gets
// overwritten — the extrapolation must not flip the play state.

#include "arpsid/core/sid_runtime_host_block.h"
#include "arpsid/core/sid_runtime_host_surface.h"
#include "arpsid/core/sid_runtime_model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

static int g_failures = 0;
static void check(bool cond, const char* msg) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}

struct HandlerTransport {
    double bpm = 120.0;
    double beatPosition = 0.0;
    bool isPlaying = false;
    bool playStateKnown = false;
    bool isLooping = false;
    double loopStart = 0.0;
    double loopEnd = 0.0;
    double sampleRate = 44100.0;
    int frameCount = 256;
    bool valid() const noexcept {
        return std::isfinite(bpm) && bpm > 0.0 &&
               std::isfinite(sampleRate) && sampleRate > 0.0;
    }
};

// Test 1 — stale snapshot retains start and advances once at commit.
static void test_stale_snapshot_extrapolates_beat() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    host.transportPlaying = true;
    host.lastTransportPlaying = true;
    host.hostTempo = 120.0;
    host.hostBeatPosition = 4.0;
    host.lastPublishedBeatPosition = 4.0;
    host.hasPublishedBeatPosition = true;

    HandlerTransport t{};
    t.bpm = 120.0;
    t.beatPosition = 4.0;        // SAME as host.hostBeatPosition — stale
    t.isPlaying = true;
    t.playStateKnown = true;
    t.sampleRate = 44100.0;
    t.frameCount = 512;

    const auto state = ArpSID::sidRuntimeBeginCanonicalHostBlock(
        model, host, t, 44100.0, 512);

    check(std::fabs(host.hostBeatPosition - 4.0) < 1.0e-9,
          "stale snapshot must retain the current internal block-start beat");
    check(std::fabs(state.currentBeatPosition - 4.0) < 1.0e-9,
          "state.currentBeatPosition must describe the start of the block");
    ArpSID::sidRuntimeAdvanceCanonicalHostBlock(host, state, 512);
    const double expected = 4.0 + 512.0 * 120.0 / (44100.0 * 60.0);
    check(std::fabs(host.hostBeatPosition - expected) < 1.0e-6,
          "block end must advance hostBeatPosition exactly once");
}

// Test 2 — fresh snapshot adopted verbatim.
static void test_fresh_snapshot_adopted() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    host.transportPlaying = true;
    host.lastTransportPlaying = true;
    host.hostTempo = 120.0;
    host.hostBeatPosition = 4.0;

    HandlerTransport t{};
    t.bpm = 120.0;
    t.beatPosition = 4.25;       // FRESH — poller updated since last block
    t.isPlaying = true;
    t.playStateKnown = true;
    t.sampleRate = 44100.0;
    t.frameCount = 512;

    const auto state = ArpSID::sidRuntimeBeginCanonicalHostBlock(
        model, host, t, 44100.0, 512);

    check(std::fabs(host.hostBeatPosition - 4.25) < 1.0e-9,
          "fresh published beat must be adopted as the new ground truth");
    (void)state;
}

// Test 3 — extrapolation only when transport is playing.
static void test_no_extrapolation_when_stopped() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    host.transportPlaying = false;
    host.lastTransportPlaying = false;
    host.hostTempo = 120.0;
    host.hostBeatPosition = 8.0;

    HandlerTransport t{};
    t.bpm = 120.0;
    t.beatPosition = 8.0;        // same beat, but transport is stopped
    t.isPlaying = false;
    t.playStateKnown = true;
    t.sampleRate = 44100.0;
    t.frameCount = 512;

    const auto state = ArpSID::sidRuntimeBeginCanonicalHostBlock(
        model, host, t, 44100.0, 512);

    check(std::fabs(host.hostBeatPosition - 8.0) < 1.0e-9,
          "no extrapolation when transport is stopped — beat must stay parked");
    check(state.isPlaying == false,
          "transport must remain stopped");
}

// Test 4 — extrapolation must not flip play state.
static void test_extrapolation_preserves_play_state() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    host.transportPlaying = true;
    host.lastTransportPlaying = true;
    host.hostTempo = 130.0;
    host.hostBeatPosition = 16.0;
    host.lastPublishedBeatPosition = 16.0;
    host.hasPublishedBeatPosition = true;

    HandlerTransport t{};
    t.bpm = 130.0;
    t.beatPosition = 16.0;       // stale
    t.isPlaying = true;
    t.playStateKnown = true;
    t.sampleRate = 48000.0;
    t.frameCount = 480;          // 10 ms at 48k

    const auto state = ArpSID::sidRuntimeBeginCanonicalHostBlock(
        model, host, t, 48000.0, 480);

    check(state.isPlaying == true,
          "extrapolation must not change isPlaying");
    check(host.transportPlaying == true,
          "extrapolation must not change host.transportPlaying");
    check(std::fabs(host.hostBeatPosition - 16.0) < 1.0e-9,
          "stale begin must not pre-advance the block");
    ArpSID::sidRuntimeAdvanceCanonicalHostBlock(host, state, 480);
    const double expected = 16.0 + 480.0 * 130.0 / (48000.0 * 60.0);
    check(std::fabs(host.hostBeatPosition - expected) < 1.0e-6,
          "extrapolation must use the right BPM and sample-rate");
}

// Test 5 — playStateKnown=false leaves transportPlaying alone (regression
// from v505 — still must hold even with extrapolation feature in place).
static void test_unknown_play_state_still_protected() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    host.transportPlaying = true;
    host.lastTransportPlaying = true;
    host.hostTempo = 120.0;
    host.hostBeatPosition = 32.0;
    host.lastPublishedBeatPosition = 32.0;
    host.hasPublishedBeatPosition = true;

    HandlerTransport t{};
    t.bpm = 120.0;
    t.beatPosition = 32.0;       // stale, but play state unknown
    t.isPlaying = false;
    t.playStateKnown = false;
    t.sampleRate = 44100.0;
    t.frameCount = 256;

    const auto state = ArpSID::sidRuntimeBeginCanonicalHostBlock(
        model, host, t, 44100.0, 256);

    check(state.isPlaying == true,
          "playStateKnown=false must not flip transport off");
    check(std::fabs(host.hostBeatPosition - 32.0) < 1.0e-9,
          "unknown play state must not pre-advance a stale snapshot");
    ArpSID::sidRuntimeAdvanceCanonicalHostBlock(host, state, 256);
    const double expected = 32.0 + 256.0 * 120.0 / (44100.0 * 60.0);
    check(std::fabs(host.hostBeatPosition - expected) < 1.0e-6,
          "known internal playing state advances once at block end");
}

static void test_repeated_snapshot_is_monotonic_across_blocks() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    host.transportPlaying = true;
    host.lastTransportPlaying = true;

    HandlerTransport t{};
    t.bpm = 120.0;
    t.beatPosition = 4.0;
    t.isPlaying = true;
    t.playStateKnown = true;
    t.sampleRate = 48000.0;

    const double delta = 480.0 * 120.0 / (48000.0 * 60.0);
    const auto first = ArpSID::sidRuntimeBeginCanonicalHostBlock(model, host, t, 48000.0, 480);
    ArpSID::sidRuntimeAdvanceCanonicalHostBlock(host, first, 480);
    check(std::fabs(host.hostBeatPosition - (4.0 + delta)) < 1.0e-9,
          "first block advances once from fresh host beat");

    const auto second = ArpSID::sidRuntimeBeginCanonicalHostBlock(model, host, t, 48000.0, 480);
    check(std::fabs(second.currentBeatPosition - (4.0 + delta)) < 1.0e-9,
          "repeated published beat must not snap the next block backward");
    ArpSID::sidRuntimeAdvanceCanonicalHostBlock(host, second, 480);
    check(std::fabs(host.hostBeatPosition - (4.0 + 2.0 * delta)) < 1.0e-9,
          "two blocks advance by exactly two block durations");
}

int main() {
    std::printf("Running SID-808 transport-integration tests (v506)...\n");
    test_stale_snapshot_extrapolates_beat();
    test_fresh_snapshot_adopted();
    test_no_extrapolation_when_stopped();
    test_extrapolation_preserves_play_state();
    test_unknown_play_state_still_protected();
    test_repeated_snapshot_is_monotonic_across_blocks();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("All SID-808 transport-integration tests passed.\n");
    return 0;
}
