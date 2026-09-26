// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// digi_sample_bank_v596.h - saved user-sample bank for the DIGI tab.

#ifndef ARPSID_GUI_DIGI_SAMPLE_BANK_V596_H
#define ARPSID_GUI_DIGI_SAMPLE_BANK_V596_H

#include "arpsid/gui/digi_panel_model.h"
#include "arpsid/gui/digi_record_limits.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ArpSID {
namespace GUI {

inline constexpr std::uint32_t kDigiSampleBankSchemaVersion = 1u;
inline constexpr std::uint8_t  kDigiUserSampleSlotCount = kDigiActiveSlotCount;
inline constexpr std::uint16_t kDigiUserSampleMaxFrames = kDigiUserSampleMaxFramesLong;
// User REC/IMPORT is persisted as a C64-style $D418 byte stream, not as
// arbitrary host-rate PCM. A classic DIGI player advances one 4-bit sample
// per volume-register write, so imported audio is pre-decimated to this
// canonical write-rate and playback defaults to one stored byte per DIGI tick.
inline constexpr std::uint8_t  kDigiUserSampleNameBytes = 32u;
inline constexpr std::uint16_t kDigiUserSampleFlagPresent = 0x0001u;
inline constexpr std::uint16_t kDigiUserSampleFlagMask = kDigiUserSampleFlagPresent;

struct DigiUserSampleClip {
    std::uint32_t handle;
    std::uint32_t sourceHash;
    std::uint32_t sourceSampleRateHz;
    std::uint16_t frameCount;
    std::uint16_t flags;
    char          displayName[kDigiUserSampleNameBytes];
    std::int8_t   pcm[kDigiUserSampleMaxFrames];
};

struct DigiSampleBankBlob {
    std::uint32_t schemaVersion;
    std::uint8_t  activeClipCount;
    std::uint8_t  reserved[3];
    DigiUserSampleClip clips[kDigiUserSampleSlotCount];
};

static_assert(sizeof(DigiUserSampleClip) == kDigiUserSampleClipPinnedBytes,
              "DigiUserSampleClip pinned to centralized DIGI sample-bank limit");
static_assert(sizeof(DigiSampleBankBlob) == kDigiSampleBankBlobPinnedBytes,
              "DigiSampleBankBlob pinned to centralized DIGI sample-bank limit");
static_assert(std::is_trivially_copyable<DigiUserSampleClip>::value,
              "DigiUserSampleClip must be trivially copyable");
static_assert(std::is_trivially_copyable<DigiSampleBankBlob>::value,
              "DigiSampleBankBlob must be trivially copyable");

[[nodiscard]] constexpr DigiUserSampleClip makeDefaultDigiUserSampleClip() noexcept {
    return DigiUserSampleClip{};
}

inline void resetDigiSampleBankBlob(DigiSampleBankBlob& b) noexcept {
    std::memset(&b, 0, sizeof(b));
    b.schemaVersion = kDigiSampleBankSchemaVersion;
}

[[nodiscard]] inline DigiSampleBankBlob makeDefaultDigiSampleBankBlob() noexcept {
    DigiSampleBankBlob b{};
    resetDigiSampleBankBlob(b);
    return b;
}

[[nodiscard]] inline std::uint32_t digiFnv1a32(const void* data,
                                                std::size_t byteCount,
                                                std::uint32_t seed = 2166136261u) noexcept {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    std::uint32_t h = seed ? seed : 2166136261u;
    for (std::size_t i = 0; i < byteCount; ++i) {
        h ^= static_cast<std::uint32_t>(p[i]);
        h *= 16777619u;
    }
    return h ? h : 2166136261u;
}

// Converts normalized PCM into the exact 4-bit value a C64 DIGI player
// can place in the low nibble of $D418. User recordings/imports are stored as
// de-quantized representatives of these 16 legal codes, not as hi-fi PCM, so
// the render path cannot accidentally become an interpolating 8-bit sampler.
[[nodiscard]] inline std::uint8_t digiFloatToD418Nibble(float v) noexcept {
    if (!std::isfinite(v)) v = 0.0f;
    const float clamped = std::clamp(v, -1.0f, 1.0f);
    const int code = static_cast<int>(std::lround((clamped + 1.0f) * 7.5f));
    return static_cast<std::uint8_t>(std::clamp(code, 0, 15));
}

[[nodiscard]] constexpr std::int8_t digiD418NibbleToPcm8(std::uint8_t nibble) noexcept {
    return static_cast<std::int8_t>((static_cast<int>(nibble & 0x0Fu) * 17) - 128);
}

[[nodiscard]] constexpr std::uint8_t digiPcm8ToD418Nibble(std::int8_t pcm) noexcept {
    const int biased = static_cast<int>(pcm) + 128;
    return static_cast<std::uint8_t>(std::clamp((biased + 8) / 17, 0, 15));
}

[[nodiscard]] inline bool digiClipIsC64D418Quantized(const DigiUserSampleClip& c) noexcept {
    if ((c.flags & kDigiUserSampleFlagPresent) == 0u) return true;
    if (c.frameCount == 0u || c.frameCount > kDigiUserSampleMaxFrames) return false;
    for (std::uint16_t i = 0; i < c.frameCount; ++i) {
        if (c.pcm[i] != digiD418NibbleToPcm8(digiPcm8ToD418Nibble(c.pcm[i]))) {
            return false;
        }
    }
    return true;
}


[[nodiscard]] inline bool digiDisplayNameStorageIsClean(const DigiUserSampleClip& c) noexcept {
    bool seenNul = false;
    for (std::uint8_t i = 0; i < kDigiUserSampleNameBytes; ++i) {
        const char ch = c.displayName[i];
        if (seenNul) {
            if (ch != '\0') return false;
            continue;
        }
        if (ch == '\0') {
            seenNul = true;
            continue;
        }
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 32u || uch > 126u) return false;
    }
    return seenNul;
}

