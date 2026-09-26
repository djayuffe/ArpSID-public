// arpsid_file_bank.cpp — ArpSID file-based patch bank persistence
// Complete file I/O: single patches (.arpsid) and banks (.arpsidbank).
// All formats are little-endian binary, self-describing with magic + version.
//
// PATCH FILE FORMAT (.arpsid):
// [0..3] Magic: kFilePatchMagic (0x48435450 "PTCH")
// [4..7] Version: kFilePatchVersion (1)
// [8..135] ArpSIDFilePatchMeta (128 bytes, fixed)
// [136..139] Blob size: uint32_le
// [140..] Binary state blob (SidBinaryStateHeader + params)
//
// BANK FILE FORMAT (.arpsidbank):
// [0..3] Magic0: kFileBankMagic0 "ADPS"
// [4..7] Magic1: kFileBankMagic1 "BANK"
// [8..11] Version: kFileBankVersion (1)
// [12..15] Slot count: uint32_le (1..128)
// For each slot:
// [+0..+3] Slot index: uint32_le
// [+4..+131] ArpSIDFilePatchMeta (128 bytes)
// [+132..+135] Blob size: uint32_le
// [+136..] Binary state blob
//
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT

#include "arpsid_file_bank.h"
#include "arpsid/core/sid_state_codec.h"
#include "factory_patch_params.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cassert>

#if defined(_WIN32)
#  include <windows.h>
#  include <shlobj.h>
#  include <direct.h>
#  define ARPSID_PATH_SEP "\\"
#elif defined(__APPLE__) || defined(__linux__)
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <pwd.h>
#  include <unistd.h>
#  define ARPSID_PATH_SEP "/"
#else
#  define ARPSID_PATH_SEP "/"
#endif

namespace ArpSID {

namespace {
static constexpr size_t kMetaWireSizeV1 = 128u;
struct FilePatchMetaWireV1 {
    char name[48]{};
    char author[24]{};
    char category[16]{};
    char tags[32]{};
    uint8_t chip_model = 0;
    uint8_t clock_system = 0;
    uint8_t forensic_en = 0;
    uint8_t role = 0;
    uint8_t reserved[4]{};
};
static_assert(sizeof(FilePatchMetaWireV1) == kMetaWireSizeV1, "wire meta must be 128 bytes");

static void copyBoundedField_(char* dst, size_t dstSize, const char* src) noexcept {
    if (!dst || dstSize == 0) return;
    std::memset(dst, 0, dstSize);
    if (!src) return;
    const size_t srcLen = std::strlen(src);
    const size_t copy = srcLen < (dstSize - 1u) ? srcLen : (dstSize - 1u);
    std::memcpy(dst, src, copy);
    // dst[copy] is already '\0' from the memset above
}

static FilePatchMetaWireV1 encodeMetaWireV1_(const ArpSIDFilePatchMeta& meta) noexcept {
    FilePatchMetaWireV1 wire{};
    copyBoundedField_(wire.name, sizeof(wire.name), meta.name);
    copyBoundedField_(wire.author, sizeof(wire.author), meta.author);
    copyBoundedField_(wire.category, sizeof(wire.category), meta.category);
    copyBoundedField_(wire.tags, sizeof(wire.tags), meta.tags);
    wire.chip_model = meta.chip_model;
    wire.clock_system = meta.clock_system;
    wire.forensic_en = meta.forensic_en;
    wire.role = meta.role;
    return wire;
}

static ArpSIDFilePatchMeta decodeMetaWireV1_(const FilePatchMetaWireV1& wire) noexcept {
    ArpSIDFilePatchMeta meta{};
    copyBoundedField_(meta.name, sizeof(meta.name), wire.name);
    copyBoundedField_(meta.author, sizeof(meta.author), wire.author);
    copyBoundedField_(meta.category, sizeof(meta.category), wire.category);
    copyBoundedField_(meta.tags, sizeof(meta.tags), wire.tags);
    meta.chip_model = wire.chip_model;
    meta.clock_system = wire.clock_system;
    meta.forensic_en = wire.forensic_en;
    meta.role = wire.role;
    return meta;
}

static std::string parentDirectoryOf_(const std::string& path) {
    const size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return {};
    return path.substr(0, pos);
}
} // namespace

// ─── Error strings ────────────────────────────────────────────────────────────
const char* fileBankErrorString(FileBankError e) noexcept {
    switch (e) {
        case FileBankError::OK:                   return "OK";
        case FileBankError::FileNotFound:         return "File not found";
        case FileBankError::OpenFailed:           return "Failed to open file";
        case FileBankError::ReadFailed:           return "Read error";
        case FileBankError::WriteFailed:          return "Write error";
        case FileBankError::BadMagic:             return "Bad magic bytes (not an ArpSID file)";
        case FileBankError::BadVersion:           return "Unsupported file version";
        case FileBankError::BadBlob:              return "Corrupt state blob";
        case FileBankError::TooManySlots:         return "Too many slots in bank";
        case FileBankError::BlobTooLarge:         return "Blob exceeds maximum size";
        case FileBankError::DirectoryCreateFailed:return "Failed to create directory";
        case FileBankError::Unknown:
        default:                                  return "Unknown error";
    }
}

// ─── Wire I/O helpers ─────────────────────────────────────────────────────────
bool ArpSIDFileBank::writeLe32(std::FILE* f, uint32_t v) noexcept {
    const uint8_t buf[4] = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 24) & 0xFF)
    };
    return std::fwrite(buf, 1, 4, f) == 4;
}

