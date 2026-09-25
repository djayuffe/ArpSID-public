#pragma once
#include "ArpSIDDSPKernel.hpp"
#include <cstdint>

namespace ArpSIDSmokeV634 {
inline std::uint32_t mixKernelConstants(std::uint32_t seed) noexcept {
    seed ^= static_cast<std::uint32_t>(kArpSIDDefaultSampleRate);
    seed *= 16777619u;
    seed ^= static_cast<std::uint32_t>(kArpSIDMinSampleRate);
    seed *= 16777619u;
    seed ^= static_cast<std::uint32_t>(kArpSIDMaxSampleRate);
    seed *= 16777619u;
    return seed ? seed : 1u;
}
}
