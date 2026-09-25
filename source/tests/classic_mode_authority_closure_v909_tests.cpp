// classic_mode_authority_closure_v909_tests.cpp
//
// v909 Classic-mode authority closure. Guards the four audit contracts:
//
// 1) Classic/BitPerfect authority: a GM drum note (MIDI channel 10, notes
//    35..81) must NOT silently promote the runtime into DrSID. Promotion now
//    requires an explicit wrapper allow flag derived from the component
//    flavor law plus the new kParamAutoGmDrumPromotion opt-in (default OFF).
//    Behavioral: channel 10 + note 36 with promotion disallowed leaves
//    kParamDrSidEnable untouched.
// 2) No-output behavioral continuity: engine + shared post-FX state must
//    advance identically whether a block renders into host buffers or into
//    scratch (no-output) buffers, and Phase2 must capture telemetry meters
//    from the scratch render instead of publishing silence.
// 3) Telemetry truth: Phase2 must publish explicit projection-mirror
//    availability (projectionMirrorAvailable / projectionMirrorBackend) and
//    no-output provenance flags instead of silently no-oping.
// 4) The HMOS-II 8580 R5 class SID chip is the default in all wrappers,
//    flavors and factory patches.

#include "arpsid/core/sid_runtime_host_policy.h"
#include "arpsid/core/sid_runtime_model.h"
#include "arpsid/core/sid_runtime_forensic_config.h"
#include "arpsid/core/sid_audio_processors.h"
#include "arpsid/engines/sid808_engine.h"
#include "arpsid/patchbank/factory_sid808_kits.h"
#include "parameter_ids.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "classic_mode_authority_closure_v909_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}



bool isPreservedPass380ClosureLineageVersion(const std::string& version) {
    // v918: keep old closure tests release-forward. These tests verify that
    // their original contracts are preserved by the current pass380 closure
    // train, not that VERSION.txt remains pinned to an obsolete v90x label.
    return version.find("0.0.690-pass380-v") != std::string::npos &&
           version.find("closure") != std::string::npos;
}

void requireContains(const std::string& s, const char* needle, const char* msg) {
    require(s.find(needle) != std::string::npos, msg);
}

// Mock canonical promotion target: records staged/applied parameter writes.
struct V909PromotionTarget {
    std::array<float, ArpSID::kNumParams> values{};
    int stageCalls = 0;
    int applyCalls = 0;
    void runtimeStageNormalizedParameterOnly(uint32_t target, float value) noexcept {
        ++stageCalls;
        if (target < values.size()) values[target] = value;
    }
    void runtimeApplyNormalizedParameter(uint32_t target, float value) noexcept {
        ++applyCalls;
        if (target < values.size()) values[target] = value;
    }
};

void test_classic_channel10_note36_never_promotes_without_optin() {
    using namespace ArpSID;
    // The exact audit regression: Classic mode + MIDI channel 10 (index 9) +
    // note 36 must not enable DrSID unless Auto GM Drum Promotion is enabled.
    V909PromotionTarget t;
    const bool classicDefaultAllow = sidCanonicalGMDrumAutoPromotionAllowed(
        false /* not a dedicated drum flavor */,
        true /* Hybrid (Classic) flavor */,
        defaultNormalizedParamValue(kParamAutoGmDrumPromotion) > 0.5f);
    require(!classicDefaultAllow,
            "Classic flavor with default parameters must not allow GM auto-promotion");
    const bool promoted = sidCanonicalApplyGMDrSidPromotion(
        t, 9u, 36u, true /* DrSID engine available */, false /* not DrSID */, classicDefaultAllow);
    require(!promoted, "channel-10 note 36 must not promote in default Classic mode");
    require(t.stageCalls == 0 && t.applyCalls == 0,
            "refused promotion must not stage or apply any parameter");
    require(t.values[(size_t)kParamDrSidEnable] == 0.0f,
            "kParamDrSidEnable must remain 0 after refused promotion");
}

void test_explicit_optin_still_promotes() {
    using namespace ArpSID;
    V909PromotionTarget t;
    const bool allow = sidCanonicalGMDrumAutoPromotionAllowed(false, true, true /* param on */);
    require(allow, "Hybrid flavor with kParamAutoGmDrumPromotion enabled must allow promotion");
    const bool promoted = sidCanonicalApplyGMDrSidPromotion(t, 9u, 36u, true, false, allow);
    require(promoted, "explicit opt-in must preserve the GM promotion feature");
    require(t.values[(size_t)kParamDrSidEnable] > 0.5f, "opt-in promotion enables DrSID");
    require(t.values[(size_t)kParamSynthModeEnable] < 0.5f, "opt-in promotion disables SynthMode");
    require(t.stageCalls == 2 && t.applyCalls == 2,
            "opt-in promotion stages and applies exactly the two mode parameters");
}

void test_runtime_model_arp_effective_authority_rejects_synthmode() {
    using namespace ArpSID;
    SidRuntimeModel model;
    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamArpEnable), 1.0f);
    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamSynthModeEnable), 0.0f);
    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamDrSidEnable), 0.0f);
    require(model.isArpEnabled(), "ARP is effective in classic/BitPerfect mode when enabled");
    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamSynthModeEnable), 1.0f);
    require(!model.isArpEnabled(), "SynthMode must mask stale raw ArpEnable in shared runtime model");
    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamSynthModeEnable), 0.0f);
    (void)model.applyAutomationPoint(static_cast<uint32_t>(kParamDrSidEnable), 1.0f);
    require(!model.isArpEnabled(), "DrSID must mask stale raw ArpEnable in shared runtime model");
}