bool ArpSIDFileBank::readLe32(std::FILE* f, uint32_t& v) noexcept {
    uint8_t buf[4];
    if (std::fread(buf, 1, 4, f) != 4) return false;
    v = static_cast<uint32_t>(buf[0])
      | (static_cast<uint32_t>(buf[1]) << 8)
      | (static_cast<uint32_t>(buf[2]) << 16)
      | (static_cast<uint32_t>(buf[3]) << 24);
    return true;
}

bool ArpSIDFileBank::writeBuf(std::FILE* f, const void* buf, size_t n) noexcept {
    if (n == 0) return true;
    return std::fwrite(buf, 1, n, f) == n;
}

bool ArpSIDFileBank::readBuf(std::FILE* f, void* buf, size_t n) noexcept {
    if (n == 0) return true;
    return std::fread(buf, 1, n, f) == n;
}

// ─── Single-patch: save ───────────────────────────────────────────────────────
FileBankError ArpSIDFileBank::savePatchToFile(const std::string& path,
                                               const SidStateRootV1& root,
                                               const ArpSIDFilePatchMeta& meta) noexcept {
    // Encode state blob first (fail fast before opening file)
    const size_t blobCap = encodedSidStateRootBinarySize(root);
    if (blobCap == 0 || blobCap > kFilePatchBlobCap) return FileBankError::BlobTooLarge;
    std::vector<uint8_t> blob(blobCap);
    const size_t blobLen = encodeSidStateRootBinary(root, blob.data(), blobCap);
    if (blobLen == 0) return FileBankError::BadBlob;

    const std::string parentDir = parentDirectoryOf_(path);
    if (!parentDir.empty() && !ensureDirectoryExists(parentDir)) return FileBankError::DirectoryCreateFailed;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return FileBankError::OpenFailed;

    bool ok = true;
    ok = ok && writeLe32(f, kFilePatchMagic);
    ok = ok && writeLe32(f, kFilePatchVersion);
    const FilePatchMetaWireV1 metaWire = encodeMetaWireV1_(meta);
    ok = ok && writeBuf(f, &metaWire, sizeof(metaWire));
    ok = ok && writeLe32(f, static_cast<uint32_t>(blobLen));
    ok = ok && writeBuf(f, blob.data(), blobLen);

    std::fclose(f);
    return ok ? FileBankError::OK : FileBankError::WriteFailed;
}

