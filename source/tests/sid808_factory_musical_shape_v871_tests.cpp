// Copyright (C) 2024-2026 Ulf Bertilsson
// sid808_factory_musical_shape_v871_tests.cpp
//
// v871 guards every canonical SID808 factory slot (120..149), not only the
// default engine kit. Factory variation must preserve the audible musical laws:
// kick/tom are triangle pitch programs, cowbell rings without late bloom, open
// hat has a bounded noise tail, and clap remains a multi-burst hit.

#include "arpsid/patchbank/factory_sid808_kits.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* message, int slot = -1) {
    if (!condition) {
        std::cerr << "sid808_factory_musical_shape_v871_tests FAIL";
        if (slot >= 0) std::cerr << " slot=" << slot;
        std::cerr << ": " << message << "\n";
        std::exit(1);
    }
}

int msToSamples(double ms) {
    return static_cast<int>(std::lround(48000.0 * (ms / 1000.0)));
}

struct RenderedHit {
    std::vector<float> l;
    std::vector<float> r;
};

RenderedHit renderHit(ArpSID::Sid808Engine& engine,
                      ArpSID::Sid808Drum drum,
                      std::uint8_t midiNote,
                      int frames) {
    RenderedHit hit{};
    hit.l.assign((size_t)frames, 0.0f);
    hit.r.assign((size_t)frames, 0.0f);
    engine.allNotesOff();
    engine.noteOn(drum, 120u, midiNote);
    engine.processBlock(hit.l.data(), hit.r.data(), frames);
    return hit;
}

float rmsRegion(const RenderedHit& hit, int begin, int end) {
    begin = std::clamp(begin, 0, static_cast<int>(hit.l.size()));
    end = std::clamp(end, begin, static_cast<int>(hit.l.size()));
    double sum = 0.0;
    int count = 0;
    for (int i = begin; i < end; ++i) {
        require(std::isfinite(hit.l[(size_t)i]) && std::isfinite(hit.r[(size_t)i]),
                "render produced non-finite sample");
        const float mono = 0.5f * (hit.l[(size_t)i] + hit.r[(size_t)i]);
        sum += static_cast<double>(mono) * static_cast<double>(mono);
        ++count;
    }
    return count > 0 ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
}

std::uint8_t expectedScaledSustainRelease(std::uint8_t base,
                                          float releaseScale,
                                          int sustainDelta) {
    const int sustain = std::clamp<int>(((base >> 4) & 0x0F) + sustainDelta, 0, 15);
    const int release = std::clamp<int>(static_cast<int>(std::lround(
        static_cast<float>(base & 0x0F) * releaseScale)), 0, 15);
    return static_cast<std::uint8_t>((sustain << 4) | release);
}

std::uint16_t expectedScaledPulseWidth(std::uint16_t base,
                                       float ratio,
                                       std::uint16_t fallback) {
    std::uint16_t cleanBase = static_cast<std::uint16_t>(base & 0x0FFFu);
    if (cleanBase == 0u) cleanBase = static_cast<std::uint16_t>(fallback & 0x0FFFu);
    const int scaled = static_cast<int>(std::lround(static_cast<float>(cleanBase) * ratio));
    return static_cast<std::uint16_t>(std::clamp(scaled, 0, 0x0FFF));
}

