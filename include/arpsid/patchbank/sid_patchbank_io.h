#pragma once
// sid_patchbank_io.h — ArpSID patch bank file I/O
//
// Two file formats:
//
// .arpatch — single patch
// [0..11] magic "ARPSIDPATCH\0" (12 bytes)
// [12..15] version uint32_t LE = 1
// [16..79] patch name, 64-byte null-padded UTF-8
// [80..83] blob length uint32_t LE
// [84..] SidStateRootV1 binary blob
//
// .arpbank — v1 user-bank-compatible bank of up to 128 patches
// [0..11] magic "ARPSIDBANK\0\0" (12 bytes)
// [12..15] version uint32_t LE = 1
// [16..79] bank name, 64-byte null-padded UTF-8
// [80..83] patch count uint32_t LE
// For each patch:
// [+0..+3] blob length uint32_t LE
// [+4..+67] patch name 64-byte null-padded UTF-8
// [+68..] SidStateRootV1 binary blob (blob_length bytes)
//
// All integer fields are little-endian.
// Copyright (C) 2024-2026 Ulf Bertilsson. MIT License.

#include "arpsid/core/sid_serializer_schema.h"
#include <cstdint>
#include <cstring>

namespace ArpSID {

// ─── File extension constants ──────────────────────────────────────────────
static constexpr const char* kPatchFileExtension = ".arpatch";
static constexpr const char* kBankFileExtension  = ".arpbank";
static constexpr int         kBankMaxPatches      = 128;

// ─── Single patch save / load ──────────────────────────────────────────────

/// Save one SidStateRootV1 to an .arpatch file.
/// @param state Canonical state to serialize.
/// @param filePath Destination file path (will be created/overwritten).
/// @param patchName Optional display name embedded in the file (≤63 chars).
/// @return true on success, false on I/O error or encode failure.
bool savePatchToFile(const SidStateRootV1& state,
                     const char* filePath,
                     const char* patchName = nullptr) noexcept;

/// Load one .arpatch file into a SidStateRootV1.
/// @param filePath Source file path.
/// @param outState Receives the decoded state.
/// @param outName Optional 64-byte buffer that receives the patch name.
/// @return true on success, false on I/O / decode error.
bool loadPatchFromFile(const char* filePath,
                       SidStateRootV1& outState,
                       char outName[64] = nullptr) noexcept;

// ─── Bank save / load ──────────────────────────────────────────────────────

struct PatchBankFile {
    char bankName[64] = {};
    int  patchCount   = 0;
    struct PatchSlot {
        char         name[64] = {};
        SidStateRootV1 state{};
        bool         valid    = false;
    } patches[kBankMaxPatches];

    void clear() noexcept {
        std::memset(bankName, 0, sizeof(bankName));
        patchCount = 0;
        for (auto& p : patches) { std::memset(p.name, 0, sizeof(p.name)); p.valid = false; }
    }
};

/// Save a PatchBankFile to an .arpbank file.
bool saveBankToFile(const PatchBankFile& bank, const char* filePath) noexcept;

/// Load an .arpbank file into a PatchBankFile.
bool loadBankFromFile(const char* filePath, PatchBankFile& outBank) noexcept;

// ─── Factory bank export ───────────────────────────────────────────────────

/// Export the built-in factory bank as a legacy .arpbank v1 128-slot user-bank-compatible subset, not the full 180-slot factory. Canonical factory slots 128..179 require state-root/.arpatch/v2 export.
bool exportFactoryBankToFile(const char* filePath) noexcept;

} // namespace ArpSID
