// SPDX-License-Identifier: BSD-3-Clause
// gui_realtime_projection_v588.h - RT-safe projection of GUI tab models.
//
// The MIX, KIT and DIGI tabs own compact POD models. This header turns those
// persisted GUI models into a bounded render-friendly projection: no allocation,
// no locks, no Objective-C, no strings, and no dynamic dispatch.

#ifndef ARPSID_GUI_REALTIME_PROJECTION_V588_H
#define ARPSID_GUI_REALTIME_PROJECTION_V588_H

#include "arpsid/gui/digi_panel_model.h"
#include "arpsid/gui/kit_state_blob.h"
#include "arpsid/gui/mix_panel_model.h"

#include <cstdint>
#include <type_traits>

namespace ArpSID {
namespace GUI {

inline constexpr std::uint32_t kGuiRealtimeProjectionSchemaVersion = 1u;

inline constexpr float guiByteUnit(std::uint8_t v) noexcept {
    return static_cast<float>(v) * (1.0f / 255.0f);
}

inline constexpr std::uint8_t guiFlag01(std::uint8_t v) noexcept {
    return v ? 1u : 0u;
}

inline constexpr std::uint8_t guiWrapStep(std::uint8_t step, std::uint8_t count) noexcept {
    return count ? static_cast<std::uint8_t>(step % count) : 0u;
}

inline constexpr float guiMixVolumeGain(std::uint8_t volume) noexcept {
    return volume == 0u ? 0.0f : static_cast<float>(volume) * (1.0f / 200.0f);
}

inline constexpr float guiMixMasterGain(const MixMaster& m) noexcept {
    const float base = guiByteUnit(m.masterVolume);
    return m.dimMonitor ? (base * 0.31622777f) : base;
}

struct GuiRealtimeMixChannel {
    float gainL;
    float gainR;
    float sendDelay;
    float sendReverb;
    std::uint8_t audible;
    std::uint8_t fxActiveCount;
    std::uint8_t solo;
    std::uint8_t mute;
};

struct GuiRealtimeMixProjection {
    std::uint32_t schemaVersion;
    std::uint8_t anySolo;
    std::uint8_t activeChannelCount;
    std::uint8_t limiterEnabled;
    std::uint8_t reserved0;
    float masterGain;
    float stereoWidth;
    float limiterThresholdDb;
    float limiterReleaseMs;
    float sendReturn[kMixSendBusCount];
    std::uint8_t sendEnabled[kMixSendBusCount];
    std::uint8_t reserved1[2];
    GuiRealtimeMixChannel channels[kMixChannelCount];
};

struct GuiRealtimeKitDrum {
    std::uint8_t drumClass;
    std::uint8_t midiNote;
    std::uint8_t activeAtStep;
    std::uint8_t stepVelocity;
    std::uint16_t drsidFactorySlot;
    std::uint16_t sid808FactorySlot;
    std::uint16_t digiFactorySlot;
    std::uint16_t selectedFactorySlot;
    std::uint8_t sid808Waveform;
    std::uint8_t sid808Attack;
    std::uint8_t sid808Decay;
    std::uint8_t sid808Sustain;
    std::uint8_t sid808Release;
    std::uint8_t sid808Flags;
    std::uint16_t sid808PulseWidth;
    std::uint8_t digiSlotIndex;
    std::int8_t digiTuneShift;
    std::uint8_t digiStartOffset;
    std::uint8_t digiLengthScale;
    std::uint8_t digiFlags;
};

struct GuiRealtimeKitProjection {
    std::uint32_t schemaVersion;
    std::uint8_t activeEngineTarget;
    std::uint8_t activeDrumClass;
    std::uint8_t activeStepIndex;
    std::uint8_t activeEditorMode;
    std::uint8_t activeStepCount;
    std::uint8_t reserved[3];
    GuiRealtimeKitDrum drums[kKitDrumClassCount];
};

struct GuiRealtimeDigiSlot {
    std::uint8_t activeAtStep;
    std::uint8_t sourceType;
    std::uint8_t factorySlotIndex;
    std::uint8_t stepVelocity;
    std::uint16_t absoluteFactorySlot;
    std::int8_t tuneShift;
    std::uint8_t startOffset;
    std::uint8_t lengthScale;
    std::uint8_t volume;
    std::uint8_t flags;
    std::uint8_t userSampleIndex;
    std::uint32_t userSampleHandle;
};

struct GuiRealtimeDigiProjection {
    std::uint32_t schemaVersion;
    std::uint8_t activeSlot;
    std::uint8_t stepIndex;
    std::uint8_t activeSlotCount;
    std::uint8_t reserved;
    GuiRealtimeDigiSlot slots[kDigiActiveSlotCount];
};

struct GuiRealtimeProjectionTelemetry {
    std::uint32_t schemaVersion;
    std::uint16_t activeMixChannels;
    std::uint16_t activeKitSteps;
    std::uint16_t activeDigiSlots;
    std::uint16_t activeGuiEvents;
    std::uint8_t anySolo;
    std::uint8_t limiterEnabled;
    std::uint8_t activeKitTarget;
    std::uint8_t activeDigiSlot;
};

struct GuiRealtimeProjection {
    std::uint32_t schemaVersion;
    GuiRealtimeMixProjection mix;
    GuiRealtimeKitProjection kit;
    GuiRealtimeDigiProjection digi;
    GuiRealtimeProjectionTelemetry telemetry;
};

static_assert(std::is_trivially_copyable<GuiRealtimeMixProjection>::value,
              "GuiRealtimeMixProjection must be trivially copyable");
static_assert(std::is_trivially_copyable<GuiRealtimeKitProjection>::value,
              "GuiRealtimeKitProjection must be trivially copyable");
static_assert(std::is_trivially_copyable<GuiRealtimeDigiProjection>::value,
              "GuiRealtimeDigiProjection must be trivially copyable");
static_assert(std::is_trivially_copyable<GuiRealtimeProjection>::value,
              "GuiRealtimeProjection must be trivially copyable");
static_assert(sizeof(GuiRealtimeProjection) <= 2048u,
              "GuiRealtimeProjection must stay small enough for render-side snapshots");

inline GuiRealtimeMixProjection projectMixRealtime(const MixPanelModel& src) noexcept {
    MixPanelModel m = src;
    sanitizeMixModel(m);

    GuiRealtimeMixProjection out{};
    out.schemaVersion = kGuiRealtimeProjectionSchemaVersion;
    out.limiterEnabled = guiFlag01(m.master.limiterEnabled);
    out.masterGain = guiMixMasterGain(m.master);
    out.stereoWidth = guiByteUnit(m.master.stereoWidth) * 2.0f;
    out.limiterThresholdDb = -24.0f + 24.0f * guiByteUnit(m.master.limiterThreshold);
    out.limiterReleaseMs = 10.0f + 490.0f * guiByteUnit(m.master.limiterRelease);

    bool anySolo = false;
    for (std::uint8_t ch = 0; ch < kMixChannelCount; ++ch) {
        if (m.channels[ch].solo) anySolo = true;
    }
    out.anySolo = anySolo ? 1u : 0u;

    for (std::uint8_t bus = 0; bus < kMixSendBusCount; ++bus) {
        const bool enabled = (m.sendBuses[bus].enabled != 0u);
        out.sendEnabled[bus] = enabled ? 1u : 0u;
        out.sendReturn[bus] = enabled ? guiByteUnit(m.sendBuses[bus].returnLevel) : 0.0f;
    }

    for (std::uint8_t ch = 0; ch < kMixChannelCount; ++ch) {
        const MixChannel& c = m.channels[ch];
        GuiRealtimeMixChannel& dst = out.channels[ch];
        dst.solo = guiFlag01(c.solo);
        dst.mute = guiFlag01(c.mute);
        const bool audible = channelIsAudible(c, anySolo) && c.volume != 0u && out.masterGain > 0.0f;
        dst.audible = audible ? 1u : 0u;
        if (audible) {
            const float base = guiMixVolumeGain(c.volume) * out.masterGain;
            const float pan = channelPanLinear(c);
            const float leftPan = (pan > 0.0f) ? (1.0f - pan) : 1.0f;
            const float rightPan = (pan < 0.0f) ? (1.0f + pan) : 1.0f;
            dst.gainL = base * leftPan;
            dst.gainR = base * rightPan;
            dst.sendDelay = guiByteUnit(c.sendToDelay) * out.sendReturn[0];
            dst.sendReverb = guiByteUnit(c.sendToReverb) * out.sendReturn[1];
            ++out.activeChannelCount;
        }
        for (std::uint8_t slot = 0; slot < kMixFxSlotsPerChannel; ++slot) {
            const MixFxSlot& fx = c.fxSlots[slot];
            if (fx.type != MixFxType::None && fx.bypass == 0u) ++dst.fxActiveCount;
        }
    }

    return out;
}

inline GuiRealtimeKitProjection projectKitRealtime(const KitStateBlob& src,
                                                   std::uint8_t sequencerStep) noexcept {
    KitStateBlob b = src;
    kitStateBlobSanitize(b);
    const std::uint8_t step = guiWrapStep(sequencerStep, kKitStepCount);
    const std::uint8_t target = b.panelModel.activeEngineTarget;

    GuiRealtimeKitProjection out{};
    out.schemaVersion = kGuiRealtimeProjectionSchemaVersion;
    out.activeEngineTarget = target;
    out.activeDrumClass = b.panelModel.activeDrumClass;
    out.activeStepIndex = step;
    out.activeEditorMode = b.panelModel.activeEditorMode;

    for (std::uint8_t dc = 0; dc < kKitDrumClassCount; ++dc) {
        GuiRealtimeKitDrum& d = out.drums[dc];
        const KitVoiceConfig& voice = b.voiceConfigGrid.voiceConfigs[dc];
        const KitAssignConfig& assign = b.assignConfigGrid.assignConfigs[dc];
        d.drumClass = dc;
        d.midiNote = kKitDrumClassMidiNote[dc];
        d.stepVelocity = kitStepVelocity(b.stepGrid, dc, step);
        d.activeAtStep = d.stepVelocity ? 1u : 0u;
        if (d.activeAtStep) ++out.activeStepCount;

        const std::uint8_t drsidIdx =
            b.panelModel.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::DrSID)].factorySlotIndex;
        const std::uint8_t sid808Idx =
            b.panelModel.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::SID808)].factorySlotIndex;
        const std::uint8_t digiIdx =
            b.panelModel.drumAssignments[dc][static_cast<std::uint8_t>(KitEngineTarget::Digi)].factorySlotIndex;
        d.drsidFactorySlot = kitAbsoluteSlot(KitEngineTarget::DrSID, drsidIdx);
        d.sid808FactorySlot = kitAbsoluteSlot(KitEngineTarget::SID808, sid808Idx);
        d.digiFactorySlot = kitAbsoluteSlot(KitEngineTarget::Digi, digiIdx);
        d.selectedFactorySlot =
            (target == static_cast<std::uint8_t>(KitEngineTarget::SID808)) ? d.sid808FactorySlot :
            (target == static_cast<std::uint8_t>(KitEngineTarget::Digi))   ? d.digiFactorySlot :
                                                                             d.drsidFactorySlot;

        d.sid808Waveform = voice.waveform;
        d.sid808Attack = kitVoiceAttack(voice);
        d.sid808Decay = kitVoiceDecay(voice);
        d.sid808Sustain = kitVoiceSustain(voice);
        d.sid808Release = kitVoiceRelease(voice);
        d.sid808Flags = voice.flags;
        d.sid808PulseWidth = kitVoicePulseWidth(voice);

        d.digiSlotIndex = kitAssignSlot(assign);
        d.digiTuneShift = kitAssignTuneShift(assign);
        d.digiStartOffset = kitAssignStartOffset(assign);
        d.digiLengthScale = kitAssignLengthScale(assign);
        d.digiFlags = assign.flags;
    }

    return out;
}

