// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <array>
#include <algorithm>
#include <cstdint>

namespace ArpSID::C64 {

struct CiaTimerPhaseSnapshot {
    uint64_t timerAUnderflows = 0;
    uint64_t timerBUnderflows = 0;
    uint64_t irqEdges = 0;
    uint64_t cntRisingEdges = 0;
    uint16_t timerA = 0xFFFFu;
    uint16_t timerB = 0xFFFFu;
    uint16_t latchA = 0xFFFFu;
    uint16_t latchB = 0xFFFFu;
    bool timerAReloadPending = false;
    bool timerBReloadPending = false;
    bool timerAJustUnderflowed = false;
    bool timerBJustUnderflowed = false;
    bool irqLevel = false;
    bool flagLatched = false;
    bool todLatched = false;
    bool todStopped = false;
    uint8_t serialBitsRemaining = 0;
    bool serialSelfClock = false;
    bool todAlarmWriteMode = false;
    uint8_t todTenths = 0;
    uint8_t todSeconds = 0;
    uint8_t todMinutes = 0;
    uint8_t todHours = 0;
    uint8_t todAlarmTenths = 0;
    uint8_t todAlarmSeconds = 0;
    uint8_t todAlarmMinutes = 0;
    uint8_t todAlarmHours = 0;
};

// MOS 6526 CIA model for the C64 projection runtime.
// Ported/merged from the supplied system.py CIA implementation, but kept
// header-only/noexcept/RT-safe for ArpSID render-side C64 projection use.
// Scope: timer A/B phi2/CNT/chained underflow semantics, one-shot/continuous,
// ICR mask/read-to-clear, TOD latch/alarm, timer read latch, serial shift and
// PB6/PB7 underflow outputs. This intentionally avoids allocation/logging.
class Cia6526 {
public:
    // Timer/CNT/underflow/IRQ events are resolved one PHI2 at a time. Batched
    // step() calls use the same single-cycle state machine, so Timer-B chaining,
    // CNT gating and same-cycle IRQ visibility cannot change with batch size.
    static constexpr bool kTimerIrqModelIsCycleExact = true;
    static constexpr uint32_t kTodPal50HzPhi2Period = 19705u;
    static constexpr uint32_t kTodNtsc60HzPhi2Period = 17045u;

    enum : uint8_t {
        IcrTimerA = 0x01u,
        IcrTimerB = 0x02u,
        IcrTod    = 0x04u,
        IcrSdr    = 0x08u,
        IcrFlag   = 0x10u
    };

    void reset() noexcept {
        pra_ = prb_ = ddra_ = ddrb_ = 0;
        portAInput_ = portBInput_ = 0xFFu;
        regs_.fill(0);
        timerA_ = timerB_ = 0xFFFFu;
        latchA_ = latchB_ = 0xFFFFu;
        irqFlags_ = irqMask_ = 0;
        irqLevel_ = false;
        taHiLatch_ = tbHiLatch_ = 0;
        taLatched_ = tbLatched_ = false;
        todTenths_ = todSeconds_ = todMinutes_ = todHours_ = 0;
        todAlarmTenths_ = todAlarmSeconds_ = todAlarmMinutes_ = todAlarmHours_ = 0;
        todLatched_ = false;
        todLatchTenths_ = todLatchSeconds_ = todLatchMinutes_ = todLatchHours_ = 0;
        todStopped_ = false;
        todWriteInProgress_ = false;
        todCycleAccum_ = todPulseAccum_ = 0;
        serialShift_ = serialBits_ = serialCntPhase_ = serialInCount_ = 0;
        serialSelfClock_ = false;
        spOut_ = spIn_ = 1;
        cntIn_ = 0;
        pb6Out_ = pb7Out_ = 0;
        pb6PulseRemaining_ = pb7PulseRemaining_ = 0;
        flagLatched_ = false;
        todFastDefaultDivider_ = true;
        timerAUnderflowCount_ = timerBUnderflowCount_ = irqEdgeCount_ = 0;
        cntRisingEdgeCount_ = pendingCntPulses_ = 0;
        timerAReloadPending_ = timerBReloadPending_ = false;
        timerAJustUnderflowed_ = timerBJustUnderflowed_ = false;
    }

