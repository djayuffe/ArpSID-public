#pragma once

#include <array>
#include <cstdint>

namespace ArpSID::C64 {

enum class VicAccessKind : uint8_t {
    Idle = 0,
    Refresh,
    Graphics,
    Matrix,
    SpritePointer,
    SpriteData
};

struct VicBusCycleInfo {
    bool ba = true;
    bool aec = true;
    bool badline = false;
    bool spriteDma = false;
    bool cpuCanUseBus = true;
    uint16_t fetchAddress = 0xFFFFu;
    VicAccessKind phi1Access = VicAccessKind::Idle;
    VicAccessKind phi2Access = VicAccessKind::Idle;
    uint16_t phi1Address = 0xFFFFu;
    uint16_t phi2Address = 0xFFFFu;
    int8_t sprite = -1;
};

// Cycle table for the MOS 6569/6567 VIC-II bus-facing state used by the C64
// runtime. Cycle numbers exposed by the public API remain zero based, while the
// comments and constants below use the conventional hardware 1..63/65 names.
//
// The table models the bus facts that matter to a 6510/PSID/RSID workload:
//  * badline BA warning on cycles 12..54 and matrix DMA on cycles 15..54;
//  * per-sprite pointer slots and the two PHI2 steals needed for three data
//    fetches, including the line wrap for sprites 3..7;
//  * the three-cycle BA lead time independently from AEC;
//  * sprite DMA start/stop, MC/MCBASE and Y-expansion line advance;
//  * PAL 63-cycle and NTSC 65-cycle end-of-line sprite slots.
class VicII {
public:
    static constexpr uint16_t kPalCyclesPerLine = 63;
    static constexpr uint16_t kPalRasterLines = 312;
    static constexpr uint16_t kNtscCyclesPerLine = 65;
    static constexpr uint16_t kNtscRasterLines = 263;
    static constexpr bool kBusStealIsCycleExact = true;

    void reset(bool pal = true) noexcept {
        pal_ = pal;
        cyclesPerLine_ = pal ? kPalCyclesPerLine : kNtscCyclesPerLine;
        rasterLines_ = pal ? kPalRasterLines : kNtscRasterLines;
        regs_.fill(0);
        spriteDmaActive_.fill(false);
        spriteDisplayActive_.fill(false);
        spriteAdvanceLine_.fill(true);
        spriteMc_.fill(0);
        spriteMcBase_.fill(0);
        rasterLine_ = 0;
        rasterCompare_ = 0;
        cycleInLine_ = 0;
        halfCycle_ = 0;
        frame_ = 0;
        irqEnabled_ = 0;
        irqStatus_ = 0;
        badline_ = false;
        denFrameLatch_ = false;
        spriteDma_ = false;
        ba_ = true;
        aec_ = true;
        totalStolen_ = totalLines_ = 0;
        irqFiredThisLine_ = false;
        vicBank_ = 3;
        videoCounter_ = 0;
        videoCounterBase_ = 0;
        rowCounter_ = 0;
        updateFetchBase_();
        lastFetchAddress_ = 0xFFFFu;
        updateLineState_();
        updateBusState_();
    }

    void write(uint8_t reg, uint8_t value) noexcept {
        reg &= 0x3Fu;
        if (reg >= 0x2Fu) return;
        if (reg == 0x1Eu || reg == 0x1Fu) return;
        regs_[reg] = value;
        if (reg == 0x11) {
            rasterCompare_ = uint16_t((rasterCompare_ & 0x00FFu) | ((value & 0x80u) ? 0x100u : 0u));
            if (rasterLine_ == 0x30u && (value & 0x10u)) denFrameLatch_ = true;
            updateLineState_();
        } else if (reg == 0x12) {
            rasterCompare_ = uint16_t((rasterCompare_ & 0x0100u) | value);
        } else if (reg == 0x19) {
            irqStatus_ &= uint8_t(~(value & 0x0Fu));
            updateIrqMasterBit_();
        } else if (reg == 0x1A) {
            irqEnabled_ = uint8_t(value & 0x0Fu);
            updateIrqMasterBit_();
        }
        updateBusState_();
    }

    uint8_t read(uint8_t reg) noexcept {
        reg &= 0x3Fu;
        if (reg >= 0x2Fu) return 0xFFu;
        if (reg == 0x11) return uint8_t((regs_[0x11] & 0x7Fu) | ((rasterLine_ & 0x100u) ? 0x80u : 0x00u));
        if (reg == 0x12) return uint8_t(rasterLine_ & 0xFFu);
        if (reg == 0x16) return uint8_t(regs_[0x16] | 0xC0u);
        if (reg == 0x18) return uint8_t(regs_[0x18] | 0x01u);
        if (reg == 0x19) return uint8_t(0x70u | (irqStatus_ & 0x0Fu) | (irq() ? 0x80u : 0x00u));
        if (reg == 0x1A) return uint8_t(irqEnabled_ | 0xF0u);
        if (reg == 0x1E || reg == 0x1F) {
            const uint8_t v = regs_[reg];
            regs_[reg] = 0;
            return v;
        }
        if (reg >= 0x20u && reg <= 0x2Eu) return uint8_t(regs_[reg] | 0xF0u);
        return regs_[reg];
    }

