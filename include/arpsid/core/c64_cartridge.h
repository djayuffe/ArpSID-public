#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ArpSID::C64 {

enum class C64CartridgeMode : uint8_t {
    None = 0,
    Ultimax = 1,
    EightK = 2,
    SixteenK = 3,
};

class C64CartridgeImage {
public:
    void reset() noexcept {
        mode_ = C64CartridgeMode::None;
        exrom_ = true;
        game_ = true;
        romL_.fill(0xFFu);
        romH_.fill(0xFFu);
    }

    void setMode(C64CartridgeMode mode) noexcept {
        mode_ = mode;
        switch (mode) {
            case C64CartridgeMode::None: exrom_ = true; game_ = true; break;
            case C64CartridgeMode::EightK: exrom_ = false; game_ = true; break;
            case C64CartridgeMode::SixteenK: exrom_ = false; game_ = false; break;
            case C64CartridgeMode::Ultimax: exrom_ = true; game_ = false; break;
        }
    }

    C64CartridgeMode mode() const noexcept { return mode_; }
    bool exromLineHigh() const noexcept { return exrom_; }
    bool gameLineHigh() const noexcept { return game_; }
    bool active() const noexcept { return mode_ != C64CartridgeMode::None; }

    void setBank(uint8_t bank) noexcept { bank_ = bank; }
    uint8_t bank() const noexcept { return bank_; }
    void setLines(bool exromHigh, bool gameHigh) noexcept { exrom_ = exromHigh; game_ = gameHigh; }
    void pokeRomL(uint16_t offset, uint8_t value) noexcept { romL_[(size_t(bank_) * 0x2000u + (offset & 0x1FFFu)) & (romL_.size() - 1u)] = value; }
    void pokeRomH(uint16_t offset, uint8_t value) noexcept { romH_[(size_t(bank_) * 0x2000u + (offset & 0x1FFFu)) & (romH_.size() - 1u)] = value; }
    bool loadRomLBank(uint8_t bank, const uint8_t* data, size_t size) noexcept { return loadBank_(romL_, bank, data, size); }
    bool loadRomHBank(uint8_t bank, const uint8_t* data, size_t size) noexcept { return loadBank_(romH_, bank, data, size); }
    uint8_t readRomL(uint16_t address) const noexcept { return romL_[(size_t(bank_) * 0x2000u + (address & 0x1FFFu)) & (romL_.size() - 1u)]; }
    uint8_t readRomH(uint16_t address) const noexcept { return romH_[(size_t(bank_) * 0x2000u + (address & 0x1FFFu)) & (romH_.size() - 1u)]; }

private:
    C64CartridgeMode mode_ = C64CartridgeMode::None;
    bool exrom_ = true;
    bool game_ = true;
    uint8_t bank_ = 0;
    std::array<uint8_t, 0x2000 * 16> romL_{};
    std::array<uint8_t, 0x2000 * 16> romH_{};

    static bool loadBank_(std::array<uint8_t, 0x2000 * 16>& dst, uint8_t bank, const uint8_t* data, size_t size) noexcept {
        if (!data || size != 0x2000u || bank >= 16u) return false;
        const size_t base = size_t(bank) * 0x2000u;
        for (size_t i = 0; i < 0x2000u; ++i) dst[base + i] = data[i];
        return true;
    }
};

} // namespace ArpSID::C64
