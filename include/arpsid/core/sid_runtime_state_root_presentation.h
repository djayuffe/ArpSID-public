// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include "sid_serializer_schema.h"
#include "sid_runtime_forensic_config.h"
#include "sid_variant_ops.h"
#include "drum_context.h"
#include "parameter_ids.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace ArpSID {

inline void sidCanonicalizeSemanticParameterEntries(SidStateRootV1& root);
// Non-RT only: calls sidSetStateRootParamValue which may allocate.
inline void sidCanonicalizePersistentVariantAuthority(SidStateRootV1& root);

inline constexpr bool sidIsDerivedLegacyVariantMirrorParam(int paramId) noexcept {
    return paramId == static_cast<int>(kParamSidModel) ||
           paramId == static_cast<int>(kParamSidClockSystem);
}

inline constexpr bool sidShouldDefaultRuntimeOnlySemanticParam(int paramId) noexcept {
    return isRuntimeOnlyOrTransientParam(paramId) && !sidIsDerivedLegacyVariantMirrorParam(paramId);
}

inline float sidStateRootParamValue(const SidStateRootV1& root, int paramId) noexcept {
    if (paramId < 0 || paramId >= kNumParams) return 0.0f;
    const float def = kParamInfos[(size_t)paramId].defaultNorm;
    for (const auto& e : root.patch.parameters.semantic_entries) {
        if (e.param_id != static_cast<uint32_t>(paramId)) continue;
        float v = std::isfinite(e.value) ? e.value : def;
        if (sidShouldDefaultRuntimeOnlySemanticParam(paramId)) v = def;
        return sanitizeNormalizedParamValue(paramId, v, def);
    }
    return sanitizeNormalizedParamValue(paramId, def, def);
}


// RT-safe read helper for pre-canonicalized / pre-hydrated state roots.
// It never inspects semantic_entries and never resizes vectors. The render-drained
// state-apply path must use this helper (or direct pre-sized values access), not
// sidStateRootParamValue(), when deriving live register mirrors from an already
// canonicalized root.
inline bool sidStateRootHasHydratedParameterValuesRT(const SidStateRootV1& root,
                                                     size_t count = static_cast<size_t>(kNumParams)) noexcept {
    return root.patch.parameters.values.size() >= count;
}

inline float sidStateRootParamValueFromHydratedValuesRT(const SidStateRootV1& root, int paramId) noexcept {
    if (paramId < 0 || paramId >= kNumParams) return 0.0f;
    const float def = kParamInfos[(size_t)paramId].defaultNorm;
    if (!sidStateRootHasHydratedParameterValuesRT(root, static_cast<size_t>(paramId) + 1u))
        return sanitizeNormalizedParamValue(paramId, def, def);
    float v = root.patch.parameters.values[(size_t)paramId];
    if (!std::isfinite(v)) v = def;
    if (sidShouldDefaultRuntimeOnlySemanticParam(paramId)) v = def;
    return sanitizeNormalizedParamValue(paramId, v, def);
}

inline bool sidSetHydratedStateRootParamValueRT(SidStateRootV1& root, int paramId, float value) noexcept {
    if (paramId < 0 || paramId >= kNumParams) return false;
    if (!sidStateRootHasHydratedParameterValuesRT(root, static_cast<size_t>(paramId) + 1u))
        return false;
    const float def = kParamInfos[(size_t)paramId].defaultNorm;
    float v = std::isfinite(value) ? value : def;
    if (sidShouldDefaultRuntimeOnlySemanticParam(paramId)) v = def;
    root.patch.parameters.values[(size_t)paramId] = sanitizeNormalizedParamValue(paramId, v, def);
    return true;
}

// Canonical read helper paired with sidSetStateRootParamValue().
// This delegates to the existing semantic-entry/default/runtime-transient read law.
inline float sidGetStateRootParamValue(const SidStateRootV1& root, int paramId) noexcept {
    return sidStateRootParamValue(root, paramId);
}

