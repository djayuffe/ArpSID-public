// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include <array>
#include <cstdint>
#include "parameter_ids.h"

namespace ArpSID {

static constexpr int kSidRuntimeRegisterCount = 0x1E;

struct SidRuntimeRegisterShadow {
    std::array<uint8_t, kSidRuntimeRegisterCount> value{};
    std::array<uint8_t, kSidRuntimeRegisterCount> valid{};
    std::array<uint16_t, kSidRuntimeRegisterCount> sample{};
    std::array<uint16_t, kSidRuntimeRegisterCount> cycle{};

    void invalidate() noexcept {
        valid.fill(0u);
        sample.fill(0u);
        cycle.fill(0u);
    }
};

} // namespace ArpSID