    void setClockHz(uint32_t hz) noexcept { clockHz_ = hz ? hz : 985248u; todFastDefaultDivider_ = false; }
    void setTod50Hz(bool enable) noexcept {
        if (enable) regs_[0x0E] |= 0x80u;
        else regs_[0x0E] &= uint8_t(~0x80u);
    }

    void setPortAInput(uint8_t value) noexcept { portAInput_ = value; }
    void setPortBInput(uint8_t value) noexcept { portBInput_ = value; }
    // Board-level view of CIA port A pins: output latch bits where DDRA=1,
    // external/pulled input bits where DDRA=0. This is intentionally side-effect
    // free and is used for CIA2 PA0/PA1 VIC-bank wiring instead of a generic
    // CPU read path.
    uint8_t portAPins() const noexcept { return uint8_t((pra_ & ddra_) | (portAInput_ & uint8_t(~ddra_))); }
    uint8_t portBPins() const noexcept { return uint8_t((prb_ & ddrb_) | (portBInput_ & uint8_t(~ddrb_))); }
    void setSpInput(uint8_t level) noexcept { spIn_ = (level & 1u) ? 1u : 0u; }
    void pulseFlag() noexcept {
        flagLatched_ = true;
        setIcrEvent_(IcrFlag);
    }

    void setCntInput(uint8_t level) noexcept {
        const uint8_t next = (level & 1u) ? 1u : 0u;
        if (next && !cntIn_) { ++pendingCntPulses_; ++cntRisingEdgeCount_; }
        cntIn_ = next;
    }
    uint8_t spOut() const noexcept { return spOut_ ? 1u : 0u; }
    uint8_t pb6() const noexcept { return pb6Out_ ? 1u : 0u; }
    uint8_t pb7() const noexcept { return pb7Out_ ? 1u : 0u; }

    void write(uint8_t reg, uint8_t value) noexcept {
        reg &= 0x0Fu;
        switch (reg) {
            case 0x00: pra_ = value; regs_[reg] = value; return;
            case 0x01: prb_ = value; regs_[reg] = value; return;
            case 0x02: ddra_ = value; regs_[reg] = value; return;
            case 0x03: ddrb_ = value; regs_[reg] = value; return;
            case 0x04: ++timerALatchWriteCount_; latchA_ = uint16_t((latchA_ & 0xFF00u) | static_cast<unsigned>(value)); regs_[reg] = value; return;
            case 0x05:
                ++timerALatchWriteCount_;
                latchA_ = uint16_t((static_cast<unsigned>(value) << 8u) | (latchA_ & 0x00FFu));
                regs_[reg] = value;
                if (!timerRunningA_()) timerA_ = latchA_;
                return;
            case 0x06: ++timerBLatchWriteCount_; latchB_ = uint16_t((latchB_ & 0xFF00u) | static_cast<unsigned>(value)); regs_[reg] = value; return;
            case 0x07:
                ++timerBLatchWriteCount_;
                latchB_ = uint16_t((static_cast<unsigned>(value) << 8u) | (latchB_ & 0x00FFu));
                regs_[reg] = value;
                if (!timerRunningB_()) timerB_ = latchB_;
                return;
            case 0x08: writeTod_(0, value); return;
            case 0x09: writeTod_(1, value); return;
            case 0x0A: writeTod_(2, value); return;
            case 0x0B: writeTod_(3, value); return;
            case 0x0C:
                serialShift_ = value;
                regs_[reg] = value;
                serialBits_ = serialOutputMode_() ? 8u : 0u;
                serialCntPhase_ = 0u;
                serialInCount_ = 0u;
                // Input mode is clocked only by external CNT rising edges.
                // There is no PHI2 "self clock" for serial input on a 6526.
                serialSelfClock_ = false;
                return;
            case 0x0D:
                if (value & 0x80u) irqMask_ |= uint8_t(value & 0x1Fu);
                else irqMask_ &= uint8_t(~(value & 0x1Fu));
                updateIrq_();
                return;
            case 0x0E:
                {
                ++timerAControlWriteCount_;
                const bool wasSerialOutput = serialOutputMode_();
                regs_[reg] = value;
                if (serialOutputMode_()) {
                    serialSelfClock_ = false;
                    if (!wasSerialOutput && serialBits_ == 0u) {
                        serialShift_ = regs_[0x0C];
                        serialBits_ = 8u;
                        serialCntPhase_ = 0u;
                    }
                } else {
                    serialBits_ = 0u;
                }
                if (value & 0x10u) forceLoadA_();
                regs_[reg] &= uint8_t(~0x10u);
                return;
                }
            case 0x0F:
                ++timerBControlWriteCount_;
                regs_[reg] = value;
                if (value & 0x10u) forceLoadB_();
                regs_[reg] &= uint8_t(~0x10u);
                return;
            default: return;
        }
    }

