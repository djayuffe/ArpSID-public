// arpsid_controller.cpp
// ArpSID — VST3 Edit Controller (Phase 3 / Phase 4)
//
// Responsibilities
// ────────────────
// • Register all kNumParams parameters with the VST3 host.
// • Implement IEditController2 / IMidiMapping for MIDI CC → parameter bridging.
// • Implement IProgramListData / IUnitInfo for the canonical 180-slot factory preset bank.
// • Provide IArpSIDTelemetryProvider query routing so the GUI can read meters.
// • Connect to the processor's shared-component (IComponent) for live communication.
//
// NOTE: This file is #included as a single translation unit by factory.cpp.
// Do NOT add it to the CMake PLUGIN_SOURCES list as a separate .cpp.
//
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT


#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/base/ibstream.h"
// ustring.h removed: UString128 replaced with manual ASCII-to-char16 conversion
#include "base/source/fstreamer.h"

#include "parameter_ids.h"
#include "arpsid_telemetry_iface.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid_preset_bank.h"
#include "arpsid/core/sid_parameter_presentation.h"
#include "gui/arpsid_vstgui_editor.h"

#include <array>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>

namespace ArpSID {

using namespace Steinberg;
using namespace Steinberg::Vst;

// ─── Helper: UTF-8 → UTF-16 (TChar) ─────────────────────────────────────────
// v966: real bounded UTF-8 decoding (surrogate pairs, deterministic U+FFFD
// replacement, truncation never splits a pair) — replaces the old
// byte-to-TChar cast that corrupted every multi-byte character.
static void utf8ToTChar(const char* src, TChar* dst, size_t maxLen) {
    (void)ArpSID_utf8ToUtf16(src, dst, maxLen);
}

// ─── ArpSIDControllerPhase3 ───────────────────────────────────────────────────
class ArpSIDControllerPhase3
    : public EditController
    , public IMidiMapping
    , public IUnitInfo
    , public IPresetControllerLivePush
{
public:
    // ── Construction ────────────────────────────────────────────────────────
    ArpSIDControllerPhase3() = default;
    ~ArpSIDControllerPhase3() override = default;

    static FUnknown* createInstance(void*) {
        return (IEditController*)new ArpSIDControllerPhase3;
    }

    // ── F29: initialize() MUST call registration helpers ────────────────────
    tresult PLUGIN_API initialize(FUnknown* context) override {
        tresult result = EditController::initialize(context);
        if (result != kResultOk) return result;
        registerAllParameters_();
        buildUnitInfo_();
        return kResultOk;
    }

    tresult PLUGIN_API terminate() override {
        editorView_ = nullptr;
        livePushFn_ = nullptr;
        livePushUser_ = nullptr;
        return EditController::terminate();
    }

    IPlugView* PLUGIN_API createView(FIDString name) override {
        // v966: editor diagnostics are compile-time opt-in. Release builds
        // must not write to stderr or disclose object addresses.
#if defined(ARPSID_VST_EDITOR_DIAGNOSTICS)
        std::fprintf(stderr, "[plugin/controller] createView name=%s\n", name ? name : "<null>");
#endif
        if (!name)
            return nullptr;
        if (FIDStringsEqual(name, ViewType::kEditor)) {
            auto* view = new ArpSIDVSTGUIEditor(this);
            editorView_ = view;
#if defined(ARPSID_VST_EDITOR_DIAGNOSTICS)
            std::fprintf(stderr, "[plugin/controller] createView -> %p\n", static_cast<void*>(view));
#endif
            return view;
        }
#if defined(ARPSID_VST_EDITOR_DIAGNOSTICS)
        std::fprintf(stderr, "[plugin/controller] createView unsupported name=%s\n", name);
#endif
        return nullptr;
    }

    void PLUGIN_API setLiveParamPushCallback(PresetLiveParamPushFn fn, void* user) noexcept override {
        livePushFn_ = fn;
        livePushUser_ = user;
    }

    // ── IPluginBase ─────────────────────────────────────────────────────────
    // v966: display and text parsing delegate to the shared parameter-ID-aware
    // presentation authority so host text matches the canonical DSP laws
    // (exponential LFO rate, quadratic portamento, limiter ms laws,
    // sequencer tempo 20 + 280 × norm, enum/boolean labels).
    tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID tag,
        Steinberg::Vst::ParamValue valueNormalized,
        String128 string) override {
        if (tag >= (Steinberg::Vst::ParamID)kNumParams) return kResultFalse;
        char buf[64] = {};
        if (!SidParameterPresentation::formatNormalized((int)tag, (float)valueNormalized,
                                                        buf, sizeof(buf)))
            return kResultFalse;
        utf8ToTChar(buf, string, 128);
        return kResultOk;
    }

    tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID tag,
                                              TChar* string,
                                              Steinberg::Vst::ParamValue& valueNormalized) override {
        if (!string || tag >= (Steinberg::Vst::ParamID)kNumParams) return kResultFalse;
        char buf[128] = {};
        (void)ArpSID_utf16ToUtf8(string, buf, sizeof(buf));
        float normalized = 0.0f;
        if (!SidParameterPresentation::parseToNormalized((int)tag, buf, normalized))
            return kResultFalse;
        valueNormalized = (ParamValue)normalized;
        return kResultOk;
    }

    // ── IMidiMapping ─────────────────────────────────────────────────────────
    tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex,
                                                    int16 channel,
                                                    CtrlNumber midiControllerNumber,
                                                    Steinberg::Vst::ParamID& id) override {
        if (busIndex != 0 || channel < 0 || channel >= 16) return kResultFalse;
        const int ch = (int)channel;
        switch (midiControllerNumber) {
            case kCtrlModWheel:
                id = (Steinberg::Vst::ParamID)((int)kParamHostCtrlModWheelBase + ch);
                return kResultOk;
            case kCtrlBreath:
                id = (Steinberg::Vst::ParamID)((int)kParamHostCtrlBreathBase + ch);
                return kResultOk;
            case kCtrlExpression:
                id = (Steinberg::Vst::ParamID)((int)kParamHostCtrlExpressionBase + ch);
                return kResultOk;
            case kCtrlSustainOnOff:
                id = (Steinberg::Vst::ParamID)((int)kParamHostCtrlSustainBase + ch);
                return kResultOk;
            case kCtrlSustenutoOnOff:
                id = (Steinberg::Vst::ParamID)((int)kParamHostCtrlSostenutoBase + ch);
                return kResultOk;
            case kAfterTouch:
                id = (Steinberg::Vst::ParamID)((int)kParamHostCtrlChannelPressureBase + ch);
                return kResultOk;
            case kPitchBend:
                id = (Steinberg::Vst::ParamID)((int)kParamHostCtrlPitchBendBase + ch);
                return kResultOk;
            default:
                return kResultFalse;
        }
    }

    // ── IUnitInfo ────────────────────────────────────────────────────────────
    int32 PLUGIN_API getUnitCount() override {
        return (int32)units_.size();
    }

    tresult PLUGIN_API getUnitInfo(int32 unitIndex, UnitInfo& info) override {
        if (unitIndex < 0 || unitIndex >= (int32)units_.size())
            return kResultFalse;
        info = units_[(size_t)unitIndex];
        return kResultOk;
    }

    int32 PLUGIN_API getProgramListCount() override {
        return 1; // one program list: slot 0
    }

    tresult PLUGIN_API getProgramListInfo(int32 listIndex, ProgramListInfo& info) override {
        if (listIndex != 0) return kResultFalse;
        info.id         = kProgramListId_;
        info.programCount = kMaxPresets_;
        utf8ToTChar("ArpSID Factory Patches", info.name, 128);
        return kResultOk;
    }

    tresult PLUGIN_API getProgramName(ProgramListID listId,
                                       int32 programIndex,
                                       String128 name) override {
        if (listId != kProgramListId_) return kResultFalse;
        if (programIndex < 0 || programIndex >= kMaxPresets_) return kResultFalse;
        const std::string n = factoryPatchNameForSlot((int)programIndex);
        utf8ToTChar(n.c_str(), name, 128);
        return kResultOk;
    }

    tresult PLUGIN_API getProgramInfo(ProgramListID listId,
                                       int32 programIndex,
                                       Steinberg::Vst::CString attributeId,
                                       String128 attributeValue) override {
        (void)listId; (void)programIndex; (void)attributeId;
        attributeValue[0] = 0;
        return kResultFalse;
    }

    tresult PLUGIN_API hasProgramPitchNames(ProgramListID, int32) override {
        return kResultFalse;
    }

    tresult PLUGIN_API getProgramPitchName(ProgramListID, int32, int16, String128) override {
        return kResultFalse;
    }

    int32 PLUGIN_API getSelectedUnit() override { return kRootUnitId; }

    tresult PLUGIN_API selectUnit(UnitID /*unitId*/) override {
        return kResultOk;
    }

    tresult PLUGIN_API getUnitByBus(MediaType, BusDirection, int32, int32,
                                     UnitID& unitId) override {
        unitId = kRootUnitId;
        return kResultOk;
    }

    tresult PLUGIN_API setUnitProgramData(int32, int32, IBStream*) override {
        return kNotImplemented;
    }

    // ── queryInterface ──────────────────────────────────────────────────────
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        QUERY_INTERFACE(_iid, obj, IMidiMapping::iid, IMidiMapping)
        QUERY_INTERFACE(_iid, obj, IUnitInfo::iid, IUnitInfo)
        QUERY_INTERFACE(_iid, obj, IPresetControllerLivePush::iid, IPresetControllerLivePush)
        return EditController::queryInterface(_iid, obj);
    }

    REFCOUNT_METHODS(EditController)