    bool tick() noexcept { return step(1u) != 0u; }

    uint32_t stepHalfCycles(uint32_t halfCycles) noexcept {
        uint32_t stolenHalfCycles = 0;
        for (uint32_t i = 0; i < halfCycles; ++i) {
            updateLineState_();
            updateBusState_();
            if (halfCycle_ == 1u && !aec_) ++stolenHalfCycles;
            ++halfCycle_;
            if (halfCycle_ >= 2u) {
                halfCycle_ = 0;
                finishCycle_();
            }
        }
        totalStolen_ += stolenHalfCycles;
        return stolenHalfCycles;
    }

    uint32_t step(uint32_t cycles) noexcept {
        uint32_t stolen = 0;
        for (uint32_t i = 0; i < cycles; ++i) {
            updateLineState_();
            serviceCycleEvents_();
            updateBusState_();
            if (!aec_) ++stolen;
            finishCycle_();
        }
        totalStolen_ += stolen;
        return stolen;
    }

    uint32_t previewStolen(uint32_t cycles) const noexcept {
        VicII copy = *this;
        const uint32_t before = copy.totalStolen_;
        copy.step(cycles);
        return copy.totalStolen_ - before;
    }

    bool irq() const noexcept { return ((irqStatus_ & irqEnabled_ & 0x0Fu) != 0u); }
    bool pal() const noexcept { return pal_; }
    uint16_t cyclesPerLine() const noexcept { return cyclesPerLine_; }
    uint16_t rasterLines() const noexcept { return rasterLines_; }
    uint16_t rasterLine() const noexcept { return rasterLine_; }
    uint16_t cycleInLine() const noexcept { return cycleInLine_; }
    uint8_t halfCycle() const noexcept { return halfCycle_; }
    uint64_t frame() const noexcept { return frame_; }
    bool badline() const noexcept { return badline_; }
    bool ba() const noexcept { return ba_; }
    bool aec() const noexcept { return aec_; }
    bool spriteDma() const noexcept { return spriteDma_; }
    bool cpuCanUseBus() const noexcept { return aec_; }
    bool cpuCanUseBusHalfCycle() const noexcept { return halfCycle_ == 0u || aec_; }
    uint32_t totalStolen() const noexcept { return totalStolen_; }
    uint32_t totalLines() const noexcept { return totalLines_; }
    void setMemoryBank(uint8_t bank) noexcept { vicBank_ = static_cast<uint8_t>(bank & 0x03u); updateFetchBase_(); }
    uint8_t memoryBank() const noexcept { return vicBank_; }
    uint16_t fetchBase() const noexcept { return fetchBase_; }
    uint16_t lastFetchAddress() const noexcept { return lastFetchAddress_; }
    bool spriteDmaActive(uint8_t sprite) const noexcept {
        return sprite < 8u ? spriteDmaActive_[sprite] : false;
    }

