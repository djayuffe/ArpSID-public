#include "arpsid/gui/tab_architecture.h"
#include <cassert>
#include <cstring>

using namespace ArpSID::GUI;

static_assert(kTabCount == 17u, "canonical visible tab count");
static_assert(isTabImplemented(ArpSIDTab::C64State), "C64 STATE implemented");

int main() {
    const auto& spec = tabSpec(ArpSIDTab::C64State);
    assert(spec.id == ArpSIDTab::C64State);
    assert(std::strcmp(tabImplementationStatusName(spec.status), "implemented") == 0);
    assert(!isVisibleProductionTab(spec.id));
    return 0;
}
