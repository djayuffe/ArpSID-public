// Copyright (C) 2024-2026 Ulf Bertilsson
// gm_drum_map_reference.h
//
// GUI-side (non-realtime) helpers that turn the canonical General MIDI drum note
// table in sid_gm_drum_kit.h into human-readable reference text for the drum
// tabs: a per-pad tooltip and a full note->name->SID-class key map. This closes
// the "no visible GM drum mapping" gap — the mapping data already existed, but
// nothing surfaced it to the user.

#pragma once

#include "arpsid/core/sid_gm_drum_kit.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace ArpSID {
namespace GUI {

// Tooltip for a single drum trigger pad bound to `midiNote`.
inline std::string gmDrumPadTooltip(int midiNote, const char* padLabel) {
    const int n = std::clamp(midiNote, 0, 127);
    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "%s  \xE2\x80\x94  GM note %d (%s): %s\n"
                  "Triggers the DrSID / SID-808 drum engine on MIDI channel 10.",
                  (padLabel && padLabel[0]) ? padLabel : "Pad",
                  n,
                  sidGMDrumClassName(sidGMDrumClassForNote(static_cast<uint8_t>(n))),
                  sidGMDrumFullName(static_cast<uint8_t>(n)));
    return std::string(buf);
}

// Full GM percussion key map (notes 35..81) as a monospace-friendly table.
inline std::string gmDrumMapReferenceText() {
    std::string out;
    out.reserve(2560);
    out += "GM PERCUSSION KEY MAP  \xE2\x80\xA2  MIDI channel 10\n";
    out += "note  name                     SID drum class\n";
    out += "----  -----------------------  --------------\n";
    for (int n = 35; n <= 81; ++n) {
        char line[96];
        std::snprintf(line, sizeof(line), "%3d   %-23s  %s\n",
                      n,
                      sidGMDrumFullName(static_cast<uint8_t>(n)),
                      sidGMDrumClassName(sidGMDrumClassForNote(static_cast<uint8_t>(n))));
        out += line;
    }
    return out;
}

} // namespace GUI
} // namespace ArpSID
