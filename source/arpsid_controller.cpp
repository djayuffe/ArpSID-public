// arpsid_controller.cpp
// ArpSID — VST3 Edit Controller (Phase 3 / Phase 4)
//
// Responsibilities
// ────────────────
// • Register all kNumParams parameters with the VST3 host.
// • Implement IEditController2 / IMidiMapping for MIDI CC → parameter bridging.
// • Implement IProgramListData / IUnitInfo for the canonical 180-slot factory preset bank.
// • Receive the processor's Vst3KernelHost (same process only) so the editor
//   reads telemetry and edits the non-parameter GUI models like the AU editor.
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
#include "vst3/arpsid_vst3_kernel_host.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid_preset_bank.h"
#include "arpsid/core/sid_parameter_presentation.h"
#include "gui/arpsid_vstgui_editor.h"
#include "arpsid_vst_messages.h"
#include "gui/vstgui/arpsid_editor_layout.h"
#include "arpsid/core/sid_midi_cc_mapping.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "au3/ArpSIDStateSerializer.h"
#include "factory_patch_params.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "base/source/fobject.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

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
        kernelHost_ = nullptr;
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
#if defined(ARPSID_VSTGUI_EDITOR)
            IPlugView* view = arpsidCreateCrossPlatformEditor(this);
#elif defined(__APPLE__)
            auto* view = new ArpSIDVSTGUIEditor(this);
#else
            // Built without the editor (ARPSID_VST3_EDITOR=OFF): no view, so
            // hosts show their generic parameter UI.
            IPlugView* view = nullptr;
            if (!view) return nullptr;
