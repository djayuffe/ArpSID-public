// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "arpsid/core/c64_open_bus.h"
#include "arpsid/core/c64_bus.h"
#include "arpsid/core/c64_cia.h"
#include "arpsid/core/c64_phi2_types.h"
#include "arpsid/core/c64_processor_port.h"
#include "arpsid/core/c64_vic.h"
#include <array>

namespace ArpSID::C64 {

struct C64DirtyWrite {
    uint16_t address = 0xFFFFu;
    uint8_t value = 0xFFu;
    uint64_t phi2 = 0;
    bool rmwDummy = false;
    RmwBusEventKind rmwEventKind = RmwBusEventKind::None;
};

enum class ReadTarget : uint8_t {
    Ram,
    ProcessorPort,
    BasicRom,
    KernalRom,
    CharRom,
    Sid,
    Vic,
    Cia1,
    Cia2,
    ColorRam,
    Cartridge,
    OpenBus
};

enum class WriteTarget : uint8_t {
    Ram,
    ProcessorPort,
    Sid,
    Vic,
    Cia1,
    Cia2,
    ColorRam,
    CartridgeIo
};

struct PlaState {
    bool loram = false;
    bool hiram = false;
    bool charen = false;
    bool game = true;
    bool exrom = true;
};

inline PlaState plaFromPortCart(const ProcessorPort6510& port,
                                bool gameHigh = true,
                                bool exromHigh = true) noexcept {
    return PlaState{port.loram(), port.hiram(), port.charen(), gameHigh, exromHigh};
}

// IO-space ($D000-$DFFF) read sub-decode, shared by normal and Ultimax modes.
inline ReadTarget decodeIoReadRegion_(uint16_t a) noexcept {
    if (a <= 0xD3FFu) return ReadTarget::Vic;
    if (a <= 0xD7FFu) return ReadTarget::Sid;
    if (a <= 0xDBFFu) return ReadTarget::ColorRam;
    if (a <= 0xDCFFu) return ReadTarget::Cia1;
    if (a <= 0xDDFFu) return ReadTarget::Cia2;
    return ReadTarget::OpenBus;
}

inline ReadTarget decodeCpuRead(uint16_t a, const PlaState& p) noexcept {
    if (a <= 0x0001u) return ReadTarget::ProcessorPort;

    // Cartridge lines are active-low: GAME/EXROM "high" (true here) == not
    // asserted == normal C64 map. When both are high (the only case for SID/
    // PSID/RSID playback and all tests) this matches the original decode exactly.
    const bool cartActive = !(p.game && p.exrom);
    if (cartActive) {
        const bool ultimax = (!p.game && p.exrom);
        if (ultimax) {
            // Ultimax: only $0000-$0FFF RAM, IO at $D000-$DFFF, cartridge ROML
            // at $8000-$9FFF and ROMH at $E000-$FFFF; KERNAL/BASIC/CHAR are NOT
            // visible. Everything else floats (open bus).
            if (a <= 0x0FFFu) return ReadTarget::Ram;
            if (a >= 0x8000u && a <= 0x9FFFu) return ReadTarget::Cartridge; // ROML
            if (a >= 0xD000u && a <= 0xDFFFu) return decodeIoReadRegion_(a);
            if (a >= 0xE000u) return ReadTarget::Cartridge;                  // ROMH
            return ReadTarget::OpenBus;
        }
        // 8K cart (game=1,exrom=0): ROML at $8000-$9FFF when loram&hiram.
        // 16K cart (game=0,exrom=0): ROML at $8000-$9FFF and ROMH at $A000-$BFFF.
        if (a >= 0x8000u && a <= 0x9FFFu && p.loram && p.hiram) return ReadTarget::Cartridge; // ROML
        if (a >= 0xA000u && a <= 0xBFFFu && !p.game && p.loram && p.hiram) return ReadTarget::Cartridge; // ROMH (16K)
        // Fall through to the normal decode for all other regions.
    }

    if (a >= 0xA000u && a <= 0xBFFFu) return (p.loram && p.hiram) ? ReadTarget::BasicRom : ReadTarget::Ram;
    if (a >= 0xD000u && a <= 0xDFFFu) {
        if (p.hiram || p.loram) {
            if (p.charen) return decodeIoReadRegion_(a);
            return ReadTarget::CharRom;
        }
        return ReadTarget::Ram;
    }
    if (a >= 0xE000u) return p.hiram ? ReadTarget::KernalRom : ReadTarget::Ram;
    return ReadTarget::Ram;
}

inline WriteTarget decodeCpuWrite(uint16_t a, const PlaState& p) noexcept {
    if (a <= 0x0001u) return WriteTarget::ProcessorPort;
    // IO is write-visible in both normal and Ultimax modes when charen selects
    // it. In Ultimax the (hiram||loram) gate does not apply, but for SID/PSID/
    // RSID playback the lines are always high so this reduces to the original.
    const bool ultimax = (!p.game && p.exrom);
    const bool ioVisible = (a >= 0xD000u && a <= 0xDFFFu) &&
                           (ultimax ? p.charen : ((p.hiram || p.loram) && p.charen));
    if (ioVisible) {
        if (a <= 0xD3FFu) return WriteTarget::Vic;
        if (a <= 0xD7FFu) return WriteTarget::Sid;
        if (a <= 0xDBFFu) return WriteTarget::ColorRam;
        if (a <= 0xDCFFu) return WriteTarget::Cia1;
        if (a <= 0xDDFFu) return WriteTarget::Cia2;
        return WriteTarget::CartridgeIo;
    }
    return WriteTarget::Ram;
}

class MemoryMatrix {
public:
    static constexpr size_t kDirtyWriteLogSize = 4096;
    void attach(ProcessorPort6510* port,
                OpenBusLatch* openBus,
                ISidRegisterWriteSink* sidSink,
                Cia6526* cia1 = nullptr,
                Cia6526* cia2 = nullptr,
                VicII* vic = nullptr) noexcept {
        port_ = port;
        openBus_ = openBus;
        sidSink_ = sidSink;
        cia1_ = cia1;
        cia2_ = cia2;
        vic_ = vic;
    }