void test_flavor_law_matrix() {
    using namespace ArpSID;
    // Dedicated drum flavors (DrumMachine / SID-808) always allow.
    require(sidCanonicalGMDrumAutoPromotionAllowed(true, false, false),
            "dedicated drum flavors always allow GM promotion");
    // Hybrid requires the explicit param.
    require(!sidCanonicalGMDrumAutoPromotionAllowed(false, true, false),
            "Hybrid without opt-in never allows GM promotion");
    require(sidCanonicalGMDrumAutoPromotionAllowed(false, true, true),
            "Hybrid with opt-in allows GM promotion");
    // Instrument / C64SidPlayer flavors never allow, even with the param on.
    require(!sidCanonicalGMDrumAutoPromotionAllowed(false, false, true),
            "non-Hybrid non-drum flavors never allow GM promotion");
    // Candidate window is unchanged: channel 10 (index 9), notes 35..81.
    require(sidCanonicalGMDrumPromotionCandidate(9u, 35u), "note 35 stays a GM candidate");
    require(sidCanonicalGMDrumPromotionCandidate(9u, 81u), "note 81 stays a GM candidate");
    require(!sidCanonicalGMDrumPromotionCandidate(9u, 34u), "note 34 not a GM candidate");
    require(!sidCanonicalGMDrumPromotionCandidate(9u, 82u), "note 82 not a GM candidate");
    require(!sidCanonicalGMDrumPromotionCandidate(0u, 36u), "channel 1 not a GM candidate");
    // And even a perfect candidate is refused without wrapper authority.
    require(!sidCanonicalEvaluateGMDrSidPromotion(9u, 36u, true, false, false).promote,
            "perfect GM candidate is refused when auto-promotion is disallowed");
}

void test_auto_gm_promotion_param_contract() {
    using namespace ArpSID;
    require(kNumParams == 512, "v909 appends exactly one parameter (512 total)");
    require((int)kParamAutoGmDrumPromotion + 1 == kNumParams,
            "kParamAutoGmDrumPromotion is the appended last ParamID");
    require(defaultNormalizedParamValue(kParamAutoGmDrumPromotion) == 0.0f,
            "Auto GM Drum Promotion defaults OFF");
    require(std::strcmp(kParamInfos[(size_t)kParamAutoGmDrumPromotion].name,
                        "Auto GM Drum Promotion") == 0,
            "parameter is host-visible under its canonical name");
    require(isPatchPersistentParam(kParamAutoGmDrumPromotion),
            "opt-in persists with the plugin state");
    require(!isRuntimeOnlyOrTransientParam(kParamAutoGmDrumPromotion),
            "opt-in is not transient");
    require(!isFactoryPatchAudioAuthorityParam(kParamAutoGmDrumPromotion),
            "factory patches must not override the user's promotion opt-in");
}