inline void digiScrubDisplayNameStorage(DigiUserSampleClip& c) noexcept {
    bool seenNul = false;
    for (std::uint8_t i = 0; i < kDigiUserSampleNameBytes; ++i) {
        char& ch = c.displayName[i];
        if (seenNul) {
            ch = '\0';
            continue;
        }
        if (ch == '\0') {
            seenNul = true;
            continue;
        }
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 32u || uch > 126u) ch = '_';
        if (i == kDigiUserSampleNameBytes - 1u) {
            ch = '\0';
            seenNul = true;
        }
    }
    if (!seenNul) c.displayName[kDigiUserSampleNameBytes - 1u] = '\0';
}

[[nodiscard]] inline bool digiActiveClipTailIsClean(const DigiUserSampleClip& c) noexcept {
    if (c.frameCount > kDigiUserSampleMaxFrames) return false;
    for (std::uint16_t i = c.frameCount; i < kDigiUserSampleMaxFrames; ++i) {
        if (c.pcm[i] != 0) return false;
    }
    return true;
}

inline void digiQuantizeClipToC64D418(DigiUserSampleClip& c) noexcept {
    if ((c.flags & kDigiUserSampleFlagPresent) == 0u) return;
    const std::uint16_t n = std::min<std::uint16_t>(c.frameCount, kDigiUserSampleMaxFrames);
    for (std::uint16_t i = 0; i < n; ++i) {
        c.pcm[i] = digiD418NibbleToPcm8(digiPcm8ToD418Nibble(c.pcm[i]));
    }
    for (std::uint16_t i = n; i < kDigiUserSampleMaxFrames; ++i) {
        c.pcm[i] = 0;
    }
}

[[nodiscard]] inline std::uint32_t digiMakeUserSampleHandle(std::uint8_t slot,
                                                             std::uint32_t sourceHash,
                                                             std::uint32_t sampleRateHz,
                                                             std::uint16_t frameCount) noexcept {
    std::uint32_t words[4] = {
        static_cast<std::uint32_t>(slot) + 1u,
        sourceHash ? sourceHash : 0x9E3779B9u,
        sampleRateHz ? sampleRateHz : 44100u,
        static_cast<std::uint32_t>(frameCount) + 0x85EBCA6Bu
    };
    std::uint32_t h = digiFnv1a32(words, sizeof(words));
    h ^= h >> 16u;
    h *= 0x7FEB352Du;
    h ^= h >> 15u;
    if (h == 0u) h = 0xA5A5A5A5u ^ static_cast<std::uint32_t>(slot + 1u);
    return h;
}


[[nodiscard]] inline std::uint32_t digiCanonicalD418TargetFrameCountRaw(std::uint32_t frameCount,
                                                                        std::uint32_t sampleRateHz) noexcept {
    if (frameCount == 0u) return 0u;
    if (sampleRateHz < 1000u || sampleRateHz > 384000u) sampleRateHz = 44100u;
    const double durationSeconds = static_cast<double>(frameCount) / static_cast<double>(sampleRateHz);
    return static_cast<std::uint32_t>(std::max<double>(
        1.0, std::floor(durationSeconds * static_cast<double>(kDigiUserSampleCanonicalRateHz) + 0.5)));
}