    uint8_t read(uint8_t reg) noexcept {
        reg &= 0x0Fu;
        switch (reg) {
            case 0x00: return uint8_t((pra_ & ddra_) | (portAInput_ & uint8_t(~ddra_)));
            case 0x01: {
                uint8_t v = uint8_t((prb_ & ddrb_) | (portBInput_ & uint8_t(~ddrb_)));
                if (regs_[0x0E] & 0x02u) v = uint8_t((v & ~0x40u) | (pb6Out_ ? 0x40u : 0x00u));
                if (regs_[0x0F] & 0x02u) v = uint8_t((v & ~0x80u) | (pb7Out_ ? 0x80u : 0x00u));
                return v;
            }
            case 0x02: return ddra_;
            case 0x03: return ddrb_;
            case 0x04: taHiLatch_ = uint8_t(timerA_ >> 8u); taLatched_ = true; return uint8_t(timerA_ & 0xFFu);
            case 0x05: if (taLatched_) { taLatched_ = false; return taHiLatch_; } return uint8_t(timerA_ >> 8u);
            case 0x06: tbHiLatch_ = uint8_t(timerB_ >> 8u); tbLatched_ = true; return uint8_t(timerB_ & 0xFFu);
            case 0x07: if (tbLatched_) { tbLatched_ = false; return tbHiLatch_; } return uint8_t(timerB_ >> 8u);
            case 0x08: case 0x09: case 0x0A: case 0x0B: return readTod_(reg);
            case 0x0C: return serialShift_;
            case 0x0D: {
                const uint8_t v = uint8_t((irqLevel_ ? 0x80u : 0x00u) | (irqFlags_ & 0x1Fu));
                irqFlags_ = 0;
                irqLevel_ = false;
                return v;
            }
            case 0x0E: return regs_[0x0E];
            case 0x0F: return regs_[0x0F];
            default: return 0;
        }
    }

    bool tick() noexcept { return step(1u, 0u, false); }

    bool step(uint32_t cycles, uint32_t cntPulses = 0, bool flagEdge = false) noexcept {
        const uint8_t flagsBefore = irqFlags_;
        timerAJustUnderflowed_ = false;
        timerBJustUnderflowed_ = false;
        cntPulses += pendingCntPulses_;
        pendingCntPulses_ = 0;
        if (flagEdge) { flagLatched_ = true; setIcrEvent_(IcrFlag); }
        if (cycles == 0u && cntPulses == 0u) return (irqFlags_ & uint8_t(~flagsBefore) & 0x1Fu) != 0u;

        // Resolve every edge in cycle order. Explicit cntPulses are consumed one
        // per PHI2 first; remaining pulses are external edges with no PHI2
        // advance, preserving the legacy step(0, pulses) API.
        const uint32_t inCycleCnt = std::min(cycles, cntPulses);
        for (uint32_t i = 0; i < cycles; ++i) tickOneCycle_(i < inCycleCnt);
        for (uint32_t i = inCycleCnt; i < cntPulses; ++i) tickCntEdge_();
        updateIrq_();
        return (irqFlags_ & uint8_t(~flagsBefore) & 0x1Fu) != 0u;
    }

