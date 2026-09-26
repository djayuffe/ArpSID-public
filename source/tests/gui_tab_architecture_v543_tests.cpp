// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/gui/tab_architecture.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    require(kTabCount == 17u, "17 visible production tabs");
    require(kPersistedTabIdCount == 19u, "19 persisted IDs including hidden migration ID");
    require(kArpSIDTabs.size() == 18u, "retired C64 STATE keeps a migration spec");
    require(!isTabImplemented(ArpSIDTab::LegacySidProjection), "legacy projection is not implemented/visible");
    require(!isVisibleProductionTab(ArpSIDTab::LegacySidProjection), "legacy projection excluded");
    require(!isVisibleProductionTab(ArpSIDTab::C64State), "C64 STATE diagnostics tab retired");
    require(nonImplementedTabCount() == 0u, "no incomplete visible tabs");
    require(nonImplementedTabCountCompat() == 1u, "one compatibility-only ID");

    std::set<unsigned> ids;
    std::set<std::string> names;
    std::set<std::string> hud;
    for (std::size_t i = 0; i < kTabCount; ++i) {
        const auto id = kProductionVisibleTabs[i];
        const auto& spec = tabSpec(id);
        require(isTabImplemented(id), "visible tab implemented");
        require(visibleTabIndex(id) == static_cast<int>(i), "visible index round-trip");
        require(tabSpec(id).id == id, "tabSpec round-trip");
        require(ids.insert(static_cast<unsigned>(id)).second, "unique persisted ID");
        require(names.insert(spec.displayName).second, "unique display name");
        require(hud.insert(spec.hudLabel).second, "unique HUD label");
        require(std::strlen(spec.hudLabel) <= 6u, "HUD label fits");
        require(std::strlen(spec.description) > 30u, "description substantive");
    }

    require(tabSpec(ArpSIDTab::DrSid).primaryContext == DrumContext::DrSID_C64Wavetable,
            "DrSID context");
    require(tabSpec(ArpSIDTab::Sequencer).primaryContext == DrumContext::SID808_AnalogProjection,
            "mode-adaptive sequencer owns SID808 context");
    require(tabSpec(ArpSIDTab::Digi).primaryContext == DrumContext::Digi4Bit,
            "DIGI context");
    require(std::strlen(ImplementationContract::kDigiTabContract) > 100u, "DIGI contract");
    require(std::strlen(ImplementationContract::kKitTabContract) > 100u, "KIT contract");
    require(std::strlen(ImplementationContract::kMixTabContract) > 100u, "MIX contract");
    require(std::strlen(ImplementationContract::kSidCoreTabContract) > 100u, "SIDCORE contract");
    require(std::strlen(ImplementationContract::kC64StateTabContract) > 100u, "C64 contract");
    require(std::strlen(ImplementationContract::kSettingsTabContract) > 100u, "settings contract");

    std::cout << "GuiTabArchitectureV543Tests PASS\n";
    return 0;
}
