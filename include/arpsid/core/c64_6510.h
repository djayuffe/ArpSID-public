#pragma once

#include "arpsid/core/c64_bus.h"
#include <cstdint>

namespace ArpSID::C64 {

static constexpr uint16_t kC64HaltAddr = 0xFFFFu;

inline bool c64Mos6510OpcodeIsReadModifyWrite(uint8_t opcode) noexcept {
    const uint8_t lo = static_cast<uint8_t>(opcode & 0x0Fu);
    const uint8_t hi = static_cast<uint8_t>(opcode & 0xF0u);
    // Official RMW: ASL/ROL/LSR/ROR/DEC/INC memory forms.
    if ((lo == 0x06u || lo == 0x0Eu) &&
        (hi == 0x00u || hi == 0x10u || hi == 0x20u || hi == 0x30u ||
         hi == 0x40u || hi == 0x50u || hi == 0x60u || hi == 0x70u ||
         hi == 0xC0u || hi == 0xD0u || hi == 0xE0u || hi == 0xF0u)) return true;
    // Stable NMOS illegal RMW composites: SLO/RLA/SRE/RRA/DCP/ISC.
    return (lo == 0x03u || lo == 0x07u || lo == 0x0Fu ||
            lo == 0x13u || lo == 0x17u || lo == 0x1Bu || lo == 0x1Fu) &&
           (hi == 0x00u || hi == 0x10u || hi == 0x20u || hi == 0x30u ||
            hi == 0x40u || hi == 0x50u || hi == 0x60u || hi == 0x70u ||
            hi == 0xC0u || hi == 0xD0u || hi == 0xE0u || hi == 0xF0u);
}

inline constexpr bool c64Mos6510OpcodeIsKilJam(uint8_t opcode) noexcept {
    switch (opcode) {
    case 0x02u: case 0x12u: case 0x22u: case 0x32u:
    case 0x42u: case 0x52u: case 0x62u: case 0x72u:
    case 0x92u: case 0xB2u: case 0xD2u: case 0xF2u:
        return true;
    default:
        return false;
    }
}

inline constexpr bool c64Mos6510OpcodeIsOfficial(uint8_t opcode) noexcept {
    switch (opcode) {
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
        return true;
    default:
        return false;
    }
}

inline constexpr bool c64Mos6510OpcodeHasPromotedMicroSequence(uint8_t opcode) noexcept {
    if (c64Mos6510OpcodeIsOfficial(opcode) || c64Mos6510OpcodeIsKilJam(opcode)) {
        return true;
    }

    switch (opcode) {
    // Unofficial NOP variants (zp, zp,X, abs, abs,X) used by many SID players.
    // These are implemented in Cpu6510Micro with their correct addressing-mode
    // cycle sequences (read and discard).
    case 0x04u: case 0x14u: case 0x34u: case 0x44u: case 0x54u:
    case 0x64u: case 0x74u: case 0xD4u: case 0xF4u: // NOP zp / NOP zp,X
    case 0x0Cu: case 0x1Cu: case 0x3Cu: case 0x5Cu:
    case 0x7Cu: case 0xDCu: case 0xFCu:             // NOP abs / NOP abs,X
    case 0x1Au: case 0x3Au: case 0x5Au: case 0x7Au:
    case 0xDAu: case 0xFAu:                          // NOP implied (extra 2-cycle NOPs)
    case 0x03u: case 0x07u: case 0x0Bu: case 0x0Fu:
    case 0x13u: case 0x17u: case 0x1Bu: case 0x1Fu:
    case 0x23u: case 0x27u: case 0x2Bu: case 0x2Fu:
    case 0x33u: case 0x37u: case 0x3Bu: case 0x3Fu:
    case 0x43u: case 0x47u: case 0x4Bu: case 0x4Fu:
    case 0x53u: case 0x57u: case 0x5Bu: case 0x5Fu:
    case 0x63u: case 0x67u: case 0x6Bu: case 0x6Fu:
    case 0x73u: case 0x77u: case 0x7Bu: case 0x7Fu:
    case 0x80u: case 0x82u: case 0x83u: case 0x87u:
    case 0x89u: case 0x8Bu: case 0x8Fu: case 0x93u:
    case 0xC2u: case 0xE2u:                          // NOP #imm (SKB) illegals

    case 0x97u: case 0x9Bu: case 0x9Cu: case 0x9Eu:
    case 0x9Fu: case 0xA3u: case 0xA7u: case 0xABu:
    case 0xAFu: case 0xB3u: case 0xB7u: case 0xBBu:
    case 0xBFu: case 0xC3u: case 0xC7u: case 0xCBu:
    case 0xCFu: case 0xD3u: case 0xD7u: case 0xDBu:
    case 0xDFu: case 0xE3u: case 0xE7u: case 0xEBu:
    case 0xEFu: case 0xF3u: case 0xF7u: case 0xFBu:
    case 0xFFu:
        return true;
    default:
        return false;
    }
}

inline constexpr bool c64Mos6510AllKilOpcodesHavePromotedMicroSequence() noexcept {
    for (uint16_t opcode = 0; opcode < 256u; ++opcode) {
        const auto op = static_cast<uint8_t>(opcode);
        if (c64Mos6510OpcodeIsKilJam(op) && !c64Mos6510OpcodeHasPromotedMicroSequence(op)) {
            return false;
        }
    }
    return true;
}

inline constexpr bool c64Mos6510AllOfficialOpcodesHavePromotedMicroSequence() noexcept {
    for (uint16_t opcode = 0; opcode < 256u; ++opcode) {
        const auto op = static_cast<uint8_t>(opcode);
        if (c64Mos6510OpcodeIsOfficial(op) && !c64Mos6510OpcodeHasPromotedMicroSequence(op)) {
            return false;
        }
    }
    return true;
}

inline constexpr bool c64Mos6510NoOfficialOpcodeNeedsSemanticFallback() noexcept {
    return c64Mos6510AllOfficialOpcodesHavePromotedMicroSequence();
}

static_assert(c64Mos6510AllKilOpcodesHavePromotedMicroSequence(),
              "All NMOS 6510 KIL/JAM opcodes must be promoted.");
static_assert(c64Mos6510NoOfficialOpcodeNeedsSemanticFallback(),
              "Official 6510 opcodes must not require semantic fallback.");
// Spot-checks that the opcode table is consistent after editing.
// If any of these fail, a case was accidentally removed or duplicated.
static_assert(c64Mos6510OpcodeIsOfficial(0x10u), "BPL must be official");
static_assert(c64Mos6510OpcodeIsOfficial(0x16u), "ASL zp,X must be official");
static_assert(c64Mos6510OpcodeIsOfficial(0x1Eu), "ASL abs,X must be official");
static_assert(!c64Mos6510OpcodeIsOfficial(0x04u), "NOP zp (illegal) must NOT be official");
static_assert(!c64Mos6510OpcodeIsKilJam(0x16u), "ASL zp,X must not be KIL");
// C2/E2 are NOP #imm illegals implemented by Cpu6510Micro; the opcode policy
// must agree so strict-RSID coverage does not flag them as unsupported.
static_assert(c64Mos6510OpcodeHasPromotedMicroSequence(0xC2u), "NOP #imm (0xC2) must be promoted");
static_assert(c64Mos6510OpcodeHasPromotedMicroSequence(0xE2u), "NOP #imm (0xE2) must be promoted");
static_assert(!c64Mos6510OpcodeIsOfficial(0xC2u), "0xC2 must NOT be classed official");
static_assert(!c64Mos6510OpcodeIsOfficial(0xE2u), "0xE2 must NOT be classed official");

struct Mos6510State {
    uint16_t pc = 0;
    uint8_t a = 0;
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t sp = 0xFF;
    uint8_t p = 0x24;
    uint8_t portDirection = 0x2F;
    uint8_t portData = 0x37;
    uint64_t phi2Cycle = 0;
    bool jammed = false;
    bool irqLine = false;
    bool nmiLine = false;
    bool nmiPending = false;
    bool trapBrkAsJam = true; // PSID fast-call compatibility; false = real BRK IRQ service
    bool lastTickUsedSemanticFallback = false;
    bool lastSemanticFallbackWasOfficial = false;
    uint8_t lastSemanticFallbackOpcode = 0;
    uint64_t semanticFallbackCount = 0;
    uint64_t officialSemanticFallbackCount = 0;
    bool lastTickUsedApproximateIllegal = false;
    uint8_t lastApproximateIllegalOpcode = 0;
    uint64_t approximateIllegalOpcodeCount = 0;
};

class Mos6510Bus {
public:
    virtual ~Mos6510Bus() = default;
    virtual uint8_t cpuRead(uint16_t address) noexcept = 0;
    virtual void cpuWrite(uint16_t address, uint8_t value) noexcept = 0;
};

class Mos6510 {
public:
    static constexpr uint8_t C = 0x01u;
    static constexpr uint8_t Z = 0x02u;
    static constexpr uint8_t I = 0x04u;
    static constexpr uint8_t D = 0x08u;
    static constexpr uint8_t B = 0x10u;
    static constexpr uint8_t U = 0x20u;
    static constexpr uint8_t V = 0x40u;
    static constexpr uint8_t N = 0x80u;

