// Copyright (C) 2024-2026 Ulf Bertilsson
// gui_viewcontroller_wiring_v590_tests.cpp
//
// Pins the v590 GUI wiring guard:
// * top-bar model/preset/mode/play-mode controls are inserted in _buildUI;
// * the dedicated DRSID tab owns live telemetry labels/LEDs as ivars;
// * _poll updates the dedicated DRSID panel, clears it on disconnect, and
// includes the tab in realtime knob overlays;
// * v593 keeps MIX master, per-channel sends, and send-bus controls live
// instead of leaving visible widgets without targets/actions.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#error "ARPSID_SOURCE_ROOT must be defined by CMake for this source-shape test"
#endif

namespace {

std::string readTextFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "FAIL: could not open " << path << "\n";
        std::abort();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

void requireContains(const std::string& text, const std::string& needle, const char* msg) {
    require(text.find(needle) != std::string::npos, msg);
}

void requireAbsent(const std::string& text, const std::string& needle, const char* msg) {
    require(text.find(needle) == std::string::npos, msg);
}

void requireAfter(const std::string& text,
                  const std::string& first,
                  const std::string& second,
                  const char* msg) {
    const auto a = text.find(first);
    const auto b = text.find(second);
    require(a != std::string::npos, msg);
    require(b != std::string::npos, msg);
    require(b > a, msg);
}

} // namespace

