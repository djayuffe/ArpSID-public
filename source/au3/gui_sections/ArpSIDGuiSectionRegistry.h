// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
// ArpSIDViewController split registry.
//
// The monolithic controller now has explicit section ownership boundaries so new
// code can move out without creating another 10k-line coordinator. These helpers
// are intentionally header-only and side-effect free: they are safe for AUv2,
// AUv3 and VST Cocoa bridge builds and introduce no render-path dependency.
//
// Section header files (define ObjC category interface for each section):
// gui_sections/ArpSIDViewController_Private.h — shared ivar/extension header
// gui_sections/ArpSIDViewController_ChromeAndPalette.h — theme, palette, window chrome
// gui_sections/ArpSIDViewController_C64SidPlayer.h — C64 SID player panel + actions
// gui_sections/ArpSIDViewController_HiFiForensicPanel.h — HiFi + Forensic panels
// gui_sections/ArpSIDViewController_SidCoreTelemetry.h — SIDCORE + SID register panels
// gui_sections/ArpSIDViewController_MixKitDigi.h — MIX, KIT EDIT, DIGI panels
// gui_sections/ArpSIDViewController_SettingsBankDrSID.h — SETTINGS, BANK, DrSID, SEQ, …
//
// Migration status: section headers and interface declarations are complete.
// Method bodies still live in ArpSIDViewController.mm (pending category extraction).

namespace ArpSID::GUISections {

enum class Section : unsigned {
    ChromeAndPalette,
    ParameterBinding,
    PatchExtraction,
    SidCoreTelemetry,
    ScopeRendering,
    HiFiForensicPanel,
    C64SidPlayer,
    ImportExport,
};

static inline const char* sectionName(Section s) noexcept {
    switch (s) {
        case Section::ChromeAndPalette: return "chrome/palette";
        case Section::ParameterBinding: return "parameter-binding";
        case Section::PatchExtraction: return "patch-extraction";
        case Section::SidCoreTelemetry: return "sidcore-telemetry";
        case Section::ScopeRendering: return "scope-rendering";
        case Section::HiFiForensicPanel: return "hifi-forensic-panel";
        case Section::C64SidPlayer: return "c64-sid-player";
        case Section::ImportExport: return "import-export";
    }
    return "unknown";
}

static inline bool sectionIsUiOnly(Section) noexcept { return true; }
static inline bool sectionMayTouchRenderThread(Section) noexcept { return false; }

} // namespace ArpSID::GUISections