    bool irq() const noexcept { return irqLevel_; }
    uint16_t timerA() const noexcept { return timerA_; }
    uint16_t timerB() const noexcept { return timerB_; }
    uint16_t latchA() const noexcept { return latchA_; }
    uint16_t latchB() const noexcept { return latchB_; }
    // v872 P0-1/P0-2: monotonic write counters (never reset) so callers can detect
    // whether a tune actually wrote Timer-A latch ($04/$05) or the Timer-A control
    // register ($0E) during init, instead of inferring it from the final latch value
    // (which cannot distinguish an intentional $FFFF latch from the reset sentinel).
    uint64_t timerALatchWriteCount() const noexcept { return timerALatchWriteCount_; }
    uint64_t timerAControlWriteCount() const noexcept { return timerAControlWriteCount_; }
    // Same monotonic write-intent counters for Timer B ($06/$07 latch, $0F control)
    // so tunes that drive chained timing / IRQ logic / tempo through Timer B can be
    // audited at the same "tune intent" fidelity as Timer A (v873 P1-8).
    uint64_t timerBLatchWriteCount() const noexcept { return timerBLatchWriteCount_; }
    uint64_t timerBControlWriteCount() const noexcept { return timerBControlWriteCount_; }
    // Side-effect-free read of the current Timer-A control register image.
    uint8_t peekControlA() const noexcept { return regs_[0x0E]; }
    uint8_t irqFlags() const noexcept { return irqFlags_; }
    uint8_t irqMask() const noexcept { return irqMask_; }
    CiaTimerPhaseSnapshot timerPhaseSnapshot() const noexcept {
        return CiaTimerPhaseSnapshot{timerAUnderflowCount_, timerBUnderflowCount_, irqEdgeCount_,
                                     cntRisingEdgeCount_, timerA_, timerB_, latchA_, latchB_,
                                     timerAReloadPending_, timerBReloadPending_,
                                     timerAJustUnderflowed_, timerBJustUnderflowed_,
                                     irqLevel_, flagLatched_, todLatched_, todStopped_,
                                     serialBitsRemaining(), serialSelfClock_, (regs_[0x0F] & 0x80u) != 0,
                                     todTenths_, todSeconds_, todMinutes_, todHours_,
                                     todAlarmTenths_, todAlarmSeconds_, todAlarmMinutes_, todAlarmHours_};
    }
    uint8_t todTenths() const noexcept { return todTenths_; }
    uint8_t todSeconds() const noexcept { return todSeconds_; }
    uint8_t todMinutes() const noexcept { return todMinutes_; }
    uint8_t todHours() const noexcept { return todHours_; }
    uint8_t serialShift() const noexcept { return serialShift_; }
    uint8_t serialBitsRemaining() const noexcept {
        if (serialOutputMode_()) return serialBits_;
        return serialInCount_ == 0u ? 0u : static_cast<uint8_t>(8u - serialInCount_);
    }
    bool flagLatched() const noexcept { return flagLatched_; }
    void clearFlagLatch() noexcept { flagLatched_ = false; }

private:
    enum class TimerSource : uint8_t { Phi2, Cnt, TimerAUnderflow, TimerAUnderflowCnt };