inline GuiRealtimeDigiProjection projectDigiRealtime(const DigiPanelModel& src,
                                                     std::uint8_t sequencerStep) noexcept {
    DigiPanelModel m = src;
    sanitizeDigiPanelModel(m);
    const std::uint8_t step = guiWrapStep(sequencerStep, kDigiStepCount);

    GuiRealtimeDigiProjection out{};
    out.schemaVersion = kGuiRealtimeProjectionSchemaVersion;
    out.activeSlot = m.activeSlot;
    out.stepIndex = step;

    for (std::uint8_t slot = 0; slot < kDigiActiveSlotCount; ++slot) {
        const DigiSampleSlot& s = m.slots[slot];
        GuiRealtimeDigiSlot& d = out.slots[slot];
        d.sourceType = static_cast<std::uint8_t>(s.sourceType);
        d.factorySlotIndex = s.factorySlotIndex;
        d.stepVelocity = digiStepVelocity(m, slot, step);
        d.absoluteFactorySlot =
            (s.sourceType == DigiSourceType::FactorySlot)
                ? static_cast<std::uint16_t>(kDigiNewFactoryRange.first + s.factorySlotIndex)
                : 0u;
        d.tuneShift = static_cast<std::int8_t>(digiTuneShift(s));
        d.startOffset = s.startOffset;
        d.lengthScale = s.lengthScale;
        d.volume = s.volume;
        d.flags = s.flags;
        d.userSampleIndex = s.userSampleIndex;
        d.userSampleHandle = s.userSampleHandle;
        const bool active = s.sourceType != DigiSourceType::None &&
                            d.stepVelocity != 0u &&
                            s.volume != 0u;
        d.activeAtStep = active ? 1u : 0u;
        if (active) ++out.activeSlotCount;
    }

    return out;
}

