// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <algorithm>
#include <cstdint>

#include "arpsid/core/psid_header.h"  // ArpSID::PsidSidModel

namespace ArpSID::C64 {

struct C64SidMixGains {
    float primary = 1.0f;
    float secondary = 0.0f;
};

// Keep the primary SID dominant while guaranteeing that the sum of all chip
// gains is exactly one. The former 1.0 + 0.5 secondary budget reached 1.5x
// full scale and forced multi-SID tunes into downstream hard clipping.
constexpr C64SidMixGains c64SidMixGains(std::uint8_t requestedChipCount,
                                        std::uint8_t maxRenderedChips = 5u) noexcept {
    const std::uint8_t chips = std::max<std::uint8_t>(
        1u, std::min<std::uint8_t>(requestedChipCount, maxRenderedChips));
    if (chips == 1u) return {};
    const float unit = 1.0f / static_cast<float>(chips + 1u);
    return {2.0f * unit, unit};
}

constexpr float c64SidMixGainSum(std::uint8_t requestedChipCount,
                                 std::uint8_t maxRenderedChips = 5u) noexcept {
    const std::uint8_t chips = std::max<std::uint8_t>(
        1u, std::min<std::uint8_t>(requestedChipCount, maxRenderedChips));
    const auto gains = c64SidMixGains(chips, maxRenderedChips);
    return gains.primary + gains.secondary * static_cast<float>(chips - 1u);
}

// PSID v2NG stores two-bit SID model hints in flags bits 4..9 for SID 1..3.
// Fourth/fifth SID extensions have no additional model fields, so they inherit
// the primary hint. 0/3 are ambiguous and should follow the user's selected
// global SID model instead of being silently forced to 8580.
constexpr std::uint8_t psidSidModelBitsForChip(std::uint16_t flags,
                                               std::uint8_t chip) noexcept {
    const std::uint8_t metadataChip = chip <= 2u ? chip : 0u;
    return static_cast<std::uint8_t>((flags >> (4u + metadataChip * 2u)) & 0x03u);
}

constexpr bool psidSidModelIsExplicit(std::uint8_t modelBits) noexcept {
    return modelBits == 1u || modelBits == 2u;
}

constexpr bool psidSidModelWants6581(std::uint8_t modelBits,
                                    bool fallbackIs6581) noexcept {
    return modelBits == 1u ? true : (modelBits == 2u ? false : fallbackIs6581);
}

// v873 audit item 5: resolve from the parser's NORMALIZED per-chip model (already carries
// SID1-inheritance for Unknown extra chips) instead of re-decoding raw flags in the render.
constexpr bool psidSidModelWants6581(ArpSID::PsidSidModel model,
                                     bool fallbackIs6581) noexcept {
    return model == ArpSID::PsidSidModel::MOS6581
               ? true
               : (model == ArpSID::PsidSidModel::MOS8580 ? false : fallbackIs6581);
}

} // namespace ArpSID::C64