void test_synthmode_transport_reset_authority_source_contract() {
    using namespace ArpSID;
    require(isTransportResetStructuralAuthorityParam(kParamProgram),
            "transport reset skips stale Program mirror");
    require(isTransportResetStructuralAuthorityParam(kParamBankSlot),
            "transport reset skips stale BankSlot mirror");
    require(isTransportResetStructuralAuthorityParam(kParamSynthModeEnable),
            "transport reset skips stale SynthModeEnable structural authority");
    require(isTransportResetStructuralAuthorityParam(kParamDrSidEnable),
            "transport reset skips stale DrSidEnable structural authority");
    require(!isTransportResetStructuralAuthorityParam(kParamMasterVolume),
            "transport reset still preserves ordinary persistent audio parameters");

    const std::string kernel = readFile(ARPSID_SOURCE_ROOT "/source/au3/ArpSIDDSPKernel.hpp");
    requireContains(kernel, "const bool structuralSynthModeAuthority =",
                    "kernel must compute SynthMode structural reset authority");
    requireContains(kernel, "const bool snapshotModeAuthorityAllowed = !hasExplicitFactorySlot;",
                    "explicit factory reset must not treat stale host snapshot mode bits as structural authority");
    requireContains(kernel, "const bool explicitFactoryStructuralModeAuthority =",
                    "explicit factory structural mode authority must arbitrate SynthMode vs DrSID");
    requireContains(kernel, "? factoryRootDrSidAuthority",
                    "explicit factory DrSID authority must beat stale contradictory snapshot authority");
    requireContains(kernel, "? factoryRootSynthModeAuthority",
                    "explicit factory SynthMode authority must beat stale contradictory snapshot authority");
    requireContains(kernel, "factoryRootSynthModeAuthority",
                    "SynthMode authority comes from live state, factory root or Instrument flavor");
    requireContains(kernel, "runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamDrSidEnable), 0.0f);\n            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSynthModeEnable), 1.0f);\n            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);\n            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);\n            runtimeModel_.setArpActiveFlag(false);",
                    "SynthMode structural reset must reject stale DrSID/ARP/SEQ authority together");
    requireContains(kernel, "(factoryRootApplied && factoryRootSynthModeAuthority) ? slot : preResetStickyBankSlot",
                    "SynthMode reset preserves explicit factory slot instead of stale slot 0");
    requireContains(kernel, "const bool factoryRootBitPerfectAuthority =",
                    "BitPerfect/Classic factory roots must become explicit structural authority, not fallback else-branch");
    requireContains(kernel, "const bool structuralBitPerfectAuthority =",
                    "BitPerfect/Classic transport reset must reject stale ARP/SEQ host snapshots");
    requireContains(kernel, "} else if (structuralBitPerfectAuthority) {",
                    "BitPerfect/Classic reset must have a concrete authority block");
    requireContains(kernel, "runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSynthModeEnable), 0.0f);\n            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamDrSidEnable), 0.0f);\n            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);\n            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);",
                    "BitPerfect/Classic structural reset must force Synth/DrSID/ARP/SEQ off together");
    requireContains(kernel, "if (BitPerfectEngine* bpe = bpe_()) bpe->allNotesOff();",
                    "BitPerfect/Classic structural reset must reset direct note authority state");
    requireContains(kernel, "const int preservedDrSidSlot = ArpSID::canonicalFactorySlotForRoot(",
                    "DrSID reset computes preserved explicit/pre-reset slot authority");
    requireContains(kernel, "(factoryRootApplied && factoryRootDrSidAuthority && !factoryRootSynthModeAuthority)",
                    "DrSID reset preserves explicit factory slot when factory root is DrSID authority");
    require(kernel.find("runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(pid), v);\n                        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(pid), v);") == std::string::npos,
            "DrSID live authority replay must not duplicate stage the same parameter");
    const auto authorityArrayPos = kernel.find("kDrSidTransportAuthorityParams{{");
    require(authorityArrayPos != std::string::npos,
            "DrSID transport authority list must be explicit and auditable");
    const auto authorityArrayEnd = kernel.find("}};", authorityArrayPos);
    require(authorityArrayEnd != std::string::npos,
            "DrSID transport authority list must have a closed initializer");
    const std::string authorityArray = kernel.substr(authorityArrayPos, authorityArrayEnd - authorityArrayPos);
    require(authorityArray.find("kParamSynthModeEnable") == std::string::npos,
            "SynthModeEnable must not live in DrSID kit-parameter replay authority");
    require(authorityArray.find("kParamDrSidEnable") == std::string::npos,
            "DrSidEnable must not live in DrSID kit-parameter replay authority");
    requireContains(kernel, "static constexpr std::array<int, 17> kDrSidTransportAuthorityParams{{",
            "DrSID transport authority list contains only the 17 non-mode DrSID kit parameters");
    requireContains(kernel, "// v924: structural mode bits are staged exactly once in this\n            // authority block.",
                    "DrSID structural mode authority must be staged exactly once outside kit replay");
    requireContains(kernel, "const bool replayLiveDrSidAuthority =\n            !transportResetOverlayApplied",
                    "v925 DrSID replay must not clobber an already-applied transport snapshot overlay");
    requireContains(kernel, "} else if (!transportResetOverlayApplied &&\n                factoryRootApplied && factoryRootDrSidAuthority",
                    "factory DrSID replay must also be suppressed after transport snapshot overlay");
    require(kernel.find("if (replayLiveDrSidAuthority)") < kernel.find("} else if (!transportResetOverlayApplied &&"),
            "DrSID live replay and factory replay branches must remain mutually exclusive");
    require(kernel.find("runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSynthModeEnable), 0.0f);\n                runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamDrSidEnable), 1.0f);") == std::string::npos,
            "DrSID replay sub-branches must not re-stage structural mode bits");
    requireContains(kernel, "SID-808 flavor only adds the model override",
                    "SID-808 flavor special-case must not duplicate structural mode staging");
    requireContains(kernel, "dirty_[(size_t)kParamBankSlot].store(false, std::memory_order_relaxed);\n            dirty_[(size_t)kParamProgram].store(false, std::memory_order_relaxed);\n            dirty_[(size_t)kParamSynthModeEnable].store(false, std::memory_order_relaxed);",
                    "DrSID structural reset must clear dirty BankSlot/Program before mode dirty flags");
    requireContains(kernel, "case ArpSID::ComponentFlavor::Instrument:\n                // Dedicated instrument flavor is the classic Synth/SidRegister",
                    "Instrument flavor is documented as Synth/SidRegister authority");
    requireContains(kernel, "forceParam(kParamSynthModeEnable, 1.0f);",
                    "Instrument flavor forces SynthModeEnable on");
    requireContains(kernel, "ArpSID::isTransportResetStructuralAuthorityParam(i)",
                    "transport reset overlay uses reset-specific structural authority filter");
    const std::string noteSurface = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/core/sid_runtime_note_surface.h");
    requireContains(noteSurface, "if (t.runtimeIsSynthModeEnabled()) {",
                    "AU3 internal handleNoteOn path must test SynthMode before BitPerfect/ARP fallback");
    requireContains(noteSurface, "ev.type = SidTimedEventType::MidiNoteOn;",
                    "AU3 internal handleNoteOn path must route SynthMode notes to SID-register scheduling, not BitPerfect");
    requireContains(noteSurface, "t.kernelSynthNoteOn(ev);",
                    "AU3 internal SynthMode NoteOn must call kernelSynthNoteOn");
    requireContains(noteSurface, "ev.type = SidTimedEventType::MidiNoteOff;",
                    "AU3 internal SynthMode NoteOff must construct canonical SID timed event");
    requireContains(noteSurface, "t.kernelSynthNoteOff(ev);",
                    "AU3 internal SynthMode NoteOff must call kernelSynthNoteOff");
    require(noteSurface.find("if (!t.runtimeHasBitPerfectEngine()) return;\n    if (t.runtimeIsArpEnabled()") == std::string::npos,
            "AU3 internal note path must not guard SynthMode behind BitPerfect engine availability");
    require(noteSurface.find("if (t.runtimeIsSynthModeEnabled())") < noteSurface.find("if (t.runtimeIsArpEnabled() && t.runtimeHasArpEngine())"),
            "AU3 internal note path must give SynthMode authority priority over stale ArpEnable");
    const std::string sharedKernel = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/core/sid_runtime_shared_kernel.h");
    require(sharedKernel.find("if (runtime.isSynthModeEnabled()) { surface.synthNoteOn(ev); return; }") <
            sharedKernel.find("if (runtime.isArpEnabled()) { surface.arpNoteOn"),
            "canonical host MIDI path must give SynthMode authority priority over ArpEnable");
    require(sharedKernel.find("if (runtime.isSynthModeEnabled()) {\n        surface.synthNoteOff(ev);\n        return;\n    }") <
            sharedKernel.find("if (runtime.isArpEnabled()) {\n        surface.arpNoteOff"),
            "canonical NoteOff path must release SynthMode before Arp fallback");

    const std::string phase2 = readFile(ARPSID_SOURCE_ROOT "/source/arpsid_processor_phase2.cpp");
    requireContains(phase2, "const bool arpMode = ArpSID::sidEffectiveArpAuthorityFromLiveParams(paramValues);",
                    "Phase2/VST virtual-gate arp authority must use shared effective ARP helper");
    requireContains(kernel, "return ArpSID::sidEffectiveArpAuthorityFromLiveParams(renderParams_);",
                    "AU3 runtimeIsArpEnabled must delegate to shared effective ARP authority helper");
    const std::string runtimeModel = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/core/sid_runtime_model.h");
    requireContains(runtimeModel, "inline bool sidEffectiveArpAuthorityFromLiveParams",
                    "shared runtime model must expose effective ARP authority helper");
    requireContains(runtimeModel, "inline bool sidEffectiveSeqAuthorityFromLiveParams",
                    "shared runtime model must expose effective SEQ authority helper");
    requireContains(kernel, "const bool directSynth =\n            ArpSID::sidResolveRenderModeFromLiveParams(renderParams_) == ArpSID::SidRuntimeRenderMode::SidRegister;",
                    "AU3 SynthMode orphan cleanup must not be disabled by stale raw ArpEnable/SeqEnable");
    requireContains(kernel, "const auto directPolyMode = ArpSID::sidResolveRenderModeFromLiveParams(renderParams_);",
                    "AU3 BitPerfect orphan cleanup must be gated by resolved render mode, not raw DrSID flag");
    requireContains(kernel, "const bool effectiveDirectPolyArp = ArpSID::sidEffectiveArpAuthorityFromLiveParams(renderParams_);",
                    "AU3 BitPerfect cleanup must use shared effective ARP authority helper");
    requireContains(kernel, "const bool effectiveDirectPolySeq = ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_);",
                    "AU3 BitPerfect cleanup must use shared effective SEQ authority helper");
    requireContains(kernel, "const bool directPoly =\n                directPolyMode == ArpSID::SidRuntimeRenderMode::BitPerfect &&\n                bpe->voiceModeIndex() == 0 &&\n                !effectiveDirectPolyArp &&\n                !effectiveDirectPolySeq;",
                    "AU3 BitPerfect direct-poly cleanup must not be poisoned by stale ARP/SEQ outside BitPerfect authority");
    requireContains(phase2, "const bool directSynth =\n        resolveTopLevelRenderMode_() == ArpSID::SidRuntimeRenderMode::SidRegister;",
                    "Phase2 SynthMode orphan cleanup must not be disabled by stale raw ArpEnable/SeqEnable");
    requireContains(kernel, "runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);\n                runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);\n                runtimeModel_.setArpActiveFlag(false);",
                    "AU3 entering SynthMode must clear stale ARP/SEQ authority immediately");
    requireContains(phase2, "runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);\n        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);",
                    "Phase2 entering SynthMode must clear stale ARP/SEQ authority immediately through canonical staging");
    requireContains(phase2, "runtimeModel_.setArpActiveFlag(ArpSID::sidEffectiveArpAuthorityFromLiveParams(paramValues));",
                    "Phase2 runtimeModel arp-active telemetry must use shared effective ARP helper");
    const std::string backendProjection = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/core/sid_runtime_backend_projection.h");
    requireContains(backendProjection, "const bool effectiveArpAuthority = sidEffectiveArpAuthorityFromLiveParams(params);",
                    "backend projection must use shared effective ARP authority helper");
    requireContains(backendProjection, "const bool willArpEnable = effectiveArpAuthority;",
                    "backend projection must not enable ARP from raw stale ArpEnable");
    requireContains(backendProjection, "if (bank.arp && effectiveArpAuthority && hostTempo > 1.0)",
                    "tempo sync must not advance ARP as active authority under SynthMode/DrSID");
    const std::string renderHost = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/core/sid_runtime_render_host.h");
    requireContains(renderHost, "if (bank.bitPerfect) bank.bitPerfect->allNotesOff();",
                    "mode transitions must silence latent BitPerfect voices");
    requireContains(renderHost, "target.runtimeModel().setSeqSamplesUntilStep(-1.0);",
                    "mode transitions must clear sequencer countdown state");
    requireContains(renderHost, "target.runtimeModel().setSeqStep(0);",
                    "mode transitions must clear sequencer step state");
    const std::string paramServices = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/core/sid_runtime_parameter_services.h");
    requireContains(paramServices, "runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamArpEnable), 0.0f);\n            runtimeStageNormalizedParameter(target, static_cast<uint32_t>(kParamSeqEnable), 0.0f);",
                    "DrSID/Synth special-param activation must clear raw ARP/SEQ authority");
    // v965 telemetry rework: AU3 publishes the effective ARP authority into an
    // atomic (telemetryArpEnabled_) using the shared effective-authority helper,
    // and the telemetry snapshot reads that published atomic instead of a live
    // local. The invariant is unchanged — telemetry ARP authority derives from
    // sidEffectiveArpAuthorityFromLiveParams, never raw kParamArpEnable.
    requireContains(kernel, "telemetryArpEnabled_.store(ArpSID::sidEffectiveArpAuthorityFromLiveParams(renderParams_) ? 1u : 0u,",
                    "AU3 telemetry ARP authority must be published from the shared effective ARP helper");
    requireContains(kernel, "t.arpEnabled = telemetryArpEnabled_.load(std::memory_order_relaxed) != 0;",
                    "AU3 telemetry snapshot must read the published effective-ARP atomic");
    requireContains(kernel, "if(arp_() && runtimeIsArpEnabled())\n            telemetryArpStep_.store(arp_()->getCurrentStep(), std::memory_order_relaxed);\n        else\n            telemetryArpStep_.store(0, std::memory_order_relaxed);",
                    "AU3 telemetry ARP step must clear when ARP is not effective authority");
    requireContains(phase2, "const bool fullEffectiveArpAuthority = arpeggiator_() &&\n            ArpSID::sidEffectiveArpAuthorityFromLiveParams(paramValues);",
                    "Phase2 full telemetry must use shared effective ARP helper");
    require(phase2.find("} else if (liveCh >= 0 && synthMode) {\n            // v929: Phase2/VST virtual-gate uses the same render-mode authority") <
            phase2.find("} else if (arpMode) {\n            if (arpeggiator_()) arpeggiator_()->noteOn"),
            "Phase2/VST virtual-gate NoteOn must route SynthMode before Arp fallback");
    require(phase2.find("if (liveCh >= 0 && synthMode) {\n            synthModeNoteOff") <
            phase2.find("} else if (arpMode) {\n            if (arpeggiator_()) arpeggiator_()->noteOff"),
            "Phase2/VST virtual-gate NoteOff must release SynthMode before Arp fallback");
    const std::string au3 = readFile(ARPSID_SOURCE_ROOT "/source/au3/ArpSIDAudioUnit.mm");
    requireContains(au3, "case ArpSID::ComponentFlavor::Instrument:\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 1.0f);",
                    "AU3 Instrument state-root flavor must persist SynthModeEnable on");
    requireContains(kernel, "case ArpSID::ComponentFlavor::Instrument:\n                // Dedicated instrument flavor is the classic Synth/SidRegister\n                // instrument surface, not BitPerfect fallback. Keep this\n                // structural mode stable across host reset snapshots.\n                forceParam(kParamDrSidEnable, 0.0f);\n                forceParam(kParamSynthModeEnable, 1.0f);\n                forceParam(kParamArpEnable, 0.0f);\n                forceParam(kParamSeqEnable, 0.0f);",
                    "AU3 Instrument render flavor policy must clear stale ARP and SEQ together");
    requireContains(au3, "case ArpSID::ComponentFlavor::Instrument:\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 1.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 0.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamArpEnable, 0.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSeqEnable, 0.0f);",
                    "AU3 Instrument state-root flavor must persist SEQ disabled with SynthMode authority");
    const std::string au2 = readFile(ARPSID_SOURCE_ROOT "/source/au2/ArpSIDAUv2Component.mm");
    requireContains(au2, "case ArpSID::ComponentFlavor::Instrument:\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 1.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 0.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamArpEnable, 0.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSeqEnable, 0.0f);",
                    "AUv2 Instrument state-root flavor must persist SEQ disabled with SynthMode authority");


    requireContains(kernel, "case ArpSID::ComponentFlavor::DrumMachine:\n                forceParam(kParamSynthModeEnable, 0.0f);\n                forceParam(kParamDrSidEnable, 1.0f);\n                forceParam(kParamArpEnable, 0.0f);\n                // v949: dedicated drum flavor owns DrSID/SID808 pattern",
                    "AU3 DrumMachine render flavor policy must preserve SEQ for drum sequencer while clearing ARP");
    requireContains(kernel, "case ArpSID::ComponentFlavor::Sid808:\n                forceParam(kParamSynthModeEnable, 0.0f);\n                forceParam(kParamDrSidEnable, 1.0f);\n                forceParam(kParamArpEnable, 0.0f);\n                // v949: preserve SeqEnable for the SID808 drum sequencer.",
                    "AU3 SID808 render flavor policy must preserve SEQ for drum sequencer while clearing ARP");
    requireContains(au3, "DrumMachine flavor must not destructively clear a restored",
                    "AU3 DrumMachine state-root flavor policy must preserve restored SEQ");
    requireContains(au3, "SID808 flavor preserves SeqEnable as drum-pattern transport",
                    "AU3 Sid808 state-root flavor policy must preserve restored SEQ");
    requireContains(au2, "DrumMachine flavor must not destructively clear a restored",
                    "AUv2 DrumMachine state-root flavor policy must preserve restored SEQ");
    requireContains(au2, "SID808 flavor preserves SeqEnable as drum-pattern transport",
                    "AUv2 Sid808 state-root flavor policy must preserve restored SEQ");
    const std::string fileBank = readFile(ARPSID_SOURCE_ROOT "/source/au3/ArpSIDFileBankBridge.mm");
    requireContains(fileBank, "Preserve SeqEnable instead of forcing the DrSID/SID808 transport off",
                    "file-bank DrSID/drum canonicalization must preserve SEQ transport");
    requireContains(paramServices, "// v941: disabling a structural mode through host automation",
                    "special-param mode-disable path must clear stale ARP/SEQ for pure CLASSIC authority");
    requireContains(paramServices, "// v941: DrSID->CLASSIC via direct automation also creates a pure",
                    "DrSID disable path must clear stale ARP/SEQ instead of reviving SEQ/ARP");

    requireContains(au3, "ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamArpEnable, 0.0f);",
                    "AU3 Instrument state-root flavor must disable arp ambiguity");
    requireContains(au2, "case ArpSID::ComponentFlavor::Instrument:\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 1.0f);",
                    "AUv2 Instrument state-root flavor must persist SynthModeEnable on");
    requireContains(au3, "case ArpSID::ComponentFlavor::C64SidPlayer:\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 0.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 0.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamArpEnable, 0.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSeqEnable, 0.0f);",
                    "AU3 C64SidPlayer state-root flavor must clear SEQ with Synth/DrSID/ARP");
    requireContains(au2, "case ArpSID::ComponentFlavor::C64SidPlayer:\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 0.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 0.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamArpEnable, 0.0f);\n            ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSeqEnable, 0.0f);",
                    "AUv2 C64SidPlayer state-root flavor must clear SEQ with Synth/DrSID/ARP");
    const std::string vc = readFile(ARPSID_SOURCE_ROOT "/source/au3/ArpSIDViewController.mm");
    requireContains(vc, "const BOOL modeIsClassic = (idx == 0);",
                    "mode selector must explicitly identify CLASSIC/BitPerfect authority");
    requireContains(vc, "const float arp = 0.f;\n    const float seq = modeIsDrSid",
                    "mode selector must clear ARP but preserve DrSID/SID808 SEQ authority selection");
    requireContains(vc, "[self _storeCachedParamValue:ArpSID::kParamArpEnable value:arp];\n    [self _storeCachedParamValue:ArpSID::kParamSeqEnable value:seq];",
                    "mode selector cache must store ARP clear plus DrSID-aware SEQ authority");
    requireContains(vc, "if(arpParam) [arpParam setValue:arp originator:_tok]; else [_au setParameterValue:arp forID:ArpSID::kParamArpEnable];\n    if(seqParam) [seqParam setValue:seq originator:_tok]; else [_au setParameterValue:seq forID:ArpSID::kParamSeqEnable];",
                    "mode selector must write ARP clear plus DrSID-aware SEQ transport through AU bridge");
    requireContains(vc, "Instrument flavor must not infer mode from a stale GUI cache",
                    "Instrument GUI sync must not read stale SynthMode cache");
    requireContains(vc, "CLASSIC SID Player",
                    "UI labels must name CLASSIC as SID Player / BitPerfect style mode");
    requireContains(vc, "SYNTH / SID REG",
                    "UI labels must expose playable Synth/SID REG mode");
    requireContains(vc, "DR SID Drums",
                    "UI labels must name DrSID drum mode explicitly");
    requireContains(vc, "v931: Pure Instrument's visible/effective mode is locked",
                    "Instrument effective-mode UI must stay SYNTH / SID REG even with stale telemetry/cache");

    requireContains(vc, "-(BOOL)_effectiveArpAuthorityEnabledForModeIndex:(NSInteger)modeIndex",
                    "GUI must centralize effective ARP authority presentation");
    requireContains(vc, "if(_componentFlavor == ArpSID::ComponentFlavor::Instrument) return NO;",
                    "GUI ARP presentation must suppress stale ARP in Pure Instrument");
    requireContains(vc, "if(modeIndex != 0) return NO;",
                    "GUI ARP presentation must only expose ARP authority in CLASSIC/BitPerfect mode");
    requireContains(vc, "const float arpEnableNorm = [self _effectiveArpAuthorityEnabledForModeIndex:modeIndex] ? 1.0f : 0.0f;",
                    "presentation line must use effective ARP authority, not raw stale cache");
    requireContains(vc, "BOOL arpOn=[self _effectiveArpAuthorityEnabledForModeIndex:[self _effectiveModeIndexFromTelemetry:&tel]];",
                    "ARP step view must mirror effective ARP authority, not raw stale cache");
    requireContains(vc, "-(BOOL)_effectiveSeqAuthorityEnabledForModeIndex:(NSInteger)modeIndex",
                    "GUI must centralize effective SEQ authority presentation");
    requireContains(vc, "v949: GUI mirrors DSP law. SEQ is valid as the melodic Classic/",
                    "GUI SEQ presentation must document Classic plus DrSID/SID808 authority");
    requireContains(vc, "if(modeIndex == 0 || modeIndex == 2)\n        return [self _cachedParamValue:ArpSID::kParamSeqEnable defaultCenter:0.0f] > 0.5f;",
                    "GUI SEQ presentation must allow CLASSIC and DrSID/SID808 sequencer authority");
    requireContains(vc, "const float seqEnableNorm = [self _effectiveSeqAuthorityEnabledForModeIndex:modeIndex] ? 1.0f : 0.0f;",
                    "presentation line must use effective SEQ authority, not raw stale cache");
    requireContains(vc, "const BOOL seqOn=tel.seqEnabled;",
                    "SEQ step view must mirror telemetry effective SEQ authority, not raw stale cache");
    requireContains(kernel, "seqEngine_.setEnabled(ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_));",
                    "AU3 sequencer engine must use shared BitPerfect-only SEQ helper");
    requireContains(kernel, "const bool seqEnabled = ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_);",
                    "AU3 sequencer advancement must use shared BitPerfect-only SEQ helper");
    // v965 telemetry rework: AU3 publishes the effective SEQ authority into the
    // telemetrySeqEnabled_ atomic via the shared helper, and the telemetry
    // snapshot reads that atomic (mirrors the ARP path above).
    requireContains(kernel, "telemetrySeqEnabled_.store(ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_) ? 1u : 0u,",
                    "AU3 telemetry SEQ authority must be published from the shared effective SEQ helper");
    requireContains(kernel, "t.seqEnabled = telemetrySeqEnabled_.load(std::memory_order_relaxed) != 0;",
                    "AU3 telemetry snapshot must read the published effective-SEQ atomic");
    requireContains(phase2, "full.seqEnabled = ArpSID::sidEffectiveSeqAuthorityFromLiveParams(paramValues);",
                    "Phase2 telemetry must use shared effective SEQ helper");
    requireContains(phase2, "const bool seqEnabled = ArpSID::sidEffectiveSeqAuthorityFromLiveParams(paramValues);",
                    "Phase2 sequencer must use shared effective SEQ helper");


}

