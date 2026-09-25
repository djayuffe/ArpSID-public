// v953 default-patch CLASSIC audibility closure.
//
// Pins the "synth classic don't work" P0: the shipped DEFAULT/init patch must
// produce audible output in CLASSIC (BitPerfect) mode from a real host MIDI
// note-on. The pre-existing v898 audibility test masked this because it set
// kParamVirtualGate=1 (which fires a GUI virtual-keyboard note) AND boosted the
// patch; the audible peak it observed came from the virtual gate, not from the
// default patch's host note.
//
// Root cause: kParamFilterMode default was 0.0, which the engine maps to
// FilterMode::None (index 0 of 8). With the default all-voice filter routing,
// FilterMode::None sends every voice to the (near-silent on the default 8580 R5
// chip) dry path, so the documented "C64-authentic LowPass" default patch was
// actually silent. The default now selects LowPass (index 1 == 1/7 normalized),
// matching the parameter_ids.h intent comment.
//
// This test uses ONLY real defaults + a host MIDI note (no VirtualGate) so it
// exercises exactly the path a user hits when they open the plugin and play.

#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"
#include <array>
#include <cmath>
#include <cstdio>

using namespace ArpSID;

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "default_patch_classic_audibility_v953_tests FAIL: %s\n", msg);
        std::exit(1);
    }
}

// Render the DEFAULT patch (fresh kernel, no snapshot, no virtual gate) and play
// a single host MIDI note in whatever mode the defaults resolve to.
static float renderDefaultPatchPeak(bool synthMode, int pitch, int channel) {
    ArpSIDDSPKernel k;
    k.setup(48000.0, 512);
    if (synthMode) {
        // Flip to SYNTH via the same async intent path the AU parameter tree uses.
        k.enqueueParameterIntent(kParamSynthModeEnable, 1.0f);
    }

    constexpr int frames = 512;
    std::array<float, frames> l{}, r{};
    float* outs[2] = {l.data(), r.data()};
    TransportState t{};
    t.isPlaying = true;
    t.playStateKnown = true;
    t.sampleRate = 48000.0;
    t.frameCount = frames;

    float peak = 0.0f;
    for (int b = 0; b < 10; ++b) {
        l.fill(0.0f); r.fill(0.0f);
        if (b == 0) {
            TimedEvent ev{};
            ev.sampleOffset = 0;
            ev.kind = EventKind::NoteOn;
            ev.channel = channel;
            ev.pitch = pitch;
            ev.value = 1.0f;
            k.processBlock(outs, 2, frames, &ev, 1, t);
        } else {
            k.processBlock(outs, 2, frames, nullptr, 0, t);
        }
        for (int i = 0; i < frames; ++i) {
            const float a = std::fabs(l[(size_t)i]);
            require(std::isfinite(a), "rendered default-patch samples must remain finite");
            if (a > peak) peak = a;
        }
    }
    return peak;
}

int main() {
    ArpSID::prewarmAllSidTables();

    // The default FilterMode must select LowPass (index 1 of 8), not None (index 0).
    // Engine map is idx = (int)(value * 8): 1/7 -> idx 1 -> LowPass.
    const float fmDefault = defaultNormalizedParamValue(kParamFilterMode);
    const int fmEngineIdx = (int)(fmDefault * 8.0f);
    require(fmEngineIdx == 1,
            "default FilterMode must map to LowPass (engine index 1), not None (index 0)");

    // CLASSIC / BitPerfect is the default mode: a plain host note on the default
    // patch must be clearly audible AT FULL LEVEL. The old hard-restart bug left
    // it near-silent (~0.0015) because scheduleHardRestart()'s gate re-assert was
    // clobbered by the envelope's 46-cycle discharge window, so this asserts a
    // real musical level, not just non-zero.
    const float classicPeak = renderDefaultPatchPeak(false, 60, 0);
    std::printf("default-patch CLASSIC host-note peak = %f\n", classicPeak);
    require(classicPeak > 0.15f,
            "DEFAULT patch must produce full-level CLASSIC output from a host MIDI note (v953 P0)");

    // SYNTH mode on the default patch must also stay audible (guard against a
    // fix that trades one silent mode for another).
    const float synthPeak = renderDefaultPatchPeak(true, 60, 0);
    std::printf("default-patch SYNTH host-note peak = %f\n", synthPeak);
    require(synthPeak > 0.05f,
            "DEFAULT patch must produce audible SYNTH output from a host MIDI note");

    // CLASSIC must be within a sane loudness ratio of SYNTH, not ~40x quieter as
    // it was while the hard-restart bug silenced the BitPerfect envelope attack.
    require(classicPeak > 0.25f * synthPeak,
            "CLASSIC loudness must be comparable to SYNTH (hard-restart attack must survive)");

    std::printf("default_patch_classic_audibility_v953_tests PASS\n");
    return 0;
}