// Non-RT only: may allocate (push_back into semantic_entries).
inline void sidSetStateRootParamValue(SidStateRootV1& root, int paramId, float value) {
    if (paramId < 0 || paramId >= kNumParams) return;
    const float def = kParamInfos[(size_t)paramId].defaultNorm;
    float v = std::isfinite(value) ? value : def;
    if (sidShouldDefaultRuntimeOnlySemanticParam(paramId)) v = def;
    v = sanitizeNormalizedParamValue(paramId, v, def);
    bool updated = false;
    for (auto& e : root.patch.parameters.semantic_entries) {
        if (e.param_id != static_cast<uint32_t>(paramId)) continue;
        e.value = v;
        updated = true;
    }
    if (!updated) root.patch.parameters.semantic_entries.push_back(SidSemanticParamEntry{static_cast<uint32_t>(paramId), v});
}

// Non-RT only: calls sidSetStateRootParamValue which may allocate.
// Strict rule: exactly one of {BitPerfect, SidRegister, DrSid} must win.
// Priority matches sidResolveRenderModeFromLiveParams: DrSid > SidRegister > BitPerfect.
// If both DrSid AND SynthMode flags are simultaneously true (a "boolean priority
// accident"), DrSid wins and SynthMode is cleared — a single mode is always canonical.
inline void sidCanonicalizeTopLevelRenderModeParams(SidStateRootV1& root) {
    const bool synthOn = sidStateRootParamValue(root, kParamSynthModeEnable) > 0.5f;
    const bool drSidOn = sidStateRootParamValue(root, kParamDrSidEnable) > 0.5f;

    if (drSidOn && synthOn) {
        // Priority accident: both flags are set — DrSid wins, SynthMode cleared.
        sidSetStateRootParamValue(root, kParamDrSidEnable, 1.0f);
        sidSetStateRootParamValue(root, kParamSynthModeEnable, 0.0f);
        return;
    }
    if (drSidOn) {
        sidSetStateRootParamValue(root, kParamDrSidEnable, 1.0f);
        sidSetStateRootParamValue(root, kParamSynthModeEnable, 0.0f);
        return;
    }
    if (synthOn) {
        sidSetStateRootParamValue(root, kParamSynthModeEnable, 1.0f);
        sidSetStateRootParamValue(root, kParamDrSidEnable, 0.0f);
        return;
    }
    sidSetStateRootParamValue(root, kParamSynthModeEnable, 0.0f);
    sidSetStateRootParamValue(root, kParamDrSidEnable, 0.0f);
}

// RT-safe: sanitize live renderParams_ array in-place.
// Call after any path that may write mode flags without going through the
// canonical resolver (e.g., MIDI program-change, SysEx restore, parameter
// automation from untrusted hosts).
template <class ParamArray>
inline void sidSanitizeRenderModeParamsRT(ParamArray& params) noexcept {
    const bool drSidOn  = params[(size_t)kParamDrSidEnable]     > 0.5f;
    const bool synthOn  = params[(size_t)kParamSynthModeEnable] > 0.5f;
    if (drSidOn && synthOn) {
        // Clear SynthMode; DrSid wins.
        params[(size_t)kParamSynthModeEnable] = 0.0f;
    }
}