// ─── Single-patch: load ───────────────────────────────────────────────────────
FileBankError ArpSIDFileBank::loadPatchFromFile(const std::string& path,
                                                 SidStateRootV1& root,
                                                 ArpSIDFilePatchMeta& meta) noexcept {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return FileBankError::FileNotFound;

    uint32_t magic = 0, version = 0;
    bool ok = readLe32(f, magic) && readLe32(f, version);
    if (!ok) { std::fclose(f); return FileBankError::ReadFailed; }
    if (magic != kFilePatchMagic) { std::fclose(f); return FileBankError::BadMagic; }
    if (version > kFilePatchVersion) { std::fclose(f); return FileBankError::BadVersion; }

    FilePatchMetaWireV1 metaWire{};
    ok = readBuf(f, &metaWire, sizeof(metaWire));
    if (!ok) { std::fclose(f); return FileBankError::ReadFailed; }
    meta = decodeMetaWireV1_(metaWire);

    uint32_t blobLen = 0;
    ok = readLe32(f, blobLen);
    if (!ok) { std::fclose(f); return FileBankError::ReadFailed; }
    if (blobLen == 0 || blobLen > kFilePatchBlobCap) {
        std::fclose(f);
        return FileBankError::BlobTooLarge;
    }

    std::vector<uint8_t> blob(blobLen);
    ok = readBuf(f, blob.data(), blobLen);
    std::fclose(f);
    if (!ok) return FileBankError::ReadFailed;

    SidStateRootV1 decoded{};
    if (!decodeSidStateRootBinary(blob.data(), blobLen, decoded))
        return FileBankError::BadBlob;

    root = decoded;
    return FileBankError::OK;
}

// ─── Bank: save ───────────────────────────────────────────────────────────────
FileBankError ArpSIDFileBank::saveBankToFile(const std::string& path,
                                              const std::vector<SidStateRootV1>& patches,
                                              const std::vector<ArpSIDFilePatchMeta>& metas) noexcept {
    const size_t count = patches.size();
    if (count > static_cast<size_t>(kFileBankMaxSlots)) return FileBankError::TooManySlots;
    if (metas.size() < count) return FileBankError::BadBlob;

    const std::string parentDir = parentDirectoryOf_(path);
    if (!parentDir.empty() && !ensureDirectoryExists(parentDir)) return FileBankError::DirectoryCreateFailed;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return FileBankError::OpenFailed;

    bool ok = true;
    ok = ok && writeLe32(f, kFileBankMagic0);
    ok = ok && writeLe32(f, kFileBankMagic1);
    ok = ok && writeLe32(f, kFileBankVersion);
    ok = ok && writeLe32(f, static_cast<uint32_t>(count));

    for (size_t i = 0; i < count && ok; ++i) {
        const size_t blobCap = encodedSidStateRootBinarySize(patches[i]);
        if (blobCap == 0 || blobCap > kFilePatchBlobCap) { ok = false; break; }
        std::vector<uint8_t> blob(blobCap);
        const size_t blobLen = encodeSidStateRootBinary(patches[i], blob.data(), blobCap);
        if (blobLen == 0 || blobLen > blobCap || blobLen > kFilePatchBlobCap) { ok = false; break; }

        ok = ok && writeLe32(f, static_cast<uint32_t>(i));
        const FilePatchMetaWireV1 metaWire = encodeMetaWireV1_(metas[i]);
        ok = ok && writeBuf(f, &metaWire, sizeof(metaWire));
        ok = ok && writeLe32(f, static_cast<uint32_t>(blobLen));
        ok = ok && writeBuf(f, blob.data(), blobLen);
    }

    std::fclose(f);
    return ok ? FileBankError::OK : FileBankError::WriteFailed;
}

