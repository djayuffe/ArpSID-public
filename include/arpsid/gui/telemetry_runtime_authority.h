// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <algorithm>

namespace ArpSID::GUI {

inline bool telemetryPsidRuntimeOwnsClock(bool psidActive,
                                          bool c64PsidRuntimeActive) noexcept {
    return psidActive || c64PsidRuntimeActive;
}

inline bool effectiveTelemetryNtsc(bool psidActive,
                                   bool c64PsidRuntimeActive,
                                   bool c64Pal,
                                   bool uiFallbackNtsc) noexcept {
    return telemetryPsidRuntimeOwnsClock(psidActive, c64PsidRuntimeActive)
        ? !c64Pal
        : uiFallbackNtsc;
}

inline bool telemetryFileOwnsSidModel(bool psidActive,
                                      bool c64PsidRuntimeActive,
                                      bool c64SidModelFromFile) noexcept {
    return telemetryPsidRuntimeOwnsClock(psidActive, c64PsidRuntimeActive) &&
           c64SidModelFromFile;
}

inline int effectiveTelemetrySidModel(bool psidActive,
                                      bool c64PsidRuntimeActive,
                                      bool c64SidModelFromFile,
                                      int c64SidModel,
                                      int uiFallbackSidModel) noexcept {
    return telemetryFileOwnsSidModel(psidActive, c64PsidRuntimeActive, c64SidModelFromFile)
        ? std::clamp(c64SidModel, 0, 2)
        : std::clamp(uiFallbackSidModel, 0, 2);
}

} // namespace ArpSID::GUI
