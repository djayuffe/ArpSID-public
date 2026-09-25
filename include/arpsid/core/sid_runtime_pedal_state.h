#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace ArpSID {

struct SidRuntimePedalState {
    std::array<uint8_t, 16> sustainByChannel{};
    std::array<uint8_t, 16> sostenutoByChannel{};

    void clear() noexcept {
        sustainByChannel.fill(0u);
        sostenutoByChannel.fill(0u);
    }

    void setSustain(int channel, bool down) noexcept {
        if (channel < 0 || channel >= 16) return;
        sustainByChannel[(size_t)channel] = down ? 1u : 0u;
    }

    void setSostenuto(int channel, bool down) noexcept {
        if (channel < 0 || channel >= 16) return;
        sostenutoByChannel[(size_t)channel] = down ? 1u : 0u;
    }

    bool sustainDown(int channel) const noexcept {
        return channel >= 0 && channel < 16 && sustainByChannel[(size_t)channel] != 0u;
    }

    bool sostenutoDown(int channel) const noexcept {
        return channel >= 0 && channel < 16 && sostenutoByChannel[(size_t)channel] != 0u;
    }
};

} // namespace ArpSID
