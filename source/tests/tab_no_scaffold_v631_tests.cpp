#include "arpsid/gui/tab_architecture.h"
#include <cstdlib>
#include <iostream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID::GUI;
    require(implementedTabCount() == kTabCount, "all tabs are implemented");
    require(nonImplementedTabCountCompat() == 1u, "one hidden compatibility-only ID remains");
    require(nonImplementedTabCount() == 0u, "no non-implemented tabs");
    for (std::size_t i = 0; i < kTabCount; ++i) {
        const auto tab = kProductionVisibleTabs[i];
        require(isTabImplemented(tab), "tab implemented");
        require(!isTabNonImplementedCompat(tab), "no tab is non-implemented");
    }
    require(std::string(tabImplementationStatusName(TabImplementationStatus::Implemented)) == "implemented",
            "implemented status string stable");
    require(isTabNonImplementedCompat(ArpSIDTab::LegacySidProjection),
            "legacy persisted projection ID is compatibility-only");
    std::cout << "TabNoNonImplementedV631Tests PASS\n";
    return 0;
}
