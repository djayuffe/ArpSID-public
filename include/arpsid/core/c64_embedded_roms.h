// SPDX-License-Identifier: BSD-3-Clause
// c64_embedded_roms.h — Commodore C64 ROM identity constants and placeholders.
//
// PUBLIC DISTRIBUTION: the Commodore KERNAL (901227-03), BASIC (901226-01) and
// CHARGEN (901225-01) ROM images are copyrighted and are NOT bundled with this
// source. The byte arrays below are zero-filled placeholders that keep the API
// and sizes intact so everything compiles; kEmbeddedC64RomsAvailable is false,
// and c64LoadEmbeddedStockRoms() (c64_embedded_rom_loader.h) therefore installs
// nothing, leaving the C64 player in its documented MissingRealRoms /
// RomIdentityUnverified state. Supply your own dumps at runtime to run real
// tunes. The CRC32 identity constants are retained so a user-supplied dump that
// matches stock is still auto-promoted to KnownStock by the identity table.
#pragma once

#include <cstddef>

namespace ArpSID { namespace C64 {

// False in this public build: no real ROM bytes are embedded.
inline constexpr bool kEmbeddedC64RomsAvailable = false;

// kernal.bin: 8192 bytes, CRC32 0xDBE3E7C7
inline constexpr unsigned int kEmbeddedC64KernalCrc32 = 0xDBE3E7C7u;
inline constexpr std::size_t kEmbeddedC64KernalSize = 8192u;
inline constexpr unsigned char kEmbeddedC64Kernal[8192] = {0};
// basic.bin: 8192 bytes, CRC32 0xF833D117
inline constexpr unsigned int kEmbeddedC64BasicCrc32 = 0xF833D117u;
inline constexpr std::size_t kEmbeddedC64BasicSize = 8192u;
inline constexpr unsigned char kEmbeddedC64Basic[8192] = {0};
// chargen.bin: 4096 bytes, CRC32 0xEC4272EE
inline constexpr unsigned int kEmbeddedC64CharacterCrc32 = 0xEC4272EEu;
inline constexpr std::size_t kEmbeddedC64CharacterSize = 4096u;
inline constexpr unsigned char kEmbeddedC64Character[4096] = {0};

} } // namespace ArpSID::C64
