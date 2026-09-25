#pragma once

#include "parameter_ids.h"
#include "arpsid/core/drum_context.h"
#include "arpsid/gui/digi_panel_model.h"
#include "arpsid/patchbank/factory_digi_kits.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ArpSID {

struct FactoryDigiParamSignature {
    int slot = 150;
    int index = 0;
    float volume = 0.78f;
    float tuneShiftNorm = 0.5f;
    float startOffsetNorm = 0.0f;
    float lengthScaleNorm = 1.0f;
    bool loop = false;
    bool reverse = false;
};

inline FactoryDigiParamSignature factoryDigiParamSignatureForSlot(int slot) noexcept {
    const int s = std::clamp(slot, static_cast<int>(kDigiNewFactoryRange.first), static_cast<int>(kDigiNewFactoryRange.last));
    const int idx = s - kDigiNewFactoryRange.first;
    FactoryDigiParamSignature sig{};
    sig.slot = s;
    sig.index = idx;
    sig.volume = std::clamp(0.68f + 0.02f * static_cast<float>(idx % 8), 0.0f, 1.0f);
    sig.tuneShiftNorm = std::clamp(0.5f + 0.02f * static_cast<float>((idx % 5) - 2), 0.0f, 1.0f);
    sig.startOffsetNorm = static_cast<float>((idx * 9) % 64) / 255.0f;
    sig.lengthScaleNorm = std::clamp(0.55f + 0.015f * static_cast<float>(idx % 16), 0.0f, 1.0f);
    sig.loop = (idx % 10) == 7;
    sig.reverse = (idx % 12) == 11;
    return sig;
}

inline constexpr std::uint8_t factoryDigiNormToByte(float v, std::uint8_t zeroMeansFull = 0u) noexcept {
    const float clamped = std::clamp(v, 0.0f, 1.0f);
    const int value = static_cast<int>(clamped * 255.0f + 0.5f);
    if (value <= 0) return zeroMeansFull;
    if (value > 255) return 255u;
    return static_cast<std::uint8_t>(value);
}

inline GUI::DigiPanelModel makeFactoryDigiPanelModelFromSignature(int slot) noexcept {
    const FactoryDigiParamSignature sig = factoryDigiParamSignatureForSlot(slot);
    GUI::DigiPanelModel model = makeFactoryDigiPanelModelForSlot(sig.slot);

    // Lane 0 mirrors the host-visible signature exactly, so tune/start/length
    // and flags are not dead metadata.
    GUI::DigiSampleSlot& s = model.slots[0];
    GUI::digiSetFactorySlot(s, static_cast<std::uint8_t>(sig.index));
    GUI::digiSetTuneShift(s, static_cast<int>((sig.tuneShiftNorm - 0.5f) * 24.0f));
    s.startOffset = factoryDigiNormToByte(sig.startOffsetNorm);
    s.lengthScale = factoryDigiNormToByte(sig.lengthScaleNorm, 0u);
    s.volume      = factoryDigiNormToByte(sig.volume);
    GUI::digiSetLoop(s, sig.loop);
    GUI::digiSetReverse(s, sig.reverse);

    return model;
}


template <typename ParamsArray>
inline void applyFactoryDigiDefaults(int slot, ParamsArray& params) noexcept {
    if (!isDigiFactorySlot(slot)) return;
    const FactoryDigiParamSignature sig = factoryDigiParamSignatureForSlot(slot);
    auto sp = [&](ParamID pid, float v) noexcept {
        params[static_cast<size_t>(pid)] = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    };
    // Digi factory identity: not SID-register synth and not DrSID/808.
    sp(kParamSynthModeEnable, 0.0f);
    sp(kParamDrSidEnable, 0.0f);
    sp(kParamDrSidMachineModel, 0.0f);
    sp(kParamSeqEnable, 1.0f);
    sp(kParamSeqMode, 0.75f);
    sp(kParamSeqTempo, 0.50f + 0.01f * static_cast<float>(sig.index % 10));
    sp(kParamSeqSwing, (sig.index % 3) == 0 ? 0.08f : 0.02f);
    sp(kParamSeqLength, 0.50f);
    sp(kParamMasterVolume, sig.volume);
    sp(kParamOutputLimiter, 1.0f);
    sp(kParamLimiterThreshold, 0.92f);
    sp(kParamLimiterAttack, 0.04f);
    sp(kParamLimiterRelease, 0.18f);
    // Full step/source payload is materialized by makeFactoryDigiPanelModelFromSignature().
    // This parameter bridge marks state identity and transport defaults while
    // the Digi payload bridge carries tune/start/length/loop/reverse/sample data.
}

} // namespace ArpSID
