#pragma once

#include "arpsid/core/drsid_instrument_program.h"
#include "arpsid/core/drum_context.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace ArpSID {

struct FactoryDrSidKitPayload {
    int slot = 80;
    int index = 0;
    Drsid::DrSidInstrumentProgram program{};
    std::uint32_t behaviorHash = 0u;
};

inline constexpr std::uint32_t drsidPayloadHashStep(std::uint32_t h, std::uint32_t v) noexcept {
    return (h ^ (v + 0x7F4A7C15u + (h << 6) + (h >> 2))) * 16777619u;
}

inline constexpr std::uint32_t factoryDrSidProgramHash(const Drsid::DrSidInstrumentProgram& p) noexcept {
    std::uint32_t h = 2166136261u;
    h = drsidPayloadHashStep(h, static_cast<std::uint8_t>(p.drumClass));
    h = drsidPayloadHashStep(h, static_cast<std::uint8_t>(p.chokeGroup));
    h = drsidPayloadHashStep(h, static_cast<std::uint8_t>(p.voicePolicy));
    h = drsidPayloadHashStep(h, p.stepCount);
    for (std::uint8_t i = 0; i < p.stepCount; ++i) {
        const auto& s = p.steps[i];
        h = drsidPayloadHashStep(h, s.cycleOffset);
        h = drsidPayloadHashStep(h, s.durationCycles);
        h = drsidPayloadHashStep(h, s.freq);
        h = drsidPayloadHashStep(h, s.pulseWidth);
        h = drsidPayloadHashStep(h, s.waveform);
        h = drsidPayloadHashStep(h, s.attackDecay);
        h = drsidPayloadHashStep(h, s.sustainRelease);
        h = drsidPayloadHashStep(h, s.filterCutoffLo);
        h = drsidPayloadHashStep(h, s.filterCutoffHi);
        h = drsidPayloadHashStep(h, s.filterResRoute);
        h = drsidPayloadHashStep(h, s.modeVolume);
        h = drsidPayloadHashStep(h, s.flags);
    }
    return h;
}

inline constexpr int factoryDrSidIndexForSlot(int slot) noexcept {
    return std::clamp(slot,
                      static_cast<int>(kDrSidNewFactoryRange.first),
                      static_cast<int>(kDrSidNewFactoryRange.last)) -
           static_cast<int>(kDrSidNewFactoryRange.first);
}

inline constexpr Drsid::DrSidInstrumentProgram makeFactoryDrSidProgramForSlot(int slot) noexcept {
    const int idx = factoryDrSidIndexForSlot(slot);
    const int family = idx / 5;
    const int variant = idx % 5;
    const SidGMDrumClass classes[8] = {
        SidGMDrumClass::Kick, SidGMDrumClass::Snare, SidGMDrumClass::ClosedHat,
        SidGMDrumClass::OpenHat, SidGMDrumClass::Clap, SidGMDrumClass::Cowbell,
        SidGMDrumClass::Tom, SidGMDrumClass::Rim
    };
    Drsid::DrSidInstrumentProgram p = Drsid::makeCanonicalDrSidProgram(classes[family & 7]);

    // Author deterministic per-slot behavior differences into the actual
    // register microprogram, not only into display names.
    for (std::uint8_t i = 0; i < p.stepCount; ++i) {
        auto& s = p.steps[i];
        const std::uint16_t freqDelta = static_cast<std::uint16_t>((variant + 1) * (family + 3) * (i + 1) * 7);
        s.freq = static_cast<std::uint16_t>(s.freq + freqDelta);
        s.durationCycles = static_cast<std::uint16_t>(s.durationCycles + (variant * 43u) + (family * 17u));
        if ((variant & 1) != 0) {
            s.pulseWidth = static_cast<std::uint16_t>((s.pulseWidth + 0x0040u + family * 0x0010u) & 0x0FFFu);
        }
        if ((variant & 2) != 0) {
            s.modeVolume = static_cast<std::uint8_t>((s.modeVolume & 0xF0u) | ((0x0Au + family + i) & 0x0Fu));
        }
        if (i + 1 == p.stepCount) {
            s.flags |= Drsid::StepFlag::kIsTerminalStep;
        }
    }

    p.fingerprint.registerTraceHash = factoryDrSidProgramHash(p);
    p.fingerprint.expectedStepCount = p.stepCount;
    p.fingerprint.firstStepWord =
        (static_cast<std::uint64_t>(p.steps[0].cycleOffset) << 48) |
        (static_cast<std::uint64_t>(p.steps[0].freq) << 16) |
        static_cast<std::uint64_t>(p.steps[0].waveform);

    return p;
}

inline constexpr FactoryDrSidKitPayload makeFactoryDrSidKitPayloadForSlot(int slot) noexcept {
    FactoryDrSidKitPayload p{};
    p.slot = std::clamp(slot,
                        static_cast<int>(kDrSidNewFactoryRange.first),
                        static_cast<int>(kDrSidNewFactoryRange.last));
    p.index = factoryDrSidIndexForSlot(p.slot);
    p.program = makeFactoryDrSidProgramForSlot(p.slot);
    p.behaviorHash = factoryDrSidProgramHash(p.program);
    return p;
}

inline constexpr bool factoryDrSidPayloadIsWellFormed(const FactoryDrSidKitPayload& p) noexcept {
    if (p.slot < static_cast<int>(kDrSidNewFactoryRange.first) ||
        p.slot > static_cast<int>(kDrSidNewFactoryRange.last)) return false;
    if (p.index != p.slot - static_cast<int>(kDrSidNewFactoryRange.first)) return false;
    if (!Drsid::programIsWellFormed(p.program)) return false;
    return p.behaviorHash != 0u &&
           p.program.fingerprint.registerTraceHash == p.behaviorHash &&
           p.program.fingerprint.expectedStepCount == p.program.stepCount;
}

static_assert(factoryDrSidPayloadIsWellFormed(makeFactoryDrSidKitPayloadForSlot(80)),
              "Factory DrSID slot 80 must have real microprogram payload");
static_assert(factoryDrSidPayloadIsWellFormed(makeFactoryDrSidKitPayloadForSlot(119)),
              "Factory DrSID slot 119 must have real microprogram payload");
static_assert(makeFactoryDrSidKitPayloadForSlot(80).behaviorHash !=
              makeFactoryDrSidKitPayloadForSlot(88).behaviorHash,
              "DrSID factory slots must be behaviorally distinct, not just renamed");

} // namespace ArpSID