#endif
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

    // ── Factory presets ──────────────────────────────────────────────────────
    // Program (kIsProgramChange) and BankSlot select a factory patch. Hosts
    // report program-list selections, program-change automation and editor
    // preset picks to the controller on the UI thread; the controller asks the
    // processor (IMessage, off the audio thread) to apply the patch and then
    // mirrors the patch's parameter values back to the host.
    tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                          Steinberg::Vst::ParamValue value) override {
        const tresult result = EditController::setParamNormalized(tag, value);
        if (result == kResultOk && !mirroringState_ &&
            (tag == (Steinberg::Vst::ParamID)kParamProgram ||
             tag == (Steinberg::Vst::ParamID)kParamBankSlot)) {
            const int slot = canonicalFactorySlotFromNormalizedBankSlot((float)value);
            if (slot != loadedFactorySlot_)
                loadFactoryPatch_(slot);
        }
        return result;
    }

    // Processor state -> controller parameters (project load, undo, duplicate).
    tresult PLUGIN_API setComponentState(IBStream* state) override {
        if (!state) return kResultFalse;
        int64 end = 0;
        if (state->seek(0, IBStream::kIBSeekEnd, &end) != kResultOk) return kResultFalse;
        if (end <= (int64)sizeof(uint32) || end > (int64)(64 * 1024 * 1024)) return kResultFalse;
        state->seek(0, IBStream::kIBSeekSet, nullptr);
        std::vector<uint8_t> bytes((size_t)end);
        int32 nRead = 0;
        if (state->read(bytes.data(), (int32)bytes.size(), &nRead) != kResultOk || nRead <= 0)
            return kResultFalse;
        SidStateRootV1 root{};
        if (!Vst3KernelHost::decodeStateRoot(bytes.data(), (size_t)nRead, root)) return kResultFalse;
        mirrorStateRootToParameters_(root, /*notifyHost*/ false);
        return kResultOk;
    }

    // ── Processor link ──────────────────────────────────────────────────────
    tresult PLUGIN_API connect(IConnectionPoint* other) override {
        const tresult result = EditController::connect(other);
        if (result == kResultOk) {
            if (IPtr<IMessage> msg = owned(allocateMessage())) {
                msg->setMessageID(kVstMsgRequestKernelHost);
                sendMessage(msg);
            }
        }
        return result;
    }

    tresult PLUGIN_API disconnect(IConnectionPoint* other) override {
        kernelHost_ = nullptr;
        return EditController::disconnect(other);
    }

    tresult PLUGIN_API notify(IMessage* message) override {
        if (message && message->getMessageID() &&
            FIDStringsEqual(message->getMessageID(), kVstMsgKernelHost)) {
            int64 ptr = 0, pid = 0;
            IAttributeList* attrs = message->getAttributes();
            if (attrs && attrs->getInt(kVstMsgAttrHostPtr, ptr) == kResultOk &&
                attrs->getInt(kVstMsgAttrPid, pid) == kResultOk && pid == currentProcessId_() && ptr != 0)
                kernelHost_ = reinterpret_cast<Vst3KernelHost*>(static_cast<std::uintptr_t>(ptr));
            return kResultOk;
        }
        return EditController::notify(message);
    }

    // The processor's kernel host when it runs in this process, else nullptr
    // (the editor then falls back to parameter-only operation).
    Vst3KernelHost* kernelHost() const noexcept { return kernelHost_; }

    // Editor model edits change the saved state without a parameter change.
    void markStateDirty() {
        if (!componentHandler) return;
        FUnknownPtr<IComponentHandler2> handler2(componentHandler);
        if (handler2) handler2->setDirty(true);
    }

    // Editor preset pick: same path as a host program selection.
    void selectFactoryPatch(int slot) {
        const ParamValue norm = (ParamValue)canonicalNormalizedBankSlotValue(std::clamp(slot, 0, kCanonicalFactoryPatchSlotMax));
        beginEdit((ParamID)kParamBankSlot);
        setParamNormalized((ParamID)kParamBankSlot, norm);
        performEdit((ParamID)kParamBankSlot, norm);
        endEdit((ParamID)kParamBankSlot);
    }
    int loadedFactorySlot() const noexcept { return loadedFactorySlot_; }
    double editorZoom() const noexcept { return editorZoom_; }
    void setEditorZoom(double z) noexcept { editorZoom_ = std::clamp(z, 0.25, 4.0); }

    // Editor on-screen keyboard -> processor (see arpsid_vst_messages.h).
    void sendUiMidi(uint8_t status, uint8_t data1, uint8_t data2) {
        IPtr<IMessage> msg = owned(allocateMessage());
        if (!msg) return;
        msg->setMessageID(kVstMsgUiMidi);
        msg->getAttributes()->setInt(kVstMsgAttrStatus, status);
        msg->getAttributes()->setInt(kVstMsgAttrData1, data1);
        msg->getAttributes()->setInt(kVstMsgAttrData2, data2);
        sendMessage(msg);
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
            case kCtrlFoot:        // CC4: same expression law as the AU/raw-MIDI path
                id = (Steinberg::Vst::ParamID)((int)kParamHostCtrlExpressionBase + ch);
                return kResultOk;
            case kCtrlVolume:      // CC7: channel volume -> master volume
                id = (Steinberg::Vst::ParamID)kParamMasterVolume;
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
            default: {
                // Shared realtime CC law (sid_midi_cc_mapping.h), e.g. the
                // AKAI MPK mini knobs CC70-77: same targets as AU/standalone.
                if (midiControllerNumber >= 0 && midiControllerNumber < 128) {
                    const ParamID mapped = sidMappedRealtimeCcParam((uint8_t)midiControllerNumber);
                    if (mapped != (ParamID)kNumParams) {
                        id = (Steinberg::Vst::ParamID)mapped;
                        return kResultOk;
                    }
                }
                return kResultFalse;
            }
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

    int32 PLUGIN_API getSelectedUnit() override { return selectedUnit_; }

    tresult PLUGIN_API selectUnit(UnitID unitId) override {
        for (const auto& u : units_)
            if (u.id == unitId) {
                selectedUnit_ = unitId;
                return kResultOk;
            }
        return kResultFalse;
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
                // Host program lists / program-change messages drive this;
                // setParamNormalized() turns it into a factory patch load.
                flags = ParameterInfo::kIsProgramChange | ParameterInfo::kIsList;
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
                unitForParam_(i));
            parameters.addParameter(rp);
        }
    }

    // ── Unit / Program-list ─────────────────────────────────────────────────
    // Units: the root unit owns the factory program list (and the Program /
    // BankSlot selectors); below it one unit per editor tab, in tab order,
    // and one for host-driven MIDI mirrors and read-only values. Hosts that
    // show units group the 512 parameters the way the editor does.
    static constexpr UnitID kFirstTabUnitId_ = 1;
    static constexpr UnitID kHostUnitId_ = kFirstTabUnitId_ + (UnitID)ArpSID::GUI::kTabCount;
    // Sub-units of kHostUnitId_: one per MIDI controller type (16 channels
    // each), in the order of the kParamHostCtrl*Base blocks.
    static constexpr int kHostCtrlKinds_ = ((int)kParamHostCtrlLast - (int)kParamHostCtrlModWheelBase + 1) / 16;
    static constexpr UnitID kFirstHostCtrlUnitId_ = kHostUnitId_ + 1;

    static UnitID unitForParam_(int id) noexcept {
        if (id == (int)kParamProgram || id == (int)kParamBankSlot) return kRootUnitId;
        const int tab = ArpSID::GUI::EditorLayout::tabIndexForParam(id);
        if (tab >= 0) return kFirstTabUnitId_ + (UnitID)tab;
        if (id >= (int)kParamHostCtrlModWheelBase && id <= (int)kParamHostCtrlLast)
            return kFirstHostCtrlUnitId_ + (UnitID)((id - (int)kParamHostCtrlModWheelBase) / 16);
        return kHostUnitId_;
    }

    void buildUnitInfo_() {
        UnitInfo root;
        root.id             = kRootUnitId;
        root.parentUnitId   = kNoParentUnitId;
        root.programListId  = kProgramListId_;
        utf8ToTChar("ArpSID", root.name, 128);
        units_.push_back(root);
        const auto& tabs = ArpSID::GUI::EditorLayout::tabs();
        for (std::size_t t = 0; t < tabs.size(); ++t) {
            UnitInfo u;
            u.id = kFirstTabUnitId_ + (UnitID)t;
            u.parentUnitId = kRootUnitId;
            u.programListId = kNoProgramListId;
            utf8ToTChar(ArpSID::GUI::tabSpec(tabs[t].id).displayName, u.name, 128);
            units_.push_back(u);
        }
        UnitInfo host;
        host.id = kHostUnitId_;
        host.parentUnitId = kRootUnitId;
        host.programListId = kNoProgramListId;
        utf8ToTChar("Host MIDI / read-only", host.name, 128);
        units_.push_back(host);
        static const char* const kCtrlNames[] = {"MIDI Mod Wheel", "MIDI Breath", "MIDI Expression",
                                                 "MIDI Sustain", "MIDI Sostenuto", "MIDI Channel Pressure",
                                                 "MIDI Pitch Bend", "MIDI RPN MSB", "MIDI RPN LSB",
                                                 "MIDI NRPN MSB", "MIDI NRPN LSB", "MIDI Data Entry MSB",
                                                 "MIDI Data Entry LSB"};
        static_assert(sizeof(kCtrlNames) / sizeof(kCtrlNames[0]) == (size_t)kHostCtrlKinds_,
                      "one unit name per host controller block");
        for (int k = 0; k < kHostCtrlKinds_; ++k) {
            UnitInfo u;
            u.id = kFirstHostCtrlUnitId_ + (UnitID)k;
            u.parentUnitId = kHostUnitId_;
            u.programListId = kNoProgramListId;
            utf8ToTChar(kCtrlNames[k], u.name, 128);
            units_.push_back(u);
        }
    }

    static constexpr ProgramListID kProgramListId_ = 1;
    // Canonical factory preset count: 180 slots, not legacy 128.
    static constexpr int32         kMaxPresets_     = ArpSID::kCanonicalFactoryPatchSlotCount;

    void loadFactoryPatch_(int slot) {
        slot = std::clamp(slot, 0, kCanonicalFactoryPatchSlotMax);
        loadedFactorySlot_ = slot;
        if (IPtr<IMessage> msg = owned(allocateMessage())) {
            msg->setMessageID(kVstMsgLoadFactoryPatch);
            msg->getAttributes()->setInt(kVstMsgAttrSlot, slot);
            sendMessage(msg);
        }
        const SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
        if (root.valid())
            mirrorStateRootToParameters_(root, /*notifyHost*/ true);
    }

    // Copy a state root's host-visible parameter values into the controller.
    // Guarded so the Program/BankSlot writes do not re-trigger a patch load.
    void mirrorStateRootToParameters_(const SidStateRootV1& root, bool notifyHost) {
        std::array<float, kNumParams> params{};
        exportPersistentPresentationParamsFromStateRoot(root, params.data(), kNumParams);
        mirroringState_ = true;
        for (int i = 0; i < kNumParams; ++i)
            EditController::setParamNormalized((Steinberg::Vst::ParamID)i, (ParamValue)params[(size_t)i]);
        mirroringState_ = false;
        loadedFactorySlot_ = canonicalFactorySlotFromNormalizedBankSlot(params[(size_t)kParamBankSlot]);
        if (notifyHost && componentHandler)
            componentHandler->restartComponent(kParamValuesChanged);
    }

    static int64 currentProcessId_() noexcept {
#if defined(_WIN32)
        return (int64)_getpid();
#else
        return (int64)getpid();
#endif
    }

    UnitID selectedUnit_ = kRootUnitId;
    double editorZoom_ = 1.0;
    bool mirroringState_ = false;
    int loadedFactorySlot_ = -1;
    Vst3KernelHost* kernelHost_ = nullptr;

    void pushLiveParam_(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) noexcept {
        if (livePushFn_)
            livePushFn_(livePushUser_, id, value);
    }

    PresetLiveParamPushFn livePushFn_ = nullptr;
    void* livePushUser_ = nullptr;
    Steinberg::IPtr<Steinberg::IPlugView> editorView_;
    std::vector<UnitInfo> units_;
};

