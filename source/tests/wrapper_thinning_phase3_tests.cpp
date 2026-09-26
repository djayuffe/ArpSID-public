// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/sid_runtime_host_block.h"
#include "arpsid/core/sid_runtime_model.h"

#include <cmath>
#include <iostream>

struct FakeTransport {
    double bpm = 120.0;
    double beatPosition = 0.0;
    bool isPlaying = false;
    bool playStateKnown = false;
    bool isLooping = false;
    double loopStart = 0.0;
    double loopEnd = 0.0;
    double sampleRate = 44100.0;
    int frameCount = 0;
    bool valid() const noexcept { return true; }
};

static int fail(const char* msg) {
    std::cerr << "FAIL: " << msg << "\n";
    return 1;
}

int main() {
    ArpSID::SidRuntimeModel model;
    ArpSID::SidRuntimeHostSurface host;

    FakeTransport stopped{};
    stopped.isPlaying = false;
    stopped.playStateKnown = true;
    stopped.bpm = 130.0;
    stopped.beatPosition = 12.0;
    stopped.sampleRate = 48000.0;

    auto a = ArpSID::sidRuntimeBeginCanonicalHostBlock(model, host, stopped, 44100.0, 64, 0);
    if (a.wasPlaying) return fail("initial wasPlaying must be false");
    if (a.isPlaying) return fail("stopped transport must remain stopped");
    // v910 live-play authority: the transport-sequencer gate (and its
    // deprecated suppressHostNoteOns alias) stays true when stopped, but the
    // live-instrument law must NEVER suppress — manual keyboard input plays
    // regardless of transport state.
    if (!a.suppressTransportSequencerNoteOns) return fail("known stopped transport must gate sequencer note-ons");
    if (!a.suppressHostNoteOns) return fail("deprecated alias must mirror the sequencer gate");
    if (a.suppressLiveInstrumentNoteOns) return fail("live instrument input must never be suppressed (stopped)");
    if (std::fabs(host.hostTempo - 130.0) > 1.0e-6) return fail("host tempo not updated through canonical law");
    if (std::fabs(host.hostBeatPosition - 12.0) > 1.0e-6) return fail("host beat not updated through canonical law");
    if (std::fabs(model.hostTempoBpm() - 130.0f) > 1.0e-5f) return fail("runtime tempo mirror not updated");
    if (model.transportPlayingFlag()) return fail("runtime transport playing flag wrong");
    ArpSID::sidRuntimeCommitCanonicalHostBlock(host, a);
    if (host.lastTransportPlaying) return fail("commit should preserve stopped last state");

    FakeTransport playing = stopped;
    playing.isPlaying = true;
    playing.beatPosition = 12.25;
    auto b = ArpSID::sidRuntimeBeginCanonicalHostBlock(model, host, playing, 48000.0, 64, 0);
    if (b.wasPlaying) return fail("start edge should see previous stopped state");
    if (!b.isPlaying) return fail("playing transport not propagated");
    if (b.suppressHostNoteOns) return fail("known playing transport must not suppress note-ons");
    if (b.suppressLiveInstrumentNoteOns) return fail("live instrument input must never be suppressed (playing)");
    if (!model.transportPlayingFlag()) return fail("runtime playing flag not updated");
    ArpSID::sidRuntimeCommitCanonicalHostBlock(host, b);
    if (!host.lastTransportPlaying) return fail("commit should record playing state");

    FakeTransport unknown = playing;
    unknown.playStateKnown = false;
    unknown.isPlaying = false;
    auto c = ArpSID::sidRuntimeBeginCanonicalHostBlock(model, host, unknown, 48000.0, 64, 0);
    if (c.suppressHostNoteOns) return fail("unknown play state must not suppress host note-ons");
    if (c.suppressLiveInstrumentNoteOns) return fail("live instrument input must never be suppressed (unknown)");

    std::cout << "PASS wrapper thinning phase3 host block law\n";
    return 0;
}
