// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
// Bus-cycle-accurate 6510 microsequencer.
// Covers all 151 official opcodes + all stable NMOS illegal opcodes.
// Each tickPhi2Begin() / tickPhi2End() pair advances exactly one PHI2 cycle.
// Reset, IRQ/NMI interrupt sequences are also modelled at bus-cycle resolution.

#include "arpsid/core/c64_cpu6510_status.h"
#include "arpsid/core/c64_phi2_types.h"
#include <array>
#include <cstdint>

namespace ArpSID::C64 {

// Policy for how the CPU handles approximate (chip-sample-dependent) illegal
// opcodes: ARR, XAA, LAX#, AHX, SHY, SHX, TAS.
//
// Allow — execute with best-effort result, count only (default).
// Jam — treat the opcode as KIL/JAM: CPU halts, jammed=true.
// Exact-mode callers can detect the halt via state().jammed.
//
// Set via Cpu6510Micro::setApproximateOpcodePolicy(). The Jam policy is the
// correct choice for RSID strict-exact playback where any value-uncertainty
// must stop execution rather than produce a silently wrong result.
enum class ApproximateOpcodePolicy : uint8_t {
    Allow = 0,  ///< Execute with best-effort result (default, existing behaviour)
    Jam   = 1,  ///< Halt the CPU (jammed=true) on first approximate opcode
};

struct Cpu6510MicroState {
    uint16_t pc = 0;
    uint8_t a = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t sp = 0xFDu;
    uint8_t p = 0x34u;
    uint8_t opcode = 0;
    uint8_t t = 0;
    uint8_t lo = 0;
    uint8_t hi = 0;
    uint8_t tmp = 0;    // RMW: holds original read value
    uint8_t tmp2 = 0;   // RMW: holds computed new value
    uint8_t operand = 0;
    uint16_t addr = 0;  // effective address (post-index, post-deref)
    uint16_t ptr = 0;   // page-wrong / zp-indexed address for indexed modes
    bool pageCross = false;
    bool active = false;
    bool jammed = false;
    bool resetSequence = false;
    bool irqSequence = false;
    bool nmiSequence = false;
    bool brkSequence = false;
    bool irqLine = false;
    bool nmiLineLow = false;
    bool resetLine = false;
    bool nmiSeenHigh = true;
    bool nmiEdgeLatched = false;
    // CLI/SEI/PLP write the I flag on their final cycle, AFTER the hardware
    // interrupt poll for the following boundary has already sampled I. Model:
    // one-shot delayed I value consumed at the next instruction-boundary IRQ
    // decision (RTI is NOT delayed — its restored I is effective immediately).
    bool iFlagPollDelayActive = false;
    bool iFlagPollDelayedValue = false;
};

class Cpu6510Micro {
public:
    void powerOn() noexcept {
        s_ = Cpu6510MicroState{};
        rdy_ = true;
        aec_ = true;
        trapBrkAsJam_ = false;
        heldByRdy_ = false;
        pending_ = CpuBusRequest{};
        unsupportedOpcodeCount_.fill(0);
        unsupportedOpcodeTotal_ = 0;
        lastUnsupportedOpcode_ = 0;
        approximateOpcodeCount_.fill(0);
        approximateOpcodeTotal_ = 0;
        lastApproximateOpcode_ = 0;
        retiredInstructionCount_ = 0;
        irqLineLatchCount_ = 0;
        nmiEdgeLatchCount_ = 0;
        approximateOpcodePolicy_ = ApproximateOpcodePolicy::Allow;
        lastJamReason_ = CpuJamReason::None;
        lastJamOpcode_ = 0;
    }

    const Cpu6510MicroState& state() const noexcept { return s_; }
    Cpu6510MicroState& state() noexcept { return s_; }
    uint16_t pc() const noexcept { return s_.pc; }
    uint8_t a() const noexcept { return s_.a; }
    uint8_t x() const noexcept { return s_.x; }
    uint8_t y() const noexcept { return s_.y; }
    uint8_t sp() const noexcept { return s_.sp; }
    uint8_t p() const noexcept { return static_cast<uint8_t>(s_.p | kFlagUnused); }
    uint8_t opcode() const noexcept { return s_.opcode; }
    uint8_t microT() const noexcept { return s_.t; }
    uint64_t retiredInstructionCount() const noexcept { return retiredInstructionCount_; }
    uint64_t irqLineLatchCount() const noexcept { return irqLineLatchCount_; }
    uint64_t nmiEdgeLatchCount() const noexcept { return nmiEdgeLatchCount_; }
    uint64_t unsupportedOpcodeTotal() const noexcept { return unsupportedOpcodeTotal_; }
    uint64_t unsupportedOpcodeCount(uint8_t op) const noexcept { return unsupportedOpcodeCount_[op]; }
    uint8_t lastUnsupportedOpcode() const noexcept { return lastUnsupportedOpcode_; }
    // Approximate (chip-sample-dependent / unstable) illegal opcodes that the
    // micro CPU executes with a best-effort deterministic result rather than a
    // cycle/value-exact one: XAA(0x8B), LAX#(0xAB), AHX(0x93,0x9F), SHY(0x9C),
    // SHX(0x9E), TAS(0x9B). ARR(0x6B) is retained in this ledger for the
    // strict-policy contract, but its computed result is value-exact NMOS
    // semantics (binary and decimal). Strict-exact callers can observe these.
    uint64_t approximateOpcodeTotal() const noexcept { return approximateOpcodeTotal_; }
    uint64_t approximateOpcodeCount(uint8_t op) const noexcept { return approximateOpcodeCount_[op]; }
    uint8_t lastApproximateOpcode() const noexcept { return lastApproximateOpcode_; }
    CpuJamReason lastJamReason() const noexcept { return lastJamReason_; }
    uint8_t lastJamOpcode() const noexcept { return lastJamOpcode_; }

    // Approximate-opcode enforcement policy. Set to Jam for RSID strict-exact
    // mode; leave Allow for normal PSID/playback where best-effort is acceptable.
    ApproximateOpcodePolicy approximateOpcodePolicy() const noexcept {
        return approximateOpcodePolicy_;
    }
    void setApproximateOpcodePolicy(ApproximateOpcodePolicy p) noexcept {
        approximateOpcodePolicy_ = p;
    }

    static constexpr bool isApproximateOpcode(uint8_t op) noexcept {
        switch (op) {
            case 0x6Bu: // ARR #
            case 0x8Bu: // XAA #
            case 0xABu: // LAX #
            case 0x93u: // AHX (ind),Y
            case 0x9Fu: // AHX abs,Y
            case 0x9Cu: // SHY abs,X
            case 0x9Eu: // SHX abs,Y
            case 0x9Bu: // TAS abs,Y
                return true;
            default:
                return false;
        }
    }

    void setPc(uint16_t pc) noexcept {
        s_.pc = pc;
        s_.active = false;
        s_.t = 0;
        s_.resetSequence = false;
        s_.irqSequence = false;
        s_.nmiSequence = false;
        s_.brkSequence = false;
        s_.iFlagPollDelayActive = false;
    }

    void setA(uint8_t v) noexcept { s_.a = v; cpuSetNZ(s_.p, s_.a); }
    void setStatus(uint8_t p) noexcept { s_.p = static_cast<uint8_t>(p | kFlagUnused); }
    void setIrqLine(bool asserted) noexcept { if (asserted && !s_.irqLine) ++irqLineLatchCount_; s_.irqLine = asserted; }
    void setResetLine(bool asserted) noexcept { s_.resetLine = asserted; }
    void setRdy(bool high) noexcept { rdy_ = high; }
    void setAec(bool high) noexcept { aec_ = high; }
    // When true, BRK (0x00) is treated as KIL/JAM — sets jammed=true instead
    // of entering the 7-cycle interrupt sequence. Used by the PSID/RSID bootstrap
    // init path so that init code ending with BRK signals completion correctly.
    void setTrapBrkAsJam(bool enable) noexcept { trapBrkAsJam_ = enable; }

    void setNmiLineLow(bool low) noexcept {
        if (!low) s_.nmiSeenHigh = true;
        if (low && s_.nmiSeenHigh) { s_.nmiEdgeLatched = true; ++nmiEdgeLatchCount_; }
        s_.nmiLineLow = low;
        if (low) s_.nmiSeenHigh = false;
    }

    void beginResetSequence() noexcept {
        s_.resetSequence = true;
        s_.irqSequence = false;
        s_.nmiSequence = false;
        s_.brkSequence = false;
        s_.active = false;
        s_.jammed = false;
        s_.t = 0;
    }

    CpuBusRequest tickPhi2Begin() noexcept {
        pending_ = CpuBusRequest{};
        heldByRdy_ = false;
        if (!aec_ || s_.jammed) return pending_;

        // Resolve the exact bus request this cycle would issue. Note: this may
        // begin interrupt/NMI sequences (it advances sequence-selection state),
        // which is correct — those transitions happen at PHI2-begin on hardware.
        pending_ = computeRequest_();

        // RDY low stalls READ cycles only (writes always complete on the 6510).
        // When held, we keep the exact same address/kind on the bus and skip the
        // commit in tickPhi2End so the micro-state does not advance — the same
        // read is re-issued next cycle until RDY returns high. This holds the
        // real bus phase (address + access kind), not a fabricated PC dummy read.
        if (!rdy_ && pending_.active() && !pending_.isWrite()) {
            heldByRdy_ = true;
        }
        return pending_;
    }

    // Resolves the bus request for the current cycle and performs the
    // PHI2-begin sequence-selection transitions (reset/NMI/IRQ entry).
    CpuBusRequest computeRequest_() noexcept {
        if (s_.resetLine && !s_.resetSequence) beginResetSequence();
        if (s_.resetSequence) return emitResetCycle_();
        if (s_.nmiSequence) return emitIntCycle_(kVectorNmiLo, kVectorNmiHi);
        if (s_.irqSequence || s_.brkSequence) return emitIntCycle_(kVectorIrqLo, kVectorIrqHi);
        if (!s_.active) {
            if (s_.nmiEdgeLatched) {
                s_.nmiEdgeLatched = false;
                s_.nmiSequence = true;
                s_.iFlagPollDelayActive = false;
                s_.t = 0;
                return emitIntCycle_(kVectorNmiLo, kVectorNmiHi);
            }
            // The IRQ decision samples the I flag the hardware poll saw: for the
            // boundary immediately after CLI/SEI/PLP that is the PRE-instruction
            // value (one-shot delay); otherwise the live flag.
            const bool iMasked = s_.iFlagPollDelayActive
                ? s_.iFlagPollDelayedValue
                : ((s_.p & kFlagI) != 0u);
            if (s_.irqLine && !iMasked) {
                s_.irqSequence = true;
                s_.iFlagPollDelayActive = false;
                s_.t = 0;
                return emitIntCycle_(kVectorIrqLo, kVectorIrqHi);
            }
            return opcodeFetch_();
        }
        return emitOpcodeCycle_();
    }

