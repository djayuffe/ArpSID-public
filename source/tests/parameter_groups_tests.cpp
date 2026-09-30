// Copyright (C) 2024-2026 Ulf Bertilsson
// Host parameter groups (arpsid_parameter_groups.h), published by AUv2 as
// parameter clumps: every host-visible parameter is in a named group, the
// groups are the editor tabs plus "Other", and unknown ids have no name.

#include "arpsid_parameter_groups.h"

#include <cstdio>
#include <cstring>
#include <set>

using namespace ArpSID;

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* msg) {
        if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); ++failures; }
    };
    std::set<std::uint32_t> used;
    int other = 0;
    for (int p = 0; p < kNumParams; ++p) {
        const std::uint32_t g = parameterGroupForParam(p);
        const char* name = parameterGroupName(g);
        check(g >= kParameterGroupFirstTab && g <= kParameterGroupOther, "group id in range");
        check(name && std::strlen(name) > 0, "every group has a name");
        used.insert(g);
        if (g == kParameterGroupOther) ++other;
    }
    check(parameterGroupForParam(kParamMasterVolume) == kParameterGroupFirstTab, "Master Volume is in the first tab (MAIN)");
    check(std::strcmp(parameterGroupName(kParameterGroupFirstTab), "MAIN") == 0, "the first group is MAIN");
    check(parameterGroupForParam(kParamHostCtrlModWheelBase) == kParameterGroupOther, "host MIDI mirrors are in Other");
    check(parameterGroupName(0) == nullptr && parameterGroupName(kParameterGroupOther + 1) == nullptr,
          "unknown group ids have no name");
    check(used.size() >= 10, "parameters spread over the tab groups");
    std::printf("parameter groups: %zu used, %d parameters in Other\n", used.size(), other);
    if (failures) return 1;
    std::puts("parameter_groups_tests PASS");
    return 0;
}
