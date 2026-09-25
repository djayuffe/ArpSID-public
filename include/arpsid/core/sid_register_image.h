#pragma once
#include <cstdint>
#include <cstring>

namespace ArpSID {

static constexpr int kSidCanonicalRegCount = 32;
static_assert(kSidCanonicalRegCount <= 32, "dirty_mask assumes at most 32 canonical SID registers");

struct SidRegisterImage {
    uint8_t reg[kSidCanonicalRegCount]{};
    uint32_t dirty_mask = 0;

    void clear() noexcept {
        std::memset(reg, 0, sizeof(reg));
        dirty_mask = 0;
    }

    void set(int idx, uint8_t value) noexcept {
        if (idx < 0 || idx >= kSidCanonicalRegCount) return;
        if (reg[idx] != value) {
            reg[idx] = value;
            dirty_mask |= (1u << (idx & 31));
        }
    }
};

} // namespace ArpSID