void test_hmos_8580_class_default_everywhere() {
    using namespace ArpSID;
    require(kSidChipRevisionDefaultIndex == 3, "default chip selector index is the 8580 R5 slot");
    const float def = defaultNormalizedParamValue(kParamSidChipRevision);
    require(sidChipRevisionIndexFromNormalized(def) == 3,
            "kParamSidChipRevision default resolves to selector index 3");
    require(std::strcmp(sidChipRevisionSelectorLabel(def), "MOS 8580 R5") == 0,
            "default chip label is MOS 8580 R5 (HMOS-II class)");
    require(!sidChipRevisionSelectorIs6581(def), "default chip is not a 6581");
    require(sidChipRevisionSelectorRevision(def) == 5u, "default chip revision byte is R5");
    // Legacy mirrors follow: kParamSidModel 1.0 == 8580, forensic revision R5.
    require(defaultNormalizedParamValue(kParamSidModel) >= 0.5f,
            "legacy SID model mirror defaults to 8580");
    require(sidForensicRevisionFromNormalized(
                defaultNormalizedParamValue(kParamForensicRevision)) == 5u,
            "forensic revision legacy mirror defaults to R5");
    // ADSR-bug quirk stays off by default (it is a 6581 behavior).
    require(defaultNormalizedParamValue(kParamSidAdsrBug6581) < 0.5f,
            "6581 ADSR bug quirk defaults off with the 8580-class chip");
}

