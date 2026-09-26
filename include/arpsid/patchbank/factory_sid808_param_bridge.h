// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/patchbank/factory_sid808_kits.h"
#include <array>
#include <cstdint>

namespace ArpSID {

struct Sid808FactoryParamSignature {
    float volume = 0.0f;
    float kickTune = 0.0f;
    float kickDecay = 0.0f;
    float snareTone = 0.0f;
    float snareSnap = 0.0f;
    float hatTune = 0.0f;
    float hatDecay = 0.0f;
    float clapDecay = 0.0f;
    float cowbellTune = 0.0f;
    float cowbellDecay = 0.0f;
    float tomTune = 0.0f;
    float tomDecay = 0.0f;
    float accent = 0.0f;
    float drive = 0.0f;
    float hatMetal = 0.0f;
    float clapSpread = 0.0f;
};

constexpr float sid808NormFromU16_(std::uint16_t v, std::uint16_t maxv) noexcept {
    return maxv == 0 ? 0.0f : static_cast<float>(v) / static_cast<float>(maxv);
}
constexpr float sid808NormFromNibble_(std::uint8_t v) noexcept {
    return static_cast<float>(v & 0x0Fu) / 15.0f;
}
constexpr float sid808DecayFromSR_(std::uint8_t sr) noexcept {
    return sid808NormFromNibble_(static_cast<std::uint8_t>(sr & 0x0Fu));
}
constexpr float sid808ToneFromFreq_(std::uint16_t freq) noexcept {
    return sid808NormFromU16_(freq, 0x7000u) > 1.0f ? 1.0f : sid808NormFromU16_(freq, 0x7000u);
}
constexpr float sid808ToneFromWave_(std::uint8_t wave) noexcept {
    return (wave & 0x80u) ? 0.80f : ((wave & 0x40u) ? 0.55f : ((wave & 0x20u) ? 0.42f : 0.32f));
}

inline Sid808FactoryParamSignature factorySid808ParamSignatureForSlot(int slot) noexcept {
    Sid808FactoryParamSignature s{};
    const auto* kit = factorySid808KitForSlot(slot);
    if (!kit) return s;

    const Sid808KitConfigTable resolved = factorySid808ResolvedKitForSlot(slot);
    const auto cfg = [&](Sid808Drum d) noexcept -> Sid808VoiceConfig {
        return resolved[static_cast<std::size_t>(d)];
    };

    const Sid808VoiceConfig kick = cfg(Sid808Drum::Kick);
    const Sid808VoiceConfig snare = cfg(Sid808Drum::Snare);
    const Sid808VoiceConfig ch = cfg(Sid808Drum::ClosedHat);
    const Sid808VoiceConfig oh = cfg(Sid808Drum::OpenHat);
    const Sid808VoiceConfig clap = cfg(Sid808Drum::Clap);
    const Sid808VoiceConfig cow = cfg(Sid808Drum::Cowbell);
    const Sid808VoiceConfig tom = cfg(Sid808Drum::Tom);
    const Sid808VoiceConfig rim = cfg(Sid808Drum::Rim);

    s.volume = (kick.voiceLevel + snare.voiceLevel + ch.voiceLevel + oh.voiceLevel +
                clap.voiceLevel + cow.voiceLevel + tom.voiceLevel + rim.voiceLevel) / 8.0f;
    s.kickTune = sid808ToneFromFreq_(kick.freq);
    s.kickDecay = sid808DecayFromSR_(kick.sustainRelease);
    s.snareTone = sid808ToneFromFreq_(snare.freq);
    s.snareSnap = sid808DecayFromSR_(snare.sustainRelease);
    s.hatTune = sid808ToneFromFreq_(ch.freq);
    s.hatDecay = (sid808DecayFromSR_(ch.sustainRelease) + sid808DecayFromSR_(oh.sustainRelease)) * 0.5f;
    s.clapDecay = sid808DecayFromSR_(clap.sustainRelease);
    s.cowbellTune = sid808ToneFromFreq_(cow.freq);
    s.cowbellDecay = sid808DecayFromSR_(cow.sustainRelease);
    s.tomTune = sid808ToneFromFreq_(tom.freq);
    s.tomDecay = sid808DecayFromSR_(tom.sustainRelease);
    s.accent = kick.voiceLevel;
    s.drive = (kick.voiceLevel + snare.voiceLevel) * 0.15f;
    s.hatMetal = sid808ToneFromWave_(ch.waveform);
    s.clapSpread = (clap.voiceLevel + rim.voiceLevel) * 0.5f;
    return s;
}

} // namespace ArpSID