    bool timerRunningA_() const noexcept { return (regs_[0x0E] & 0x01u) != 0; }
    bool timerRunningB_() const noexcept { return (regs_[0x0F] & 0x01u) != 0; }
    bool timerOneShotA_() const noexcept { return (regs_[0x0E] & 0x08u) != 0; }
    bool timerOneShotB_() const noexcept { return (regs_[0x0F] & 0x08u) != 0; }
    bool serialOutputMode_() const noexcept { return (regs_[0x0E] & 0x40u) != 0; }
    TimerSource timerSourceA_() const noexcept { return (regs_[0x0E] & 0x20u) ? TimerSource::Cnt : TimerSource::Phi2; }
    TimerSource timerSourceB_() const noexcept {
        const uint8_t mode = uint8_t((regs_[0x0F] >> 5u) & 0x03u);
        if (mode == 0u) return TimerSource::Phi2;
        if (mode == 1u) return TimerSource::Cnt;
        if (mode == 2u) return TimerSource::TimerAUnderflow;
        return TimerSource::TimerAUnderflowCnt;
    }

    void updateIrq_() noexcept { const bool before = irqLevel_; irqLevel_ = ((irqFlags_ & irqMask_ & 0x1Fu) != 0); if (irqLevel_ && !before) ++irqEdgeCount_; }
    void setIcrEvent_(uint8_t bit) noexcept { irqFlags_ |= uint8_t(bit & 0x1Fu); updateIrq_(); }

    static uint8_t bcdInc0_9_(uint8_t bcd, bool& carry) noexcept {
        uint8_t lo = uint8_t((bcd & 0x0Fu) + 1u);
        if (lo >= 10u) { carry = true; return 0; }
        carry = false; return uint8_t(lo & 0x0Fu);
    }
    static uint8_t bcdInc0_59_(uint8_t bcd, bool& carry) noexcept {
        uint8_t lo = uint8_t((bcd & 0x0Fu) + 1u);
        uint8_t hi = uint8_t((bcd >> 4u) & 0x0Fu);
        if (lo >= 10u) { lo = 0; ++hi; }
        if ((uint8_t(hi * 10u + lo)) >= 60u) { carry = true; return 0; }
        carry = false; return uint8_t((hi << 4u) | lo);
    }
    static uint8_t bcdHourInc_(uint8_t hr) noexcept {
        uint8_t pm = uint8_t(hr & 0x80u);
        uint8_t h = uint8_t(hr & 0x7Fu);
        uint8_t n = uint8_t(((h >> 4u) & 0x0Fu) * 10u + (h & 0x0Fu));
        if (n < 1u || n > 12u) n = 1u;
        ++n;
        if (n == 12u) pm ^= 0x80u;
        if (n > 12u) n = 1u;
        return uint8_t(pm | ((n / 10u) << 4u) | (n % 10u));
    }

    bool todIs50Hz_() const noexcept { return (regs_[0x0E] & 0x80u) != 0; }
    uint8_t todPulsesPerTenth_() const noexcept { return todFastDefaultDivider_ ? 5u : (todIs50Hz_() ? 5u : 6u); }
    void tickTodOneTenth_() noexcept {
        bool carry = false;
        todTenths_ = bcdInc0_9_(todTenths_, carry);
        if (!carry) return;
        todSeconds_ = bcdInc0_59_(todSeconds_, carry);
        if (!carry) return;
        todMinutes_ = bcdInc0_59_(todMinutes_, carry);
        if (!carry) return;
        todHours_ = bcdHourInc_(todHours_);
    }
    bool todAlarmMatch_() const noexcept {
        return ((todTenths_ & 0x0Fu) == (todAlarmTenths_ & 0x0Fu)) &&
               todSeconds_ == todAlarmSeconds_ && todMinutes_ == todAlarmMinutes_ && todHours_ == todAlarmHours_;
    }
    void stepTod_(uint32_t cycles) noexcept {
        if (todStopped_ || cycles == 0u) return;
        const uint32_t todHz = todIs50Hz_() ? 50u : 60u;
        const uint32_t period = todFastDefaultDivider_ ? 1u :
            todIs50Hz_() && clockHz_ == 985248u ? kTodPal50HzPhi2Period :
            (!todIs50Hz_() && clockHz_ == 1022727u ? kTodNtsc60HzPhi2Period :
             clockHz_ / todHz + (((clockHz_ % todHz) * 2u >= todHz) ? 1u : 0u));
        todCycleAccum_ += cycles;
        while (todCycleAccum_ >= period) {
            todCycleAccum_ -= period;
            if (++todPulseAccum_ >= todPulsesPerTenth_()) {
                todPulseAccum_ = 0;
                tickTodOneTenth_();
                if (todAlarmMatch_()) setIcrEvent_(IcrTod);
            }
        }
    }

