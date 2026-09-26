// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/gui/tab_architecture.h"
#include <cassert>
#include <cstring>

using namespace ArpSID::GUI;

static_assert(kTabCount == 17u, "canonical production ring has 17 tabs");
static_assert(implementedTabCount() == kTabCount, "all production tabs implemented");
static_assert(nonImplementedTabCount() == 0u, "no visible scaffold");
static_assert(nonImplementedTabCountCompat() == 1u, "hidden legacy ID retained");
static_assert(isTabImplemented(ArpSIDTab::Kit), "KIT implemented");
static_assert(isTabImplemented(ArpSIDTab::Mix), "MIX implemented");
static_assert(isTabImplemented(ArpSIDTab::SidCore), "SIDCORE implemented");
static_assert(isTabImplemented(ArpSIDTab::Settings), "SETTINGS implemented");
static_assert(isTabImplemented(ArpSIDTab::Digi), "DIGI implemented");
static_assert(isTabImplemented(ArpSIDTab::C64State), "C64 STATE implemented");

int main() {
    for (std::size_t i = 0; i < kTabCount; ++i) {
        const auto id = kProductionVisibleTabs[i];
        assert(tabSpec(id).id == id);
        assert(visibleTabIndex(id) == static_cast<int>(i));
        assert(std::strlen(tabSpec(id).hudLabel) <= 6u);
    }
    assert(!isVisibleProductionTab(ArpSIDTab::C64State));
    return 0;
}