// No-output behavioral continuity: the same engine sequence must produce
// bit-identical audio whether the middle block renders into a "host" buffer
// or into a discarded scratch buffer — destination must not affect state.
void test_nooutput_scratch_block_is_state_continuous() {
    using namespace ArpSID;
    constexpr int kFrames = 512;
    Sid808Engine hostPath;
    Sid808Engine scratchPath;
    hostPath.prepare(48000.0);
    scratchPath.prepare(48000.0);
    require(applyFactorySid808Kit(120, hostPath), "kit applies to host-path engine");
    require(applyFactorySid808Kit(120, scratchPath), "kit applies to scratch-path engine");
    hostPath.noteOn(Sid808Drum::Tom, 112u, 45u);
    scratchPath.noteOn(Sid808Drum::Tom, 112u, 45u);

    std::vector<float> hostL(kFrames), hostR(kFrames);
    std::vector<float> refBlock3L(kFrames), refBlock3R(kFrames);
    std::vector<float> scratchL(kFrames), scratchR(kFrames);
    std::vector<float> outBlock3L(kFrames), outBlock3R(kFrames);

    // Reference: three consecutive host-buffer blocks.
    hostPath.processBlock(hostL.data(), hostR.data(), kFrames);
    hostPath.processBlock(hostL.data(), hostR.data(), kFrames);
    hostPath.processBlock(refBlock3L.data(), refBlock3R.data(), kFrames);

    // Candidate: block 2 renders into scratch (the v908/v909 no-output path).
    scratchPath.processBlock(scratchL.data(), scratchR.data(), kFrames);
    std::fill(scratchL.begin(), scratchL.end(), 0.0f);
    std::fill(scratchR.begin(), scratchR.end(), 0.0f);
    scratchPath.processBlock(scratchL.data(), scratchR.data(), kFrames); // "no-output" block
    scratchPath.processBlock(outBlock3L.data(), outBlock3R.data(), kFrames);

    float peakScratch = 0.0f;
    for (int i = 0; i < kFrames; ++i)
        peakScratch = std::max(peakScratch, std::fabs(scratchL[(size_t)i]));
    require(peakScratch > 0.0f,
            "the no-output block must actually render (tom tail is audible in block 2)");
    for (int i = 0; i < kFrames; ++i) {
        require(refBlock3L[(size_t)i] == outBlock3L[(size_t)i] &&
                refBlock3R[(size_t)i] == outBlock3R[(size_t)i],
                "block 3 must be bit-identical whether block 2 went to host or scratch buffers");
    }
}