    void configureSidBases(const uint16_t* bases, uint8_t count) noexcept {
        sidBases_.fill(0);
        sidChipCount_ = static_cast<uint8_t>(std::clamp<int>(count, 1, 5));
        sidBases_[0] = 0xD400u;
        if (!bases) return;
        for (uint8_t i = 0; i < sidChipCount_; ++i) {
            sidBases_[i] = bases[i] ? bases[i] : (i == 0u ? 0xD400u : 0u);
        }
    }

    void powerOn(bool deterministic) noexcept {
        for (size_t i = 0; i < ram_.size(); ++i) {
            ram_[i] = deterministic ? static_cast<uint8_t>((i * 37u + 0x5Au) & 0xFFu) : 0;
        }
        colorRam_.fill(0);
        basicRom_.fill(0xEAu);
        kernalRom_.fill(0xEAu);
        charRom_.fill(0xFFu);
        openBusReads_ = 0;
        ioHiddenSidStoresToRam_ = 0;
        dirtyWriteCount_ = 0;
        dirtyWriteLogHead_ = 0;
        dirtyWriteLogWrapped_ = false;
        lastDirtyAddress_ = 0xFFFFu;
        lastDirtyValue_ = 0xFFu;
        cartGameHigh_ = true;
        cartExromHigh_ = true;
    }

