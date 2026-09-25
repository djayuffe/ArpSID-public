// ArpSIDFileBankBridge.mm — ObjC bridge for file bank ops in AUv3 context
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#import "ArpSIDFileBankBridge.h"
#import "ArpSIDDSPKernelAdapter.h"
#include "ArpSIDDSPKernel.hpp"
#include "../arpsid_file_bank.h"
#include "ArpSIDStateSerializer.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "../factory_patch_params.h"

// Explicit C-library includes — do not rely on transitive headers.
#include <algorithm>    // std::clamp
#include <cstdio>       // std::snprintf
#include <cstring>      // std::strncpy, std::memset
#include <vector>

// ─── ArpSIDPatchMetaObj ───────────────────────────────────────────────────────
@implementation ArpSIDPatchMetaObj
- (instancetype)init {
    if ((self = [super init])) {
        _name     = @"";
        _author   = @"";
        _category = @"";
        _tags     = @"";
    }
    return self;
}
@end

// ─── Internal helpers ─────────────────────────────────────────────────────────

// Safe null-terminated copy helper — guarantees NUL at last byte.
static void safeCopyStr(char* dst, size_t dstSize, const char* src) noexcept {
    if (!dst || dstSize == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    std::snprintf(dst, dstSize, "%s", src);
}

static NSString* safeUTF8Field(const char* src, size_t maxLen) {
    if (!src || maxLen == 0 || src[0] == '\0') return @"";
    const size_t len = strnlen(src, maxLen);
    NSString* decoded = [[NSString alloc] initWithBytes:src length:len encoding:NSUTF8StringEncoding];
    return decoded ? decoded : @"";
}

static ArpSID::ArpSIDFilePatchMeta metaFromObj(ArpSIDPatchMetaObj* obj) {
    ArpSID::ArpSIDFilePatchMeta m{};
    safeCopyStr(m.name,     sizeof(m.name),     obj.name     ? obj.name.UTF8String     : "");
    safeCopyStr(m.author,   sizeof(m.author),   obj.author   ? obj.author.UTF8String   : "");
    safeCopyStr(m.category, sizeof(m.category), obj.category ? obj.category.UTF8String : "");
    safeCopyStr(m.tags,     sizeof(m.tags),     obj.tags     ? obj.tags.UTF8String     : "");
    m.chip_model   = (uint8_t)std::clamp((int)obj.chipModel,   0, 2);
    m.clock_system = (uint8_t)std::clamp((int)obj.clockSystem, 0, 1);
    m.forensic_en  = obj.forensicEnabled ? 1u : 0u;
    m.role         = (uint8_t)std::clamp((int)obj.role, 0, 15);
    return m;
}

static ArpSIDPatchMetaObj* metaToObj(const ArpSID::ArpSIDFilePatchMeta& m) {
    ArpSIDPatchMetaObj* obj = [[ArpSIDPatchMetaObj alloc] init];
    obj.name     = safeUTF8Field(m.name, sizeof(m.name));
    obj.author   = safeUTF8Field(m.author, sizeof(m.author));
    obj.category = safeUTF8Field(m.category, sizeof(m.category));
    obj.tags     = safeUTF8Field(m.tags, sizeof(m.tags));
    obj.chipModel        = (NSInteger)m.chip_model;
    obj.clockSystem      = (NSInteger)m.clock_system;
    obj.forensicEnabled  = (m.forensic_en != 0);
    obj.role             = (NSInteger)m.role;
    return obj;
}

static ArpSIDFileBankResult mapError(ArpSID::FileBankError e) {
    switch (e) {
        case ArpSID::FileBankError::OK:                    return ArpSIDFileBankResultOK;
        case ArpSID::FileBankError::FileNotFound:          return ArpSIDFileBankResultFileNotFound;
        case ArpSID::FileBankError::OpenFailed:            return ArpSIDFileBankResultOpenFailed;
        case ArpSID::FileBankError::ReadFailed:            return ArpSIDFileBankResultReadFailed;
        case ArpSID::FileBankError::WriteFailed:           return ArpSIDFileBankResultWriteFailed;
        case ArpSID::FileBankError::BadMagic:
        case ArpSID::FileBankError::BadVersion:            return ArpSIDFileBankResultBadFormat;
        case ArpSID::FileBankError::BadBlob:               return ArpSIDFileBankResultBadBlob;
        case ArpSID::FileBankError::TooManySlots:          return ArpSIDFileBankResultTooManySlots;
        case ArpSID::FileBankError::BlobTooLarge:          return ArpSIDFileBankResultBlobTooLarge;
        case ArpSID::FileBankError::DirectoryCreateFailed: return ArpSIDFileBankResultDirFailed;
        default:                                           return ArpSIDFileBankResultUnknown;
    }
}

static BOOL stringContainsCaseInsensitive(NSString* haystack, NSString* needle) {
    if (!haystack || !needle || haystack.length == 0 || needle.length == 0) return NO;
    return [haystack rangeOfString:needle options:NSCaseInsensitiveSearch].location != NSNotFound;
}

static BOOL metaLooksLikeDrumKit(const ArpSID::ArpSIDFilePatchMeta& meta) {
    if (meta.role == (uint8_t)ArpSID::PatchRole::Drum) return YES;
    NSString* category = safeUTF8Field(meta.category, sizeof(meta.category));
    NSString* tags = safeUTF8Field(meta.tags, sizeof(meta.tags));
    return stringContainsCaseInsensitive(category, @"drum") ||
           stringContainsCaseInsensitive(category, @"drsid") ||
           stringContainsCaseInsensitive(tags, @"drum") ||
           stringContainsCaseInsensitive(tags, @"drsid") ||
           stringContainsCaseInsensitive(tags, @"gm");
}

static BOOL rootLooksLikeDrumKit(const ArpSID::SidStateRootV1& root) {
    const float drSid = ArpSID::sidStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable);
    const float synth = ArpSID::sidStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable);
    return drSid > 0.5f && synth <= 0.5f;
}