    VicBusCycleInfo cycleInfo(uint16_t cycle) const noexcept {
        cycle = static_cast<uint16_t>(cycle % cyclesPerLine_);
        const uint16_t hw = static_cast<uint16_t>(cycle + 1u);
        VicBusCycleInfo out{};
        out.badline = badline_;

        // Normal PHI1 graphics/refresh schedule.
        if (hw >= 16u && hw <= 55u) {
            out.phi1Access = VicAccessKind::Graphics;
            out.phi1Address = graphicsAddress_(static_cast<uint8_t>(hw - 16u));
        } else if (hw >= 11u && hw <= 15u) {
            out.phi1Access = VicAccessKind::Refresh;
            out.phi1Address = static_cast<uint16_t>(fetchBase_ | (0x3F00u | uint16_t(0xFFu - (hw - 11u))));
        }

        // Sprite pointer slots are fixed and occur even when DMA is inactive.
        for (uint8_t s = 0; s < 8u; ++s) {
            if (hw == spriteFirstCycle_(s)) {
                out.phi1Access = VicAccessKind::SpritePointer;
                out.phi1Address = spritePointerAddress_(s);
                out.sprite = static_cast<int8_t>(s);
                break;
            }
        }

        // Badline matrix accesses own PHI2 on cycles 15..54. BA warns from 12.
        if (badline_ && hw >= 12u && hw <= 54u) out.ba = false;
        if (badline_ && hw >= 15u && hw <= 54u) {
            out.aec = false;
            out.phi2Access = VicAccessKind::Matrix;
            out.phi2Address = matrixAddress_(static_cast<uint8_t>(hw - 15u));
        }

        // Sprite data slots. Each active sprite consumes the PHI2 halves of two
        // consecutive cycles. BA is independently asserted three cycles before
        // the first stolen cycle and stays low across merged sprite slots.
        for (uint8_t s = 0; s < 8u; ++s) {
            if (!spriteDmaActive_[s]) continue;
            const uint16_t first = spriteFirstCycle_(s);
            if (cycleDistance_(hw, first) <= 1u) {
                out.aec = false;
                out.spriteDma = true;
                out.sprite = static_cast<int8_t>(s);
                out.phi2Access = VicAccessKind::SpriteData;
                const uint8_t byte = cycleDistance_(hw, first) == 0u ? 0u : 2u;
                out.phi2Address = spriteDataAddress_(s, byte);
            }
            const uint16_t warning = wrapHardwareCycle_(static_cast<int>(first) - 3);
            const uint16_t warningSpan = 5u; // 3 warning + 2 DMA cycles
            if (cycleDistance_(hw, warning) < warningSpan) out.ba = false;
        }

        out.cpuCanUseBus = out.aec;
        if (out.phi2Access != VicAccessKind::Idle) out.fetchAddress = out.phi2Address;
        else out.fetchAddress = out.phi1Address;
        return out;
    }

private:
    uint16_t wrapHardwareCycle_(int cycle) const noexcept {
        while (cycle < 1) cycle += cyclesPerLine_;
        while (cycle > static_cast<int>(cyclesPerLine_)) cycle -= cyclesPerLine_;
        return static_cast<uint16_t>(cycle);
    }

    uint16_t cycleDistance_(uint16_t current, uint16_t start) const noexcept {
        return current >= start ? uint16_t(current - start)
                                : uint16_t(current + cyclesPerLine_ - start);
    }

    uint16_t spriteFirstCycle_(uint8_t sprite) const noexcept {
        static constexpr std::array<uint8_t, 8> kPal = {58, 60, 62, 1, 3, 5, 7, 9};
        static constexpr std::array<uint8_t, 8> kNtsc = {60, 62, 64, 1, 3, 5, 7, 9};
        return pal_ ? kPal[sprite & 7u] : kNtsc[sprite & 7u];
    }

    uint16_t screenBase_() const noexcept {
        return static_cast<uint16_t>(fetchBase_ + ((uint16_t(regs_[0x18] & 0xF0u)) << 6u));
    }

    uint16_t charBase_() const noexcept {
        return static_cast<uint16_t>(fetchBase_ + ((uint16_t(regs_[0x18] & 0x0Eu)) << 10u));
    }

    uint16_t matrixAddress_(uint8_t column) const noexcept {
        return static_cast<uint16_t>(screenBase_() + ((videoCounter_ + column) & 0x03FFu));
    }

    uint16_t graphicsAddress_(uint8_t column) const noexcept {
        const uint16_t cell = static_cast<uint16_t>((videoCounter_ + column) & 0x03FFu);
        return static_cast<uint16_t>(charBase_() + ((cell & 0x00FFu) << 3u) + (rowCounter_ & 7u));
    }

    uint16_t spritePointerAddress_(uint8_t sprite) const noexcept {
        return static_cast<uint16_t>(screenBase_() + 0x03F8u + (sprite & 7u));
    }

    uint16_t spriteDataAddress_(uint8_t sprite, uint8_t byte) const noexcept {
        // The actual pointer byte is memory data and is not available inside the
        // timing-only VIC. Use the MC low six bits as the address suffix so trace
        // consumers still receive the exact byte-within-sprite fetch position.
        return static_cast<uint16_t>(fetchBase_ + ((uint16_t(sprite) & 7u) << 6u)
                                   + ((spriteMc_[sprite] + byte) & 0x3Fu));
    }

    void updateRasterIrq_() noexcept {
        if (!irqFiredThisLine_ && rasterLine_ == rasterCompare_ && cycleInLine_ == 0u) {
            irqStatus_ |= 0x01u;
            updateIrqMasterBit_();
            irqFiredThisLine_ = true;
        }
    }

    void updateIrqMasterBit_() noexcept {
        if (irq()) irqStatus_ |= 0x80u;
        else irqStatus_ &= uint8_t(~0x80u);
    }

    void updateLineState_() noexcept {
        if (rasterLine_ == 0u && cycleInLine_ == 0u) denFrameLatch_ = false;
        if (rasterLine_ == 0x30u && (regs_[0x11] & 0x10u)) denFrameLatch_ = true;
        const uint8_t yscroll = uint8_t(regs_[0x11] & 0x07u);
        badline_ = denFrameLatch_ && rasterLine_ >= 0x30u && rasterLine_ <= 0xF7u
                && ((rasterLine_ & 0x07u) == yscroll);
    }

