// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/base/funknown.h"
#include "parameter_ids.h"
#include <array>
#include <string>
#include <vector>

namespace ArpSID {


using PresetLiveParamPushFn = void (*)(void* user, Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

struct IPresetControllerLivePush : Steinberg::FUnknown {
    static const Steinberg::FUID iid;
    virtual void PLUGIN_API setLiveParamPushCallback(PresetLiveParamPushFn fn, void* user) noexcept = 0;
};

inline void registerPresetControllerLivePush (Steinberg::Vst::EditController* controller,
                                              PresetLiveParamPushFn fn,
                                              void* user) {
    if (!controller) return;
    IPresetControllerLivePush* iface = nullptr;
    if (controller->queryInterface(IPresetControllerLivePush::iid, reinterpret_cast<void**>(&iface)) == Steinberg::kResultOk && iface) {
        iface->setLiveParamPushCallback(fn, user);
        iface->release();
    }
}

struct PresetBankSnapshot {
    int32_t selectedSlot = 0;
    std::vector<std::string> names;
    std::vector<std::array<double, (size_t)kNumParams>> slots;
};

bool exportPresetBankSnapshot (Steinberg::Vst::EditController* controller,
                               PresetBankSnapshot& outSnapshot);

bool importPresetBankSnapshot (Steinberg::Vst::EditController* controller,
                               const PresetBankSnapshot& snapshot,
                               bool applySelectedSlot);

bool exportPresetSlotSnapshot (Steinberg::Vst::EditController* controller,
                               int32_t slot,
                               std::array<double, (size_t)kNumParams>& outValues,
                               std::string& outName);

bool importPresetSlotSnapshot (Steinberg::Vst::EditController* controller,
                               int32_t slot,
                               const std::array<double, (size_t)kNumParams>& values,
                               const std::string& name,
                               bool applyNow);

bool renamePresetSlot (Steinberg::Vst::EditController* controller,
                       int32_t slot,
                       const std::string& name,
                       bool notifyEditorAndHost = true);

bool selectPresetSlot (Steinberg::Vst::EditController* controller,
                       int32_t slot,
                       bool applyNow = true);

bool storePresetToSlot (Steinberg::Vst::EditController* controller,
                        int32_t slot);

bool getSelectedPresetSlot (Steinberg::Vst::EditController* controller,
                            int32_t& outSlot);


bool resetPresetBankToFactory (Steinberg::Vst::EditController* controller,
                               int32_t selectedSlot = 1,
                               bool applySelectedSlot = true);

} // namespace ArpSID

// Stable VST3 interface ID used instead of cross-binary RTTI/dynamic_cast.
inline const Steinberg::FUID ArpSID::IPresetControllerLivePush::iid (0x1A0EFA11, 0x6A0A4B3Cu, 0x8E5E3F72u, 0x6E6A1B55u);
