// SPDX-License-Identifier: BSD-3-Clause
// Canonical product-tab identity and production navigation inventory.
#ifndef ARPSID_GUI_TAB_ARCHITECTURE_H
#define ARPSID_GUI_TAB_ARCHITECTURE_H

#include "arpsid/core/drum_context.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ArpSID::GUI {

// These values are persisted by existing hosts/projects. ID 2 is retained only
// as a migration token and is never part of the visible production ring.
enum class ArpSIDTab : std::uint8_t {
    Main                = 0,
    LfoArp              = 1,
    LegacySidProjection = 2,
    SidRegisters        = 3,
    Sequencer           = 4,
    Filter              = 5,
    Macro               = 6,
    Forensic            = 7,
    SidCore             = 8,
    Bank                = 9,
    Options             = 10,
    C64                 = 11,
    HiFi                = 12,
    DrSid               = 13,
    Settings            = 14,
    C64State            = 15,
    Mix                 = 16,
    Kit                 = 17,
    Digi                = 18,

    // Source-compatibility spellings for older model/tests. SID808 was never a
    // distinct persisted Cocoa tab; it is the mode-adaptive Sequencer surface.
    DRSID = DrSid,
    SID808 = Sequencer,
    DIGI = Digi,
    SEQ = Sequencer,
    KIT = Kit,
    MIX = Mix,
    SIDCORE = SidCore,
    C64STATE = C64State,
    SETTINGS = Settings,
};

inline constexpr std::size_t kPersistedTabIdCount = 19u;
inline constexpr std::size_t kTabCount = 17u;
inline constexpr std::size_t kTabSpecCount = 18u;

enum class TabImplementationStatus : std::uint8_t {
    Implemented = 0,
    CompatibilityOnly = 1,
};

constexpr const char* tabImplementationStatusName(TabImplementationStatus status) noexcept {
    return status == TabImplementationStatus::Implemented ? "implemented" : "compatibility-only";
}

struct TabSpec {
    ArpSIDTab id;
    const char* displayName;
    const char* hudLabel;
    const char* description;
    TabImplementationStatus status;
    DrumContext primaryContext;
};

// This order is the segmented-control order. It is shared directly by the
// Cocoa controller; there are no flavor-specific copies.
inline constexpr std::array<ArpSIDTab, kTabCount> kProductionVisibleTabs = {{
    ArpSIDTab::Main,
    ArpSIDTab::LfoArp,
    ArpSIDTab::SidRegisters,
    ArpSIDTab::Sequencer,
    ArpSIDTab::DrSid,
    ArpSIDTab::Filter,
    ArpSIDTab::Macro,
    ArpSIDTab::Forensic,
    ArpSIDTab::SidCore,
    ArpSIDTab::C64,
    ArpSIDTab::HiFi,
    ArpSIDTab::Bank,
    ArpSIDTab::Options,
    ArpSIDTab::Settings,
    ArpSIDTab::Mix,
    ArpSIDTab::Kit,
    ArpSIDTab::Digi,
}};

// C64State remains described here so projects that persisted ID 15 can migrate
// deterministically, but it is no longer part of the production navigation
// ring. Its diagnostics remain available internally and on the main C64/SIDCORE
// surfaces without shipping a duplicate inspector tab.
inline constexpr std::array<TabSpec, kTabSpecCount> kArpSIDTabs = {{
    {ArpSIDTab::Main, "MAIN", "MAIN", "Primary SID instrument, VCO, filter, envelope and output surface.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::LfoArp, "LFO / ARP", "LFO", "Four realtime LFOs and the host/internal arpeggiator.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::SidRegisters, "SID REG", "SIDREG", "Direct SID register editor with OSC3, ENV3, POT and voice scopes.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::Sequencer, "SEQ", "SEQ", "Global step sequencer and SID-808/DrSID performance surface.",
     TabImplementationStatus::Implemented, DrumContext::SID808_AnalogProjection},
    {ArpSIDTab::DrSid, "DRSID", "DRSID", "Dedicated C64 register-microprogram drum surface.",
     TabImplementationStatus::Implemented, DrumContext::DrSID_C64Wavetable},
    {ArpSIDTab::Filter, "FILTER", "FILTER", "Expanded filter controls and chronological input/output scopes.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::Macro, "MACRO", "MACRO", "Eight live macro controls and modulation routing.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::Forensic, "FORENSIC", "FOREN", "Analog-forensic controls, live VCO scopes and bus stress telemetry.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::SidCore, "SIDCORE", "SCORE", "GPU SID bus matrix, register timeline, chip state and VCO scopes.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::C64, "C64", "C64", "Complete realtime C64 CPU, VIC-II, CIA, SID, memory and debug cockpit.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::HiFi, "HI-FI", "HIFI", "Post-SID quality, safety and audible-delta monitoring.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::Bank, "BANK", "BANK", "Factory and user patch/bank browser covering the canonical slot space.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::Options, "OPTIONS", "OPT", "Compact runtime options and C64 control hub.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::Settings, "SETTINGS", "SET", "Topology, theme, language, diagnostics and canonical-policy status.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::C64State, "C64 STATE", "C64ST", "Dense CPU/VIC/CIA/SID/memory inspector and audit dashboard.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::Mix, "MIX", "MIX", "Per-instrument mixer, FX, sends, limiter and monitor controls.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::Kit, "KIT", "KIT", "DrSID, SID-808 and DIGI kit assignment and voice editor.",
     TabImplementationStatus::Implemented, DrumContext::None},
    {ArpSIDTab::Digi, "DIGI", "DIGI", "$D418 sample import, capture, pads, sequencing and bus telemetry.",
     TabImplementationStatus::Implemented, DrumContext::Digi4Bit},
}};