// Shared post-FX continuity: reverb state must age through a scratch block
// exactly as through a host block (the v908 no-output FX contract, proven
// behaviorally on the same SchroederReverb class both wrappers use).
void test_nooutput_postfx_state_ages_behaviorally() {
    using namespace ArpSID;
    // Blocks must be longer than the largest comb delay (~1.5k samples at
    // 48 kHz) so the third block actually contains reverb-tail energy.
    constexpr int kFrames = 2048;
    SchroederReverb ref;
    SchroederReverb cand;
    ref.init(48000.0);
    cand.init(48000.0);

    auto feedBlock = [&](SchroederReverb& rv, bool impulse, float* sinkL, float* sinkR) {
        for (int i = 0; i < kFrames; ++i) {
            const float x = (impulse && i == 0) ? 0.9f : 0.0f;
            float l = 0.0f, r = 0.0f;
            rv.process(x, x, l, r);
            if (sinkL) sinkL[i] = l;
            if (sinkR) sinkR[i] = r;
        }
    };

    std::vector<float> refTail(kFrames), refTailR(kFrames);
    std::vector<float> candTail(kFrames), candTailR(kFrames);
    std::vector<float> scratch(kFrames), scratchR(kFrames);

    feedBlock(ref, true, scratch.data(), scratchR.data());   // impulse block
    feedBlock(ref, false, scratch.data(), scratchR.data());  // host block
    feedBlock(ref, false, refTail.data(), refTailR.data());  // observed tail

    feedBlock(cand, true, scratch.data(), scratchR.data());  // impulse block
    feedBlock(cand, false, nullptr, nullptr);                 // "no-output" scratch block (discarded)
    feedBlock(cand, false, candTail.data(), candTailR.data());// observed tail

    float tailPeak = 0.0f;
    for (int i = 0; i < kFrames; ++i) {
        require(refTail[(size_t)i] == candTail[(size_t)i] &&
                refTailR[(size_t)i] == candTailR[(size_t)i],
                "reverb tail must be bit-identical after a discarded scratch block");
        tailPeak = std::max(tailPeak, std::fabs(refTail[(size_t)i]));
    }
    require(tailPeak > 0.0f, "reverb tail must still ring in the third block");
}