    void reset(uint16_t resetVector = 0xFCE2) noexcept {
        state_ = {};
        state_.pc = resetVector;
        state_.sp = 0xFF;
        state_.p = uint8_t(I | U);
        state_.portDirection = 0x2F;
        state_.portData = 0x37;
    }

    const Mos6510State& state() const noexcept { return state_; }
    Mos6510State& state() noexcept { return state_; }

    BusCycle makeWrite(uint16_t address, uint8_t data, uint64_t phi2Offset = 0) const noexcept {
        return BusCycle{address, data, false, state_.phi2Cycle + phi2Offset, 0};
    }
    BusCycle makeRead(uint16_t address, uint64_t phi2Offset = 0) const noexcept {
        return BusCycle{address, 0, true, state_.phi2Cycle + phi2Offset, 0};
    }
    void advance(uint64_t cycles) noexcept { state_.phi2Cycle += cycles; }

    void irq(bool asserted) noexcept { state_.irqLine = asserted; }
    void nmi(bool asserted) noexcept {
        const bool before = state_.nmiLine;
        state_.nmiLine = asserted;
        if (asserted && !before) state_.nmiPending = true;
    }
    bool irqLine() const noexcept { return state_.irqLine; }
    bool nmiLine() const noexcept { return state_.nmiLine; }
    bool nmiPending() const noexcept { return state_.nmiPending; }
    void setTrapBrkAsJam(bool enable) noexcept { state_.trapBrkAsJam = enable; }
    bool trapBrkAsJam() const noexcept { return state_.trapBrkAsJam; }

    uint8_t stepInstruction(Mos6510Bus& bus) noexcept {
        return stepInstructionWithOpcodeHint(bus, 0, false);
    }

    uint8_t tickWithOpcodeHint(Mos6510Bus& bus, uint8_t opcodeHint) noexcept {
        return stepInstructionWithOpcodeHint(bus, opcodeHint, true);
    }

