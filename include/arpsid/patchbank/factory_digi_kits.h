// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/core/drum_context.h"
#include "arpsid/gui/digi_panel_model.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace ArpSID {

struct FactoryDigiKitPayload {
    int slot = 150;
    int index = 0;
    GUI::DigiPanelModel model{};
    std::uint32_t payloadHash = 0u;
};

inline constexpr std::uint32_t digiPayloadHashStep(std::uint32_t h, std::uint32_t v) noexcept {
    return (h ^ (v + 0x9E3779B9u + (h << 6) + (h >> 2))) * 16777619u;
}

inline constexpr std::uint32_t factoryDigiPayloadHash(const GUI::DigiPanelModel& m) noexcept {
    std::uint32_t h = 2166136261u;
    h = digiPayloadHashStep(h, m.schemaVersion);
    h = digiPayloadHashStep(h, m.activeSlot);
    for (std::uint8_t s = 0; s < GUI::kDigiActiveSlotCount; ++s) {
        const GUI::DigiSampleSlot& slot = m.slots[s];
        h = digiPayloadHashStep(h, static_cast<std::uint8_t>(slot.sourceType));
        h = digiPayloadHashStep(h, slot.factorySlotIndex);
        h = digiPayloadHashStep(h, slot.tuneShiftBias);
        h = digiPayloadHashStep(h, slot.startOffset);
        h = digiPayloadHashStep(h, slot.lengthScale);
        h = digiPayloadHashStep(h, slot.volume);
        h = digiPayloadHashStep(h, slot.flags);
        for (std::uint8_t step = 0; step < GUI::kDigiStepCount; ++step) {
            h = digiPayloadHashStep(h, m.steps[s][step]);
        }
    }
    return h;
}

inline constexpr int factoryDigiIndexForSlot(int slot) noexcept {
    return std::clamp(slot,
                      static_cast<int>(kDigiNewFactoryRange.first),
                      static_cast<int>(kDigiNewFactoryRange.last)) -
           static_cast<int>(kDigiNewFactoryRange.first);
}

inline constexpr GUI::DigiPanelModel makeFactoryDigiPanelModelForSlot(int slot) noexcept {
    const int idx = factoryDigiIndexForSlot(slot);
    GUI::DigiPanelModel m = GUI::makeDefaultDigiPanelModel();
    m.activeSlot = 0u;

    // Eight playable lanes are intentionally authored, not left silent.
    for (std::uint8_t lane = 0; lane < GUI::kDigiActiveSlotCount; ++lane) {
        GUI::DigiSampleSlot& s = m.slots[lane];
        const std::uint8_t factoryIdx =
            static_cast<std::uint8_t>((idx + lane * 3) % GUI::kKitDigiSlotCount);
        GUI::digiSetFactorySlot(s, factoryIdx);

        const int semis = ((idx + static_cast<int>(lane)) % 7) - 3;
        GUI::digiSetTuneShift(s, semis);
        s.startOffset = static_cast<std::uint8_t>((idx * 11 + lane * 17) & 0xFF);
        s.lengthScale = static_cast<std::uint8_t>(96u + ((idx * 5 + lane * 9) & 0x7F));
        s.volume      = static_cast<std::uint8_t>(176u + ((idx + lane * 13) & 0x3F));
        GUI::digiSetLoop(s, ((idx + lane) % 11) == 0);
        GUI::digiSetReverse(s, ((idx + lane) % 13) == 0);

        // Deterministic 32-step pattern. Every lane has at least two active
        // hits, so factory Digi slots are audible after projection.
        const std::uint8_t a = static_cast<std::uint8_t>((idx + lane * 4) % GUI::kDigiStepCount);
        const std::uint8_t b = static_cast<std::uint8_t>((a + 8 + (idx % 7)) % GUI::kDigiStepCount);
        const std::uint8_t c = static_cast<std::uint8_t>((a + 16 + (lane % 5)) % GUI::kDigiStepCount);
        GUI::digiStepSetActive(m, lane, a, static_cast<std::uint8_t>(96u + ((idx + lane) & 0x1F)));
        GUI::digiStepSetActive(m, lane, b, static_cast<std::uint8_t>(80u + ((idx * 3 + lane) & 0x2F)));
        if ((idx + lane) % 2 == 0) {
            GUI::digiStepSetActive(m, lane, c, static_cast<std::uint8_t>(64u + ((idx + lane * 5) & 0x3F)));
        }
    }

    return m;
}

inline constexpr FactoryDigiKitPayload makeFactoryDigiKitPayloadForSlot(int slot) noexcept {
    FactoryDigiKitPayload p{};
    p.slot = std::clamp(slot,
                        static_cast<int>(kDigiNewFactoryRange.first),
                        static_cast<int>(kDigiNewFactoryRange.last));
    p.index = factoryDigiIndexForSlot(p.slot);
    p.model = makeFactoryDigiPanelModelForSlot(p.slot);
    p.payloadHash = factoryDigiPayloadHash(p.model);
    return p;
}

inline constexpr bool factoryDigiPayloadIsWellFormed(const FactoryDigiKitPayload& p) noexcept {
    if (p.slot < static_cast<int>(kDigiNewFactoryRange.first) ||
        p.slot > static_cast<int>(kDigiNewFactoryRange.last)) return false;
    if (p.index != p.slot - static_cast<int>(kDigiNewFactoryRange.first)) return false;
    if (!GUI::digiPanelIsWellFormed(p.model)) return false;
    bool anyActive = false;
    for (std::uint8_t lane = 0; lane < GUI::kDigiActiveSlotCount; ++lane) {
        if (p.model.slots[lane].sourceType != GUI::DigiSourceType::FactorySlot) return false;
        for (std::uint8_t step = 0; step < GUI::kDigiStepCount; ++step) {
            anyActive = anyActive || GUI::digiStepIsActive(p.model, lane, step);
        }
    }
    return anyActive && p.payloadHash != 0u;
}

static_assert(factoryDigiPayloadIsWellFormed(makeFactoryDigiKitPayloadForSlot(150)),
              "Factory Digi slot 150 must have an audible payload");
static_assert(factoryDigiPayloadIsWellFormed(makeFactoryDigiKitPayloadForSlot(179)),
              "Factory Digi slot 179 must have an audible payload");

} // namespace ArpSID