namespace detail {
constexpr std::size_t cstrLen(const char* s) noexcept {
    std::size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

constexpr bool visibleTabsAreUnique() noexcept {
    for (std::size_t i = 0; i < kProductionVisibleTabs.size(); ++i)
        for (std::size_t j = i + 1; j < kProductionVisibleTabs.size(); ++j)
            if (kProductionVisibleTabs[i] == kProductionVisibleTabs[j]) return false;
    return true;
}

// The HUD draws every tab's hudLabel inside a fixed 6-glyph status cell, so any
// label that overruns it silently truncates or clips on screen. Validate the
// whole table (previously only tabs [0] and [17] were spot-checked) and reject
// empty/null display text so a new tab cannot ship with a blank chip or a HUD
// label that overflows the cell.
inline constexpr std::size_t kHudLabelMaxGlyphs = 6u;

constexpr bool allTabTextWellFormed() noexcept {
    for (const auto& spec : kArpSIDTabs) {
        if (!spec.displayName || cstrLen(spec.displayName) == 0u) return false;
        if (!spec.hudLabel || cstrLen(spec.hudLabel) == 0u) return false;
        if (cstrLen(spec.hudLabel) > kHudLabelMaxGlyphs) return false;
        if (!spec.description || cstrLen(spec.description) == 0u) return false;
    }
    return true;
}

constexpr bool everyVisibleTabHasSpec() noexcept {
    for (const auto& tab : kProductionVisibleTabs) {
        bool found = false;
        for (const auto& spec : kArpSIDTabs)
            if (spec.id == tab) { found = true; break; }
        if (!found) return false;
    }
    return true;
}
} // namespace detail

static_assert(detail::visibleTabsAreUnique(), "production tab ring must contain unique IDs");
static_assert(detail::everyVisibleTabHasSpec(), "every visible tab must have a TabSpec");
static_assert(kProductionVisibleTabs[0] == ArpSIDTab::Main, "MAIN remains first");
static_assert(kProductionVisibleTabs.back() == ArpSIDTab::Digi, "DIGI remains last");
static_assert(kProductionVisibleTabs[2] != ArpSIDTab::LegacySidProjection,
              "legacy projection ID must never be visible");

constexpr const TabSpec& tabSpec(ArpSIDTab tab) noexcept {
    for (const auto& spec : kArpSIDTabs)
        if (spec.id == tab) return spec;
    return kArpSIDTabs[0];
}

constexpr bool isTabImplemented(ArpSIDTab tab) noexcept {
    if (tab == ArpSIDTab::LegacySidProjection) return false;
    return tabSpec(tab).id == tab &&
           tabSpec(tab).status == TabImplementationStatus::Implemented;
}

constexpr bool isTabNonImplementedCompat(ArpSIDTab tab) noexcept {
    return tab == ArpSIDTab::LegacySidProjection;
}

constexpr std::size_t implementedTabCount() noexcept { return kProductionVisibleTabs.size(); }
constexpr std::size_t implementedTabSpecCount() noexcept { return kArpSIDTabs.size(); }
constexpr std::size_t nonImplementedTabCount() noexcept { return 0u; }
constexpr std::size_t nonImplementedTabCountCompat() noexcept { return 1u; }

constexpr int visibleTabIndex(ArpSIDTab tab) noexcept {
    for (std::size_t i = 0; i < kProductionVisibleTabs.size(); ++i)
        if (kProductionVisibleTabs[i] == tab) return static_cast<int>(i);
    return -1;
}

constexpr bool isVisibleProductionTab(ArpSIDTab tab) noexcept {
    return visibleTabIndex(tab) >= 0;
}

static_assert(implementedTabCount() == 17u, "all 17 visible production tabs are implemented");
static_assert(implementedTabSpecCount() == 18u,
              "the retired C64 STATE persisted ID retains an implemented migration spec");
static_assert(nonImplementedTabCount() == 0u, "no visible production tab is incomplete");
static_assert(nonImplementedTabCountCompat() == 1u, "one hidden migration-only ID remains");

static_assert(detail::allTabTextWellFormed(),
              "every tab needs a non-empty name/description and a HUD label that fits the 6-glyph cell");

namespace ImplementationContract {
inline constexpr const char* kDigiTabContract =
    "DIGI provides import, capture, destructive trim, clipboard, eight audition pads, "
    "32-step sequencing, AUTH/FAST $D418 policy, chronological scope and complete bus telemetry.";
inline constexpr const char* kKitTabContract =
    "KIT edits DrSID register programs, SID-808 voice configuration and DIGI assignments, "
    "validates bounded payloads, and publishes one atomic render projection at block boundaries.";
inline constexpr const char* kMixTabContract =
    "MIX exposes channel gain, pan, solo, mute, five FX slots, delay/reverb sends, master width, "
    "dim and limiter controls; every visible control has a realtime model action.";
inline constexpr const char* kSidCoreTabContract =
    "SIDCORE consumes one coherent render-published telemetry frame for register timeline, "
    "chronological VCO/filter scopes, C64 bus state, Metal animation and bounded vector overlays.";
inline constexpr const char* kC64StateTabContract =
    "C64 STATE exposes CPU, VIC-II BA/AEC/DMA, CIA timers/TOD/serial/IRQ, SID readback, memory diffs, "
    "PSID lifecycle, ROM identity, bus scopes and diagnostics without direct GUI runtime reads.";
inline constexpr const char* kSettingsTabContract =
    "SETTINGS exposes only mutable topology, theme, language and diagnostic controls; fixed host-tempo, "
    "GM mapping and canonical routing policies are honest status rows instead of dead widgets.";
} // namespace ImplementationContract

} // namespace ArpSID::GUI

#endif