    uint8_t stepInstructionWithOpcodeHint(Mos6510Bus& bus,
                                          uint8_t opcodeHint,
                                          bool hasOpcodeHint = true) noexcept {
        if (state_.jammed) return 0;
        if (state_.pc == kC64HaltAddr) { state_.jammed = true; return 0; }
        state_.lastTickUsedSemanticFallback = false;
        state_.lastSemanticFallbackWasOfficial = false;

        if (state_.nmiPending) {
            state_.nmiPending = false;
            serviceInterrupt_(bus, 0xFFFAu, false);
            advance(7);
            return 7;
        }
        if (state_.irqLine && !(state_.p & I)) {
            serviceInterrupt_(bus, 0xFFFEu, false);
            advance(7);
            return 7;
        }

        const uint8_t op = hasOpcodeHint ? opcodeHint : rd8_(bus);
        if (hasOpcodeHint) ++state_.pc;
        bool cross = false;
        uint16_t addr = 0;
        uint8_t v = 0;

        switch (op) {
        case 0x00:
            if (state_.trapBrkAsJam) { state_.jammed = true; advance(7); return 7; }
            ++state_.pc; // BRK is a two-byte instruction; push PC+2 relative to opcode address.
            serviceInterrupt_(bus, 0xFFFEu, true);
            advance(7);
            return 7;
        case 0x01: addr=addrINDX_(bus); state_.a|=bus.cpuRead(addr); setZN_(state_.a); advance(6); return 6;
        case 0x05: addr=addrZP_(bus); state_.a|=bus.cpuRead(addr); setZN_(state_.a); advance(3); return 3;
        case 0x06: addr=addrZP_(bus); rmwASL_(bus,addr); advance(5); return 5;
        case 0x08: push8_(bus, uint8_t(state_.p|B|U)); advance(3); return 3;
        case 0x09: state_.a|=rd8_(bus); setZN_(state_.a); advance(2); return 2;
        case 0x0A: state_.a=asl_(state_.a); advance(2); return 2;
        case 0x0D: addr=addrABS_(bus); state_.a|=bus.cpuRead(addr); setZN_(state_.a); advance(4); return 4;
        case 0x0E: addr=addrABS_(bus); rmwASL_(bus,addr); advance(6); return 6;
        case 0x10: return branch_(bus, !(state_.p&N));
        case 0x11: addr=addrINDY_(bus,&cross); state_.a|=bus.cpuRead(addr); setZN_(state_.a); advance(cross?6:5); return uint8_t(cross?6:5);
        case 0x15: addr=addrZPX_(bus); state_.a|=bus.cpuRead(addr); setZN_(state_.a); advance(4); return 4;
        case 0x16: addr=addrZPX_(bus); rmwASL_(bus,addr); advance(6); return 6;
        case 0x18: state_.p&=uint8_t(~C); advance(2); return 2;
        case 0x19: addr=addrABSY_(bus,&cross); state_.a|=bus.cpuRead(addr); setZN_(state_.a); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0x1D: addr=addrABSX_(bus,&cross); state_.a|=bus.cpuRead(addr); setZN_(state_.a); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0x1E: addr=addrABSX_(bus); rmwASL_(bus,addr); advance(7); return 7;
        case 0x20: { const uint16_t t=rd16_(bus); push16_(bus,uint16_t(state_.pc-1u)); state_.pc=t; advance(6); return 6; }
        case 0x21: addr=addrINDX_(bus); state_.a&=bus.cpuRead(addr); setZN_(state_.a); advance(6); return 6;
        case 0x24: addr=addrZP_(bus); bit_(bus.cpuRead(addr)); advance(3); return 3;
        case 0x25: addr=addrZP_(bus); state_.a&=bus.cpuRead(addr); setZN_(state_.a); advance(3); return 3;
        case 0x26: addr=addrZP_(bus); rmwROL_(bus,addr); advance(5); return 5;
        case 0x28: state_.p=uint8_t((pull8_(bus)&uint8_t(~B))|U); advance(4); return 4;
        case 0x29: state_.a&=rd8_(bus); setZN_(state_.a); advance(2); return 2;
        case 0x2A: state_.a=rol_(state_.a); advance(2); return 2;
        case 0x2C: addr=addrABS_(bus); bit_(bus.cpuRead(addr)); advance(4); return 4;
        case 0x2D: addr=addrABS_(bus); state_.a&=bus.cpuRead(addr); setZN_(state_.a); advance(4); return 4;
        case 0x2E: addr=addrABS_(bus); rmwROL_(bus,addr); advance(6); return 6;
        case 0x30: return branch_(bus, !!(state_.p&N));
        case 0x31: addr=addrINDY_(bus,&cross); state_.a&=bus.cpuRead(addr); setZN_(state_.a); advance(cross?6:5); return uint8_t(cross?6:5);
        case 0x35: addr=addrZPX_(bus); state_.a&=bus.cpuRead(addr); setZN_(state_.a); advance(4); return 4;
        case 0x36: addr=addrZPX_(bus); rmwROL_(bus,addr); advance(6); return 6;
        case 0x38: state_.p|=C; advance(2); return 2;
        case 0x39: addr=addrABSY_(bus,&cross); state_.a&=bus.cpuRead(addr); setZN_(state_.a); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0x3D: addr=addrABSX_(bus,&cross); state_.a&=bus.cpuRead(addr); setZN_(state_.a); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0x3E: addr=addrABSX_(bus); rmwROL_(bus,addr); advance(7); return 7;
        case 0x40: state_.p=uint8_t((pull8_(bus)&uint8_t(~B))|U); state_.pc=pull16_(bus); advance(6); return 6;
        case 0x41: addr=addrINDX_(bus); state_.a^=bus.cpuRead(addr); setZN_(state_.a); advance(6); return 6;
        case 0x45: addr=addrZP_(bus); state_.a^=bus.cpuRead(addr); setZN_(state_.a); advance(3); return 3;
        case 0x46: addr=addrZP_(bus); rmwLSR_(bus,addr); advance(5); return 5;
        case 0x48: push8_(bus,state_.a); advance(3); return 3;
        case 0x49: state_.a^=rd8_(bus); setZN_(state_.a); advance(2); return 2;
        case 0x4A: state_.a=lsr_(state_.a); advance(2); return 2;
        case 0x4C: state_.pc=rd16_(bus); advance(3); return 3;
        case 0x4D: addr=addrABS_(bus); state_.a^=bus.cpuRead(addr); setZN_(state_.a); advance(4); return 4;
        case 0x4E: addr=addrABS_(bus); rmwLSR_(bus,addr); advance(6); return 6;
        case 0x50: return branch_(bus, !(state_.p&V));
        case 0x51: addr=addrINDY_(bus,&cross); state_.a^=bus.cpuRead(addr); setZN_(state_.a); advance(cross?6:5); return uint8_t(cross?6:5);
        case 0x55: addr=addrZPX_(bus); state_.a^=bus.cpuRead(addr); setZN_(state_.a); advance(4); return 4;
        case 0x56: addr=addrZPX_(bus); rmwLSR_(bus,addr); advance(6); return 6;
        case 0x58: state_.p&=uint8_t(~I); advance(2); return 2;
        case 0x59: addr=addrABSY_(bus,&cross); state_.a^=bus.cpuRead(addr); setZN_(state_.a); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0x5D: addr=addrABSX_(bus,&cross); state_.a^=bus.cpuRead(addr); setZN_(state_.a); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0x5E: addr=addrABSX_(bus); rmwLSR_(bus,addr); advance(7); return 7;
        case 0x60: state_.pc=uint16_t(pull16_(bus)+1u); advance(6); if (state_.pc == kC64HaltAddr) state_.jammed = true; return 6;
        case 0x61: addr=addrINDX_(bus); adc_(bus.cpuRead(addr)); advance(6); return 6;
        case 0x65: addr=addrZP_(bus); adc_(bus.cpuRead(addr)); advance(3); return 3;
        case 0x66: addr=addrZP_(bus); rmwROR_(bus,addr); advance(5); return 5;
        case 0x68: state_.a=pull8_(bus); setZN_(state_.a); advance(4); return 4;
        case 0x69: adc_(rd8_(bus)); advance(2); return 2;
        case 0x6A: state_.a=ror_(state_.a); advance(2); return 2;
        case 0x6C: { const uint16_t ia=rd16_(bus); const uint16_t hi_a=uint16_t((ia&0xFF00u)|((ia+1u)&0x00FFu)); state_.pc=uint16_t(bus.cpuRead(ia)|(uint16_t(bus.cpuRead(hi_a))<<8)); advance(5); return 5; }
        case 0x6D: addr=addrABS_(bus); adc_(bus.cpuRead(addr)); advance(4); return 4;
        case 0x6E: addr=addrABS_(bus); rmwROR_(bus,addr); advance(6); return 6;
        case 0x70: return branch_(bus, !!(state_.p&V));
        case 0x71: addr=addrINDY_(bus,&cross); adc_(bus.cpuRead(addr)); advance(cross?6:5); return uint8_t(cross?6:5);
        case 0x75: addr=addrZPX_(bus); adc_(bus.cpuRead(addr)); advance(4); return 4;
        case 0x76: addr=addrZPX_(bus); rmwROR_(bus,addr); advance(6); return 6;
        case 0x78: state_.p|=I; advance(2); return 2;
        case 0x79: addr=addrABSY_(bus,&cross); adc_(bus.cpuRead(addr)); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0x7D: addr=addrABSX_(bus,&cross); adc_(bus.cpuRead(addr)); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0x7E: addr=addrABSX_(bus); rmwROR_(bus,addr); advance(7); return 7;
        case 0x81: addr=addrINDX_(bus); bus.cpuWrite(addr,state_.a); advance(6); return 6;
        case 0x84: addr=addrZP_(bus); bus.cpuWrite(addr,state_.y); advance(3); return 3;
        case 0x85: addr=addrZP_(bus); bus.cpuWrite(addr,state_.a); advance(3); return 3;
        case 0x86: addr=addrZP_(bus); bus.cpuWrite(addr,state_.x); advance(3); return 3;
        case 0x88: state_.y=uint8_t(state_.y-1u); setZN_(state_.y); advance(2); return 2;
        case 0x8A: state_.a=state_.x; setZN_(state_.a); advance(2); return 2;
        case 0x8C: addr=addrABS_(bus); bus.cpuWrite(addr,state_.y); advance(4); return 4;
        case 0x8D: addr=addrABS_(bus); bus.cpuWrite(addr,state_.a); advance(4); return 4;
        case 0x8E: addr=addrABS_(bus); bus.cpuWrite(addr,state_.x); advance(4); return 4;
        case 0x90: return branch_(bus, !(state_.p&C));
        case 0x91: addr=addrINDY_(bus); bus.cpuWrite(addr,state_.a); advance(6); return 6;
        case 0x94: addr=addrZPX_(bus); bus.cpuWrite(addr,state_.y); advance(4); return 4;
        case 0x95: addr=addrZPX_(bus); bus.cpuWrite(addr,state_.a); advance(4); return 4;
        case 0x96: addr=addrZPY_(bus); bus.cpuWrite(addr,state_.x); advance(4); return 4;
        case 0x98: state_.a=state_.y; setZN_(state_.a); advance(2); return 2;
        case 0x99: addr=addrABSY_(bus); bus.cpuWrite(addr,state_.a); advance(5); return 5;
        case 0x9A: state_.sp=state_.x; advance(2); return 2;
        case 0x9D: addr=addrABSX_(bus); bus.cpuWrite(addr,state_.a); advance(5); return 5;
        case 0xA0: state_.y=rd8_(bus); setZN_(state_.y); advance(2); return 2;
        case 0xA1: addr=addrINDX_(bus); state_.a=bus.cpuRead(addr); setZN_(state_.a); advance(6); return 6;
        case 0xA2: state_.x=rd8_(bus); setZN_(state_.x); advance(2); return 2;
        case 0xA4: addr=addrZP_(bus); state_.y=bus.cpuRead(addr); setZN_(state_.y); advance(3); return 3;
        case 0xA5: addr=addrZP_(bus); state_.a=bus.cpuRead(addr); setZN_(state_.a); advance(3); return 3;
        case 0xA6: addr=addrZP_(bus); state_.x=bus.cpuRead(addr); setZN_(state_.x); advance(3); return 3;
        case 0xA8: state_.y=state_.a; setZN_(state_.y); advance(2); return 2;
        case 0xA9: state_.a=rd8_(bus); setZN_(state_.a); advance(2); return 2;
        case 0xAA: state_.x=state_.a; setZN_(state_.x); advance(2); return 2;
        case 0xAC: addr=addrABS_(bus); state_.y=bus.cpuRead(addr); setZN_(state_.y); advance(4); return 4;
        case 0xAD: addr=addrABS_(bus); state_.a=bus.cpuRead(addr); setZN_(state_.a); advance(4); return 4;
        case 0xAE: addr=addrABS_(bus); state_.x=bus.cpuRead(addr); setZN_(state_.x); advance(4); return 4;
        case 0xB0: return branch_(bus, !!(state_.p&C));
        case 0xB1: addr=addrINDY_(bus,&cross); state_.a=bus.cpuRead(addr); setZN_(state_.a); advance(cross?6:5); return uint8_t(cross?6:5);
        case 0xB4: addr=addrZPX_(bus); state_.y=bus.cpuRead(addr); setZN_(state_.y); advance(4); return 4;
        case 0xB5: addr=addrZPX_(bus); state_.a=bus.cpuRead(addr); setZN_(state_.a); advance(4); return 4;
        case 0xB6: addr=addrZPY_(bus); state_.x=bus.cpuRead(addr); setZN_(state_.x); advance(4); return 4;
        case 0xB8: state_.p&=uint8_t(~V); advance(2); return 2;
        case 0xB9: addr=addrABSY_(bus,&cross); state_.a=bus.cpuRead(addr); setZN_(state_.a); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0xBA: state_.x=state_.sp; setZN_(state_.x); advance(2); return 2;
        case 0xBC: addr=addrABSX_(bus,&cross); state_.y=bus.cpuRead(addr); setZN_(state_.y); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0xBD: addr=addrABSX_(bus,&cross); state_.a=bus.cpuRead(addr); setZN_(state_.a); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0xBE: addr=addrABSY_(bus,&cross); state_.x=bus.cpuRead(addr); setZN_(state_.x); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0xC0: cmp_(state_.y,rd8_(bus)); advance(2); return 2;
        case 0xC1: addr=addrINDX_(bus); cmp_(state_.a,bus.cpuRead(addr)); advance(6); return 6;
        case 0xC4: addr=addrZP_(bus); cmp_(state_.y,bus.cpuRead(addr)); advance(3); return 3;
        case 0xC5: addr=addrZP_(bus); cmp_(state_.a,bus.cpuRead(addr)); advance(3); return 3;
        case 0xC6: addr=addrZP_(bus); rmwDEC_(bus,addr); advance(5); return 5;
        case 0xC8: state_.y=uint8_t(state_.y+1u); setZN_(state_.y); advance(2); return 2;
        case 0xC9: cmp_(state_.a,rd8_(bus)); advance(2); return 2;
        case 0xCA: state_.x=uint8_t(state_.x-1u); setZN_(state_.x); advance(2); return 2;
        case 0xCC: addr=addrABS_(bus); cmp_(state_.y,bus.cpuRead(addr)); advance(4); return 4;
        case 0xCD: addr=addrABS_(bus); cmp_(state_.a,bus.cpuRead(addr)); advance(4); return 4;
        case 0xCE: addr=addrABS_(bus); rmwDEC_(bus,addr); advance(6); return 6;
        case 0xD0: return branch_(bus, !(state_.p&Z));
        case 0xD1: addr=addrINDY_(bus,&cross); cmp_(state_.a,bus.cpuRead(addr)); advance(cross?6:5); return uint8_t(cross?6:5);
        case 0xD5: addr=addrZPX_(bus); cmp_(state_.a,bus.cpuRead(addr)); advance(4); return 4;
        case 0xD6: addr=addrZPX_(bus); rmwDEC_(bus,addr); advance(6); return 6;
        case 0xD8: state_.p&=uint8_t(~D); advance(2); return 2;
        case 0xD9: addr=addrABSY_(bus,&cross); cmp_(state_.a,bus.cpuRead(addr)); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0xDD: addr=addrABSX_(bus,&cross); cmp_(state_.a,bus.cpuRead(addr)); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0xDE: addr=addrABSX_(bus); rmwDEC_(bus,addr); advance(7); return 7;
        case 0xE0: cmp_(state_.x,rd8_(bus)); advance(2); return 2;
        case 0xE1: addr=addrINDX_(bus); sbc_(bus.cpuRead(addr)); advance(6); return 6;
        case 0xE4: addr=addrZP_(bus); cmp_(state_.x,bus.cpuRead(addr)); advance(3); return 3;
        case 0xE5: addr=addrZP_(bus); sbc_(bus.cpuRead(addr)); advance(3); return 3;
        case 0xE6: addr=addrZP_(bus); rmwINC_(bus,addr); advance(5); return 5;
        case 0xE8: state_.x=uint8_t(state_.x+1u); setZN_(state_.x); advance(2); return 2;
        case 0xE9: sbc_(rd8_(bus)); advance(2); return 2;
        case 0xEA: advance(2); return 2;
        case 0xEC: addr=addrABS_(bus); cmp_(state_.x,bus.cpuRead(addr)); advance(4); return 4;
        case 0xED: addr=addrABS_(bus); sbc_(bus.cpuRead(addr)); advance(4); return 4;
        case 0xEE: addr=addrABS_(bus); rmwINC_(bus,addr); advance(6); return 6;
        case 0xF0: return branch_(bus, !!(state_.p&Z));
        case 0xF1: addr=addrINDY_(bus,&cross); sbc_(bus.cpuRead(addr)); advance(cross?6:5); return uint8_t(cross?6:5);
        case 0xF5: addr=addrZPX_(bus); sbc_(bus.cpuRead(addr)); advance(4); return 4;
        case 0xF6: addr=addrZPX_(bus); rmwINC_(bus,addr); advance(6); return 6;
        case 0xF8: state_.p|=D; advance(2); return 2;
        case 0xF9: addr=addrABSY_(bus,&cross); sbc_(bus.cpuRead(addr)); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0xFD: addr=addrABSX_(bus,&cross); sbc_(bus.cpuRead(addr)); advance(cross?5:4); return uint8_t(cross?5:4);
        case 0xFE: addr=addrABSX_(bus); rmwINC_(bus,addr); advance(7); return 7;

        // Deterministic NMOS 6510 illegal-opcode subset used by many C64/RSID players.
        // These are the stable composite opcodes, not the bus-unstable JAM/KIL family.
        case 0xA7: addr=addrZP_(bus); state_.a=state_.x=bus.cpuRead(addr); setZN_(state_.a); advance(3); return 3; // LAX zp
        case 0xB7: addr=addrZPY_(bus); state_.a=state_.x=bus.cpuRead(addr); setZN_(state_.a); advance(4); return 4; // LAX zp,Y
        case 0xAF: addr=addrABS_(bus); state_.a=state_.x=bus.cpuRead(addr); setZN_(state_.a); advance(4); return 4; // LAX abs
        case 0xBF: addr=addrABSY_(bus,&cross); state_.a=state_.x=bus.cpuRead(addr); setZN_(state_.a); advance(cross?5:4); return uint8_t(cross?5:4); // LAX abs,Y
        case 0xA3: addr=addrINDX_(bus); state_.a=state_.x=bus.cpuRead(addr); setZN_(state_.a); advance(6); return 6; // LAX (zp,X)
        case 0xB3: addr=addrINDY_(bus,&cross); state_.a=state_.x=bus.cpuRead(addr); setZN_(state_.a); advance(cross?6:5); return uint8_t(cross?6:5); // LAX (zp),Y
        case 0x87: addr=addrZP_(bus); bus.cpuWrite(addr, uint8_t(state_.a & state_.x)); advance(3); return 3; // SAX zp
        case 0x97: addr=addrZPY_(bus); bus.cpuWrite(addr, uint8_t(state_.a & state_.x)); advance(4); return 4; // SAX zp,Y
        case 0x8F: addr=addrABS_(bus); bus.cpuWrite(addr, uint8_t(state_.a & state_.x)); advance(4); return 4; // SAX abs
        case 0x83: addr=addrINDX_(bus); bus.cpuWrite(addr, uint8_t(state_.a & state_.x)); advance(6); return 6; // SAX (zp,X)
        case 0xC7: addr=addrZP_(bus); v=rmwDEC_(bus,addr); cmp_(state_.a,v); advance(5); return 5; // DCP zp
        case 0xCF: addr=addrABS_(bus); v=rmwDEC_(bus,addr); cmp_(state_.a,v); advance(6); return 6; // DCP abs
        case 0xE7: addr=addrZP_(bus); v=rmwINC_(bus,addr); sbc_(v); advance(5); return 5; // ISC zp
        case 0xEF: addr=addrABS_(bus); v=rmwINC_(bus,addr); sbc_(v); advance(6); return 6; // ISC abs
        case 0x07: addr=addrZP_(bus); v=rmwASL_(bus,addr); state_.a|=v; setZN_(state_.a); advance(5); return 5; // SLO zp
        case 0x0F: addr=addrABS_(bus); v=rmwASL_(bus,addr); state_.a|=v; setZN_(state_.a); advance(6); return 6; // SLO abs
        case 0x27: addr=addrZP_(bus); v=rmwROL_(bus,addr); state_.a&=v; setZN_(state_.a); advance(5); return 5; // RLA zp
        case 0x2F: addr=addrABS_(bus); v=rmwROL_(bus,addr); state_.a&=v; setZN_(state_.a); advance(6); return 6; // RLA abs
        case 0x47: addr=addrZP_(bus); v=rmwLSR_(bus,addr); state_.a^=v; setZN_(state_.a); advance(5); return 5; // SRE zp
        case 0x4F: addr=addrABS_(bus); v=rmwLSR_(bus,addr); state_.a^=v; setZN_(state_.a); advance(6); return 6; // SRE abs
        case 0x67: addr=addrZP_(bus); v=rmwROR_(bus,addr); adc_(v); advance(5); return 5; // RRA zp
        case 0x6F: addr=addrABS_(bus); v=rmwROR_(bus,addr); adc_(v); advance(6); return 6; // RRA abs
        case 0x0B: state_.a &= rd8_(bus); setZN_(state_.a); setFlag_(C, state_.a & 0x80u); advance(2); return 2; // ANC #
        case 0x2B: state_.a &= rd8_(bus); setZN_(state_.a); setFlag_(C, state_.a & 0x80u); advance(2); return 2; // ANC #
        case 0x4B: state_.a &= rd8_(bus); state_.a = lsr_(state_.a); advance(2); return 2; // ALR #
        // ARR #: deterministic-subset approximation (V/C flag interaction omitted).
        // IllegalApprox: full ARR behavior includes ADC-like BCD interaction on some chips.
        case 0x6B: recordApproximateIllegal_(op); state_.a &= rd8_(bus); state_.a = ror_(state_.a); advance(2); return 2; // ARR # IllegalApprox
        case 0xCB: { const uint8_t imm=rd8_(bus); const uint16_t t=uint16_t(state_.a & state_.x) - imm; setFlag_(C, t < 0x100u); state_.x=uint8_t(t); setZN_(state_.x); advance(2); return 2; } // AXS #
        // XAA #: behaviour is NMOS-chip-dependent and not fully deterministic.
        // This is an IllegalApprox: the AND-magic constant varies by chip sample.
        // In strict RSID mode this opcode may produce incorrect results for tunes
        // that depend on its exact magic constant. Telemetry should flag this.
        case 0x8B: recordApproximateIllegal_(op); state_.a = uint8_t(state_.x & rd8_(bus)); setZN_(state_.a); advance(2); return 2; // XAA # IllegalApprox
        case 0xBB: addr=addrABSY_(bus,&cross); v=uint8_t(bus.cpuRead(addr) & state_.sp); state_.a=state_.x=state_.sp=v; setZN_(v); advance(cross?5:4); return uint8_t(cross?5:4); // LAS abs,Y

        case 0x03: addr=addrINDX_(bus); v=rmwASL_(bus,addr); state_.a|=v; setZN_(state_.a); advance(8); return 8; // SLO (zp,X)
        case 0x13: addr=addrINDY_(bus); v=rmwASL_(bus,addr); state_.a|=v; setZN_(state_.a); advance(8); return 8; // SLO (zp),Y
        case 0x17: addr=addrZPX_(bus); v=rmwASL_(bus,addr); state_.a|=v; setZN_(state_.a); advance(6); return 6; // SLO zp,X
        case 0x1B: addr=addrABSY_(bus); v=rmwASL_(bus,addr); state_.a|=v; setZN_(state_.a); advance(7); return 7; // SLO abs,Y
        case 0x1F: addr=addrABSX_(bus); v=rmwASL_(bus,addr); state_.a|=v; setZN_(state_.a); advance(7); return 7; // SLO abs,X
        case 0x23: addr=addrINDX_(bus); v=rmwROL_(bus,addr); state_.a&=v; setZN_(state_.a); advance(8); return 8; // RLA (zp,X)
        case 0x33: addr=addrINDY_(bus); v=rmwROL_(bus,addr); state_.a&=v; setZN_(state_.a); advance(8); return 8; // RLA (zp),Y
        case 0x37: addr=addrZPX_(bus); v=rmwROL_(bus,addr); state_.a&=v; setZN_(state_.a); advance(6); return 6; // RLA zp,X
        case 0x3B: addr=addrABSY_(bus); v=rmwROL_(bus,addr); state_.a&=v; setZN_(state_.a); advance(7); return 7; // RLA abs,Y
        case 0x3F: addr=addrABSX_(bus); v=rmwROL_(bus,addr); state_.a&=v; setZN_(state_.a); advance(7); return 7; // RLA abs,X
        case 0x43: addr=addrINDX_(bus); v=rmwLSR_(bus,addr); state_.a^=v; setZN_(state_.a); advance(8); return 8; // SRE (zp,X)
        case 0x53: addr=addrINDY_(bus); v=rmwLSR_(bus,addr); state_.a^=v; setZN_(state_.a); advance(8); return 8; // SRE (zp),Y
        case 0x57: addr=addrZPX_(bus); v=rmwLSR_(bus,addr); state_.a^=v; setZN_(state_.a); advance(6); return 6; // SRE zp,X
        case 0x5B: addr=addrABSY_(bus); v=rmwLSR_(bus,addr); state_.a^=v; setZN_(state_.a); advance(7); return 7; // SRE abs,Y
        case 0x5F: addr=addrABSX_(bus); v=rmwLSR_(bus,addr); state_.a^=v; setZN_(state_.a); advance(7); return 7; // SRE abs,X
        case 0x63: addr=addrINDX_(bus); v=rmwROR_(bus,addr); adc_(v); advance(8); return 8; // RRA (zp,X)
        case 0x73: addr=addrINDY_(bus); v=rmwROR_(bus,addr); adc_(v); advance(8); return 8; // RRA (zp),Y
        case 0x77: addr=addrZPX_(bus); v=rmwROR_(bus,addr); adc_(v); advance(6); return 6; // RRA zp,X
        case 0x7B: addr=addrABSY_(bus); v=rmwROR_(bus,addr); adc_(v); advance(7); return 7; // RRA abs,Y
        case 0x7F: addr=addrABSX_(bus); v=rmwROR_(bus,addr); adc_(v); advance(7); return 7; // RRA abs,X
        case 0xC3: addr=addrINDX_(bus); v=rmwDEC_(bus,addr); cmp_(state_.a,v); advance(8); return 8; // DCP (zp,X)
        case 0xD3: addr=addrINDY_(bus); v=rmwDEC_(bus,addr); cmp_(state_.a,v); advance(8); return 8; // DCP (zp),Y
        case 0xD7: addr=addrZPX_(bus); v=rmwDEC_(bus,addr); cmp_(state_.a,v); advance(6); return 6; // DCP zp,X
        case 0xDB: addr=addrABSY_(bus); v=rmwDEC_(bus,addr); cmp_(state_.a,v); advance(7); return 7; // DCP abs,Y
        case 0xDF: addr=addrABSX_(bus); v=rmwDEC_(bus,addr); cmp_(state_.a,v); advance(7); return 7; // DCP abs,X
        case 0xE3: addr=addrINDX_(bus); v=rmwINC_(bus,addr); sbc_(v); advance(8); return 8; // ISC (zp,X)
        case 0xF3: addr=addrINDY_(bus); v=rmwINC_(bus,addr); sbc_(v); advance(8); return 8; // ISC (zp),Y
        case 0xF7: addr=addrZPX_(bus); v=rmwINC_(bus,addr); sbc_(v); advance(6); return 6; // ISC zp,X
        case 0xFB: addr=addrABSY_(bus); v=rmwINC_(bus,addr); sbc_(v); advance(7); return 7; // ISC abs,Y
        case 0xFF: addr=addrABSX_(bus); v=rmwINC_(bus,addr); sbc_(v); advance(7); return 7; // ISC abs,X
        case 0x93: recordApproximateIllegal_(op); addr=addrINDY_(bus); bus.cpuWrite(addr, uint8_t(state_.a & state_.x & (((addr >> 8u) + 1u) & 0xFFu))); advance(6); return 6; // AHX (zp),Y IllegalApprox
        case 0x9F: recordApproximateIllegal_(op); addr=addrABSY_(bus); bus.cpuWrite(addr, uint8_t(state_.a & state_.x & (((addr >> 8u) + 1u) & 0xFFu))); advance(5); return 5; // AHX abs,Y IllegalApprox
        case 0x9C: recordApproximateIllegal_(op); addr=addrABSX_(bus); bus.cpuWrite(addr, uint8_t(state_.y & (((addr >> 8u) + 1u) & 0xFFu))); advance(5); return 5; // SHY abs,X IllegalApprox
        case 0x9E: recordApproximateIllegal_(op); addr=addrABSY_(bus); bus.cpuWrite(addr, uint8_t(state_.x & (((addr >> 8u) + 1u) & 0xFFu))); advance(5); return 5; // SHX abs,Y IllegalApprox
        case 0x9B: recordApproximateIllegal_(op); addr=addrABSY_(bus); state_.sp=uint8_t(state_.a & state_.x); bus.cpuWrite(addr, uint8_t(state_.sp & (((addr >> 8u) + 1u) & 0xFFu))); advance(5); return 5; // TAS abs,Y IllegalApprox
        case 0x02: case 0x12: case 0x22: case 0x32:
        case 0x42: case 0x52: case 0x62: case 0x72:
        case 0x92: case 0xB2: case 0xD2: case 0xF2:
            state_.jammed = true;
            return 0;
        default:
            recordSemanticFallback_(op);
            state_.jammed = true;
            return 0;
        }
    }

private:
    void recordApproximateIllegal_(uint8_t opcode) noexcept {
        state_.lastTickUsedApproximateIllegal = true;
        state_.lastApproximateIllegalOpcode = opcode;
        ++state_.approximateIllegalOpcodeCount;
    }