void test_phase2_source_contracts() {
    const std::string cpp = readFile(ARPSID_SOURCE_ROOT "/source/arpsid_processor_phase2.cpp");
    // No-output telemetry capture: the scratch branch must capture meters and
    // publish scope pointers, not report silence.
    requireContains(cpp,
                    "applyOutputFX_(tmpOutL.data(), tmpOutR.data(), numSamples);\n"
                    "        captureTelemetryMeters(tmpOutL.data(), tmpOutR.data());\n"
                    "        telemetryOutL = tmpOutL.data();\n"
                    "        telemetryOutR = tmpOutR.data();",
                    "Phase2 no-output branch must capture telemetry meters/scope from the scratch render");
    requireContains(cpp, "noOutputBusActive = true;",
                    "Phase2 no-output branch must mark no-output provenance");
    requireContains(cpp, "full.noOutputBusActive = noOutputBusActive;",
                    "Phase2 telemetry must publish no-output provenance");
    requireContains(cpp, "full.telemetryRepresentsHostOutput = !noOutputBusActive;",
                    "Phase2 telemetry must state whether meters represent host output");
    // Projection mirror truth: Phase2 publishes explicit unavailability.
    requireContains(cpp, "full.projectionMirrorAvailable = false;",
                    "Phase2 must publish projectionMirrorAvailable=false");
    requireContains(cpp, "full.projectionMirrorBackend = kArpSIDProjectionMirrorBackendUnavailablePhase2;",
                    "Phase2 must publish the UnavailablePhase2 mirror backend");
    // Promotion gating at the VST3 ingress.
    requireContains(cpp, "ArpSID::sidCanonicalGMDrumAutoPromotionAllowed(",
                    "Phase2 NoteOn ingress must gate GM promotion through the shared flavor law");
    requireContains(cpp, "paramValues[(size_t)kParamAutoGmDrumPromotion] > 0.5f",
                    "Phase2 GM promotion must consult the explicit opt-in parameter");

    const std::string kernel = readFile(ARPSID_SOURCE_ROOT "/source/au3/ArpSIDDSPKernel.hpp");
    requireContains(kernel, "ArpSID::sidCanonicalGMDrumAutoPromotionAllowed(",
                    "AU3 kernel must gate GM promotion through the shared flavor law");
    requireContains(kernel, "renderParams_[(size_t)kParamAutoGmDrumPromotion] > 0.5f",
                    "AU3 kernel GM promotion must consult the explicit opt-in parameter");

    const std::string au2 = readFile(ARPSID_SOURCE_ROOT "/source/au2/ArpSIDAUv2Component.mm");
    requireContains(au2, "sidCanonicalGMDrumAutoPromotionAllowed(",
                    "AUv2 wrapper must gate GM promotion through the shared flavor law");

    const std::string adapter = readFile(ARPSID_SOURCE_ROOT "/source/au3/ArpSIDDSPKernelAdapter.mm");
    requireContains(adapter, "out->projectionMirrorAvailable = true;",
                    "AU3 adapter must publish its real mirror sink as available");
    requireContains(adapter, "out->projectionMirrorBackend = kArpSIDProjectionMirrorBackendAU3Kernel;",
                    "AU3 adapter must name its mirror backend");
}