[[nodiscard]] inline bool digiWouldTruncateToUserSampleBank(std::uint32_t frameCount,
                                                            std::uint32_t sampleRateHz) noexcept {
    return digiCanonicalD418TargetFrameCountRaw(frameCount, sampleRateHz) > kDigiUserSampleMaxFrames;
}

[[nodiscard]] inline std::uint32_t digiComputeUserClipSourceHash(const DigiUserSampleClip& c) noexcept {
    std::uint32_t h = digiFnv1a32(c.pcm, c.frameCount);
    h = digiFnv1a32(c.displayName, kDigiUserSampleNameBytes, h);
    h = digiFnv1a32(&c.sourceSampleRateHz, sizeof(c.sourceSampleRateHz), h);
    return h;
}

inline void digiRefreshUserClipIdentityForSlot(DigiUserSampleClip& c, std::uint8_t slot) noexcept {
    if ((c.flags & kDigiUserSampleFlagPresent) == 0u) {
        c.sourceHash = 0u;
        c.handle = 0u;
        return;
    }
    c.sourceSampleRateHz = kDigiUserSampleCanonicalRateHz;
    c.sourceHash = digiComputeUserClipSourceHash(c);
    c.handle = digiMakeUserSampleHandle(slot, c.sourceHash, c.sourceSampleRateHz, c.frameCount);
}

[[nodiscard]] constexpr bool digiUserSampleClipIsPresent(const DigiUserSampleClip& c) noexcept {
    return (c.flags & kDigiUserSampleFlagPresent) != 0u;
}

[[nodiscard]] inline bool digiUserSampleClipIsStructurallySalvageable(const DigiUserSampleClip& c) noexcept {
    if (c.flags & ~kDigiUserSampleFlagMask) return false;
    if (!digiUserSampleClipIsPresent(c)) {
        if (c.handle != 0u || c.sourceHash != 0u ||
            c.sourceSampleRateHz != 0u || c.frameCount != 0u) {
            return false;
        }
        for (std::uint8_t i = 0; i < kDigiUserSampleNameBytes; ++i) {
            if (c.displayName[i] != '\0') return false;
        }
        for (std::uint16_t i = 0; i < kDigiUserSampleMaxFrames; ++i) {
            if (c.pcm[i] != 0) return false;
        }
        return true;
    }
    if (c.handle == 0u) return false;
    if (c.frameCount == 0u || c.frameCount > kDigiUserSampleMaxFrames) return false;
    // Legacy/restored projects may still carry their original host sample-rate
    // metadata. Such clips are salvageable by sanitizeDigiSampleBankBlob(),
    // but they are not well-formed for realtime/auth playback until the rate
    // has been forced back to the canonical 8 kHz $D418 write stream.
    if (c.sourceSampleRateHz < 1000u || c.sourceSampleRateHz > 384000u) return false;
    return true;
}

[[nodiscard]] inline bool digiUserSampleClipIsWellFormed(const DigiUserSampleClip& c) noexcept {
    if (!digiUserSampleClipIsStructurallySalvageable(c)) return false;
    if (digiUserSampleClipIsPresent(c)) {
        if (!digiDisplayNameStorageIsClean(c)) return false;
        if (!digiActiveClipTailIsClean(c)) return false;
        if (c.sourceSampleRateHz != kDigiUserSampleCanonicalRateHz) return false;
        // Realtime/auth lookup must not accept an arbitrary 8-bit PCM payload
        // merely because it already carries the canonical 8 kHz rate. Older
        // or corrupted project state is still structurally salvageable and is
        // repaired by sanitizeDigiSampleBankBlob(), but the live well-formed
        // contract is stricter: persisted user samples are exactly the 16
        // representable $D418 low-nibble codes.
        if (!digiClipIsC64D418Quantized(c)) return false;
    }
    return true;
}

[[nodiscard]] inline bool digiSampleBankIsWellFormed(const DigiSampleBankBlob& b) noexcept {
    if (b.schemaVersion != kDigiSampleBankSchemaVersion) return false;
    if (b.reserved[0] || b.reserved[1] || b.reserved[2]) return false;
    std::uint8_t active = 0u;
    for (std::uint8_t i = 0; i < kDigiUserSampleSlotCount; ++i) {
        const DigiUserSampleClip& c = b.clips[i];
        if (!digiUserSampleClipIsWellFormed(c)) return false;
        if (digiUserSampleClipIsPresent(c)) ++active;
    }
    return b.activeClipCount == active;
}

