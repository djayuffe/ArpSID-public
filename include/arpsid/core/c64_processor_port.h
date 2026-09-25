#pragma once

#include <cstdint>

namespace ArpSID::C64 {

struct ProcessorPort6510 {
    uint8_t ddr = 0x2Fu;
    uint8_t data = 0x37u;

    void powerOn() noexcept {
        ddr = 0x2Fu;
        data = 0x37u;
    }

    uint8_t read(uint16_t addr, uint8_t openBus) const noexcept {
        if ((addr & 0xFFFFu) == 0x0000u) return ddr;
        if ((addr & 0xFFFFu) == 0x0001u) {
            (void)openBus;
            const uint8_t output = static_cast<uint8_t>(data & ddr);
            const uint8_t floatingLow = static_cast<uint8_t>(0x3Fu & static_cast<uint8_t>(~ddr));
            return static_cast<uint8_t>(0xC0u | output | floatingLow);
        }
        return openBus;
    }

    void write(uint16_t addr, uint8_t value) noexcept {
        if ((addr & 0xFFFFu) == 0x0000u) ddr = value;
        else if ((addr & 0xFFFFu) == 0x0001u) data = value;
    }

    uint8_t effectivePort() const noexcept {
        return static_cast<uint8_t>(0xC0u | (data & ddr) | (0x3Fu & static_cast<uint8_t>(~ddr)));
    }

    bool loram() const noexcept { return (effectivePort() & 0x01u) != 0; }
    bool hiram() const noexcept { return (effectivePort() & 0x02u) != 0; }
    bool charen() const noexcept { return (effectivePort() & 0x04u) != 0; }
};

} // namespace ArpSID::C64

