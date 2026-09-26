// SPDX-License-Identifier: see project license
// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_embedded_rom_loader.h — loads the embedded verified stock C64 ROMs into a
// C64Platform. The CRC32 of each image matches c64_pla.h's known-stock identity
// table, so C64RomSet::loadX() auto-promotes trust to KnownStock, giving
// hasCompleteExternalRomSet()==true and hasVerifiedStockRomSet()==true. This
// clears the MissingRealRoms and RomIdentityUnverified physical-exactness
// blockers and lets the C64 core execute against real ROM code.
#pragma once

#include "c64_embedded_roms.h"
#include "c64_platform.h"

namespace ArpSID {
namespace C64 {

// Load all three embedded stock ROMs into `platform`. Safe to call on any thread
// at setup/load time (not the realtime audio callback — it copies 20 KB). Returns
// true iff the platform now reports a fully verified stock ROM set.
inline bool c64LoadEmbeddedStockRoms(C64Platform& platform) noexcept {
    // Public build: no ROM bytes are bundled (see c64_embedded_roms.h). Install
    // nothing and leave the platform in its MissingRealRoms/unverified state so
    // the caller can fall back to user-supplied ROMs.
    if (!kEmbeddedC64RomsAvailable) return false;
    platform.loadKernalRom(kEmbeddedC64Kernal, kEmbeddedC64KernalSize);
    platform.loadBasicRom(kEmbeddedC64Basic, kEmbeddedC64BasicSize);
    platform.loadCharacterRom(kEmbeddedC64Character, kEmbeddedC64CharacterSize);
    return platform.hasVerifiedStockRomSet();
}

} // namespace C64
} // namespace ArpSID
