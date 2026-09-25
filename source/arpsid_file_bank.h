#pragma once
// arpsid_file_bank.h — ArpSID file-based patch bank persistence
//
// Provides save/load for individual patches (.arpsid) and full banks (.arpsidbank).
// All I/O is synchronous, lock-free, and may be called from any non-RT thread.
// Binary format is platform-independent (little-endian, no padding).
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#include "arpsid/core/sid_state_codec.h"
#include "arpsid/core/sid_serializer_schema.h"
#include "arpsid/patchbank/forensic_patch_bank.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ArpSID {

// ─── File format constants ────────────────────────────────────────────────────
static constexpr uint32_t kFileBankMagic0    = 0x53504441u; // "ADPS"
static constexpr uint32_t kFileBankMagic1    = 0x4B4E4142u; // "BANK"
static constexpr uint32_t kFileBankVersion   = 1u;
static constexpr uint32_t kFilePatchMagic    = 0x48435450u; // "PTCH"
static constexpr uint32_t kFilePatchVersion  = 1u;
static constexpr int      kFileBankMaxSlots  = 128;
static constexpr size_t   kFilePatchBlobCap  = 65536u;   // max serialized blob

// ─── Patch metadata (persisted in file headers) ───────────────────────────────
struct ArpSIDFilePatchMeta {
    char     name[64]     = {};   // display name, null-terminated
    char     author[32]   = {};   // author string
    char     category[32] = {};   // e.g. "Bass", "Lead", "Drum"
    char     tags[64]     = {};   // space-separated keywords
    uint8_t  chip_model   = 0;    // 0=6581, 1=8580, 2=portable
    uint8_t  clock_system = 0;    // 0=PAL, 1=NTSC
    uint8_t  forensic_en  = 0;    // 1=forensic mode defaults active
    uint8_t  role         = 0;    // PatchRole ordinal
    uint8_t  pad[12]      = {};
};

// ─── Progress callback (optional) ─────────────────────────────────────────────
using BankProgressFn = std::function<void(int loaded, int total, const char* name)>;

// ─── Error codes ──────────────────────────────────────────────────────────────
enum class FileBankError : int {
    OK = 0,
    FileNotFound,
    OpenFailed,
    ReadFailed,
    WriteFailed,
    BadMagic,
    BadVersion,
    BadBlob,
    TooManySlots,
    BlobTooLarge,
    DirectoryCreateFailed,
    Unknown
};
const char* fileBankErrorString(FileBankError e) noexcept;

// ─── Main API ─────────────────────────────────────────────────────────────────
class ArpSIDFileBank {
public:
    // ── Single-patch file I/O (.arpsid) ──────────────────────────────────────
    // Save one patch (state root + metadata) to path.
    static FileBankError savePatchToFile(const std::string& path,
                                         const SidStateRootV1& root,
                                         const ArpSIDFilePatchMeta& meta) noexcept;

    // Load one patch from path. Returns OK and fills root/meta on success.
    static FileBankError loadPatchFromFile(const std::string& path,
                                            SidStateRootV1& root,
                                            ArpSIDFilePatchMeta& meta) noexcept;

    // ── Bank file I/O (.arpsidbank, up to 128 slots) ─────────────────────────
    // Save all patches. patches.size() must equal metas.size() and be <= 128.
    static FileBankError saveBankToFile(const std::string& path,
                                         const std::vector<SidStateRootV1>& patches,
                                         const std::vector<ArpSIDFilePatchMeta>& metas) noexcept;

    // Load bank from file. patches/metas are appended or resized to match file.
    static FileBankError loadBankFromFile(const std::string& path,
                                           std::vector<SidStateRootV1>& patches,
                                           std::vector<ArpSIDFilePatchMeta>& metas,
                                           BankProgressFn progress = {}) noexcept;

    // ── Directory export: one .arpsid per slot ────────────────────────────────
    // Creates dir if it does not exist. Names files as "NNN_PatchName.arpsid".
    static FileBankError exportAllToDirectory(const std::string& dir,
                                               const std::vector<SidStateRootV1>& patches,
                                               const std::vector<ArpSIDFilePatchMeta>& metas) noexcept;

    // ── Factory bank convenience ───────────────────────────────────────────────
    // Build ArpSIDFilePatchMeta from a PatchDefinition.
    static ArpSIDFilePatchMeta metaFromDefinition(const PatchDefinition& def) noexcept;

    // Save the built-in factory patches to a bank file.
    static FileBankError saveFactoryBankToFile(const std::string& path) noexcept;

    // ── Platform default paths ─────────────────────────────────────────────────
    // macOS: ~/Music/ArpSID/
    // Windows: %%USERPROFILE%%\Documents\ArpSID (no trailing backslash in this comment)
    static std::string defaultUserBankDirectory() noexcept;
    static std::string defaultUserBankFilePath()   noexcept;  // .arpsidbank
    static std::string defaultFactoryBankFilePath() noexcept; // factory-read-only copy

    // ── Path utilities ─────────────────────────────────────────────────────────
    static bool        ensureDirectoryExists(const std::string& path) noexcept;
    static bool        fileExists(const std::string& path) noexcept;
    static std::string sanitizeFilename(const std::string& name) noexcept;
    static std::string joinPath(const std::string& dir, const std::string& file) noexcept;

    // File extension constants
    static constexpr const char* kPatchExt = ".arpsid";
    static constexpr const char* kBankExt  = ".arpsidbank";

private:
    // Internal wire helpers
    static bool writeLe32(std::FILE* f, uint32_t v) noexcept;
    static bool readLe32 (std::FILE* f, uint32_t& v) noexcept;
    static bool writeBuf (std::FILE* f, const void* buf, size_t n) noexcept;
    static bool readBuf  (std::FILE* f, void* buf, size_t n) noexcept;
};

} // namespace ArpSID
