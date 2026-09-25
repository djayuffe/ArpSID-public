#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ArpSID::C64 {

// Deterministic IEC serial bus surface for RSID/full-C64 projection.
// C64 IEC lines are wired-AND/open-collector: any device pulling low makes the
// observed bus low. This model exposes the line law without doing unbounded IEC
// protocol work on the audio thread.
class C64IecBus {
public:
    enum Device : uint8_t { Computer = 0, Drive8 = 1, Drive9 = 2, DeviceCount = 3 };

    void reset() noexcept {
        atnLow_.fill(false); clkLow_.fill(false); dataLow_.fill(false); srqLow_.fill(false);
    }

    void setLine(Device dev, bool atnLow, bool clkLow, bool dataLow, bool srqLow = false) noexcept {
        const auto i = static_cast<size_t>(dev);
        if (i >= atnLow_.size()) return;
        atnLow_[i] = atnLow; clkLow_[i] = clkLow; dataLow_[i] = dataLow; srqLow_[i] = srqLow;
    }

    bool atn() const noexcept { return !anyLow_(atnLow_); }
    bool clk() const noexcept { return !anyLow_(clkLow_); }
    bool data() const noexcept { return !anyLow_(dataLow_); }
    bool srq() const noexcept { return !anyLow_(srqLow_); }

    // CIA2 PA read contribution. Bits are high when the observed IEC line is high.
    // Common C64 mapping: PA3 ATN out, PA4 CLK, PA5 DATA, PA6 CLK in, PA7 DATA in.
    uint8_t cia2PortAInputMask() const noexcept {
        uint8_t v = 0xFFu;
        if (!clk())  v &= uint8_t(~0x40u);
        if (!data()) v &= uint8_t(~0x80u);
        return v;
    }

private:
    static bool anyLow_(const std::array<bool, DeviceCount>& a) noexcept {
        for (bool v : a) if (v) return true;
        return false;
    }
    std::array<bool, DeviceCount> atnLow_{};
    std::array<bool, DeviceCount> clkLow_{};
    std::array<bool, DeviceCount> dataLow_{};
    std::array<bool, DeviceCount> srqLow_{};
};

// Minimal datasette/tape signal model: motor/sense/write are CPU/CIA visible,
// read pulses can be injected by corpus fixtures and sampled deterministically.
class C64TapePort {
public:
    void reset() noexcept { motor_ = false; sense_ = true; write_ = false; read_ = true; pulseCount_ = 0; }
    void setMotor(bool on) noexcept { motor_ = on; }
    void setSense(bool inserted) noexcept { sense_ = inserted; }
    void setWrite(bool high) noexcept { write_ = high; }
    void injectReadPulse(bool high) noexcept { if (read_ != high) ++pulseCount_; read_ = high; }
    bool motor() const noexcept { return motor_; }
    bool sense() const noexcept { return sense_; }
    bool write() const noexcept { return write_; }
    bool read() const noexcept { return read_; }
    uint64_t pulseCount() const noexcept { return pulseCount_; }
private:
    bool motor_ = false;
    bool sense_ = true;
    bool write_ = false;
    bool read_ = true;
    uint64_t pulseCount_ = 0;
};

struct RselCorpusCase {
    const char* name = "";
    uint16_t resetVector = 0;
    uint32_t maxInstructions = 0;
};

struct RselCorpusResult {
    uint32_t casesRun = 0;
    uint32_t casesPassed = 0;
    uint32_t casesFailed = 0;
    bool complete = false;
};

} // namespace ArpSID::C64
