#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

// CANONICAL TRUTH: Token allocation. Token 0 is always invalid.
// nextToken() returns a monotonically increasing token >= 1.

namespace ArpSID {

class SidVoiceTokenPool {
public:
    static constexpr uint64_t kHardwareTokenNamespace = (1ull << 63);
    SidVoiceTokenPool() noexcept = default;

    uint64_t nextToken() noexcept {
        uint64_t cur = next_.fetch_add(1u, std::memory_order_relaxed);
        if (cur == std::numeric_limits<uint64_t>::max()) {
            next_.store(0u, std::memory_order_relaxed);
            return kHardwareTokenNamespace | 1u;
        }
        const uint64_t tok = (cur + 1u) | kHardwareTokenNamespace;
        return tok == 0u ? (kHardwareTokenNamespace | 1u) : tok;
    }

    void reset() noexcept {
        next_.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> next_{0};
};

} // namespace ArpSID
