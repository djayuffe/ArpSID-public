// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/core/c64_phi2_types.h"

namespace ArpSID::C64 {

struct Phi2TraceRecord {
    uint64_t cycle = 0;
    uint16_t pc = 0;
    uint8_t opcode = 0;
    uint8_t t = 0;
    uint16_t addr = 0xFFFFu;
    uint8_t data = 0xFFu;
    bool rw = true;
    bool sidWrite = false;
    uint8_t sidReg = 0;
    uint8_t a = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t sp = 0xFDu;
    uint8_t p = 0x34u;
    bool ba = true;
    bool aec = true;
    bool irq = false;
    bool nmi = false;
};

template <size_t Capacity>
class FixedPhi2Trace final : public IPhi2TraceSink {
public:
    void clear() noexcept { count_ = 0; dropped_ = 0; }
    void onPhi2(const Phi2BusPhase& phase) noexcept override {
        if (count_ < records_.size()) {
            Phi2TraceRecord& r = records_[count_++];
            r.cycle = phase.cycle;
            r.addr = phase.address;
            r.data = phase.data;
            r.rw = phase.rw;
            r.sidWrite = phase.sidWrite;
            r.sidReg = phase.sidReg;
            r.ba = phase.ba;
            r.aec = phase.aec;
            r.irq = phase.irqBeforeSample;
            r.nmi = phase.nmiBeforeSample;
        } else {
            ++dropped_;
        }
    }
    size_t size() const noexcept { return count_; }
    uint64_t dropped() const noexcept { return dropped_; }
    const Phi2TraceRecord& operator[](size_t i) const noexcept { return records_[i]; }

private:
    std::array<Phi2TraceRecord, Capacity> records_{};
    size_t count_ = 0;
    uint64_t dropped_ = 0;
};

} // namespace ArpSID::C64

