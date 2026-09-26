// Copyright (C) 2024-2026 Ulf Bertilsson
// host_transport_partial_state_v505_tests.cpp
//
// Regression coverage for the host-transport snapshot when the host provides
// partial / unknown transport state. Three guarantees are pinned:
//
// 1) playStateKnown=false must NOT overwrite host.transportPlaying.
// Previously a default-constructed (read-failed) snapshot trampled the
// kernel's host.transportPlaying to false, synthesising spurious stop
// edges every render block where the seqlock read missed.
//
// 2) An undefined beat position must NOT snap host.hostBeatPosition to 0.
// Beat zero is a legitimate musical position; missing data must
// preserve the previously published value.
//
// 3) Even with playStateKnown=false the snapshot tempo must be honoured if
// it is finite and within range, so the sequencer/tempo math is correct
// while we wait for the host to expose its play state.

#include "arpsid/core/sid_runtime_host_block.h"
#include "arpsid/core/sid_runtime_host_surface.h"
#include "arpsid/core/sid_runtime_model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

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

static void test_unknown_play_state_does_not_synth_stop_edge() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    // Simulate: host previously confirmed transport is playing.
    host.transportPlaying = true;
    host.lastTransportPlaying = true;
    host.hostTempo = 130.0;
    host.hostBeatPosition = 64.0;

    // New poll fails to obtain play state — playStateKnown=false (read failed
    // or host doesn't expose the callback). With the fix in place, the model
    // must NOT flip transportPlaying off and synthesise a stop edge.
    HandlerTransport t{};
    t.bpm = 130.0;
    t.beatPosition = 64.25;
    t.isPlaying = false;          // default value when read failed
    t.playStateKnown = false;     // sentinel: don't adopt isPlaying
    t.sampleRate = 44100.0;
    t.frameCount = 256;

    const auto state = ArpSID::sidRuntimeBeginCanonicalHostBlock(
        model, host, t, 44100.0, 256);

    check(state.isPlaying == true,
          "unknown play-state must not flip transport off when it was previously playing");
    check(host.transportPlaying == true,
          "host.transportPlaying must not be overwritten when playStateKnown=false");
    // wasPlaying still reads lastTransportPlaying which was true; start/stop
    // edge detection in handleTransportDiscontinuity_ would now see
    // was==is==true, i.e. no edge — exactly what we want.
    check(state.wasPlaying == true,
          "wasPlaying must remain true so no synthetic edge fires");
}

static void test_undefined_beat_does_not_snap_to_zero() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    host.hostBeatPosition = 32.0;
    host.hostTempo = 120.0;

    HandlerTransport t{};
    t.bpm = 120.0;
    t.beatPosition = std::numeric_limits<double>::quiet_NaN(); // host did not report
    t.playStateKnown = false;
    t.sampleRate = 44100.0;
    t.frameCount = 256;

    const auto state = ArpSID::sidRuntimeBeginCanonicalHostBlock(
        model, host, t, 44100.0, 256);
    (void)state;
    check(host.hostBeatPosition == 32.0,
          "NaN beat must NOT snap host.hostBeatPosition to 0; previous value must be preserved");
}

static void test_negative_beat_does_not_snap_to_zero() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    host.hostBeatPosition = 4.0;
    host.hostTempo = 120.0;

    HandlerTransport t{};
    t.bpm = 120.0;
    t.beatPosition = -1.0;        // out of range
    t.playStateKnown = false;
    t.sampleRate = 44100.0;
    t.frameCount = 256;

    const auto state = ArpSID::sidRuntimeBeginCanonicalHostBlock(
        model, host, t, 44100.0, 256);
    (void)state;
    check(host.hostBeatPosition == 4.0,
          "negative beat must NOT snap host.hostBeatPosition to 0");
}

static void test_known_play_state_is_adopted_normally() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    host.transportPlaying = false;
    host.lastTransportPlaying = false;
    host.hostTempo = 120.0;

    HandlerTransport t{};
    t.bpm = 140.0;
    t.beatPosition = 8.0;
    t.isPlaying = true;
    t.playStateKnown = true;     // host reported a real play state
    t.sampleRate = 44100.0;
    t.frameCount = 256;

    const auto state = ArpSID::sidRuntimeBeginCanonicalHostBlock(
        model, host, t, 44100.0, 256);

    check(state.isPlaying == true,
          "known play-state=true must be adopted by the runtime");
    check(host.transportPlaying == true,
          "host.transportPlaying must reflect the known transport state");
    check(host.hostBeatPosition == 8.0,
          "valid beat position must be adopted");
    check(host.hostTempo > 139.0 && host.hostTempo < 141.0,
          "valid tempo must be adopted");
}

static void test_known_play_state_off_is_adopted_normally() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host{};
    host.transportPlaying = true;
    host.lastTransportPlaying = true;
    host.hostTempo = 120.0;

    HandlerTransport t{};
    t.bpm = 120.0;
    t.beatPosition = 12.0;
    t.isPlaying = false;
    t.playStateKnown = true;     // host explicitly says "stopped"
    t.sampleRate = 44100.0;
    t.frameCount = 256;

    const auto state = ArpSID::sidRuntimeBeginCanonicalHostBlock(
        model, host, t, 44100.0, 256);

    check(state.isPlaying == false,
          "known play-state=false must be adopted by the runtime");
    check(host.transportPlaying == false,
          "host.transportPlaying must reflect the explicit stop");
    // wasPlaying came from lastTransportPlaying which was true — this is the
    // real stop edge that handleTransportDiscontinuity_ should act on.
    check(state.wasPlaying == true,
          "wasPlaying must be true so the real stop edge fires");
}

int main() {
    std::printf("Running host-transport partial-state tests (v505)...\n");
    test_unknown_play_state_does_not_synth_stop_edge();
    test_undefined_beat_does_not_snap_to_zero();
    test_negative_beat_does_not_snap_to_zero();
    test_known_play_state_is_adopted_normally();
    test_known_play_state_off_is_adopted_normally();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("All host-transport partial-state tests passed.\n");
    return 0;
}
