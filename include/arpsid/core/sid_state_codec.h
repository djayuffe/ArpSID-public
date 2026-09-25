#pragma once
#include "sid_serializer_schema.h"
#include "sid_runtime_state_root_presentation.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace ArpSID {

inline uint16_t sidReadLE16(const uint8_t* p) noexcept {
    return (uint16_t)p[0] | (uint16_t)(p[1] << 8);
}
inline uint32_t sidReadLE32(const uint8_t* p) noexcept {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void sidWriteLE16(uint8_t* p, uint16_t v) noexcept {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}
inline void sidWriteLE32(uint8_t* p, uint32_t v) noexcept {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static constexpr uint32_t kSidBinaryStateMagic        = 0x41535344u;  // "ASSD"
static constexpr uint32_t kSidBinaryStateMajorVersion = 1u;
static constexpr uint32_t kSidBinaryStateMinorVersion = 0u;
static constexpr uint32_t kSidBinaryPatchStateMagic   = 0x41535043u;  // "ASPC"
static constexpr uint32_t kSidBinaryProjectStateMagic = 0x41535052u;  // "ASPR"
static constexpr uint32_t kSidBinaryStateMaxParamCount    = 4096u;
static constexpr uint32_t kSidBinaryStateMaxSemanticCount = 4096u;
static constexpr uint32_t kSidBinaryStateMaxRouteCount    = 512u;
// Per-field string caps — tight to prevent large allocation from crafted blobs.
// A malformed blob with a 3x1MB string fields previously forced ~3 MB before rejection.
static constexpr uint32_t kSidBinaryStateMaxProgramNameBytes  = 512u;       // display name
static constexpr uint32_t kSidBinaryStateMaxProgramRefBytes   = 1024u;      // UUID/path ref
static constexpr uint32_t kSidBinaryStateMaxLayoutBlobBytes   = 65536u;     // editor layout JSON
// Unified alias kept for code that was written against the old constant.
// Mapped to the largest of the three per-type caps so existing checks stay conservative.
static constexpr uint32_t kSidBinaryStateMaxStringBytes = kSidBinaryStateMaxLayoutBlobBytes;

struct SidBinaryStateHeader {
    uint32_t magic        = 0;
    uint16_t majorVersion = kSidBinaryStateMajorVersion;
    uint16_t minorVersion = kSidBinaryStateMinorVersion;
    uint32_t paramCount   = 0;
    uint32_t featureFlags = 0;
    uint32_t checksum     = 0;

    bool valid(uint32_t expectedMagic) const noexcept {
        return magic == expectedMagic && paramCount <= kSidBinaryStateMaxParamCount;
    }
};
static_assert(sizeof(SidBinaryStateHeader) == 20, "SidBinaryStateHeader must be 20 bytes");

inline uint32_t sidAdler32(const uint8_t* data, size_t len) noexcept {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a)       % 65521u;
    }
    return (b << 16u) | a;
}

inline size_t encodedSidStateRootBinarySize(const SidStateRootV1& root) noexcept {
    const uint32_t routeCount = (uint32_t)root.patch.mod_routes.size();
    const uint32_t nameLen = (uint32_t)root.document.program_name.size();
    const uint32_t refLen = (uint32_t)root.document.current_program_ref.size();
    const uint32_t layoutLen = (uint32_t)root.document.editor_layout_blob.size();
    const size_t baseBytes = 6u * sizeof(uint32_t) +
                (size_t)nameLen + (size_t)refLen + (size_t)layoutLen;
    const uint32_t semanticCount = (uint32_t)root.patch.parameters.semantic_entries.size();
    const size_t extBytes =
        2u * sizeof(uint32_t) + /* EXT tag + version */
        sizeof(uint32_t) + static_cast<size_t>(semanticCount) * (sizeof(uint32_t) + sizeof(float)) + /* semantic params */
        5u * sizeof(uint32_t) + 2u * sizeof(float) + 1u + /* variant profile */
        10u * sizeof(float) + 2u * sizeof(uint32_t) + /* measured posterior */
        sizeof(uint32_t) + static_cast<size_t>(routeCount) * (2u + sizeof(float) + 3u) + /* mod routes */
        8u * sizeof(float) + /* macros */
        (1u + sizeof(float) + 1u + 1u + sizeof(int32_t)) + /* arp */
        (1u + sizeof(float) + sizeof(int32_t) + sizeof(float)) + /* seq */
        (sizeof(uint32_t) + 8u + sizeof(uint32_t)) + /* start policy */
        (3u * sizeof(uint16_t) + 3u + 3u) + /* SID runtime envelope counters + ADSR-delay hold */
        (2u * sizeof(float) + 1u + sizeof(uint32_t)); /* forensic analog state */
    return sizeof(SidBinaryStateHeader) + baseBytes + extBytes;
}

