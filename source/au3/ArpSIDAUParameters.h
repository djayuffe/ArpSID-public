// ArpSIDAUParameters.h
// ArpSID AUv3 — Parameter Address Definitions
//
// AUParameterAddress is uint64_t. We simply cast each ArpSID::ParamID
// enum value so the two worlds stay in 1-to-1 sync without a translation
// table. Any new VST3 parameter automatically appears in AUv3 as well.
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#pragma once

#ifdef __cplusplus
#include "parameter_ids.h"   // ArpSID::ParamID, kNumParams

namespace ArpSIDAU {

/// Convert a normalized [0,1] VST3 value to the AU domain value that
/// AUParameter expects. For most parameters this is identity; it exists
/// here so we can add per-parameter scaling in one place if needed.
inline float normToAU(int /*paramID*/, float norm) noexcept { return norm; }

/// Convert an AUParameter value back to the [0,1] norm used internally.
inline float auToNorm(int /*paramID*/, float auVal) noexcept { return auVal; }

} // namespace ArpSIDAU
#endif // __cplusplus

#ifdef __OBJC__
#import <AudioToolbox/AudioToolbox.h>

/// All AUParameterAddress values are identical to their ArpSID::ParamID
/// integer values so the same enum can be used in both C++ and ObjC code.
/// Use these constants when constructing the AUParameterTree nodes.
static NSString * const kArpSIDParamID_MasterVolume        = @"0";   // kParamMasterVolume
static NSString * const kArpSIDParamID_FilterCutoff        = @"30";  // kParamFilterCutoff
static NSString * const kArpSIDParamID_FilterResonance     = @"31";  // kParamFilterResonance
static NSString * const kArpSIDParamID_Attack              = @"37";  // kParamAttack
static NSString * const kArpSIDParamID_Decay               = @"38";  // kParamDecay
static NSString * const kArpSIDParamID_Sustain             = @"39";  // kParamSustain
static NSString * const kArpSIDParamID_Release             = @"40";  // kParamRelease
static NSString * const kArpSIDParamID_ForensicTemp        = @"392"; // kParamForensicTemp
static NSString * const kArpSIDParamID_ForensicSupply      = @"393"; // kParamForensicSupply
static NSString * const kArpSIDParamID_ForensicRevision    = @"394"; // kParamForensicRevision
static NSString * const kArpSIDParamID_ForensicChipSeed    = @"395"; // kParamForensicChipSeed

/// Bundle-ID of the framework — must match MACOSX_FRAMEWORK_IDENTIFIER in CMake.
static NSString * const kArpSIDFrameworkBundleID = @"com.arpsid.auv3.framework";

#endif // __OBJC__
