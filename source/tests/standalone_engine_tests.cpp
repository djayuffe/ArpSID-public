// Copyright (C) 2024-2026 Ulf Bertilsson
// Standalone app engine (Linux / Windows app), headless:
//   - process() renders a note, feeds the clock, clears extra outputs;
//   - the MIDI channel filter, MIDI Start / Stop and panic;
//   - factory and user patch names, and a MIDI program change replacing a
//     user patch;
//   - the session round trip (sound + user patch name);
//   - settings parse / serialize and sanitising.

#include "standalone/arpsid_standalone_engine.h"
#include "standalone/arpsid_standalone_settings.h"

#include "arpsid/patchbank/forensic_patch_bank.h"
#include "factory_patch_params.h"
#include "parameter_ids.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace ArpSID;
using namespace ArpSID::Standalone;

namespace {

int failures = 0;
void check(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    }
}

// Render <blocks> x 256 frames; returns the peak.
float render(Engine& e, int blocks, int outs = 2) {
    std::vector<std::vector<float>> buf(static_cast<std::size_t>(outs), std::vector<float>(256, 1.0f));
    std::vector<float*> ptrs;
    for (auto& b : buf) ptrs.push_back(b.data());
    float peak = 0.f;
    for (int i = 0; i < blocks; ++i) {
        for (auto& b : buf) std::fill(b.begin(), b.end(), 1.0f);
        e.process(ptrs.data(), outs, nullptr, 0, 256);
        for (int c = 0; c < std::min(outs, 2); ++c)
            for (float v : buf[static_cast<std::size_t>(c)]) peak = std::max(peak, std::fabs(v));
        for (int c = 2; c < outs; ++c)
            for (float v : buf[static_cast<std::size_t>(c)]) check(v == 0.0f, "outputs past the second are cleared");
    }
    return peak;
}

} // namespace

