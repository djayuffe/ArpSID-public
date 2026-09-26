// Copyright (C) 2024-2026 Ulf Bertilsson
// gm_drum_map_reference_v848_tests.cpp
//
// Guards the GUI-side GM drum key-map reference helpers used by the drum tabs.
// The mapping data lives in sid_gm_drum_kit.h; these helpers surface it to the
// user as pad tooltips and a full note->name->class table.

#include "arpsid/gui/gm_drum_map_reference.h"

#include <cstdio>
#include <string>

using namespace ArpSID::GUI;

static int g_failures = 0;
static void require(bool ok, const char* msg) {
    if (!ok) { std::printf("FAIL: %s\n", msg); ++g_failures; }
}
static bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

int main() {
    const std::string map = gmDrumMapReferenceText();

    // The full GM percussion range 35..81 is present, with canonical names.
    require(contains(map, "Bass Drum 1"),        "map includes note 36 Bass Drum 1");
    require(contains(map, "Acoustic Snare"),     "map includes note 38 Acoustic Snare");
    require(contains(map, "Closed Hi-Hat"),      "map includes note 42 Closed Hi-Hat");
    require(contains(map, "Open Hi-Hat"),        "map includes note 46 Open Hi-Hat");
    require(contains(map, "Cowbell"),            "map includes note 56 Cowbell");
    require(contains(map, "Open Triangle"),      "map includes note 81 Open Triangle");
    require(contains(map, "MIDI channel 10"),    "map states the GM drum channel");

    // Every note in 35..81 contributes a line (heuristic: 47 data rows + 3 header rows).
    int lines = 0;
    for (char c : map) if (c == '\n') ++lines;
    require(lines >= 47 + 3, "map has a row for every GM drum note plus header");

    // Pad tooltip formats the pad label, note, class and instrument name.
    const std::string tip = gmDrumPadTooltip(36, "KICK");
    require(contains(tip, "KICK"),        "pad tooltip shows the pad label");
    require(contains(tip, "36"),          "pad tooltip shows the MIDI note");
    require(contains(tip, "Kick"),        "pad tooltip shows the SID drum class");
    require(contains(tip, "Bass Drum 1"), "pad tooltip shows the GM instrument name");

    // Out-of-range note is clamped, not a crash/UB.
    const std::string clamped = gmDrumPadTooltip(999, "X");
    require(!clamped.empty(), "pad tooltip handles out-of-range note safely");

    if (g_failures == 0) { std::printf("gm_drum_map_reference_v848_tests: PASS\n"); return 0; }
    std::printf("gm_drum_map_reference_v848_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
