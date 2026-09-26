// Copyright (C) 2024-2026 Ulf Bertilsson
#include "dsp_kernel_multi_tu_smoke_v634_common.h"
#include <cstdint>

extern "C" std::uint32_t arpsid_kernel_smoke_view_controller_v634() noexcept {
    return ArpSIDSmokeV634::mixKernelConstants(48879u);
}