    uint8_t cpuRead(uint64_t phi2, uint16_t a, Phi2BusPhase& phase) noexcept {
        // cpuRead() is a complete read bus-event boundary. Callers may reuse a
        // Phi2BusPhase object across several bus operations, so clear all
        // read/write side-effect fields before decoding this read. Otherwise a
        // prior SID write, RMW write, vector fetch, or open-bus event can leak
        // into this read's diagnostics.
        phase.sidWrite = false;
        phase.sidRead = false;
        phase.sidReg = 0;
        phase.openBusSource = false;
        phase.rmwDummyWrite = false;
        phase.rmwFinalWrite = false;
        phase.rmwEventKind = RmwBusEventKind::None;
        phase.vectorFetch = false;
        phase.interruptEntry = InterruptEntryKind::None;
        const ProcessorPort6510 fallbackPort{};
        OpenBusLatch fallbackBus{};
        const ProcessorPort6510& port = port_ ? *port_ : fallbackPort;
        OpenBusLatch& bus = openBus_ ? *openBus_ : fallbackBus;
        const PlaState p = plaFromPortCart(port, cartGameHigh_, cartExromHigh_);
        const ReadTarget t = decodeCpuRead(a, p);
        uint8_t v = bus.value();
        switch (t) {
            case ReadTarget::ProcessorPort: v = port.read(a, bus.value()); break;
            case ReadTarget::Ram: v = ram_[a]; break;
            case ReadTarget::BasicRom: v = basicRom_[static_cast<size_t>(a - 0xA000u)]; break;
            case ReadTarget::KernalRom: v = kernalRom_[static_cast<size_t>(a - 0xE000u)]; break;
            case ReadTarget::CharRom: v = charRom_[static_cast<size_t>(a - 0xD000u)]; break;
            case ReadTarget::Sid:
                phase.sidRead = true;
                if (sidSink_) {
                    uint8_t encodedReg = 0;
                    if (sidAddressToEncodedReg_(a, encodedReg)) {
                        phase.sidReg = encodedReg;
                        v = sidSink_->readSidRegisterPhi2(phi2, encodedReg, bus.value());
                    } else {
                        v = bus.value();
                    }
                } else {
                    v = bus.value();
                }
                break;
            case ReadTarget::ColorRam: {
                const uint8_t low = static_cast<uint8_t>(colorRam_[static_cast<size_t>((a - 0xD800u) & 0x03FFu)] & 0x0Fu);
                v = static_cast<uint8_t>((bus.value() & 0xF0u) | low);
                break;
            }
            case ReadTarget::Vic:
                v = vic_ ? vic_->read(static_cast<uint8_t>(a & 0x3Fu)) : bus.value();
                break;
            case ReadTarget::Cia1:
                v = cia1_ ? cia1_->read(static_cast<uint8_t>(a & 0x0Fu)) : bus.value();
                break;
            case ReadTarget::Cia2:
                v = cia2_ ? cia2_->read(static_cast<uint8_t>(a & 0x0Fu)) : bus.value();
                break;
            case ReadTarget::Cartridge:
            case ReadTarget::OpenBus:
                v = bus.value();
                phase.openBusSource = true;
                ++openBusReads_;
                break;
        }
        bus.drive(v, phi2);
        phase.access = BusAccess::Read;
        phase.address = a;
        phase.dataIn = v;
        phase.data = v;
        phase.rw = true;
        return v;
    }

    void cpuWrite(uint64_t phi2, uint16_t a, uint8_t v, Phi2BusPhase& phase, bool rmwDummy = false) noexcept {
        // cpuWrite() is a complete write bus-event boundary. Callers may reuse a
        // Phi2BusPhase object across several memory writes, so clear all
        // write-specific/side-effect fields before decoding this write. Preserve
        // an explicitly supplied rmwFinalWrite request; clear stale final state
        // only when it was not requested for this call.
        const bool requestedRmwFinal = phase.rmwFinalWrite ||
            phase.rmwEventKind == RmwBusEventKind::FinalWriteNewValue;
        phase.sidWrite = false;
        phase.sidRead = false;
        phase.sidReg = 0;
        phase.openBusSource = false;
        phase.rmwDummyWrite = rmwDummy;
        phase.rmwFinalWrite = (!rmwDummy && requestedRmwFinal);
        phase.rmwEventKind = rmwDummy ? RmwBusEventKind::DummyWriteOldValue
                                      : (requestedRmwFinal ? RmwBusEventKind::FinalWriteNewValue
                                                           : RmwBusEventKind::None);
        const ProcessorPort6510 fallbackPort{};
        const ProcessorPort6510& port = port_ ? *port_ : fallbackPort;
        const PlaState p = plaFromPortCart(port, cartGameHigh_, cartExromHigh_);
        const WriteTarget t = decodeCpuWrite(a, p);
        switch (t) {
            case WriteTarget::ProcessorPort:
                if (port_) port_->write(a, v);
                ram_[a] = v;
                break;
            case WriteTarget::Ram:
                if (a >= 0xD400u && a <= 0xD7FFu && !((p.hiram || p.loram) && p.charen)) ++ioHiddenSidStoresToRam_;
                ram_[a] = v;
                break;
            case WriteTarget::Sid: {
                uint8_t encodedReg = 0;
                if (sidAddressToEncodedReg_(a, encodedReg)) {
                    const uint8_t localReg = static_cast<uint8_t>(encodedReg & 0x1Fu);
                    if (c64SidRegWriteable(localReg)) {
                        if (sidSink_) sidSink_->writeSidRegisterPhi2(phi2, encodedReg, v, rmwDummy);
                        phase.sidWrite = true;
                        phase.sidReg = encodedReg;
                    } else {
                        ++droppedSidHoleWrites_;
                        phase.sidWrite = false;
                        phase.sidReg = encodedReg;
                    }
                }
                break;
            }
            case WriteTarget::ColorRam:
                colorRam_[static_cast<size_t>((a - 0xD800u) & 0x03FFu)] = static_cast<uint8_t>(v & 0x0Fu);
                break;
            case WriteTarget::Vic:
                if (vic_) vic_->write(static_cast<uint8_t>(a & 0x3Fu), v);
                break;
            case WriteTarget::Cia1:
                if (cia1_) cia1_->write(static_cast<uint8_t>(a & 0x0Fu), v);
                break;
            case WriteTarget::Cia2:
                if (cia2_) cia2_->write(static_cast<uint8_t>(a & 0x0Fu), v);
                if (vic_ && ((a & 0x0Fu) == 0x00u || (a & 0x0Fu) == 0x02u)) {
                    // CIA2 PA0/PA1 are board wires into VIC bank select. Use the
                    // side-effect-free pin view, not a generic CPU read, so bank
                    // updates remain a physical port-latch/DDR operation.
                    const uint8_t pra = cia2_ ? cia2_->portAPins() : v;
                    vic_->setMemoryBank(static_cast<uint8_t>(pra & 0x03u));
                }
                break;
            case WriteTarget::CartridgeIo:
                break;
        }
        recordDirtyWrite_(phi2, a, v, phase.rmwEventKind);
        if (openBus_) openBus_->drive(v, phi2);
        phase.access = BusAccess::Write;
        phase.address = a;
        phase.dataOut = v;
        phase.data = v;
        phase.rw = false;
        phase.rmwDummyWrite = rmwDummy;
    }