int main() {
    const std::string vcPath =
        std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDViewController.mm";
    const std::string vc = readTextFile(vcPath);
    const std::string tabHeader =
        readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/gui/tab_architecture.h");
    const std::string adapterHeaderPath =
        std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDDSPKernelAdapter.h";
    const std::string adapterHeader = readTextFile(adapterHeaderPath);
    const std::string adapterPath =
        std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDDSPKernelAdapter.mm";
    const std::string adapter = readTextFile(adapterPath);
    const std::string audioUnitPath =
        std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDAudioUnit.mm";
    const std::string audioUnit = readTextFile(audioUnitPath);
    const std::string auv2Path =
        std::string(ARPSID_SOURCE_ROOT) + "/source/au2/ArpSIDAUv2Component.mm";
    const std::string auv2 = readTextFile(auv2Path);
    const std::string d418SpecPath =
        std::string(ARPSID_SOURCE_ROOT) + "/docs/D418_NIBBLE_SPEC.md";
    const std::string d418Spec = readTextFile(d418SpecPath);
    // device-bound AudioQueue capture backend.
    const std::string aqcapPath =
        std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDDigiAudioQueueCapture.mm";
    const std::string aqcap = readTextFile(aqcapPath);
    const std::string aqhdrPath =
        std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDDigiAudioQueueCapture.h";
    const std::string aqhdr = readTextFile(aqhdrPath);
    const std::string canonicalEventsPath =
        std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDCanonicalEvents.h";
    const std::string canonicalEvents = readTextFile(canonicalEventsPath);
    const std::string kernelPath =
        std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDDSPKernel.hpp";
    const std::string kernel = readTextFile(kernelPath);
    const std::string bitperfectPath =
        std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/engines/bitperfect_engine.h";
    const std::string bitperfect = readTextFile(bitperfectPath);
    const std::string backendProjection =
        readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/core/sid_runtime_backend_projection.h");
    const std::string fractionalRender =
        readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/core/sid_runtime_fractional_render.h");
    const std::string sidRegister =
        readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/engines/sid_register_engine.h");
    const std::string c64Platform =
        readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/core/c64_platform.h");
    const std::string c64Bus =
        readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/core/c64_bus.h");
    const std::string memoryMatrix =
        readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/core/c64_memory_matrix.h");
    const std::string phi2Machine =
        readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/core/c64_phi2_machine.h");
    const std::string sidBridge =
        readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/core/c64_sid_bridge.h");
    const std::string sid808 =
        readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/engines/sid808_engine.h");

    // v688: the inline top-bar SID-chip (_modelPop) and patch (_presetPop) popups
    // were REMOVED from the upper bar — chip/patch selection now lives in the
    // native in-window pull-down NSMenu. The objects are still CONSTRUCTED
    // off-screen (so the existing refresh/reconciliation code keeps working
    // without nil-guards) but intentionally NOT added to the view. Assert exactly
    // that contract rather than the old "mounted" wiring.
    requireContains(vc, "_modelPop=[[NSPopUpButton",
                    "top bar model popup is still constructed (off-screen, for refresh code)");
    requireAbsent(vc, "[root addSubview:_modelPop];",
                  "v688: model popup is intentionally NOT mounted — superseded by native pull-down menu");
    requireContains(vc, "_presetPop=[[NSPopUpButton",
                    "top bar preset popup is still constructed (off-screen, for refresh code)");
    requireAbsent(vc, "[root addSubview:_presetPop];",
                  "v688: preset popup is intentionally NOT mounted — superseded by native pull-down menu");
    requireAfter(vc, "_modePop=[[NSPopUpButton", "[root addSubview:_modePop];",
                 "top bar mode popup is mounted after creation");
    requireAfter(vc, "_playModeSeg=[[ArpSIDSegmentedToolTipControl", "[root addSubview:_playModeSeg];",
                 "top bar play-mode segmented control is mounted after creation");
    requireContains(vc, "sidChipRevisionSelectorLabelFromIndex((int)i)",
                    "top bar SID chip selector exposes exact 6581/8580 revisions");

    requireContains(audioUnit, "kArpSIDStateKey_SidChipSelectionIndex",
                    "PASS228: AU state stores the discrete selected SID chip selector index");
    requireContains(audioUnit, "kArpSIDStateKey_SidChipFamilyNorm",
                    "PASS228: AU state stores the coherent SID family mirror");
    requireContains(audioUnit, "binding.paramID == ArpSID::kParamSidChipRevision",
                    "PASS228: SID chip revision snapshot uses the current selected UI/parameter value, not stale factory root defaults");
    requireContains(audioUnit, "sidChipRevisionIndexFromNormalized(chipNorm228",
                    "PASS228: SID chip normalized state round-trips through a stable 0..3 selector index");
    requireContains(audioUnit, "forID:ArpSID::kParamSidChipRevision updateTree:YES",
                    "PASS228: restore reapplies selected SID chip revision to parameter tree");
    requireContains(audioUnit, "forID:ArpSID::kParamSidModel updateTree:YES",
                    "PASS228: restore repairs kParamSidModel family mirror after selected chip restore");
    requireContains(kernel, "pureSid1Q1OutputMode_",
                    "PASS229: kernel owns an atomic pure 1:1 SID output mode flag outside ParamID space");
    requireContains(kernel, "AUv2 pure 1:1 SID output mode",
                    "PASS229: kernel documents direct SID-engine output semantics");
    requireContains(kernel, "applyMixFxToOutputs_(outputs, numFrames);",
                    "PASS229: normal processed path still applies mix FX when pure mode is off");
    requireContains(kernel, "sanitizePureSid1Q1Outputs_(outputs, numFrames);",
                    "PASS229: pure mode keeps only invalid-float cleanup and bypasses post-FX");
    requireContains(kernel, "publishHiFiTelemetryFromProcessor_(resolveHiFiConfigFromRenderParams_())",
                    "PASS229: pure mode publishes telemetry without running HiFi processing");
    requireContains(adapterHeader, "setPureSid1Q1OutputMode",
                    "PASS229: adapter exposes pure 1:1 SID output setter");
    requireContains(adapterHeader, "pureSid1Q1OutputMode",
                    "PASS229: adapter exposes pure 1:1 SID output getter for state save");
    requireContains(audioUnit, "kArpSIDStateKey_PureSid1Q1OutputMode",
                    "PASS229: AU state persists pure 1:1 SID output mode");
    requireContains(vc, "PURE 1:1 AU SID ENGINE OUTPUT",
                    "PASS229: GUI exposes pure 1:1 SID-engine output toggle");
    requireContains(vc, "_pureSid1Q1Toggle_v229:",
                    "PASS229: GUI toggle is wired to adapter state");
    requireContains(vc, "const CGFloat y=host.bounds.size.height-24.f;",
                    "SID chip / patch selector row is mounted high in active panel chrome");
    requireContains(vc, "kH-kBW-kHdrH+18",
                    "initial SID chip / patch selector frames start in the raised header row");
    requireContains(vc, "return ArpSID::sidChipRevisionIndexToNormalized((int)std::clamp(popupIndex",
                    "SID chip popup maps directly to the chip-revision parameter");
    requireContains(vc, "const float familyNorm = idx >= 3 ? 1.0f : 0.0f;",
                    "SID chip popup keeps kParamSidModel coherent with 8580-only index");
    requireAbsent(vc, "sidChipRevisionIndexToNormalized(4)",
                  "SID chip quick action must not address a non-existent fifth selector slot");
    requireContains(backendProjection, "profile.family = selectedFamily;",
                    "chip revision selector updates runtime variant family");
    requireContains(backendProjection, "profile.chip_revision_code = selectorRevision;",
                    "chip revision selector updates runtime variant revision");
    requireContains(backendProjection, "sreg.setModel(selectedSidModel);",
                    "chip revision selector projects into SID-register mode");
    requireContains(backendProjection, "sreg.writeSystemByte(sidSystemByteFromVariantProfile(runtimeModel.variantProfile(), adsrBug));",
                    "chip revision selector projects live SID system byte");
    requireContains(fractionalRender, "mode == SidRuntimeRenderMode::SidRegister",
                    "fractional dispatcher gives SID-register mode its own accumulation law");
    requireContains(fractionalRender, "runtimeFractionalWeightData",
                    "fractional dispatcher tracks SID-register interval widths");
    requireContains(sidRegister, "kSidSubcycleLast, numerator / denom",
                    "SID-register subphase mapping preserves the full 0..255 lattice");
    requireContains(c64Bus, "std::sort(events_.begin()",
                    "C64 bus queue uses allocation-free std::sort instead of insertion sort");
    requireContains(c64Platform, "OpenBusLatch openBusLatch_{};",
                    "C64 platform uses the shared PHI2-aware open-bus latch");
    requireContains(c64Platform, "(readOpenBus() & 0xF0u)",
                    "C64 platform color RAM reads use open-bus high nibble");
    requireContains(c64Platform, "openBusLatch_.decayToPhi2(phi2Cycle_, !vic_.cpuCanUseBus())",
                    "C64 platform open-bus decay follows PHI2/VIC bus ownership");
    requireAbsent(c64Platform, "PLACEHOLDER: fixed 4096-cycle decay",
                  "C64 platform open-bus model must not be left as a fixed-decay placeholder");
    requireAbsent(c64Platform, "(end - phi2Cycle_) < 8u",
                  "C64 realtime core must not defer short opcodes just because fewer than 8 cycles remain");
    requireContains(kernel, "player->enablePhi2Machine(true)",
                    "AUv3 production PSID/RSID load enables the PHI2 runtime instead of leaving it test-only");
    requireContains(kernel, "ArpSID_cyclesPerSampleQ32(sr, clockHz)",
                    "C64 SID write mapping uses fixed-point host/sample cycle timing, not floating llround authority");
    requireContains(kernel, "sreg_().queueSubphaseWrite(boundedCycle, 0u, w.reg, w.value)",
                    "C64 SID writes enter the SID-register interval queue at their intra-sample cycle");
    requireContains(kernel, "sreg_().renderIntervalAccurate(iv, outL, outR)",
                    "C64 SID audio is interval-rendered per sample after timed writes are queued");
    // Multi-SID render: chips 0..4 are routed to per-chip render engines via
    // sregForChip_(). Writes for chip >= kMaxRenderedSidChips (5) are skipped.
    requireContains(kernel, "++droppedMultiSidWritesThisBlock;",
                    "C64 SID render path accounts/downgrades chips at or above the 5-chip render limit");
    requireContains(kernel, "w.chip == 0u",
                    "C64 SID render path routes chip-0 writes to the primary render engine");
    requireContains(kernel, "} else if (w.chip < activeSidChips) {",
                    "C64 SID render path routes secondary chip writes to their per-chip engine");
    requireContains(kernel, "sregForChip_(w.chip)",
                    "C64 SID render path selects the render engine by chip index");
    requireContains(kernel, "static constexpr uint8_t kMaxRenderedSidChips = 5u;",
                    "C64 SID render supports up to 5 SID chips");
    requireAbsent(kernel, "sampleOffset = base->sample + static_cast<int>(std::llround",
                  "C64 SID render path must not round PHI2 deltas into sample-boundary writes");
    requireContains(memoryMatrix, "configureSidBases",
                    "PHI2 memory matrix accepts PSID multi-SID base routing metadata");
    requireContains(memoryMatrix, "cia1_->read",
                    "PHI2 memory matrix maps CIA1 reads");
    requireContains(memoryMatrix, "vic_->write",
                    "PHI2 memory matrix maps VIC-II writes");
    requireContains(phi2Machine, "const bool irqLine = cia1_.irq() || vic_.irq();",
                    "PHI2 machine samples CIA1/VIC IRQ before wiring into the 6510 microcore");
    requireContains(phi2Machine, "cpu_.setIrqLine(irqLine)",
                    "PHI2 machine wires sampled CIA1/VIC IRQ into the 6510 microcore");
    requireContains(phi2Machine, "cpu_.setNmiLineLow(cia2_.irq())",
                    "PHI2 machine wires CIA2 IRQ into the 6510 NMI line");
    requireContains(phi2Machine, "phase.owner = aecHigh ? Phi2Owner::Cpu : Phi2Owner::Vic",
                    "PHI2 machine uses AEC, not BA warning, for bus ownership");
    requireContains(phi2Machine, "cpu_.setRdy(baHigh)",
                    "PHI2 machine wires VIC BA warning to 6510 RDY");
    requireContains(sidBridge, "std::array<std::array<uint8_t, 32>, 5> regsByChip",
                    "C64 SID bridge preserves per-chip register banks");
    requireContains(sidBridge, "uint8_t chip = 0;",
                    "C64 SID bridge timed writes retain chip index");
    requireContains(vc, "case ArpSID::ComponentFlavor::Hybrid:\n            return YES;",
                    "hybrid GUI preset browsing exposes both instrument patches and drum kits");
    requireContains(vc, "Instrument flavor must not infer mode from a stale GUI cache",
                    "pure Instrument GUI must not read stale SynthMode cache");
    requireContains(vc, "if(_au) [self _applyModeSelectionIndex:1];",
                    "pure Instrument GUI sync must force SYNTH / SID REG mode");
    requireContains(vc, "_componentFlavor == ArpSID::ComponentFlavor::Instrument) {\n        // v930: Pure Instrument is always the playable SYNTH / SID REG surface",
                    "pure Instrument apply-mode path must lock every request to SYNTH / SID REG");
    requireContains(vc, "// v950: the mode popup selects structural note authority. ARP is always",
                    "mode selector must clear ARP but preserve DrSID/SID808 SEQ transport");
    requireContains(vc, "const float arp = 0.f;\n    const float seq = modeIsDrSid",
                    "mode selector must preserve SEQ when selecting DrSID/SID808 authority");
    requireContains(vc, "[self _storeCachedParamValue:ArpSID::kParamArpEnable value:arp];\n    [self _storeCachedParamValue:ArpSID::kParamSeqEnable value:seq];",
                    "mode selector must cache ARP disabled and DrSID-aware SEQ transport after authority selection");
    requireContains(vc, "id<ArpSIDParameterLike> seqParam=[_au.parameterTree parameterWithAddress:(AUParameterAddress)ArpSID::kParamSeqEnable];",
                    "pure Instrument/Synth apply-mode path has a concrete SEQ param authority target");
    requireContains(vc, "if(arpParam) [arpParam setValue:arp originator:_tok]; else [_au setParameterValue:arp forID:ArpSID::kParamArpEnable];\n    if(seqParam) [seqParam setValue:seq originator:_tok]; else [_au setParameterValue:seq forID:ArpSID::kParamSeqEnable];",
                    "mode selector must write ARP clear plus DrSID-aware SEQ transport through the AU bridge");
    requireAbsent(vc, "[self _applyModeSelectionIndex:instrumentMode];",
                  "pure Instrument GUI must not reintroduce stale-cache mode inference");
    requireAbsent(vc, "_componentFlavor == ArpSID::ComponentFlavor::Instrument && idx == 2) idx = 0",
                  "pure Instrument GUI must not map DrSID requests to CLASSIC/BitPerfect");
    requireContains(vc, "if(_componentFlavor == ArpSID::ComponentFlavor::Instrument) {\n        // v931: Pure Instrument's visible/effective mode is locked",
                    "pure Instrument effective-mode path must never report CLASSIC during stale telemetry/cache windows");
    requireContains(vc, "-(BOOL)_effectiveArpAuthorityEnabledForModeIndex:(NSInteger)modeIndex",
                    "GUI effective ARP authority helper must exist");
    requireContains(vc, "if(_componentFlavor == ArpSID::ComponentFlavor::Instrument) return NO;",
                    "pure Instrument GUI must suppress stale ARP authority presentation");
    requireContains(vc, "if(modeIndex != 0) return NO;",
                    "GUI must only present ARP as authority in CLASSIC/BitPerfect mode");
    requireContains(vc, "const float arpEnableNorm = [self _effectiveArpAuthorityEnabledForModeIndex:modeIndex] ? 1.0f : 0.0f;",
                    "presentation context must not report stale ARP under SynthMode/DrSID");
    requireContains(vc, "-(BOOL)_effectiveSeqAuthorityEnabledForModeIndex:(NSInteger)modeIndex",
                    "GUI effective SEQ authority helper must exist");
    requireContains(vc, "v949: GUI mirrors DSP law. SEQ is valid as the melodic Classic/",
                    "GUI effective SEQ authority must document CLASSIC plus DrSID/SID808 drum sequencer law");
    requireContains(vc, "if(modeIndex == 0 || modeIndex == 2)\n        return [self _cachedParamValue:ArpSID::kParamSeqEnable defaultCenter:0.0f] > 0.5f;",
                    "GUI effective SEQ authority must expose DrSID/SID808 sequencer authority");
    requireContains(vc, "const float seqEnableNorm = [self _effectiveSeqAuthorityEnabledForModeIndex:modeIndex] ? 1.0f : 0.0f;",
                    "presentation context must suppress SEQ under SynthMode/Pure Instrument and allow DrSID/SID808");
    requireContains(vc, "const BOOL seqOn=tel.seqEnabled;",
                    "SEQ step view must use telemetry effective SEQ authority, not stale raw cache");
    requireContains(vc, "BOOL arpOn=[self _effectiveArpAuthorityEnabledForModeIndex:[self _effectiveModeIndexFromTelemetry:&tel]];",
                    "ARP step view must not light up from stale raw cache");
    requireContains(vc, "ArpSID::GUI::kProductionVisibleTabs",
                    "Cocoa navigation consumes the shared canonical tab ring");
    requireContains(vc, "kArpSIDProductionVisibleTabs_v269",
                    "visible tab ring has one controller alias");
    requireContains(tabHeader, "visibleTabsAreUnique",
                    "shared tab inventory has compile-time duplicate-tab guard");
    requireContains(tabHeader, "LegacySidProjection",
                    "shared inventory retains and hides the migration-only ID");
    requireAbsent(vc, "typedef NS_ENUM(NSInteger,ArpSIDTab)",
                  "Cocoa controller must not define a second tab enum");
    requireContains(vc, "ArpSIDVisibleTabSetForFlavor_v269",
                    " count/index/segment lookup uses one flavor tab-set accessor");
    requireContains(vc, "ArpSIDTabSegmentWidthForLabel_v269",
                    " segmented tab labels receive deterministic width sizing");
    requireContains(vc, "tabBar.accessibilityLabel = @\"ArpSID tab navigation\"",
                    " tab bar exposes a stable accessibility label");
    requireContains(vc, "case 4: return @\"SEQ tab. Global step-sequencer",
                    "Instrument flavor tooltip covers the now-visible SEQ tab");
    requireContains(vc, "case 13: return @\"DRSID tab. Auxiliary C64 wavetable drum surface available from the instrument flavor.",
                    "Instrument flavor tooltip covers the now-visible DRSID tab");
    requireContains(vc, "case 0: return @\"MAIN tab. Shared synth scope, filter, and voice overview available beside the SID-808 surface.",
                    "SID-808 flavor tooltip covers the now-visible MAIN tab");
    requireContains(vc, "case 0: return @\"MAIN tab. Shared synth scope, filter, and voice overview available beside the drum-machine surface.",
                    "DrumMachine flavor tooltip covers the now-visible MAIN tab");

    requireContains(vc, "NSTextField*_drsidPanelClockLabel;",
                    "DRSID panel clock label is owned by the controller");
    requireContains(vc, "NSTextField*_drsidPanelChipLabel;",
                    "DRSID panel chip label is owned by the controller");
    requireContains(vc, "NSTextField*_drsidPanelHUDLabel;",
                    "DRSID panel HUD label is owned by the controller");
    requireContains(vc, "ArpSIDLEDView*_drsidPanelSyncLED;",
                    "DRSID panel sync LED is owned by the controller");
    requireContains(vc, "ArpSIDLEDView*_drsidPanelMidiLED;",
                    "DRSID panel MIDI LED is owned by the controller");
    requireContains(vc, "ArpSIDLEDView*_drsidPanelSidLED;",
                    "DRSID panel SID LED is owned by the controller");
    requireContains(vc, "ArpSIDLEDView*_drsidPanelRtLED;",
                    "DRSID panel RT LED is owned by the controller");
    requireContains(vc, "ArpSIDOscilloscopeView*_drsidPanelScopeView;",
                    "DRSID panel owns a dedicated realtime scope view");

    requireContains(vc, "_drsidPanelClockLabel=[NSTextField labelWithString",
                    "DRSID clock label builder assigns ivar directly");
    requireContains(vc, "_drsidPanelChipLabel=[NSTextField labelWithString",
                    "DRSID chip label builder assigns ivar directly");
    requireContains(vc, "_drsidPanelHUDLabel=[NSTextField labelWithString",
                    "DRSID footer HUD builder assigns ivar directly");
    requireContains(vc, "_drsidPanelScopeView=[[ArpSIDOscilloscopeView alloc]",
                    "DRSID panel builds a dedicated realtime scope view");
    requireContains(vc, "[scopeBox addSubview:_drsidPanelScopeView];",
                    "DRSID scope view is mounted in the panel");
    requireContains(vc, "if(i==0) _drsidPanelSyncLED=led;",
                    "DRSID LED loop stores sync LED ivar");
    requireContains(vc, "else if(i==3) _drsidPanelRtLED=led;",
                    "DRSID LED loop stores RT LED ivar");
    requireAbsent(vc, "identifier=@\"ArpSIDDrsidPanelClock\"",
                  "DRSID clock label no longer relies on dead identifier lookup");
    requireAbsent(vc, "identifier=@\"ArpSIDDrsidPanelChip\"",
                  "DRSID chip label no longer relies on dead identifier lookup");
    requireAbsent(vc, "identifier=@\"ArpSIDDrsidPanelHUD\"",
                  "DRSID HUD label no longer relies on dead identifier lookup");

    requireContains(vc, "-(void)_clearDrsidPanelTelemetry",
                    "DRSID panel has disconnect/stale-state clear helper");
    requireContains(vc, "const BOOL wantsDrsidPanelUI = (_tab==ArpSIDTabDrsid);",
                    "poll loop recognizes dedicated DRSID tab");
    requireContains(vc, "const BOOL wantsC64Data = wantsC64Tab || wantsSidCoreViz || wantsC64StateTab;",
                    "poll loop limits heavy C64 snapshot demand to C64-facing views");
    requireContains(vc, "readTelemetry:&tel includeScopes:wantsScopeData includeC64Snapshot:wantsC64Data",
                    "poll loop forwards explicit C64 snapshot demand to the adapter");
    requireContains(vc, "ArpSID::GUI::wantsTelemetryScopePayload({",
                    "scope demand uses the shared runtime-tested consumer policy");
    requireContains(vc, "wantsOptionsUI",
                    "Options requests the core scope payload it visibly renders");
    requireContains(vc, "wantsMainScope || wantsDrSidUI || wantsDrsidPanelUI",
                    "oscilloscope readback includes dedicated DRSID panel");
    requireContains(vc, "_drsidPanelScopeView.forensicActivity = 0.f;",
                    "DRSID scope clears stale forensic activity on disconnect");
    requireContains(vc, "[_drsidPanelScopeView updateWithSamples:nullptr count:0];",
                    "DRSID scope clears stale samples on disconnect/tab hide");
    requireContains(vc, "_drsidPanelClockLabel,",
                    "clear HUD label list includes DRSID clock label");
    requireContains(vc, "_drsidPanelChipLabel,",
                    "clear HUD label list includes DRSID chip label");
    requireContains(vc, "_drsidPanelHUDLabel,",
                    "clear HUD label list includes DRSID footer label");
    requireContains(vc, "_drsidPanelSyncLED.on = tel.seqEnabled && tel.seqFollowHost;",
                    "DRSID sync LED updates from live telemetry");
    requireContains(vc, "_drsidPanelSidLED.on = drSidOn;",
                    "DRSID SID LED updates from live telemetry");
    requireContains(vc, "_drsidPanelRtLED.on = drumPeak > 0.015f || tel.sid808BridgeReplacedOutput || tel.hostPlaying;",
                    "DRSID RT LED updates from live telemetry, including SID-808 bridge replacement activity");
    requireContains(vc, "_drsidPanelScopeView.forensicActivity = forensicActivity * 0.45f + drumPeak * 0.55f;",
                    "DRSID scope blends forensic and drum activity");
    requireContains(vc, "[_drsidPanelScopeView updateWithSamples:_oscBuf count:n];",
                    "DRSID scope is fed by live oscilloscope samples");
    requireContains(vc, "case ArpSIDTabDrsid:",
                    "DRSID tab participates in realtime knob overlays");

    requireAbsent(adapterHeader, "drumEngineRouterOptInForRealtime",
                  "adapter must not expose a second render authority");
    requireAbsent(adapterHeader, "drumEngineRouterOptInAtomicTarget",
                  "adapter must not expose a wrapper-side router target");
    requireAbsent(adapter, "_settingsRouterOptInRt",
                  "settings compatibility state must not be projected into render control");
    requireContains(adapter, "_diagSplitBrainDiagnosticCount",
                    "adapter mirrors AUv2 split-brain diagnostics into GUI telemetry");
    requireContains(adapter, "out->splitBrainDiagnosticCount       = _diagSplitBrainDiagnosticCount.load",
                    "diagnostic snapshot exposes AUv2 split-brain diagnostics");
    requireContains(adapterHeader, "Auv3ScratchCounterAtomicTargets",
                    "adapter exposes raw AUv3 scratch diagnostic atomics");
    requireContains(adapterHeader, "Auv2DiagnosticCounterAtomicTargets",
                    "adapter exposes raw AUv2 diagnostic atomics");
    requireContains(adapter, "return { &_diagRenderScratchEpochAuv3, &_diagScratchUnderCapacityCountAuv3 };",
                    "adapter returns raw AUv3 scratch diagnostic atomic targets");
    requireContains(adapter, "- (ArpSID::GUI::Auv2DiagnosticCounterAtomicTargets)auv2DiagnosticCounterAtomicTargets",
                    "adapter returns raw AUv2 diagnostic atomic targets");
    // audit P0.2/P0.3: scratch validity is entry/exit epoch stability, not a
    // creation-time captured-epoch comparison.
    requireContains(audioUnit, "const uint64_t scratchEpochAtEntry = scratchEpoch->load(std::memory_order_acquire);",
                    "AUv3 render block loads scratch epoch at entry");
    requireContains(audioUnit, "scratchEpochAtExit != scratchEpochAtEntry",
                    "AUv3 render block fails closed if scratch epoch changed during render");
    requireContains(audioUnit, "diagScratchEpoch->store",
                    "AUv3 render diagnostics write raw atomics instead of Objective-C messages");
    requireAbsent(audioUnit, "[diagAdapter storeAuv3ScratchEpoch",
                  "AUv3 render block must not send Objective-C diagnostics on the realtime thread");
    requireContains(canonicalEvents, "std::sort(events, events + count, TimedEvent::before);",
                    "AUv3 EventBuffer uses allocation-free std::sort instead of insertion sort");
    requireContains(bitperfect, "if (count < MAX_POLYPHONY) order[(size_t)count++] = i;",
                    "BitPerfect forced-held seeding guards the fixed voice order array");
    requireContains(bitperfect, "for (int i = 1; i < count; ++i)",
                    "BitPerfect forced-held seeding uses tiny explicit sort over active voices");
    requireContains(kernel, "float telemetryPeakDecayPerSample_",
                    "AUv3 kernel caches telemetry peak decay outside the render hot path");
    requireContains(kernel, "realtimePowUnit_(telemetryPeakDecayPerSample_, frames)",
                    "AUv3 kernel uses multiply-only telemetry decay in render");
    requireAbsent(kernel, "std::exp(std::log(0.001f) * (static_cast<float>(frames)",
                  "AUv3 telemetry must not call exp/log every render block");
    requireContains(kernel, "publishForensicTelemetry_(resolveEffectiveForensicConfigForBlock_());",
                    "forensic telemetry publishes from effective params in every render mode");
    requireContains(sid808, "float telemetryDecayPerSample_",
                    "SID-808 engine caches its telemetry decay outside processBlock");
    requireContains(sid808, "realtimePowUnit_(telemetryDecayPerSample_",
                    "SID-808 engine uses multiply-only telemetry decay in render");
    requireAbsent(sid808, "std::exp(std::log(0.001) *",
                  "SID-808 processBlock must not call exp/log for telemetry decay");
    requireAbsent(auv2, "publishedRouterOptInAtomic",
                  "AUv2 must not retain a wrapper-side router render target");
    requireAbsent(auv2, "routerOptIn->load(std::memory_order_acquire) != 0u",
                  "AUv2 render must not bypass the canonical kernel through a wrapper-side router");
    requireAbsent(auv2, "[settingsAdapter drumEngineRouterOptInForRealtime]",
                  "AUv2 render gate must not send Objective-C settings messages");
    requireContains(auv2, "publishedAuv2DiagTargets",
                    "AUv2 stores raw diagnostic atomic targets for render");
    requireContains(auv2, "diag.renderEpoch->store",
                    "AUv2 render diagnostics write raw atomics directly");
    requireAbsent(auv2, "[diagAdapter storeAuv2DiagCounters",
                  "AUv2 render diagnostics must not send Objective-C messages");
    requireAbsent(auv2, "impl->drumEngineBridge.loadFactorySlot(",
                  "AUv2 render must not mutate a separate drum engine");
    requireAbsent(auv2, "impl->drumEngineBridge.processBlock(",
                  "AUv2 render must preserve the wrapped kernel as sole audio authority");
    requireContains(auv2, "const AUAudioUnitStatus status = renderBlock(",
                    "AUv2 render always delegates to the canonical wrapped kernel");
    requireContains(auv2, "const bool postRenderAuthorityChanged",
                    "AUv2 render checks authority changes after wrapped render");
    requireContains(auv2, "preserve successful",
                    "AUv2 split-brain path preserves successful audio instead of zero-after-advance");
    requireContains(auv2, "impl->splitBrainDiagnosticCount.fetch_add",
                    "AUv2 split-brain path reports telemetry");
    requireAbsent(auv2, "&impl->splitBrainDiagnosticCount);",
                  "AUv2 split-brain path must not fail-close silence after state advanced");
    requireContains(vc, "_c64DiagValueLabels_v549_.count == 55",
                    "C64 state diagnostics includes split-brain counter row");
    requireAbsent(vc, "Show diagnostic counter dashboard",
                  "retired C64 STATE tab must not leave a dead Settings toggle");
    requireContains(vc, "DIAGNOSTICS — C64 + SIDCORE LIVE TELEMETRY",
                    "Settings explains where the retained diagnostics now live");
    requireContains(vc, "if (requested == ArpSIDTabC64StateV544) return ArpSIDTabC64;",
                    "persisted C64 STATE navigation migrates to the main C64 tab");
    requireContains(vc, "_pC64StateV544 = nil;",
                    "retired C64 STATE panel is not constructed");
    requireContains(vc, "snap.splitBrainDiagnosticCount",
                    "C64 state diagnostics displays split-brain counter value");

    requireContains(vc, "-(void)_mixDelaySendChanged_v593_:(id)sender",
                    "MIX delay-send handler exists");
    requireContains(vc, "_mixModel_v547_.channels[(size_t)ch].sendToDelay",
                    "MIX delay-send handler writes channel model");
    requireContains(vc, "dlySend.action     = @selector(_mixDelaySendChanged_v593_:);",
                    "MIX delay-send slider is wired");
    requireContains(vc, "-(void)_mixReverbSendChanged_v593_:(id)sender",
                    "MIX reverb-send handler exists");
    requireContains(vc, "_mixModel_v547_.channels[(size_t)ch].sendToReverb",
                    "MIX reverb-send handler writes channel model");
    requireContains(vc, "rvbSend.action     = @selector(_mixReverbSendChanged_v593_:);",
                    "MIX reverb-send slider is wired");

    requireContains(vc, "-(void)_mixMasterVolumeChanged_v593_:(id)sender",
                    "MIX master-volume handler exists");
    requireContains(vc, "mvol.action     = @selector(_mixMasterVolumeChanged_v593_:);",
                    "MIX master-volume slider is wired");
    requireContains(vc, "-(void)_mixLimiterToggled_v593_:(id)sender",
                    "MIX limiter handler exists");
    requireContains(vc, "action:@selector(_mixLimiterToggled_v593_:)",
                    "MIX limiter checkbox is wired");
    requireContains(vc, "-(void)_mixLimiterThresholdChanged_v593_:(id)sender",
                    "MIX limiter-threshold handler exists");
    requireContains(vc, "thrSl.action     = @selector(_mixLimiterThresholdChanged_v593_:);",
                    "MIX limiter-threshold slider is wired");
    requireContains(vc, "-(void)_mixLimiterReleaseChanged_v593_:(id)sender",
                    "MIX limiter-release handler exists");
    requireContains(vc, "relSl.action     = @selector(_mixLimiterReleaseChanged_v593_:);",
                    "MIX limiter-release slider is wired");
    requireContains(vc, "-(void)_mixStereoWidthChanged_v593_:(id)sender",
                    "MIX stereo-width handler exists");
    requireContains(vc, "widthSl.action     = @selector(_mixStereoWidthChanged_v593_:);",
                    "MIX stereo-width slider is wired");
    requireContains(vc, "-(void)_mixDimMonitorToggled_v593_:(id)sender",
                    "MIX dim-monitor handler exists");
    requireContains(vc, "action:@selector(_mixDimMonitorToggled_v593_:)",
                    "MIX dim-monitor checkbox is wired");
    requireContains(vc, "-(void)_mixSendEnabledToggled_v593_:(id)sender",
                    "MIX send-bus enable handler exists");
    requireContains(vc, "action:@selector(_mixSendEnabledToggled_v593_:)",
                    "MIX send-bus enable checkbox is wired");
    requireContains(vc, "-(void)_mixSendReturnChanged_v593_:(id)sender",
                    "MIX send-return handler exists");
    requireContains(vc, "retSl.action     = @selector(_mixSendReturnChanged_v593_:);",
                    "MIX send-return slider is wired");
    requireAbsent(vc, "checkboxWithTitle:@\"Limiter\"\n                                                target:nil",
                  "MIX limiter checkbox must not be inert");
    requireAbsent(vc, "checkboxWithTitle:@\"Dim -10dB\"\n                                                target:nil",
                  "MIX dim checkbox must not be inert");
    requireAbsent(vc, "checkboxWithTitle:@\"Enable\"\n                                                       target:nil action:nil",
                  "MIX send-bus enable checkbox must not be inert");

    requireAbsent(vc, "auto baseBank = std::make_shared<ArpSID::GUI::DigiSampleBankBlob>(_digiSampleBank_v596_);",
                  "DIGI import must not capture and later publish a stale full sample-bank snapshot");
    requireContains(vc, "ArpSID::GUI::DigiUserSampleClip builtClip = ArpSID::GUI::makeDefaultDigiUserSampleClip();",
                    "DIGI import quantizes into an isolated worker-local temp clip instead of a 480 KB temp bank");
    requireContains(vc, "digiBuildUserSampleClipFromFloatMono",
                    "DIGI import/REC use the direct clip builder to avoid giant temporary banks");
    requireContains(vc, "strongSelf->_digiSampleBank_v596_.clips[slot] = *loadedClip;",
                    "DIGI import merges only the completed slot clip back into the current GUI bank");
    requireContains(vc, "ArpSID::GUI::digiRecountSampleBank(strongSelf->_digiSampleBank_v596_);",
                    "DIGI import recounts the current bank after slot-level merge");

    requireContains(vc, "const UInt32 channels = buffer.format.channelCount;",
                    "DIGI import must not synthesize a fake channel for zero-channel buffers");
    requireContains(vc, "if (channels == 0u || !channelData)",
                    "DIGI import rejects zero-channel/null-channel-data layouts before indexing channelData");
    requireContains(vc, "const double safeSampleRate = std::isfinite(rawSampleRate) ? rawSampleRate : 44100.0;",
                    "DIGI import sanitizes non-finite buffer sample rates before integer cast");
    requireContains(vc, "AVAudioEngine* _digiRecordEngine_v158_;",
                    "DIGI record owns an AVAudioEngine capture object");
    requireContains(vc, "std::vector<float> _digiRecordMonoFloatBuffer_v182_;",
                    "DIGI record stores bounded mono float capture data in a preallocated buffer before quantize");
    requireContains(vc, "std::atomic<NSUInteger> _digiRecordWriteFrames_v182_;",
                    "DIGI record uses atomic write position instead of locking in the CoreAudio tap");
    requireContains(vc, "std::atomic<std::uint32_t> _digiRecordGeneration_v184_;",
                    "DIGI record tap must use a generation token to reject stale callbacks across STOP/START");
    requireContains(vc, "std::atomic<std::uint32_t> _digiRecordTapCallbacksInFlight_v184_;",
                    "DIGI record STOP must know whether a tap callback is still writing before copying capture storage");
    requireContains(vc, "recordGeneration = _digiRecordGeneration_v184_",
                    "DIGI record input tap must capture the current generation token");
    requireContains(vc, "TapExitGuard",
                    "DIGI record input tap must decrement the in-flight counter on every early return");
    requireContains(vc, "-(BOOL)_digiWaitForRecordTapCallbacksToDrain_v185_:(NSUInteger)spinLimit",
                    "DIGI record uses one bounded UI-side helper to drain in-flight tap callbacks");
    requireContains(vc, "const BOOL drainedTapCallbacks = [self _digiWaitForRecordTapCallbacksToDrain_v185_:2000u];",
                    "DIGI record STOP must fail closed instead of copying while a tap callback is still in-flight");
    requireContains(vc, "DIGI REC TAP BUSY",
                    "DIGI record reports and drops an unsafe take if tap callbacks do not drain in time");
    requireContains(vc, "ArpSIDDigiPrepareRecordVector_v326",
                    "DIGI record reuses fixed capture storage through the allocation-safe prepare helper");
    requireContains(vc, "else std::fill(buffer.begin(), buffer.end(), 0.0f);",
                    "DIGI record prepare helper clears existing storage without reallocating");
    requireContains(vc, "std::atomic<bool> _digiRecordActive_v158_;",
                    "DIGI record active flag is shared with the CoreAudio tap and must be atomic");
    requireContains(vc, "const NSUInteger reservedStart = strongSelf->_digiRecordWriteFrames_v182_.fetch_add(sourceFrames",
                    "DIGI record tap must atomically reserve non-overlapping write ranges");
    requireAbsent(vc, "_digiRecordWriteFrames_v182_.load(std::memory_order_relaxed)",
                  "DIGI record tap must not use load/loop/store cursor reservation");
    requireContains(vc, "!strongSelf->_digiRecordActive_v158_.load(std::memory_order_acquire)",
                    "CoreAudio tap reads the DIGI record active flag atomically");
    requireContains(vc, "keeping the\n    // preallocated storage alive until the next start avoids a use-after-free",
                    "DIGI record STOP must not free the capture buffer while a CoreAudio tap callback can be in-flight");
    requireAbsent(vc, "NSMutableData* _digiRecordMonoFloatData_v158_;",
                  "DIGI record must not mutate NSMutableData from the CoreAudio tap");
    requireAbsent(vc, "std::mutex _digiRecordBufferMutex_v160_;",
                  "DIGI record must not use a mutex for CoreAudio tap capture");
    requireAbsent(vc, "std::vector<float> mono(writableSourceFrames",
                  "DIGI record must not heap-allocate a mono vector in the CoreAudio tap");
    requireAbsent(vc, "appendBytes:",
                  "DIGI record tap must not append to Objective-C data on the audio callback");
    requireContains(vc, "AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio",
                    "DIGI record requests microphone permission before starting capture");
    requireContains(vc, "_digiRecordSampleRateHz_v160_ = format.sampleRate;",
                    "DIGI record preserves the selected input sample rate for quantization");
    requireContains(vc, "System Default Input",
                    "DIGI record input UI includes a safe system-default option");
    requireContains(vc, "temporarily switches CoreAudio default input",
                    "DIGI record input popup documents selected-source routing and restore policy");
    requireContains(vc, "DIGI REC TAP FAILED",
                    "DIGI record fails closed when AVAudioEngine tap installation throws");
    requireContains(vc, "DIGI REC MIC DENIED",
                    "DIGI record reports denied microphone permission without starting capture");
    requireContains(vc, "-(void)_digiRecordToggle_v158_:(id)sender",
                    "DIGI record has a GUI toggle action");
    requireContains(vc, "action:@selector(_digiRecordToggle_v158_:)",
                    "DIGI REC button is wired to the record toggle action");
    requireContains(vc, "kArpSIDDigiRecordInputTag",
                    "DIGI record exposes an input source popup tag");
    requireContains(vc, "_digiRecordInputChanged_v159_:",
                    "DIGI record has an input source action");
    requireContains(vc, "pop.enabled = enabled && !_digiRecordActive_v158_",
                    "DIGI record input popup is enabled only while not recording");
    requireAbsent(vc, "DIGI REC SET MAC INPUT FIRST",
                    "DIGI record no longer exposes fake named-device validation failures");
    requireContains(vc, "ArpSIDDigiSetDefaultInputDevice_v227",
                  "DIGI record can route a selected REC source by temporarily switching default input");
    requireContains(vc, "AudioObjectSetPropertyData(kAudioObjectSystemObject",
                  "DIGI record selected-source routing uses CoreAudio default input switching");
    requireContains(vc, "_digiRestorePreviousRecordInputDevice_v159_",
                    "DIGI record restores the previous input device on stop/failure");
    requireContains(vc, "DIGI REC INPUT %@",
                    "DIGI record status action reports selected input source");
    requireContains(vc, "installTapOnBus:0 bufferSize:tapFrames format:format",
                    "DIGI record captures from the selected/default input through an AVAudioEngine tap");
    requireContains(vc, "_digiCommitRecordedMono_v158_:_digiPendingTakeMonoFloat_v199_.data()",
                    "DIGI KEEP commits a safe main-thread pending take through the sample-bank loader");
    requireContains(vc, "[(id<ArpSIDDebugAdapterLike>)adapter setDigiModel:&_digiModel_v563_ sampleBank:&_digiSampleBank_v596_];",
                    "DIGI record uses the same atomic model+sample-bank publish path as import");
    requireContains(vc, "STOP creates a pending take that must be KEEP/DISCARDed",
                    "DIGI REC button tooltip documents safe take workflow and overwrite safety");
    requireContains(vc, "ArpSIDDigiPrepareRecordedMono_v161",
                    "DIGI record preprocesses captured mono before quantize");
    requireContains(vc, "trimmedFrames",
                    "DIGI record trims leading/trailing silence before storing the clip");
    requireContains(vc, "Normalize recorded/imported input before 4-bit DIGI quantization",
                    "DIGI normalize control documents pre-quantization normalization");
    requireContains(vc, "DIGI REC SAVED S%u %u→%u frames",
                    "DIGI record HUD reports saved input and committed frame counts plus flags");
    requireContains(vc, "REC %.2fs  PEAK",
                    "DIGI runtime HUD/meter preserves live REC progress and level instead of only D418 telemetry");
    requireContains(vc, "if(_digiPlayLED) _digiPlayLED.on = digiVoices > 0 || digiPeak > 0.01f;",
                    "DIGI PLAY LED must indicate audible/playing DIGI, not merely configured slots");

    requireContains(vc, "@interface ArpSIDDigiNibbleLadderView : NSView",
                    "DIGI eyecandy has a dedicated $D418 nibble ladder view");
    requireContains(vc, "-(void)updateWithNibble:(uint8_t)nibble",
                    "DIGI nibble ladder exposes explicit realtime-safe update API");
    requireContains(vc, "ArpSIDDigiNibbleLadderView*_digiNibbleLadderView_v179_;",
                    "DIGI nibble ladder is owned by the controller as an ivar");
    requireContains(vc, "[_digiNibbleLadderView_v179_ updateWithNibble:tel.digiD418LastNibble",
                    "poll loop drives DIGI nibble ladder from live $D418 telemetry");
    requireContains(vc, "oldD418:tel.digiD418LastOldD418",
                    "DIGI nibble ladder shows the old preserved $D418 byte");
    requireContains(vc, "d418:tel.digiD418LastD418",
                    "DIGI nibble ladder shows the final $D418 byte");
    requireContains(vc, "recording:_digiRecordActive_v158_",
                    "DIGI nibble ladder reflects REC state without touching audio state");
    requireContains(vc, "the ladder sparkle is visual-only state",
                    "DIGI nibble ladder must decay GUI spark even when telemetry is stable");
    requireContains(vc, "if (!valueChanged && fabsf(oldSpark - _spark) <= 0.002f) return;",
                    "DIGI nibble ladder avoids stale permanent boost while remaining redraw-efficient");
    requireContains(vc, "[scopeBox addSubview:_digiNibbleLadderView_v179_];",
                    "DIGI eyecandy is mounted inside the D418 dashboard scope box");
    requireContains(vc, "[NSButton buttonWithTitle:@\"AUTH\"",
                    "DIGI auth preset button clearly selects AUTH C64-bus D418");
    requireContains(vc, "[NSButton buttonWithTitle:@\"AUTH 8K\"",
                    "DIGI default button must describe AUTH C64-bus D418 8 kHz policy honestly");
    requireContains(vc, "tel.digiD418TimelineDiscontinuityResetCount || tel.digiUnavailableUserImports",
                    "DIGI warning label color must include timeline discontinuity resets, not only the warning string text");
    requireContains(vc, "Detailed D418 route/counters/warnings",
                    "DIGI HUD tooltip must use honest D418 naming rather than stale AUTH wording");
    requireContains(vc, "DIGI $D418 nibble ladder",
                    "DIGI eyecandy tooltip documents AUTH/private $D418 semantics honestly");
    requireAbsent(vc, "buttonWithTitle:@\"C64\"\n                                                            target:self\n                                                            action:@selector(_digiAuthSetModePreset_v115_:)",
                  "DIGI standalone D418 preset must not be labeled C64");
    requireAbsent(vc, "buttonWithTitle:@\"D418 8K\"",
                  "DIGI default preset must not use vague standalone D418 8K label");
    requireAbsent(vc, "AUTH DIGI dashboard",
                  "DIGI dashboard tooltip must not use stale AUTH naming");
    requireAbsent(vc, "OK: C64 bus → open-bus/SID decode → $D418 volume DAC",
                  "DIGI warning OK string must not imply main C64 bus mutation for standalone D418 mode");
    requireAbsent(vc, "C64 SID $D418",
                  "DIGI mode label must not imply main C64 SID bus authority");
    requireAbsent(vc, "FAST SID $D418",
                  "DIGI fast mode label must not imply main SID bus authority");
    requireAbsent(vc, "AUTH/FAST =>",
                  "DIGI pad tooltip must use standalone/fast naming, not stale AUTH naming");
    requireAbsent(vc, "DIGI AUTH HUD cleared",
                  "DIGI clear-HUD text must use honest D418 naming, not stale AUTH naming");

    requireContains(adapterHeader, "setDigiModel:(const ArpSID::GUI::DigiPanelModel*)model\n          sampleBank:(const ArpSID::GUI::DigiSampleBankBlob*)bank",
                    "adapter exposes one-shot DIGI model+bank publication");
    requireContains(adapter, "- (void)setDigiModel:(const ArpSID::GUI::DigiPanelModel*)model\n          sampleBank:(const ArpSID::GUI::DigiSampleBankBlob*)bank",
                    "adapter implements one-shot DIGI model+bank publication");
    requireContains(adapter, "_digiPair_v596_.model = *model;\n        _digiPair_v596_.bank = *bank;",
                    "combined DIGI publish stores both blobs in the locked pair before kernel publication");
    requireContains(adapter, "- (void)_publishSanitizedGuiRealtimeModels_v150",
                    "adapter has a single sanitized full-tuple publish helper");
    requireContains(adapter, "digiRepairUserSampleReferences(_digiPair_v596_.model, _digiPair_v596_.bank);\n    if (_kernel) {\n        _kernel->publishGuiRealtimeModels(&_mixModel_v561_,",
                    "full-tuple publish repairs a paired DIGI snapshot immediately before kernel publication");
    requireContains(adapter, "includeDigiSampleBank ? &_digiPair_v596_.bank : nullptr",
                    "full-tuple publish splits the large sample bank from the normal realtime GUI mailbox");
    requireContains(adapter, "[self _publishSanitizedGuiRealtimeModels_v150];",
                    "MIX/KIT/setup/DIGI publishes route through the sanitized full-tuple helper");
    requireAbsent(adapter, "Legacy split selectors below update adapter shadow only",
                  "adapter comments must not claim legacy split setters still mutate shadows");
    requireContains(adapter, "Legacy split setter: fail closed. Even shadow-only mutation is unsafe",
                    "legacy split DIGI setters must not mutate live shadows that later MIX/KIT publishes could expose");
    requireContains(adapter, "would indirectly publish a half-updated DIGI pair. New callers must use",
                    "legacy split DIGI setters document the indirect publish hazard");
    requireContains(adapterHeader, "compatibility only; fail-closed no-op. Use atomic pair API",
                    "legacy split DIGI setter contract is documented as fail-closed no-op");
    requireContains(adapter, "Legacy split getter: fail closed with an empty/default model",
                    "legacy split DIGI model getter must not return the live half of the pair");
    requireContains(adapter, "*out = ArpSID::GUI::makeDefaultDigiPanelModel();",
                    "legacy split DIGI model getter returns a fail-closed default model");
    requireContains(adapter, "Legacy split getter: fail closed with an empty/default bank",
                    "legacy split DIGI sample-bank getter must not return the live half of the pair");
    requireContains(adapter, "ArpSID::GUI::resetDigiSampleBankBlob(*out);",
                    "legacy split DIGI sample-bank getter resets the caller bank in place");
    requireContains(adapterHeader, "split writes are no-ops and split reads return default",
                    "adapter header documents fail-closed split DIGI read/write compatibility stubs");
    requireAbsent(adapter, "Legacy split setter: update adapter shadow only",
                  "legacy split DIGI setters must not keep the unsafe shadow-only mutation contract");
    requireContains(vc, "[adapter respondsToSelector:@selector(getDigiModel:sampleBank:)]",
                    "DIGI UI restore/pull requires atomic model+bank readback");
    requireContains(vc, "- (void)getDigiModel:(ArpSID::GUI::DigiPanelModel*)model",
                    "debug adapter protocol must declare atomic DIGI readback getter used by ObjC++ callsite");
    requireContains(vc, "sampleBank:(ArpSID::GUI::DigiSampleBankBlob*)bank;",
                    "debug adapter protocol atomic getter must include sampleBank parameter");
    requireContains(vc, "AVCaptureDeviceTypeMicrophone, AVCaptureDeviceTypeExternal",
                    "macOS 14+ DIGI record input enumeration should use non-deprecated AVCaptureDeviceType constants");
    requireContains(vc, "if (adapter2 && [adapter2 respondsToSelector:@selector(getDigiModel:sampleBank:)])",
                    "connect-time DIGI pull probes the atomic readback selector, not the legacy split model getter");
    requireAbsent(vc, "if (adapter2 && [adapter2 respondsToSelector:@selector(getDigiModel:)])",
                  "connect-time DIGI pull must not probe the old split model getter");
    requireContains(vc, "ArpSID::GUI::digiRepairUserSampleReferences(_digiModel_v563_, _digiSampleBank_v596_);",
                    "DIGI UI pull repairs local model handles against the pulled sample bank");
    requireContains(vc, "if (!adapter || ![adapter respondsToSelector:@selector(setDigiModel:sampleBank:)])",
                    "async DIGI import refuses non-atomic adapters instead of falling back to split model/bank publication");
    requireContains(vc, "DIGI IMPORT ATOMIC ADAPTER MISSING",
                    "async DIGI import reports missing atomic adapter support");
    requireContains(vc, "[(id<ArpSIDDebugAdapterLike>)adapter setDigiModel:&strongSelf->_digiModel_v563_\n                                                        sampleBank:&strongSelf->_digiSampleBank_v596_];",
                    "async DIGI import publishes exactly one matched model+bank snapshot atomically");
    requireAbsent(vc, "[(id<ArpSIDDebugAdapterLike>)adapter setDigiSampleBank:&strongSelf->_digiSampleBank_v596_];",
                  "async DIGI import must not fallback to split sample-bank publication");
    requireAbsent(vc, "-(void)_pushDigiStateAndSampleBankToAdapter_v141_",
                  "stale duplicate DIGI combined-push helper must not remain");
    requireContains(vc, "DIGI ATOMIC ADAPTER MISSING",
                    "DIGI atomic push helper reports missing atomic adapter support");
    requireContains(vc, "[(id<ArpSIDDebugAdapterLike>)adapter setDigiModel:&_digiModel_v563_ sampleBank:&_digiSampleBank_v596_];",
                    "model-only DIGI UI changes publish the matched model+bank pair atomically");
    requireContains(vc, "[self _digiAuditAndRepairSampleBankForUIReason:@\"before publish\"];\n    [(id<ArpSIDDebugAdapterLike>)adapter setDigiModel:&_digiModel_v563_ sampleBank:&_digiSampleBank_v596_];",
                    "ViewController repairs and surfaces its local DIGI model+bank shadow before atomic publish");
    requireContains(vc, "DIGI repaired %@ — corrupted sample bank/model sanitized",
                    "DIGI sample-bank/model corruption repair is surfaced in HUD");
    requireAbsent(vc, "[(id<ArpSIDDebugAdapterLike>)adapter setDigiModel:&_digiModel_v563_];",
                  "model-only DIGI UI changes must not use the old split model-only publication path");
    requireAbsent(vc, "[self _pushDigiSampleBankToAdapter_v596_];\n    [self _pushDigiStateToAdapter_v565_];",
                  "combined DIGI push helper must not fallback to split publication");
    requireAbsent(vc, "[self _pushDigiStateAndSampleBankToAdapter_v141_]",
                  "stale duplicate DIGI combined-push helper must not be called");

    requireContains(audioUnit, "- (void)_restoreDigiStateAndSampleBankFromStateDictionary:(NSDictionary<NSString*,id>*)state",
                    "AU state restore uses a combined DIGI model+sample-bank helper");
    requireContains(audioUnit, "[_adapter setDigiModel:&model sampleBank:bank.get()];",
                    "AU state restore publishes matched DIGI model+bank snapshot atomically when both blobs exist");
    requireContains(audioUnit, "if (haveModel && haveBank) {",
                    "AU state restore treats a paired DIGI model+bank as one atomic restore unit");
    requireContains(audioUnit, "Do not fall back to two separate publishes when both blobs are present",
                    "AU state restore documents and enforces no split fallback for paired DIGI blobs");
    requireContains(audioUnit, "No partial DIGI restore: model and sample-bank are a matched persistence",
                    "AU state restore drops incomplete DIGI blobs instead of split-publishing one side");
    requireAbsent(audioUnit, "// Partial restore fallback",
                  "AU state restore must not keep partial/split DIGI restore fallback");
    requireAbsent(audioUnit, "[_adapter setDigiSampleBank:&bank];",
                  "AU state restore must not publish sample bank alone");
    requireAbsent(audioUnit, "[_adapter setDigiModel:&model];",
                  "AU state restore must not publish model alone");
    requireAbsent(audioUnit, "_restoreDigiSampleBankFromStateDictionary",
                  "AU state restore must not publish DIGI sample bank through an old separate helper");
    requireAbsent(audioUnit, "_restoreDigiStateFromStateDictionary",
                  "AU state restore must not publish DIGI model through an old separate helper");
    requireContains(audioUnit, "[_adapter getDigiModel:&digiModel sampleBank:bank.get()];",
                    "AU fullState serializes a matched DIGI model+sample-bank snapshot");
    requireAbsent(audioUnit, "Older separate getters are only a compatibility fallback",
                  "AU fullState must not document or keep separate DIGI getter fallback");
    requireAbsent(audioUnit, "[_adapter getDigiModel:&digiModel];",
                  "AU fullState must not serialize DIGI model through separate getter fallback");
    requireAbsent(audioUnit, "[_adapter getDigiSampleBank:&bank];",
                  "AU fullState must not serialize DIGI sample bank through separate getter fallback");
    requireContains(audioUnit, "[self _restoreDigiStateAndSampleBankFromStateDictionary:state];",
                    "setFullState/setFullStateForDocument call the atomic DIGI restore helper");



    // DIGI AUTH closure must remain hard-isolated from the main C64 SID bridge.
    requireContains(kernel, "ArpSID::C64::C64SidBridgeState digiD418Bridge_{};",
                    "kernel owns a dedicated private DIGI D418 bridge");
    requireContains(kernel, "digiD418Bridge_.engine = nullptr;",
                    "private DIGI bridge cannot call the live SID register engine");
    requireContains(kernel, "digiD418Bridge_.mirrorSink = nullptr;",
                    "private DIGI bridge cannot call the main C64 mirror sink");
    requireContains(kernel, "digiD418Bridge_.deferEngineWrites = true;",
                    "private DIGI bridge records timed writes only");
    requireContains(kernel, "digiD418_.processToSidBridge(guiRealtimeProjectionRender_.digi,",
                    "DIGI D418 processing receives the private timeline");
    requireContains(kernel, "digiD418Bridge_,",
                    "DIGI D418 processing writes to the private bridge");
    requireAbsent(kernel, "bridgeCountBeforeDigi",
                  "kernel must not snapshot only part of c64SidBridge_ for DIGI rollback");
    requireAbsent(kernel, "bridgeOverflowBeforeDigi",
                  "kernel must not partially rollback main bridge overflow state after DIGI");
    requireContains(kernel, "double digiPhi2Remainder_ = 0.0;",
                    "DIGI PHI2 timing keeps sub-cycle remainder across blocks");
    requireContains(kernel, "digiPhi2Remainder_ = exactPhi2Advance - wholePhi2AdvanceD;",
                    "DIGI PHI2 timing must not floor-and-drop fractional cycles each block");
    requireContains(kernel, "const std::uint64_t blockEndDigiPhi2 = blockStartDigiPhi2 + wholePhi2Advance;",
                    "DIGI kernel computes one authoritative PHI2 block end from the fractional accumulator");
    requireContains(kernel, "telemetryC64ProcessorPort_.load(std::memory_order_relaxed),\n                                        blockEndDigiPhi2);",
                    "DIGI stream engine receives the kernel-owned exact PHI2 block end");
    requireAbsent(kernel, "++wi;\n                        ++wi;",
                  "private DIGI D418 renderer must not skip every second timed write while draining the bridge queue");
    requireContains(kernel, "writeFrame = std::clamp(writeFrame, 0, numFrames - 1);",
                    "private DIGI D418 renderer maps every in-block PHI2 event into a valid host frame before discarding the private queue");
    requireContains(kernel, "#if defined(ARPSID_ENABLE_LEGACY_FLOAT_DIGI) && ARPSID_ENABLE_LEGACY_FLOAT_DIGI",
                    "legacy float DIGI is compile-time debug-gated");

    requireContains(vc, "AUTH C64-BUS D418",
                    "release GUI labels the default mode as C64-bus D418, not generic standalone");
    requireContains(vc, "FAST PRIVATE D418",
                    "release GUI labels fast mode as private/non-bus-authentic");
    requireContains(vc, "LEGACY FLOAT is debug-only",
                    "GUI tooltip documents legacy as debug-only when compiled in");
    requireContains(vc, "AUTH => C64-bus $D418/open-bus telemetry",
                    "release pad tooltip describes AUTH as C64-bus/open-bus D418");
    requireContains(vc, "FAST => private SID $D418",
                    "release pad tooltip describes FAST as private SID D418");
    requireContains(vc, "#if defined(ARPSID_ENABLE_LEGACY_FLOAT_DIGI) && ARPSID_ENABLE_LEGACY_FLOAT_DIGI",
                    "legacy float GUI text is guarded behind the debug compile flag");
    requireContains(vc, "std::clamp<NSInteger>(mode, 0, 1)",
                    "release GUI preset buttons cannot select legacy float mode");
    requireContains(vc, "if (idx < 0 || idx > 1) idx = 0;",
                    "release GUI popup cannot select legacy float mode");
    requireContains(vc, "safeDigiAuthPopupIndex = (NSInteger)std::min<std::uint8_t>(tel.digiAuthMode, 1u);",
                    "release telemetry polling must not select popup index 2 when the release popup has only two items");
    requireContains(vc, "DIGI DEFAULT: AUTH C64-BUS $D418 @ 8000 Hz",
                    "default action HUD uses release-auth naming instead of stale standalone wording");
    requireContains(vc, "DIGI AUTH C64-BUS D418  •  idle",
                    "initial DIGI HUD uses release-auth naming instead of stale standalone wording");
    requireContains(vc, "AUTH C64-BUS D418 preserves IO-bank/open-bus telemetry",
                    "badge tooltip explains AUTH/FAST policy without stale standalone wording");
    requireContains(vc, "case 0u: return @\"AUTH C64-BUS D418\";",
                    "mode name helper must not return stale STANDALONE naming");
    requireContains(vc, "case 0u: return @\"AUTH BUS\";",
                    "mode badge helper must not return stale STANDALONE naming");
    requireAbsent(vc, "STANDALONE D418",
                  "release UI must not expose stale STANDALONE D418 naming");
    requireContains(kernel, "safeDigiSampleRate",
                    "private D418 renderer guards sample-rate division with sanitized sample rate");
    requireContains(kernel, "w.phi2Cycle >= blockStartDigiPhi2",
                    "private D418 renderer clamps defensive pre-block PHI2 events instead of unsigned-underflowing");
    requireContains(kernel, "Advance the PHI2 counter for the next block using the same\n            // exact advance that was supplied to DigiD418StreamEngine above",
                    "DIGI PHI2 counter advance must share the same exact advance used for stream-engine block-end reconciliation");
    requireContains(readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/engines/digi_d418_stream_engine.h"),
                    "std::uint64_t exactBlockEndPhi2 = 0u",
                    "DigiD418StreamEngine exposes an owner-supplied exact block-end PHI2 parameter");
    requireContains(readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/engines/digi_d418_stream_engine.h"),
                    "const std::uint64_t blockEndPhi2 = (exactBlockEndPhi2 > blockStartPhi2)",
                    "DigiD418StreamEngine uses owner-supplied exact block end to avoid false fractional-clock discontinuities");
    const std::string digiD418Header = readTextFile(std::string(ARPSID_SOURCE_ROOT) + "/include/arpsid/engines/digi_d418_stream_engine.h");
    requireAbsent(digiD418Header, "static constexpr int kScopeLen = 128;\n    static constexpr int kScopeLen = 128;",
                  "DigiD418StreamEngine header must not duplicate kScopeLen; strict compilers reject duplicate class members");
    requireAbsent(digiD418Header, "switch back to StandaloneD418Layer",
                  "release-auth comments must not describe stale StandaloneD418Layer policy after AUTH/FAST rename");
    requireAbsent(kernel, "StandaloneD418Layer semantics",
                  "private renderer comments must describe isolated audio render semantics, not stale standalone policy");
    requireAbsent(kernel, "telemetryDigiConfiguredFactorySlots_.store(0u, std::memory_order_relaxed);\n        telemetryDigiConfiguredFactorySlots_.store(0u, std::memory_order_relaxed);",
                  "DIGI telemetry clear must not contain duplicate factory-slot store");

    requireAbsent(vc, "typeof(self)",
                  "ArpSIDViewController.mm must not use GNU typeof(self); ObjC++ release build rejects it");
    requireAbsent(vc, "__typeof__",
                  "ArpSIDViewController.mm must not use GNU __typeof__; strict ObjC++ AUv2 build rejects it");
    requireAbsent(vc, " ?: ",
                  "ArpSIDViewController.mm must not use GNU omitted-middle ?: extension");
    requireContains(vc, "AVCaptureDeviceDiscoverySession",
                    "DIGI record input enumeration uses AVCaptureDeviceDiscoverySession");
    requireContains(vc, "AVCaptureDeviceTypeMicrophone",
                    "macOS 14+ discovery uses non-deprecated microphone device type");
    requireContains(vc, "AVCaptureDeviceTypeExternal",
                    "macOS 14+ discovery uses non-deprecated external device type");
    requireAbsent(vc, "AVCaptureDeviceTypeBuiltInMicrophone",
                  "ArpSIDViewController.mm must not reference macOS-14-deprecated built-in microphone constant");
    requireAbsent(vc, "AVCaptureDeviceTypeExternalUnknown",
                  "ArpSIDViewController.mm must not reference macOS-14-deprecated external-unknown constant");
    requireAbsent(vc, "[AVCaptureDevice devicesWithMediaType:",
                  "ArpSIDViewController.mm must not call deprecated devicesWithMediaType: directly; use discovery session or runtime fallback");
    requireContains(vc, "__weak ArpSIDViewController* weakSelf = self;",
                    "DIGI async import/permission closures use portable Objective-C++ weak self typing");
    requireContains(adapterHeader, "@interface ArpSIDDSPKernelAdapter (DigiD418RuntimePolicyPass104)",
                    "D418 runtime methods are declared in the same category they are implemented in");
    requireContains(adapter, "@implementation ArpSIDDSPKernelAdapter (DigiD418RuntimePolicyPass104)",
                    "D418 runtime policy category implementation exists");

    // DIGI sampler product workflow closure.
    requireContains(vc, "kArpSIDDigiExportTag", "DIGI export button tag exists");
    requireContains(vc, "_digiExportSample_v199_", "DIGI active sample export action is wired");
    requireContains(vc, "_digiRawD418DataForClip_v199_", "DIGI raw D418 export serializer exists");
    requireContains(vc, "_digiWavPreviewDataForClip_v199_", "DIGI WAV preview export serializer exists");
    requireContains(vc, "kArpSIDDigiRecordKeepTag", "DIGI KEEP take button tag exists");
    requireContains(vc, "_digiKeepRecordedTake_v199_", "DIGI KEEP take action is wired");
    requireContains(vc, "kArpSIDDigiRecordDiscardTag", "DIGI DISCARD take button tag exists");
    requireContains(vc, "_digiDiscardRecordedTake_v199_", "DIGI DISCARD take action is wired");
    requireContains(vc, "_digiRecordOverwriteArmed_v199_", "DIGI REC overwrite safety arm is present");
    requireContains(vc, "_digiRecordOverwriteArmedSlot_v208_", "PASS208: DIGI REC overwrite arm is tied to the slot that produced the warning");
    requireContains(vc, "(!_digiRecordOverwriteArmed_v199_ || _digiRecordOverwriteArmedSlot_v208_ != slot)", "PASS208: DIGI REC cannot reuse an overwrite arm from another occupied slot");
    requireContains(vc, "_digiRecordOverwriteArmedSlot_v208_ = 0xFFu;", "PASS208: DIGI REC overwrite-arm slot is invalidated after slot changes/start/clear");
    requireContains(vc, "DIGI REC WOULD OVERWRITE", "DIGI REC overwrite warning is user-visible");
    requireContains(vc, "press REC again while still on S%u", "PASS208: DIGI REC overwrite warning tells the user the arm is slot-local");
    requireContains(vc, "ArpSIDDigiPCMBufferMonoSample_v238", "PASS238: REC/MON meters read all AVAudioPCMBuffer layouts, not only floatChannelData");
    requireContains(vc, "buffer.int16ChannelData", "PASS238: REC/MON meters handle Int16 CoreAudio input buffers");
    requireContains(vc, "buffer.int32ChannelData", "PASS238: REC/MON meters handle Int32 CoreAudio input buffers");
    requireContains(vc, "buffer.audioBufferList", "PASS238: REC/MON meters handle interleaved/raw AudioBufferList taps");
    requireContains(vc, "ArpSIDDigiAudioBufferHasReadableFrame_v239", "PASS239: REC/MON raw AudioBufferList reads are byte-size guarded");
    requireContains(vc, "mDataByteSize", "PASS239: REC/MON raw AudioBufferList reads must not assume enough bytes for a frame");
    requireContains(kernel, "Capture now sits directly after canonical SID rendering", "PASS239: Pure SID REC captures before drum/DIGI overlay and AU post-FX");
    requireContains(kernel, "capturePureSid1Q1RecordSource_(outputs, numFrames);\n        renderDrumBridgeIfActive_", "PASS239: Pure SID REC capture must be before bridge drums and DIGI sampler layer");
    requireContains(vc, "const NSUInteger maxFrames = strongSelf->_digiRecordMaxFrames_v160_", "PASS239: CoreAudio REC tap uses the configured record-capacity, not a stale literal");
    requireContains(vc, "WAITING FOR AU RENDER", "PASS240: Pure SID MON/REC must explain zero frames as host render not running instead of fake 0 signal");
    requireContains(vc, "SILENCE/NO SIGNAL", "PASS240: MON/REC must distinguish real silence from missing routing");
    requireContains(vc, "do not guess a random \"loopback-looking\" input", "PASS240: output-monitor source must not auto-pick unrelated BlackHole/Loopback and meter zeros");
    // output capture is now implemented for real via the macOS 14.2+
    // Core Audio process-tap API. The exact-loopback requirement is now only the
    // pre-14.2 fallback path, gated behind @available.
    requireContains(vc, "AudioHardwareCreateProcessTap", "PASS248: real output capture uses the Core Audio process-tap API");
    requireContains(vc, "CATapDescription", "PASS248: output capture builds a CATapDescription global tap");
    requireContains(vc, "kAudioAggregateDeviceTapListKey", "PASS248: output capture routes the tap through a private aggregate device");
    requireContains(vc, "_digiStartOutputTapForPreview_v248_", "PASS248: REC and MON share a real output-capture backend start path");
    requireContains(vc, "_digiOutputTapIngestRecord_v248_", "PASS248: output-tap IOProc feeds the same record capture buffer");
    requireContains(vc, "NEEDS LOOPBACK INPUT (macOS < 14.2)", "PASS248: pre-14.2 fallback still requires an explicit loopback/aggregate input");
    requireContains(vc, "System Audio Capture:", "PASS248: output entries are labelled as real system-audio capture on supported systems");

    // device-bound AudioQueue input capture backend.
    requireContains(aqcap, "AudioQueueNewInput", "PASS251: input capture uses AudioQueueNewInput");
    requireContains(aqcap, "kAudioQueueProperty_CurrentDevice", "PASS251: capture binds to the selected device UID, not the global default");
    requireContains(aqcap, "AudioQueueAllocateBuffer", "PASS251: AudioQueue allocates capture buffers");
    requireContains(aqcap, "AudioQueueEnqueueBuffer", "PASS251: AudioQueue re-enqueues buffers in the input callback");
    requireContains(aqcap, "kAudioQueueProperty_IsRunning", "PASS251: AudioQueue running state is observed");
    requireContains(vc, "_digiShouldUseAudioQueueInput_v251_", "PASS251: REC/MON route normal inputs to the AudioQueue backend");
    requireContains(vc, "do not switch global default input", "selected input no longer mutates the global default input");
    // TAP BUSY must restore the previous default input (legacy cleanup bug fix).
    {
        // The REC-start drain-failure path must restore the previous default input
        // before returning on TAP BUSY (legacy cleanup bug). Anchor on the drain
        // guard so we check the start path, not the stop-path TAP BUSY message.
        const auto drain = vc.find("_digiWaitForRecordTapCallbacksToDrain_v185_:2000u]) {");
        require(drain != std::string::npos, "PASS251: REC-start drain guard present");
        const std::string window = vc.substr(drain, 600);
        require(window.find("_digiRestorePreviousRecordInputDevice_v159_") != std::string::npos,
                "PASS251: TAP BUSY (REC start) restores previous default input");
        require(window.find("DIGI REC TAP BUSY") != std::string::npos,
                "PASS251: drain guard reports TAP BUSY");
    }
    // ASBD-correct process-tap conversion (not Float32-only).
    requireContains(vc, "ArpSIDDigiOutputTapMixMonoFromASBD_v250", "PASS251: process tap mixes per the real ASBD");
    requireContains(vc, "kAudioFormatFlagIsNonInterleaved", "PASS251: process tap handles planar/non-interleaved");
    requireContains(vc, "mBitsPerChannel == 16", "PASS251: process tap handles Int16");
    requireContains(vc, "mBitsPerChannel == 24", "PASS251: process tap handles Int24");
    requireContains(vc, "mBitsPerChannel == 32", "PASS251: process tap handles Int32/Float32");
    // Silence-aware diagnostics.
    requireContains(vc, "NO CALLBACKS", "PASS251: diagnostic distinguishes no callbacks");
    requireContains(vc, "CALLBACKS BUT SILENCE", "PASS251: diagnostic distinguishes callbacks with silence");

    // ── audit closures ────────────────────────────────────────────────
    // P0-1: device mismatch fails closed (stop + return false), not just warn.
    requireContains(aqcap, "DIGI AQ DEVICE MISMATCH", "PASS252: device mismatch reported");
    {
        const auto mm = aqcap.find("DIGI AQ DEVICE MISMATCH");
        require(mm != std::string::npos, "PASS252: mismatch message present");
        const auto winStart = mm > 200 ? mm - 200 : 0;
        const std::string win = aqcap.substr(winStart, 400);
        require(win.find("stopLocked(true)") != std::string::npos && win.find("return false") != std::string::npos,
                "PASS252: device mismatch path fails closed (stop + return false)");
    }
    // P0-3: no std::vector allocation on the capture callback path.
    requireAbsent(aqcap, "out.assign(frames", "PASS252: converter must not allocate (assign) on the callback thread");
    // per-buffer scratch (no shared-scratch race) preallocated in start().
    requireContains(aqcap, "perBufferScratch_.assign(bufferCount", "PASS253: one mono scratch per AudioQueue buffer, preallocated in start()");
    requireContains(aqcap, "buffer->mUserData", "PASS683: callback uses direct per-buffer metadata context");
    requireAbsent(aqcap, "buffers_[i] == buffer", "PASS683: callback no longer scans buffer vectors");
    // fail closed on unsupported negotiated stream format before capture.
    requireContains(aqcap, "ArpSIDDigiAQFormatSupported", "PASS253: negotiated ASBD validated at start");
    requireContains(aqcap, "DIGI AQ UNSUPPORTED STREAM FORMAT", "PASS253: unsupported negotiated format fails closed");
    // multi-input channel capture (mic on ch 3/4).
    requireContains(aqcap, "ArpSIDDigiQueryInputChannelCount", "PASS253: requests all device input channels (multi-input support)");
    requireContains(aqcap, "kAudioDevicePropertyStreamConfiguration", "PASS253: device channel count queried from stream configuration");
    // deterministic immediate input stop.
    requireContains(aqcap, "AudioQueueStop(queue_, true)", "PASS253: input capture stops immediately (deterministic)");
    // property-listener status is recorded.
    requireContains(aqcap, "OSStatus listenSt = AudioQueueAddPropertyListener", "PASS253: AddPropertyListener status checked");
    // output-tap unsupported-format detection (no fake-silence garbage).
    requireContains(vc, "ArpSIDDigiTapASBDSupported_v253", "PASS253: output tap validates ASBD layout before converting");
    requireContains(vc, "kAudioFormatFlagIsAlignedHigh", "PASS253: output tap rejects aligned-high layouts");
    requireContains(vc, "DIGI OUTPUT TAP UNSUPPORTED FORMAT", "PASS253: output tap surfaces unsupported format in HUD");
    // MON generation token (stale callback/message guard, like REC).
    requireContains(vc, "aqPrevGen", "PASS253: MON ingest checks a generation token");
    requireContains(vc, "_digiPreviewGeneration_v253_", "PASS253: MON generation token exists");
    // P0-5/P1-6: unsupported format produces an explicit error + stat, not silent empty.
    requireContains(aqcap, "unsupportedFormatBuffers", "PASS252: unsupported-format buffers are counted");
    requireContains(aqcap, "kAudioFormatUnsupportedDataFormatError", "PASS252: unsupported format sets an explicit error");
    // P0-6: hardware level metering enabled + polled.
    requireContains(aqcap, "kAudioQueueProperty_EnableLevelMetering", "PASS252: AudioQueue hardware metering enabled");
    requireContains(aqcap, "kAudioQueueProperty_CurrentLevelMeterDB", "PASS252: AudioQueue hardware meter polled");
    // P1-1: actual negotiated stream description is read back.
    requireContains(aqcap, "kAudioQueueProperty_StreamDescription", "PASS252: actual stream description read back after bind");
    // P1-2: correct CF ownership for CurrentDevice readback.
    requireContains(aqcap, "CFBridgingRelease", "PASS252: CurrentDevice CFString ownership via CFBridgingRelease");
    // P0-4: AudioQueue REC ingest uses atomic fetch_add reservation, not load/store.
    requireContains(vc, "_digiRecordWriteFrames_v182_.fetch_add(chunk.frames", "PASS252: AQ REC ingest reserves its span atomically");
    // P1-11: generation token guards stale AQ callbacks.
    requireContains(vc, "aqRecGen", "PASS252: AQ REC ingest checks a generation token");
    // P0-2: popup fails closed on a missing selected UID.
    requireContains(vc, "Missing Input: ", "PASS252: missing selected input shown explicitly");
    requireContains(vc, "DIGI SELECTED INPUT MISSING", "PASS252: missing selected input fails closed, no default fallback");

    // ── hard closures ────────────────────────────────────────────────
    requireContains(aqcap, "ArpSIDDigiDefaultInputDeviceUID", "PASS254: default input UID is resolved for channel-count probing");
    requireContains(aqcap, "AQInputChannelMode", "PASS254: backend exposes explicit input-channel selection modes");
    requireContains(aqcap, "mBytesPerFrame != expected", "PASS254: AQ ASBD validates exact packed frame stride");
    requireContains(aqcap, "mBytesPerPacket != expected", "PASS254: AQ ASBD validates exact packet stride");
    requireContains(aqcap, "ArpSIDDigiAQSelectChannels", "PASS254: AQ converter selects explicit channel sets instead of hardcoded ch1/2");
    requireContains(aqcap, "unsupportedChannelBuffers", "PASS254: impossible selected-channel layouts are counted explicitly");
    requireContains(aqcap, "std::lock_guard<std::mutex> lk(queueMutex_)", "PASS254: non-callback AQ lifetime polling/stop is mutex-guarded");
    requireContains(vc, "ArpSIDDigiTapChannelSlotBytes_v254", "PASS254: output tap computes real per-channel slot stride");
    requireContains(vc, "ArpSIDDigiTapReadStatus_v254", "PASS254: output tap reader reports unsupported status, not fake zero samples");
    requireContains(vc, "slot == 3u", "PASS254: output tap accepts packed 24-bit only and rejects 24-in-32 until implemented");
    requireContains(vc, "abl->mNumberBuffers != 1u", "PASS254: ambiguous interleaved multi-buffer tap layouts fail closed");
    // v965+: the DIGI render call is formatted across multiple lines with the
    // output channel count as an explicit argument. Assert the call site and the
    // signature contract rather than a brittle single-line spelling.
    requireContains(kernel, "renderDigiSamplerLayer_(outputs,", "PASS254: DIGI render layer call passes the output buffers");
    requireContains(kernel, "int outputChannelCount", "PASS254: DIGI render layer receives output channel count");

    // ── final low-level hardening guards ─────────────────────────────
    requireContains(aqcap, "AQInputChannelMode::AutoStrongest", " AQ default/auto channel mode avoids all-channel dilution/cancellation");
    requireContains(aqcap, "callbacksInFlight_", " AQ callback lifetime is guarded before clearing vectors");
    requireContains(aqcap, "InFlightGuard", " AQ callbacks decrement in-flight count on every return path");
    requireContains(vc, "_digiOutputTapMonoScratchRing_v255_", " output tap uses scratch ring, not one shared buffer");
    requireContains(vc, "_digiOutputTapScratchIndex_v255_", " output tap selects scratch slots atomically");

    // ── final closure guards ────────────────────────────────────────
    requireContains(aqcap, "choose the strongest channel for the whole buffer", "AutoStrongest is stable per-buffer, not per-sample channel hopping");
    requireContains(vc, "_digiOutputTapCallbacksInFlight_v256_", " output tap IOProc has in-flight teardown guard");
    requireContains(vc, "_digiOutputTapCallbackCount_v256_", " output tap callback counter is atomic, not __block data-racy state");
    requireContains(vc, "array<std::vector<float>, 8>", " output tap scratch ring expanded to eight slots");
    requireContains(vc, "std::this_thread::sleep_for(std::chrono::milliseconds(1))", " output tap stop waits for in-flight IOProc blocks before reuse");

    // P1-9: no unsafe legacy Float32 fallback when ASBD is known but unsupported.
    requireContains(vc, "s->_digiOutputTapASBD_v250_.mFormatID == 0", "PASS252: legacy tap fallback only when ASBD unknown");
    // P0-6 surfaced: device-has-signal vs real-silence distinction.
    requireContains(vc, "DEVICE HAS SIGNAL BUT CONVERSION", "PASS252: diagnostic distinguishes hw-signal-but-conversion-zero");
    requireContains(vc, "Output Monitor (needs loopback):", "PASS243/248: pre-14.2 output entries remain labelled as loopback-required");
    requireContains(vc, "DIGI MON WAITING FOR TAP", "PASS243: MON must diagnose active graph with zero frames instead of silently showing 0");
    requireContains(vc, "select routed CoreAudio Input/BlackHole directly", "PASS243: output monitor diagnostic must tell users to select the actual loopback input directly");

    requireContains(vc, "kArpSIDDigiRecordMeterTag", "DIGI record progress/level meter tag exists");
    requireContains(vc, "_digiRecordPeak_v199_", "DIGI input peak meter state exists");
    requireContains(vc, "_digiRecordRms_v199_", "DIGI input RMS meter state exists");
    requireContains(vc, "REC %.2fs  PEAK", "DIGI record HUD shows duration and level");
    requireContains(vc, "TAKE READY S%u %.2fs @ %.0fHz", "PASS208: DIGI pending-take meter reports the pending slot and its captured sample rate");
    requireContains(vc, "System Default Input", "DIGI input selector is honest about system-default recording");
    requireAbsent(vc, "DIGI REC SET MAC INPUT FIRST", "DIGI input selector must not expose fake per-device routing failure");
    requireContains(vc, "TRUNC", "DIGI import/record truncation warning is visible");
    requireContains(vc, "ArpSIDDigiConvertMode_v199", "DIGI conversion weighting mode exists");
    requireContains(vc, "kArpSIDDigiNormalizeTag", "DIGI normalize toggle exists");
    requireContains(vc, "kArpSIDDigiTrimTag", "DIGI trim toggle exists");
    requireContains(vc, "_digiRenameSample_v199_", "DIGI sample rename action exists");
    requireContains(vc, "_digiCopySampleToNextSlot_v199_", "DIGI sample copy action exists");
    requireContains(vc, "_digiSwapSampleWithNextSlot_v199_", "DIGI sample swap action exists");
    requireContains(vc, "_digiExportKit_v199_", "DIGI kit export action exists");
    requireContains(vc, "_digiImportKit_v199_", "DIGI kit import action exists");

    // remaining DIGI sampler product guard: audition, drag/drop, MIDI learn and clear-all safety.
    requireContains(vc, "kArpSIDDigiAuditionTag", "DIGI PLAY/audition button tag exists");
    requireContains(vc, "_digiAuditionActiveSlot_v200_", "DIGI PLAY/audition action is wired");
    requireContains(vc, "triggerDigiPadSlot:slot velocity", "DIGI audition uses the same render-safe pad queue as GUI pads");
    requireContains(vc, "kArpSIDDigiMidiLearnTag", "DIGI MIDI learn button tag exists");
    requireContains(vc, "_digiMidiLearnToggle_v200_", "DIGI MIDI learn action is wired");
    requireContains(vc, "_digiApplyMidiLearnFromTelemetry_v200_:tel", "DIGI MIDI learn consumes live telemetry in the UI poll loop");
    requireContains(vc, "CC113(root) and CC114(channel) remain host-automation-safe", "DIGI learn UX documents host-automation-safe CC bridge");
    requireContains(vc, "registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]", "DIGI tab registers for drag/drop file URLs");
    requireContains(vc, "performDragOperation", "DIGI drag/drop performDragOperation is implemented");
    requireContains(vc, "_digiImportDraggedFileURL_v200_", "DIGI drag/drop routes into the same import pipeline");
    requireContains(vc, "kArpSIDDigiClearAllTag", "DIGI clear-all user bank button tag exists");
    requireContains(vc, "_digiClearAllUserSamples_v200_", "DIGI clear-all user bank action is wired with confirmation");
    requireContains(vc, "DROP AUDIO ON DIGI TAB", "DIGI drag/drop status label is visible");


    // complete remaining DIGI sampler product gaps: robust kit file headers, target-slot clipboard paste, and destructive trim UI.
    requireContains(vc, "ArpSIDDigiKitFileHeader_v201", "DIGI kit export uses a versioned/magic file header, not an opaque raw struct blob only");
    requireContains(vc, "kArpSIDDigiKitMagic_v201", "DIGI kit file format has a stable magic value");
    requireContains(vc, "payloadHash", "DIGI kit import/export validates payload hash");
    requireContains(vc, "DIGI KIT IMPORT HASH FAIL", "DIGI kit import fail-closes on hash mismatch");
    requireContains(vc, "data.length == legacyNeed", "DIGI kit import remains backward-compatible with PASS199 raw kit blobs");
    requireContains(vc, "kArpSIDDigiPasteTag", "DIGI paste button tag exists");
    requireContains(vc, "_digiPasteSampleToActiveSlot_v201_", "DIGI paste action is wired to the active target slot");
    requireContains(vc, "DIGI COPIED S%u TO CLIPBOARD", "DIGI copy is now explicit clipboard copy, not hidden copy-to-next only");
    requireContains(vc, "choose target slot and PASTE", "DIGI copy UX documents target-slot paste workflow");
    requireContains(vc, "kArpSIDDigiTrimStartTag", "DIGI destructive trim start slider tag exists");
    requireContains(vc, "kArpSIDDigiTrimEndTag", "DIGI destructive trim end slider tag exists");
    requireContains(vc, "_digiApplyDestructiveTrim_v201_", "DIGI destructive trim apply action is implemented");
    requireContains(vc, "DIGI TRIMMED S%u %u→%u frames", "DIGI destructive trim reports old/new frame counts");
    requireContains(vc, "buttonWithTitle:@\"PASTE\"", "DIGI paste button is actually instantiated in the panel, not only tagged");
    requireContains(vc, "action:@selector(_digiPasteSampleToActiveSlot_v201_:)", "DIGI paste button is wired to active-slot paste action");
    requireContains(vc, "buttonWithTitle:@\"APPLY TRIM\"", "DIGI destructive trim apply button is actually instantiated in the panel");
    requireContains(vc, "kArpSIDDigiTrimApplyTag", "DIGI trim apply button has a stable tag");
    requireContains(vc, "_digiAsmD418DataForClip_v202_", "DIGI sample export supports C64 assembler byte-table output");
    requireContains(vc, "@\"d418\", @\"raw\", @\"wav\", @\"asm\", @\"s\"", "DIGI export panel advertises raw, WAV and ASM formats");
    requireContains(vc, "Copy active user sample to the DIGI clipboard", "DIGI copy tooltip matches clipboard/paste behavior");
    requireAbsent(vc, "Copy active user sample into the next slot", "DIGI copy UI must not claim hidden copy-to-next behavior");
    requireContains(vc, "digiSetNoSource(_digiModel_v563_.slots[i])", "DIGI swap clears stale user-import references when the swapped-in target is empty");

    // final DIGI product hardening: Objective-C selector correctness and async export lifetime.
    requireContains(vc, "sampleRate:(std::uint32_t)std::clamp<double>(std::round(_digiPendingTakeSampleRateHz_v199_)",
                    "DIGI KEEP commits the pending take with its captured sample rate; stale name: selector mismatch is not allowed");
    requireContains(vc, "_digiRecordCommittedFrames_v161_ = clip.frameCount;",
                    "DIGI record HUD/telemetry must report persisted canonical D418 bank frames, not pre-truncation host-vector length");
    requireContains(vc, "digiWouldTruncateToUserSampleBank",
                    "PASS216: DIGI import/record truncation uses canonical 8 kHz D418 target-frame prediction, not host-frames vs persisted-frames");
    requireAbsent(vc, "prepared.samples.size() > (std::size_t)clip.frameCount",
                  "PASS216: DIGI record truncation must not compare host/prepared frames against persisted canonical bank frames");
    requireContains(vc, "preparedFrameCount_v205_",
                    "PASS205: DIGI record commit must pass the prepared take frame count into digiLoadUserSampleFromFloatMono");
    requireAbsent(vc, "prepared.samples.data(),\n                                                                     _digiRecordCommittedFrames_v161_",
                  "PASS205: DIGI record commit must not pass the zeroed persisted-count telemetry field as the source frame count");

    requireAbsent(vc, "name:[NSString stringWithFormat:@\"REC_SLOT_%u\"",
                  "DIGI KEEP must not call the old removed name: selector on _digiCommitRecordedMono_v158_");
    requireContains(vc, "const auto clip = _digiSampleBank_v596_.clips[slot];",
                    "DIGI async sample export captures a value copy, not a stack reference that can dangle after the save panel returns");
    requireContains(vc, "ACME .asm !byte table",
                    "DIGI export tooltip documents assembler output as well as raw/WAV");

    // microphone permission callback must record the slot where REC was requested.
    requireContains(vc, "_digiRecordPermissionRequestedSlot_v217_",
                    "PASS217: DIGI REC permission flow stores the requested slot");
    requireContains(vc, "DIGI REC MIC PERMISSION… S%u",
                    "PASS217: DIGI REC permission HUD shows the intended slot");
    requireContains(vc, "const std::uint8_t requestedSlot = strongSelf->_digiRecordPermissionRequestedSlot_v217_;",
                    "PASS217: permission callback captures requested slot before clearing state");
    requireContains(vc, "strongSelf->_digiModel_v563_.activeSlot = requestedSlot;",
                    "PASS217: permission callback restores intended slot before starting record");

    // GUI pad audition must be as honest as the active-slot PLAY button.
    requireContains(vc, "_digiSlotHasPlayableSource_v218_",
                    "PASS218: DIGI audition has one centralized playable-source predicate");
    requireContains(vc, "DIGI PAD AUDITION EMPTY SLOT",
                    "PASS218: GUI pad audition must not queue an empty/no-source slot");
    requireContains(vc, "btn.state = NSControlStateValueOff;",
                    "PASS218: empty GUI pad audition must not leave the pad visually queued");
    requireContains(vc, "DIGI PAD AUDITION ADAPTER MISSING",
                    "PASS218: GUI pad audition reports adapter failure without claiming queued audio");

    // the visible GUI affordances must also be honest, not merely the click handlers.
    requireContains(vc, "auditionBtn_v219.enabled = [self _digiSlotHasPlayableSource_v218_:slot];",
                    "PASS219: active PLAY button must be disabled when the active slot has no playable source");
    requireContains(vc, "pad_v219.enabled = [self _digiSlotHasPlayableSource_v218_:(std::uint8_t)i_v219];",
                    "PASS219: GUI pad buttons must be disabled for empty/no-source slots");
    requireContains(vc, "b.enabled = [self _digiSlotHasPlayableSource_v218_:(std::uint8_t)i];",
                    "PASS219: pad refresh must keep per-slot enabled state synchronized after source/model changes");


    // final product correctness: import must fail closed on silence, status must show real persisted C64 length, and kit export snapshots async state.
    requireContains(vc, "DIGI IMPORT SILENCE / EMPTY AFTER TRIM",
                    "PASS206: DIGI import must fail closed when trim/conversion leaves no audible sample; it must not silently store untrimmed silence");
    requireAbsent(vc, "const std::vector<float>& publishSamples = prepared.samples.empty() ? *mono : prepared.samples",
                  "PASS206: DIGI import must not fall back to raw mono when prepared samples are empty");
    requireContains(vc, "strongSelf->_digiLastTruncated_v199_ = truncated ? YES : NO;",
                    "PASS206: DIGI import must persist the truncation warning state after canonical bank-load");
    requireContains(vc, "%.0f ms  8kHz 4-bit",
                    "PASS206: DIGI user sample status must show persisted canonical D418 duration/rate/bit-depth");
    requireContains(vc, "likelyTruncated_v206_",
                    "PASS206: DIGI user sample status must flag bank-limit clips as likely truncated");
    requireContains(vc, "const auto modelSnapshot_v206_ = _digiModel_v563_;",
                    "PASS206: DIGI kit export must snapshot the model before async NSSavePanel completion");
    requireContains(vc, "const auto bankSnapshot_v206_ = _digiSampleBank_v596_;",
                    "PASS206: DIGI kit export must snapshot the sample bank before async NSSavePanel completion");
    requireAbsent(vc, "std::memcpy(bytes + sizeof(header), &_digiModel_v563_, modelBytes);",
                  "PASS206: DIGI kit export must not read live model state inside the async save completion");


    // central active-slot workflow refresh. Late-added sampler buttons must not keep stale enabled state after slot changes, kit import, clear or trim.
    requireContains(vc, "const BOOL activeHasUserClip_v207_ = [self _digiActiveSlotHasUserSample_v199_]",
                    "PASS207: central slot refresh computes whether active slot has a valid user sample");
    requireContains(vc, "exportBtn_v207.enabled = activeHasUserClip_v207_",
                    "PASS207: EXPORT button enabled state follows active slot sample presence");
    requireContains(vc, "renameBtn_v207.enabled = activeHasUserClip_v207_",
                    "PASS207: RENAME button enabled state follows active slot sample presence");
    requireContains(vc, "copyBtn_v207.enabled   = activeHasUserClip_v207_",
                    "PASS207: COPY button enabled state follows active slot sample presence");
    requireContains(vc, "swapBtn_v207.enabled   = activeHasUserClip_v207_",
                    "PASS207: SWAP button enabled state follows active slot sample presence");
    requireContains(vc, "clearBtn_v207.enabled  = activeHasUserClip_v207_",
                    "PASS207: CLEAR button enabled state follows active slot sample presence");
    requireContains(vc, "[self _digiRefreshClipboardAndTrimControls_v201_];\n    [self _digiRefreshRecordTakeButtons_v199_]",
                    "PASS207: slot refresh also updates PASTE/TRIM and record-take controls from one authority");


    // final slot replacement/take coherency. Any operation that replaces
    // or clears the active slot must invalidate a stale pending take for that same
    // slot, otherwise KEEP can later resurrect an older recording over the new
    // import/paste/trim/clear result.

    requireContains(vc, "KIT IN atomically replaces every DIGI slot/sample",
                    "PASS210: kit import documents that previous take/clipboard state is stale");
    requireContains(vc, "_digiClipboardClip_v201_ = ArpSID::GUI::DigiUserSampleClip{};",
                    "PASS210: successful kit import clears stale clipboard clip");
    requireContains(vc, "_digiClipboardHasClip_v201_ = NO;",
                    "PASS210: successful kit import disables paste from the previous kit");
    requireContains(vc, "DIGI KIT IMPORTED — stale take/clipboard cleared",
                    "PASS210: HUD confirms full kit import also cleared stale workflow state");

    // stale async import and all sample-source variants are guarded by
    // one generation counter, so a slow background decode cannot publish over a
    // newer kit import, clear, paste, trim, rename, source change or factory-slot change.
    requireContains(vc, "_digiSampleWorkflowGeneration_v211_",
                    "PASS211: DIGI sample workflow generation is present");
    requireContains(vc, "record KEEP/commit mutates the persisted user-sample bank",
                    "PASS212: record KEEP/commit must bump sample workflow generation");
    requireContains(vc, "destructive trim is a sample-bank mutation",
                    "PASS212: destructive trim must bump sample workflow generation");
    requireContains(vc, "clear mutates a user-sample slot",
                    "PASS212: clear-slot must bump sample workflow generation");

    requireContains(vc, "stale async import success must be completely silent",
                    "PASS211/PASS214: async DIGI import fails closed if newer sample/kit state wins, without stale HUD overwrite");
    requireContains(vc, "_digiIsSampleWorkflowGenerationCurrent_v211_",
                    "PASS211: async import checks generation before publishing");
    requireContains(vc, "_digiSetImportFailureHUDIfCurrent_v213_",
                    "PASS213: async import failure HUD exits are generation-gated too");
    requireContains(vc, "async import/decode has many fail-fast exits",
                    "PASS213: source documents stale failure-HUD variant being guarded");
    requireContains(vc, "_digiSetImportFailureHUDIfCurrent_v213_:importGeneration_v211_ message:@\"DIGI IMPORT READ FAILED\"",
                    "PASS213: import read failure must not overwrite newer workflow HUD");
    requireContains(vc, "_digiSetImportFailureHUDIfCurrent_v213_:importGeneration_v211_ message:@\"DIGI IMPORT SILENCE / EMPTY AFTER TRIM\"",
                    "PASS213: import empty-after-trim failure must be stale-generation safe");
    requireAbsent(vc, "DIGI IMPORT STALE — newer kit/sample edit won",
                  "PASS214: stale async import success must return silently, not overwrite HUD/status");
    requireContains(vc, "auto srcType = (ArpSID::GUI::DigiSourceType)",
                    "PASS211: DIGI source-type variants remain covered by source-change handler");
    requireContains(vc, "NSInteger idx = std::clamp((NSInteger)st.intValue",
                    "PASS211: factory-slot variants remain covered by factory-change handler");
    requireContains(vc, "selecting User Import with no valid clip must not leave",
                    "PASS215: User Import selection with no cached clip documents the dangling handle=0 failure mode");
    requireContains(vc, "digiSetNoSource(_digiModel_v563_.slots[slot]);",
                    "PASS215: empty User Import selection keeps the slot NoSource until a successful import publishes a real handle");
    requireContains(vc, "[self _digiImportSample_v596_:sender];\n            return;",
                    "PASS215: empty User Import selection returns immediately after opening import, avoiding dangling handle=0 publish");
    requireAbsent(vc, "ArpSID::GUI::digiSetUserSampleSlot(_digiModel_v563_.slots[slot], slot, handle);\n        if (handle == 0u)",
                  "PASS215: empty User Import selection must not publish dangling handle=0 state before showing import panel");
    requireContains(vc, "_digiInvalidatePendingTakeForSlot_v209_",
                    "PASS209: central helper exists to invalidate stale pending takes for a specific slot");
    requireContains(vc, "[self _digiInvalidatePendingTakeForSlot_v209_:slot reason:nil];",
                    "PASS209: active-slot publish/clear/trim paths invalidate stale pending takes for that slot");
    requireContains(vc, "[self _digiInvalidatePendingTakeForSlot_v209_:dst reason:nil];",
                    "PASS209: paste into active slot invalidates stale pending take for target slot");
    requireContains(vc, "[strongSelf _digiInvalidatePendingTakeForSlot_v209_:slot reason:nil];",
                    "PASS209: async import invalidates stale pending take before publishing replaced slot");
    requireContains(vc, "KEEP cannot later overwrite the newly",
                    "PASS209: source comment documents the exact stale-KEEP failure mode being guarded");


    // final callback/diagnostic hardening for DIGI REC/MON.
    requireContains(vc, "_digiOutputTapGeneration_v257_",
                    " output process-tap callbacks are generation-gated");
    requireContains(vc, "_digiOutputTapStopping_v257_",
                    " output process-tap callbacks reject teardown windows");
    requireContains(vc, "if (s->_digiOutputTapGeneration_v257_.load(std::memory_order_acquire) != outputTapGen) return;",
                    " stale output-tap IOProc callback cannot feed a newer REC/MON session");
    requireContains(aqcap, "ArpSIDDigiAQStartHint",
                    " AudioQueue start/bind failures include host-permission/device hints");
    requireContains(aqcap, "selectedChannelZeroBased.store(bestChannel",
                    " AutoStrongest publishes the selected input channel for diagnostics");
    requireContains(aqhdr, "effectiveChannels",
                    " AudioQueue stats expose effective requested channel count");
    requireContains(aqhdr, "channelQueryFailed",
                    " AudioQueue stats expose failed hardware channel probing");

    // final integration of stale-buffer fail-closed and stable channel auto-pick.
    requireContains(aqcap, "unknown buffer means stale callback",
                    " AudioQueue never re-enqueues unknown/stale buffers into possibly disposed queues");
    requireContains(aqhdr, "autoStrongestChannel_",
                    " AutoStrongest has sticky channel state, not buffer-to-buffer chatter");
    requireContains(aqcap, "sticky AutoStrongest with hysteresis",
                    " AutoStrongest changes channel only when materially stronger");
    requireContains(vc, "DIGI REC AQ FAILED: %@%@",
                    " REC AudioQueue failure HUD preserves OSStatus plus actionable hint");
    requireContains(vc, "DIGI MON AQ FAILED: %@%@",
                    " MON AudioQueue failure HUD preserves OSStatus plus actionable hint");

    // final lifetime exactness: AQ queue pointer gate, exact output-tap scratch slot claims, and malformed-buffer rejection.
    requireContains(aqhdr, "activeQueuePtr_",
                    " AudioQueue callbacks are gated by the currently-owned queue pointer");
    requireContains(aqcap, "activeQueuePtr_.load(std::memory_order_acquire) != reinterpret_cast<uintptr_t>(queue)",
                    " stale AudioQueue callbacks exit before touching buffer/scratch vectors");
    requireContains(aqcap, "mAudioDataByteSize % bytesPerFrame",
                    " malformed partial AudioQueue buffers fail closed instead of truncating frames");
    requireContains(vc, "_digiOutputTapScratchClaimed_v259_",
                    " output process-tap scratch slots use exact atomic claims, not modulo-only reuse");
    requireContains(vc, "ArpSIDDigiOutputTapScratchClaimGuard_v259",
                    " output process-tap scratch claims are released with RAII after IOProc conversion/ingest");
    requireContains(vc, "re-check generation after conversion before writing to REC/MON",
                    " output process-tap revalidates generation immediately before ingest");


    // final-final GUI/HUD preview telemetry polish without custom fullscreen chrome.
    requireAbsent(vc, "fullscreenButton",
                  " custom FULL/fullscreen HUD button ivar and wiring remain fully removed");
    requireContains(vc, " polished GPU/layer HUD preview with avg/peak/headroom meters",
                    " DIGI preview documents avg/peak/headroom/DC HUD contract");
    requireContains(vc, "_cursorLayer",
                    " DIGI preview has a GPU/layer cursor shimmer layer");
    requireContains(vc, "_sparkLayer",
                    " DIGI preview has transient sparkle/peak markers");
    requireContains(vc, "_meterAvgLayer",
                    " DIGI preview exposes avg level meter separate from peak meter");
    requireContains(vc, "_dcBiasLayer",
                    " DIGI preview exposes DC/bias marker for bad $D418 centering");
    requireContains(vc, "_headroomLayer",
                    " DIGI preview exposes headroom/clipping meter");
    requireContains(vc, "D%+03.0f",
                    " DIGI compact readout includes signed DC/bias telemetry");
    requireContains(vc, "digi_preview_cursor_pulse",
                    " cursor shimmer animation remains layer/GPU side, not audio-thread work");


    // final closure of residual custom HUD wording and preview text refresh.
    requireContains(vc, "COMPLETE C64 SYSTEM OVERVIEW",
                    " remaining visible FULL wording in C64 overview title is renamed to COMPLETE");
    requireContains(vc, "ROM KERNAL %@ BASIC %@ CHARGEN %@ SET %@",
                    " ROM completeness label no longer uses confusing FULL wording");
    requireContains(vc, "status text participates in the preview fingerprint",
                    " preview status labels refresh when status changes without waveform changes");
    requireContains(vc, "self.accessibilityLabel = self.toolTip",
                    " preview publishes tooltip/accessibility telemetry");
    requireAbsent(vc, "FULL C64 SYSTEM OVERVIEW",
                  " old FULL C64 overview title is gone");
    requireAbsent(vc, "CHARGEN %@ FULL %@",
                  " old ROM FULL visible label is gone");

    // GUI tab cleanup: one canonical tab-bar population path, no stale 11-segment bootstrap.
    requireContains(vc, "ArpSIDConfigureVisibleTabBar_v268",
                    " segmented tab bar is populated from the canonical visible-tab arrays only");
    requireContains(vc, "never from a stale",
                    " source documents why hard-coded bootstrap tabs are forbidden");
    requireAbsent(vc, "_tabBar.segmentCount=11",
                  " obsolete 11-segment bootstrap tab bar is removed");
    requireAbsent(vc, "setLabel:@\"◈ DRSID\" forSegment:3",
                  " duplicate stale DRSID bootstrap label is removed");
    requireContains(vc, "if(_tabBar) _tabBar.selectedSegment=t",
                    " prev/next tab handlers are nil-safe for embedded hosts");

    requireContains(vc, "TAKE→4BIT",
                    "pending REC TAKE preview is explicitly displayed as 4-bit $D418 visual, not raw PCM");
    requireContains(vc, "digiFloatToD418Nibble(std::clamp(src_v273",
                    "pending REC preview is quantized through the $D418 nibble ladder before drawing");
    requireContains(vc, "digiPcm8ToD418Nibble(clip.pcm[i])",
                    "committed waveform preview converts stored PCM8 back to canonical $D418 nibbles before drawing");
    requireContains(vc, "REC TAKE and committed samples are drawn as 4-bit $D418 nibbles",
                    "preview tooltip documents strict 4-bit visual contract");

    // massive canonical $D418 documentation and no-dead-comment guard.
    requireContains(d418Spec, "A committed ArpSID DIGI user sample is **not PCM**",
                    " technical spec must explicitly say committed DIGI is not PCM");
    requireContains(d418Spec, "canonical, 8 kHz stream of unsigned 4-bit SID `$D418` volume-DAC nibbles",
                    " technical spec must define canonical 8 kHz 4-bit $D418 nibbles");
    requireContains(d418Spec, "Pending REC TAKE",
                    " technical spec must document pending REC TAKE preview semantics");
    requireContains(d418Spec, "digiFloatToD418Nibble",
                    " technical spec must name the pending TAKE quantization helper");
    requireContains(d418Spec, "digiPcm8ToD418Nibble",
                    " technical spec must name the committed-clip nibble recovery helper");
    requireContains(d418Spec, "No render path calls full sample-bank strict validation",
                    " technical spec must preserve realtime lookup/validation contract");
    requireContains(vc, "this view is a canonical $D418 4-bit visualizer, not a raw",
                    "source comments must state preview is not a raw PCM oscilloscope");
    requireContains(vc, "Do not draw raw host PCM here",
                    "pending TAKE source comments must forbid raw host PCM drawing");


    // resource lifetime/state-restore final closure: popout observer helper, no strong self dispatch, display-link diagnostics.
    requireContains(vc, "_removeSidCorePopoutObservers_v279_",
                    " popout fullscreen observer tokens are removed through one canonical helper");
    requireContains(vc, "[self _removeSidCorePopoutObservers_v279_]",
                    " popout open/close/teardown all route through observer-token cleanup");
    requireContains(vc, "_sidCorePopoutWindow.contentView = nil",
                    " popout teardown detaches the content view before releasing the window");
    requireContains(vc, "VSync unavailable — using bounded UI timer",
                    " CVDisplayLink creation/timing failure is surfaced in the HUD AND falls back to a bounded timer instead of silently failing or pausing the UI");
    requireContains(vc, "VSync start failed — using bounded UI timer",
                    " CVDisplayLink start failure is surfaced in the HUD AND falls back to a bounded timer instead of silently failing");
    requireContains(vc, "_startPollFallbackTimer_v822_",
                    " v822: a bounded main-thread timer drives polling when no valid vsync is available (headless out-of-process AU host) so the link cannot free-run");
    requireContains(vc, "_displayLinkTickIsFreeRunning_v822_",
                    " v822: a free-running CVDisplayLink (no real vsync) is detected and handed over to the bounded timer");
    requireContains(vc, "CVDisplayLinkSetCurrentCGDisplay",
                    " v822: the display link is bound to the editor's concrete display so it has real vsync timing");
    requireContains(vc, "textField.allowsWritingTools = NO",
                    " dense plugin HUD labels disable macOS Writing Tools during AU view-service layout");
    requireContains(vc, "textView.writingToolsBehavior = NSWritingToolsBehaviorNone",
                    " any text views used by the editor opt out of Writing Tools in the AU host");
    requireContains(vc, "__weak ArpSIDViewController* weakSelf = self;\n        dispatch_async(dispatch_get_main_queue(), ^{\n            ArpSIDViewController* strongSelf = weakSelf;",
                    " cross-thread MIDI HUD update uses weak/strong capture, not a strong self block");
    requireAbsent(vc, "dispatch_async(dispatch_get_main_queue(), ^{ [self",
                  " no main-queue block keeps a strong self capture through shorthand [self ...]");

    // Logic AUv2 OOP bootstrap closure: build MAIN/chrome first, then lazily
    // construct heavy tabs only after the first-paint fence is released.
    const auto buildBegin = vc.find("-(void)_buildUI{");
    const auto buildEnd = vc.find("// ─── Panel builders", buildBegin);
    require(buildBegin != std::string::npos && buildEnd != std::string::npos,
            "_buildUI body can be isolated for startup-panel guard");
    const std::string buildBody = vc.substr(buildBegin, buildEnd - buildBegin);
    requireContains(buildBody, "_pMain=[self _mainPanel:cr];",
                    "startup builds the visible main panel");
    require(buildBody.find("_pMixV547 = [self _mixPanel_v547_") == std::string::npos &&
            buildBody.find("_pKitV555 = [self _kitPanel_v555_") == std::string::npos &&
            buildBody.find("_pDigiV563 = [self _digiPanel_v563_") == std::string::npos &&
            buildBody.find("_pSettingsV544 = [self _settingsPanel_v544_") == std::string::npos,
            "startup does not construct inactive MIX/KIT/DIGI/SETTINGS panels");
    requireContains(vc, "-(NSView*)_ensureContentPanelForTab_v825:",
                    "tab selection lazily constructs panels on demand");
    requireContains(vc, "const BOOL mixPanelVisible_v824 = (_tab == ArpSIDTabMixV547 && _pMixV547 && _pMixV547.superview == self.view);",
                    "state restore checks that MIX already exists and is visible before rebuilding it");
    requireContains(vc, "_mixPanelNeedsDeferredRebuild_v824 = YES;\n        return;",
                    "state restore defers MIX rebuild instead of constructing MIX during bootstrap");
    requireContains(vc, "v826-v830: first-paint fence for Logic/AUv2 OOP bootstrap",
                    "first-paint fence state is documented");

    const auto showBegin = vc.find("-(void)_showTab:(ArpSIDTab)t{");
    const auto showEnd = vc.find("-(void)_knobChg:", showBegin);
    require(showBegin != std::string::npos && showEnd != std::string::npos,
            "_showTab body can be isolated for first-paint guard");
    const std::string showBody = vc.substr(showBegin, showEnd - showBegin);
    require(showBody.find("if(!self.view)") != std::string::npos &&
            showBody.find("_tab = ArpSIDTabMain;") < showBody.find("!_firstEditorPaintCompleted_v826 && requested != ArpSIDTabMain"),
            "pre-load host/menu tab selection queues intent but does not build panels");
    require(showBody.find("!_firstEditorPaintCompleted_v826 && requested != ArpSIDTabMain") != std::string::npos &&
            showBody.find("_pendingBootstrapTab_v826 = requested") < showBody.find("[self _ensureContentPanelForTab_v825:_tab]"),
            "pre-paint tab selection cannot construct heavy lazy panels");
    require(showBody.find("[self _scheduleFirstPaintFenceRelease_v827_]") != std::string::npos,
            "late pre-paint tab selection arms the delayed release scheduler");

    const auto appearBegin = vc.find("-(void)viewDidAppear{");
    const auto appearEnd = vc.find("-(void)_prepareWindowForUserFullscreen", appearBegin);
    require(appearBegin != std::string::npos && appearEnd != std::string::npos,
            "viewDidAppear body can be isolated for first-paint release guard");
    const std::string appearBody = vc.substr(appearBegin, appearEnd - appearBegin);
    require(appearBody.find("[self _scheduleFirstPaintFenceRelease_v827_]") != std::string::npos &&
            appearBody.find("[self _completeFirstPaintFence_v826_]") == std::string::npos,
            "viewDidAppear cannot synchronously release queued heavy tab construction");
    requireContains(vc, "_firstEditorPaintReleaseRetryCount_v828",
                    "bounded retries are tracked when a remote AUv2 host delays window attachment");
    requireContains(vc, "_firstEditorPaintReleaseRetryCount_v828 < 30",
                    "window-attachment retry loop is bounded");
    const auto schedBegin = vc.find("-(void)_scheduleFirstPaintFenceRelease_v827_");
    const auto schedEnd = vc.find("-(void)viewDidAppear", schedBegin);
    require(schedBegin != std::string::npos && schedEnd != std::string::npos,
            "first-paint scheduler body can be isolated for no-view guard");
    const std::string schedBody = vc.substr(schedBegin, schedEnd - schedBegin);
    require(schedBody.find("if(!strongSelf2.view)") != std::string::npos &&
            schedBody.find("if(!strongSelf2.view)") < schedBody.find("if(!strongSelf2.view.window)"),
            "scheduler cannot complete/retry pending tab before loadView creates a root view");
    const auto layoutBegin = vc.find("-(void)viewDidLayout{");
    const auto layoutEnd = vc.find("-(void)_removeBridgeObserver", layoutBegin);
    require(layoutBegin != std::string::npos && layoutEnd != std::string::npos,
            "viewDidLayout body can be isolated for window-attach retry guard");
    const std::string layoutBody = vc.substr(layoutBegin, layoutEnd - layoutBegin);
    require(layoutBody.find("!_firstEditorPaintCompleted_v826 && self.view.window") != std::string::npos &&
            layoutBody.find("[self _scheduleFirstPaintFenceRelease_v827_]") != std::string::npos,
            "view/window attachment after appearance retriggers delayed fence release");

    std::cout << "gui_viewcontroller_wiring_v590_tests: top bar, DRSID panel, MIX controls, DIGI import atomic publish, and Logic lazy-tab bootstrap pinned\n";
    return 0;
}
