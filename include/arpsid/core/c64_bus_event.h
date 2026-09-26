// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_bus_event.h - cycle-tagged C64 bus event contract for SID/DIGI authority.

#pragma once

#include <cstdint>

namespace ArpSID::C64 {

// Enumerates the types of accesses that can occur on the C64 bus. These
// differentiate between reads, writes, read-modify-write cycles and DMA. See
// the 6510 datasheet for cycle diagrams; these enums encode those states.
enum class C64BusAccess : std::uint8_t {
    Read           = 0,
    Write          = 1,
    RmwRead        = 2,
    RmwDummyWrite  = 3,
    RmwFinalWrite  = 4,
    Dma            = 5,
    Refresh        = 6,
    Idle           = 7,
};

// Sources that can drive the C64 bus. Important for debugging when multiple
// subsystems contend for the bus: CPU, VIC, CIA, cartridge, SID runtime, the
// new DIGI virtual device, host automation or debugger.
enum class C64BusSource : std::uint8_t {
    CPU6510        = 0,
    VICII          = 1,
    CIA1           = 2,
    CIA2           = 3,
    Cartridge      = 4,
    SidRuntime     = 5,
    DigiVirtualDevice = 6,
    HostAutomation = 7,
    Debugger       = 8,
};

// Represents a single bus event tagged with the PHI2 cycle. These events
// record the address, data, access type and source along with flags used at
// apply time to indicate whether IO was visible, whether the VIC stole the
// cycle, whether the open bus was driven and whether the SID accepted the
// write without overflowing its queue.
struct C64BusEvent final {
    std::uint64_t phi2Cycle = 0;
    std::uint16_t address   = 0;
    std::uint8_t  data      = 0;
    C64BusAccess access     = C64BusAccess::Idle;
    C64BusSource source     = C64BusSource::CPU6510;
    bool ioVisible = true;
    bool vicStolen = false;
    bool openBusDriven = false;
    bool sidAccepted = false;
};

// Returns true when the given address lies in the SID IO region ($D400–$D7FF).
inline bool c64IsSidAddress(std::uint16_t a) noexcept {
    return a >= 0xD400u && a <= 0xD7FFu;
}

// Returns the SID chip index (0..2) for the given address. ArpSID uses
// conventional mirrors at $D420 and $D440 for multi-SID modes.
inline std::uint8_t c64SidChipForAddress(std::uint16_t a) noexcept {
    if (a >= 0xD440u && a <= 0xD45Fu) return 2u;
    if (a >= 0xD420u && a <= 0xD43Fu) return 1u;
    return 0u;
}

// Returns the SID register index (0..31) for the given address. The lower
// five bits of the address map directly to the register number.
inline std::uint8_t c64SidRegForAddress(std::uint16_t a) noexcept {
    return static_cast<std::uint8_t>(a & 0x1Fu);
}

// Determines whether IO is visible at $D000–$DFFF based on the 6510 port
// register at $0001. When IO is not visible, writes to $D418 are blocked.
inline bool c64D000IoVisibleFromPort01(std::uint8_t port01) noexcept {
    const bool lram  = (port01 & 0x01u) != 0u;
    const bool hram  = (port01 & 0x02u) != 0u;
    const bool charen = (port01 & 0x04u) != 0u;
    return charen && (lram || hram);
}

// Constructs a new $D418 value by preserving the upper nibble of oldD418 and
// inserting the 4‑bit sample nibble into the lower nibble. Authentic DIGI
// writes must call this helper to avoid destroying filter or voice‑off bits.
inline std::uint8_t makeD418DigiValue(std::uint8_t oldD418,
                                      std::uint8_t nibble) noexcept {
    return static_cast<std::uint8_t>((oldD418 & 0xF0u) | (nibble & 0x0Fu));
}

} // namespace ArpSID::C64