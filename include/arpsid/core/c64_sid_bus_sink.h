#pragma once

#include "arpsid/core/c64_phi2_types.h"
#include "arpsid/core/c64_sid_readback.h"

namespace ArpSID::C64 {

class SidBusSink final : public ISidRegisterWriteSink {
public:
    static constexpr size_t kMaxCapturedWrites = 8192;
    SidBusSink() noexcept { clear(); }

    void clear() noexcept {
        last_.fill(0);
        writes_.fill(SidPhi2Write{});
        writeCount_ = 0;
        droppedWrites_ = 0;
        rmwDummyWrites_ = 0;
        potX_ = 0xFFu;
        potY_ = 0xFFu;
        osc3_ = 0;
        env3_ = 0;
        readback_.reset();
        manualReadable_ = false;
    }

    void writeSidRegisterPhi2(uint64_t phi2,
                              uint8_t reg,
                              uint8_t value,
                              bool rmwDummy) noexcept override {
        reg = static_cast<uint8_t>(reg & 0x1Fu);
        last_[reg] = value;
        if (reg <= 0x18u) readback_.write(phi2, reg, value);
        if (rmwDummy) ++rmwDummyWrites_;
        if (writeCount_ < writes_.size()) {
            writes_[writeCount_++] = SidPhi2Write{phi2, reg, value, rmwDummy, true};
        } else {
            ++droppedWrites_;
        }
    }

    uint8_t readSidRegisterPhi2(uint64_t phi2, uint8_t reg, uint8_t openBus) noexcept override {
        if (manualReadable_) {
            switch (static_cast<uint8_t>(reg & 0x1Fu)) {
                case 0x19u: return potX_;
                case 0x1Au: return potY_;
                case 0x1Bu: return osc3_;
                case 0x1Cu: return env3_;
                default: return openBus;
            }
        }
        return readback_.read(phi2, reg, openBus);
    }

    void setReadable(uint8_t potX, uint8_t potY, uint8_t osc3, uint8_t env3) noexcept {
        potX_ = potX;
        potY_ = potY;
        osc3_ = osc3;
        env3_ = env3;
        readback_.setPotTargets(potX, potY);
        manualReadable_ = true;
    }

    uint8_t last(uint8_t reg) const noexcept { return last_[static_cast<uint8_t>(reg & 0x1Fu)]; }
    size_t writeCount() const noexcept { return writeCount_; }
    uint64_t droppedWrites() const noexcept { return droppedWrites_; }
    uint64_t rmwDummyWrites() const noexcept { return rmwDummyWrites_; }
    const SidPhi2Write& write(size_t index) const noexcept { return writes_[index]; }

private:
    std::array<uint8_t, 32> last_{};
    std::array<SidPhi2Write, kMaxCapturedWrites> writes_{};
    size_t writeCount_ = 0;
    uint64_t droppedWrites_ = 0;
    uint64_t rmwDummyWrites_ = 0;
    uint8_t potX_ = 0xFFu;
    uint8_t potY_ = 0xFFu;
    uint8_t osc3_ = 0;
    uint8_t env3_ = 0;
    SidReadbackModel readback_{};
    bool manualReadable_ = false;
};

} // namespace ArpSID::C64
