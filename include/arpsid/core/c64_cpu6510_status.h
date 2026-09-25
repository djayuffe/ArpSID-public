#pragma once

#include <cstdint>

namespace ArpSID::C64 {

static constexpr uint8_t kFlagC = 0x01u;
static constexpr uint8_t kFlagZ = 0x02u;
static constexpr uint8_t kFlagI = 0x04u;
static constexpr uint8_t kFlagD = 0x08u;
static constexpr uint8_t kFlagB = 0x10u;
static constexpr uint8_t kFlagUnused = 0x20u;
static constexpr uint8_t kFlagV = 0x40u;
static constexpr uint8_t kFlagN = 0x80u;

inline void cpuSetNZ(uint8_t& p, uint8_t v) noexcept {
    if (v == 0) p = static_cast<uint8_t>(p | kFlagZ);
    else p = static_cast<uint8_t>(p & static_cast<uint8_t>(~kFlagZ));
    if (v & 0x80u) p = static_cast<uint8_t>(p | kFlagN);
    else p = static_cast<uint8_t>(p & static_cast<uint8_t>(~kFlagN));
    p = static_cast<uint8_t>(p | kFlagUnused);
}

} // namespace ArpSID::C64

