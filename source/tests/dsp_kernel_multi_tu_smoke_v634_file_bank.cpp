#include "dsp_kernel_multi_tu_smoke_v634_common.h"
#include <cstdint>

extern "C" std::uint32_t arpsid_kernel_smoke_file_bank_v634() noexcept {
    return ArpSIDSmokeV634::mixKernelConstants(61872u);
}