inline size_t encodeSidStateRootBinary(const SidStateRootV1& root,
                                       uint8_t* out,
                                       size_t outSize) noexcept {
    if (!out || outSize < sizeof(SidBinaryStateHeader)) return 0;
    const uint32_t paramCount = 0u;
    const uint32_t routeCount = (uint32_t)root.patch.mod_routes.size();
    const uint32_t nameLen = (uint32_t)root.document.program_name.size();
    const uint32_t refLen = (uint32_t)root.document.current_program_ref.size();
    const uint32_t layoutLen = (uint32_t)root.document.editor_layout_blob.size();
    const uint32_t extTag = 0x45585431u; // EXT1
    const uint32_t extVersion = 7u;
    const size_t baseBytes = 6u * sizeof(uint32_t) +
                (size_t)nameLen + (size_t)refLen + (size_t)layoutLen;
    const uint32_t semanticCount = (uint32_t)root.patch.parameters.semantic_entries.size();
    const size_t extBytes =
        2u * sizeof(uint32_t) + /* EXT tag + version */
        sizeof(uint32_t) + static_cast<size_t>(semanticCount) * (sizeof(uint32_t) + sizeof(float)) + /* semantic params */
        5u * sizeof(uint32_t) + 2u * sizeof(float) + 1u + /* variant profile */
        10u * sizeof(float) + 2u * sizeof(uint32_t) + /* measured posterior */
        sizeof(uint32_t) + static_cast<size_t>(routeCount) * (2u + sizeof(float) + 3u) + /* mod routes */
        8u * sizeof(float) + /* macros */
        (1u + sizeof(float) + 1u + 1u + sizeof(int32_t)) + /* arp */
        (1u + sizeof(float) + sizeof(int32_t) + sizeof(float)) + /* seq */
        (sizeof(uint32_t) + 8u + sizeof(uint32_t)) + /* start policy */
        (3u * sizeof(uint16_t) + 3u + 3u) + /* SID runtime envelope counters + ADSR-delay hold */
        (2u * sizeof(float) + 1u + sizeof(uint32_t)); /* forensic analog state */
    const size_t payloadBytes = baseBytes + extBytes;
    const size_t needed = encodedSidStateRootBinarySize(root);
    if (outSize < needed) return 0;

    uint8_t* pcur = out + sizeof(SidBinaryStateHeader);
    auto put32 = [&](uint32_t v) noexcept { sidWriteLE32(pcur, v); pcur += sizeof(v); };
    auto putFloat = [&](float v) noexcept { uint32_t bits = 0; std::memcpy(&bits, &v, sizeof(bits)); sidWriteLE32(pcur, bits); pcur += sizeof(bits); };
    auto putU8 = [&](uint8_t v) noexcept { *pcur++ = v; };
    auto putI32 = [&](int32_t v) noexcept { sidWriteLE32(pcur, static_cast<uint32_t>(v)); pcur += sizeof(v); };

    put32(root.magic);
    put32(root.schema_version);
    put32(0u); // canonical payload no longer serializes flat parameter arrays
    put32(nameLen);
    put32(refLen);
    put32(layoutLen);

    if (nameLen)   { std::memcpy(pcur, root.document.program_name.data(), nameLen); pcur += nameLen; }
    if (refLen)    { std::memcpy(pcur, root.document.current_program_ref.data(), refLen); pcur += refLen; }
    if (layoutLen) { std::memcpy(pcur, root.document.editor_layout_blob.data(), layoutLen); pcur += layoutLen; }

    put32(extTag);
    put32(extVersion);
    put32(semanticCount);
    for (uint32_t i = 0; i < semanticCount; ++i) {
        const auto& e = root.patch.parameters.semantic_entries[(size_t)i];
        put32(e.param_id);
        putFloat(std::isfinite(e.value) ? std::clamp(e.value, 0.0f, 1.0f) : 0.0f);
    }
    put32((uint32_t)root.patch.variant_profile.family);
    put32(root.patch.variant_profile.chip_revision_code);
    put32((uint32_t)root.patch.variant_profile.video_standard);
    put32((uint32_t)root.patch.variant_profile.board_revision);
    put32((uint32_t)root.patch.variant_profile.output_stage);
    putFloat(root.patch.variant_profile.master_clock_hz);
    putFloat(root.patch.variant_profile.nominal_sid_clock_hz);
    putU8(root.patch.variant_profile.allow_runtime_variant_switch ? 1u : 0u);

    putFloat(root.patch.posterior.confidence);
    putFloat(root.patch.posterior.cutoff_bias);
    putFloat(root.patch.posterior.resonance_bias);
    putFloat(root.patch.posterior.output_gain_bias);
    putFloat(root.patch.posterior.noise_bias);
    putFloat(root.patch.posterior.waveform_memory_bias);
    putFloat(root.patch.posterior.leakage_bias);
    putFloat(root.patch.posterior.dc_bias_millivolts);
    putFloat(root.patch.posterior.supply_sag_bias);
    putFloat(root.patch.posterior.thermal_tracking_bias);
    put32(root.patch.posterior.source_signature);
    put32(root.patch.posterior.stable_chip_identity);

    put32(routeCount);
    for (uint32_t i = 0; i < routeCount; ++i) {
        const SidModRoute& r = root.patch.mod_routes[(size_t)i];
        putU8((uint8_t)r.source);
        putU8((uint8_t)r.target);
        putFloat(r.depth);
        putU8((uint8_t)r.transform);
        putU8(r.bipolar ? 1u : 0u);
        putU8(r.enabled ? 1u : 0u);
    }
    for (float v : root.patch.macros.values) putFloat(v);
    putU8(root.patch.arp.enabled ? 1u : 0u);
    putFloat(root.patch.arp.rate);
    putU8(root.patch.arp.hold ? 1u : 0u);
    putU8(root.patch.arp.latch ? 1u : 0u);
    putI32(root.patch.arp.transpose);
    putU8(root.patch.seq.enabled ? 1u : 0u);
    putFloat(root.patch.seq.tempo);
    putI32(root.patch.seq.length);
    putFloat(root.patch.seq.swing);

    put32((uint32_t)root.patch.start_policy.id);
    putU8(root.patch.start_policy.gateOffBeforeStart ? 1u : 0u);
    putU8(root.patch.start_policy.useHardRestart ? 1u : 0u);
    putU8(root.patch.start_policy.strictHardRestart ? 1u : 0u);
    putU8(root.patch.start_policy.preloadWaveform ? 1u : 0u);
    putU8(root.patch.start_policy.preloadPulseWidth ? 1u : 0u);
    putU8(root.patch.start_policy.preloadFilterRoute ? 1u : 0u);
    putU8(root.patch.start_policy.preloadFrequency ? 1u : 0u);
    putU8(root.patch.start_policy.useTestBitPrecharge ? 1u : 0u);
    put32(root.patch.start_policy.postStartDelaySamples);

    for (uint16_t v : root.patch.sid_runtime.envelopeRateCounter) {
        sidWriteLE16(pcur, v);
        pcur += sizeof(uint16_t);
    }
    for (uint8_t v : root.patch.sid_runtime.envelopeExponentialCounter) putU8(v);
    for (bool v : root.patch.sid_runtime.envelopeAdsrDelayHold) putU8(v ? 1u : 0u);

    putFloat(root.patch.forensic_temperature_celsius);
    putFloat(root.patch.forensic_supply_voltage);
    putU8(root.patch.forensic_revision);
    put32(root.patch.forensic_chip_seed);

    SidBinaryStateHeader hdr{};
    hdr.magic = kSidBinaryProjectStateMagic;
    hdr.majorVersion = kSidBinaryStateMajorVersion;
    hdr.minorVersion = kSidBinaryStateMinorVersion;
    hdr.paramCount = paramCount;
    hdr.featureFlags = root.document.editor_metadata_present ? 1u : 0u;
    hdr.checksum = sidAdler32(out + sizeof(SidBinaryStateHeader), payloadBytes);
    sidWriteLE32(out + 0, hdr.magic);
    sidWriteLE16(out + 4, hdr.majorVersion);
    sidWriteLE16(out + 6, hdr.minorVersion);
    sidWriteLE32(out + 8, hdr.paramCount);
    sidWriteLE32(out + 12, hdr.featureFlags);
    sidWriteLE32(out + 16, hdr.checksum);
    return needed;
}