Vst3KernelHost* arpsidControllerKernelHost(void* editController) noexcept {
    auto* c = static_cast<ArpSIDControllerPhase3*>(static_cast<EditController*>(editController));
    return c ? c->kernelHost() : nullptr;
}

void arpsidControllerMarkStateDirty(void* editController) noexcept {
    if (auto* c = static_cast<ArpSIDControllerPhase3*>(static_cast<EditController*>(editController)))
        c->markStateDirty();
}

void arpsidControllerSelectFactoryPatch(void* editController, int slot) noexcept {
    if (auto* c = static_cast<ArpSIDControllerPhase3*>(static_cast<EditController*>(editController)))
        c->selectFactoryPatch(slot);
}

int arpsidControllerLoadedFactorySlot(void* editController) noexcept {
    auto* c = static_cast<ArpSIDControllerPhase3*>(static_cast<EditController*>(editController));
    return c ? c->loadedFactorySlot() : 0;
}

void arpsidControllerSendUiMidi(void* editController, unsigned char status, unsigned char data1,
                                unsigned char data2) noexcept {
    if (auto* c = static_cast<ArpSIDControllerPhase3*>(static_cast<EditController*>(editController)))
        c->sendUiMidi(status, data1, data2);
}

double arpsidControllerEditorZoom(void* editController) noexcept {
    auto* c = static_cast<ArpSIDControllerPhase3*>(static_cast<EditController*>(editController));
    return c ? c->editorZoom() : 1.0;
}

void arpsidControllerSetEditorZoom(void* editController, double zoom) noexcept {
    if (auto* c = static_cast<ArpSIDControllerPhase3*>(static_cast<EditController*>(editController)))
        c->setEditorZoom(zoom);
}

} // namespace ArpSID
