// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// v544_tab_wire_completeness_v546_tests.cpp
//
// Pins v544 tab wire-up completeness across ALL wrapper-side surfaces:
// A. tab_architecture.h inventory still holds 9 entries (all implemented as of v581)
// B. SettingsPanelModel is the model the live _settingsPanel_v544_ uses
// C. SidCorePanelModel (v545) is ready for the live C64-state panel
// D. New tab enum constants ArpSIDTabSettingsV544=14, ArpSIDTabC64StateV544=15
// do not collide with any existing tab index (compile-time pinned)
// E. v544 tab inventory order/spacing supports host automation stability
//
// The actual NSView builders + tab-bar wire-up are pinned by the AUv2
// build target succeeding (which it does — verified upstream).

#include "arpsid/gui/settings_panel_model.h"
#include "arpsid/gui/sidcore_panel_model.h"
#include "arpsid/gui/tab_architecture.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

} // namespace

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    // ── A. tab_architecture.h mirrors the shipped production ring ───────
    {
        require(kTabCount == 17, "17 visible tabs in architecture");
        require(implementedTabCount() == 17, "17 visible tabs implemented");
        require(nonImplementedTabCount() == 0, "0 non-implemented (v581)");
    }

    // ── B. SettingsPanelModel is wire-up-ready ──────────────────────────
    {
        const auto def = makeDefaultSettings();
        require(settingsIsWellFormed(def),
                "default settings well-formed");
        require(def.audioEngineMode == AudioEngineMode::BitPerfect,
                "default engine: BitPerfect (audit-correct)");
        require(def.drumEngineRouterOptIn == 1u,
                "DrumEngineRouter default ON for complete SID-808 production path");
        require(def.diagnosticDashboardEnabled == 1u,
                "diagnostic dashboard default ON for live C64 STATE telemetry");
    }

    // ── C. SidCorePanelModel is wire-up-ready ───────────────────────────
    {
        SidCorePanelModel model;
        SidCoreRegisterWriteEvent ev{};
        ev.sidCycleStamp = 1;
        ev.registerIndex = 0x04;
        model.publishRegisterWrite(ev);

        std::array<SidCoreRegisterWriteEvent, 4> out{};
        bool ov = false;
        require(model.drainRegisterWrites(out.data(), 4, &ov) == 1,
                "model drains 1 event after 1 publish");
        require(!ov, "no overflow");
    }

    // ── D. New tab enum constants (14, 15) don't collide ────────────────
    //
    // The ArpSIDTab enum in ArpSIDViewController.mm puts the v544 tabs
    // at indices 14 and 15. We can't include the .mm header here, so we
    // mirror the contract via the architecture header which carries
    // the same stable persisted IDs as the Cocoa controller.
    {
        require(static_cast<std::uint8_t>(ArpSIDTab::DRSID) == 13,
                "DRSID persisted ID 13");
        require(static_cast<std::uint8_t>(ArpSIDTab::SETTINGS) == 14,
                "SETTINGS persisted ID 14");
        require(static_cast<std::uint8_t>(ArpSIDTab::C64STATE) == 15,
                "C64 STATE persisted ID 15");
    }

    // ── E. Tab inventory order supports host automation stability ───────
    //
    // Every canonical tab is now implemented, while the original numeric
    // positions remain stable for host automation and source-shape tests.
    {
        require(isTabImplemented(ArpSIDTab::DRSID),
                "DRSID implemented (host automation stable)");
        require(isTabImplemented(ArpSIDTab::SID808),
                "SID808 mode-adaptive sequencer implemented");
        require(isTabImplemented(ArpSIDTab::SEQ),
                "SEQ implemented");
    }

    // ── F. SidCorePanelModel diagnostic counters surface correctly ──────
    {
        SidCorePanelModel model;
        require(model.lostRegisterWriteCount() == 0,
                "fresh model has 0 lost events");
        // Push 300 events to overflow the 256-slot ring.
        for (int i = 0; i < 300; ++i) {
            SidCoreRegisterWriteEvent ev{};
            ev.sidCycleStamp = static_cast<std::uint64_t>(i);
            model.publishRegisterWrite(ev);
        }
        // Drain — counter ticks when consumer was > capacity behind.
        std::array<SidCoreRegisterWriteEvent, 512> buf{};
        bool ov = false;
        model.drainRegisterWrites(buf.data(), buf.size(), &ov);
        require(ov, "overflow detected on drain");
        require(model.lostRegisterWriteCount() >= 1,
                "lost-event counter ticked");
    }

    std::cout << "v544_tab_wire_completeness_v546_tests: tab inventory + models wire-up complete (settings + sidcore live)\n";
    return 0;
}