// Non-RT only: may allocate (assign/reserve/push_back on vectors).
inline void sidEnsureSemanticParameterEntries(SidStateRootV1& root) {
    sidCanonicalizeSemanticParameterEntries(root);
    sidCanonicalizePersistentVariantAuthority(root);

    std::array<float, (size_t)kNumParams> canonical{};
    for (int i = 0; i < kNumParams; ++i) {
        canonical[(size_t)i] = sanitizeNormalizedParamValue(
            i, kParamInfos[(size_t)i].defaultNorm, kParamInfos[(size_t)i].defaultNorm);
    }
    for (const auto& e : root.patch.parameters.semantic_entries) {
        if (e.param_id < (uint32_t)kNumParams) canonical[(size_t)e.param_id] = e.value;
    }

    root.patch.forensic_temperature_celsius = sidForensicTemperatureFromNormalized(canonical[(size_t)kParamForensicTemp]);
    root.patch.forensic_supply_voltage = sidForensicSupplyFromNormalized(canonical[(size_t)kParamForensicSupply]);
    root.patch.forensic_revision = sidForensicRevisionFromNormalized(canonical[(size_t)kParamForensicRevision]);
    const float seedNorm = canonical[(size_t)kParamForensicChipSeed];
    const float existingSeedNorm = sidForensicChipSeedToNormalized(root.patch.forensic_chip_seed);
    if (root.patch.forensic_chip_seed == 0u || std::fabs(existingSeedNorm - seedNorm) > (1.0f / 16777216.0f))
        root.patch.forensic_chip_seed = sidForensicChipSeedFromNormalized(seedNorm);
    canonical[(size_t)kParamForensicChipSeed] = sidForensicChipSeedToNormalized(root.patch.forensic_chip_seed);

    sidCanonicalizePersistentVariantAuthority(root);
    canonical[(size_t)kParamSidModel] =
        root.patch.variant_profile.family == SidFamily::MOS8580 ? 1.0f : 0.0f;
    canonical[(size_t)kParamSidClockSystem] =
        root.patch.variant_profile.video_standard == SidVideoStandard::NTSC ? 1.0f : 0.0f;
    for (auto& e : root.patch.parameters.semantic_entries) {
        if (e.param_id < (uint32_t)kNumParams) e.value = canonical[(size_t)e.param_id];
    }
    auto& vals = root.patch.parameters.values;
    vals.assign((size_t)kNumParams, 0.0f);
    for (int i = 0; i < kNumParams; ++i) vals[(size_t)i] = canonical[(size_t)i];
}

// Non-RT only: may allocate (assign on values vector).
inline void sidHydrateParameterValuesFromSemanticEntries(SidStateRootV1& root) {
    auto& vals = root.patch.parameters.values;
    vals.assign((size_t)kNumParams, 0.0f);
    for (int i = 0; i < kNumParams; ++i) {
        vals[(size_t)i] = sanitizeNormalizedParamValue(
            i, kParamInfos[(size_t)i].defaultNorm, kParamInfos[(size_t)i].defaultNorm);
    }
    for (const auto& e : root.patch.parameters.semantic_entries) {
        if (e.param_id >= (uint32_t)kNumParams) continue;
        float v = std::isfinite(e.value) ? e.value : kParamInfos[e.param_id].defaultNorm;
        if (sidShouldDefaultRuntimeOnlySemanticParam((int)e.param_id)) v = kParamInfos[e.param_id].defaultNorm;
        vals[e.param_id] = sanitizeNormalizedParamValue(static_cast<int>(e.param_id), v,
                                                        kParamInfos[e.param_id].defaultNorm);
    }
    root.patch.forensic_temperature_celsius = sidForensicTemperatureFromNormalized(vals[(size_t)kParamForensicTemp]);
    root.patch.forensic_supply_voltage = sidForensicSupplyFromNormalized(vals[(size_t)kParamForensicSupply]);
    root.patch.forensic_revision = sidForensicRevisionFromNormalized(vals[(size_t)kParamForensicRevision]);
    const float seedNorm = vals[(size_t)kParamForensicChipSeed];
    const float existingSeedNorm = sidForensicChipSeedToNormalized(root.patch.forensic_chip_seed);
    if (root.patch.forensic_chip_seed == 0u || std::fabs(existingSeedNorm - seedNorm) > (1.0f / 16777216.0f))
        root.patch.forensic_chip_seed = sidForensicChipSeedFromNormalized(seedNorm);
    vals[(size_t)kParamForensicChipSeed] = sidForensicChipSeedToNormalized(root.patch.forensic_chip_seed);
}