int main() {
    // ── audio, MIDI filter, clock ──────────────────────────────────────────
    {
        Engine e;
        e.prepare(48000.0, 256);
        e.selectFactoryPatch(0);
        render(e, 20);
        const std::uint8_t on[3] = {0x90, 60, 100};
        e.setMidiChannel(2);
        e.midiIn(on, 3); // channel 1: filtered out
        check(render(e, 20) < 1e-4f, "a note on another channel is filtered out");
        check(e.lastMidiMs() == 0, "filtered MIDI does not count as activity");
        e.setMidiChannel(1);
        e.midiIn(on, 3);
        check(render(e, 20) > 0.01f, "a note on the input channel plays");
        check(e.lastMidiMs() != 0, "accepted MIDI is reported");
        e.panic();
        render(e, 400);
        check(render(e, 10) < 1e-5f, "panic silences everything");

        e.setMidiChannel(0);
        const std::uint8_t on2[3] = {0x95, 64, 100};
        e.midiIn(on2, 3);
        check(render(e, 20, 4) > 0.01f, "omni accepts any channel (and 4 outputs work)");
        e.uiMidi(on2, 3);
        e.panic();
        render(e, 400);

        e.setTempo(150.0);
        check(e.tempo() == 150.0, "tempo set");
        e.setTempo(1000.0);
        check(e.tempo() == 300.0, "tempo clamped");
        e.setTempo(120.0);
        check(!e.playing(), "the clock starts stopped");
        const std::uint8_t start = 0xFA, stop = 0xFC;
        e.midiIn(&start, 1);
        check(e.playing(), "MIDI Start plays");
        render(e, 188); // 188 x 256 / 48000 s = ~1.0 s = 2 beats at 120 BPM
        check(std::fabs(e.beatPosition() - 2.0) < 0.02, "the clock advances at the tempo");
        e.midiIn(&stop, 1);
        check(!e.playing(), "MIDI Stop stops");
        const double b = e.beatPosition();
        render(e, 10);
        check(e.beatPosition() == b, "a stopped clock stays put");
        e.setPlaying(true);
        render(e, 1);
        check(e.beatPosition() < 0.05, "play restarts from the top");
        const std::uint8_t clock = 0xF8;
        e.midiIn(&clock, 1); // ignored
        const std::uint8_t shortMsg[2] = {0x90, 60};
        e.midiIn(shortMsg, 2); // incomplete: ignored
        check(e.load() >= 0.0f && std::isfinite(e.load()), "load meter is finite");
    }

    // ── patch names and session ────────────────────────────────────────────
    {
        Engine e;
        e.prepare(48000.0, 256);
        e.selectFactoryPatch(12);
        render(e, 2);
        check(e.currentFactorySlot() == 12 && !e.isUserPatch(), "factory patch selected");
        check(e.patchName() == factoryPatchNameForSlot(12), "factory patch name");
        e.loadPatch(makeFactoryPatchStateRootForSlot(40), "Glass Strings");
        check(e.isUserPatch() && e.patchName() == "Glass Strings", "user patch is named before it renders");
        render(e, 2);
        check(e.currentFactorySlot() == 40 && e.patchName() == "Glass Strings", "user patch plays and keeps its name");

        // Session round trip into a fresh engine.
        const std::vector<std::uint8_t> session = e.saveSession();
        Engine f;
        f.prepare(48000.0, 256);
        check(f.loadSession(session.data(), session.size()), "session loads");
        render(f, 2);
        check(f.isUserPatch() && f.patchName() == "Glass Strings" && f.currentFactorySlot() == 40,
              "the session restores the patch and its name");
        check(!f.loadSession(session.data(), 10), "a truncated session is refused");
        std::vector<std::uint8_t> bad = session;
        bad[0] ^= 0xFF;
        check(!f.loadSession(bad.data(), bad.size()), "a foreign file is refused");

        // A MIDI program change (factory patch) replaces the user patch.
        const std::uint8_t pc[2] = {0xC0, 5};
        f.midiIn(pc, 2);
        check(f.applyPendingProgramChange(), "a program change is queued for the UI thread");
        check(!f.applyPendingProgramChange(), "and applied once");
        render(f, 4);
        check(f.currentFactorySlot() == 5, "a MIDI program change selects that factory patch");
        check(!f.isUserPatch(), "a program change to another patch drops the user name");
        // Bank Select MSB 1 reaches slots 128..179; beyond the bank is ignored.
        const std::uint8_t bank1[3] = {0xB0, 0, 1}, pc20[2] = {0xC0, 20}, pc60[2] = {0xC0, 60};
        f.midiIn(bank1, 3);
        f.midiIn(pc20, 2);
        f.applyPendingProgramChange();
        render(f, 2);
        check(f.currentFactorySlot() == 148, "bank 1, program 20 is slot 149");
        f.midiIn(pc60, 2);
        check(!f.applyPendingProgramChange(), "a program past the last slot is ignored");
        f.selectFactoryPatch(3);
        render(f, 2);
        check(!f.isUserPatch() && f.patchName() == factoryPatchNameForSlot(3), "a factory pick drops the user name");
        f.setUserPatchName("Saved As");
        check(f.isUserPatch() && f.patchName() == "Saved As", "save-as names the current patch");

        Engine g;
        g.prepare(48000.0, 256);
        g.selectFactoryPatch(7);
        render(g, 2);
        const std::vector<std::uint8_t> plain = g.saveSession();
        Engine h;
        check(h.loadSession(plain.data(), plain.size()), "factory session loads");
        render(h, 2);
        check(!h.isUserPatch() && h.currentFactorySlot() == 7, "a factory session stays the factory patch");
    }

    // ── settings ───────────────────────────────────────────────────────────
    {
        Settings s;
        s.audioApi = "pulse";
        s.audioOutput = "Built-in Audio Analog Stereo";
        s.audioInput = "USB Mic";
        s.sampleRate = 44100;
        s.bufferFrames = 128;
        s.midiInput = "Arturia KeyStep 32:Arturia KeyStep 32 MIDI 1";
        s.midiChannel = 3;
        s.bpm = 98.5;
        s.zoom = 1.25;
        s.tab = 4;
        const Settings r = parse(serialize(s));
        check(r.audioApi == s.audioApi && r.audioOutput == s.audioOutput && r.audioInput == s.audioInput &&
                  r.sampleRate == 44100 && r.bufferFrames == 128 && r.midiInput == s.midiInput &&
                  r.midiChannel == 3 && r.bpm == 98.5 && r.zoom == 1.25 && r.tab == 4,
              "settings round-trip");
        const Settings d = parse("# junk\nunknown = 1\nsample_rate = 5\nmidi_channel = 99\nbpm = abc\nzoom = 9\n");
        check(d.sampleRate == 48000 && d.midiChannel == 16 && d.bpm == 120.0 && d.zoom == 3.0 && d.midiInput == "*",
              "bad values are sanitised and unknown keys ignored");
        check(parse("").bufferFrames == 256, "empty settings are the defaults");
    }

    if (failures) {
        std::fprintf(stderr, "standalone_engine_tests: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("standalone_engine_tests PASS");
    return 0;
}