void testFactorySlot(int slot) {
    using namespace ArpSID;

    Sid808Engine kick;
    kick.prepare(48000.0);
    require(applyFactorySid808Kit(slot, kick), "factory SID808 kit must apply", slot);
    const Sid808VoiceConfig kickCfg = kick.drumVoiceConfig(Sid808Drum::Kick);
    require(kickCfg.waveform == 0x10u, "factory kick source must be triangle", slot);
    require(kickCfg.pulseWidth == 0u, "factory kick must not carry pulse width", slot);
    (void)renderHit(kick, Sid808Drum::Kick, 36u, msToSamples(180.0));
    require(kick.microStageAppliedCount(Sid808Drum::Kick) >= 3u,
            "factory kick must apply all pitch-sweep stages", slot);
    require(kick.lastMicroStageWaveformForDrum(Sid808Drum::Kick) == 0x10u,
            "factory kick micro stages must stay triangle", slot);
    require(kick.lastMicroStageFreqForDrum(Sid808Drum::Kick) <
            kick.lastInitialFreqForDrum(Sid808Drum::Kick),
            "factory kick must sweep downward", slot);

    Sid808Engine cowbell;
    cowbell.prepare(48000.0);
    require(applyFactorySid808Kit(slot, cowbell), "factory SID808 kit must apply", slot);
    const Sid808VoiceConfig cowCfg = cowbell.drumVoiceConfig(Sid808Drum::Cowbell);
    const RenderedHit cow = renderHit(cowbell, Sid808Drum::Cowbell, 56u, msToSamples(650.0));
    const Sid808VoiceConfig cowTail = cowbell.lastMicroStageConfigForDrum(Sid808Drum::Cowbell);
    const float cowAttack = rmsRegion(cow, msToSamples(0.0), msToSamples(45.0));
    const float cowRing = rmsRegion(cow, msToSamples(100.0), msToSamples(260.0));
    const float cowLate = rmsRegion(cow, msToSamples(350.0), msToSamples(500.0));
    require(cowbell.microStageAppliedCount(Sid808Drum::Cowbell) >= 3u,
            "factory cowbell must alternate pulse partials", slot);
    require(cowbell.lastMicroStageWaveformForDrum(Sid808Drum::Cowbell) == 0x40u,
            "factory cowbell must remain pulse-based", slot);
    require(cowTail.sustainRelease == expectedScaledSustainRelease(cowCfg.sustainRelease, 1.15f, 4),
            "factory cowbell tail envelope must scale from the kit config", slot);
    require(cowTail.pulseWidth == expectedScaledPulseWidth(cowCfg.pulseWidth, 1.35f, 0x0880u),
            "factory cowbell tail pulse width must scale from the kit config", slot);
    require(cowAttack > 0.001f, "factory cowbell attack must be audible", slot);
    require(cowRing > cowAttack * 0.012f && cowRing > 0.00004f,
            "factory cowbell must keep a measurable ring", slot);
    require(cowLate <= std::max(cowAttack * 1.05f, cowRing * 1.20f),
            "factory cowbell late tail must not bloom louder than attack/ring", slot);

    Sid808Engine hat;
    hat.prepare(48000.0);
    require(applyFactorySid808Kit(slot, hat), "factory SID808 kit must apply", slot);
    const RenderedHit oh = renderHit(hat, Sid808Drum::OpenHat, 46u, msToSamples(560.0));
    const float hatEarly = rmsRegion(oh, msToSamples(0.0), msToSamples(45.0));
    const float hatRing = rmsRegion(oh, msToSamples(110.0), msToSamples(230.0));
    const float hatLate = rmsRegion(oh, msToSamples(360.0), msToSamples(520.0));
    require(hat.microStageAppliedCount(Sid808Drum::OpenHat) >= 3u,
            "factory open hat must apply staged ring", slot);
    require(hat.lastMicroStageWaveformForDrum(Sid808Drum::OpenHat) == 0x80u,
            "factory open hat stages must stay noise", slot);
    require(hatEarly > 0.001f, "factory open hat attack must be audible", slot);
    require(hatRing > hatEarly * 0.018f && hatRing > 0.00006f,
            "factory open hat must have a ring window", slot);
    require(hatLate <= std::max(hatEarly * 0.65f, hatRing * 1.35f),
            "factory open hat late tail must be bounded", slot);

    Sid808Engine clap;
    clap.prepare(48000.0);
    require(applyFactorySid808Kit(slot, clap), "factory SID808 kit must apply", slot);
    const Sid808VoiceConfig clapCfg = clap.drumVoiceConfig(Sid808Drum::Clap);
    const RenderedHit cl = renderHit(clap, Sid808Drum::Clap, 39u, msToSamples(150.0));
    const Sid808VoiceConfig clapTail = clap.lastMicroStageConfigForDrum(Sid808Drum::Clap);
    const float first = rmsRegion(cl, msToSamples(0.4), msToSamples(2.8));
    const float second = rmsRegion(cl, msToSamples(3.6), msToSamples(5.8));
    const float third = rmsRegion(cl, msToSamples(7.0), msToSamples(9.2));
    const float tail = rmsRegion(cl, msToSamples(12.0), msToSamples(32.0));
    require(clap.microStageAppliedCount(Sid808Drum::Clap) >= 3u,
            "factory clap must apply burst train", slot);
    require(clap.lastMicroStageRaisedGateForDrum(Sid808Drum::Clap),
            "factory clap burst stages must re-gate", slot);
    require(clapTail.sustainRelease == expectedScaledSustainRelease(clapCfg.sustainRelease, 2.00f, 4),
            "factory clap tail envelope must scale from the kit config", slot);
    require(first > 0.0007f, "factory clap first burst must be audible", slot);
    require(second > first * 0.06f, "factory clap second burst must be audible", slot);
    require(third > first * 0.04f, "factory clap third burst must be audible", slot);
    require(tail > first * 0.025f, "factory clap tail must outlive burst train", slot);
}

} // namespace

int main() {
    for (int slot = static_cast<int>(ArpSID::kSid808NewFactoryRange.first);
         slot <= static_cast<int>(ArpSID::kSid808NewFactoryRange.last);
         ++slot) {
        testFactorySlot(slot);
    }
    std::cout << "sid808_factory_musical_shape_v871_tests PASS\n";
    return 0;
}
