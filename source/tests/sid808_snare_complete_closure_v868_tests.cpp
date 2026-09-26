// Copyright (C) 2024-2026 Ulf Bertilsson
// sid808_snare_complete_closure_v868_tests.cpp
//
// v868 guards the remaining SID808 snare-audio audit:
// - SID808 reserves a physical voice without first gating the default melodic voice;
// - snare hits cannot be converted back into pulse-only/sustained notes;
// - every factory SID808 snare opts into filter shaping;
// - the live voice starts as a noise snap and switches to the authored body.

#include "arpsid/engines/sid808_engine.h"
#include "arpsid/patchbank/factory_sid808_kits.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "sid808_snare_complete_closure_v868_tests FAIL: "
                  << message << "\n";
        std::exit(1);
    }
}

std::string readSourceFile(const char* relativePath) {
#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif
    std::ifstream in(std::string(ARPSID_SOURCE_DIR) + "/" + relativePath);
    require(in.good(), "source file must be readable");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

void testSourceOwnershipAndSchedulingGuards() {
    const std::string singleSid =
        readSourceFile("include/arpsid/engines/single_sid_three_voice_engine.h");
    const std::string sid808 =
        readSourceFile("include/arpsid/engines/sid808_engine.h");

    require(contains(singleSid, "forceNoteOnVoiceBookkeepingOnly"),
            "single-SID engine must expose allocator-only forced voice ownership");
    require(contains(sid808, "forceNoteOnVoiceBookkeepingOnly(fixedVoice, token, velocity, choke)"),
            "SID808 must reserve the fixed voice without default melodic gate programming");
    require(!contains(sid808, "sidEngine_.forceNoteOnVoice(fixedVoice, token, velocity, choke);"),
            "SID808 must not route hits through forceNoteOnVoice default-gate path");
    require(contains(sid808, "sid808SnareSnapConfig_"),
            "SID808 must keep a dedicated snare noise-snap program");
    require(contains(sid808, "armSnareBodyStage_"),
            "SID808 must schedule a snare body stage after the snap");
    require(contains(sid808, "nextScheduledEventChunk_"),
            "SID808 block rendering must chunk at micro-stage and release boundaries");
    require(contains(sid808, "FilterMode::BpHp"),
            "SID808 snare filter shape must use high-pass/band-pass shaping");
}

void testFactorySnareFlagsAndRuntimeSanitizer() {
    using namespace ArpSID;
    for (int slot = 120; slot <= 149; ++slot) {
        const Sid808KitConfigTable kit = factorySid808ResolvedKitForSlot(slot);
        const Sid808VoiceConfig snare = kit[static_cast<std::size_t>(Sid808Drum::Snare)];
        require((sid808NormalizeWaveformControl(snare.waveform) & 0x80u) != 0u,
                "factory snare must include the noise bit");
        require(((snare.sustainRelease >> 4u) & 0x0Fu) == 0u,
                "factory snare must keep zero sustain after slot variation");
        require((snare.flags & Sid808Detail::kFlagFilter) != 0u,
                "factory snare must opt into SID filter shaping");
    }

    Sid808Engine engine;
    engine.prepare(48000.0);

    Sid808HitOverride pulseOnly{};
    pulseOnly.waveform = 0x40u;
    pulseOnly.sustainRelease = 0xB7u;
    pulseOnly.flags = 0u;
    pulseOnly.hasWaveform = true;
    pulseOnly.hasSustainRelease = true;
    pulseOnly.hasFlags = true;

    engine.noteOnWithOverride(Sid808Drum::Snare, 112u, 38u, pulseOnly);
    const Sid808VoiceConfig applied = engine.lastAppliedConfig();
    require((applied.waveform & 0x80u) != 0u,
            "runtime snare sanitizer must restore noise when an override is pulse-only");
    require(((applied.sustainRelease >> 4u) & 0x0Fu) == 0u,
            "runtime snare sanitizer must zero the sustain nibble");
    require((applied.flags & Sid808Detail::kFlagFilter) != 0u,
            "runtime snare sanitizer must route snare through the filter");
}

void testSnareSnapThenBodyRegisters() {
    using namespace ArpSID;
    Sid808Engine engine;
    engine.prepare(48000.0);
    engine.noteOn(Sid808Drum::Snare, 120u, 38u);

    const auto snap = engine.chip().getVoice(1).snapshot();
    require(snap.waveform == static_cast<std::uint8_t>(Waveform::Noise),
            "snare first program must be noise-only snap");
    require(snap.frequency >= 0x5800u,
            "snare noise snap must use high-frequency register drive");
    require(snap.pulseWidth == 0u,
            "snare noise snap must not carry pulse width");
    require(snap.gate,
            "snare snap must own the gate after SID808 programming");

    std::array<float, 400> l{};
    std::array<float, 400> r{};
    engine.processBlock(l.data(), r.data(), static_cast<int>(l.size()));

    const auto body = engine.chip().getVoice(1).snapshot();
    const std::uint8_t pulseNoise =
        static_cast<std::uint8_t>(static_cast<std::uint8_t>(Waveform::Pulse) |
                                  static_cast<std::uint8_t>(Waveform::Noise));
    require(body.waveform == pulseNoise,
            "snare body stage must restore pulse+noise body after snap");
    require(body.frequency == 0x3000u,
            "snare body stage must restore authored body frequency");
    require(body.pulseWidth == 800u,
            "snare body stage must restore authored body pulse width");
    require(body.gate,
            "snare body stage must preserve the original one-shot gate");
}

} // namespace

int main() {
    testSourceOwnershipAndSchedulingGuards();
    testFactorySnareFlagsAndRuntimeSanitizer();
    testSnareSnapThenBodyRegisters();
    std::cout << "sid808_snare_complete_closure_v868_tests PASS\n";
    return 0;
}