inline bool decodeSidStateRootBinary(const uint8_t* blob,
                                     size_t blobSize,
                                     SidStateRootV1& outRoot) noexcept {
    if (!blob || blobSize < sizeof(SidBinaryStateHeader)) return false;
    SidBinaryStateHeader hdr{};
    hdr.magic = sidReadLE32(blob + 0);
    hdr.majorVersion = sidReadLE16(blob + 4);
    hdr.minorVersion = sidReadLE16(blob + 6);
    hdr.paramCount = sidReadLE32(blob + 8);
    hdr.featureFlags = sidReadLE32(blob + 12);
    hdr.checksum = sidReadLE32(blob + 16);
    if (!hdr.valid(kSidBinaryProjectStateMagic)) return false;
    if (hdr.majorVersion > kSidBinaryStateMajorVersion) return false;
    const uint8_t* pcur = blob + sizeof(SidBinaryStateHeader);
    const size_t remain = blobSize - sizeof(SidBinaryStateHeader);
    if (sidAdler32(pcur, remain) != hdr.checksum) return false;

    auto take = [&](void* dst, size_t n) noexcept -> bool {
        if ((size_t)(blob + blobSize - pcur) < n) return false;
        std::memcpy(dst, pcur, n);
        pcur += n;
        return true;
    };
    auto get32 = [&](uint32_t& v) noexcept -> bool { if ((size_t)(blob + blobSize - pcur) < 4) return false; v = sidReadLE32(pcur); pcur += 4; return true; };
    auto getFloat = [&](float& v) noexcept -> bool { uint32_t bits = 0; if (!get32(bits)) return false; std::memcpy(&v, &bits, sizeof(v)); return true; };
    auto getU8 = [&](uint8_t& v) noexcept -> bool { return take(&v, sizeof(v)); };
    auto getI32 = [&](int32_t& v) noexcept -> bool { uint32_t bits = 0; if (!get32(bits)) return false; v = static_cast<int32_t>(bits); return true; };

    uint32_t rootMagic = 0, schema = 0, paramCount = 0, nameLen = 0, refLen = 0, layoutLen = 0;
    if (!get32(rootMagic) || !get32(schema) || !get32(paramCount) || !get32(nameLen) || !get32(refLen) || !get32(layoutLen)) return false;
    SidStateRootV1 root{};
    root.magic = rootMagic;
    root.schema_version = schema;
    root.document.editor_metadata_present = (hdr.featureFlags & 1u) != 0u;
    if (paramCount > kSidBinaryStateMaxParamCount ||
        nameLen   > kSidBinaryStateMaxProgramNameBytes ||
        refLen    > kSidBinaryStateMaxProgramRefBytes  ||
        layoutLen > kSidBinaryStateMaxLayoutBlobBytes) return false;

    std::vector<float> legacyValues;
    legacyValues.resize(static_cast<size_t>(paramCount));
    for (uint32_t i = 0; i < paramCount; ++i) {
        uint32_t bits = 0;
        if (!get32(bits)) return false;
        float fv = 0.f; std::memcpy(&fv, &bits, 4);
        if (!std::isfinite(fv)) fv = 0.f;
        legacyValues[(size_t)i] = std::clamp(fv, 0.f, 1.f);
    }
    auto takeString = [&](std::string& s, uint32_t n) noexcept -> bool {
        if ((size_t)(blob + blobSize - pcur) < n) return false;
        s.assign((const char*)pcur, (size_t)n);
        pcur += n;
        return true;
    };
    if (!takeString(root.document.program_name, nameLen)) return false;
    if (!takeString(root.document.current_program_ref, refLen)) return false;
    if (!takeString(root.document.editor_layout_blob, layoutLen)) return false;

    bool hasForensicExtension = false;
    if ((size_t)(blob + blobSize - pcur) > 0) {
        uint32_t extTag = 0, extVersion = 0;
        if (!get32(extTag) || !get32(extVersion)) return false;
        if (extTag != 0x45585431u || (extVersion != 1u && extVersion != 2u && extVersion != 3u && extVersion != 4u && extVersion != 5u && extVersion != 6u && extVersion != 7u)) return false;
        if (extVersion >= 4u) {
            uint32_t semanticCount = 0;
            if (!get32(semanticCount)) return false;
            if (semanticCount > kSidBinaryStateMaxSemanticCount) return false;
            root.patch.parameters.semantic_entries.clear();
            root.patch.parameters.semantic_entries.reserve(static_cast<size_t>(semanticCount));
            for (uint32_t i = 0; i < semanticCount; ++i) {
                uint32_t pid = 0;
                float val = 0.0f;
                if (!get32(pid) || !getFloat(val)) return false;
                if (!std::isfinite(val)) val = 0.0f;
                root.patch.parameters.semantic_entries.push_back(SidSemanticParamEntry{pid, std::clamp(val, 0.0f, 1.0f)});
            }
        }
        uint32_t family = 0, chipRev = 0, video = 0, board = 0, outputStage = 0;
        if (!get32(family) || !get32(chipRev) || !get32(video) || !get32(board) || !get32(outputStage)) return false;
        root.patch.variant_profile.family = (SidFamily)family;
        root.patch.variant_profile.chip_revision_code = chipRev;
        root.patch.variant_profile.video_standard = (SidVideoStandard)video;
        root.patch.variant_profile.board_revision = (SidBoardRevision)board;
        root.patch.variant_profile.output_stage = (SidOutputStageProfile)outputStage;
        if (!getFloat(root.patch.variant_profile.master_clock_hz) || !getFloat(root.patch.variant_profile.nominal_sid_clock_hz)) return false;
        uint8_t allowSwitch = 0;
        if (!getU8(allowSwitch)) return false;
        root.patch.variant_profile.allow_runtime_variant_switch = allowSwitch != 0;

        if (!getFloat(root.patch.posterior.confidence) || !getFloat(root.patch.posterior.cutoff_bias) ||
            !getFloat(root.patch.posterior.resonance_bias) || !getFloat(root.patch.posterior.output_gain_bias) ||
            !getFloat(root.patch.posterior.noise_bias)) return false;
        if (extVersion >= 3u) {
            if (!getFloat(root.patch.posterior.waveform_memory_bias) || !getFloat(root.patch.posterior.leakage_bias) ||
                !getFloat(root.patch.posterior.dc_bias_millivolts) || !getFloat(root.patch.posterior.supply_sag_bias) ||
                !getFloat(root.patch.posterior.thermal_tracking_bias) || !get32(root.patch.posterior.source_signature) ||
                !get32(root.patch.posterior.stable_chip_identity)) return false;
        } else {
            root.patch.posterior.waveform_memory_bias = 0.0f;
            root.patch.posterior.leakage_bias = 0.0f;
            root.patch.posterior.dc_bias_millivolts = 0.0f;
            root.patch.posterior.supply_sag_bias = 0.0f;
            root.patch.posterior.thermal_tracking_bias = 0.0f;
            root.patch.posterior.stable_chip_identity = 0;
            if (!get32(root.patch.posterior.source_signature)) return false;
        }

        uint32_t routeCount = 0;
        if (!get32(routeCount)) return false;
        if (routeCount > kSidBinaryStateMaxRouteCount) return false;
        root.patch.mod_routes.clear();
        root.patch.mod_routes.reserve(static_cast<size_t>(routeCount));
        for (uint32_t i = 0; i < routeCount; ++i) {
            SidModRoute r{};
            uint8_t src = 0, dst = 0, tf = 0, bipolar = 0, enabled = 0;
            if (!getU8(src) || !getU8(dst) || !getFloat(r.depth) || !getU8(tf) || !getU8(bipolar) || !getU8(enabled)) return false;
            r.source = (SidModSource)src;
            r.target = (SidModTarget)dst;
            r.transform = (SidModTransform)tf;
            r.bipolar = bipolar != 0;
            r.enabled = enabled != 0;
            r.sanitize();
            root.patch.mod_routes.push_back(r);
        }
        for (float& v : root.patch.macros.values) {
            if (!getFloat(v)) return false;
            if (!std::isfinite(v)) v = 0.f;
            v = std::clamp(v, 0.f, 1.f);
        }
        uint8_t b = 0;
        if (!getU8(b) || !getFloat(root.patch.arp.rate)) return false;
        root.patch.arp.enabled = b != 0;
        if (!getU8(b)) return false;
        root.patch.arp.hold = b != 0;
        if (!getU8(b)) return false;
        root.patch.arp.latch = b != 0;
        if (!getI32(root.patch.arp.transpose)) return false;
        if (!getU8(b)) return false;
        root.patch.seq.enabled = b != 0;
        if (!getFloat(root.patch.seq.tempo) || !getI32(root.patch.seq.length) || !getFloat(root.patch.seq.swing)) return false;

        if (extVersion >= 2u) {
            uint32_t startId = 0;
            uint8_t gateOff = 0, hardRestart = 0, strictHardRestart = 0;
            uint8_t preloadWaveform = 0, preloadPulseWidth = 0, preloadFilterRoute = 0;
            uint8_t preloadFrequency = 0, useTestBitPrecharge = 0;
            uint32_t postDelay = 0;
            if (!get32(startId) || !getU8(gateOff) || !getU8(hardRestart) || !getU8(strictHardRestart) ||
                !getU8(preloadWaveform) || !getU8(preloadPulseWidth) || !getU8(preloadFilterRoute) ||
                !getU8(preloadFrequency) || !getU8(useTestBitPrecharge) || !get32(postDelay)) return false;
            root.patch.start_policy.id = (StartPolicyId)startId;
            root.patch.start_policy.gateOffBeforeStart = gateOff != 0;
            root.patch.start_policy.useHardRestart = hardRestart != 0;
            root.patch.start_policy.strictHardRestart = strictHardRestart != 0;
            root.patch.start_policy.preloadWaveform = preloadWaveform != 0;
            root.patch.start_policy.preloadPulseWidth = preloadPulseWidth != 0;
            root.patch.start_policy.preloadFilterRoute = preloadFilterRoute != 0;
            root.patch.start_policy.preloadFrequency = preloadFrequency != 0;
            root.patch.start_policy.useTestBitPrecharge = useTestBitPrecharge != 0;
            root.patch.start_policy.postStartDelaySamples = postDelay;
        }
        if (extVersion >= 7u) {
            for (uint16_t& v : root.patch.sid_runtime.envelopeRateCounter) {
                if ((size_t)(blob + blobSize - pcur) < sizeof(uint16_t)) return false;
                v = sidReadLE16(pcur);
                pcur += sizeof(uint16_t);
            }
            for (uint8_t& v : root.patch.sid_runtime.envelopeExponentialCounter) {
                if (!getU8(v)) return false;
            }
            for (bool& v : root.patch.sid_runtime.envelopeAdsrDelayHold) {
                uint8_t hold = 0;
                if (!getU8(hold)) return false;
                v = hold != 0u;
            }
        }
        if (extVersion >= 6u) {
            hasForensicExtension = true;
            if (!getFloat(root.patch.forensic_temperature_celsius) ||
                !getFloat(root.patch.forensic_supply_voltage) ||
                !getU8(root.patch.forensic_revision) ||
                !get32(root.patch.forensic_chip_seed)) return false;
        }
    }

    if (pcur != blob + blobSize) return false;
    if (!root.valid()) return false;
    root.patch.variant_profile.sanitize();
    root.patch.posterior.sanitize();
    // [COMPAT-IMPORT-ONLY] If the blob was written by a pre-ext4 encoder it will not have
    // carried semantic_entries in the extension block. Reconstruct them from legacyValues so
    // old patches load correctly. This path does NOT author new state — it migrates old blobs.
    // New saves always write semantic_entries in the extension block (extVersion >= 4).
    if (root.patch.parameters.semantic_entries.empty() && !legacyValues.empty()) {
        root.patch.parameters.semantic_entries.reserve((size_t)kNumParams);
        for (uint32_t i = 0; i < legacyValues.size() && i < (uint32_t)kNumParams; ++i)
            root.patch.parameters.semantic_entries.push_back(SidSemanticParamEntry{i, legacyValues[(size_t)i]});
    }
    if (hasForensicExtension) {
        sidSetStateRootParamValue(root, kParamForensicTemp, sidForensicTemperatureToNormalized(root.patch.forensic_temperature_celsius));
        sidSetStateRootParamValue(root, kParamForensicSupply, sidForensicSupplyToNormalized(root.patch.forensic_supply_voltage));
        sidSetStateRootParamValue(root, kParamForensicRevision, sidForensicRevisionToNormalized(root.patch.forensic_revision));
        sidSetStateRootParamValue(root, kParamForensicChipSeed, sidForensicChipSeedToNormalized(root.patch.forensic_chip_seed));
    }
    sanitizePersistentStateRootForSerialization(root);
    outRoot = std::move(root);
    return true;
}

} // namespace ArpSID