inline SidVariantProfile sidVariantProfileFromLegacyMirrors(const float* params, int paramCount) noexcept {
    SidFamily family = SidFamily::MOS8580;
    SidVideoStandard video = SidVideoStandard::PAL;
    if (params && paramCount > (int)kParamSidModel)
        family = (params[kParamSidModel] >= 0.5f) ? SidFamily::MOS8580 : SidFamily::MOS6581;
    if (params && paramCount > (int)kParamSidClockSystem)
        video = (params[kParamSidClockSystem] >= 0.5f) ? SidVideoStandard::NTSC : SidVideoStandard::PAL;
    SidVariantProfile p = sidDefaultVariantProfile(family, video);
    p.sanitize();
    return p;
}

inline void sidApplyVariantProfileToLegacyMirrors(const SidVariantProfile& variant,
                                                  float* params, int paramCount) noexcept {
    if (!params || paramCount <= 0) return;
    if ((int)kParamSidModel < paramCount)
        params[kParamSidModel] = variant.family == SidFamily::MOS8580 ? 1.0f : 0.0f;
    if ((int)kParamSidClockSystem < paramCount)
        params[kParamSidClockSystem] = variant.video_standard == SidVideoStandard::NTSC ? 1.0f : 0.0f;
}


// Canonical persistence authority helper.
//
// SidStateRootV1.patch.variant_profile is the only persistent variant authority.
// kParamSidModel/kParamSidClockSystem are legacy UI/host mirrors and must be
// derived from the profile during serialization/export. This prevents stale
// normalized arrays or semantic mirror entries from silently changing PAL/NTSC
// or 6581/8580 identity during project restore.
// Non-RT only: calls sidSetStateRootParamValue which may allocate.
inline void sidCanonicalizePersistentVariantAuthority(SidStateRootV1& root) {
    root.patch.variant_profile.sanitize();
    sidSetStateRootParamValue(root, kParamSidModel,
        root.patch.variant_profile.family == SidFamily::MOS8580 ? 1.0f : 0.0f);
    sidSetStateRootParamValue(root, kParamSidClockSystem,
        root.patch.variant_profile.video_standard == SidVideoStandard::NTSC ? 1.0f : 0.0f);
}

// Non-RT only: may allocate (clear/reserve/push_back on semantic_entries).
inline void sidCanonicalizeSemanticParameterEntries(SidStateRootV1& root) {
    std::array<float, (size_t)kNumParams> canonical{};
    std::array<bool, (size_t)kNumParams> seen{};
    for (int i = 0; i < kNumParams; ++i) {
        canonical[(size_t)i] = sanitizeNormalizedParamValue(
            i, kParamInfos[(size_t)i].defaultNorm, kParamInfos[(size_t)i].defaultNorm);
    }

    // Last writer wins for duplicate semantic entries. This gives deterministic
    // malformed-state repair while avoiding vector erase/allocation work.
    for (const auto& e : root.patch.parameters.semantic_entries) {
        if (e.param_id >= (uint32_t)kNumParams) continue;
        float v = std::isfinite(e.value) ? e.value : kParamInfos[e.param_id].defaultNorm;
        if (sidShouldDefaultRuntimeOnlySemanticParam((int)e.param_id)) v = kParamInfos[e.param_id].defaultNorm;
        canonical[(size_t)e.param_id] = sanitizeNormalizedParamValue(
            static_cast<int>(e.param_id), v, kParamInfos[e.param_id].defaultNorm);
        seen[(size_t)e.param_id] = true;
    }

    // Legacy flat values are import-only fallback. If semantic entries are absent,
    // migrate from values. If semantic entries are present, values are ignored.
    if (root.patch.parameters.semantic_entries.empty()) {
        const size_t n = std::min(root.patch.parameters.values.size(), (size_t)kNumParams);
        for (size_t i = 0; i < n; ++i) {
            float v = std::isfinite(root.patch.parameters.values[i]) ? root.patch.parameters.values[i]
                                                                     : kParamInfos[i].defaultNorm;
            if (sidShouldDefaultRuntimeOnlySemanticParam((int)i)) v = kParamInfos[i].defaultNorm;
            canonical[i] = sanitizeNormalizedParamValue(static_cast<int>(i), v,
                                                        kParamInfos[i].defaultNorm);
            seen[i] = true;
        }
    }

    root.patch.parameters.semantic_entries.clear();
    root.patch.parameters.semantic_entries.reserve((size_t)kNumParams);
    for (int i = 0; i < kNumParams; ++i) {
        const float def = kParamInfos[(size_t)i].defaultNorm;
        const float v = seen[(size_t)i] ? canonical[(size_t)i]
                                        : sanitizeNormalizedParamValue(i, def, def);
        root.patch.parameters.semantic_entries.push_back(SidSemanticParamEntry{static_cast<uint32_t>(i), v});
    }
}