// ─── Bank: load ───────────────────────────────────────────────────────────────
FileBankError ArpSIDFileBank::loadBankFromFile(const std::string& path,
                                                std::vector<SidStateRootV1>& patches,
                                                std::vector<ArpSIDFilePatchMeta>& metas,
                                                BankProgressFn progress) noexcept {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return FileBankError::FileNotFound;

    uint32_t magic0 = 0, magic1 = 0, version = 0, count = 0;
    bool ok = readLe32(f, magic0) && readLe32(f, magic1) &&
              readLe32(f, version) && readLe32(f, count);
    if (!ok) { std::fclose(f); return FileBankError::ReadFailed; }
    if (magic0 != kFileBankMagic0 || magic1 != kFileBankMagic1) {
        std::fclose(f); return FileBankError::BadMagic;
    }
    if (version > kFileBankVersion) { std::fclose(f); return FileBankError::BadVersion; }
    if (count > static_cast<uint32_t>(kFileBankMaxSlots)) {
        std::fclose(f); return FileBankError::TooManySlots;
    }

    std::vector<SidStateRootV1> decodedPatches(count);
    std::vector<ArpSIDFilePatchMeta> decodedMetas(count);
    std::vector<uint8_t> seen(count, 0u);
    std::vector<uint8_t> blob;

    for (uint32_t i = 0; i < count && ok; ++i) {
        uint32_t slotIdx = 0;
        ok = readLe32(f, slotIdx);
        if (!ok) break;
        if (slotIdx >= count || seen[slotIdx]) { ok = false; break; }

        FilePatchMetaWireV1 metaWire{};
        ok = readBuf(f, &metaWire, sizeof(metaWire));
        if (!ok) break;

        uint32_t blobLen = 0;
        ok = ok && readLe32(f, blobLen);
        if (!ok) break;
        if (blobLen == 0 || blobLen > kFilePatchBlobCap) { ok = false; break; }
        blob.resize(blobLen);
        ok = readBuf(f, blob.data(), blobLen);
        if (!ok) break;

        SidStateRootV1 decoded{};
        if (!decodeSidStateRootBinary(blob.data(), blobLen, decoded)) { ok = false; break; }
        decodedPatches[slotIdx] = decoded;
        decodedMetas[slotIdx] = decodeMetaWireV1_(metaWire);
        seen[slotIdx] = 1u;

        if (progress) progress(static_cast<int>(i + 1), static_cast<int>(count), decodedMetas[slotIdx].name);
    }

    std::fclose(f);
    if (!ok) return FileBankError::BadBlob;
    for (uint32_t i = 0; i < count; ++i)
        if (!seen[i]) return FileBankError::BadBlob;

    patches = std::move(decodedPatches);
    metas = std::move(decodedMetas);
    return FileBankError::OK;
}

// ─── Directory export ─────────────────────────────────────────────────────────
FileBankError ArpSIDFileBank::exportAllToDirectory(const std::string& dir,
                                                    const std::vector<SidStateRootV1>& patches,
                                                    const std::vector<ArpSIDFilePatchMeta>& metas) noexcept {
    if (!ensureDirectoryExists(dir)) return FileBankError::DirectoryCreateFailed;
    const size_t count = std::min(patches.size(), metas.size());
    for (size_t i = 0; i < count; ++i) {
        char nameBuf[32];
        const std::string safe = sanitizeFilename(metas[i].name[0] ? metas[i].name : "Patch");
        std::snprintf(nameBuf, sizeof(nameBuf), "%03zu_", i);
        const std::string filename = std::string(nameBuf) + safe + kPatchExt;
        const std::string fullPath = joinPath(dir, filename);
        const auto err = savePatchToFile(fullPath, patches[i], metas[i]);
        if (err != FileBankError::OK) return err;
    }
    return FileBankError::OK;
}

