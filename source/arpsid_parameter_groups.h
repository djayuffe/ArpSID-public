// Copyright (C) 2024-2026 Ulf Bertilsson
// Host-facing parameter groups shared by the plug-in wrappers: one group per
// editor tab (MAIN ... DIGI, in tab order) holding the parameters that tab
// shows first, then "Other" for host-visible parameters without an editor
// control. AUv2 publishes them as parameter clumps; the VST3 controller builds
// its units from the same editor layout.
#pragma once

#include "gui/vstgui/arpsid_editor_layout.h"
#include "parameter_ids.h"

#include <array>
#include <cstdint>

namespace ArpSID {

// Group ids start at 1 (AU clump id 0 is reserved for "no clump").
inline constexpr std::uint32_t kParameterGroupFirstTab = 1u;
inline constexpr std::uint32_t kParameterGroupOther =
    kParameterGroupFirstTab + static_cast<std::uint32_t>(GUI::kTabCount);

// Group of a parameter id (computed once for all parameters).
inline std::uint32_t parameterGroupForParam(int id) noexcept {
    struct Table {
        std::array<std::uint8_t, static_cast<std::size_t>(kNumParams)> group{};
        Table() noexcept {
            for (int p = 0; p < kNumParams; ++p) {
                const int tab = GUI::EditorLayout::tabIndexForParam(p);
                group[static_cast<std::size_t>(p)] = static_cast<std::uint8_t>(
                    tab >= 0 ? kParameterGroupFirstTab + static_cast<std::uint32_t>(tab) : kParameterGroupOther);
            }
        }
    };
    static const Table table;
    if (id < 0 || id >= kNumParams) return kParameterGroupOther;
    return table.group[static_cast<std::size_t>(id)];
}

// Display name of a group, or nullptr for an unknown id.
inline const char* parameterGroupName(std::uint32_t group) noexcept {
    if (group == kParameterGroupOther) return "Other";
    if (group < kParameterGroupFirstTab || group >= kParameterGroupOther) return nullptr;
    const auto& tabs = GUI::EditorLayout::tabs();
    return GUI::tabSpec(tabs[group - kParameterGroupFirstTab].id).displayName;
}

} // namespace ArpSID
