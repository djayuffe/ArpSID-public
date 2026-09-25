// ─── ArpSIDStateSerializer.h ──────────────────────────────────────────────────
// Canonical persistence goes through the SidStateRootV1 schema/codec.
// Normalized parameter arrays are compatibility/UI import-export helpers only and
// must not be treated as the primary persisted truth surface or primary restore path.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include "../parameter_ids.h"
#include "arpsid/core/sid_serializer_schema.h"
#include "arpsid/core/sid_state_codec.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace ArpSID {

inline uint16_t stateReadLE16_(const uint8_t* p) noexcept {
    return (uint16_t)p[0] | (uint16_t)(p[1] << 8);
}
inline uint32_t stateReadLE32_(const uint8_t* p) noexcept {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void stateWriteLE16_(uint8_t* p, uint16_t v) noexcept {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}
inline void stateWriteLE32_(uint8_t* p, uint32_t v) noexcept {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static constexpr uint32_t kStateMagic        = kSidBinaryStateMagic;
static constexpr uint16_t kStateMajorVersion = kSidBinaryStateMajorVersion;
static constexpr uint16_t kStateMinorVersion = kSidBinaryStateMinorVersion;
static constexpr uint32_t kPatchStateMagic   = kSidBinaryPatchStateMagic;
static constexpr uint32_t kProjectStateMagic = kSidBinaryProjectStateMagic;

inline bool decodeNormalizedStateLegacyInternal(const uint8_t* blob, size_t blobSize,
                        float* params, int paramCount,
                        uint32_t expectedMagic) noexcept {
    if (!blob || blobSize < sizeof(SidBinaryStateHeader) || !params || paramCount <= 0) return false;

    SidBinaryStateHeader hdr{};
    hdr.magic = stateReadLE32_(blob + 0);
    hdr.majorVersion = stateReadLE16_(blob + 4);
    hdr.minorVersion = stateReadLE16_(blob + 6);
    hdr.paramCount = stateReadLE32_(blob + 8);
    hdr.featureFlags = stateReadLE32_(blob + 12);
    hdr.checksum = stateReadLE32_(blob + 16);
    if (!hdr.valid(expectedMagic)) return false;
    if (hdr.majorVersion > kStateMajorVersion) return false;
    if (hdr.paramCount > static_cast<uint32_t>(kNumParams)) return false;

    const size_t paramBytes = hdr.paramCount * sizeof(uint32_t);
    if (blobSize < sizeof(SidBinaryStateHeader) + paramBytes) return false;

    const uint8_t* words = blob + sizeof(SidBinaryStateHeader);
    const uint32_t cs = sidAdler32(words, paramBytes);
    if (cs != hdr.checksum) return false;

    const int readCount = (int)std::min((uint32_t)paramCount, hdr.paramCount);
    for (int i = 0; i < readCount; ++i) {
        const uint32_t bits = stateReadLE32_(words + (size_t)i * sizeof(uint32_t));
        float fv; std::memcpy(&fv, &bits, 4);
        if (!std::isfinite(fv)) fv = kParamInfos[(size_t)i].defaultNorm;
        params[i] = std::clamp(fv, 0.f, 1.f);
    }
    for (int i = readCount; i < paramCount; ++i)
        params[i] = kParamInfos[(size_t)i].defaultNorm;

    return true;
}

inline bool migrateNormalizedParams(float* params, int paramCount,
                           uint16_t fromMajor, uint16_t fromMinor) noexcept {
    if (!params || paramCount <= 0) return false;
    for (int i = 0; i < paramCount; ++i) {
        const float def = (i >= 0 && i < kNumParams) ? kParamInfos[(size_t)i].defaultNorm : 0.0f;
        float v = params[i];
        if (!std::isfinite(v)) v = def;
        params[i] = std::clamp(v, 0.0f, 1.0f);
    }
    SidStateRootV1 root = importPresentationParamsToStateRootForSchema(params, paramCount,
                                                                 FactorySlotSchema::Legacy128);
    sanitizePersistentStateRootForSerialization(root);
    if (fromMajor == 0u || (fromMajor == 1u && fromMinor < 1u)) {
        root.patch.variant_profile = sidVariantProfileFromLegacyMirrors(params, paramCount);
        root.patch.variant_profile.sanitize();
    }
    exportPersistentPresentationParamsFromStateRoot(root, params, paramCount);
    return true;
}


inline bool decodeStateRootCanonical(const uint8_t* blob,
                                     size_t blobSize,
                                     SidStateRootV1& root,
                                     uint32_t expectedMagic) noexcept {
    // Primary path: new-format binary blob with embedded semantic_entries.
    if (decodeSidStateRootBinary(blob, blobSize, root)) {
        sanitizePersistentStateRootForSerialization(root);
        return root.valid();
    }

    // [COMPAT-IMPORT-ONLY] Secondary path: old flat-param array blobs written before
    // the schema-root binary format. This branch exists only to load pre-migration
    // patches from very early plugin versions. It must not be reached by any host
    // session that was saved by the current encoder (which always writes the canonical
    // binary format). If this branch is taken for a new-format blob, decodeSidStateRootBinary
    // has failed for a reason that should be investigated — not silently swallowed here.
    if (!blob || blobSize < sizeof(SidBinaryStateHeader)) return false;
    if (expectedMagic == kPatchStateMagic || expectedMagic == kProjectStateMagic || expectedMagic == kStateMagic) {
        // FIX 3.3: Hard version gate. If the blob has the old flat-param magic but
        // carries a version number >= the current canonical major version, it is either
        // a new-format blob that decodeSidStateRootBinary already rejected (and we must
        // not silently corrupt it), or a malformed blob crafted to force the legacy path.
        // Reject rather than falling back.
        if (blobSize >= 6) {
            const uint16_t blobMajor = static_cast<uint16_t>(blob[4]) |
                                       (static_cast<uint16_t>(blob[5]) << 8);
            if (blobMajor >= kStateMajorVersion) return false;
        }
        float legacyParams[kNumParams]{};
        if (!decodeNormalizedStateLegacyInternal(blob, blobSize, legacyParams, kNumParams, expectedMagic)) return false;
        root = importPresentationParamsToStateRootForSchema(legacyParams, kNumParams,
                                                       FactorySlotSchema::Legacy128);
        sanitizePersistentStateRootForSerialization(root);
        return root.valid();
    }
    return false;
}

inline size_t encodeStateRoot(const SidStateRootV1& rootIn,
                          uint32_t magic, uint8_t* out, size_t outSize) noexcept {
    (void)magic;
    SidStateRootV1 root = rootIn;
    sanitizePersistentStateRootForSerialization(root);
    if (!root.valid()) return 0;
    return encodeSidStateRootBinary(root, out, outSize);
}

inline bool decodeStateToRoot(const uint8_t* blob, size_t blobSize,
                        SidStateRootV1& root,
                        uint32_t expectedMagic) noexcept {
    return decodeStateRootCanonical(blob, blobSize, root, expectedMagic);
}


static constexpr size_t kStateBufferSize = 1024u * 1024u;

} // namespace ArpSID
