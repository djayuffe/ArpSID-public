// Copyright (C) 2024-2026 Ulf Bertilsson
// The cross-platform editor layout must reach every user parameter: every
// parameter is either on at least one tab or explicitly exempt (host MIDI
// mirrors, read-only telemetry, preset selectors, legacy mirrors), and the
// tab list matches the canonical production tab ring.
//
// It also pins the stepped-parameter decode law the editor's menus and labels
// use (stepIndexForParam) to the engine's decoders, so a menu never names a
// different entry than the one the engine plays.

#include "gui/vstgui/arpsid_editor_labels.h"
#include "gui/vstgui/arpsid_editor_layout.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>

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

    // Decode law. Engine references:
    //   bitperfect_engine.h valueToWaveform: floor(clamp(v, 0, 0.999999) * 8)
    //   bitperfect_engine.h setFilterMode:    int(v * 8), clamped to 0..7
    //   arpeggiator.h setOctaves:             1 + int(v * 3)
    //   every other stepped parameter:        round(v * steps)
    using ArpSID::Editor::stepIndexForParam;
    using ArpSID::Editor::stepNormForIndex;
    for (int i = 0; i <= 4000; ++i) {
        const float v = static_cast<float>(i) / 4000.f;
        for (int id : {int(kParamVCO1Waveform), int(kParamVCO2Waveform), int(kParamVCO3Waveform)}) {
            const int engine = std::clamp(static_cast<int>(std::floor(std::min(v, 0.999999f) * 8.f)), 0, 7);
            require(stepIndexForParam(id, v) == engine, "waveform index follows the engine's floor(v*8)");
        }
        require(stepIndexForParam(kParamFilterMode, v) == std::clamp(static_cast<int>(v * 8.f), 0, 7),
                "filter mode index follows the engine's int(v*8)");
        require(stepIndexForParam(kParamArpOctaves, v) == static_cast<int>(v * 3.f),
                "arp octaves index follows the engine's int(v*3)");
        require(stepIndexForParam(kParamVoiceMode, v) == static_cast<int>(std::lround(v * 3.f)),
                "voice mode index rounds");
    }
    // Every stepped parameter: the value a menu writes decodes to its entry.
    for (int id = 0; id < kNumParams; ++id) {
        const int steps = static_cast<int>(normalizedParamStepCount(id));
        if (steps <= 0 || steps > 255) continue;
        for (int i = 0; i <= steps; ++i)
            if (stepIndexForParam(id, stepNormForIndex(id, i)) != i) {
                std::fprintf(stderr, "  parameter %d index %d does not round-trip\n", id, i);
                require(false, "stepped index round-trips through its normalized value");
                break;
            }
    }
    // A host value between grid points is labelled with what the engine plays.
    require(ArpSID::Editor::editorValueText(kParamVCO1Waveform, 0.42f) == "NOISE",
            "waveform 0.42 is labelled NOISE (engine floor(3.36) = 3)");
    require(ArpSID::Editor::editorValueText(kParamFilterMode, 0.26f) == "BAND-PASS",
            "filter mode 0.26 is labelled BAND-PASS (engine int(2.08) = 2)");

    if (failures) return 1;
    std::printf("editor_layout_coverage_tests PASS (%zu parameters on %zu tabs)\n", onEditor.size(), tabs.size());
    return 0;
}