private:
    // ── Parameter Registration ───────────────────────────────────────────────
    void registerAllParameters_() {
        for (int i = 0; i < kNumParams; ++i) {
            const ParamInfo& info = kParamInfos[(size_t)i];

            int32 flags = info.automatable ? ParameterInfo::kCanAutomate : ParameterInfo::kIsReadOnly;
            if (i == (int)kParamProgram) {
                flags = ParameterInfo::kIsList;
            } else if (i == (int)kParamPanic ||
                       i == (int)kParamVirtualGate ||
                       i == (int)kParamBankCommand) {
                flags = ParameterInfo::kIsHidden;
            }

            String128 title{};
            String128 units{};
            utf8ToTChar(info.name, title, 128);
            // v966: host-visible unit text comes from the shared typed
            // descriptor, not the raw table string, so all wrappers agree.
            const char* unitSuffix = SidParameterPresentation::unit(i).suffix;
            if (unitSuffix && unitSuffix[0])
                utf8ToTChar(unitSuffix, units, 128);

            // One shared semantic-cardinality contract drives VST3, AUv2,
            // state repair and the DSP boundary. Ordinary toggles are never
            // marked kIsBypass; that flag is reserved for a real host bypass.
            const int32 stepCount = static_cast<int32>(normalizedParamStepCount(i));

            auto* rp = new RangeParameter(
                title,
                (Steinberg::Vst::ParamID)i,
                units,
                0.0,
                1.0,
                (Steinberg::Vst::ParamValue)info.defaultNorm,
                stepCount,
                flags,  // no kIsBypass here — bypass is a host-level concept
                kRootUnitId);
            parameters.addParameter(rp);
        }
    }

    // ── Unit / Program-list ─────────────────────────────────────────────────
    void buildUnitInfo_() {
        UnitInfo root;
        root.id             = kRootUnitId;
        root.parentUnitId   = kNoParentUnitId;
        root.programListId  = kProgramListId_;
        utf8ToTChar("Root", root.name, 128);
        units_.push_back(root);
    }

    static constexpr ProgramListID kProgramListId_ = 1;
    // Canonical factory preset count: 180 slots, not legacy 128.
    static constexpr int32         kMaxPresets_     = ArpSID::kCanonicalFactoryPatchSlotCount;

    void pushLiveParam_(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) noexcept {
        if (livePushFn_)
            livePushFn_(livePushUser_, id, value);
    }

    PresetLiveParamPushFn livePushFn_ = nullptr;
    void* livePushUser_ = nullptr;
    Steinberg::IPtr<Steinberg::IPlugView> editorView_;
    std::vector<UnitInfo> units_;
};

} // namespace ArpSID