    void recordSemanticFallback_(uint8_t opcode) noexcept {
        state_.lastTickUsedSemanticFallback = true;
        state_.lastSemanticFallbackOpcode = opcode;
        state_.lastSemanticFallbackWasOfficial = c64Mos6510OpcodeIsOfficial(opcode);
        ++state_.semanticFallbackCount;
        if (state_.lastSemanticFallbackWasOfficial) {
            ++state_.officialSemanticFallbackCount;
        }
    }

    uint8_t rd8_(Mos6510Bus& bus) noexcept { return bus.cpuRead(state_.pc++); }
    uint16_t rd16_(Mos6510Bus& bus) noexcept {
        const uint16_t lo = rd8_(bus);
        return uint16_t(lo | (uint16_t(rd8_(bus)) << 8));
    }
    void push8_(Mos6510Bus& bus, uint8_t v) noexcept { bus.cpuWrite(uint16_t(0x0100u + state_.sp), v); state_.sp = uint8_t(state_.sp - 1u); }

    void serviceInterrupt_(Mos6510Bus& bus, uint16_t vector, bool brk) noexcept {
        push16_(bus, state_.pc);
        push8_(bus, uint8_t((state_.p & uint8_t(~B)) | U | (brk ? B : 0u)));
        state_.p = uint8_t((state_.p | I | U) & uint8_t(~B));
        const uint8_t lo = bus.cpuRead(vector);
        const uint8_t hi = bus.cpuRead(uint16_t(vector + 1u));
        state_.pc = uint16_t(lo | (uint16_t(hi) << 8u));
        if (state_.pc == kC64HaltAddr) state_.jammed = true;
    }
    uint8_t pull8_(Mos6510Bus& bus) noexcept { state_.sp = uint8_t(state_.sp + 1u); return bus.cpuRead(uint16_t(0x0100u + state_.sp)); }
    void push16_(Mos6510Bus& bus, uint16_t v) noexcept { push8_(bus, uint8_t(v >> 8)); push8_(bus, uint8_t(v)); }
    uint16_t pull16_(Mos6510Bus& bus) noexcept { const uint16_t lo = pull8_(bus); const uint16_t hi = pull8_(bus); return uint16_t(lo | (hi << 8)); }
    uint16_t addrZP_(Mos6510Bus& bus) noexcept { return rd8_(bus); }
    uint16_t addrZPX_(Mos6510Bus& bus) noexcept { return uint8_t(rd8_(bus) + state_.x); }
    uint16_t addrZPY_(Mos6510Bus& bus) noexcept { return uint8_t(rd8_(bus) + state_.y); }
    uint16_t addrABS_(Mos6510Bus& bus) noexcept { return rd16_(bus); }
    uint16_t addrABSX_(Mos6510Bus& bus, bool* cross = nullptr) noexcept {
        const uint16_t base = rd16_(bus); const uint16_t eff = uint16_t(base + state_.x);
        const bool crossed = (base >> 8) != (eff >> 8);
        if (cross) { *cross = crossed; if (crossed) (void)bus.cpuRead(uint16_t((base & 0xFF00u) | (eff & 0x00FFu))); }
        return eff;
    }
    uint16_t addrABSY_(Mos6510Bus& bus, bool* cross = nullptr) noexcept {
        const uint16_t base = rd16_(bus); const uint16_t eff = uint16_t(base + state_.y);
        const bool crossed = (base >> 8) != (eff >> 8);
        if (cross) { *cross = crossed; if (crossed) (void)bus.cpuRead(uint16_t((base & 0xFF00u) | (eff & 0x00FFu))); }
        return eff;
    }
    uint16_t addrINDX_(Mos6510Bus& bus) noexcept {
        const uint8_t zp = uint8_t(rd8_(bus) + state_.x);
        return uint16_t(bus.cpuRead(zp) | (uint16_t(bus.cpuRead(uint8_t(zp + 1u))) << 8));
    }
    uint16_t addrINDY_(Mos6510Bus& bus, bool* cross = nullptr) noexcept {
        const uint8_t zp = rd8_(bus);
        const uint16_t base = uint16_t(bus.cpuRead(zp) | (uint16_t(bus.cpuRead(uint8_t(zp + 1u))) << 8));
        const uint16_t eff = uint16_t(base + state_.y);
        const bool crossed = (base >> 8) != (eff >> 8);
        if (cross) { *cross = crossed; if (crossed) (void)bus.cpuRead(uint16_t((base & 0xFF00u) | (eff & 0x00FFu))); }
        return eff;
    }
    uint8_t branch_(Mos6510Bus& bus, bool cond) noexcept {
        const int8_t off = static_cast<int8_t>(rd8_(bus));
        uint8_t cyc = 2;
        if (cond) { const uint16_t newPc = uint16_t(state_.pc + int16_t(off)); cyc = uint8_t(cyc + (((state_.pc ^ newPc) >> 8) ? 2 : 1)); state_.pc = newPc; }
        advance(cyc); return cyc;
    }
    void setFlag_(uint8_t mask, bool cond) noexcept { if (cond) state_.p |= mask; else state_.p &= uint8_t(~mask); }
    void setZN_(uint8_t v) noexcept { setFlag_(Z, v == 0); setFlag_(N, (v & 0x80u) != 0); }
    uint8_t rmwASL_(Mos6510Bus& bus, uint16_t addr) noexcept { const uint8_t old=bus.cpuRead(addr); bus.cpuWrite(addr, old); const uint8_t nv=asl_(old); bus.cpuWrite(addr, nv); return nv; }
    uint8_t rmwLSR_(Mos6510Bus& bus, uint16_t addr) noexcept { const uint8_t old=bus.cpuRead(addr); bus.cpuWrite(addr, old); const uint8_t nv=lsr_(old); bus.cpuWrite(addr, nv); return nv; }
    uint8_t rmwROL_(Mos6510Bus& bus, uint16_t addr) noexcept { const uint8_t old=bus.cpuRead(addr); bus.cpuWrite(addr, old); const uint8_t nv=rol_(old); bus.cpuWrite(addr, nv); return nv; }
    uint8_t rmwROR_(Mos6510Bus& bus, uint16_t addr) noexcept { const uint8_t old=bus.cpuRead(addr); bus.cpuWrite(addr, old); const uint8_t nv=ror_(old); bus.cpuWrite(addr, nv); return nv; }
    uint8_t rmwINC_(Mos6510Bus& bus, uint16_t addr) noexcept { const uint8_t old=bus.cpuRead(addr); bus.cpuWrite(addr, old); const uint8_t nv=uint8_t(old+1u); setZN_(nv); bus.cpuWrite(addr, nv); return nv; }
    uint8_t rmwDEC_(Mos6510Bus& bus, uint16_t addr) noexcept { const uint8_t old=bus.cpuRead(addr); bus.cpuWrite(addr, old); const uint8_t nv=uint8_t(old-1u); setZN_(nv); bus.cpuWrite(addr, nv); return nv; }
    uint8_t asl_(uint8_t v) noexcept { setFlag_(C, v & 0x80u); v=uint8_t(v<<1); setZN_(v); return v; }
    uint8_t lsr_(uint8_t v) noexcept { setFlag_(C, v & 0x01u); v=uint8_t(v>>1); setZN_(v); return v; }
    uint8_t rol_(uint8_t v) noexcept { const uint8_t c=(state_.p&C)?1u:0u; setFlag_(C, v&0x80u); v=uint8_t((v<<1)|c); setZN_(v); return v; }
    uint8_t ror_(uint8_t v) noexcept { const uint8_t c=(state_.p&C)?0x80u:0u; setFlag_(C, v&0x01u); v=uint8_t((v>>1)|c); setZN_(v); return v; }
    void bit_(uint8_t v) noexcept { setFlag_(Z, (state_.a & v) == 0); setFlag_(N, v & 0x80u); setFlag_(V, v & 0x40u); }
    void cmp_(uint8_t reg, uint8_t v) noexcept { const uint8_t r=uint8_t(reg-v); setFlag_(C, reg>=v); setZN_(r); }
    void adc_(uint8_t v) noexcept {
        const uint8_t a0 = state_.a;
        const uint8_t carryIn = (state_.p & C) ? 1u : 0u;
        const uint16_t binary = uint16_t(a0) + v + carryIn;
        const uint8_t binary8 = static_cast<uint8_t>(binary);
        setFlag_(V, (~(a0 ^ v) & (a0 ^ binary8) & 0x80u) != 0);
        if (state_.p & D) {
            uint16_t lo = static_cast<uint16_t>(static_cast<unsigned>(a0 & 0x0Fu) + static_cast<unsigned>(v & 0x0Fu) + static_cast<unsigned>(carryIn));
            uint16_t hi = static_cast<uint16_t>(static_cast<unsigned>(a0 >> 4u) + static_cast<unsigned>(v >> 4u));
            if (lo > 9u) { lo += 6u; ++hi; }
            if (hi > 9u) hi += 6u;
            setFlag_(C, hi > 0x0Fu);
            state_.a = static_cast<uint8_t>((static_cast<unsigned>(hi) << 4u) | (static_cast<unsigned>(lo) & 0x0Fu));
        } else {
            setFlag_(C, binary > 0xFFu);
            state_.a = binary8;
        }
        setZN_(state_.a);
    }
    void sbc_(uint8_t v) noexcept {
        const uint8_t a0 = state_.a;
        const uint8_t carryIn = (state_.p & C) ? 1u : 0u;
        const uint16_t binary = uint16_t(a0) - v - (carryIn ? 0u : 1u);
        const uint8_t binary8 = static_cast<uint8_t>(binary);
        setFlag_(V, ((a0 ^ v) & (a0 ^ binary8) & 0x80u) != 0);
        if (state_.p & D) {
            int lo = (a0 & 0x0F) - (v & 0x0F) - (carryIn ? 0 : 1);
            int hi = (a0 >> 4) - (v >> 4);
            if (lo < 0) { lo -= 6; --hi; }
            if (hi < 0) hi -= 6;
            setFlag_(C, binary < 0x100u);
            state_.a = static_cast<uint8_t>(((hi << 4) | (lo & 0x0F)) & 0xFF);
        } else {
            setFlag_(C, binary < 0x100u);
            state_.a = binary8;
        }
        setZN_(state_.a);
    }

    Mos6510State state_{};
};

} // namespace ArpSID::C64