    static uint8_t normalizeTodWrite_(uint8_t idx, uint8_t value) noexcept {
        if (idx == 0) return uint8_t(value & 0x0Fu);
        if (idx == 1 || idx == 2) {
            const uint8_t hi = uint8_t((value >> 4u) & 0x0Fu);
            const uint8_t lo = uint8_t(value & 0x0Fu);
            return uint8_t((std::min<uint8_t>(hi, 5u) << 4u) | std::min<uint8_t>(lo, 9u));
        }
        const uint8_t pm = uint8_t(value & 0x80u);
        uint8_t hi = uint8_t((value >> 4u) & 0x01u);
        uint8_t lo = uint8_t(value & 0x0Fu);
        uint8_t hour = uint8_t(hi * 10u + lo);
        if (hour < 1u) hour = 1u;
        if (hour > 12u) hour = 12u;
        return uint8_t(pm | ((hour / 10u) << 4u) | (hour % 10u));
    }
    void writeTod_(uint8_t idx, uint8_t value) noexcept {
        const bool alarm = (regs_[0x0F] & 0x80u) != 0;
        const uint8_t norm = normalizeTodWrite_(idx, value);
        if (alarm) {
            if (idx == 0) todAlarmTenths_ = norm;
            else if (idx == 1) todAlarmSeconds_ = norm;
            else if (idx == 2) todAlarmMinutes_ = norm;
            else todAlarmHours_ = norm;
            return;
        }

        // MOS 6526 TOD write protocol: writing HOURS ($0B) stops the TOD
        // counter so software can update hours/minutes/seconds/tenths as an
        // atomic time-of-day value. Writing TENTHS ($08) restarts the clock.
        // The previous implementation had this reversed, which could freeze
        // tunes that program TOD in the normal $0B,$0A,$09,$08 order and could
        // let the counter run while the high fields were only partially written.
        if (idx == 3) {
            todHours_ = norm;
            todStopped_ = true;
            todWriteInProgress_ = true;
            todCycleAccum_ = 0;
            todPulseAccum_ = 0;
            return;
        }
        if (idx == 2) { todMinutes_ = norm; return; }
        if (idx == 1) { todSeconds_ = norm; return; }

        todTenths_ = norm;
        todStopped_ = false;
        todWriteInProgress_ = false;
        todCycleAccum_ = 0;
        todPulseAccum_ = 0;
    }
    uint8_t readTod_(uint8_t reg) noexcept {
        if (reg == 0x0B && !todLatched_) {
            todLatchTenths_ = todTenths_; todLatchSeconds_ = todSeconds_; todLatchMinutes_ = todMinutes_; todLatchHours_ = todHours_; todLatched_ = true;
        }
        if (todLatched_) {
            if (reg == 0x08) { todLatched_ = false; return todLatchTenths_; }
            if (reg == 0x09) return todLatchSeconds_;
            if (reg == 0x0A) return todLatchMinutes_;
            return todLatchHours_;
        }
        if (reg == 0x08) return todTenths_;
        if (reg == 0x09) return todSeconds_;
        if (reg == 0x0A) return todMinutes_;
        return todHours_;
    }