// [COMPAT-IMPORT-ONLY] importPresentationParamsToStateRoot — flat param array → SidStateRootV1.
// This bridge exists solely to migrate/import pre-schema-root state blobs (format versions
// that did not carry semantic_entries) into the canonical root format. It MUST NOT be
// used as a primary save/load path for new state. The canonical round-trip is:
// save: buildStateRootFromPresentationTemplate → encodeSidStateRootBinary
// load: decodeSidStateRootBinary → (root is authoritative, flat params are presentation only)
inline void sidApplyFactorySlotSchemaContextToImportedStateRoot(SidStateRootV1& root,
                                                                    FactorySlotSchema schema) {
    const float slotNorm = sidStateRootParamValue(root, kParamBankSlot);
    const int slot = canonicalFactorySlotFromNormalizedBankSlot(slotNorm);
    const DrumContext ctx = factorySlotContextForSchema(slot, schema);
    if (ctx == DrumContext::DrSID_C64Wavetable) {
        sidSetStateRootParamValue(root, kParamSynthModeEnable, 0.0f);
        sidSetStateRootParamValue(root, kParamDrSidEnable, 1.0f);
        sidSetStateRootParamValue(root, kParamDrSidMachineModel, 0.0f);
    } else if (ctx == DrumContext::SID808_AnalogProjection) {
        sidSetStateRootParamValue(root, kParamSynthModeEnable, 0.0f);
        sidSetStateRootParamValue(root, kParamDrSidEnable, 1.0f);
        sidSetStateRootParamValue(root, kParamDrSidMachineModel, 1.0f);
    } else if (ctx == DrumContext::Digi4Bit) {
        sidSetStateRootParamValue(root, kParamSynthModeEnable, 0.0f);
        sidSetStateRootParamValue(root, kParamDrSidEnable, 0.0f);
    }
    sidEnsureSemanticParameterEntries(root);
    sidHydrateParameterValuesFromSemanticEntries(root);
}

inline SidStateRootV1 importPresentationParamsToStateRootForSchema(const float* params,
                                                                  int paramCount,
                                                                  FactorySlotSchema schema) {
    SidStateRootV1 root{};
    root.patch.parameters.semantic_entries.clear();
    root.patch.parameters.semantic_entries.reserve((size_t)kNumParams);
    root.patch.variant_profile = sidVariantProfileFromLegacyMirrors(params, paramCount);
    for (int i = 0; i < kNumParams; ++i) {
        const float def = kParamInfos[(size_t)i].defaultNorm;
        float v = (params && i < paramCount) ? params[i] : def;
        if (!std::isfinite(v)) v = def;
        if (sidShouldDefaultRuntimeOnlySemanticParam(i)) v = def;
        sidSetStateRootParamValue(root, i, sanitizeNormalizedParamValue(i, v, def));
    }
    sidEnsureSemanticParameterEntries(root);
    sidHydrateParameterValuesFromSemanticEntries(root);
    sidApplyFactorySlotSchemaContextToImportedStateRoot(root, schema);
    return root;
}

