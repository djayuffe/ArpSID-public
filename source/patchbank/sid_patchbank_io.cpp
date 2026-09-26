// sid_patchbank_io.cpp — ArpSID patch bank file I/O implementation
// Copyright (C) 2024-2026 Ulf Bertilsson. MIT License.

#include "arpsid/patchbank/sid_patchbank_io.h"
#include "arpsid/core/sid_state_codec.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "../factory_patch_params.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

namespace ArpSID {

// ─── Internal helpers ─────────────────────────────────────────────────────────

static constexpr char kPatchMagic[12] = {'A','R','P','S','I','D','P','A','T','C','H','\0'};
static constexpr char kBankMagic[12]  = {'A','R','P','S','I','D','B','A','N','K','\0','\0'};
static constexpr uint32_t kPatchVersion = 1u;
static constexpr uint32_t kBankVersion  = 1u;

static bool writeU32LE(std::FILE* f, uint32_t v) noexcept {
    uint8_t b[4] = { uint8_t(v), uint8_t(v>>8), uint8_t(v>>16), uint8_t(v>>24) };
    return std::fwrite(b, 1, 4, f) == 4;
}

static bool readU32LE(std::FILE* f, uint32_t& out) noexcept {
    uint8_t b[4];
    if (std::fread(b, 1, 4, f) != 4) return false;
    out = uint32_t(b[0]) | (uint32_t(b[1])<<8) | (uint32_t(b[2])<<16) | (uint32_t(b[3])<<24);
    return true;
}

static bool writeName64(std::FILE* f, const char* name) noexcept {
    char buf[64] = {};
    if (name) {
        const size_t len = std::strlen(name);
        const size_t copy = len < 63u ? len : 63u;
        std::memcpy(buf, name, copy);
        buf[copy] = '\0';  // always null-terminate; buf is zero-inited so buf[63]=0 too
    }
    return std::fwrite(buf, 1, 64, f) == 64;
}

static bool readName64(std::FILE* f, char out[64]) noexcept {
    char buf[64] = {};
    if (std::fread(buf, 1, 64, f) != 64) return false;
    buf[63] = '\0';
    std::memcpy(out, buf, 64);
    return true;
}

// Encode state root into a heap buffer; caller owns the memory.
static std::vector<uint8_t> encodeState(const SidStateRootV1& state) {
    const size_t cap = encodedSidStateRootBinarySize(state);
    if (cap == 0) return {};
    std::vector<uint8_t> buf(cap);
    const size_t written = encodeSidStateRootBinary(state, buf.data(), cap);
    if (written == 0) return {};
    buf.resize(written);
    return buf;
}

// Build a temporary file path adjacent to the target path.
// Uses the same directory so rename() is always on the same filesystem volume.
static std::string makeTempPath(const char* targetPath) {
    std::string tmp(targetPath);
    tmp += ".arpsid_tmp";
    return tmp;
}

// Atomic file write: write to a temp path, fsync, then rename over the target.
// If writing fails, the temp file is removed and false is returned.
// The target file is never partially overwritten.
using WriteBodyFn = bool(*)(std::FILE*, void*);

static bool atomicWriteFile(const char* targetPath, WriteBodyFn writeFn, void* ctx) noexcept {
    const std::string tmpPath = makeTempPath(targetPath);
    std::FILE* f = std::fopen(tmpPath.c_str(), "wb");
    if (!f) return false;

    const bool ok = writeFn(f, ctx);
    std::fflush(f);
    std::fclose(f);

    if (!ok) {
        std::remove(tmpPath.c_str());
        return false;
    }

    // Atomic rename: replaces target file only after the temp is fully written.
    if (std::rename(tmpPath.c_str(), targetPath) != 0) {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
}

// ─── Single patch ─────────────────────────────────────────────────────────────

struct PatchWriteCtx {
    const std::vector<uint8_t>* blob;
    const char* name;
};

static bool writePatchBody(std::FILE* f, void* ctx) noexcept {
    auto* p = static_cast<PatchWriteCtx*>(ctx);
    bool ok = true;
    ok = ok && (std::fwrite(kPatchMagic, 1, 12, f) == 12);
    ok = ok && writeU32LE(f, kPatchVersion);
    ok = ok && writeName64(f, p->name);
    ok = ok && writeU32LE(f, static_cast<uint32_t>(p->blob->size()));
    ok = ok && (std::fwrite(p->blob->data(), 1, p->blob->size(), f) == p->blob->size());
    return ok;
}

bool savePatchToFile(const SidStateRootV1& state,
                     const char* filePath,
                     const char* patchName) noexcept {
    if (!filePath) return false;

    SidStateRootV1 sanitized = state;
    sanitizePersistentStateRootForSerialization(sanitized);

    const auto blob = encodeState(sanitized);
    if (blob.empty()) return false;

    const char* name = patchName ? patchName : sanitized.document.program_name.c_str();
    PatchWriteCtx ctx{ &blob, name };
    return atomicWriteFile(filePath, writePatchBody, &ctx);
}

bool loadPatchFromFile(const char* filePath,
                       SidStateRootV1& outState,
                       char outName[64]) noexcept {
    if (!filePath) return false;

    std::FILE* f = std::fopen(filePath, "rb");
    if (!f) return false;

    bool ok = true;
    char magic[12] = {};
    ok = ok && (std::fread(magic, 1, 12, f) == 12);
    ok = ok && (std::memcmp(magic, kPatchMagic, 12) == 0);

    uint32_t version = 0;
    ok = ok && readU32LE(f, version);
    ok = ok && (version >= 1u);

    char nameBuf[64] = {};
    ok = ok && readName64(f, nameBuf);

    uint32_t blobLen = 0;
    ok = ok && readU32LE(f, blobLen);
    ok = ok && (blobLen > 0 && blobLen <= 1024u * 1024u);

    if (ok) {
        std::vector<uint8_t> blob(blobLen);
        ok = (std::fread(blob.data(), 1, blobLen, f) == blobLen);
        if (ok) {
            ok = decodeSidStateRootBinary(blob.data(), blobLen, outState);
            if (ok && outName) std::memcpy(outName, nameBuf, 64);
        }
    }

    std::fclose(f);
    return ok;
}

// ─── Bank ────────────────────────────────────────────────────────────────────

struct BankWriteCtx {
    const PatchBankFile* bank;
};

static bool writeBankBody(std::FILE* f, void* ctx) noexcept {
    auto* p = static_cast<BankWriteCtx*>(ctx);
    const PatchBankFile& bank = *p->bank;

    const int count = std::clamp(bank.patchCount, 0, kBankMaxPatches);
    bool ok = true;
    ok = ok && (std::fwrite(kBankMagic, 1, 12, f) == 12);
    ok = ok && writeU32LE(f, kBankVersion);
    ok = ok && writeName64(f, bank.bankName);
    ok = ok && writeU32LE(f, static_cast<uint32_t>(count));

    for (int i = 0; i < count && ok; ++i) {
        const auto& slot = bank.patches[i];
        if (!slot.valid) {
            ok = ok && writeU32LE(f, 0u);
            ok = ok && writeName64(f, slot.name);
            continue;
        }
        SidStateRootV1 sanitized = slot.state;
        sanitizePersistentStateRootForSerialization(sanitized);
        const auto blob = encodeState(sanitized);
        const uint32_t blobLen = static_cast<uint32_t>(blob.size());
        ok = ok && writeU32LE(f, blobLen);
        ok = ok && writeName64(f, slot.name);
        if (blobLen > 0)
            ok = ok && (std::fwrite(blob.data(), 1, blobLen, f) == blobLen);
    }
    return ok;
}

bool saveBankToFile(const PatchBankFile& bank, const char* filePath) noexcept {
    if (!filePath) return false;
    BankWriteCtx ctx{ &bank };
    return atomicWriteFile(filePath, writeBankBody, &ctx);
}

bool loadBankFromFile(const char* filePath, PatchBankFile& outBank) noexcept {
    if (!filePath) return false;

    std::FILE* f = std::fopen(filePath, "rb");
    if (!f) return false;

    outBank.clear();
    bool ok = true;

    char magic[12] = {};
    ok = ok && (std::fread(magic, 1, 12, f) == 12);
    ok = ok && (std::memcmp(magic, kBankMagic, 12) == 0);

    uint32_t version = 0;
    ok = ok && readU32LE(f, version);
    ok = ok && (version >= 1u);
    ok = ok && readName64(f, outBank.bankName);

    uint32_t count = 0;
    ok = ok && readU32LE(f, count);
    ok = ok && (count <= static_cast<uint32_t>(kBankMaxPatches));

    if (!ok) { std::fclose(f); return false; }

    outBank.patchCount = static_cast<int>(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t blobLen = 0;
        if (!readU32LE(f, blobLen)) { outBank.patchCount = (int)i; break; }

        auto& slot = outBank.patches[i];
        if (!readName64(f, slot.name)) { outBank.patchCount = (int)i; break; }

        if (blobLen == 0) { slot.valid = false; continue; }
        if (blobLen > 1024u * 1024u) { std::fclose(f); return false; }

        std::vector<uint8_t> blob(blobLen);
        if (std::fread(blob.data(), 1, blobLen, f) != blobLen) {
            outBank.patchCount = (int)i;
            break;
        }
        slot.valid = decodeSidStateRootBinary(blob.data(), blobLen, slot.state);
    }

    std::fclose(f);
    return true;
}

// ─── Factory bank export ──────────────────────────────────────────────────────

// .arpbank v1 export is a 128-slot user-bank-compatible subset, not the full 180-slot factory.
bool exportFactoryBankToFile(const char* filePath) noexcept {
    PatchBankFile bank;
    std::strncpy(bank.bankName, "ArpSID Factory Bank v1 subset", sizeof(bank.bankName) - 1);
    bank.bankName[sizeof(bank.bankName) - 1] = '\0';
    bank.patchCount = kBankMaxPatches;

    for (int i = 0; i < kBankMaxPatches; ++i) {
        auto& slot = bank.patches[i];
        const SidStateRootV1 root = makeFactoryPatchStateRootForSlot(i);
        slot.valid = root.valid();
        slot.state = root;
        const std::string name = factoryPatchNameForSlot(i);
        const size_t nlen = name.size() < 63u ? name.size() : 63u;
        std::memcpy(slot.name, name.c_str(), nlen);
        slot.name[nlen] = '\0';
    }

    return saveBankToFile(bank, filePath);
}

} // namespace ArpSID