// ─── Meta from PatchDefinition ────────────────────────────────────────────────
ArpSIDFilePatchMeta ArpSIDFileBank::metaFromDefinition(const PatchDefinition& def) noexcept {
    ArpSIDFilePatchMeta m{};
    std::strncpy(m.name,     def.displayName.c_str(), sizeof(m.name) - 1);
    std::strncpy(m.author,   "ArpSID Factory",         sizeof(m.author) - 1);
    std::strncpy(m.category, toString(def.usage.role),  sizeof(m.category) - 1);
    std::strncpy(m.tags,     def.id.c_str(),            sizeof(m.tags) - 1);
    m.chip_model   = (def.staticState.chip == SidChipTarget::MOS8580) ? 1u
                   : (def.staticState.chip == SidChipTarget::MOS6581) ? 0u : 2u;
    m.clock_system = (def.staticState.clock == ClockTarget::NTSC_First) ? 1u : 0u;
    m.forensic_en  = (def.usage.authenticity >= AuthenticityGrade::Forensic) ? 1u : 0u;
    m.role         = static_cast<uint8_t>(def.usage.role);
    return m;
}

// ─── Factory bank save ────────────────────────────────────────────────────────
FileBankError ArpSIDFileBank::saveFactoryBankToFile(const std::string& path) noexcept {
    const auto& defs = getFactoryPatchDefinitions();
    std::vector<SidStateRootV1> patches;
    std::vector<ArpSIDFilePatchMeta> metas;
    patches.reserve(defs.size());
    metas.reserve(defs.size());
    for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
        patches.push_back(makeFactoryPatchStateRootForSlot(i));
        metas.push_back(metaFromDefinition(defs[static_cast<size_t>(i)]));
    }
    return saveBankToFile(path, patches, metas);
}

// ─── Platform paths ───────────────────────────────────────────────────────────
std::string ArpSIDFileBank::defaultUserBankDirectory() noexcept {
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (home) {
        return std::string(home) + "/Music/ArpSID";
    }
    return "/tmp/ArpSID";
#elif defined(_WIN32)
    char path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, path)))
        return std::string(path) + "\\ArpSID";
    return "C:\\ArpSID";
#else
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/.arpsid";
    return "/tmp/arpsid";
#endif
}

std::string ArpSIDFileBank::defaultUserBankFilePath() noexcept {
    return joinPath(defaultUserBankDirectory(), "UserBank" + std::string(kBankExt));
}

std::string ArpSIDFileBank::defaultFactoryBankFilePath() noexcept {
    return joinPath(defaultUserBankDirectory(), "FactoryBank" + std::string(kBankExt));
}

// ─── Path utilities ───────────────────────────────────────────────────────────
bool ArpSIDFileBank::ensureDirectoryExists(const std::string& path) noexcept {
    if (path.empty()) return false;
#if defined(_WIN32)
    if (_mkdir(path.c_str()) == 0) return true;
    return errno == EEXIST;
#elif defined(__APPLE__) || defined(__linux__)
    if (mkdir(path.c_str(), 0755) == 0) return true;
    if (errno == EEXIST) {
        struct stat st{};
        return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
    // Try creating intermediate directories
    std::string p = path;
    size_t pos = 0;
    while ((pos = p.find('/', pos + 1)) != std::string::npos) {
        const std::string sub = p.substr(0, pos);
        if (!sub.empty()) mkdir(sub.c_str(), 0755);
    }
    return mkdir(p.c_str(), 0755) == 0 || errno == EEXIST;
#else
    return false;
#endif
}

bool ArpSIDFileBank::fileExists(const std::string& path) noexcept {
    if (path.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

std::string ArpSIDFileBank::sanitizeFilename(const std::string& name) noexcept {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (c == ' ' || c == '\t') out += '_';
        else if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                 c == '"' || c == '<' || c == '>' || c == '|' || c < 32)
            continue;
        else out += c;
    }
    if (out.size() > 48) out.resize(48);
    if (out.empty()) out = "Patch";
    return out;
}

std::string ArpSIDFileBank::joinPath(const std::string& dir, const std::string& file) noexcept {
    if (dir.empty()) return file;
    const char last = dir.back();
    if (last == '/' || last == '\\') return dir + file;
    return dir + ARPSID_PATH_SEP + file;
}

} // namespace ArpSID
