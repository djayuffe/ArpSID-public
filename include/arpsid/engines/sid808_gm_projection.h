// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/core/sid_gm_drum_kit.h"
#include "arpsid/engines/sid808_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ArpSID {

inline constexpr float kSid808GMTuneSemitoneRange = 4.0f;

inline bool sid808GMSpecMatchesClass(const SidGMDrumNoteSpec& spec,
                                     SidGMDrumClass drumClass) noexcept {
    return spec.drumClass != SidGMDrumClass::Unsupported &&
           spec.drumClass == drumClass;
}

inline SidGMDrumNoteSpec sid808GMProjectionSpecForNote(std::uint8_t gmNote,
                                                       SidGMDrumClass drumClass) noexcept {
    const SidGMDrumNoteSpec spec = sidGMDrumSpecForNote(gmNote);
    return sid808GMSpecMatchesClass(spec, drumClass)
        ? spec
        : sidGMUnsupportedDrumSpec(gmNote);
}

inline Sid808PercProfile sid808GMProfileForNote(std::uint8_t gmNote) noexcept {
    switch (gmNote) {
        case 41: return Sid808PercProfile::TomLow;
        case 43: return Sid808PercProfile::TomLow;
        case 45: return Sid808PercProfile::TomMid;
        case 47: return Sid808PercProfile::TomMid;
        case 48: return Sid808PercProfile::TomHigh;
        case 50: return Sid808PercProfile::TomHigh;
        case 44: return Sid808PercProfile::PedalHat;
        case 54: return Sid808PercProfile::Tambourine;
        case 53: return Sid808PercProfile::RideBell;
        case 58: return Sid808PercProfile::Vibraslap;
        case 67: return Sid808PercProfile::AgogoHigh;
        case 68: return Sid808PercProfile::AgogoLow;
        case 75: return Sid808PercProfile::Claves;
        case 76: return Sid808PercProfile::WoodBlockHigh;
        case 77: return Sid808PercProfile::WoodBlockLow;
        case 60: return Sid808PercProfile::BongoHigh;
        case 61: return Sid808PercProfile::BongoLow;
        case 62: return Sid808PercProfile::CongaMute;
        case 63: return Sid808PercProfile::CongaOpen;
        case 64: return Sid808PercProfile::CongaLow;
        case 65: return Sid808PercProfile::TimbaleHigh;
        case 66: return Sid808PercProfile::TimbaleLow;
        case 69: return Sid808PercProfile::Shaker;
        case 70: return Sid808PercProfile::Shaker;
        case 73: return Sid808PercProfile::GuiroShort;
        case 78: return Sid808PercProfile::CuicaMute;
        case 79: return Sid808PercProfile::CuicaOpen;
        case 46: return Sid808PercProfile::OpenHat;
        case 49: return Sid808PercProfile::Crash;
        case 51: return Sid808PercProfile::Ride;
        case 52: return Sid808PercProfile::China;
        case 55: return Sid808PercProfile::Splash;
        case 57: return Sid808PercProfile::Crash;
        case 59: return Sid808PercProfile::Ride;
        case 71: return Sid808PercProfile::WhistleShort;
        case 72: return Sid808PercProfile::WhistleLong;
        case 74: return Sid808PercProfile::GuiroLong;
        case 80: return Sid808PercProfile::TriangleMute;
        case 81: return Sid808PercProfile::Triangle;
        default: return Sid808PercProfile::Default;
    }
}

inline float sid808GMProfileFrequencyRatio(Sid808PercProfile profile) noexcept {
    switch (profile) {
        case Sid808PercProfile::BongoHigh:    return 1.62f;
        case Sid808PercProfile::BongoLow:     return 1.34f;
        case Sid808PercProfile::CongaMute:    return 1.42f;
        case Sid808PercProfile::CongaOpen:    return 1.26f;
        case Sid808PercProfile::CongaLow:     return 1.04f;
        case Sid808PercProfile::TimbaleHigh:  return 1.84f;
        case Sid808PercProfile::TimbaleLow:   return 1.58f;
        case Sid808PercProfile::CuicaMute:    return 1.92f;
        case Sid808PercProfile::CuicaOpen:    return 1.76f;
        case Sid808PercProfile::Crash:        return 0.96f;
        case Sid808PercProfile::Ride:         return 0.78f;
        case Sid808PercProfile::Splash:       return 1.08f;
        case Sid808PercProfile::China:        return 1.18f;
        case Sid808PercProfile::Triangle:     return 0.72f;
        case Sid808PercProfile::WhistleShort: return 0.70f;
        case Sid808PercProfile::WhistleLong:  return 0.82f;
        case Sid808PercProfile::GuiroShort:   return 0.74f;
        case Sid808PercProfile::GuiroLong:    return 0.68f;
        case Sid808PercProfile::Shaker:       return 0.92f;
        case Sid808PercProfile::Tambourine:   return 1.06f;
        case Sid808PercProfile::PedalHat:     return 0.86f;
        case Sid808PercProfile::RideBell:     return 1.54f;
        case Sid808PercProfile::AgogoHigh:    return 1.92f;
        case Sid808PercProfile::AgogoLow:     return 1.60f;
        case Sid808PercProfile::TriangleMute: return 0.70f;
        case Sid808PercProfile::Vibraslap:    return 1.12f;
        case Sid808PercProfile::Claves:       return 1.38f;
        case Sid808PercProfile::WoodBlockHigh: return 1.46f;
        case Sid808PercProfile::WoodBlockLow:  return 1.16f;
        default:                              return 1.0f;
    }
}

