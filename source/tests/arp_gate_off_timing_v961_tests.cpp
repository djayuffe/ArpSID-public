// Copyright (C) 2024-2026 Ulf Bertilsson
// v961 arpeggiator gate-off timing closure (the former "P2" arp+non-poly dip).
//
// Root cause found: pendingGateOff_ carried TWO meanings — (a) the normal "this
// sounding step will need its gate-off at the gate position" state, armed by
// EVERY note-on, and (b) "flush the release at the next block start", intended
// only for transport rewinds/jumps and buffer-exhausted step boundaries. The
// top-of-block flush in collectTimedEvents() couldn't tell them apart, so every
// arp note was gated off at the start of the next render block (~10 ms at
// 512/48k). Fast rates masked it (a 20 ms step expects a ~16 ms gate); poly
// masked it behind release tails; mono/legato/unison at slow rates played
// 10 ms ticks instead of held steps (misread as "~2 dB attenuation" earlier).
//
// Fix: flushGateOffAtBlockStart_ carries meaning (b); the step loop now emits
// the armed gate-off at its TRUE gate position (gateLength * stepSamples into
// the step window) as render time reaches it, so GATE shapes the duty cycle at
// any rate.
//
// Part 1 pins the Arpeggiator event mechanics directly; part 2 renders the real
// kernel and requires non-poly slow-rate arp steps to hold full level.

#include "arpsid/engines/arpeggiator.h"
#include "au3/ArpSIDDSPKernel.hpp"
#include "parameter_ids.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace ArpSID;

static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "arp_gate_off_timing_v961_tests FAIL: %s\n", msg); std::exit(1); }
}

// Collect events over many fixed-size blocks; return flat (absoluteSample, note, gate).
struct Ev { long t; int note; bool gate; };
static std::vector<Ev> run(Arpeggiator& arp, int blocks, int blockSize) {
    std::vector<Ev> evs;
    Arpeggiator::TimedArpEvent buf[64];
    for (int b = 0; b < blocks; ++b) {
        const int n = arp.collectTimedEvents(blockSize, buf, 64);
        for (int i = 0; i < n; ++i)
            evs.push_back({(long)b * blockSize + buf[i].sampleOffset, buf[i].note.midiNote, buf[i].note.gate});
    }
    return evs;
}

int main() {
    ArpSID::prewarmAllSidTables();

    // ── Part 1a: normal steps — gate-off lands at the gate position, not at the
    //             next block boundary.
    {
        Arpeggiator arp;
        arp.setSampleRate(48000.0);
        arp.setEnabled(true);
        arp.setRate(0.4f);          // ≈1.2 Hz → ≈0.84 s steps (slow, many blocks per step)
        arp.setGateLength(0.5f);    // 50% duty: unambiguous mid-step gate-off
        arp.noteOn(60, 1.0f);
        arp.noteOn(64, 1.0f);
        arp.noteOn(67, 1.0f);

        const auto evs = run(arp, 260, 512); // ~2.8 s
        require(evs.size() >= 4, "slow arp must emit multiple events");
        // Find the first note-on and its matching gate-off.
        size_t on = 0; while (on < evs.size() && !evs[on].gate) ++on;
        require(on < evs.size(), "arp must emit a note-on");
        size_t off = on + 1; while (off < evs.size() && !(evs[off].note == evs[on].note && !evs[off].gate)) ++off;
        require(off < evs.size(), "the sounding arp note must receive a gate-off");
        const long held = evs[off].t - evs[on].t;
        std::printf("slow-rate 50%% gate: note held %ld samples (%.1f ms)\n", held, held / 48.0);
        require(held > 2048, "gate-off must NOT chop the note at the next render block (v961 bug)");
        // 50% duty of the step: the off-on distance must be ~half the on-on distance.
        size_t on2 = off + 1; while (on2 < evs.size() && !evs[on2].gate) ++on2;
        require(on2 < evs.size(), "arp must emit a second note-on");
        const long stepLen = evs[on2].t - evs[on].t;
        const double duty = (double)held / (double)stepLen;
        std::printf("step=%ld samples, duty=%.2f (gate=0.50)\n", stepLen, duty);
        require(duty > 0.40 && duty < 0.60, "gate-off must land at ~gateLength fraction of the step");
    }

    // ── Part 1b: the legit flush still works — a transport rewind releases the
    //             sounding note at the start of the next block (no stuck voice).
    {
        Arpeggiator arp;
        arp.setSampleRate(48000.0);
        arp.setEnabled(true);
        arp.setRate(0.4f);
        arp.setGateLength(0.9f);
        arp.noteOn(60, 1.0f);
        Arpeggiator::TimedArpEvent buf[64];
        int n = arp.collectTimedEvents(512, buf, 64);
        require(n >= 1 && buf[0].note.gate, "first block must emit the first note-on");
        const int sounding = buf[0].note.midiNote;
        arp.rewindPhase(true);      // transport jump while the note sounds
        n = arp.collectTimedEvents(512, buf, 64);
        require(n >= 1, "post-rewind block must emit events");
        require(!buf[0].note.gate && buf[0].note.midiNote == sounding && buf[0].sampleOffset == 0,
                "rewind must flush the sounding note's gate-off at the next block start");
    }

    // ── Part 2: kernel render — non-poly arp at slow rate holds full level.
    auto renderPeak = [](float voiceModeNorm, bool arpOn, float rateNorm) {
        auto k = std::make_unique<ArpSIDDSPKernel>();
        k->setup(48000.0, 512);
        k->setParameter((int)kParamVoiceMode, voiceModeNorm);
        k->setParameter((int)kParamArpEnable, arpOn ? 1.0f : 0.0f);
        k->setParameter((int)kParamArpRate, rateNorm);
        constexpr int F = 512;
        std::array<float, F> l{}, r{};
        float* o[2] = { l.data(), r.data() };
        TransportState t{};
        t.isPlaying = true; t.playStateKnown = true; t.sampleRate = 48000.0; t.frameCount = F;
        k->processBlock(o, 2, F, nullptr, 0, t);
        TimedEvent on[3]{};
        for (int i = 0; i < 3; ++i) { on[i].sampleOffset=0; on[i].kind=EventKind::NoteOn; on[i].channel=0; on[i].value=1.0f; }
        on[0].pitch=60; on[1].pitch=64; on[2].pitch=67;
        float peak = 0.0f;
        for (int b = 0; b < 160; ++b) {
            l.fill(0.0f); r.fill(0.0f);
            if (b == 0) k->processBlock(o, 2, F, on, 3, t);
            else        k->processBlock(o, 2, F, nullptr, 0, t);
            for (int i = 0; i < F; ++i) {
                require(std::isfinite(l[(size_t)i]), "arp render must stay finite");
                const float a = std::fabs(l[(size_t)i]); if (a > peak) peak = a;
            }
        }
        return peak;
    };
    const float MONO = 1.0f / 3.0f, UNISON = 1.0f;
    const float monoNoArp  = renderPeak(MONO, false, 0.5f);
    const float monoSlow   = renderPeak(MONO, true, 0.0f);
    const float unisonSlow = renderPeak(UNISON, true, 0.0f);
    std::printf("mono noArp=%.3f monoSlowArp=%.3f unisonSlowArp=%.3f\n", monoNoArp, monoSlow, unisonSlow);
    require(monoSlow > monoNoArp * 0.85f,
            "mono slow-rate arp must hold full level (was chopped to ~10ms ticks)");
    require(unisonSlow > 0.85f,
            "unison slow-rate arp must hold full level");

    std::printf("arp_gate_off_timing_v961_tests PASS\n");
    return 0;
}
