// Copyright (C) 2024-2026 Ulf Bertilsson
// The cross-platform editor layout must reach every user parameter: every
// parameter is either on at least one tab or explicitly exempt (host MIDI
// mirrors, read-only telemetry, preset selectors, legacy mirrors), and the
// tab list matches the canonical production tab ring.

#include "gui/vstgui/arpsid_editor_layout.h"

#include <cstdio>
#include <set>

using namespace ArpSID;
using namespace ArpSID::GUI;

static int failures = 0;
static void require(bool ok, const char* msg) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; }
}

int main() {
    const auto& tabs = EditorLayout::tabs();
    for (std::size_t i = 0; i < tabs.size(); ++i)
        require(tabs[i].id == kProductionVisibleTabs[i], "editor tab order matches the production tab ring");

    std::set<int> onEditor;
    for (const auto& tab : tabs)
        for (const auto& row : tab.rows) {
            if (row.heightWeight <= 0.f) break;
            for (const auto& sec : row.sections) {
                if (!sec.title) break;
                for (int p = 0; p < kNumParams; ++p)
                    if (EditorLayout::displayEditsParam(sec.display, p)) onEditor.insert(p);
                for (int p : sec.params) {
                    if (p == EditorLayout::kEnd) break;
                    require(p >= 0 && p < kNumParams, "section parameter id in range");
                    require(!EditorLayout::isEditorExemptParam(p), "exempt parameter placed on a tab");
                    onEditor.insert(p);
                }
            }
        }

    int missing = 0;
    for (int p = 0; p < kNumParams; ++p) {
        if (EditorLayout::isEditorExemptParam(p) || onEditor.count(p)) continue;
        std::fprintf(stderr, "  parameter %d (%s) has no editor control\n", p, kParamInfos[(std::size_t)p].name);
        ++missing;
    }
    require(missing == 0, "every user parameter has an editor control");
    if (failures) return 1;
    std::printf("editor_layout_coverage_tests PASS (%zu parameters on %zu tabs)\n", onEditor.size(), tabs.size());
    return 0;
}