inline SidStateRootV1 importPresentationParamsToStateRoot(const float* params, int paramCount) {
    return importPresentationParamsToStateRootForSchema(params, paramCount, FactorySlotSchema::CanonicalV500Plus);
}

inline void exportStateRootToPresentationParams(const SidStateRootV1& root,
                                              float* params,
                                              int paramCount) noexcept {
    if (!params || paramCount <= 0) return;
    for (int i = 0; i < paramCount; ++i) {
        const float def = (i >= 0 && i < kNumParams) ? kParamInfos[(size_t)i].defaultNorm : 0.0f;
        float v = (i >= 0 && i < kNumParams) ? sidStateRootParamValue(root, i) : def;
        if (!std::isfinite(v)) v = def;
        params[i] = (i >= 0 && i < kNumParams)
            ? sanitizeNormalizedParamValue(i, v, def)
            : clampNormalized01(v);
    }
    sidApplyVariantProfileToLegacyMirrors(root.patch.variant_profile, params, paramCount);
}

// Non-RT only: calls heap-mutating canonicalization helpers. Must never be
// called from the render thread. Sanitize on the writer side before handing
// state to the RT double-buffer (schedulePendingStateRestore).
inline void sanitizePersistentStateRootForSerialization(SidStateRootV1& root) {
    root.magic = kSidStateRootMagic;
    root.schema_version = kSidStateRootSchemaVersion;
    root.patch.variant_profile.sanitize();
    root.patch.posterior.sanitize();
    sidCanonicalizeSemanticParameterEntries(root);
    sidCanonicalizePersistentVariantAuthority(root);
    sidEnsureSemanticParameterEntries(root);
    sidHydrateParameterValuesFromSemanticEntries(root);
}

// Full canonicalization a SidStateRootV1 must undergo before it is installed into
// the live SidRuntimeModel. This is the exact transform that SidRuntimeModel's
// internal sanitizeStateRoot_ used to perform on the audio thread.
//
// audit P0-3 / RT-safety: it ALLOCATES (rebuilds the parameter/semantic vectors via
// sanitizePersistentStateRootForSerialization) and MUST run on a non-realtime
// thread. The deferred-restore producer (schedulePendingStateRestore) calls this so
// the realtime apply path (applyStateRootBySwap) can install a pre-canonicalized
// root WITHOUT re-running this allocating sanitizer. The transform is idempotent, so
// re-canonicalizing an already-canonical root is a no-op in value terms.
inline void sidCanonicalizeStateRootForApply(SidStateRootV1& root) {
    root.magic = kSidStateRootMagic;
    root.schema_version = kSidStateRootSchemaVersion;
    root.patch.variant_profile.sanitize();
    root.patch.posterior.sanitize();
    sanitizePersistentStateRootForSerialization(root);
    sidCanonicalizeTopLevelRenderModeParams(root);
    // Sanitize mod routes in place — no temporary-vector copy/allocation.
    for (SidModRoute& r : root.patch.mod_routes) r.sanitize();
    for (float& v : root.patch.macros.values) {
        if (!std::isfinite(v)) v = 0.0f;
        v = std::clamp(v, 0.0f, 1.0f);
    }
    if (!std::isfinite(root.patch.arp.rate)) root.patch.arp.rate = 0.0f;
    root.patch.arp.rate = std::clamp(root.patch.arp.rate, 0.0f, 1.0f);
    root.patch.arp.transpose = std::clamp(root.patch.arp.transpose, -48, 48);
    if (!std::isfinite(root.patch.seq.tempo)) root.patch.seq.tempo = 0.0f;
    root.patch.seq.tempo = std::clamp(root.patch.seq.tempo, 0.0f, 400.0f);
    root.patch.seq.length = std::clamp(root.patch.seq.length, 0, 128);
    if (!std::isfinite(root.patch.seq.swing)) root.patch.seq.swing = 0.0f;
    root.patch.seq.swing = std::clamp(root.patch.seq.swing, 0.0f, 1.0f);
    if (root.document.program_name.size() > 1024) root.document.program_name.resize(1024);
    if (root.document.current_program_ref.size() > 1024) root.document.current_program_ref.resize(1024);
    if (root.document.editor_layout_blob.size() > 1u * 1024u * 1024u)
        root.document.editor_layout_blob.resize(1u * 1024u * 1024u);
}