    uint8_t peekRam(uint16_t a) const noexcept { return ram_[a]; }
    // Color RAM ($D800-$DBFF) is a distinct 4-bit array, NOT part of ram_. A
    // PHI2 write to this range lands in colorRam_ (see WriteTarget::ColorRam) and
    // leaves ram_ untouched, so peekRam() would return a stale byte. Callers that
    // mirror PHI2 color-RAM writes back to the platform must read the value from
    // here, not peekRam().
    uint8_t peekColorRam(uint16_t a) const noexcept {
        return static_cast<uint8_t>(colorRam_[static_cast<size_t>((a - 0xD800u) & 0x03FFu)] & 0x0Fu);
    }
    uint8_t peekKernalRom(uint16_t a) const noexcept {
        return (a >= 0xE000u) ? kernalRom_[static_cast<size_t>(a - 0xE000u)] : 0xFFu;
    }
    void pokeRam(uint16_t a, uint8_t v) noexcept { ram_[a] = v; }
    void pokeBasicRom(uint16_t a, uint8_t v) noexcept {
        if (a >= 0xA000u && a <= 0xBFFFu) basicRom_[static_cast<size_t>(a - 0xA000u)] = v;
    }
    void pokeKernalRom(uint16_t a, uint8_t v) noexcept {
        if (a >= 0xE000u) kernalRom_[static_cast<size_t>(a - 0xE000u)] = v;
    }
    void pokeCharRom(uint16_t a, uint8_t v) noexcept {
        if (a >= 0xD000u && a <= 0xDFFFu) charRom_[static_cast<size_t>(a - 0xD000u)] = v;
    }
    void pokeColorRam(uint16_t a, uint8_t v) noexcept {
        colorRam_[static_cast<size_t>((a - 0xD800u) & 0x03FFu)] = static_cast<uint8_t>(v & 0x0Fu);
    }

    uint64_t openBusReads() const noexcept { return openBusReads_; }
    uint64_t ioHiddenSidStoresToRam() const noexcept { return ioHiddenSidStoresToRam_; }
    uint64_t droppedSidHoleWrites() const noexcept { return droppedSidHoleWrites_; }
    uint64_t dirtyWriteCount() const noexcept { return dirtyWriteCount_; }
    uint16_t lastDirtyAddress() const noexcept { return lastDirtyAddress_; }
    uint8_t lastDirtyValue() const noexcept { return lastDirtyValue_; }
    bool dirtyWriteLogWrapped() const noexcept { return dirtyWriteLogWrapped_; }
    size_t dirtyWriteLogSize() const noexcept { return dirtyWriteLogWrapped_ ? kDirtyWriteLogSize : dirtyWriteLogHead_; }
    C64DirtyWrite dirtyWriteAt(size_t index) const noexcept {
        const size_t n = dirtyWriteLogSize();
        if (index >= n) return C64DirtyWrite{};
        const size_t physical = dirtyWriteLogWrapped_
            ? ((dirtyWriteLogHead_ + index) % kDirtyWriteLogSize)
            : index;
        return dirtyWriteLog_[physical];
    }
    bool consumeDirtyWriteLogWrapped() noexcept {
        const bool wrapped = dirtyWriteLogWrapped_;
        dirtyWriteLogWrapped_ = false;
        return wrapped;
    }
    void clearDirtyWriteLog() noexcept {
        dirtyWriteLogHead_ = 0u;
        dirtyWriteLogWrapped_ = false;
    }