static void canonicalizePatchRootForPersistence(ArpSID::SidStateRootV1& root,
                                                const ArpSID::ArpSIDFilePatchMeta& meta) {
    ArpSID::sidCanonicalizeTopLevelRenderModeParams(root);
    if (metaLooksLikeDrumKit(meta) || rootLooksLikeDrumKit(root)) {
        const float existingSeq = ArpSID::sidStateRootParamValue(root, (int)ArpSID::kParamSeqEnable);
        ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 1.0f);
        ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 0.0f);
        ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamArpEnable, 0.0f);
        // v950: Preserve SeqEnable instead of forcing the DrSID/SID808 transport off
        // when serializing/restoring a drum kit or pattern root.
        ArpSID::sidSetStateRootParamValue(root, (int)ArpSID::kParamSeqEnable, existingSeq);
    }
    ArpSID::sidEnsureSemanticParameterEntries(root);
}

static void buildFactoryBankVectors(std::vector<ArpSID::SidStateRootV1>& patches,
                                    std::vector<ArpSID::ArpSIDFilePatchMeta>& metas) {
    const auto& defs = ArpSID::getFactoryPatchDefinitions();
    patches.resize((size_t)ArpSID::kFileBankMaxSlots);
    metas.resize((size_t)ArpSID::kFileBankMaxSlots);
    for (int slot = 0; slot < ArpSID::kFileBankMaxSlots; ++slot) {
        patches[(size_t)slot] = ArpSID::makeFactoryPatchStateRootForSlot(slot);
        metas[(size_t)slot] = ((size_t)slot < defs.size())
            ? ArpSID::ArpSIDFileBank::metaFromDefinition(defs[(size_t)slot])
            : ArpSID::ArpSIDFilePatchMeta{};
    }
}

// ─── Bridge implementation ────────────────────────────────────────────────────
@implementation ArpSIDFileBankBridge

+ (NSString*)defaultUserBankDirectory {
    return [NSString stringWithUTF8String:ArpSID::ArpSIDFileBank::defaultUserBankDirectory().c_str()];
}

+ (NSString*)defaultUserBankFilePath {
    return [NSString stringWithUTF8String:ArpSID::ArpSIDFileBank::defaultUserBankFilePath().c_str()];
}

+ (NSString*)defaultFactoryBankFilePath {
    return [NSString stringWithUTF8String:ArpSID::ArpSIDFileBank::defaultFactoryBankFilePath().c_str()];
}

+ (BOOL)ensureDefaultUserBankDirectoryExists {
    const std::string dir = ArpSID::ArpSIDFileBank::defaultUserBankDirectory();
    return ArpSID::ArpSIDFileBank::ensureDirectoryExists(dir) ? YES : NO;
}

+ (NSString*)stringForResult:(ArpSIDFileBankResult)result {
    switch (result) {
        case ArpSIDFileBankResultOK:            return @"OK";
        case ArpSIDFileBankResultFileNotFound:  return @"File not found";
        case ArpSIDFileBankResultOpenFailed:    return @"Failed to open file";
        case ArpSIDFileBankResultReadFailed:    return @"Read error";
        case ArpSIDFileBankResultWriteFailed:   return @"Write error";
        case ArpSIDFileBankResultBadFormat:     return @"Invalid file format or version";
        case ArpSIDFileBankResultBadBlob:       return @"Corrupt patch data";
        case ArpSIDFileBankResultTooManySlots:  return @"Too many slots in bank";
        case ArpSIDFileBankResultBlobTooLarge:  return @"Patch data too large";
        case ArpSIDFileBankResultDirFailed:     return @"Failed to create directory";
        default:                                return @"Unknown error";
    }
}

