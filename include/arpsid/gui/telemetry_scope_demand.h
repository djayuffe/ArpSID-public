// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

namespace ArpSID::GUI {

struct TelemetryScopeDemand {
    bool editorVisible = false;
    bool sidCorePopoutVisible = false;
    bool mainScope = false;
    bool dedicatedVcoScopes = false;
    bool filter = false;
    bool sidCore = false;
    bool options = false;
    bool drSidSequence = false;
    bool drSidPanel = false;
    bool digi = false;
};

constexpr bool wantsTelemetryScopePayload(const TelemetryScopeDemand& d) noexcept {
    const bool anyVisibleConsumer = d.editorVisible || d.sidCorePopoutVisible;
    return anyVisibleConsumer &&
        (d.mainScope || d.dedicatedVcoScopes || d.filter || d.sidCore ||
         d.sidCorePopoutVisible || d.options || d.drSidSequence ||
         d.drSidPanel || d.digi);
}

} // namespace ArpSID::GUI