void test_v909_identity() {
    const std::string version = readFile(ARPSID_SOURCE_ROOT "/VERSION.txt");
    require(isPreservedPass380ClosureLineageVersion(version),
            "VERSION.txt must identify the preserved pass380 closure lineage");
    const std::string status = readFile(ARPSID_SOURCE_ROOT "/STATUS.md");
    requireContains(status, "v909 Classic-mode authority closure",
                    "STATUS.md must document the v909 closure");
    const std::string notes = readFile(ARPSID_SOURCE_ROOT "/RELEASE_NOTES_V909.md");
    requireContains(notes, "kParamAutoGmDrumPromotion",
                    "v909 notes must document the promotion opt-in parameter");
    requireContains(notes, "8580", "v909 notes must document the HMOS SID default");
    requireContains(notes, "no-output", "v909 notes must document no-output telemetry capture");
    const std::string sweep = readFile(ARPSID_SOURCE_ROOT "/scripts/run_timing_music_contract_sweep.sh");
    requireContains(sweep, "ClassicModeAuthorityClosureV909Tests",
                    "timing/music sweep must run the v909 closure tests");
}

} // namespace

int main() {
    test_classic_channel10_note36_never_promotes_without_optin();
    test_explicit_optin_still_promotes();
    test_runtime_model_arp_effective_authority_rejects_synthmode();
    test_flavor_law_matrix();
    test_auto_gm_promotion_param_contract();
    test_synthmode_transport_reset_authority_source_contract();
    test_hmos_8580_class_default_everywhere();
    test_nooutput_scratch_block_is_state_continuous();
    test_nooutput_postfx_state_ages_behaviorally();
    test_phase2_source_contracts();
    test_v909_identity();
    std::cout << "classic_mode_authority_closure_v909_tests PASS\n";
    return 0;
}