    void tickPhi2End(uint8_t dataIn) noexcept {
        if (!pending_.active()) return;
        // RDY held this read cycle: do not advance — the same bus phase repeats.
        if (heldByRdy_) return;
        if (s_.resetSequence) { commitResetCycle_(dataIn); return; }
        if (s_.nmiSequence || s_.irqSequence || s_.brkSequence) { commitIntCycle_(dataIn); return; }
        if (pending_.kind == CpuAccessKind::OpcodeFetch) {
            // The boundary IRQ decision for this instruction has been made
            // (and committed — not RDY-held), so any one-shot CLI/SEI/PLP
            // delayed-I value has been consumed.
            s_.iFlagPollDelayActive = false;
            s_.opcode = dataIn;
            ++s_.pc;
            s_.active = true;
            s_.t = 1;
            if (isKil_(s_.opcode) || (s_.opcode == 0x00u && trapBrkAsJam_)) {
                s_.jammed = true;
                s_.active = false;
                s_.t = 0;
                lastJamOpcode_ = s_.opcode;
                lastJamReason_ = isKil_(s_.opcode) ? CpuJamReason::KilOpcode
                                                    : CpuJamReason::TrapBrkAsJam;
            } else if (!isImplemented_(s_.opcode)) {
                ++unsupportedOpcodeCount_[s_.opcode];
                ++unsupportedOpcodeTotal_;
                lastUnsupportedOpcode_ = s_.opcode;
                s_.jammed = true;
                s_.active = false;
                s_.t = 0;
                lastJamOpcode_ = s_.opcode;
                lastJamReason_ = CpuJamReason::UnsupportedOpcode;
            } else if (isApproximateOpcode(s_.opcode)) {
                // Implemented, but with a best-effort (not value/cycle-exact)
                // result. Count it so strict-exact mode can downgrade/flag.
                ++approximateOpcodeCount_[s_.opcode];
                ++approximateOpcodeTotal_;
                lastApproximateOpcode_ = s_.opcode;
                if (approximateOpcodePolicy_ == ApproximateOpcodePolicy::Jam) {
                    // Strict-exact policy: halt the CPU rather than produce a
                    // silently wrong result. Callers detect the halt via jammed().
                    s_.jammed = true;
                    s_.active = false;
                    s_.t = 0;
                    lastJamOpcode_ = s_.opcode;
                    lastJamReason_ = CpuJamReason::ApproximateOpcodeStrictPolicy;
                }
            }
            return;
        }
        commitOpcodeCycle_(dataIn);
    }

private:
    Cpu6510MicroState s_{};
    CpuBusRequest pending_{};
    bool rdy_ = true;
    bool aec_ = true;
    bool trapBrkAsJam_ = false;
    bool heldByRdy_ = false;
    std::array<uint64_t, 256> unsupportedOpcodeCount_{};
    std::array<uint64_t, 256> approximateOpcodeCount_{};
    uint64_t approximateOpcodeTotal_ = 0;
    uint8_t lastApproximateOpcode_ = 0;
    uint64_t unsupportedOpcodeTotal_ = 0;
    uint8_t lastUnsupportedOpcode_ = 0;
    uint64_t retiredInstructionCount_ = 0;
    uint64_t irqLineLatchCount_ = 0;
    uint64_t nmiEdgeLatchCount_ = 0;
    ApproximateOpcodePolicy approximateOpcodePolicy_ = ApproximateOpcodePolicy::Allow;
    CpuJamReason lastJamReason_ = CpuJamReason::None;
    uint8_t lastJamOpcode_ = 0;

    // ── KIL/JAM detection ────────────────────────────────────────────────────
    static bool isKil_(uint8_t op) noexcept {
        switch (op) {
            case 0x02u: case 0x12u: case 0x22u: case 0x32u:
            case 0x42u: case 0x52u: case 0x62u: case 0x72u:
            case 0x92u: case 0xB2u: case 0xD2u: case 0xF2u:
                return true;
            default: return false;
        }
    }

    // ── Opcode coverage ──────────────────────────────────────────────────────
    // All 151 official opcodes + all stable NMOS illegals. KIL not listed here.
    static bool isImplemented_(uint8_t op) noexcept {
        // Official opcodes
        switch (op) {
            case 0x00u: case 0x01u: case 0x05u: case 0x06u: case 0x08u:
            case 0x09u: case 0x0Au: case 0x0Du: case 0x0Eu: case 0x10u:
            case 0x11u: case 0x15u: case 0x16u: case 0x18u: case 0x19u:
            case 0x1Du: case 0x1Eu: case 0x20u: case 0x21u: case 0x24u:
            case 0x25u: case 0x26u: case 0x28u: case 0x29u: case 0x2Au:
            case 0x2Cu: case 0x2Du: case 0x2Eu: case 0x30u: case 0x31u:
            case 0x35u: case 0x36u: case 0x38u: case 0x39u: case 0x3Du:
            case 0x3Eu: case 0x40u: case 0x41u: case 0x45u: case 0x46u:
            case 0x48u: case 0x49u: case 0x4Au: case 0x4Cu: case 0x4Du:
            case 0x4Eu: case 0x50u: case 0x51u: case 0x55u: case 0x56u:
            case 0x58u: case 0x59u: case 0x5Du: case 0x5Eu: case 0x60u:
            case 0x61u: case 0x65u: case 0x66u: case 0x68u: case 0x69u:
            case 0x6Au: case 0x6Cu: case 0x6Du: case 0x6Eu: case 0x70u:
            case 0x71u: case 0x75u: case 0x76u: case 0x78u: case 0x79u:
            case 0x7Du: case 0x7Eu: case 0x81u: case 0x84u: case 0x85u:
            case 0x86u: case 0x88u: case 0x8Au: case 0x8Cu: case 0x8Du:
            case 0x8Eu: case 0x90u: case 0x91u: case 0x94u: case 0x95u:
            case 0x96u: case 0x98u: case 0x99u: case 0x9Au: case 0x9Du:
            case 0xA0u: case 0xA1u: case 0xA2u: case 0xA4u: case 0xA5u:
            case 0xA6u: case 0xA8u: case 0xA9u: case 0xAAu: case 0xACu:
            case 0xADu: case 0xAEu: case 0xB0u: case 0xB1u: case 0xB4u:
            case 0xB5u: case 0xB6u: case 0xB8u: case 0xB9u: case 0xBAu:
            case 0xBCu: case 0xBDu: case 0xBEu: case 0xC0u: case 0xC1u:
            case 0xC4u: case 0xC5u: case 0xC6u: case 0xC8u: case 0xC9u:
            case 0xCAu: case 0xCCu: case 0xCDu: case 0xCEu: case 0xD0u:
            case 0xD1u: case 0xD5u: case 0xD6u: case 0xD8u: case 0xD9u:
            case 0xDDu: case 0xDEu: case 0xE0u: case 0xE1u: case 0xE4u:
            case 0xE5u: case 0xE6u: case 0xE8u: case 0xE9u: case 0xEAu:
            case 0xECu: case 0xEDu: case 0xEEu: case 0xF0u: case 0xF1u:
            case 0xF5u: case 0xF6u: case 0xF8u: case 0xF9u: case 0xFDu:
            case 0xFEu:
            // Stable illegals (NOPs with operands + composites)
            case 0x04u: case 0x0Cu: case 0x14u: case 0x1Au: case 0x1Cu:
            case 0x34u: case 0x3Au: case 0x3Cu: case 0x44u: case 0x54u:
            case 0x5Au: case 0x5Cu: case 0x64u: case 0x74u: case 0x7Au:
            case 0x7Cu: case 0x80u: case 0x82u: case 0x89u: case 0xC2u:
            case 0xD4u: case 0xDAu: case 0xDCu: case 0xE2u: case 0xF4u:
            case 0xFAu: case 0xFCu:
            case 0x03u: case 0x07u: case 0x0Bu: case 0x0Fu:
            case 0x13u: case 0x17u: case 0x1Bu: case 0x1Fu:
            case 0x23u: case 0x27u: case 0x2Bu: case 0x2Fu:
            case 0x33u: case 0x37u: case 0x3Bu: case 0x3Fu:
            case 0x43u: case 0x47u: case 0x4Bu: case 0x4Fu:
            case 0x53u: case 0x57u: case 0x5Bu: case 0x5Fu:
            case 0x63u: case 0x67u: case 0x6Bu: case 0x6Fu:
            case 0x73u: case 0x77u: case 0x7Bu: case 0x7Fu:
            case 0x83u: case 0x87u: case 0x8Bu: case 0x8Fu:
            case 0x93u: case 0x97u: case 0x9Bu: case 0x9Cu:
            case 0x9Eu: case 0x9Fu:
            case 0xA3u: case 0xA7u: case 0xABu: case 0xAFu:
            case 0xB3u: case 0xB7u: case 0xBBu: case 0xBFu:
            case 0xC3u: case 0xC7u: case 0xCBu: case 0xCFu:
            case 0xD3u: case 0xD7u: case 0xDBu: case 0xDFu:
            case 0xE3u: case 0xE7u: case 0xEBu: case 0xEFu:
            case 0xF3u: case 0xF7u: case 0xFBu: case 0xFFu:
                return true;
            default: return false;
        }
    }

    // ── Bus request helpers ──────────────────────────────────────────────────
    CpuBusRequest req_(CpuAccessKind kind, uint16_t address, uint8_t dataOut = 0xFFu) const noexcept {
        CpuBusRequest r{};
        r.kind = kind;
        r.address = address;
        r.dataOut = dataOut;
        r.rw = !r.isWrite();
        r.sync = (kind == CpuAccessKind::OpcodeFetch);
        r.stackAccess = (kind == CpuAccessKind::StackRead || kind == CpuAccessKind::StackWrite);
        r.vectorFetch = (kind == CpuAccessKind::VectorReadLow || kind == CpuAccessKind::VectorReadHigh);
        r.dummy = (kind == CpuAccessKind::DummyRead || kind == CpuAccessKind::DummyWrite);
        return r;
    }
    CpuBusRequest opcodeFetch_() const noexcept { return req_(CpuAccessKind::OpcodeFetch, s_.pc); }
    CpuBusRequest rdPc_() const noexcept { return req_(CpuAccessKind::Read, s_.pc); }
    CpuBusRequest rdAddr_() const noexcept { return req_(CpuAccessKind::Read, s_.addr); }
    CpuBusRequest rdPtr_() const noexcept { return req_(CpuAccessKind::Read, s_.ptr); }
    CpuBusRequest wrAddr_(uint8_t v) const noexcept { return req_(CpuAccessKind::Write, s_.addr, v); }
    CpuBusRequest wrAddrRmwDummy_(uint8_t v) const noexcept {
        auto r = req_(CpuAccessKind::DummyWrite, s_.addr, v);
        r.rmwDummyWrite = true;
        return r;
    }
    CpuBusRequest wrAddrRmwFinal_(uint8_t v) const noexcept {
        auto r = req_(CpuAccessKind::Write, s_.addr, v);
        r.rmwFinalWrite = true;
        return r;
    }
    CpuBusRequest dummyRd_(uint16_t a) const noexcept { return req_(CpuAccessKind::DummyRead, a); }
    CpuBusRequest stkRd_() const noexcept { return req_(CpuAccessKind::StackRead, static_cast<uint16_t>(0x0100u | s_.sp)); }
    CpuBusRequest stkWr_(uint8_t v) const noexcept { return req_(CpuAccessKind::StackWrite, static_cast<uint16_t>(0x0100u | s_.sp), v); }