    void reloadA_() noexcept { timerA_ = latchA_; }
    void reloadB_() noexcept { timerB_ = latchB_; }
    void forceLoadA_() noexcept { timerA_ = latchA_; timerAReloadPending_ = false; }
    void forceLoadB_() noexcept { timerB_ = latchB_; timerBReloadPending_ = false; }
    void pb6OnUnderflow_() noexcept {
        if (!(regs_[0x0E] & 0x02u)) return;
        if (regs_[0x0E] & 0x04u) { pb6Out_ = 1u; pb6PulseRemaining_ = 1u; }
        else pb6Out_ ^= 1u;
    }
    void pb7OnUnderflow_() noexcept {
        if (!(regs_[0x0F] & 0x02u)) return;
        if (regs_[0x0F] & 0x04u) { pb7Out_ = 1u; pb7PulseRemaining_ = 1u; }
        else pb7Out_ ^= 1u;
    }
    void tickPbPulsesOneCycle_() noexcept {
        if (pb6PulseRemaining_ && --pb6PulseRemaining_ == 0u) pb6Out_ = 0u;
        if (pb7PulseRemaining_ && --pb7PulseRemaining_ == 0u) pb7Out_ = 0u;
    }
    void serialOnTimerAUnderflow_() noexcept {
        if (!serialOutputMode_()) return;
        if (serialBits_ == 0u) { serialShift_ = regs_[0x0C]; serialBits_ = 8u; serialCntPhase_ = 0u; }
        serialCntPhase_ ^= 1u;
        if (serialCntPhase_ == 0u) return;
        spOut_ = (serialShift_ & 0x80u) ? 1u : 0u;
        serialShift_ = uint8_t(serialShift_ << 1u);
        if (--serialBits_ == 0u) setIcrEvent_(IcrSdr);
    }
    void serialOnCntPulseInput_() noexcept {
        if (serialOutputMode_()) return;
        if (serialInCount_ == 0u) serialShift_ = 0;
        serialShift_ = uint8_t((static_cast<unsigned>(serialShift_) << 1u) | (spIn_ ? 1u : 0u));
        if (++serialInCount_ >= 8u) { serialInCount_ = 0; regs_[0x0C] = serialShift_; setIcrEvent_(IcrSdr); }
    }
    bool clockTimerA_() noexcept {
        if (!timerRunningA_()) return false;
        timerA_ = uint16_t(timerA_ - 1u);
        if (timerA_ != 0xFFFFu) return false;
        underflowA_();
        return true;
    }
    void clockTimerB_() noexcept {
        if (!timerRunningB_()) return;
        timerB_ = uint16_t(timerB_ - 1u);
        if (timerB_ == 0xFFFFu) underflowB_();
    }
    void tickCntEdge_() noexcept {
        if (!serialOutputMode_()) serialOnCntPulseInput_();
        bool taUnderflow = false;
        if (timerSourceA_() == TimerSource::Cnt) taUnderflow = clockTimerA_();
        if (!timerRunningB_()) return;
        const TimerSource src = timerSourceB_();
        if (src == TimerSource::Cnt) clockTimerB_();
        else if (taUnderflow && src == TimerSource::TimerAUnderflow) clockTimerB_();
        else if (taUnderflow && src == TimerSource::TimerAUnderflowCnt && cntIn_) clockTimerB_();
    }
    void tickOneCycle_(bool cntEdge) noexcept {
        tickPbPulsesOneCycle_();
        stepTod_(1u);
        bool taUnderflow = false;
        if (timerSourceA_() == TimerSource::Phi2) taUnderflow = clockTimerA_();
        if (cntEdge) {
            if (!serialOutputMode_()) serialOnCntPulseInput_();
            if (timerSourceA_() == TimerSource::Cnt) taUnderflow = clockTimerA_() || taUnderflow;
        }
        if (!timerRunningB_()) return;
        const TimerSource src = timerSourceB_();
        if (src == TimerSource::Phi2) clockTimerB_();
        else if (cntEdge && src == TimerSource::Cnt) clockTimerB_();
        else if (taUnderflow && src == TimerSource::TimerAUnderflow) clockTimerB_();
        else if (taUnderflow && src == TimerSource::TimerAUnderflowCnt && cntIn_) clockTimerB_();
    }
    bool underflowA_() noexcept {
        ++timerAUnderflowCount_;
        timerAJustUnderflowed_ = true;
        timerAReloadPending_ = true;
        pb6OnUnderflow_();
        serialOnTimerAUnderflow_();
        setIcrEvent_(IcrTimerA);
        reloadA_();
        timerAReloadPending_ = false;
        if (timerOneShotA_()) regs_[0x0E] &= uint8_t(~0x01u);
        return true;
    }
    bool underflowB_() noexcept {
        ++timerBUnderflowCount_;
        timerBJustUnderflowed_ = true;
        timerBReloadPending_ = true;
        pb7OnUnderflow_();
        setIcrEvent_(IcrTimerB);
        reloadB_();
        timerBReloadPending_ = false;
        if (timerOneShotB_()) regs_[0x0F] &= uint8_t(~0x01u);
        return true;
    }

