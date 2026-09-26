// Copyright (C) 2024-2026 Ulf Bertilsson
// gui_realtime_projection_v588_tests.cpp
// Pins the RT-safe projection from GUI-owned MIX/KIT/DIGI models into render
// intent and lightweight telemetry counts.

#include "arpsid/gui/gui_realtime_projection_v588.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <type_traits>

using namespace ArpSID;
using namespace ArpSID::GUI;

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

static void requireNear(float a, float b, float eps, const char* msg) {
    require(std::fabs(a - b) <= eps, msg);
}

int main() {
    static_assert(std::is_trivially_copyable<GuiRealtimeProjection>::value,
                  "projection is trivially copyable");
    static_assert(sizeof(GuiRealtimeProjection) <= 2048u,
                  "projection stays compact");

    {
        const auto p = projectMixRealtime(makeDefaultMixModel());
        require(p.schemaVersion == kGuiRealtimeProjectionSchemaVersion, "mix projection schema");
        require(p.activeChannelCount == kMixChannelCount, "default mix has all 16 channels audible");
        require(p.anySolo == 0u, "default mix has no solo");
        require(p.limiterEnabled == 1u, "default limiter enabled");
        requireNear(p.masterGain, 1.0f, 0.0001f, "default master at unity");
        requireNear(p.channels[0].gainL, 1.0f, 0.0001f, "default ch0 left unity");
        requireNear(p.channels[0].gainR, 1.0f, 0.0001f, "default ch0 right unity");
    }

    {
        auto m = makeDefaultMixModel();
        m.channels[2].solo = 1u;
        m.channels[2].pan = 255u;
        m.channels[2].sendToDelay = 255u;
        m.sendBuses[0].enabled = 1u;
        m.sendBuses[0].returnLevel = 200u;
        setMixFxSlot(m.channels[2].fxSlots[0], MixFxType::Compressor);
        const auto p = projectMixRealtime(m);
        require(p.anySolo == 1u, "solo flag projected");
        require(p.activeChannelCount == 1u, "solo leaves one audible channel");
        require(p.channels[0].audible == 0u, "unsoloed channel muted by solo bus");
        require(p.channels[2].audible == 1u, "soloed channel audible");
        require(p.channels[2].gainL < 0.01f, "full-right pan mutes left projection");
        require(p.channels[2].gainR > 0.9f, "full-right pan keeps right projection");
        require(p.channels[2].sendDelay > 0.70f, "enabled delay send projected");
        require(p.channels[2].fxActiveCount == 1u, "active FX slot counted");
    }

    {
        auto kit = makeDefaultKitStateBlob();
        kit.panelModel.activeEngineTarget = static_cast<std::uint8_t>(KitEngineTarget::SID808);
        kitStepSetActive(kit.stepGrid, static_cast<std::uint8_t>(KitDrumClass::Tom), 7u, 127u);
        KitVoiceConfig& tomVoice =
            kit.voiceConfigGrid.voiceConfigs[static_cast<std::uint8_t>(KitDrumClass::Tom)];
        kitVoiceSetWaveform(tomVoice, kKitVoiceWavePul);
        kitVoiceSetAttack(tomVoice, 3u);
        kitVoiceSetDecay(tomVoice, 9u);
        kitVoiceSetSustain(tomVoice, 6u);
        kitVoiceSetRelease(tomVoice, 5u);
        kitVoiceSetPulseWidth(tomVoice, 3072u);
        KitAssignConfig& tomAssign =
            kit.assignConfigGrid.assignConfigs[static_cast<std::uint8_t>(KitDrumClass::Tom)];
        kitAssignSetSlot(tomAssign, 17u);
        kitAssignSetTuneShift(tomAssign, -7);
        kitAssignSetLoop(tomAssign, true);

        const auto p = projectKitRealtime(kit, 7u);
        const auto& tom = p.drums[static_cast<std::uint8_t>(KitDrumClass::Tom)];
        require(p.activeStepCount == 1u, "one active kit step projected");
        require(tom.activeAtStep == 1u, "tom step active");
        require(tom.stepVelocity == 127u, "tom velocity projected");
        require(tom.midiNote == 47u, "tom canonical note is 47");
        require(tom.drsidFactorySlot == 86u, "tom DrSID absolute slot");
        require(tom.sid808FactorySlot == 126u, "tom SID808 absolute slot");
        require(tom.digiFactorySlot == 156u, "tom Digi absolute slot");
        require(tom.selectedFactorySlot == 126u, "active SID808 selected slot");
        require(tom.sid808Waveform == kKitVoiceWavePul, "SID808 waveform projected");
        require(tom.sid808Attack == 3u && tom.sid808Decay == 9u, "SID808 AD projected");
        require(tom.sid808Sustain == 6u && tom.sid808Release == 5u, "SID808 SR projected");
        require(tom.sid808PulseWidth == 3072u, "SID808 pulse width projected");
        require(tom.digiSlotIndex == 17u, "Digi assign slot projected");
        require(tom.digiTuneShift == -7, "Digi assign tune projected");
        require((tom.digiFlags & kKitAssignFlagLoop) != 0u, "Digi assign loop projected");
    }

    {
        auto digi = makeDefaultDigiPanelModel();
        digi.activeSlot = 3u;
        digiSetFactorySlot(digi.slots[3], 14u);
        digiSetTuneShift(digi.slots[3], -5);
        digi.slots[3].volume = 210u;
        digi.slots[3].startOffset = 42u;
        digi.slots[3].lengthScale = 180u;
        digiSetReverse(digi.slots[3], true);
        digiStepSetActive(digi, 3u, 5u, 90u);
        const auto p = projectDigiRealtime(digi, 5u);
        require(p.activeSlot == 3u, "active Digi slot projected");
        require(p.activeSlotCount == 1u, "one active Digi slot projected");
        require(p.slots[3].activeAtStep == 1u, "Digi slot active at step");
        require(p.slots[3].absoluteFactorySlot == 164u, "Digi absolute factory slot");
        require(p.slots[3].stepVelocity == 90u, "Digi velocity projected");
        require(p.slots[3].tuneShift == -5, "Digi tune projected");
        require(p.slots[3].startOffset == 42u, "Digi start projected");
        require(p.slots[3].lengthScale == 180u, "Digi length projected");
        require((p.slots[3].flags & kDigiFlagReverse) != 0u, "Digi reverse projected");
    }

    {
        auto mix = makeDefaultMixModel();
        for (std::uint8_t ch = 1; ch < kMixChannelCount; ++ch) mix.channels[ch].enabled = 0u;
        auto kit = makeDefaultKitStateBlob();
        kitStepSetActive(kit.stepGrid, static_cast<std::uint8_t>(KitDrumClass::Kick), 0u, 100u);
        auto digi = makeDefaultDigiPanelModel();
        digiSetFactorySlot(digi.slots[0], 0u);
        digiStepSetActive(digi, 0u, 0u, 64u);
        const auto p = projectGuiRealtime(mix, kit, digi, 32u);
        require(p.telemetry.activeMixChannels == 1u, "telemetry mix count");
        require(p.telemetry.activeKitSteps == 1u, "telemetry kit count");
        require(p.telemetry.activeDigiSlots == 1u, "telemetry digi count");
        require(p.telemetry.activeGuiEvents == 2u, "telemetry event count");
        require(p.kit.activeStepIndex == 0u, "kit sequencer step wraps");
        require(p.digi.stepIndex == 0u, "digi sequencer step wraps");
    }

    {
        auto mix = makeDefaultMixModel();
        mix.channels[0].solo = 99u; // corrupt, should sanitize to defaults
        auto kit = makeDefaultKitStateBlob();
        kit.schemaVersion = 99u;
        auto digi = makeDefaultDigiPanelModel();
        digi.schemaVersion = 99u;
        const auto p = projectGuiRealtime(mix, kit, digi, 0u);
        require(p.mix.activeChannelCount == kMixChannelCount, "bad mix sanitizes to default");
        require(p.kit.activeStepCount == 0u, "bad kit sanitizes to default");
        require(p.digi.activeSlotCount == 0u, "bad digi sanitizes to default");
    }

    std::cout << "gui_realtime_projection_v588_tests: GUI realtime projection pinned\n";
    return 0;
}
