// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "arpsid/gui/kit_sequencer.h"
#include "arpsid/gui/kit_assign_config.h"
#include "arpsid/gui/kit_voice_config.h"
#include "arpsid/core/drum_context.h"
#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace ArpSID::GUI {

struct KitResolvedHit {
    std::uint8_t drumClass = 0u;
    std::uint8_t midiNote = 0u;
    std::uint8_t velocity = 0u;
    KitEngineTarget target = KitEngineTarget::DrSID;
    std::uint16_t selectedFactorySlot = 0u;

    // SID808/DrSID shared SID voice payload.
    std::uint8_t voiceWaveform = 0u;
    std::uint8_t voiceAttackDecay = 0u;
    std::uint8_t voiceSustainRelease = 0u;
    std::uint16_t voicePulseWidth = 0u;
    std::uint8_t voiceFlags = 0u;
    std::uint8_t voiceOverrideMask = 0u;

    // Dense one-shot Digi projection contract.
    std::uint8_t digiRuntimeSlot = 0u;
    std::uint8_t digiSourceSlotIndex = 0u;
    std::uint8_t digiActiveSlotCount = 1u;
    std::uint16_t digiAbsoluteFactorySlot = 0u;
    std::int8_t digiTuneShift = 0;
    std::uint8_t digiStartOffset = 0u;
    std::uint8_t digiLengthScale = 0u;
    std::uint8_t digiFlags = 0u;
};
static_assert(std::is_trivially_copyable<KitResolvedHit>::value,
              "KitResolvedHit must remain trivially copyable");

inline std::uint16_t kitEventSelectedFactorySlot(const CompiledKitEvent& ev) noexcept {
    return static_cast<std::uint16_t>(ev.selectedFactorySlotLo |
                                      (static_cast<std::uint16_t>(ev.selectedFactorySlotHi) << 8u));
}

inline KitEngineTarget kitResolvedTarget(const CompiledKitEvent& ev) noexcept {
    const auto t = static_cast<KitEngineTarget>(ev.engineTarget);
    switch (t) {
        case KitEngineTarget::DrSID:
        case KitEngineTarget::SID808:
        case KitEngineTarget::Digi:
            return t;
        default:
            return KitEngineTarget::DrSID;
    }
}

inline std::uint16_t kitAbsoluteSlotForTarget(KitEngineTarget target, std::uint8_t zeroBasedSlot) noexcept {
    switch (target) {
        case KitEngineTarget::DrSID:
            return static_cast<std::uint16_t>(kDrSidNewFactoryRange.first +
                std::min<std::uint8_t>(zeroBasedSlot, static_cast<std::uint8_t>(kKitDrSidSlotCount - 1u)));
        case KitEngineTarget::SID808:
            return static_cast<std::uint16_t>(kSid808NewFactoryRange.first +
                std::min<std::uint8_t>(zeroBasedSlot, static_cast<std::uint8_t>(kKitSid808SlotCount - 1u)));
        case KitEngineTarget::Digi:
            return static_cast<std::uint16_t>(kDigiNewFactoryRange.first +
                std::min<std::uint8_t>(zeroBasedSlot, static_cast<std::uint8_t>(kKitDigiSlotCount - 1u)));
        default:
            return 0u;
    }
}

inline bool kitResolvedSlotMatchesTarget(const KitResolvedHit& hit) noexcept {
    switch (hit.target) {
        case KitEngineTarget::DrSID:
            return ::ArpSID::isDrSidFactorySlot(static_cast<int>(hit.selectedFactorySlot));
        case KitEngineTarget::SID808:
            return ::ArpSID::isSid808FactorySlot(static_cast<int>(hit.selectedFactorySlot));
        case KitEngineTarget::Digi:
            return ::ArpSID::isDigiFactorySlot(static_cast<int>(hit.selectedFactorySlot));
        default:
            return false;
    }
}

inline KitResolvedHit resolveCompiledKitEvent(const CompiledKitEvent& ev,
                                              const KitAssignConfig* assignCfgOrNull = nullptr) noexcept {
    KitResolvedHit out{};
    out.drumClass = ev.drumClass;
    out.midiNote = ev.midiNote;
    out.velocity = ev.velocity;
    out.target = kitResolvedTarget(ev);
    out.selectedFactorySlot = kitEventSelectedFactorySlot(ev);

    out.voiceWaveform = static_cast<std::uint8_t>(ev.voiceWaveform & kKitVoiceWaveMask);
    out.voiceAttackDecay = ev.voiceAttackDecay;
    out.voiceSustainRelease = ev.voiceSustainRelease;
    out.voicePulseWidth = static_cast<std::uint16_t>((ev.voicePulseWidthLo |
        (static_cast<std::uint16_t>(ev.voicePulseWidthHi & 0x0Fu) << 8u)) & kKitVoicePWMax);
    out.voiceFlags = static_cast<std::uint8_t>(ev.voiceFlags & 0x07u);
    out.voiceOverrideMask = ev.voiceOverrideMask;

    // Dense Digi one-shot: runtime slot is always 0. The original source slot
    // remains identity/preset metadata, so scanning sparse inactive slots is
    // never required on render.
    const std::uint8_t sourceSlot = assignCfgOrNull
        ? std::min<std::uint8_t>(assignCfgOrNull->digiSlotIndex, static_cast<std::uint8_t>(kKitDigiSlotCount - 1u))
        : ev.digiSlotIndex;
    out.digiRuntimeSlot = 0u;
    out.digiSourceSlotIndex = std::min<std::uint8_t>(sourceSlot, static_cast<std::uint8_t>(kKitDigiSlotCount - 1u));
    out.digiActiveSlotCount = 1u;
    out.digiAbsoluteFactorySlot = kitAbsoluteSlotForTarget(KitEngineTarget::Digi, out.digiSourceSlotIndex);
    out.digiTuneShift = assignCfgOrNull ? kitAssignTuneShift(*assignCfgOrNull) : 0;
    out.digiStartOffset = assignCfgOrNull ? assignCfgOrNull->startOffset : 0u;
    out.digiLengthScale = assignCfgOrNull ? assignCfgOrNull->lengthScale : 0u;
    out.digiFlags = assignCfgOrNull ? static_cast<std::uint8_t>(assignCfgOrNull->flags & kKitAssignFlagMask) : 0u;

    return out;
}

} // namespace ArpSID::GUI
