// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/engines/sid808_gm_projection.h"
#include "arpsid/patchbank/factory_sid808_kits.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "Sid808GMProjectionV873Tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace ArpSID;

    const Sid808VoiceConfig base{0x2400u, 0x0123u, 0x10u, 0x25u, 0x77u, 0u, 0.75f};
    const SidGMDrumNoteSpec lowTom = sidGMDrumSpecForNote(41u);
    Sid808HitOverride none{};

    const Sid808HitOverride projected = sid808GMNoteOverride(lowTom, base, none);
    const float lowTomRatio = std::pow(2.0f,
        (lowTom.tuneOffsetNorm * kSid808GMTuneSemitoneRange) / 12.0f);
    const auto expectedFreq = static_cast<std::uint16_t>(
        std::clamp<long>(std::lround(static_cast<float>(base.freq) * lowTomRatio), 1, 65535));

    require(projected.hasFreq, "GM tuneOffsetNorm must project to override frequency");
    require(projected.freq == expectedFreq, "GM projected frequency must scale from the supplied base config");
    require(projected.hasAttackDecay, "GM decayScale must project to override attack/decay");
    require(projected.attackDecay == 0x26u, "GM projected decay must preserve attack nibble and scale decay nibble");

    Sid808HitOverride user{};
    user.freq = 0x3333u;
    user.attackDecay = 0xA1u;
    user.hasFreq = true;
    user.hasAttackDecay = true;
    const Sid808HitOverride preserved = sid808GMNoteOverride(lowTom, base, user);
    require(preserved.freq == user.freq && preserved.hasFreq,
            "user frequency override must win over GM projection");
    require(preserved.attackDecay == user.attackDecay && preserved.hasAttackDecay,
            "user attack/decay override must win over GM projection");

    require(sid808GMScaledVelocity(80u, sidGMDrumSpecForNote(69u).velocityScale) == 62u,
            "GM velocityScale must be applied with MIDI-safe rounding");

    DrumEngineHostBridge bridge;
    bridge.prepare(48000.0);
    require(bridge.loadFactorySlot(120), "test bridge must load SID808 factory slot");

    bridge.noteOn(SidGMDrumClass::ClosedHat, 80u, 69u);
    const float expectedVel = 62.0f / 127.0f;
    require(std::fabs(bridge.sid808Engine().lastVelocity() - expectedVel) < 0.0001f,
            "router must apply GM velocityScale for direct SID808 MIDI hits");

    Sid808HitOverride kitStyle{};
    kitStyle.selectedFactorySlot = 120u;
    kitStyle.hasSelectedFactorySlot = true;
    bridge.noteOnAtWithOverride(4, SidGMDrumClass::ClosedHat, 80u, 69u, kitStyle, true);
    float l[16]{};
    float r[16]{};
    bridge.processBlock(l, r, 16);
    require(std::fabs(bridge.sid808Engine().lastVelocity() - expectedVel) < 0.0001f,
            "router must apply GM velocityScale for scheduled KIT-style SID808 hits");

    const Sid808VoiceConfig tomBase = bridge.sid808Engine().compensatedDrumVoiceConfig(Sid808Drum::Tom);
    const SidGMDrumNoteSpec timbale = sidGMDrumSpecForNote(65u);
    const Sid808HitOverride expectedTom = sid808GMNoteOverride(timbale, tomBase, none);
    bridge.noteOn(SidGMDrumClass::Tom, 90u, 65u);
    const Sid808VoiceConfig appliedTom = bridge.sid808Engine().lastAppliedConfig();
    require(bridge.sid808Engine().lastPercProfile() == Sid808PercProfile::TimbaleHigh,
            "router must select the GM timbale profile for note 65");
    require(appliedTom.freq == expectedTom.freq,
            "router must fold GM profile/tune projection into the bounded SID808 base frequency");
    require(appliedTom.attackDecay == expectedTom.attackDecay,
            "router must fold GM decayScale into SID808 attack/decay before staging");

    Sid808HitOverride ov{};
    ov.freq = 0x2222u;
    ov.attackDecay = 0xA7u;
    ov.hasFreq = true;
    ov.hasAttackDecay = true;
    bridge.noteOnWithOverride(SidGMDrumClass::Tom, 90u, 65u, ov);
    const Sid808VoiceConfig userApplied = bridge.sid808Engine().lastAppliedConfig();
    require(userApplied.freq == ov.freq,
            "router must preserve user frequency override after GM projection");
    require(userApplied.attackDecay == ov.attackDecay,
            "router must preserve user attack/decay override after GM projection");

    std::cout << "Sid808GMProjectionV873Tests PASS\n";
    return 0;
}