    void serviceCycleEvents_() noexcept {
        const uint16_t hw = static_cast<uint16_t>(cycleInLine_ + 1u);

        if (hw == 55u || hw == 56u) {
            for (uint8_t s = 0; s < 8u; ++s) {
                const bool enabled = (regs_[0x15] & uint8_t(1u << s)) != 0u;
                if (enabled && !spriteDmaActive_[s] && regs_[uint8_t(s * 2u + 1u)] == uint8_t(rasterLine_)) {
                    spriteDmaActive_[s] = true;
                    spriteMcBase_[s] = 0;
                    spriteAdvanceLine_[s] = true;
                }
            }
        }
        if (hw == 56u) {
            for (uint8_t s = 0; s < 8u; ++s) {
                if (spriteDmaActive_[s] && (regs_[0x17] & uint8_t(1u << s))) {
                    spriteAdvanceLine_[s] = !spriteAdvanceLine_[s];
                }
            }
        }
        if (hw == 58u) {
            for (uint8_t s = 0; s < 8u; ++s) {
                spriteMc_[s] = spriteMcBase_[s];
                if (spriteDmaActive_[s] && regs_[uint8_t(s * 2u + 1u)] == uint8_t(rasterLine_))
                    spriteDisplayActive_[s] = true;
                else if (!spriteDmaActive_[s])
                    spriteDisplayActive_[s] = false;
            }
        }
        if (hw == 16u) {
            for (uint8_t s = 0; s < 8u; ++s) {
                if (spriteAdvanceLine_[s]) spriteMcBase_[s] = spriteMc_[s];
                if (spriteMcBase_[s] >= 63u) spriteDmaActive_[s] = false;
            }
            if (badline_) {
                videoCounter_ = videoCounterBase_;
                rowCounter_ = 0;
            } else if (rowCounter_ == 7u) {
                videoCounterBase_ = videoCounter_;
                rowCounter_ = 0;
            } else {
                ++rowCounter_;
            }
        }

        // Three bytes are fetched per active sprite in its fixed two-cycle slot.
        for (uint8_t s = 0; s < 8u; ++s) {
            if (spriteDmaActive_[s] && hw == wrapHardwareCycle_(spriteFirstCycle_(s) + 1)) {
                spriteMc_[s] = static_cast<uint8_t>(spriteMc_[s] + 3u);
            }
        }
    }

    void updateBusState_() noexcept {
        const VicBusCycleInfo info = cycleInfo(cycleInLine_);
        spriteDma_ = info.spriteDma;
        ba_ = info.ba;
        aec_ = info.aec;
        lastFetchAddress_ = info.fetchAddress;
    }

    void finishCycle_() noexcept {
        ++cycleInLine_;
        if (cycleInLine_ >= cyclesPerLine_) {
            cycleInLine_ = 0;
            ++totalLines_;
            rasterLine_ = uint16_t((rasterLine_ + 1u) % rasterLines_);
            irqFiredThisLine_ = false;
            if (rasterLine_ == 0u) ++frame_;
        }
        updateLineState_();
        updateRasterIrq_();
        updateBusState_();
    }

    void updateFetchBase_() noexcept {
        fetchBase_ = static_cast<uint16_t>((3u - (vicBank_ & 0x03u)) * 0x4000u);
    }

    std::array<uint8_t, 64> regs_{};
    std::array<bool, 8> spriteDmaActive_{};
    std::array<bool, 8> spriteDisplayActive_{};
    std::array<bool, 8> spriteAdvanceLine_{};
    std::array<uint8_t, 8> spriteMc_{};
    std::array<uint8_t, 8> spriteMcBase_{};
    bool pal_ = true;
    uint16_t cyclesPerLine_ = kPalCyclesPerLine;
    uint16_t rasterLines_ = kPalRasterLines;
    uint16_t rasterLine_ = 0;
    uint16_t rasterCompare_ = 0;
    uint16_t cycleInLine_ = 0;
    uint8_t halfCycle_ = 0;
    uint64_t frame_ = 0;
    uint8_t irqEnabled_ = 0;
    uint8_t irqStatus_ = 0;
    bool badline_ = false;
    bool denFrameLatch_ = false;
    bool spriteDma_ = false;
    bool ba_ = true;
    bool aec_ = true;
    uint32_t totalStolen_ = 0;
    uint32_t totalLines_ = 0;
    bool irqFiredThisLine_ = false;
    uint8_t vicBank_ = 3;
    uint16_t fetchBase_ = 0x0000u;
    uint16_t lastFetchAddress_ = 0xFFFFu;
    uint16_t videoCounter_ = 0;
    uint16_t videoCounterBase_ = 0;
    uint8_t rowCounter_ = 0;
};

} // namespace ArpSID::C64
