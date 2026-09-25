#include "arpsid/core/c64_sid_mix.h"
#include "arpsid/gui/tab_architecture.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

static std::string slurp(const char* relative) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + relative, std::ios::binary);
    require(static_cast<bool>(in), relative);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int main() {
    using namespace ArpSID;
    using namespace ArpSID::C64;
    using namespace ArpSID::GUI;

    for (std::uint8_t chips = 1u; chips <= 5u; ++chips) {
        const auto gains = c64SidMixGains(chips);
        require(std::fabs(c64SidMixGainSum(chips) - 1.0f) < 1.0e-6f,
                "multi-SID gain budget sums to unity");
        require(gains.primary > 0.0f, "primary SID remains audible");
        if (chips > 1u) {
            require(gains.primary > gains.secondary, "primary SID remains dominant");
            require(gains.secondary > 0.0f, "secondary SIDs remain audible");
        }
    }

    const std::uint16_t flags =
        static_cast<std::uint16_t>((1u << 4u) | (2u << 6u) | (3u << 8u));
    require(psidSidModelBitsForChip(flags, 0u) == 1u, "SID1 model bits decoded");
    require(psidSidModelBitsForChip(flags, 1u) == 2u, "SID2 model bits decoded");
    require(psidSidModelBitsForChip(flags, 2u) == 3u, "SID3 model bits decoded");
    require(psidSidModelBitsForChip(flags, 3u) == 1u, "SID4 inherits primary model hint");
    require(psidSidModelWants6581(0u, true), "ambiguous model follows 6581 preference");
    require(!psidSidModelWants6581(3u, false), "both-model hint follows 8580 preference");

    require(kTabCount == 17u, "C64 STATE removal leaves 17 visible tabs");
    require(!isVisibleProductionTab(ArpSIDTab::C64State), "C64 STATE is not visible");
    require(isVisibleProductionTab(ArpSIDTab::Digi), "DIGI remains visible");

    const std::string kernel = slurp("source/au3/ArpSIDDSPKernel.hpp");
    const std::string view = slurp("source/au3/ArpSIDViewController.mm");
    const std::string host = slurp("source/au3/ArpSIDHostAppDelegate.mm");

    require(kernel.find("c64SidMixGains(activeSidChips") != std::string::npos,
            "C64 player uses bounded multi-SID gains");
    require(kernel.find("outL *= sidMixGains.primary;") != std::string::npos &&
            kernel.find("outL += ls * sidMixGains.secondary;") != std::string::npos,
            "primary and secondary SID gains are applied");
    require(kernel.find("if (nWrites == 0u) {\n            sreg_().renderBlock") == std::string::npos,
            "no-write blocks no longer drop secondary SID engines");
    // v873 audit items 5/6: render now consumes the parser's normalized per-chip model
    // (hdr.sidModel[ch]) rather than re-decoding raw PSID flags.
    require(kernel.find("psidSidModelBitsForChip(flags, ch)") == std::string::npos,
            "render no longer re-decodes raw PSID flags for per-chip SID model");
    require(kernel.find("hdr.sidModel[ch]") != std::string::npos,
            "render consumes normalized per-chip SID model metadata");

    require(view.find("if (requested == ArpSIDTabC64StateV544) return ArpSIDTabC64;") != std::string::npos,
            "persisted C64 STATE requests migrate to C64");
    require(view.find("_pC64StateV544 = nil;") != std::string::npos,
            "retired diagnostic panel is not constructed");
    require(view.find("_pSettingsV544,_pC64StateV544") == std::string::npos &&
            view.find("@[_presentationHUDLabel") == std::string::npos,
            "optional or retired GUI pointers are never inserted into Objective-C array literals");
    require(host.find("{ \"C64 STATE\", 15 }") == std::string::npos,
            "standalone View menu no longer exposes C64 STATE");
    require(view.find("case '3': idx = 3;") != std::string::npos &&
            view.find("case '5': idx = 5;") != std::string::npos,
            "command-number shortcuts use stable tab IDs");
    require(view.find("ArpSIDFitVisibleTabBarWidths_v320") != std::string::npos,
            "tab widths fit the available navigation row");
    require(view.find("fabs(currentWidth - desiredWidth) > 0.5f") != std::string::npos,
            "tab width fitting is idempotent and cannot perpetually invalidate layout");
    require(view.find("[tabBar setWidth:ArpSIDTabSegmentWidthForLabel_v269(label) forSegment:seg]") ==
                std::string::npos,
            "tab refresh does not expand every segment immediately before fitting it");
    const auto configureBegin = view.find("static void ArpSIDConfigureVisibleTabBar_v268");
    const auto configureEnd = view.find("// DIGI conversion mode must be declared", configureBegin);
    require(configureBegin != std::string::npos && configureEnd != std::string::npos,
            "tab-bar configuration helper remains present");
    const std::string configureBody = view.substr(configureBegin, configureEnd - configureBegin);
    require(configureBody.find("BOOL tabGeometryChanged_v823 = NO;") != std::string::npos &&
            configureBody.find("tabGeometryChanged_v823 = YES;") != std::string::npos,
            "tab width cache invalidation is driven by real segment-count/label changes");
    require(configureBody.find("tabGeometryChanged_v823 && [tabBar isKindOfClass") != std::string::npos,
            "ordinary tab chrome refreshes must not force segment-width remeasurement");
    require(view.find("-(void)_attachOnlyActiveContentPanel_v824") != std::string::npos &&
            view.find("if(panel.superview == root) {\n            [panel removeFromSuperview];") != std::string::npos,
            "Logic/AUv2 view bootstrap detaches inactive tab panels from root layout");
    require(view.find("for(NSView* p : panels) if(p) [root addSubview:p];") == std::string::npos,
            "initial UI build must not attach every hidden tab panel before first Logic layout");
    const auto buildBegin = view.find("-(void)_buildUI{");
    const auto buildEnd = view.find("// ─── Panel builders", buildBegin);
    require(buildBegin != std::string::npos && buildEnd != std::string::npos,
            "initial UI builder remains present");
    const std::string buildBody = view.substr(buildBegin, buildEnd - buildBegin);
    require(buildBody.find("_pMixV547 = [self _mixPanel_v547_:cr];") == std::string::npos &&
            buildBody.find("_pKitV555 = [self _kitPanel_v555_:cr];") == std::string::npos &&
            buildBody.find("_pDigiV563 = [self _digiPanel_v563_:cr];") == std::string::npos,
            "initial AUv2 view bootstrap must not construct heavy inactive MIX/KIT/DIGI panels");
    require(view.find("-(NSView*)_ensureContentPanelForTab_v825:") != std::string::npos &&
            view.find("_pMixV547 = [self _mixPanel_v547_:cr];") != std::string::npos &&
            view.find("NSView* activePanel_v825 = [self _ensureContentPanelForTab_v825:_tab];") != std::string::npos,
            "tab selection lazily constructs the selected content panel");
    require(view.find("_mixPanelNeedsDeferredRebuild_v824") != std::string::npos &&
            view.find("const BOOL mixPanelVisible_v824 = (_tab == ArpSIDTabMixV547 && _pMixV547 && _pMixV547.superview == self.view);") != std::string::npos,
            "restored MIX state must not rebuild the inactive 16-channel panel during AUv2 view bootstrap");
    require(view.find("if(_mixPanelNeedsDeferredRebuild_v824 && _pMixV547)") != std::string::npos &&
            view.find("_mixPanelNeedsDeferredRebuild_v824 = NO;") != std::string::npos,
            "deferred MIX rebuild is consumed only when the MIX panel exists on the visible path");
    require(view.find("_firstEditorPaintCompleted_v826") != std::string::npos &&
            view.find("_pendingBootstrapTab_v826") != std::string::npos,
            "Logic/AUv2 bootstrap queues non-MAIN tab selection until after first paint");
    const auto showBegin = view.find("-(void)_showTab:(ArpSIDTab)t{");
    const auto showEnd = view.find("-(void)_knobChg:", showBegin);
    require(showBegin != std::string::npos && showEnd != std::string::npos,
            "_showTab body remains available for first-paint fence guard");
    const std::string showBody = view.substr(showBegin, showEnd - showBegin);
    require(showBody.find("if(!self.view)") != std::string::npos &&
            showBody.find("_tab = ArpSIDTabMain;") < showBody.find("!_firstEditorPaintCompleted_v826 && requested != ArpSIDTabMain"),
            "pre-loadView tab selection queues intent without constructing panels");
    require(showBody.find("!_firstEditorPaintCompleted_v826 && requested != ArpSIDTabMain") != std::string::npos &&
            showBody.find("_pendingBootstrapTab_v826 = requested") < showBody.find("[self _ensureContentPanelForTab_v825:_tab]"),
            "_showTab defers heavy panel construction before first paint");
    require(showBody.find("[self _scheduleFirstPaintFenceRelease_v827_]") != std::string::npos,
            "late pre-paint tab selection arms the delayed first-paint scheduler");
    require(view.find("_firstEditorPaintReleaseScheduled_v827") != std::string::npos &&
            view.find("-(void)_scheduleFirstPaintFenceRelease_v827_") != std::string::npos,
            "first-paint fence release is scheduled after runloop/draw, not inline");
    require(view.find("_firstEditorPaintReleaseRetryCount_v828") != std::string::npos &&
            view.find("_firstEditorPaintReleaseRetryCount_v828 < 30") != std::string::npos,
            "window-attachment retry loop is bounded");
    const auto schedBegin = view.find("-(void)_scheduleFirstPaintFenceRelease_v827_");
    const auto schedEnd = view.find("-(void)viewDidAppear", schedBegin);
    require(schedBegin != std::string::npos && schedEnd != std::string::npos,
            "first-paint scheduler body remains available for no-root-view guard");
    const std::string schedBody = view.substr(schedBegin, schedEnd - schedBegin);
    require(schedBody.find("if(!strongSelf2.view)") != std::string::npos &&
            schedBody.find("if(!strongSelf2.view)") < schedBody.find("if(!strongSelf2.view.window)"),
            "scheduler must test missing root view before missing window");
    const auto appearBegin = view.find("-(void)viewDidAppear{");
    const auto appearEnd = view.find("-(void)_prepareWindowForUserFullscreen", appearBegin);
    require(appearBegin != std::string::npos && appearEnd != std::string::npos,
            "viewDidAppear body remains available for delayed-release guard");
    const std::string appearBody = view.substr(appearBegin, appearEnd - appearBegin);
    require(appearBody.find("[self _scheduleFirstPaintFenceRelease_v827_]") != std::string::npos &&
            appearBody.find("[self _completeFirstPaintFence_v826_]") == std::string::npos,
            "viewDidAppear must not construct pending heavy tab synchronously");
    const auto layoutBegin = view.find("-(void)viewDidLayout{");
    const auto layoutEnd = view.find("-(void)_removeBridgeObserver", layoutBegin);
    require(layoutBegin != std::string::npos && layoutEnd != std::string::npos,
            "viewDidLayout body remains available for window-attach retry guard");
    const std::string layoutBody = view.substr(layoutBegin, layoutEnd - layoutBegin);
    require(layoutBody.find("!_firstEditorPaintCompleted_v826 && self.view.window") != std::string::npos &&
            layoutBody.find("[self _scheduleFirstPaintFenceRelease_v827_]") != std::string::npos,
            "window attachment after appearance retriggers delayed first-paint release");
    const auto syncBegin = view.find("-(void)_syncNavigationState{");
    const auto syncEnd = view.find("-(void)_rewireEmbeddedVSTInteractiveControls", syncBegin);
    require(syncBegin != std::string::npos && syncEnd != std::string::npos,
            "navigation synchronization method remains present");
    const std::string syncBody = view.substr(syncBegin, syncEnd - syncBegin);
    require(syncBody.find("layoutSubtreeIfNeeded") == std::string::npos,
            "tab selection never forces nested synchronous AppKit layout");
    require(syncBody.find("_refreshModeAdaptiveChrome") == std::string::npos,
            "navigation state synchronization does not recursively rebuild tab chrome");

    std::cout << "C64SidPlayerAndUiClosureV746Tests PASS\n";
    return 0;
}