    static uint16_t mk16_(uint8_t lo, uint8_t hi) noexcept {
        return static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8u));
    }
    void finish_() noexcept { ++retiredInstructionCount_; s_.active = false; s_.brkSequence = false; s_.t = 0; s_.opcode = 0; }
    void stkPush_() noexcept { s_.sp = static_cast<uint8_t>(s_.sp - 1u); }
    void stkPop_() noexcept { s_.sp = static_cast<uint8_t>(s_.sp + 1u); }

    // ── NZ flag helpers ──────────────────────────────────────────────────────
    void setNZ_(uint8_t v) noexcept {
        if (v) s_.p = static_cast<uint8_t>(s_.p & static_cast<uint8_t>(~kFlagZ));
        else   s_.p = static_cast<uint8_t>(s_.p | kFlagZ);
        if (v & 0x80u) s_.p = static_cast<uint8_t>(s_.p | kFlagN);
        else           s_.p = static_cast<uint8_t>(s_.p & static_cast<uint8_t>(~kFlagN));
        s_.p = static_cast<uint8_t>(s_.p | kFlagUnused);
    }
    void setF_(uint8_t mask, bool cond) noexcept {
        if (cond) s_.p = static_cast<uint8_t>(s_.p | mask);
        else      s_.p = static_cast<uint8_t>(s_.p & static_cast<uint8_t>(~mask));
    }

    // ── ALU operations ───────────────────────────────────────────────────────
    uint8_t doASL_(uint8_t v) noexcept {
        setF_(kFlagC, (v & 0x80u) != 0u);
        v = static_cast<uint8_t>(v << 1u);
        setNZ_(v);
        return v;
    }
    uint8_t doLSR_(uint8_t v) noexcept {
        setF_(kFlagC, (v & 0x01u) != 0u);
        v = static_cast<uint8_t>(v >> 1u);
        setNZ_(v);
        return v;
    }
    uint8_t doROL_(uint8_t v) noexcept {
        const uint8_t cin = (s_.p & kFlagC) ? 1u : 0u;
        setF_(kFlagC, (v & 0x80u) != 0u);
        v = static_cast<uint8_t>((v << 1u) | cin);
        setNZ_(v);
        return v;
    }
    uint8_t doROR_(uint8_t v) noexcept {
        const uint8_t cin = (s_.p & kFlagC) ? 0x80u : 0u;
        setF_(kFlagC, (v & 0x01u) != 0u);
        v = static_cast<uint8_t>((v >> 1u) | cin);
        setNZ_(v);
        return v;
    }
    uint8_t doINC_(uint8_t v) noexcept { v = static_cast<uint8_t>(v + 1u); setNZ_(v); return v; }
    uint8_t doDEC_(uint8_t v) noexcept { v = static_cast<uint8_t>(v - 1u); setNZ_(v); return v; }
    void doADC_(uint8_t v) noexcept {
        const uint8_t a0 = s_.a;
        const uint8_t cin = (s_.p & kFlagC) ? 1u : 0u;
        const uint16_t sum = static_cast<uint16_t>(a0) + v + cin;
        const uint8_t r = static_cast<uint8_t>(sum);
        if (s_.p & kFlagD) {
            // NMOS decimal ADC. The accumulator gets the BCD-adjusted result,
            // but the flags come from intermediate values, not the BCD result:
            // Z is the binary sum; N/V come from the high-nibble intermediate
            // before the +0x60 correction (6502 decimal-mode die behaviour).
            unsigned al = static_cast<unsigned>(a0 & 0x0Fu) + static_cast<unsigned>(v & 0x0Fu) + cin;
            if (al >= 0x0Au) al = ((al + 0x06u) & 0x0Fu) + 0x10u;
            unsigned t = static_cast<unsigned>(a0 & 0xF0u) + static_cast<unsigned>(v & 0xF0u) + al;
            setF_(kFlagZ, r == 0u);
            setF_(kFlagN, (t & 0x80u) != 0u);
            setF_(kFlagV, ((~(a0 ^ v) & (a0 ^ static_cast<uint8_t>(t))) & 0x80u) != 0u);
            if (t >= 0xA0u) t += 0x60u;
            setF_(kFlagC, t >= 0x100u);
            s_.a = static_cast<uint8_t>(t);
            s_.p = static_cast<uint8_t>(s_.p | kFlagUnused);
        } else {
            setF_(kFlagV, ((~(a0 ^ v) & (a0 ^ r)) & 0x80u) != 0u);
            setF_(kFlagC, sum > 0xFFu);
            s_.a = r;
            setNZ_(s_.a);
        }
    }
    void doSBC_(uint8_t v) noexcept {
        const uint8_t a0 = s_.a;
        const uint8_t cin = (s_.p & kFlagC) ? 1u : 0u;
        const uint16_t diff = static_cast<uint16_t>(a0) - v - (cin ? 0u : 1u);
        const uint8_t r = static_cast<uint8_t>(diff);
        setF_(kFlagV, (((a0 ^ v) & (a0 ^ r)) & 0x80u) != 0u);
        setF_(kFlagC, diff < 0x100u);
        if (s_.p & kFlagD) {
            // NMOS decimal SBC: ALL flags (N/Z/C/V) come from the binary
            // difference; only the accumulator gets the BCD-adjusted value.
            int al = static_cast<int>(a0 & 0x0Fu) - static_cast<int>(v & 0x0Fu) - static_cast<int>(cin ? 0 : 1);
            if (al < 0) al = ((al - 0x06) & 0x0F) - 0x10;
            int t = static_cast<int>(a0 & 0xF0u) - static_cast<int>(v & 0xF0u) + al;
            if (t < 0) t -= 0x60;
            setNZ_(r);
            s_.a = static_cast<uint8_t>(t & 0xFF);
        } else {
            s_.a = r;
            setNZ_(s_.a);
        }
    }
    void doCMP_(uint8_t reg, uint8_t v) noexcept {
        const uint8_t r = static_cast<uint8_t>(reg - v);
        setF_(kFlagC, reg >= v);
        setNZ_(r);
    }
    void doBIT_(uint8_t v) noexcept {
        setF_(kFlagZ, (s_.a & v) == 0u);
        setF_(kFlagN, (v & 0x80u) != 0u);
        setF_(kFlagV, (v & 0x40u) != 0u);
    }

    // ── Absolute-X/Y page cross helpers ─────────────────────────────────────
    // At T2 (after fetching hi byte), compute effective addr + page info.
    // s_.lo holds low byte of base, dataIn holds high byte.
    // index: X or Y register value.
    void computeAbsIdx_(uint8_t dataIn, uint8_t index) noexcept {
        s_.hi = dataIn;
        ++s_.pc;
        const uint16_t base = mk16_(s_.lo, s_.hi);
        const uint16_t eff = static_cast<uint16_t>(base + index);
        s_.pageCross = ((base ^ eff) & 0xFF00u) != 0u;
        s_.addr = eff;
        // page-wrong addr: correct lo + original hi
        s_.ptr = mk16_(static_cast<uint8_t>(eff), s_.hi);
    }

    // ── Emit: produce bus request for current T ───────────────────────────
    CpuBusRequest emitOpcodeCycle_() noexcept {
        const uint8_t op = s_.opcode;
        const uint8_t t  = s_.t;

        // ── Implied / accumulator (2 cycles) ─────────────────────────────
        // T1: dummy read of PC (already incremented past opcode).
        switch (op) {
            case 0x18u: case 0x38u: case 0x58u: case 0x78u: // CLC SEC CLI SEI
            case 0x88u: case 0x8Au: case 0x98u: case 0x9Au: // DEY TXA TYA TXS
            case 0xA8u: case 0xAAu: case 0xBAu: case 0xB8u: // TAY TAX TSX CLV
            case 0xC8u: case 0xCAu: case 0xD8u: case 0xE8u: // INY DEX CLD INX
            case 0xEAu: case 0xF8u:                          // NOP SED
            // NOP implied illegals
            case 0x1Au: case 0x3Au: case 0x5Au: case 0x7Au:
            case 0xDAu: case 0xFAu:
                return dummyRd_(s_.pc);

            // ── Accumulator shift (2 cycles) ───────────────────────────
            case 0x0Au: case 0x2Au: case 0x4Au: case 0x6Au: // ASL ROL LSR ROR acc
                return dummyRd_(s_.pc);

            // ── Push to stack: PHA(48) PHP(08) (3 cycles) ─────────────
            // T1: dummy read PC. T2: write stack.
            case 0x48u: case 0x08u:
                if (t == 1) return dummyRd_(s_.pc);
                return stkWr_(op == 0x48u ? s_.a
                                           : static_cast<uint8_t>(s_.p | kFlagB | kFlagUnused));

            // ── Pull from stack: PLA(68) PLP(28) (4 cycles) ───────────
            // T1: dummy rd PC. T2: dummy rd SP (S unchanged). T3: pull.
            case 0x68u: case 0x28u:
                if (t == 1) return dummyRd_(s_.pc);
                if (t == 2) return dummyRd_(static_cast<uint16_t>(0x0100u | s_.sp));
                return stkRd_(); // reads from (SP+1) — committed in commit

            // ── BRK (7 cycles: enters brkSequence at T1) ──────────────
            case 0x00u:
                s_.brkSequence = true;
                s_.active = false;
                s_.t = 1; // enter interrupt sequence at T1 (BRK increments PC)
                return emitIntCycle_(kVectorIrqLo, kVectorIrqHi);

            // ── RTI (6 cycles) ────────────────────────────────────────
            // T1: dummy rd PC. T2: dummy rd SP. T3: pull P. T4: pull PCL. T5: pull PCH.
            case 0x40u:
                if (t == 1) return dummyRd_(s_.pc);
                if (t == 2) return dummyRd_(static_cast<uint16_t>(0x0100u | s_.sp));
                if (t == 3) return stkRd_(); // pull P → s_.sp+1 committed
                if (t == 4) return stkRd_(); // pull PCL
                return stkRd_();             // pull PCH

            // ── RTS (6 cycles) ────────────────────────────────────────
            // T1: dummy rd PC. T2: dummy rd SP. T3: pull PCL. T4: pull PCH. T5: dummy rd (PC++).
            case 0x60u:
                if (t == 1) return dummyRd_(s_.pc);
                if (t == 2) return dummyRd_(static_cast<uint16_t>(0x0100u | s_.sp));
                if (t == 3) return stkRd_(); // PCL
                if (t == 4) return stkRd_(); // PCH
                return dummyRd_(s_.pc);      // T5: dummy read of pulled PC (before +1)

            // ── JSR abs (6 cycles) ────────────────────────────────────
            // T1: rd PC (ADL). T2: dummy rd SP. T3: push PCH. T4: push PCL. T5: rd PC (ADH).
            case 0x20u:
                if (t == 1) return rdPc_();
                if (t == 2) return dummyRd_(static_cast<uint16_t>(0x0100u | s_.sp));
                if (t == 3) return stkWr_(static_cast<uint8_t>(s_.pc >> 8u));
                if (t == 4) return stkWr_(static_cast<uint8_t>(s_.pc & 0xFFu));
                return rdPc_(); // T5: ADH

            // ── JMP abs (3 cycles) ────────────────────────────────────
            case 0x4Cu:
                if (t == 1) return rdPc_();
                return rdPc_(); // T2: ADH

            // ── JMP (ind) (5 cycles) ──────────────────────────────────
            // T1: rd ADL. T2: rd ADH → form ptr. T3: rd(ptr). T4: rd(ptr_hi_same_page).
            case 0x6Cu:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPc_();
                if (t == 3) return rdAddr_();  // addr=ptr, s_.addr set at T2 commit
                return req_(CpuAccessKind::Read,  // T4: NMOS bug: wraps within page
                            static_cast<uint16_t>((s_.addr & 0xFF00u) | ((s_.addr + 1u) & 0x00FFu)));

            // ── Branch (2-4 cycles) ───────────────────────────────────
            // T1: read displacement. T2 (taken): dummy read at the post-operand PC
            // (the next-instruction byte) while the ALU adds the displacement —
            // 64doc: the address bus still carries PC on this cycle. T3 (page-cross
            // only): dummy read at the wrong-page address (old PCH | new PCL,
            // s_.ptr) before PCH is fixed up. PC itself is finalized at commit.
            case 0x10u: case 0x30u: case 0x50u: case 0x70u: // BPL BMI BVC BVS
            case 0x90u: case 0xB0u: case 0xD0u: case 0xF0u: // BCC BCS BNE BEQ
                if (t == 1) return rdPc_(); // read displacement
                if (t == 2) return dummyRd_(s_.pc); // T2: next-instruction byte (dummy)
                return dummyRd_(s_.ptr); // T3: wrong-page target (page-cross only)

            // ── Immediate (2 cycles) ──────────────────────────────────
            case 0x09u: case 0x29u: case 0x49u: case 0x69u: // ORA AND EOR ADC imm
            case 0xA0u: case 0xA2u: case 0xA9u: case 0xC0u: // LDY LDX LDA CPY
            case 0xC9u: case 0xE0u: case 0xE9u:              // CMP CPX SBC
            // NOP #imm illegals (2 cycles, read and discard)
            case 0x80u: case 0x82u: case 0x89u: case 0xC2u: case 0xE2u:
            // Stable illegal #imm
            case 0x0Bu: case 0x2Bu: case 0x4Bu: case 0x6Bu:
            case 0x8Bu: case 0xABu: case 0xCBu: case 0xEBu:
                return rdPc_();

            default:
                break;
        }

        // ── Multi-byte addressing modes ───────────────────────────────────
        // Grouped by mode family, T state.
        // ZP (3 cycle): T1 → rdPc_ ; T2 → rd(ZP)
        // ZP,X/Y (4 cycle): T1 → rdPc_ ; T2 → dummyRd(ZP) ; T3 → rd(ZP+idx)
        // ABS (4 cycle): T1 → rdPc_ ; T2 → rdPc_ ; T3 → rd(abs) or wr(abs)
        // ABS RMW (6 cycle): T1,T2 addr ; T3 rd ; T4 dummy-wr(old) ; T5 wr(new)
        // ABS,X/Y read (4-5 cycle): T1 T2 addr ; T3 [dummy] ; T3/T4 rd
        // ABS,X/Y write (5 cycle): T1 T2 addr ; T3 dummy ; T4 wr
        // ABS,X/Y RMW (7 cycle): T1 T2 T3(dummy) T4 rd T5 dummy-wr T6 wr
        // (ind,X) (6 cycle): T1 rd(ZP) ; T2 dummy ; T3 rd(ZP+X) ; T4 rd(ZP+X+1) ; T5 rd/wr
        // (ind),Y read (5-6): T1 rd(ZP) ; T2 rd ptr_lo ; T3 rd ptr_hi ; T4 [dummy] ; T4/5 rd
        // (ind),Y write (6): same but always dummy T4 and wr T5
        // (ind),Y RMW (8): T1-T3 as read ; T4 dummy ; T5 rd ; T6 dummy-wr ; T7 wr

        // Dispatch by opcode family using a helper
        return emitMultiByteCycle_();
    }

    CpuBusRequest emitMultiByteCycle_() noexcept {
        const uint8_t op = s_.opcode;
        const uint8_t t  = s_.t;

        // Helper lambdas for addr-mode dispatch based on opcode mode tag
        // All these return on first match.

        // ── ZP read (3 cycles) ────────────────────────────────────────────
        // (various ops not already handled in the main switch above)
        // In emitOpcodeCycle_ we already handle common ZP cases but missed some.
        // Let me handle ALL modes cleanly here using mode classification.

        switch (addrModeTag_(op)) {
            // ── ZP read ───────────────────────────────────────────────────
            case kZP_R:
            case kZP_W:
                if (t == 1) return rdPc_();
                if (kZP_R == addrModeTag_(op)) return rdAddr_();  // T2: read
                return wrAddr_(zpWriteData_(op));  // T2: write (ZP,W)
            case kZP_RMW:
                if (t == 1) return rdPc_();
                if (t == 2) return rdAddr_();
                if (t == 3) return wrAddrRmwDummy_(s_.tmp);
                return wrAddrRmwFinal_(s_.tmp2);
            // ── ZP,X read/write ───────────────────────────────────────────
            case kZPX_R:
            case kZPX_W:
                if (t == 1) return rdPc_();
                if (t == 2) return dummyRd_(s_.lo); // dummy before indexing
                if (kZPX_R == addrModeTag_(op)) return rdAddr_(); // T3: read
                return wrAddr_(zpxWriteData_(op));  // T3: write
            case kZPX_RMW:
                if (t == 1) return rdPc_();
                if (t == 2) return dummyRd_(s_.lo);
                if (t == 3) return rdAddr_();
                if (t == 4) return wrAddrRmwDummy_(s_.tmp);
                return wrAddrRmwFinal_(s_.tmp2);
            // ── ZP,Y read/write ───────────────────────────────────────────
            case kZPY_R:
            case kZPY_W:
                if (t == 1) return rdPc_();
                if (t == 2) return dummyRd_(s_.lo);
                if (kZPY_R == addrModeTag_(op)) return rdAddr_();
                return wrAddr_(zpyWriteData_(op));
            // ── ABS read/write ────────────────────────────────────────────
            case kABS_R:
            case kABS_W:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPc_();
                if (kABS_R == addrModeTag_(op)) return rdAddr_();
                return wrAddr_(absWriteData_(op));
            case kABS_RMW:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPc_();
                if (t == 3) return rdAddr_();
                if (t == 4) return wrAddrRmwDummy_(s_.tmp);
                return wrAddrRmwFinal_(s_.tmp2);
            // ── ABS,X read (4-5 cycles) ───────────────────────────────────
            case kABSX_R:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPc_();
                if (t == 3) {
                    if (!s_.pageCross) return rdAddr_(); // no cross: read effective
                    return dummyRd_(s_.ptr);             // cross: read wrong-page first
                }
                return rdAddr_(); // T4 (only reached on page cross)
            // ── ABS,X write (5 cycles, always dummy T3) ───────────────────
            case kABSX_W:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPc_();
                if (t == 3) return dummyRd_(s_.ptr); // always
                return wrAddr_(absxWriteData_(op));
            // ── ABS,X RMW (7 cycles) ──────────────────────────────────────
            case kABSX_RMW:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPc_();
                if (t == 3) return dummyRd_(s_.ptr);
                if (t == 4) return rdAddr_();
                if (t == 5) return wrAddrRmwDummy_(s_.tmp);
                return wrAddrRmwFinal_(s_.tmp2);
            // ── ABS,Y read (4-5 cycles) ───────────────────────────────────
            case kABSY_R:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPc_();
                if (t == 3) {
                    if (!s_.pageCross) return rdAddr_();
                    return dummyRd_(s_.ptr);
                }
                return rdAddr_();
            // ── ABS,Y write (5 cycles) ────────────────────────────────────
            case kABSY_W:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPc_();
                if (t == 3) return dummyRd_(s_.ptr);
                return wrAddr_(absyWriteData_(op));
            // ── ABS,Y RMW (7 cycles) ──────────────────────────────────────
            case kABSY_RMW:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPc_();
                if (t == 3) return dummyRd_(s_.ptr);
                if (t == 4) return rdAddr_();
                if (t == 5) return wrAddrRmwDummy_(s_.tmp);
                return wrAddrRmwFinal_(s_.tmp2);
            // ── (ind,X) read (6 cycles) ───────────────────────────────────
            case kINDX_R:
            case kINDX_W:
                if (t == 1) return rdPc_();       // ZP byte
                if (t == 2) return dummyRd_(s_.lo); // dummy before +X
                if (t == 3) return rdPtr_();        // ptr_lo from ZP+X
                if (t == 4) return req_(CpuAccessKind::Read,
                                        static_cast<uint16_t>((s_.ptr + 1u) & 0x00FFu)); // ptr_hi
                if (kINDX_R == addrModeTag_(op)) return rdAddr_();
                return wrAddr_(indxWriteData_(op));
            case kINDX_RMW:
                if (t == 1) return rdPc_();
                if (t == 2) return dummyRd_(s_.lo);
                if (t == 3) return rdPtr_();
                if (t == 4) return req_(CpuAccessKind::Read,
                                        static_cast<uint16_t>((s_.ptr + 1u) & 0x00FFu));
                if (t == 5) return rdAddr_();
                if (t == 6) return wrAddrRmwDummy_(s_.tmp);
                return wrAddrRmwFinal_(s_.tmp2);
            // ── (ind),Y read (5-6 cycles) ─────────────────────────────────
            case kINDY_R:
                if (t == 1) return rdPc_();       // ZP pointer
                if (t == 2) return rdPtr_();       // ptr_lo (ptr = ZP byte)
                if (t == 3) return req_(CpuAccessKind::Read,
                                        static_cast<uint16_t>((s_.ptr + 1u) & 0x00FFu)); // ptr_hi
                if (t == 4) {
                    if (!s_.pageCross) return rdAddr_();
                    return dummyRd_(mk16_(static_cast<uint8_t>(s_.addr), s_.hi));
                }
                return rdAddr_(); // T5 (page cross)
            // ── (ind),Y write (6 cycles) ─────────────────────────────────
            case kINDY_W:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPtr_();
                if (t == 3) return req_(CpuAccessKind::Read,
                                        static_cast<uint16_t>((s_.ptr + 1u) & 0x00FFu));
                if (t == 4) return dummyRd_(mk16_(static_cast<uint8_t>(s_.addr), s_.hi));
                return wrAddr_(indyWriteData_(op));
            // ── (ind),Y RMW (8 cycles) ────────────────────────────────────
            case kINDY_RMW:
                if (t == 1) return rdPc_();
                if (t == 2) return rdPtr_();
                if (t == 3) return req_(CpuAccessKind::Read,
                                        static_cast<uint16_t>((s_.ptr + 1u) & 0x00FFu));
                if (t == 4) return dummyRd_(mk16_(static_cast<uint8_t>(s_.addr), s_.hi));
                if (t == 5) return rdAddr_();
                if (t == 6) return wrAddrRmwDummy_(s_.tmp);
                return wrAddrRmwFinal_(s_.tmp2);
            default:
                break;
        }
        return dummyRd_(s_.pc); // fallback
    }

    // ── Commit: process bus result for current T ──────────────────────────
    void commitOpcodeCycle_(uint8_t dataIn) noexcept {
        const uint8_t op = s_.opcode;
        const uint8_t t  = s_.t;

        switch (op) {
            // ── Implied / accumulator ─────────────────────────────────────
            case 0x18u: s_.p &= static_cast<uint8_t>(~kFlagC); finish_(); return; // CLC
            case 0x38u: s_.p |= kFlagC;           finish_(); return; // SEC
            case 0x58u: // CLI — I write lands after the boundary poll (1-instr delay)
                s_.iFlagPollDelayedValue = (s_.p & kFlagI) != 0u;
                s_.iFlagPollDelayActive = true;
                s_.p &= static_cast<uint8_t>(~kFlagI); finish_(); return;
            case 0x78u: // SEI — same delayed-poll semantics as CLI
                s_.iFlagPollDelayedValue = (s_.p & kFlagI) != 0u;
                s_.iFlagPollDelayActive = true;
                s_.p |= kFlagI;           finish_(); return;
            case 0xB8u: s_.p &= static_cast<uint8_t>(~kFlagV); finish_(); return; // CLV
            case 0xD8u: s_.p &= static_cast<uint8_t>(~kFlagD); finish_(); return; // CLD
            case 0xF8u: s_.p |= kFlagD;           finish_(); return; // SED
            case 0x88u: s_.y = doDEC_(s_.y); finish_(); return; // DEY
            case 0xC8u: s_.y = doINC_(s_.y); finish_(); return; // INY
            case 0xCAu: s_.x = doDEC_(s_.x); finish_(); return; // DEX
            case 0xE8u: s_.x = doINC_(s_.x); finish_(); return; // INX
            case 0x8Au: s_.a = s_.x; setNZ_(s_.a); finish_(); return; // TXA
            case 0x98u: s_.a = s_.y; setNZ_(s_.a); finish_(); return; // TYA
            case 0xA8u: s_.y = s_.a; setNZ_(s_.y); finish_(); return; // TAY
            case 0xAAu: s_.x = s_.a; setNZ_(s_.x); finish_(); return; // TAX
            case 0xBAu: s_.x = s_.sp; setNZ_(s_.x); finish_(); return; // TSX
            case 0x9Au: s_.sp = s_.x; finish_(); return; // TXS
            case 0xEAu: finish_(); return; // NOP
            // NOP implied illegals
            case 0x1Au: case 0x3Au: case 0x5Au: case 0x7Au:
            case 0xDAu: case 0xFAu: finish_(); return;
            // Accumulator shifts
            case 0x0Au: s_.a = doASL_(s_.a); finish_(); return; // ASL A
            case 0x2Au: s_.a = doROL_(s_.a); finish_(); return; // ROL A
            case 0x4Au: s_.a = doLSR_(s_.a); finish_(); return; // LSR A
            case 0x6Au: s_.a = doROR_(s_.a); finish_(); return; // ROR A

            // ── PHA / PHP ──────────────────────────────────────────────
            case 0x48u: case 0x08u:
                if (t == 1) { s_.t = 2; return; } // T1: dummy read
                stkPush_(); finish_(); return; // T2: push done (value already in write)

            // ── PLA / PLP ──────────────────────────────────────────────
            case 0x68u:
                if (t == 1) { s_.t = 2; return; }
                if (t == 2) { stkPop_(); s_.t = 3; return; } // increment SP
                s_.a = dataIn; setNZ_(s_.a); finish_(); return;
            case 0x28u: // PLP — I write lands after the boundary poll (1-instr delay)
                if (t == 1) { s_.t = 2; return; }
                if (t == 2) { stkPop_(); s_.t = 3; return; }
                s_.iFlagPollDelayedValue = (s_.p & kFlagI) != 0u;
                s_.iFlagPollDelayActive = true;
                s_.p = static_cast<uint8_t>((dataIn & static_cast<uint8_t>(~kFlagB)) | kFlagUnused);
                finish_(); return;

            // ── RTI ───────────────────────────────────────────────────
            case 0x40u:
                if (t == 1) { s_.t = 2; return; }
                if (t == 2) { stkPop_(); s_.t = 3; return; }
                if (t == 3) {
                    s_.p = static_cast<uint8_t>((dataIn & static_cast<uint8_t>(~kFlagB)) | kFlagUnused);
                    stkPop_(); s_.t = 4; return;
                }
                if (t == 4) { s_.lo = dataIn; stkPop_(); s_.t = 5; return; }
                s_.pc = mk16_(s_.lo, dataIn); finish_(); return;

            // ── RTS ───────────────────────────────────────────────────
            case 0x60u:
                if (t == 1) { s_.t = 2; return; }
                if (t == 2) { stkPop_(); s_.t = 3; return; }
                if (t == 3) { s_.lo = dataIn; stkPop_(); s_.t = 4; return; }
                if (t == 4) { s_.pc = mk16_(s_.lo, dataIn); s_.t = 5; return; }
                // T5: dummy read at PC, then increment PC
                ++s_.pc; finish_(); return;

            // ── JSR ───────────────────────────────────────────────────
            case 0x20u:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { s_.t = 3; return; } // dummy SP read
                if (t == 3) { stkPush_(); s_.t = 4; return; }
                if (t == 4) { stkPush_(); s_.t = 5; return; }
                s_.pc = mk16_(s_.lo, dataIn); finish_(); return;

            // ── JMP abs ───────────────────────────────────────────────
            case 0x4Cu:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                s_.pc = mk16_(s_.lo, dataIn); finish_(); return;

            // ── JMP (ind) ─────────────────────────────────────────────
            case 0x6Cu:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { s_.addr = mk16_(s_.lo, dataIn); ++s_.pc; s_.t = 3; return; }
                if (t == 3) { s_.lo = dataIn; s_.t = 4; return; }
                s_.pc = mk16_(s_.lo, dataIn); finish_(); return;

            // ── Branches ──────────────────────────────────────────────
            case 0x10u: case 0x30u: case 0x50u: case 0x70u:
            case 0x90u: case 0xB0u: case 0xD0u: case 0xF0u:
                commitBranch_(op, dataIn); return;

            // ── Immediate ops ─────────────────────────────────────────
            case 0x09u: s_.a |= dataIn; setNZ_(s_.a); ++s_.pc; finish_(); return; // ORA #
            case 0x29u: s_.a &= dataIn; setNZ_(s_.a); ++s_.pc; finish_(); return; // AND #
            case 0x49u: s_.a ^= dataIn; setNZ_(s_.a); ++s_.pc; finish_(); return; // EOR #
            case 0x69u: doADC_(dataIn); ++s_.pc; finish_(); return; // ADC #
            case 0xE9u: doSBC_(dataIn); ++s_.pc; finish_(); return; // SBC #
            case 0xA9u: s_.a = dataIn; setNZ_(s_.a); ++s_.pc; finish_(); return; // LDA #
            case 0xA2u: s_.x = dataIn; setNZ_(s_.x); ++s_.pc; finish_(); return; // LDX #
            case 0xA0u: s_.y = dataIn; setNZ_(s_.y); ++s_.pc; finish_(); return; // LDY #
            case 0xC9u: doCMP_(s_.a, dataIn); ++s_.pc; finish_(); return; // CMP #
            case 0xC0u: doCMP_(s_.y, dataIn); ++s_.pc; finish_(); return; // CPY #
            case 0xE0u: doCMP_(s_.x, dataIn); ++s_.pc; finish_(); return; // CPX #
            // NOP #imm illegals
            case 0x80u: case 0x82u: case 0x89u: case 0xC2u: case 0xE2u:
                ++s_.pc; finish_(); return;
            // Illegal #imm
            case 0x0Bu: case 0x2Bu: // ANC #
                s_.a &= dataIn; setNZ_(s_.a); setF_(kFlagC, (s_.a & 0x80u) != 0u);
                ++s_.pc; finish_(); return;
            case 0x4Bu: // ALR #
                s_.a &= dataIn; s_.a = doLSR_(s_.a); ++s_.pc; finish_(); return;
            case 0x6Bu: { // ARR # — exact NMOS semantics (value-deterministic).
                // Still counted in the approximate-opcode ledger for the strict
                // RSID policy contract, but the computed result is now exact:
                // t = A & imm; A = (t >> 1) | (C << 7);
                // N = old C; Z from result; C = result bit 6; V = bit6 ^ bit5.
                // Decimal mode applies the documented NMOS nibble fixups.
                const uint8_t t0 = static_cast<uint8_t>(s_.a & dataIn);
                const uint8_t cOld = (s_.p & kFlagC) ? 1u : 0u;
                uint8_t r = static_cast<uint8_t>((t0 >> 1u) | (cOld << 7u));
                setF_(kFlagN, cOld != 0u);
                setF_(kFlagZ, r == 0u);
                setF_(kFlagV, (((t0 ^ r) & 0x40u)) != 0u);
                if (s_.p & kFlagD) {
                    const uint8_t al = static_cast<uint8_t>(t0 & 0x0Fu);
                    const uint8_t ah = static_cast<uint8_t>(t0 >> 4u);
                    if (static_cast<unsigned>(al) + (al & 1u) > 5u) {
                        r = static_cast<uint8_t>((r & 0xF0u) | ((r + 6u) & 0x0Fu));
                    }
                    const bool cOut = static_cast<unsigned>(ah) + (ah & 1u) > 5u;
                    setF_(kFlagC, cOut);
                    if (cOut) r = static_cast<uint8_t>(r + 0x60u);
                } else {
                    setF_(kFlagC, (r & 0x40u) != 0u);
                }
                s_.p = static_cast<uint8_t>(s_.p | kFlagUnused);
                s_.a = r;
                ++s_.pc; finish_(); return;
            }
            case 0x8Bu: // XAA # (approximation)
                s_.a = static_cast<uint8_t>(s_.x & dataIn); setNZ_(s_.a);
                ++s_.pc; finish_(); return;
            case 0xABu: // LAX # (approximation)
                s_.a = s_.x = dataIn; setNZ_(s_.a); ++s_.pc; finish_(); return;
            case 0xCBu: { // AXS #
                const uint16_t d = static_cast<uint16_t>(s_.a & s_.x) - dataIn;
                s_.x = static_cast<uint8_t>(d);
                setF_(kFlagC, d < 0x100u);
                setNZ_(s_.x);
                ++s_.pc; finish_(); return;
            }
            case 0xEBu: doSBC_(dataIn); ++s_.pc; finish_(); return; // USBC # (same as SBC)

            default:
                break;
        }

        // Multi-byte addressing mode commit
        commitMultiByteCycle_(dataIn);
    }

    void commitMultiByteCycle_(uint8_t dataIn) noexcept {
        const uint8_t op = s_.opcode;
        const uint8_t t  = s_.t;
        const uint8_t am = addrModeTag_(op);

        switch (am) {
            case kZP_R:
                if (t == 1) { s_.addr = dataIn; ++s_.pc; s_.t = 2; return; }
                // T2: data read, apply operation
                applyReadOp_(op, dataIn); finish_(); return;
            case kZP_W:
                if (t == 1) { s_.addr = dataIn; ++s_.pc; s_.t = 2; return; }
                finish_(); return; // T2: write done
            case kZP_RMW:
                if (t == 1) { s_.addr = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { s_.tmp = dataIn; s_.tmp2 = applyRmwOp_(op, dataIn); s_.t = 3; return; }
                if (t == 3) { s_.t = 4; return; } // dummy write done
                finish_(); return; // actual write done

            case kZPX_R:
            case kZPX_W:
                if (t == 1) {
                    s_.lo = dataIn; ++s_.pc;
                    s_.addr = static_cast<uint16_t>((s_.lo + s_.x) & 0xFFu);
                    s_.t = 2; return;
                }
                if (t == 2) { s_.t = 3; return; } // dummy read done (addr already set)
                if (am == kZPX_R) { applyReadOp_(op, dataIn); finish_(); return; }
                finish_(); return;
            case kZPX_RMW:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.addr = static_cast<uint16_t>((s_.lo + s_.x) & 0xFFu); s_.t = 2; return; }
                if (t == 2) { s_.t = 3; return; }
                if (t == 3) { s_.tmp = dataIn; s_.tmp2 = applyRmwOp_(op, dataIn); s_.t = 4; return; }
                if (t == 4) { s_.t = 5; return; }
                finish_(); return;

            case kZPY_R:
            case kZPY_W:
                if (t == 1) {
                    s_.lo = dataIn; ++s_.pc;
                    s_.addr = static_cast<uint16_t>((s_.lo + s_.y) & 0xFFu);
                    s_.t = 2; return;
                }
                if (t == 2) { s_.t = 3; return; }
                if (am == kZPY_R) { applyReadOp_(op, dataIn); finish_(); return; }
                finish_(); return;

            case kABS_R:
            case kABS_W:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { s_.addr = mk16_(s_.lo, dataIn); ++s_.pc; s_.t = 3; return; }
                if (am == kABS_R) { applyReadOp_(op, dataIn); finish_(); return; }
                finish_(); return;
            case kABS_RMW:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { s_.addr = mk16_(s_.lo, dataIn); ++s_.pc; s_.t = 3; return; }
                if (t == 3) { s_.tmp = dataIn; s_.tmp2 = applyRmwOp_(op, dataIn); s_.t = 4; return; }
                if (t == 4) { s_.t = 5; return; }
                finish_(); return;

            case kABSX_R:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { computeAbsIdx_(dataIn, s_.x); s_.t = 3; return; }
                if (t == 3 && !s_.pageCross) { applyReadOp_(op, dataIn); finish_(); return; }
                if (t == 3) { s_.t = 4; return; } // dummy done, go to T4
                applyReadOp_(op, dataIn); finish_(); return;
            case kABSX_W:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { computeAbsIdx_(dataIn, s_.x); s_.t = 3; return; }
                if (t == 3) { s_.t = 4; return; }
                finish_(); return;
            case kABSX_RMW:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { computeAbsIdx_(dataIn, s_.x); s_.t = 3; return; }
                if (t == 3) { s_.t = 4; return; }
                if (t == 4) { s_.tmp = dataIn; s_.tmp2 = applyRmwOp_(op, dataIn); s_.t = 5; return; }
                if (t == 5) { s_.t = 6; return; }
                finish_(); return;

            case kABSY_R:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { computeAbsIdx_(dataIn, s_.y); s_.t = 3; return; }
                if (t == 3 && !s_.pageCross) { applyReadOp_(op, dataIn); finish_(); return; }
                if (t == 3) { s_.t = 4; return; }
                applyReadOp_(op, dataIn); finish_(); return;
            case kABSY_W:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { computeAbsIdx_(dataIn, s_.y); s_.t = 3; return; }
                if (t == 3) { s_.t = 4; return; }
                finish_(); return;
            case kABSY_RMW:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { computeAbsIdx_(dataIn, s_.y); s_.t = 3; return; }
                if (t == 3) { s_.t = 4; return; }
                if (t == 4) { s_.tmp = dataIn; s_.tmp2 = applyRmwOp_(op, dataIn); s_.t = 5; return; }
                if (t == 5) { s_.t = 6; return; }
                finish_(); return;

            case kINDX_R:
            case kINDX_W:
                if (t == 1) {
                    s_.lo = dataIn; ++s_.pc;  // ZP base
                    s_.ptr = static_cast<uint16_t>((s_.lo + s_.x) & 0xFFu); // ZP+X pointer
                    s_.t = 2; return;
                }
                if (t == 2) { s_.t = 3; return; } // dummy read
                if (t == 3) { s_.lo = dataIn; s_.t = 4; return; } // ptr_lo at ZP+X
                if (t == 4) { s_.addr = mk16_(s_.lo, dataIn); s_.t = 5; return; }
                if (am == kINDX_R) { applyReadOp_(op, dataIn); finish_(); return; }
                finish_(); return;
            case kINDX_RMW:
                if (t == 1) { s_.lo = dataIn; ++s_.pc; s_.ptr = static_cast<uint16_t>((s_.lo + s_.x) & 0xFFu); s_.t = 2; return; }
                if (t == 2) { s_.t = 3; return; }
                if (t == 3) { s_.lo = dataIn; s_.t = 4; return; }
                if (t == 4) { s_.addr = mk16_(s_.lo, dataIn); s_.t = 5; return; }
                if (t == 5) { s_.tmp = dataIn; s_.tmp2 = applyRmwOp_(op, dataIn); s_.t = 6; return; }
                if (t == 6) { s_.t = 7; return; }
                finish_(); return;

            case kINDY_R:
                if (t == 1) {
                    s_.ptr = dataIn; ++s_.pc; // ZP pointer address
                    s_.t = 2; return;
                }
                if (t == 2) { s_.lo = dataIn; s_.t = 3; return; } // ptr_lo
                if (t == 3) {
                    s_.hi = dataIn; // ptr_hi
                    const uint16_t base = mk16_(s_.lo, s_.hi);
                    const uint16_t eff = static_cast<uint16_t>(base + s_.y);
                    s_.pageCross = ((base ^ eff) & 0xFF00u) != 0u;
                    s_.addr = eff;
                    s_.t = 4; return;
                }
                if (t == 4 && !s_.pageCross) { applyReadOp_(op, dataIn); finish_(); return; }
                if (t == 4) { s_.t = 5; return; }
                applyReadOp_(op, dataIn); finish_(); return;
            case kINDY_W:
                if (t == 1) { s_.ptr = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { s_.lo = dataIn; s_.t = 3; return; }
                if (t == 3) {
                    s_.hi = dataIn;
                    const uint16_t base = mk16_(s_.lo, s_.hi);
                    s_.addr = static_cast<uint16_t>(base + s_.y);
                    s_.t = 4; return;
                }
                if (t == 4) { s_.t = 5; return; }
                finish_(); return;
            case kINDY_RMW:
                if (t == 1) { s_.ptr = dataIn; ++s_.pc; s_.t = 2; return; }
                if (t == 2) { s_.lo = dataIn; s_.t = 3; return; }
                if (t == 3) {
                    s_.hi = dataIn;
                    const uint16_t base = mk16_(s_.lo, s_.hi);
                    s_.addr = static_cast<uint16_t>(base + s_.y);
                    s_.t = 4; return;
                }
                if (t == 4) { s_.t = 5; return; }
                if (t == 5) { s_.tmp = dataIn; s_.tmp2 = applyRmwOp_(op, dataIn); s_.t = 6; return; }
                if (t == 6) { s_.t = 7; return; }
                finish_(); return;

            default:
                finish_(); return;
        }
    }

    // ── Branch commit ─────────────────────────────────────────────────────
    void commitBranch_(uint8_t op, uint8_t dataIn) noexcept {
        if (s_.t == 1) {
            ++s_.pc; // post-displacement PC
            bool taken = false;
            switch (op) {
                case 0x10u: taken = (s_.p & kFlagN) == 0u; break; // BPL
                case 0x30u: taken = (s_.p & kFlagN) != 0u; break; // BMI
                case 0x50u: taken = (s_.p & kFlagV) == 0u; break; // BVC
                case 0x70u: taken = (s_.p & kFlagV) != 0u; break; // BVS
                case 0x90u: taken = (s_.p & kFlagC) == 0u; break; // BCC
                case 0xB0u: taken = (s_.p & kFlagC) != 0u; break; // BCS
                case 0xD0u: taken = (s_.p & kFlagZ) == 0u; break; // BNE
                case 0xF0u: taken = (s_.p & kFlagZ) != 0u; break; // BEQ
                default: break;
            }
            if (!taken) { finish_(); return; } // 2-cycle not-taken

            const int16_t rel = static_cast<int16_t>(static_cast<int8_t>(dataIn));
            const uint16_t newPc = static_cast<uint16_t>(s_.pc + rel);
            s_.addr = newPc; // final target
            // Wrong-page dummy address for the T3 fix-up cycle: high byte from the
            // post-displacement PC, low byte from the target.
            s_.ptr = static_cast<uint16_t>((s_.pc & 0xFF00u) | (newPc & 0x00FFu));
            s_.pageCross = ((s_.pc ^ newPc) & 0xFF00u) != 0u;
            // PC stays at the post-operand value through T2 so the T2 dummy read
            // is issued at the next-instruction address, as on hardware.
            s_.t = 2; return;
        }
        if (s_.t == 2) {
            // T2 dummy read (at post-operand PC) has just completed.
            // For no-cross: 3 cycles total — load the target PC now; done.
            // For page-cross: 4 cycles — go to T3 dummy read at wrong-page addr.
            if (!s_.pageCross) { s_.pc = s_.addr; finish_(); return; }
            s_.t = 3; return;
        }
        // T3: page-cross wrong-page dummy read (at s_.ptr) done — fix PCH.
        s_.pc = s_.addr; // load correct target page PC
        finish_(); return;
    }

    // ── Read operation dispatch ───────────────────────────────────────────
    void applyReadOp_(uint8_t op, uint8_t dataIn) noexcept {
        switch (op) {
            // LDA
            case 0xA5u: case 0xB5u: case 0xADu: case 0xBDu: case 0xB9u:
            case 0xA1u: case 0xB1u:
                s_.a = dataIn; setNZ_(s_.a); return;
            // LDX
            case 0xA6u: case 0xB6u: case 0xAEu: case 0xBEu:
                s_.x = dataIn; setNZ_(s_.x); return;
            // LDY
            case 0xA4u: case 0xB4u: case 0xACu: case 0xBCu:
                s_.y = dataIn; setNZ_(s_.y); return;
            // ORA
            case 0x05u: case 0x15u: case 0x0Du: case 0x1Du: case 0x19u:
            case 0x01u: case 0x11u:
                s_.a |= dataIn; setNZ_(s_.a); return;
            // AND
            case 0x25u: case 0x35u: case 0x2Du: case 0x3Du: case 0x39u:
            case 0x21u: case 0x31u:
                s_.a &= dataIn; setNZ_(s_.a); return;
            // EOR
            case 0x45u: case 0x55u: case 0x4Du: case 0x5Du: case 0x59u:
            case 0x41u: case 0x51u:
                s_.a ^= dataIn; setNZ_(s_.a); return;
            // ADC
            case 0x65u: case 0x75u: case 0x6Du: case 0x7Du: case 0x79u:
            case 0x61u: case 0x71u:
                doADC_(dataIn); return;
            // SBC
            case 0xE5u: case 0xF5u: case 0xEDu: case 0xFDu: case 0xF9u:
            case 0xE1u: case 0xF1u:
                doSBC_(dataIn); return;
            // CMP
            case 0xC5u: case 0xD5u: case 0xCDu: case 0xDDu: case 0xD9u:
            case 0xC1u: case 0xD1u:
                doCMP_(s_.a, dataIn); return;
            // CPX
            case 0xE4u: case 0xECu:
                doCMP_(s_.x, dataIn); return;
            // CPY
            case 0xC4u: case 0xCCu:
                doCMP_(s_.y, dataIn); return;
            // BIT
            case 0x24u: case 0x2Cu:
                doBIT_(dataIn); return;
            // NOP zp / NOP abs / NOP abs,X read-and-discard
            case 0x04u: case 0x0Cu: case 0x14u: case 0x1Cu: case 0x34u:
            case 0x3Cu: case 0x44u: case 0x54u: case 0x5Cu: case 0x64u:
            case 0x74u: case 0x7Cu: case 0xD4u: case 0xDCu: case 0xF4u:
            case 0xFCu:
                return; // discard
            // LAX (illegal LDA+LDX)
            case 0xA7u: case 0xB7u: case 0xAFu: case 0xBFu:
            case 0xA3u: case 0xB3u:
                s_.a = s_.x = dataIn; setNZ_(s_.a); return;
            // LAS abs,Y (0xBBu): A = X = SP = mem & SP
            case 0xBBu:
                s_.a = s_.x = s_.sp = static_cast<uint8_t>(dataIn & s_.sp); setNZ_(s_.a); return;
            default: return;
        }
    }

    // ── RMW operation dispatch ────────────────────────────────────────────
    uint8_t applyRmwOp_(uint8_t op, uint8_t dataIn) noexcept {
        switch (op) {
            case 0x06u: case 0x16u: case 0x0Eu: case 0x1Eu: // ASL
                return doASL_(dataIn);
            case 0x26u: case 0x36u: case 0x2Eu: case 0x3Eu: // ROL
                return doROL_(dataIn);
            case 0x46u: case 0x56u: case 0x4Eu: case 0x5Eu: // LSR
                return doLSR_(dataIn);
            case 0x66u: case 0x76u: case 0x6Eu: case 0x7Eu: // ROR
                return doROR_(dataIn);
            case 0xC6u: case 0xD6u: case 0xCEu: case 0xDEu: // DEC
                return doDEC_(dataIn);
            case 0xE6u: case 0xF6u: case 0xEEu: case 0xFEu: // INC
                return doINC_(dataIn);
            // SLO (ASL then ORA): RMW returns ASL result, ORA applied as side-effect
            case 0x07u: case 0x17u: case 0x0Fu: case 0x1Fu:
            case 0x1Bu: case 0x03u: case 0x13u: {
                const uint8_t v = doASL_(dataIn); s_.a |= v; return v; }
            // RLA (ROL then AND)
            case 0x27u: case 0x37u: case 0x2Fu: case 0x3Fu:
            case 0x3Bu: case 0x23u: case 0x33u: {
                const uint8_t v = doROL_(dataIn); s_.a &= v; setNZ_(s_.a); return v; }
            // SRE (LSR then EOR)
            case 0x47u: case 0x57u: case 0x4Fu: case 0x5Fu:
            case 0x5Bu: case 0x43u: case 0x53u: {
                const uint8_t v = doLSR_(dataIn); s_.a ^= v; setNZ_(s_.a); return v; }
            // RRA (ROR then ADC)
            case 0x67u: case 0x77u: case 0x6Fu: case 0x7Fu:
            case 0x7Bu: case 0x63u: case 0x73u: {
                const uint8_t v = doROR_(dataIn); doADC_(v); return v; }
            // DCP (DEC then CMP)
            case 0xC7u: case 0xD7u: case 0xCFu: case 0xDFu:
            case 0xDBu: case 0xC3u: case 0xD3u: {
                const uint8_t v = doDEC_(dataIn); doCMP_(s_.a, v); return v; }
            // ISC (INC then SBC)
            case 0xE7u: case 0xF7u: case 0xEFu: case 0xFFu:
            case 0xFBu: case 0xE3u: case 0xF3u: {
                const uint8_t v = doINC_(dataIn); doSBC_(v); return v; }
            default: return dataIn;
        }
    }

    // ── Write data helpers (what value to write for write-mode opcodes) ───
    uint8_t zpWriteData_(uint8_t op) noexcept { return writeDataFor_(op); }
    uint8_t zpxWriteData_(uint8_t op) noexcept { return writeDataFor_(op); }
    uint8_t zpyWriteData_(uint8_t op) noexcept { return writeDataFor_(op); }
    uint8_t absWriteData_(uint8_t op) noexcept { return writeDataFor_(op); }
    uint8_t absxWriteData_(uint8_t op) noexcept { return writeDataFor_(op); }
    uint8_t absyWriteData_(uint8_t op) noexcept { return writeDataFor_(op); }
    uint8_t indxWriteData_(uint8_t op) noexcept { return writeDataFor_(op); }
    uint8_t indyWriteData_(uint8_t op) noexcept { return writeDataFor_(op); }
    uint8_t writeDataFor_(uint8_t op) noexcept {
        switch (op) {
            case 0x85u: case 0x95u: case 0x8Du: case 0x9Du: case 0x99u:
            case 0x81u: case 0x91u: return s_.a; // STA
            case 0x86u: case 0x96u: case 0x8Eu: return s_.x; // STX
            case 0x84u: case 0x94u: case 0x8Cu: return s_.y; // STY
            case 0x87u: case 0x97u: case 0x8Fu: case 0x83u:  // SAX (A&X)
                return static_cast<uint8_t>(s_.a & s_.x);
            case 0x93u: { // AHX (ind),Y: A&X&(hi+1)
                const uint8_t addrHi = static_cast<uint8_t>(s_.addr >> 8u);
                return static_cast<uint8_t>(s_.a & s_.x & static_cast<uint8_t>(addrHi + 1u));
            }
            case 0x9Fu: { // AHX abs,Y: same
                const uint8_t addrHi = static_cast<uint8_t>(s_.addr >> 8u);
                return static_cast<uint8_t>(s_.a & s_.x & static_cast<uint8_t>(addrHi + 1u));
            }
            case 0x9Cu: { // SHY abs,X: Y&(addrHi+1)
                const uint8_t addrHi = static_cast<uint8_t>(s_.addr >> 8u);
                return static_cast<uint8_t>(s_.y & static_cast<uint8_t>(addrHi + 1u));
            }
            case 0x9Eu: { // SHX abs,Y: X&(addrHi+1)
                const uint8_t addrHi = static_cast<uint8_t>(s_.addr >> 8u);
                return static_cast<uint8_t>(s_.x & static_cast<uint8_t>(addrHi + 1u));
            }
            case 0x9Bu: { // TAS abs,Y: SP=A&X, write SP&(addrHi+1)
                s_.sp = static_cast<uint8_t>(s_.a & s_.x);
                const uint8_t addrHi = static_cast<uint8_t>(s_.addr >> 8u);
                return static_cast<uint8_t>(s_.sp & static_cast<uint8_t>(addrHi + 1u));
            }
            default: return 0xFFu;
        }
    }

    // ── Addressing mode tags ──────────────────────────────────────────────
    enum AddrModeTag : uint8_t {
        kUnknown = 0,
        kZP_R, kZP_W, kZP_RMW,
        kZPX_R, kZPX_W, kZPX_RMW,
        kZPY_R, kZPY_W,
        kABS_R, kABS_W, kABS_RMW,
        kABSX_R, kABSX_W, kABSX_RMW,
        kABSY_R, kABSY_W, kABSY_RMW,
        kINDX_R, kINDX_W, kINDX_RMW,
        kINDY_R, kINDY_W, kINDY_RMW,
    };

    static uint8_t addrModeTag_(uint8_t op) noexcept {
        switch (op) {
            // ZP read
            case 0xA5u: case 0xA6u: case 0xA4u: // LDA LDX LDY
            case 0x24u: case 0x05u: case 0x25u: // BIT ORA AND
            case 0x45u: case 0x65u: case 0x85u: // EOR ADC ... wait STA is write
            // Let me redo this more carefully...
                break;
            default: break;
        }
        // Use a complete table
        switch (op) {
            // ── ZP read ─────────────────────────────────────────────────
            case 0xA5u: // LDA zp
            case 0xA6u: // LDX zp
            case 0xA4u: // LDY zp
            case 0x24u: // BIT zp
            case 0x05u: // ORA zp
            case 0x25u: // AND zp
            case 0x45u: // EOR zp
            case 0x65u: // ADC zp
            case 0xE5u: // SBC zp
            case 0xC5u: // CMP zp
            case 0xE4u: // CPX zp
            case 0xC4u: // CPY zp
            // NOP zp illegals (read-discard)
            case 0x04u: case 0x44u: case 0x64u:
            // LAX zp
            case 0xA7u:
                return kZP_R;
            // ── ZP write ────────────────────────────────────────────────
            case 0x85u: // STA zp
            case 0x86u: // STX zp
            case 0x84u: // STY zp
            case 0x87u: // SAX zp
                return kZP_W;
            // ── ZP RMW ──────────────────────────────────────────────────
            case 0x06u: // ASL zp
            case 0x26u: // ROL zp
            case 0x46u: // LSR zp
            case 0x66u: // ROR zp
            case 0xC6u: // DEC zp
            case 0xE6u: // INC zp
            case 0x07u: // SLO zp
            case 0x27u: // RLA zp
            case 0x47u: // SRE zp
            case 0x67u: // RRA zp
            case 0xC7u: // DCP zp
            case 0xE7u: // ISC zp
                return kZP_RMW;
            // ── ZP,X read ───────────────────────────────────────────────
            case 0xB5u: // LDA zp,X
            case 0xB4u: // LDY zp,X
            case 0x15u: // ORA zp,X
            case 0x35u: // AND zp,X
            case 0x55u: // EOR zp,X
            case 0x75u: // ADC zp,X
            case 0xF5u: // SBC zp,X
            case 0xD5u: // CMP zp,X
            // NOP zp,X illegals
            case 0x14u: case 0x34u: case 0x54u: case 0x74u: case 0xD4u: case 0xF4u:
                return kZPX_R;
            // ── ZP,X write ──────────────────────────────────────────────
            case 0x95u: // STA zp,X
            case 0x94u: // STY zp,X
                return kZPX_W;
            // ── ZP,X RMW ────────────────────────────────────────────────
            case 0x16u: // ASL zp,X
            case 0x36u: // ROL zp,X
            case 0x56u: // LSR zp,X
            case 0x76u: // ROR zp,X
            case 0xD6u: // DEC zp,X
            case 0xF6u: // INC zp,X
            case 0x17u: // SLO zp,X
            case 0x37u: // RLA zp,X
            case 0x57u: // SRE zp,X
            case 0x77u: // RRA zp,X
            case 0xD7u: // DCP zp,X
            case 0xF7u: // ISC zp,X
                return kZPX_RMW;
            // ── ZP,Y read ───────────────────────────────────────────────
            case 0xB6u: // LDX zp,Y
            case 0xB7u: // LAX zp,Y
                return kZPY_R;
            // ── ZP,Y write ──────────────────────────────────────────────
            case 0x96u: // STX zp,Y
            case 0x97u: // SAX zp,Y
                return kZPY_W;
            // ── ABS read ────────────────────────────────────────────────
            case 0xADu: // LDA abs
            case 0xAEu: // LDX abs
            case 0xACu: // LDY abs
            case 0x2Cu: // BIT abs
            case 0x0Du: // ORA abs
            case 0x2Du: // AND abs
            case 0x4Du: // EOR abs
            case 0x6Du: // ADC abs
            case 0xEDu: // SBC abs
            case 0xCDu: // CMP abs
            case 0xECu: // CPX abs
            case 0xCCu: // CPY abs
            case 0x0Cu: // NOP abs
            case 0xAFu: // LAX abs
                return kABS_R;
            // ── ABS write ───────────────────────────────────────────────
            case 0x8Du: // STA abs
            case 0x8Eu: // STX abs
            case 0x8Cu: // STY abs
            case 0x8Fu: // SAX abs
                return kABS_W;
            // ── ABS RMW ─────────────────────────────────────────────────
            case 0x0Eu: // ASL abs
            case 0x2Eu: // ROL abs
            case 0x4Eu: // LSR abs
            case 0x6Eu: // ROR abs
            case 0xCEu: // DEC abs
            case 0xEEu: // INC abs
            case 0x0Fu: // SLO abs
            case 0x2Fu: // RLA abs
            case 0x4Fu: // SRE abs
            case 0x6Fu: // RRA abs
            case 0xCFu: // DCP abs
            case 0xEFu: // ISC abs
                return kABS_RMW;
            // ── ABS,X read ──────────────────────────────────────────────
            case 0xBDu: // LDA abs,X
            case 0xBCu: // LDY abs,X
            case 0x1Du: // ORA abs,X
            case 0x3Du: // AND abs,X
            case 0x5Du: // EOR abs,X
            case 0x7Du: // ADC abs,X
            case 0xFDu: // SBC abs,X
            case 0xDDu: // CMP abs,X
            case 0x1Cu: case 0x3Cu: case 0x5Cu: case 0x7Cu: // NOP abs,X
            case 0xDCu: case 0xFCu:
                return kABSX_R;
            // ── ABS,X write ─────────────────────────────────────────────
            case 0x9Du: // STA abs,X
                return kABSX_W;
            // ── ABS,X RMW ───────────────────────────────────────────────
            case 0x1Eu: // ASL abs,X
            case 0x3Eu: // ROL abs,X
            case 0x5Eu: // LSR abs,X
            case 0x7Eu: // ROR abs,X
            case 0xDEu: // DEC abs,X
            case 0xFEu: // INC abs,X
            case 0x1Fu: // SLO abs,X
            case 0x3Fu: // RLA abs,X
            case 0x5Fu: // SRE abs,X
            case 0x7Fu: // RRA abs,X
            case 0xDFu: // DCP abs,X
            case 0xFFu: // ISC abs,X
                return kABSX_RMW;
            // ── ABS,Y read ──────────────────────────────────────────────
            case 0xB9u: // LDA abs,Y
            case 0xBEu: // LDX abs,Y
            case 0x19u: // ORA abs,Y
            case 0x39u: // AND abs,Y
            case 0x59u: // EOR abs,Y
            case 0x79u: // ADC abs,Y
            case 0xF9u: // SBC abs,Y
            case 0xD9u: // CMP abs,Y
            case 0xBFu: // LAX abs,Y
            case 0xBBu: // LAS abs,Y
                return kABSY_R;
            // ── ABS,Y write ─────────────────────────────────────────────
            case 0x99u: // STA abs,Y
            case 0x9Fu: // AHX abs,Y
            case 0x9Eu: // SHX abs,Y
            case 0x9Bu: // TAS abs,Y
                return kABSY_W;
            // ── ABS,Y RMW ───────────────────────────────────────────────
            case 0x1Bu: // SLO abs,Y
            case 0x3Bu: // RLA abs,Y
            case 0x5Bu: // SRE abs,Y
            case 0x7Bu: // RRA abs,Y
            case 0xDBu: // DCP abs,Y
            case 0xFBu: // ISC abs,Y
                return kABSY_RMW;
            // ── (ind,X) read ────────────────────────────────────────────
            case 0xA1u: // LDA (ind,X)
            case 0x01u: // ORA (ind,X)
            case 0x21u: // AND (ind,X)
            case 0x41u: // EOR (ind,X)
            case 0x61u: // ADC (ind,X)
            case 0xE1u: // SBC (ind,X)
            case 0xC1u: // CMP (ind,X)
            case 0xA3u: // LAX (ind,X)
                return kINDX_R;
            // ── (ind,X) write ───────────────────────────────────────────
            case 0x81u: // STA (ind,X)
            case 0x83u: // SAX (ind,X)
                return kINDX_W;
            // ── (ind,X) RMW ─────────────────────────────────────────────
            case 0x03u: // SLO (ind,X)
            case 0x23u: // RLA (ind,X)
            case 0x43u: // SRE (ind,X)
            case 0x63u: // RRA (ind,X)
            case 0xC3u: // DCP (ind,X)
            case 0xE3u: // ISC (ind,X)
                return kINDX_RMW;
            // ── (ind),Y read ────────────────────────────────────────────
            case 0xB1u: // LDA (ind),Y
            case 0x11u: // ORA (ind),Y
            case 0x31u: // AND (ind),Y
            case 0x51u: // EOR (ind),Y
            case 0x71u: // ADC (ind),Y
            case 0xF1u: // SBC (ind),Y
            case 0xD1u: // CMP (ind),Y
            case 0xB3u: // LAX (ind),Y
                return kINDY_R;
            // ── (ind),Y write ───────────────────────────────────────────
            case 0x91u: // STA (ind),Y
            case 0x93u: // AHX (ind),Y
                return kINDY_W;
            // ── (ind),Y RMW ─────────────────────────────────────────────
            case 0x13u: // SLO (ind),Y
            case 0x33u: // RLA (ind),Y
            case 0x53u: // SRE (ind),Y
            case 0x73u: // RRA (ind),Y
            case 0xD3u: // DCP (ind),Y
            case 0xF3u: // ISC (ind),Y
                return kINDY_RMW;
            default: return kUnknown;
        }
    }

    // ── Reset and interrupt cycles ────────────────────────────────────────
    CpuBusRequest emitResetCycle_() const noexcept {
        switch (s_.t) {
            case 0: case 1: case 2:
                return dummyRd_(s_.pc);
            case 3: case 4: case 5:
                return dummyRd_(static_cast<uint16_t>(0x0100u | s_.sp));
            case 6:
                return req_(CpuAccessKind::VectorReadLow, kVectorResetLo);
            default:
                return req_(CpuAccessKind::VectorReadHigh, kVectorResetHi);
        }
    }
    void commitResetCycle_(uint8_t dataIn) noexcept {
        // Cycles T3..T5 are the three "suppressed-push" stack cycles of the
        // 6502 reset sequence: the address bus uses the current SP (read in
        // emitResetCycle_) and the stack pointer is decremented each cycle (the
        // writes are suppressed during reset). Net effect: SP -= 3 across reset,
        // matching real 6502/6510 hardware (software then typically does TXS).
        if (s_.t >= 3u && s_.t <= 5u) s_.sp = static_cast<uint8_t>(s_.sp - 1u);
        if (s_.t == 6) { s_.lo = dataIn; s_.t = 7; return; }
        if (s_.t == 7) {
            s_.pc = mk16_(s_.lo, dataIn);
            s_.p = static_cast<uint8_t>((s_.p | kFlagI | kFlagUnused) & static_cast<uint8_t>(~kFlagB));
            s_.resetSequence = false; s_.active = false; s_.t = 0; return;
        }
        ++s_.t;
    }

    CpuBusRequest emitIntCycle_(uint16_t vecLo, uint16_t vecHi) const noexcept {
        switch (s_.t) {
            case 0: return dummyRd_(s_.pc);
            case 1: return dummyRd_(s_.pc); // BRK: reads next byte here (discarded)
            case 2: return stkWr_(static_cast<uint8_t>(s_.pc >> 8u));
            case 3: return stkWr_(static_cast<uint8_t>(s_.pc & 0xFFu));
            case 4: {
                uint8_t pushed = static_cast<uint8_t>(s_.p | kFlagUnused);
                if (s_.brkSequence) pushed = static_cast<uint8_t>(pushed | kFlagB);
                else pushed = static_cast<uint8_t>(pushed & static_cast<uint8_t>(~kFlagB));
                return stkWr_(pushed);
            }
            case 5: { auto r = req_(CpuAccessKind::VectorReadLow, vecLo); r.vectorFetch = true; return r; }
            default: { auto r = req_(CpuAccessKind::VectorReadHigh, vecHi); r.vectorFetch = true; return r; }
        }
    }
    void commitIntCycle_(uint8_t dataIn) noexcept {
        if (s_.brkSequence && s_.t == 1) ++s_.pc; // BRK skips the padding byte
        if (s_.t == 2 || s_.t == 3 || s_.t == 4) stkPush_();
        if (s_.t == 5) { s_.lo = dataIn; s_.t = 6; return; }
        if (s_.t == 6) {
            s_.pc = mk16_(s_.lo, dataIn);
            s_.p = static_cast<uint8_t>((s_.p | kFlagI | kFlagUnused) & static_cast<uint8_t>(~kFlagB));
            s_.irqSequence = false; s_.nmiSequence = false; s_.brkSequence = false;
            ++retiredInstructionCount_;
            s_.active = false; s_.t = 0; return;
        }
        ++s_.t;
    }
};

} // namespace ArpSID::C64
