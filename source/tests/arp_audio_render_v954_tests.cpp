// v954 arpeggiator audio-render closure.
//
// Pins the P0 that the arpeggiator (the plugin's namesake feature) produced NO
// audible output in the AU3/canonical render path. The arp received held notes
// and even created BitPerfect voices, but BitPerfect advertised fractional
// sub-sample-span support, so the host-cycle dispatcher rendered fractional
// intervals that fought renderBitPerfectWithArp() (the only path that pumps the
// arp's timed step events and interleaves them with render sub-blocks). The arp
// voices were created but overwritten -> silent arpeggio, envelope stuck ~0.11.
//
// Fix evolved in v965: ARP gate transitions are materialized into the same
// canonical sample-stamped queue before the final dispatch. BitPerfect can now
// retain cycle/subphase rendering while ARP notes are opened/closed at their
// exact host sample offsets; the old coarse slice fallback is no longer needed.
//
// There was previously no test that rendered arp audio through the kernel; the
// arpeggiator_rate_exp test only covers the arp step-rate math.

#include "au3/ArpSIDDSPKernel.hpp"
#include "parameter_ids.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>

using namespace ArpSID;

static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "arp_audio_render_v954_tests FAIL: %s\n", msg); std::exit(1); }
}

static float renderArpChordPeak(float arpRate) {
    auto k = std::make_unique<ArpSIDDSPKernel>();
    k->setup(48000.0, 512);
    // CLASSIC / BitPerfect is default. Enable the arp and set a rate.
    k->enqueueParameterIntent(kParamArpEnable, 1.0f);
    k->enqueueParameterIntent(kParamArpRate, arpRate);
    k->enqueueParameterIntent(kParamArpGate, 0.9f);

    constexpr int F = 512;
    std::array<float, F> l{}, r{};
    float* o[2] = { l.data(), r.data() };
    TransportState t{};
    t.isPlaying = true; t.playStateKnown = true; t.bpm = 120.0; t.sampleRate = 48000.0; t.frameCount = F;
    // drain the param intents before the chord arrives
    k->processBlock(o, 2, F, nullptr, 0, t);

    TimedEvent chord[3];
    for (int i = 0; i < 3; ++i) {
        chord[i] = TimedEvent{};
        chord[i].sampleOffset = 0;
        chord[i].kind = EventKind::NoteOn;
        chord[i].channel = 0;
        chord[i].pitch = static_cast<int16_t>(60 + i * 4); // C-E-G
        chord[i].value = 1.0f;
    }

    float peak = 0.0f;
    for (int b = 0; b < 200; ++b) {
        l.fill(0.0f); r.fill(0.0f);
        t.beatPosition = 0.02 * b;
        k->processBlock(o, 2, F, b == 0 ? chord : nullptr, b == 0 ? 3 : 0, t);
        for (int i = 0; i < F; ++i) {
            require(std::isfinite(l[(size_t)i]), "arp render must stay finite");
            const float a = std::fabs(l[(size_t)i]);
            if (a > peak) peak = a;
        }
    }
    return peak;
}

int main() {
    ArpSID::prewarmAllSidTables();
    const float fast   = renderArpChordPeak(1.0f);
    const float mid    = renderArpChordPeak(0.5f);
    const float slow   = renderArpChordPeak(0.1f);
    const float synced = renderArpChordPeak(0.0f);
    std::printf("arp peaks: fast=%f mid=%f slow=%f tempo-synced=%f\n", fast, mid, slow, synced);
    require(fast   > 0.1f, "arp (fast rate) must produce audible arpeggio (v954 P0)");
    require(mid    > 0.1f, "arp (mid rate) must produce audible arpeggio (v954 P0)");
    require(slow   > 0.1f, "arp (slow rate) must produce audible arpeggio (v954 P0)");
    require(synced > 0.1f, "arp (tempo-synced) must produce audible arpeggio (v954 P0)");
    std::printf("arp_audio_render_v954_tests PASS\n");
    return 0;
}