inline void digiRecountSampleBank(DigiSampleBankBlob& b) noexcept {
    std::uint8_t active = 0u;
    for (std::uint8_t i = 0; i < kDigiUserSampleSlotCount; ++i) {
        if (digiUserSampleClipIsPresent(b.clips[i])) ++active;
    }
    b.activeClipCount = active;
}

inline void digiClearUserSample(DigiSampleBankBlob& b, std::uint8_t index) noexcept {
    if (index >= kDigiUserSampleSlotCount) return;
    b.clips[index] = makeDefaultDigiUserSampleClip();
    digiRecountSampleBank(b);
}

inline void sanitizeDigiSampleBankBlob(DigiSampleBankBlob& b) noexcept {
    if (b.schemaVersion != kDigiSampleBankSchemaVersion) {
        resetDigiSampleBankBlob(b);
        return;
    }
    b.reserved[0] = b.reserved[1] = b.reserved[2] = 0u;
    for (std::uint8_t i = 0; i < kDigiUserSampleSlotCount; ++i) {
        DigiUserSampleClip& c = b.clips[i];
        if (!digiUserSampleClipIsPresent(c)) {
            // Empty slots must be byte-clean. Older project states could carry
            // stale names/PCM in an inactive clip with flags==0; that data must
            // never survive as a hidden future DIGI payload.
            c = makeDefaultDigiUserSampleClip();
        } else if (!digiUserSampleClipIsStructurallySalvageable(c)) {
            c = makeDefaultDigiUserSampleClip();
        } else {
            digiScrubDisplayNameStorage(c);
            // C64-auth restore hardening: older projects may contain a
            // structurally valid user clip with a host/original sample rate
            // such as 22050/44100 Hz. User samples are now persisted as an
            // 8 kHz $D418 byte stream, so the playback scheduler must never
            // inherit the old host PCM rate from restored state. Keep the
            // handle/source hash stable so existing UserImport references
            // survive, but force the playback/write rate back to the
            // canonical C64 DIGI stream rate.
            c.sourceSampleRateHz = kDigiUserSampleCanonicalRateHz;
            digiQuantizeClipToC64D418(c);
        }
    }
    digiRecountSampleBank(b);
}

// Strict lookup is intended for GUI/load/save validation paths. It may scan
// the whole active clip/tail to prove structural cleanliness and is therefore
// not suitable for the realtime render callback when long DIGI clips are
// enabled.
[[nodiscard]] inline const DigiUserSampleClip* digiFindUserSampleClip(const DigiSampleBankBlob& b,
                                                                       std::uint8_t index,
                                                                       std::uint32_t handle) noexcept {
    if (b.schemaVersion != kDigiSampleBankSchemaVersion) return nullptr;
    if (index >= kDigiUserSampleSlotCount || handle == 0u) return nullptr;
    const DigiUserSampleClip& c = b.clips[index];
    if (!digiUserSampleClipIsWellFormed(c)) return nullptr;
    if (!digiUserSampleClipIsPresent(c)) return nullptr;
    return c.handle == handle ? &c : nullptr;
}

// Realtime lookup is O(1). The GUI/control side sanitizes and validates the
// sample bank before publishing it to the render projection, so the audio
// thread must only perform cheap schema/slot/handle/range checks and must not
// scan up to kDigiUserSampleMaxFrames every block.
[[nodiscard]] inline const DigiUserSampleClip* digiFindUserSampleClipRealtime(const DigiSampleBankBlob& b,
                                                                               std::uint8_t index,
                                                                               std::uint32_t handle) noexcept {
    if (b.schemaVersion != kDigiSampleBankSchemaVersion) return nullptr;
    if (index >= kDigiUserSampleSlotCount || handle == 0u) return nullptr;
    const DigiUserSampleClip& c = b.clips[index];
    if ((c.flags & kDigiUserSampleFlagPresent) == 0u) return nullptr;
    if (c.handle != handle) return nullptr;
    if (c.frameCount == 0u || c.frameCount > kDigiUserSampleMaxFrames) return nullptr;
    if (c.sourceSampleRateHz != kDigiUserSampleCanonicalRateHz) return nullptr;
    return &c;
}