inline GuiRealtimeProjection projectGuiRealtime(const MixPanelModel& mix,
                                                const KitStateBlob& kit,
                                                const DigiPanelModel& digi,
                                                std::uint8_t sequencerStep) noexcept {
    GuiRealtimeProjection out{};
    out.schemaVersion = kGuiRealtimeProjectionSchemaVersion;
    out.mix = projectMixRealtime(mix);
    out.kit = projectKitRealtime(kit, sequencerStep);
    out.digi = projectDigiRealtime(digi, sequencerStep);
    out.telemetry.schemaVersion = kGuiRealtimeProjectionSchemaVersion;
    out.telemetry.activeMixChannels = out.mix.activeChannelCount;
    out.telemetry.activeKitSteps = out.kit.activeStepCount;
    out.telemetry.activeDigiSlots = out.digi.activeSlotCount;
    out.telemetry.activeGuiEvents = static_cast<std::uint16_t>(
        out.telemetry.activeKitSteps + out.telemetry.activeDigiSlots);
    out.telemetry.anySolo = out.mix.anySolo;
    out.telemetry.limiterEnabled = out.mix.limiterEnabled;
    out.telemetry.activeKitTarget = out.kit.activeEngineTarget;
    out.telemetry.activeDigiSlot = out.digi.activeSlot;
    return out;
}

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_REALTIME_PROJECTION_V588_H
