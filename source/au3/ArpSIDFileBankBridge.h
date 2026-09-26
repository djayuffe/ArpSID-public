// ArpSIDFileBankBridge.h — ObjC bridge for file bank operations in AU context
// Wraps ArpSIDFileBank for use from ObjC/Swift UI layers.
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT
#pragma once
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@class ArpSIDDSPKernelAdapter;

// ─── ObjC patch metadata mirror ───────────────────────────────────────────────
@interface ArpSIDPatchMetaObj : NSObject
@property (nonatomic, copy) NSString*  name;
@property (nonatomic, copy) NSString*  author;
@property (nonatomic, copy) NSString*  category;
@property (nonatomic, copy) NSString*  tags;
@property (nonatomic) NSInteger        chipModel;    // 0=6581, 1=8580, 2=portable
@property (nonatomic) NSInteger        clockSystem;  // 0=PAL, 1=NTSC
@property (nonatomic) BOOL             forensicEnabled;
@property (nonatomic) NSInteger        role;         // PatchRole ordinal
@end

// ─── Load/save result ─────────────────────────────────────────────────────────
typedef NS_ENUM(NSInteger, ArpSIDFileBankResult) {
    ArpSIDFileBankResultOK = 0,
    ArpSIDFileBankResultFileNotFound,
    ArpSIDFileBankResultOpenFailed,
    ArpSIDFileBankResultReadFailed,
    ArpSIDFileBankResultWriteFailed,
    ArpSIDFileBankResultBadFormat,
    ArpSIDFileBankResultBadBlob,
    ArpSIDFileBankResultTooManySlots,
    ArpSIDFileBankResultBlobTooLarge,
    ArpSIDFileBankResultDirFailed,
    ArpSIDFileBankResultUnknown,
};

// ─── Bridge ───────────────────────────────────────────────────────────────────
@interface ArpSIDFileBankBridge : NSObject

// Platform default paths
+ (NSString*) defaultUserBankDirectory;
+ (NSString*) defaultUserBankFilePath;
+ (NSString*) defaultFactoryBankFilePath;

// ── Single patch I/O ──────────────────────────────────────────────────────────
/// Save the patch currently loaded in adapter's kernel to a .arpsid file.
- (ArpSIDFileBankResult) savePatchToFile:(NSString*)path
                                 adapter:(ArpSIDDSPKernelAdapter*)adapter
                                    meta:(ArpSIDPatchMetaObj*)meta;

/// Load a .arpsid file into the adapter's kernel.
- (ArpSIDFileBankResult) loadPatchFromFile:(NSString*)path
                                   adapter:(ArpSIDDSPKernelAdapter*)adapter
                              metaOut:(ArpSIDPatchMetaObj* _Nullable __autoreleasing * _Nullable)metaOut;

// ── Bank file I/O ─────────────────────────────────────────────────────────────
/// Save a full 128-slot bank to a .arpsidbank file, capturing the current live slot
/// into the active bank position and filling the remaining slots from the factory map.
- (ArpSIDFileBankResult) saveBankToFile:(NSString*)path
                                adapter:(ArpSIDDSPKernelAdapter*)adapter;

/// Load a .arpsidbank file, return slot metadata, and schedule the current bank slot
/// from that file into the live kernel when an adapter is present.
- (ArpSIDFileBankResult) loadBankFromFile:(NSString*)path
                                  adapter:(ArpSIDDSPKernelAdapter*)adapter
                                slotMetas:(NSArray<ArpSIDPatchMetaObj*>* _Nullable __autoreleasing * _Nullable)slotMetas
                                 progress:(void (^_Nullable)(NSInteger loaded, NSInteger total, NSString* name))progress;

// ── Factory bank ──────────────────────────────────────────────────────────────
/// Write the built-in factory patches to the default factory bank path.
- (ArpSIDFileBankResult) saveFactoryBankToDefaultPath;

/// Apply factory slot to adapter kernel (same as setCurrentPreset: but by slot index).
- (BOOL) applyFactorySlot:(NSInteger)slot adapter:(ArpSIDDSPKernelAdapter*)adapter;

// ── Utilities ─────────────────────────────────────────────────────────────────
/// Ensure the default user bank directory exists.
+ (BOOL) ensureDefaultUserBankDirectoryExists;

/// Human-readable error string for a result code.
+ (NSString*) stringForResult:(ArpSIDFileBankResult)result;

/// Build an ArpSIDPatchMetaObj from the current kernel state.
- (ArpSIDPatchMetaObj*) metaFromKernel:(ArpSIDDSPKernelAdapter*)adapter
                                  name:(NSString*)name
                                author:(NSString*)author
                              category:(NSString*)category;

@end

NS_ASSUME_NONNULL_END