    std::array<uint8_t, 16> regs_{};
    uint32_t clockHz_ = 985248u;
    uint8_t pra_ = 0, prb_ = 0, ddra_ = 0, ddrb_ = 0;
    uint8_t portAInput_ = 0xFFu, portBInput_ = 0xFFu;
    uint16_t latchA_ = 0xFFFFu, latchB_ = 0xFFFFu;
    uint16_t timerA_ = 0xFFFFu, timerB_ = 0xFFFFu;
    // v872: monotonic; intentionally NOT cleared by reset() so an init-time CIA
    // reset cannot corrupt a write-count delta taken across the tune's init run.
    uint64_t timerALatchWriteCount_ = 0;
    uint64_t timerAControlWriteCount_ = 0;
    uint64_t timerBLatchWriteCount_ = 0;
    uint64_t timerBControlWriteCount_ = 0;
    uint8_t irqFlags_ = 0, irqMask_ = 0;
    bool irqLevel_ = false;
    uint64_t timerAUnderflowCount_ = 0;
    uint64_t timerBUnderflowCount_ = 0;
    uint64_t irqEdgeCount_ = 0;
    uint32_t pendingCntPulses_ = 0;
    uint64_t cntRisingEdgeCount_ = 0;
    bool timerAReloadPending_ = false;
    bool timerBReloadPending_ = false;
    bool timerAJustUnderflowed_ = false;
    bool timerBJustUnderflowed_ = false;
    uint8_t taHiLatch_ = 0, tbHiLatch_ = 0;
    bool taLatched_ = false, tbLatched_ = false;
    uint8_t todTenths_ = 0, todSeconds_ = 0, todMinutes_ = 0, todHours_ = 0;
    uint8_t todAlarmTenths_ = 0, todAlarmSeconds_ = 0, todAlarmMinutes_ = 0, todAlarmHours_ = 0;
    bool todLatched_ = false;
    uint8_t todLatchTenths_ = 0, todLatchSeconds_ = 0, todLatchMinutes_ = 0, todLatchHours_ = 0;
    bool todStopped_ = false, todWriteInProgress_ = false;
    uint32_t todCycleAccum_ = 0, todPulseAccum_ = 0;
    uint8_t serialShift_ = 0, serialBits_ = 0, serialCntPhase_ = 0, serialInCount_ = 0;
    bool serialSelfClock_ = false;
    uint8_t spOut_ = 1, spIn_ = 1, cntIn_ = 0;
    uint8_t pb6Out_ = 0, pb7Out_ = 0;
    uint8_t pb6PulseRemaining_ = 0, pb7PulseRemaining_ = 0;
    bool flagLatched_ = false;
    bool todFastDefaultDivider_ = true;
};

} // namespace ArpSID::C64