// Non-RT only: calls sanitizePersistentStateRootForSerialization which may allocate.
template <class ParamReader>
inline void buildStateRootFromPresentationTemplate(const SidStateRootV1& templateRoot,
                                                   SidStateRootV1& out,
                                                   ParamReader&& readParam) {
    out = templateRoot;
    out.patch.parameters.semantic_entries.clear();
    out.patch.parameters.semantic_entries.reserve((size_t)kNumParams);
    for (int i = 0; i < kNumParams; ++i) {
        const float def = kParamInfos[(size_t)i].defaultNorm;
        float v = readParam(i);
        if (!std::isfinite(v)) v = def;
        if (sidShouldDefaultRuntimeOnlySemanticParam(i)) v = def;
        sidSetStateRootParamValue(out, i, sanitizeNormalizedParamValue(i, v, def));
    }
    auto& variant = out.patch.variant_profile;
    if (variant.family != SidFamily::MOS6581 && variant.family != SidFamily::MOS8580)
        variant = sidVariantProfileFromLegacyMirrors(nullptr, 0);
    variant.sanitize();
    if (variant.family != SidFamily::MOS6581 && variant.family != SidFamily::MOS8580) {
        variant.family = templateRoot.patch.variant_profile.family;
        if (variant.family != SidFamily::MOS6581 && variant.family != SidFamily::MOS8580)
            variant.family = SidFamily::MOS8580;
    }
    if (variant.video_standard != SidVideoStandard::PAL && variant.video_standard != SidVideoStandard::NTSC) {
        variant.video_standard = templateRoot.patch.variant_profile.video_standard;
        if (variant.video_standard != SidVideoStandard::PAL && variant.video_standard != SidVideoStandard::NTSC)
            variant.video_standard = SidVideoStandard::PAL;
    }
    out.patch.variant_profile = variant;
    sidCanonicalizePersistentVariantAuthority(out);
    sanitizePersistentStateRootForSerialization(out);
    sidEnsureSemanticParameterEntries(out);
}


inline void exportPersistentPresentationParamsFromStateRoot(const SidStateRootV1& root,
                                                            float* outParams,
                                                            int paramCount) noexcept {
    if (!outParams || paramCount <= 0) return;
    exportStateRootToPresentationParams(root, outParams, paramCount);
    const int safeCount = std::min(paramCount, kNumParams);
    for (int i = 0; i < safeCount; ++i) {
        const bool persistent = !isRuntimeOnlyOrTransientParam(i);
        const bool metadata = (i == kParamProgram || i == kParamBankSlot);
        const bool legacyVariantMirror = (i == static_cast<int>(kParamSidModel) ||
                                          i == static_cast<int>(kParamSidClockSystem));
        if (!(persistent || metadata || legacyVariantMirror)) {
            outParams[i] = kParamInfos[(size_t)i].defaultNorm;
        } else {
            outParams[i] = sanitizeNormalizedParamValue(i, outParams[i],
                                                        kParamInfos[(size_t)i].defaultNorm);
        }
    }
    sidApplyVariantProfileToLegacyMirrors(root.patch.variant_profile, outParams, paramCount);
    for (int i = safeCount; i < paramCount; ++i) outParams[i] = 0.0f;
}

} // namespace ArpSID
