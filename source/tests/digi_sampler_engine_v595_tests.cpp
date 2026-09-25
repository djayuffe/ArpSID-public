// SPDX-License-Identifier: BSD-3-Clause
// v595 - DIGI sampler render/telemetry contract tests.

#include "arpsid/engines/digi_sampler_engine.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

using namespace ArpSID;
using namespace ArpSID::GUI;

static float maxAbs(const std::array<float, 256>& a) {
    float m = 0.0f;
    for (float v : a) m = std::max(m, std::fabs(v));
    return m;
}

static bool hasNonZeroScope(const DigiSamplerEngine& e) {
    float scope[DigiSamplerEngine::kScopeLen]{};
    e.copyScope(scope, DigiSamplerEngine::kScopeLen);
    for (float v : scope) {
        if (std::fabs(v) > 1.0e-5f) return true;
    }
    return false;
}

static DigiPanelModel oneFactoryHitModel(std::uint8_t factoryIndex = 0u) {
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetFactorySlot(m.slots[0], factoryIndex);
    m.slots[0].volume = 220u;
    digiStepSetActive(m, 0u, 0u, 112u);
    return m;
}

static void factorySlotTriggersAndRendersAudio() {
    DigiSamplerEngine e;
    e.prepare(48000.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    DigiPanelModel m = oneFactoryHitModel(0u);
    const auto p = projectDigiRealtime(m, 0u);
    std::array<float, 256> l{};
    std::array<float, 256> r{};

    e.process(p, bank, l.data(), r.data(), static_cast<int>(l.size()), true, true);
    const auto& t = e.telemetry();

    assert(maxAbs(l) > 1.0e-4f);
    assert(maxAbs(r) > 1.0e-4f);
    assert(t.configuredFactorySlots == 1u);
    assert(t.activeSlotCount == 1u);
    assert(t.playingVoiceCount >= 1u);
    assert(t.triggerCount == 1u);
    assert(t.lastTriggeredSlot == 0u);
    assert(t.lastTriggeredFactorySlot == 150u);
    assert(t.outputPeak > 1.0e-4f);
    assert(hasNonZeroScope(e));
}

static void sameStepDoesNotRetrigger() {
    DigiSamplerEngine e;
    e.prepare(44100.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    DigiPanelModel m = oneFactoryHitModel(5u);
    const auto p = projectDigiRealtime(m, 0u);
    std::array<float, 128> l{};
    std::array<float, 128> r{};

    e.process(p, bank, l.data(), r.data(), static_cast<int>(l.size()), true, true);
    e.process(p, bank, l.data(), r.data(), static_cast<int>(l.size()), true, true);

    assert(e.telemetry().triggerCount == 1u);
    assert(e.telemetry().lastTriggeredFactorySlot == 155u);
}

static void newStepCanTriggerAgain() {
    DigiSamplerEngine e;
    e.prepare(44100.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    DigiPanelModel m = oneFactoryHitModel(2u);
    digiStepSetActive(m, 0u, 1u, 96u);
    std::array<float, 128> l{};
    std::array<float, 128> r{};

    e.process(projectDigiRealtime(m, 0u), bank, l.data(), r.data(), static_cast<int>(l.size()), true, true);
    e.process(projectDigiRealtime(m, 1u), bank, l.data(), r.data(), static_cast<int>(l.size()), true, true);

    assert(e.telemetry().triggerCount == 2u);
    assert(e.telemetry().lastTriggeredFactorySlot == 152u);
}

static void missingUserImportIsTelemetryVisibleButSilent() {
    DigiSamplerEngine e;
    e.prepare(48000.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetUserSampleSlot(m.slots[0], 0u, 0x12345678u);
    m.slots[0].volume = 200u;
    digiStepSetActive(m, 0u, 0u, 100u);
    std::array<float, 256> l{};
    std::array<float, 256> r{};

    e.process(projectDigiRealtime(m, 0u), bank, l.data(), r.data(), static_cast<int>(l.size()), true, true);

    assert(e.telemetry().configuredUserImportSlots == 1u);
    assert(e.telemetry().unavailableUserImportCount == 1u);
    assert(e.telemetry().triggerCount == 0u);
    assert(maxAbs(l) == 0.0f);
    assert(maxAbs(r) == 0.0f);
}


static void legacyUserImportPlaybackIsSteppedC64D418NotInterpolated() {
    DigiSamplerEngine e;
    e.prepare(48000.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float clip[2] = { -1.0f, 1.0f };

    // Source rate is half the render rate. Frame 1 lands at sample position
    // 0.5. The old hi-fi path interpolated -1..+1 to ~0 here; the C64-auth
    // path must hold the first $D418 nibble until the next DIGI tick.
    assert(digiLoadUserSampleFromFloatMono(bank, 0u, clip, 2u, 24000u, "step2", 9u));

    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetUserSampleSlot(m.slots[0], 0u, bank.clips[0].handle);
    m.slots[0].volume = 255u;
    digiStepSetActive(m, 0u, 0u, 127u);

    std::array<float, 6> l{};
    std::array<float, 6> r{};
    e.process(projectDigiRealtime(m, 0u), bank, l.data(), r.data(), static_cast<int>(l.size()), true, true);

    assert(std::fabs(l[0]) > 1.0e-4f);
    assert(std::fabs(l[1]) > 1.0e-4f);
    assert((l[0] < 0.0f && l[1] < 0.0f) || (l[0] > 0.0f && l[1] > 0.0f));
    assert(std::fabs(l[1] - l[0]) <= std::max(1.0e-4f, std::fabs(l[0]) * 0.02f));
}

static void userImportWithSavedClipTriggersAndRendersAudio() {
    DigiSamplerEngine e;
    e.prepare(48000.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float clip[12] = {
        -1.0f, -0.75f, -0.25f, 0.0f, 0.25f, 0.75f,
         1.0f,  0.75f,  0.25f, 0.0f, -0.25f, -0.75f
    };
    assert(digiLoadUserSampleFromFloatMono(bank, 0u, clip, 12u, 24000u, "user-hit", 8u));
    DigiPanelModel m = makeDefaultDigiPanelModel();
    digiSetUserSampleSlot(m.slots[0], 0u, bank.clips[0].handle);
    m.slots[0].volume = 240u;
    digiStepSetActive(m, 0u, 0u, 127u);
    std::array<float, 256> l{};
    std::array<float, 256> r{};

    e.process(projectDigiRealtime(m, 0u), bank, l.data(), r.data(), static_cast<int>(l.size()), true, true);

    assert(e.telemetry().configuredUserImportSlots == 1u);
    assert(e.telemetry().unavailableUserImportCount == 0u);
    assert(e.telemetry().triggerCount == 1u);
    assert(e.telemetry().lastTriggeredSlot == 0u);
    assert(maxAbs(l) > 1.0e-4f);
    assert(maxAbs(r) > 1.0e-4f);
}


static void nanInputBusIsSanitizedDuringAdditiveDigiMix() {
    DigiSamplerEngine e;
    e.prepare(48000.0);
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    DigiPanelModel m = oneFactoryHitModel(0u);
    const auto p = projectDigiRealtime(m, 0u);
    std::array<float, 64> l{};
    std::array<float, 64> r{};
    l.fill(std::numeric_limits<float>::quiet_NaN());
    r.fill(std::numeric_limits<float>::infinity());

    e.process(p, bank, l.data(), r.data(), static_cast<int>(l.size()), true, true);

    for (float v : l) {
        assert(std::isfinite(v));
        assert(v >= -1.25f && v <= 1.25f);
    }
    for (float v : r) {
        assert(std::isfinite(v));
        assert(v >= -1.25f && v <= 1.25f);
    }
    assert(std::isfinite(e.telemetry().outputPeak));
}

} // namespace

int main() {
    factorySlotTriggersAndRendersAudio();
    sameStepDoesNotRetrigger();
    newStepCanTriggerAgain();
    missingUserImportIsTelemetryVisibleButSilent();
    legacyUserImportPlaybackIsSteppedC64D418NotInterpolated();
    userImportWithSavedClipTriggersAndRendersAudio();
    nanInputBusIsSanitizedDuringAdditiveDigiMix();
    return 0;
}
