// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/gui/tab_architecture.h"
#include <cassert>

using namespace ArpSID::GUI;

static_assert(kTabCount == 17u, "17 production tabs");
static_assert(isTabImplemented(ArpSIDTab::Digi), "DIGI production complete");
static_assert(isTabImplemented(ArpSIDTab::C64State), "C64 STATE production complete");
static_assert(!isVisibleProductionTab(ArpSIDTab::LegacySidProjection), "legacy projection hidden");
static_assert(!isVisibleProductionTab(ArpSIDTab::C64State), "C64 STATE diagnostics tab retired");

int main() {
    assert(kProductionVisibleTabs.front() == ArpSIDTab::Main);
    assert(kProductionVisibleTabs.back() == ArpSIDTab::Digi);
    for (const auto& tab : kProductionVisibleTabs) assert(isTabImplemented(tab));
    return 0;
}
