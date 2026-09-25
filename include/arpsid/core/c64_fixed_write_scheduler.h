#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ArpSID::C64 {

// Allocation-free, deterministic scheduler for C64 render writes.
//
// Input writes are produced in ordinal order. Four stable 8-bit bucket passes
// order them by (sample, cycle) while preserving ordinal order for exact ties.
// Runtime is fixed at O(4N + 1024), with no recursion, comparison sort, locks,
// allocation, or data-dependent worst-case branch tree on the audio thread.
template <typename Write, std::size_t Capacity>
inline void stableScheduleC64RenderWrites(
    std::array<Write, Capacity>& primary,
    std::array<Write, Capacity>& scratch,
    std::uint32_t count) noexcept {
    if (count < 2u) return;
    if (count > Capacity) count = static_cast<std::uint32_t>(Capacity);

    bool alreadySorted = true;
    for (std::uint32_t i = 1u; i < count; ++i) {
        const Write& prev = primary[i - 1u];
        const Write& cur = primary[i];
        if (prev.sample > cur.sample ||
            (prev.sample == cur.sample && prev.cycle > cur.cycle)) {
            alreadySorted = false;
            break;
        }
    }
    if (alreadySorted) return;

    const auto pass = [count](const std::array<Write, Capacity>& src,
                              std::array<Write, Capacity>& dst,
                              bool sampleKey,
                              unsigned shift) noexcept {
        std::array<std::uint32_t, 256> offsets{};
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t key = sampleKey
                ? static_cast<std::uint32_t>(src[i].sample)
                : static_cast<std::uint32_t>(src[i].cycle);
            ++offsets[(key >> shift) & 0xFFu];
        }
        std::uint32_t sum = 0u;
        for (std::uint32_t& bucket : offsets) {
            const std::uint32_t n = bucket;
            bucket = sum;
            sum += n;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::uint32_t key = sampleKey
                ? static_cast<std::uint32_t>(src[i].sample)
                : static_cast<std::uint32_t>(src[i].cycle);
            dst[offsets[(key >> shift) & 0xFFu]++] = src[i];
        }
    };

    pass(primary, scratch, false, 0u);
    pass(scratch, primary, false, 8u);
    pass(primary, scratch, true, 0u);
    pass(scratch, primary, true, 8u);
}

} // namespace ArpSID::C64
