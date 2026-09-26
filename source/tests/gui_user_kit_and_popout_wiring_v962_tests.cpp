// Copyright (C) 2024-2026 Ulf Bertilsson
// v962 GUI wiring closure: DrSID user-kit library strip + SIDCORE popout Cmd-F.
//
// A reverse-wiring audit (defined action methods vs controls that reference
// them) found two shipped-but-unreachable GUI features:
//
// 1. The DrSID user-kit LIBRARY: `_drumUserKitPop` was declared and consumed
//    (reload/selection/enable logic all existed) but never constructed, so the
//    popup did not exist in any panel. Its handler `_drumUserKitPopChg:` and
//    the complete `_drumRefreshUserKitLibrary:` / `_drumExportUserKitBank:` /
//    `_drumImportUserKitBank:` implementations were dead — users could SAVE
//    kits (DrSID footer) but never load/manage them, even though the DrSID
//    panel label promises "switch to BANK for import/export and full library
//    management". The BANK panel now builds the strip (label + popup + RESCAN
//    / KIT EXP / KIT IMP) and reloads the library on build.
//
// 2. The SIDCORE popout advertised "(Cmd-F fullscreen)" in its window title
//    and its creation comment referenced an `ArpSIDSidCorePopoutWindow` key
//    handler "class" that did not exist — the popout was a plain NSWindow and
//    ⌘F did nothing. The subclass now exists (performKeyEquivalent: ⌘F toggles
//    fullscreen, ⌘W closes) and the popout is created from it.
//
// Source-text pins so the links cannot silently regress.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "gui_user_kit_and_popout_wiring_v962_tests FAIL: " << message << "\n"; std::exit(1); }
}

static std::string readFile(const char* relativePath) {
    std::ifstream file(std::string(ARPSID_SOURCE_ROOT) + "/" + relativePath, std::ios::binary);
    require(static_cast<bool>(file), relativePath);
    std::ostringstream out; out << file.rdbuf(); return out.str();
}

int main() {
    const std::string vc = readFile("source/au3/ArpSIDViewController.mm");

    // ── 1. DrSID user-kit strip in the BANK panel ───────────────────────────
    require(vc.find("_drumUserKitPop=[[NSPopUpButton alloc]") != std::string::npos,
            "the user-kit popup must actually be constructed (it was consumed but never created)");
    require(vc.find("_drumUserKitPop.action=@selector(_drumUserKitPopChg:);") != std::string::npos,
            "selecting a saved user kit must be wired to _drumUserKitPopChg:");
    require(vc.find("_drumUserKitPop.tag=-1;") != std::string::npos,
            "the popup (an NSButton subclass) must opt out of the factory-grid recolor loop (tag=-1)");
    require(vc.find("@\"_drumRefreshUserKitLibrary:\"") != std::string::npos,
            "RESCAN must be wired to _drumRefreshUserKitLibrary:");
    require(vc.find("@\"_drumExportUserKitBank:\"") != std::string::npos,
            "KIT EXP must be wired to _drumExportUserKitBank:");
    require(vc.find("@\"_drumImportUserKitBank:\"") != std::string::npos,
            "KIT IMP must be wired to _drumImportUserKitBank:");
    // The bank panel must populate the library after building the strip.
    const std::size_t bankPanel = vc.find("-(NSView*)_bankPanel:(NSRect)r{");
    require(bankPanel != std::string::npos, "_bankPanel builder must exist");
    const std::size_t bankPanelEnd = vc.find("// ─── Bank panel actions", bankPanel);
    require(bankPanelEnd != std::string::npos, "bank panel section end marker must exist");
    const std::string bankBody = vc.substr(bankPanel, bankPanelEnd - bankPanel);
    require(bankBody.find("[self _reloadDrumUserKitLibrary]") != std::string::npos,
            "the bank panel must reload the user-kit library so the popup is populated");
    require(bankBody.find("DRSID USER KITS") != std::string::npos,
            "the bank panel must label the user-kit strip");

    // ── 2. SIDCORE popout Cmd-F window ─────────────────────────────────────
    require(vc.find("@interface ArpSIDSidCorePopoutWindow : NSWindow") != std::string::npos,
            "the promised ArpSIDSidCorePopoutWindow class must exist");
    require(vc.find("_sidCorePopoutWindow = [[ArpSIDSidCorePopoutWindow alloc]") != std::string::npos,
            "the popout must be created from the Cmd-F-capable subclass, not plain NSWindow");
    const std::size_t popCls = vc.find("@implementation ArpSIDSidCorePopoutWindow");
    require(popCls != std::string::npos, "popout window implementation must exist");
    const std::string popBody = vc.substr(popCls, vc.find("@end", popCls) - popCls);
    require(popBody.find("performKeyEquivalent:") != std::string::npos &&
            popBody.find("toggleFullScreen:") != std::string::npos,
            "the popout window must handle the advertised Cmd-F fullscreen toggle");

    std::printf("gui_user_kit_and_popout_wiring_v962_tests PASS\n");
    return 0;
}