    // Cartridge control lines (active-low; "high"==true==not asserted). Both
    // high (the default) is the normal no-cartridge C64 map. Setting these lets
    // the PHI2 matrix honor 8K/16K/Ultimax banking instead of assuming none.
    void setCartridgeLines(bool gameHigh, bool exromHigh) noexcept {
        cartGameHigh_ = gameHigh;
        cartExromHigh_ = exromHigh;
    }
    bool cartridgeGameHigh() const noexcept { return cartGameHigh_; }
    bool cartridgeExromHigh() const noexcept { return cartExromHigh_; }

private:
    void recordDirtyWrite_(uint64_t phi2, uint16_t a, uint8_t v, RmwBusEventKind kind) noexcept {
        ++dirtyWriteCount_;
        lastDirtyAddress_ = a;
        lastDirtyValue_ = v;
        const bool rmwDummy = (kind == RmwBusEventKind::DummyWriteOldValue);
        dirtyWriteLog_[dirtyWriteLogHead_] = C64DirtyWrite{a, v, phi2, rmwDummy, kind};
        dirtyWriteLogHead_ = (dirtyWriteLogHead_ + 1u) % kDirtyWriteLogSize;
        if (dirtyWriteLogHead_ == 0u) dirtyWriteLogWrapped_ = true;
    }

    bool sidAddressToEncodedReg_(uint16_t a, uint8_t& encodedReg) const noexcept {
        // Exact configured SID bases win first. This lets a second/third SID at
        // $D420/$D500/etc. receive its own 32-byte register window even though
        // a real single SID would mirror every $20 inside $D400-$D7FF.
        for (uint8_t i = 0; i < sidChipCount_; ++i) {
            const uint16_t base = sidBases_[i];
            if (base != 0u && a >= base && a < static_cast<uint16_t>(base + 0x20u)) {
                encodedReg = static_cast<uint8_t>(i * 32u + static_cast<uint8_t>(a - base));
                return true;
            }
        }

        // Physical C64 SID decode mirrors the primary SID every $20 throughout
        // $D400-$D7FF. Earlier code only enabled this fallback when exactly one
        // SID was configured, which made primary $D418 mirror writes such as
        // $D458/$D478 disappear as soon as a multi-SID configuration existed.
        // Keep the fallback after exact-base matching so explicit extra SID
        // windows still override the primary mirror at their configured bases.
        if (sidBases_[0] == 0xD400u && a >= 0xD400u && a <= 0xD7FFu) {
            encodedReg = static_cast<uint8_t>(a & 0x1Fu);
            return true;
        }
        return false;
    }

    std::array<uint8_t, 65536> ram_{};
    std::array<uint8_t, 8192> basicRom_{};
    std::array<uint8_t, 8192> kernalRom_{};
    std::array<uint8_t, 4096> charRom_{};
    std::array<uint8_t, 1024> colorRam_{};
    ProcessorPort6510* port_ = nullptr;
    OpenBusLatch* openBus_ = nullptr;
    ISidRegisterWriteSink* sidSink_ = nullptr;
    Cia6526* cia1_ = nullptr;
    Cia6526* cia2_ = nullptr;
    VicII* vic_ = nullptr;
    std::array<uint16_t, 5> sidBases_{{0xD400u, 0u, 0u, 0u, 0u}};
    uint8_t sidChipCount_ = 1;
    bool cartGameHigh_ = true;   // GAME line not asserted (no cartridge)
    bool cartExromHigh_ = true;  // EXROM line not asserted (no cartridge)
    uint64_t openBusReads_ = 0;
    uint64_t ioHiddenSidStoresToRam_ = 0;
    uint64_t droppedSidHoleWrites_ = 0;
    uint64_t dirtyWriteCount_ = 0;
    std::array<C64DirtyWrite, kDirtyWriteLogSize> dirtyWriteLog_{};
    size_t dirtyWriteLogHead_ = 0;
    bool dirtyWriteLogWrapped_ = false;
    uint16_t lastDirtyAddress_ = 0xFFFFu;
    uint8_t lastDirtyValue_ = 0xFFu;
};

} // namespace ArpSID::C64