// ── Single patch save ─────────────────────────────────────────────────────────
- (ArpSIDFileBankResult)savePatchToFile:(NSString*)path
                                adapter:(ArpSIDDSPKernelAdapter*)adapter
                                   meta:(ArpSIDPatchMetaObj*)meta {
    if (!path || !adapter || !meta) return ArpSIDFileBankResultUnknown;
    ArpSID::ArpSIDDSPKernel* k = [adapter kernelPtr];
    if (!k) return ArpSIDFileBankResultUnknown;

    ArpSID::SidStateRootV1 root{};
    k->buildSerializableStateRootFromShadow(root);

    const ArpSID::ArpSIDFilePatchMeta m = metaFromObj(meta);
    canonicalizePatchRootForPersistence(root, m);
    return mapError(ArpSID::ArpSIDFileBank::savePatchToFile(path.UTF8String, root, m));
}

// ── Single patch load ─────────────────────────────────────────────────────────
- (ArpSIDFileBankResult)loadPatchFromFile:(NSString*)path
                                  adapter:(ArpSIDDSPKernelAdapter*)adapter
                              metaOut:(ArpSIDPatchMetaObj* __autoreleasing *)metaOut {
    if (!path || !adapter) return ArpSIDFileBankResultUnknown;
    ArpSID::ArpSIDDSPKernel* k = [adapter kernelPtr];
    if (!k) return ArpSIDFileBankResultUnknown;

    ArpSID::SidStateRootV1 root{};
    ArpSID::ArpSIDFilePatchMeta m{};
    const auto err = ArpSID::ArpSIDFileBank::loadPatchFromFile(path.UTF8String, root, m);
    if (err != ArpSID::FileBankError::OK) return mapError(err);
    canonicalizePatchRootForPersistence(root, m);

    // Deferred apply — safe across render-thread boundary
    k->schedulePendingStateRestore(root);

    if (metaOut) { *metaOut = metaToObj(m); }
    return ArpSIDFileBankResultOK;
}

// ── Bank save ─────────────────────────────────────────────────────────────────
- (ArpSIDFileBankResult)saveBankToFile:(NSString*)path
                               adapter:(ArpSIDDSPKernelAdapter*)adapter {
    if (!path || !adapter) return ArpSIDFileBankResultUnknown;
    ArpSID::ArpSIDDSPKernel* k = [adapter kernelPtr];
    if (!k) return ArpSIDFileBankResultUnknown;

    std::vector<ArpSID::SidStateRootV1> patches;
    std::vector<ArpSID::ArpSIDFilePatchMeta> metas;
    buildFactoryBankVectors(patches, metas);

    ArpSID::SidStateRootV1 liveRoot{};
    k->buildSerializableStateRootFromShadow(liveRoot);
    ArpSIDPatchMetaObj* liveMetaObj = [self metaFromKernel:adapter name:@"" author:@"User" category:@""];
    ArpSID::ArpSIDFilePatchMeta liveMeta = metaFromObj(liveMetaObj);
    canonicalizePatchRootForPersistence(liveRoot, liveMeta);

    const auto tel = k->readTelemetry();
    const int slot = std::clamp(tel.bankSlot, 0, ArpSID::kCanonicalFactoryPatchSlotMax);
    if (slot >= 0 && slot < ArpSID::kFileBankMaxSlots) {
        patches[(size_t)slot] = liveRoot;
        metas[(size_t)slot] = liveMeta;
    }
    // v1 .arpsidbank files are 128 user-bank slots. Extended factory
    // slots 128..179 are intentionally not aliased into slot 127; they
    // require factory preset/state-root export or a future v2 bank format.

    return mapError(ArpSID::ArpSIDFileBank::saveBankToFile(path.UTF8String, patches, metas));
}