inline void digiCopyDisplayName(char (&dst)[kDigiUserSampleNameBytes],
                                const char* src,
                                std::uint32_t srcLen) noexcept {
    std::memset(dst, 0, kDigiUserSampleNameBytes);
    const char* fallback = "User Sample";
    if (!src || srcLen == 0u) {
        src = fallback;
        srcLen = 11u;
    }
    const std::uint32_t n = std::min<std::uint32_t>(srcLen, kDigiUserSampleNameBytes - 1u);
    for (std::uint32_t i = 0; i < n; ++i) {
        const unsigned char ch = static_cast<unsigned char>(src[i]);
        dst[i] = (ch >= 32u && ch <= 126u) ? static_cast<char>(ch) : '_';
    }
}


// Repairs cross-blob user-sample references after model/bank restore or atomic
// publication. UserImport is a persisted/live reference and therefore requires
// a non-zero stable clip handle that resolves in the matching bank snapshot.
// Transient GUI intent must not leak into saved/realtime state as handle==0.
inline void digiRepairUserSampleReferences(DigiPanelModel& model,
                                           const DigiSampleBankBlob& bank) noexcept {
    for (std::uint8_t i = 0; i < kDigiActiveSlotCount; ++i) {
        DigiSampleSlot& s = model.slots[i];
        if (s.sourceType != DigiSourceType::UserImport) continue;
        if (s.userSampleHandle == 0u || !digiFindUserSampleClip(bank, s.userSampleIndex, s.userSampleHandle)) {
            digiSetNoSource(s);
        }
    }
}

[[nodiscard]] inline bool digiBuildUserSampleClipFromFloatMono(DigiUserSampleClip& out,
                                                                        std::uint8_t slot,
                                                                        const float* samples,
                                                                        std::uint32_t frameCount,
                                                                        std::uint32_t sampleRateHz,
                                                                        const char* name,
                                                                        std::uint32_t nameLen) noexcept {
    out = makeDefaultDigiUserSampleClip();
    if (slot >= kDigiUserSampleSlotCount || !samples || frameCount == 0u) return false;
    if (sampleRateHz < 1000u || sampleRateHz > 384000u) sampleRateHz = 44100u;

    out.sourceSampleRateHz = kDigiUserSampleCanonicalRateHz;
    out.flags = kDigiUserSampleFlagPresent;
    digiCopyDisplayName(out.displayName, name, nameLen);

    // C64-auth storage: convert host-rate recording/import into a bounded
    // byte stream intended for one byte per $D418 write. Keep schema v1 under
    // eight seconds and uint16_t-compatible; longer host takes are visibly
    // reported as TRUNC by the caller.
    const std::uint32_t targetFramesRaw = digiCanonicalD418TargetFrameCountRaw(frameCount, sampleRateHz);
    out.frameCount = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(targetFramesRaw, kDigiUserSampleMaxFrames));

    for (std::uint16_t i = 0; i < out.frameCount; ++i) {
        const double srcPos = (static_cast<double>(i) * static_cast<double>(sampleRateHz)) /
                              static_cast<double>(kDigiUserSampleCanonicalRateHz);
        const std::uint32_t srcIndex = static_cast<std::uint32_t>(std::min<double>(
            static_cast<double>(frameCount - 1u), std::floor(srcPos + 0.5)));
        float v = samples[srcIndex];
        if (!std::isfinite(v)) v = 0.0f;
        out.pcm[i] = digiD418NibbleToPcm8(digiFloatToD418Nibble(v));
    }

    digiQuantizeClipToC64D418(out);
    digiRefreshUserClipIdentityForSlot(out, slot);
    return out.handle != 0u && out.frameCount > 0u &&
           out.frameCount <= kDigiUserSampleMaxFrames &&
           digiUserSampleClipIsWellFormed(out);
}

[[nodiscard]] inline bool digiLoadUserSampleFromFloatMono(DigiSampleBankBlob& b,
                                                           std::uint8_t slot,
                                                           const float* samples,
                                                           std::uint32_t frameCount,
                                                           std::uint32_t sampleRateHz,
                                                           const char* name,
                                                           std::uint32_t nameLen) noexcept {
    if (b.schemaVersion != kDigiSampleBankSchemaVersion) {
        resetDigiSampleBankBlob(b);
    }

    DigiUserSampleClip c = makeDefaultDigiUserSampleClip();
    if (!digiBuildUserSampleClipFromFloatMono(c, slot, samples, frameCount, sampleRateHz, name, nameLen)) {
        return false;
    }

    b.clips[slot] = c;
    sanitizeDigiSampleBankBlob(b);
    return digiFindUserSampleClip(b, slot, c.handle) != nullptr;
}

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_DIGI_SAMPLE_BANK_V596_H
