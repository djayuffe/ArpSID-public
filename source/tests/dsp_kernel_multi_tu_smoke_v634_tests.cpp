// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdint>
#include <cstdlib>
#include <iostream>

extern "C" std::uint32_t arpsid_kernel_smoke_auv2_component_v634() noexcept;
extern "C" std::uint32_t arpsid_kernel_smoke_au3_audio_unit_v634() noexcept;
extern "C" std::uint32_t arpsid_kernel_smoke_adapter_v634() noexcept;
extern "C" std::uint32_t arpsid_kernel_smoke_view_controller_v634() noexcept;
extern "C" std::uint32_t arpsid_kernel_smoke_file_bank_v634() noexcept;

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    const std::uint32_t a = arpsid_kernel_smoke_auv2_component_v634();
    const std::uint32_t b = arpsid_kernel_smoke_au3_audio_unit_v634();
    const std::uint32_t c = arpsid_kernel_smoke_adapter_v634();
    const std::uint32_t d = arpsid_kernel_smoke_view_controller_v634();
    const std::uint32_t e = arpsid_kernel_smoke_file_bank_v634();
    require(a != 0u && b != 0u && c != 0u && d != 0u && e != 0u,
            "all DSP kernel include translation units compiled and linked");
    require(a != b && b != c && c != d && d != e,
            "translation-unit smoke functions are distinct");
    std::cout << "DspKernelMultiTuSmokeV634Tests PASS\n";
    return 0;
}