// ── Bank load ─────────────────────────────────────────────────────────────────
- (ArpSIDFileBankResult)loadBankFromFile:(NSString*)path
                                 adapter:(ArpSIDDSPKernelAdapter*)adapter
                               slotMetas:(NSArray<ArpSIDPatchMetaObj*>* __autoreleasing *)slotMetas
                                progress:(void (^)(NSInteger, NSInteger, NSString*))progress {
    if (!path) return ArpSIDFileBankResultUnknown;

    std::vector<ArpSID::SidStateRootV1> patches;
    std::vector<ArpSID::ArpSIDFilePatchMeta> metas;

    ArpSID::BankProgressFn progressFn = nullptr;
    if (progress) {
        progressFn = [progress](int loaded, int total, const char* name) {
            NSString* ns = (name && name[0]) ? [NSString stringWithUTF8String:name] : @"";
            progress((NSInteger)loaded, (NSInteger)total, ns);
        };
    }

    const auto err = ArpSID::ArpSIDFileBank::loadBankFromFile(
        path.UTF8String, patches, metas, progressFn);
    if (err != ArpSID::FileBankError::OK) return mapError(err);

    for (size_t i = 0; i < patches.size() && i < metas.size(); ++i) {
        canonicalizePatchRootForPersistence(patches[i], metas[i]);
    }

    if (slotMetas) {
        NSMutableArray* arr = [NSMutableArray arrayWithCapacity:(NSUInteger)metas.size()];
        for (const auto& m : metas) { [arr addObject:metaToObj(m)]; }
        *slotMetas = [arr copy];
    }

    if (adapter) {
        ArpSID::ArpSIDDSPKernel* k = [adapter kernelPtr];
        if (k && !patches.empty()) {
            const auto tel = k->readTelemetry();
            const size_t preferred = std::clamp((size_t)std::max(tel.bankSlot, 0), (size_t)0, patches.size() - 1u);
            k->schedulePendingStateRestore(patches[preferred]);
        }
    }
    return ArpSIDFileBankResultOK;
}

// ── Factory bank ──────────────────────────────────────────────────────────────
- (ArpSIDFileBankResult)saveFactoryBankToDefaultPath {
    [ArpSIDFileBankBridge ensureDefaultUserBankDirectoryExists];
    const std::string path = ArpSID::ArpSIDFileBank::defaultFactoryBankFilePath();
    return mapError(ArpSID::ArpSIDFileBank::saveFactoryBankToFile(path));
}

- (BOOL)applyFactorySlot:(NSInteger)slot adapter:(ArpSIDDSPKernelAdapter*)adapter {
    if (!adapter) return NO;
    ArpSID::ArpSIDDSPKernel* k = [adapter kernelPtr];
    if (!k) return NO;
    const int s = ArpSID::normalizeFactoryPatchSlot((int)slot);
    const ArpSID::SidStateRootV1 root = ArpSID::makeFactoryPatchStateRootForSlot(s);
    if (!root.valid()) return NO;
    k->schedulePendingStateRestore(root);
    return YES;
}

// ── Meta builder (from running kernel state) ──────────────────────────────────
- (ArpSIDPatchMetaObj*)metaFromKernel:(ArpSIDDSPKernelAdapter*)adapter
                                 name:(NSString*)name
                               author:(NSString*)author
                             category:(NSString*)category {
    ArpSIDPatchMetaObj* obj = [[ArpSIDPatchMetaObj alloc] init];
    // Use explicit nil checks — no GNU omit-middle ?: extension
    obj.name     = (name     && name.length)     ? name     : @"";
    obj.author   = (author   && author.length)   ? author   : @"";
    obj.category = (category && category.length) ? category : @"";

    if (!adapter) return obj;
    ArpSID::ArpSIDDSPKernel* k = [adapter kernelPtr];
    if (!k) return obj;

    const auto tel = k->readTelemetry();
    obj.chipModel       = (NSInteger)(tel.sidModel ? 1 : 0);
    obj.clockSystem     = ([adapter getParameterID:(int)ArpSID::kParamSidClockSystem] > 0.5f) ? 1 : 0;
    obj.forensicEnabled = ([adapter getParameterID:(int)ArpSID::kParamForensicEnable] > 0.5f);
    obj.role            = tel.drSidMode ? (NSInteger)ArpSID::PatchRole::Drum : (NSInteger)ArpSID::PatchRole::Lead;

    const int curSlot = std::clamp(tel.bankSlot, 0, ArpSID::kCanonicalFactoryPatchSlotMax);
    const ArpSID::PatchDefinition* def = ArpSID::getFactoryPatchDefinition(curSlot);
    if (tel.drSidMode) {
        if (obj.category.length == 0) obj.category = @"DrSID Kit";
        if (obj.name.length == 0) obj.name = [NSString stringWithFormat:@"DrSID Kit %03d", curSlot + 1];
        obj.tags = @"drsid gm drumkit midi";
        obj.role = (NSInteger)ArpSID::PatchRole::Drum;
        return obj;
    }
    if (def) {
        if (!def->displayName.empty() && obj.name.length == 0) {
            obj.name = [NSString stringWithUTF8String:def->displayName.c_str()];
        }
        if (obj.category.length == 0) obj.category = [NSString stringWithUTF8String:ArpSID::toString(def->usage.role)];
        if (obj.tags.length == 0 && !def->id.empty()) obj.tags = [NSString stringWithUTF8String:def->id.c_str()];
        obj.role = (NSInteger)def->usage.role;
    }
    if (obj.name.length == 0) {
        obj.name = [NSString stringWithFormat:@"Patch %03d", curSlot + 1];
    }
    return obj;
}

@end