inline Sid808HitOverride sid808GMNoteOverride(const SidGMDrumNoteSpec& spec,
                                              const Sid808VoiceConfig& base,
                                              const Sid808HitOverride& userOverride) noexcept {
    Sid808HitOverride out = userOverride;
    // A note/class mismatch (sid808GMProjectionSpecForNote) resolves to an
    // Unsupported spec that still carries the original note number. Without this
    // guard, sid808GMProfileForNote(spec.note) below would project that note's
    // profile/frequency onto whatever drum family the caller actually selected —
    // e.g. noteOn(ClosedHat, note=49) would inject a Crash profile. Return the
    // user's override unchanged instead (v873 GM class-mismatch guard).
    if (spec.drumClass == SidGMDrumClass::Unsupported) {
        return out;
    }
    const Sid808PercProfile profile = sid808GMProfileForNote(spec.note);

    if (!out.hasProfile && profile != Sid808PercProfile::Default) {
        out.profile = profile;
        out.hasProfile = true;
    }

    const float profileRatio = sid808GMProfileFrequencyRatio(profile);
    if (!out.hasFreq && base.freq != 0u &&
        (spec.tuneOffsetNorm != 0.0f || profileRatio != 1.0f)) {
        const float semis =
            std::clamp(spec.tuneOffsetNorm, -1.0f, 1.0f) * kSid808GMTuneSemitoneRange;
        const float ratio = profileRatio * std::pow(2.0f, semis / 12.0f);
        const long f = std::lround(static_cast<float>(base.freq) * ratio);
        out.freq = static_cast<std::uint16_t>(std::clamp<long>(f, 1, 65535));
        out.hasFreq = true;
    }

    if (!out.hasAttackDecay && spec.decayScale != 1.0f) {
        const std::uint8_t attackN =
            static_cast<std::uint8_t>((base.attackDecay >> 4) & 0x0Fu);
        const std::uint8_t decayN = static_cast<std::uint8_t>(base.attackDecay & 0x0Fu);
        const long scaled =
            std::lround(static_cast<float>(decayN) *
                        std::clamp(spec.decayScale, 0.0f, 4.0f));
        const std::uint8_t newDecayN =
            static_cast<std::uint8_t>(std::clamp<long>(scaled, 0, 15));
        out.attackDecay = static_cast<std::uint8_t>((attackN << 4) | newDecayN);
        out.hasAttackDecay = true;
    }

    // Many SID808 hats/cymbals carry a zero decay nibble in attackDecay, so scaling
    // that nibble alone leaves the audible tail identical between e.g. Crash 1
    // (decayScale 1.70) and Crash 2 (1.62). Project decayScale into the release
    // nibble of sustainRelease as well so the perceived tail length actually tracks
    // the GM decay figure (v873 GM decayScale→release).
    if (!out.hasSustainRelease && spec.decayScale != 1.0f) {
        const std::uint8_t sustainN =
            static_cast<std::uint8_t>((base.sustainRelease >> 4) & 0x0Fu);
        const std::uint8_t releaseN = static_cast<std::uint8_t>(base.sustainRelease & 0x0Fu);
        const long scaled =
            std::lround(static_cast<float>(releaseN) *
                        std::clamp(spec.decayScale, 0.0f, 4.0f));
        const std::uint8_t newReleaseN =
            static_cast<std::uint8_t>(std::clamp<long>(scaled, 0, 15));
        out.sustainRelease = static_cast<std::uint8_t>((sustainN << 4) | newReleaseN);
        out.hasSustainRelease = true;
    }

    return out;
}

inline std::uint8_t sid808GMScaledVelocity(std::uint8_t velocity,
                                           float velocityScale) noexcept {
    if (velocity == 0u) return 0u;
    const int v = static_cast<int>(
        std::lround(static_cast<float>(velocity) *
                    std::clamp(velocityScale, 0.0f, 4.0f)));
    return static_cast<std::uint8_t>(std::clamp(v, 1, 127));
}

} // namespace ArpSID
