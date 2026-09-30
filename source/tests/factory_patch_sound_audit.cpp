// Copyright (C) 2024-2026 Ulf Bertilsson
// Factory patch sound audit: renders every factory patch (and the init
// sound) through the VST3 kernel host and measures what can go wrong with
// the sound: silence, clipping, non-finite samples, DC offset, idle noise,
// notes that never stop, and level spread between patches.
//
//   arpsid_factory_patch_sound_audit            table of every patch
//   arpsid_factory_patch_sound_audit --check    also fail on hard errors

#include "vst3/arpsid_vst3_kernel_host.h"

#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "au3/ArpSIDCanonicalEvents.h"
#include "factory_patch_params.h"
#include "parameter_ids.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ArpSID;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

double db(double v) { return v > 0.0 ? 20.0 * std::log10(v) : -999.0; }

struct Stats {
    double peak = 0, sumSq = 0, sumL = 0, sumR = 0, sqL = 0, sqR = 0;
    long n = 0, clips = 0, nonFinite = 0;
    void add(float l, float r) {
        if (!std::isfinite(l) || !std::isfinite(r)) { ++nonFinite; return; }
        peak = std::max({peak, (double)std::fabs(l), (double)std::fabs(r)});
        if (std::fabs(l) >= 0.999f || std::fabs(r) >= 0.999f) ++clips;
        sumSq += 0.5 * ((double)l * l + (double)r * r);
        sumL += l; sumR += r; sqL += (double)l * l; sqR += (double)r * r;
        ++n;
    }
    double rms() const { return n ? std::sqrt(sumSq / n) : 0; }
    double dc() const { return n ? 0.5 * std::fabs(sumL / n + sumR / n) : 0; }
    double balanceDb() const { return (sqL > 0 && sqR > 0) ? 10.0 * std::log10(sqL / sqR) : 0.0; }
};

struct Rig {
    Vst3KernelHost h;
    std::vector<float> l = std::vector<float>(kBlock), r = std::vector<float>(kBlock);
    TransportState t{};
    Rig() {
        h.setup(kRate, kBlock);
        t.sampleRate = kRate;
        t.frameCount = kBlock;
        t.bpm = 120.0;
        t.isPlaying = true;
        t.playStateKnown = true;
    }
    void block(const std::vector<TimedEvent>& ev, Stats* s) {
        float* o[2] = {l.data(), r.data()};
        h.render(o, 2, kBlock, ev.empty() ? nullptr : ev.data(), (int)ev.size(), t);
        t.beatPosition += kBlock * t.bpm / (kRate * 60.0);
        if (s) for (int i = 0; i < kBlock; ++i) s->add(l[(size_t)i], r[(size_t)i]);
    }
    void run(int blocks, Stats* s) { for (int b = 0; b < blocks; ++b) block({}, s); }
};

TimedEvent note(bool on, int pitch, int ch = 0) {
    TimedEvent e{};
    e.sampleOffset = 0;
    e.kind = on ? EventKind::NoteOn : EventKind::NoteOff;
    e.channel = (uint8_t)ch;
    e.pitch = (int16_t)pitch;
    e.value = on ? 0.9f : 0.0f;
    return e;
}

int blocksFor(double seconds) { return (int)std::ceil(seconds * kRate / kBlock); }

struct Result {
    int slot = 0;
    std::string name;
    bool drum = false, autoplay = false;
    Stats idle, hold, tailEnd;
    double releaseSec = -1;  // time for the output to fall 60 dB below the hold peak
};

Result audit(int slot, bool initOnly) {
    Result res;
    res.slot = slot;
    Rig g;
    if (!initOnly) g.h.loadFactorySlot(slot);
    g.run(4, nullptr);  // apply the patch
    const float drumEnable = g.h.parameter(kParamDrSidEnable);
    res.drum = drumEnable > 0.5f;
    res.autoplay = g.h.parameter(kParamArpEnable) > 0.5f || g.h.parameter(kParamSeqEnable) > 0.5f;
    res.name = initOnly ? "(init)" : factoryPatchNameForSlot(slot);
    g.run(blocksFor(1.0), &res.idle);  // before any note
    const int pitch = res.drum ? 38 : 60;
    g.block({note(true, pitch)}, &res.hold);
    g.run(blocksFor(1.0), &res.hold);
    g.block({note(false, pitch)}, nullptr);
    // Release: find when the level drops 60 dB below the hold peak.
    const double threshold = std::max(res.hold.peak * 1e-3, 1e-6);
    for (int b = 0; b < blocksFor(6.0); ++b) {
        Stats s;
        g.block({}, &s);
        if (res.releaseSec < 0 && s.peak < threshold) res.releaseSec = (b + 1) * kBlock / kRate;
    }
    g.run(blocksFor(1.0), &res.tailEnd);  // 6..7 s after note-off
    return res;
}

} // namespace

int main(int argc, char** argv) {
    const bool check = argc > 1 && std::strcmp(argv[1], "--check") == 0;
    std::vector<Result> all;
    all.push_back(audit(-1, true));
    for (int s = 0; s < kCanonicalFactoryPatchSlotCount; ++s) all.push_back(audit(s, false));

    int hard = 0;
    std::printf("%4s %-28s %-4s %8s %8s %7s %6s %7s %8s %8s %6s %5s\n", "slot", "name", "kind", "peak", "rms",
                "dc", "clips", "bal", "idle", "tail7s", "rel", "nan");
    for (const auto& r : all) {
        std::printf("%4d %-28.28s %-4s %8.1f %8.1f %7.4f %6ld %7.1f %8.1f %8.1f %6.2f %5ld\n", r.slot, r.name.c_str(),
                    r.drum ? "drum" : (r.autoplay ? "auto" : "syn"), db(r.hold.peak), db(r.hold.rms()), r.hold.dc(),
                    r.hold.clips, r.hold.balanceDb(), db(r.idle.peak), db(r.tailEnd.peak), r.releaseSec,
                    r.hold.nonFinite + r.idle.nonFinite + r.tailEnd.nonFinite);
        const bool silent = r.hold.peak < 1e-3;
        const bool nonFinite = (r.hold.nonFinite + r.idle.nonFinite + r.tailEnd.nonFinite) > 0;
        if (check && (silent || nonFinite || r.hold.clips > 0)) ++hard;
    }
    if (check && hard) {
        std::fprintf(stderr, "factory patch sound audit: %d patch(es) silent, clipping or non-finite\n", hard);
        return 1;
    }
    return 0;
}
