#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << "\n";
        std::exit(2);
    }
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static bool lineExistsUncommented(const std::string& text, const std::string& needle) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(pos, end - pos);
        const std::size_t comment = line.find("//");
        const std::size_t hit = line.find(needle);
        if (hit != std::string::npos && (comment == std::string::npos || hit < comment)) return true;
        pos = end + 1;
    }
    return false;
}

static std::string sliceBetween(const std::string& text,
                                const std::string& beginNeedle,
                                const std::string& endNeedle,
                                std::size_t searchFrom = 0) {
    const std::size_t begin = text.find(beginNeedle, searchFrom);
    if (begin == std::string::npos) return {};
    const std::size_t end = text.find(endNeedle, begin + beginNeedle.size());
    if (end == std::string::npos) return text.substr(begin);
    return text.substr(begin, end - begin);
}

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    const std::string vc = readFile(std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDViewController.mm");
    const std::string vh = readFile(std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDViewController.h");
    const std::string au2 = readFile(std::string(ARPSID_SOURCE_ROOT) + "/source/au2/ArpSIDAUv2Component.mm");
    const std::string vst = readFile(std::string(ARPSID_SOURCE_ROOT) + "/source/gui/arpsid_vst_cocoa_bridge.mm");
    const std::string au3ext = readFile(std::string(ARPSID_SOURCE_ROOT) + "/source/au3/ArpSIDAUExtensionViewController.mm");

    require(vc.find("vc->_controllerTearingDown_v277_") == std::string::npos,
            "CVDisplayLink callback must not access private tearing-down ivar directly");
    require(vc.find("strongVC->_controllerTearingDown_v277_") == std::string::npos,
            "CVDisplayLink dispatch block must not access private tearing-down ivar directly");
    require(lineExistsUncommented(vc, "- (BOOL)_isControllerTearingDown_v277_;"),
            "private tearing-down state must be exposed through an Objective-C helper declaration");
    require(lineExistsUncommented(vc, "- (BOOL)_isControllerTearingDown_v277_ {"),
            "private tearing-down state must be exposed through an Objective-C helper implementation");
    require(vc.find("if ([vc _isControllerTearingDown_v277_] || [vc _isEditorRuntimePaused_v322_])") != std::string::npos,
            "CVDisplayLink callback must use lifecycle helpers before queuing a frame");
    require(vc.find("__weak ArpSIDViewController* weakForDispatch = vc;") != std::string::npos &&
            vc.find("ArpSIDViewController* strongForDispatch = weakForDispatch;") != std::string::npos &&
            vc.find("if (!strongForDispatch) return;") != std::string::npos &&
            vc.find("@finally {\n            [strongForDispatch _endPollTick_v246_];") != std::string::npos,
            "CVDisplayLink dispatch must weak-capture the controller and release its poll gate in @finally when still alive");
    require(vc.find("_editorRuntimePaused_v322_") != std::string::npos,
            "display polling must have an explicit reusable-pause latch");
    require(vc.find("-(void)viewWillDisappear{[super viewWillDisappear];[self _pauseReusableEditorRuntime_v322_];}") != std::string::npos,
            "viewWillDisappear must pause reusable runtime instead of destroying it");
    require(vh.find("- (void)pauseEditorViewForHostDetach;") != std::string::npos &&
            vh.find("- (void)prepareForFinalEditorDisposal;") != std::string::npos &&
            vh.find("- (void)reconnectExistingEditorViewToAudioUnit:") != std::string::npos,
            "public host lifecycle hooks must remain declared");
    require(vc.find("- (void)resumeAfterHostAttach {") != std::string::npos &&
            vc.find("_layersBuilt = NO;") != std::string::npos,
            "Metal surfaces must be reconstructible after host reattachment");
    require((au2.find("[existingController reconnectExistingEditorViewToAudioUnit:strongAudioUnit_v795") != std::string::npos ||
             au2.find("[existingController reconnectExistingEditorViewToAudioUnit:audioUnit") != std::string::npos),
            "AUv2 existing-view reuse must reconnect and resume its controller via a live AU reference");
    require(au2.find("[oldController viewWillDisappear]") == std::string::npos,
            "AUv2 factory must not manually invoke viewWillDisappear as a destructor");
    require(au2.find("[oldController prepareForFinalEditorDisposal]") != std::string::npos,
            "AUv2 factory must use explicit final disposal");
    require(vst.find("[_controllerVC viewWillDisappear]") == std::string::npos &&
            vst.find("[_controllerVC pauseEditorViewForHostDetach]") != std::string::npos,
            "VST detach must pause rather than destructively tear down the editor");
    require(vst.find("[handle.controllerVC prepareForEmbeddedVSTPresentation];\n        if") == std::string::npos,
            "VST resize must not rerun the entire presentation/bootstrap path");
    require(au3ext.find("layoutSubtreeIfNeeded") == std::string::npos,
            "AUv3 view-configuration changes must not force nested synchronous layout");
    require(au3ext.find("#import \"ArpSIDAudioUnit.h\"") != std::string::npos &&
            au3ext.find("[[ArpSIDAudioUnit alloc] initWithComponentDescription:d options:0 error:e]") != std::string::npos &&
            au3ext.find("[super createAudioUnitWithComponentDescription:d error:e]") == std::string::npos,
            "AUv3 extension factory must create the AU directly without touching UI controller state off-main");
    require(au3ext.find("dispatch_async(dispatch_get_main_queue(), ^{\n            ArpSIDAUExtensionViewController *strongSelf = weakSelf;") != std::string::npos &&
            au3ext.find("dispatch_sync(dispatch_get_main_queue(), ^{\n            accepted = [self selectViewConfiguration:viewConfiguration];") != std::string::npos,
            "AUv3 extension UI attachment/configuration must marshal back to main");
    require(vc.find("updateVsyncEyeCandyWithPhase:") != std::string::npos,
            "root visual polish must be driven by the coalesced display tick");
    require(vc.find("kArpSIDGuiIdlePollHz_v820") != std::string::npos &&
            vc.find("kArpSIDGuiActivePollHz_v820") != std::string::npos,
            "display-link driven GUI redraws must retain idle/active cadence limits");
    require(vc.find("_lastPollWallTime_v820_") != std::string::npos &&
            vc.find("_lastPollActive_v820_") != std::string::npos &&
            vc.find("now_v820 - _lastPollWallTime_v820_ < minPollInterval_v820") != std::string::npos,
            "heavy _poll telemetry/redraw path must be cadence-gated to avoid GUI-open CPU spin");
    require(vc.find("static constexpr double kArpSIDGuiIdlePollHz_v820 = 8.0;") != std::string::npos &&
            vc.find("static constexpr double kArpSIDGuiActivePollHz_v820 = 24.0;") != std::string::npos,
            "GUI cadence limits must keep idle open below active visual rate");
    require(vc.find("_lastPollActive_v820_ = (visualActivity > 0.012f || visualForensic > 0.012f ||") != std::string::npos &&
            vc.find("tel.hostPlaying || activeVoices > 0") == std::string::npos,
            "display poll must switch to active cadence only for real visual/audio activity");
    require(vc.find("ArpSIDSetPreferredContentSizeOnMainThread") != std::string::npos &&
            vc.find("dispatch_async(dispatch_get_main_queue(), ^{\n        controller.preferredContentSize = size;") != std::string::npos,
            "preferredContentSize updates must be marshalled to main for AUv3 extension construction");
    require(vc.find("self.preferredContentSize = NSMakeSize(kW, kH);") == std::string::npos,
            "ArpSIDViewController init must not set preferredContentSize directly off-main");

    const std::string connectAudioUnitBody =
        sliceBetween(vc, "-(void)connectAudioUnit:(AUAudioUnit*)au{", "-(void)connectBridge:");
    const std::string connectBridgeBody =
        sliceBetween(vc, "-(void)connectBridge:(id)bridge{", "-(void)beginHostPresetApply");
    const std::string connectBridgeInternalBody =
        sliceBetween(vc, "-(void)_connectBridgeInternal:(id<ArpSIDUIBridgeLike>)bridge{", "// ─── View Config");
    require(!connectAudioUnitBody.empty() && connectAudioUnitBody.find("_startTimer") == std::string::npos,
            "AUv2/AUv3 connectAudioUnit must only wire the bridge; display runtime starts after host window attach");
    require(!connectBridgeBody.empty() && connectBridgeBody.find("_startTimer") == std::string::npos,
            "generic bridge connect must not start CVDisplayLink before window attach");
    require(!connectBridgeInternalBody.empty() && connectBridgeInternalBody.find("_startTimer") == std::string::npos,
            "_connectBridgeInternal contract says it must not start UI runtime work");
    require(vc.find("@property(nonatomic, weak) ArpSIDViewController* lifecycleOwner;") != std::string::npos &&
            vc.find("_ev.lifecycleOwner=self;") != std::string::npos &&
            vc.find("editorRootViewDidAttachToWindow_v821") != std::string::npos &&
            vc.find("editorRootViewDidDetachFromWindow_v821") != std::string::npos,
            "raw AUv2 NSView must notify the controller on window attach/detach");
    require(vc.find("- (BOOL)_editorRuntimeMayRun_v821_") != std::string::npos &&
            vc.find("return (view && view.window) ? YES : NO;") != std::string::npos &&
            vc.find("if (![self _editorRuntimeMayRun_v821_]) return;") != std::string::npos,
            "CVDisplayLink start must be gated on an attached NSWindow");
    require(vc.find("if (!_pollWin) return;") != std::string::npos,
            "_poll must refuse unattached editor views even if a display callback slips through");
    {
        const std::size_t metalImpl = vc.find("@implementation ArpSIDSidCoreMetalBackdropView");
        const std::string metalInit =
            sliceBetween(vc, "- (instancetype)initWithFrame:(NSRect)frameRect {",
                         "- (void)configureAsC64SidBusSurface", metalImpl);
        const std::string metalResume =
            sliceBetween(vc, "- (void)resumeAfterHostAttach {",
                         "- (void)dealloc", metalImpl);
        require(!metalInit.empty() &&
                metalInit.find("MTLCreateSystemDefaultDevice") == std::string::npos &&
                metalInit.find("_buildShaderPipelineIfPossibleWithDevice") == std::string::npos,
                "SIDCORE MTKView constructor must not create Metal device/pipeline before AUHostingService attaches the view");
        require(!metalResume.empty() &&
                metalResume.find("if (!self.window) { self.paused = YES; return; }") != std::string::npos &&
                metalResume.find("MTLCreateSystemDefaultDevice") != std::string::npos,
                "SIDCORE Metal setup must be deferred to host-window attach/resume");
    }

    require(vc.find("kArpSIDKitDrumTagBase >= 0x9000") == std::string::npos,
            "KIT tag static_assert must not require the old/wrong 0x9000 lower bound");
    require(vc.find("kArpSIDKitDrumTagBase >= 0x6000") != std::string::npos,
            "KIT tag static_assert must allow the actual 0x6000 KIT tag range");
    require(vc.find("KIT tags must stay in the 0x6000-0xBFFF range and not collide with DIGI tags") != std::string::npos,
            "KIT tag static_assert message must document the actual 0x6000-0xBFFF range");

    std::cout << "Auv2ViewControllerCompileGuardV731Tests PASS\n";
    return 0;
}
