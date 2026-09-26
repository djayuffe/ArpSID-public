// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// v596 - DIGI saved user-sample bank contract tests.

#include "arpsid/gui/digi_sample_bank_v596.h"

#include <cassert>
#include <cstring>
#include <vector>

using namespace ArpSID::GUI;

static_assert(sizeof(DigiUserSampleClip) == 60048u, "DigiUserSampleClip size");
static_assert(sizeof(DigiSampleBankBlob) == 480392u, "DigiSampleBankBlob size");

static void defaultBankIsWellFormed() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    assert(digiSampleBankIsWellFormed(bank));
    assert(bank.schemaVersion == kDigiSampleBankSchemaVersion);
    assert(bank.activeClipCount == 0u);
    for (std::uint8_t i = 0; i < kDigiUserSampleSlotCount; ++i) {
        assert(!digiUserSampleClipIsPresent(bank.clips[i]));
    }
}

static void loadFloatMonoCreatesStableClip() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float src[9] = { -1.0f, -0.8f, -0.4f, 0.0f, 0.3f, 0.7f, 1.0f, 0.2f, -0.2f };
    assert(digiLoadUserSampleFromFloatMono(bank, 2u, src, 9u, 32000u, "snare.wav", 9u));
    assert(digiSampleBankIsWellFormed(bank));
    assert(bank.activeClipCount == 1u);
    const DigiUserSampleClip& c = bank.clips[2];
    assert(digiUserSampleClipIsPresent(c));
    assert(c.handle != 0u);
    assert(c.sourceHash != 0u);
    assert(c.sourceSampleRateHz == kDigiUserSampleCanonicalRateHz);
    assert(c.frameCount == 2u);
    assert(std::strcmp(c.displayName, "snare.wav") == 0);
    assert(digiFindUserSampleClip(bank, 2u, c.handle) == &c);
    assert(digiFindUserSampleClip(bank, 2u, c.handle + 1u) == nullptr);
}

static void longSampleResamplesIntoPinnedCapacity() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    std::vector<float> src(70000u);
    for (std::size_t i = 0; i < src.size(); ++i) src[i] = (i & 1u) ? 0.5f : -0.5f;
    assert(digiLoadUserSampleFromFloatMono(bank, 7u, src.data(), static_cast<std::uint32_t>(src.size()), kDigiUserSampleCanonicalRateHz, "long", 4u));
    assert(bank.clips[7].frameCount == kDigiUserSampleMaxFrames);
    assert(digiSampleBankIsWellFormed(bank));
}


static void importDecimatesToC64D418ByteStreamWithoutInterpolation() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float src[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
    assert(digiLoadUserSampleFromFloatMono(bank, 0u, src, 4u, kDigiUserSampleCanonicalRateHz * 2u, "decim", 5u));
    assert(bank.clips[0].sourceSampleRateHz == kDigiUserSampleCanonicalRateHz);
    assert(bank.clips[0].frameCount == 2u);
    assert(bank.clips[0].pcm[0] == digiD418NibbleToPcm8(0u));
    assert(bank.clips[0].pcm[1] == digiD418NibbleToPcm8(0u));
    assert(digiClipIsC64D418Quantized(bank.clips[0]));
}

static void loadAndSanitizePreserveOnlyC64D418Codes() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float src[17] = { -1.0f, -0.93f, -0.80f, -0.66f, -0.50f, -0.33f, -0.20f, -0.07f,
                            0.0f, 0.07f, 0.20f, 0.33f, 0.50f, 0.66f, 0.80f, 0.93f, 1.0f };
    assert(digiLoadUserSampleFromFloatMono(bank, 0u, src, 17u, 22050u, "auth4", 5u));
    assert(digiClipIsC64D418Quantized(bank.clips[0]));
    for (std::uint16_t i = 0; i < bank.clips[0].frameCount; ++i) {
        const std::uint8_t nibble = digiPcm8ToD418Nibble(bank.clips[0].pcm[i]);
        assert(bank.clips[0].pcm[i] == digiD418NibbleToPcm8(nibble));
    }

    // Project restore or older banks may carry arbitrary 8-bit PCM while still
    // being structurally well-formed. Sanitization must snap them back to the
    // 16 legal $D418 volume-DAC codes instead of preserving hi-fi PCM.
    bank.clips[0].pcm[0] = 43;
    assert(!digiClipIsC64D418Quantized(bank.clips[0]));
    sanitizeDigiSampleBankBlob(bank);
    assert(digiSampleBankIsWellFormed(bank));
    assert(digiClipIsC64D418Quantized(bank.clips[0]));
}


static void sanitizeForcesRestoredClipsToCanonicalD418Rate() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float src[8] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 0.5f, 0.0f, -0.5f };
    assert(digiLoadUserSampleFromFloatMono(bank, 4u, src, 8u, 44100u, "oldrate", 7u));

    const std::uint32_t stableHandle = bank.clips[4].handle;
    bank.clips[4].sourceSampleRateHz = 44100u;  // salvageable legacy/restored state
    assert(digiUserSampleClipIsStructurallySalvageable(bank.clips[4]));
    assert(!digiUserSampleClipIsWellFormed(bank.clips[4]));
    assert(digiFindUserSampleClip(bank, 4u, stableHandle) == nullptr);

    sanitizeDigiSampleBankBlob(bank);

    assert(digiSampleBankIsWellFormed(bank));
    assert(bank.clips[4].handle == stableHandle);
    assert(bank.clips[4].sourceSampleRateHz == kDigiUserSampleCanonicalRateHz);
    assert(digiFindUserSampleClip(bank, 4u, stableHandle) == &bank.clips[4]);
}

static void unsanitizedLegacyRateCannotReachRealtimeLookup() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float src[8] = { -1.0f, -0.75f, -0.5f, -0.25f, 0.25f, 0.5f, 0.75f, 1.0f };
    assert(digiLoadUserSampleFromFloatMono(bank, 5u, src, 8u, 48000u, "legacy", 6u));
    const std::uint32_t stableHandle = bank.clips[5].handle;

    bank.clips[5].sourceSampleRateHz = 48000u;
    assert(digiUserSampleClipIsStructurallySalvageable(bank.clips[5]));
    assert(!digiSampleBankIsWellFormed(bank));
    assert(digiFindUserSampleClip(bank, 5u, stableHandle) == nullptr);

    sanitizeDigiSampleBankBlob(bank);
    assert(digiSampleBankIsWellFormed(bank));
    assert(bank.clips[5].sourceSampleRateHz == kDigiUserSampleCanonicalRateHz);
    assert(digiFindUserSampleClip(bank, 5u, stableHandle) == &bank.clips[5]);
}


static void unsanitizedCanonicalRateButNonD418PayloadCannotReachRealtimeLookup() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float src[8] = { -1.0f, -0.75f, -0.5f, -0.25f, 0.25f, 0.5f, 0.75f, 1.0f };
    assert(digiLoadUserSampleFromFloatMono(bank, 6u, src, 8u, 48000u, "pcm8", 4u));
    const std::uint32_t stableHandle = bank.clips[6].handle;

    // Simulate restored/corrupt project state that already has the canonical
    // auth stream rate but still contains arbitrary 8-bit PCM. It is
    // structurally salvageable, but it must not be treated as live/realtime
    // well-formed until sanitize snaps it back to the 16 legal $D418 codes.
    bank.clips[6].sourceSampleRateHz = kDigiUserSampleCanonicalRateHz;
    bank.clips[6].pcm[0] = 43;

    assert(digiUserSampleClipIsStructurallySalvageable(bank.clips[6]));
    assert(!digiClipIsC64D418Quantized(bank.clips[6]));
    assert(!digiUserSampleClipIsWellFormed(bank.clips[6]));
    assert(!digiSampleBankIsWellFormed(bank));
    assert(digiFindUserSampleClip(bank, 6u, stableHandle) == nullptr);

    sanitizeDigiSampleBankBlob(bank);
    assert(digiSampleBankIsWellFormed(bank));
    assert(bank.clips[6].handle == stableHandle);
    assert(bank.clips[6].sourceSampleRateHz == kDigiUserSampleCanonicalRateHz);
    assert(digiClipIsC64D418Quantized(bank.clips[6]));
    assert(digiFindUserSampleClip(bank, 6u, stableHandle) == &bank.clips[6]);
}

static void sanitizeClearsMalformedClip() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float src[2] = { -1.0f, 1.0f };
    assert(digiLoadUserSampleFromFloatMono(bank, 1u, src, 2u, 22050u, "ok", 2u));
    bank.clips[1].frameCount = 0u;
    sanitizeDigiSampleBankBlob(bank);
    assert(digiSampleBankIsWellFormed(bank));
    assert(bank.activeClipCount == 0u);
    assert(!digiUserSampleClipIsPresent(bank.clips[1]));
}


static void sanitizeClearsInactiveClipPayloadCompletely() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    bank.clips[0].flags = 0u;
    bank.clips[0].handle = 0u;
    bank.clips[0].sourceHash = 0u;
    bank.clips[0].sourceSampleRateHz = 0u;
    bank.clips[0].frameCount = 0u;
    bank.clips[0].displayName[0] = 'X';
    bank.clips[0].pcm[0] = 43;

    assert(!digiUserSampleClipIsStructurallySalvageable(bank.clips[0]));
    sanitizeDigiSampleBankBlob(bank);

    assert(digiSampleBankIsWellFormed(bank));
    assert(bank.activeClipCount == 0u);
    assert(!digiUserSampleClipIsPresent(bank.clips[0]));
    assert(bank.clips[0].displayName[0] == '\0');
    assert(bank.clips[0].pcm[0] == 0);
}


static void sanitizeClearsActiveClipHiddenTailAndDisplayStorage() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float src[4] = { -1.0f, -0.25f, 0.25f, 1.0f };
    assert(digiLoadUserSampleFromFloatMono(bank, 1u, src, 4u, kDigiUserSampleCanonicalRateHz, "tail", 4u));
    const std::uint32_t stableHandle = bank.clips[1].handle;

    // Simulate project/persistence garbage hidden outside the audible C64
    // $D418 byte stream: bytes after frameCount and after the first NUL in the
    // label must not be considered live-auth well-formed.
    bank.clips[1].pcm[bank.clips[1].frameCount] = 43;
    bank.clips[1].displayName[4] = '\0';
    bank.clips[1].displayName[5] = 'X';

    assert(digiUserSampleClipIsStructurallySalvageable(bank.clips[1]));
    assert(!digiUserSampleClipIsWellFormed(bank.clips[1]));
    assert(digiFindUserSampleClip(bank, 1u, stableHandle) == nullptr);

    sanitizeDigiSampleBankBlob(bank);

    assert(digiSampleBankIsWellFormed(bank));
    assert(bank.clips[1].handle == stableHandle);
    assert(digiFindUserSampleClip(bank, 1u, stableHandle) == &bank.clips[1]);
    for (std::uint16_t i = bank.clips[1].frameCount; i < kDigiUserSampleMaxFrames; ++i) {
        assert(bank.clips[1].pcm[i] == 0);
    }
    assert(std::strcmp(bank.clips[1].displayName, "tail") == 0);
    for (std::uint8_t i = 5u; i < kDigiUserSampleNameBytes; ++i) {
        assert(bank.clips[1].displayName[i] == '\0');
    }
}

static void repairClearsAllStaleOrZeroUserHandles() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    float src[4] = { -1.0f, -0.25f, 0.25f, 1.0f };
    assert(digiLoadUserSampleFromFloatMono(bank, 2u, src, 4u, 44100u, "hit", 3u));

    DigiPanelModel model = makeDefaultDigiPanelModel();
    digiSetUserSampleSlot(model.slots[0], 2u, bank.clips[2].handle);
    digiSetUserSampleSlot(model.slots[1], 2u, bank.clips[2].handle + 1u);
    digiSetUserSampleSlot(model.slots[3], 3u, 0u);

    digiRepairUserSampleReferences(model, bank);

    assert(model.slots[0].sourceType == DigiSourceType::UserImport);
    assert(model.slots[0].userSampleHandle == bank.clips[2].handle);
    assert(model.slots[1].sourceType == DigiSourceType::None);
    assert(model.slots[1].userSampleIndex == 0u);
    assert(model.slots[1].userSampleHandle == 0u);
    assert(model.slots[3].sourceType == DigiSourceType::None);
    assert(model.slots[3].userSampleIndex == 0u);
    assert(model.slots[3].userSampleHandle == 0u);
}

static void longRecordSchemaV1LimitIsUnderEightSecondsAndPinned() {
    static_assert(kDigiUserSampleMaxFrames == 60000u,
                  "PASS260/261 long DIGI payload stays at 60000 canonical bytes");
    static_assert(kDigiUserSampleMaxSeconds == 7.5,
                  "60000 canonical frames at 8 kHz is exactly 7.5 seconds");
    static_assert(kDigiUserSampleMaxSeconds < 8.0,
                  "schema v1 long DIGI payload must remain under eight seconds");
    static_assert(sizeof(DigiUserSampleClip) == kDigiUserSampleClipPinnedBytes,
                  "clip size follows central long-record constant");
    static_assert(sizeof(DigiSampleBankBlob) == kDigiSampleBankBlobPinnedBytes,
                  "bank size follows central long-record constant");

    DigiUserSampleClip clip = makeDefaultDigiUserSampleClip();
    std::vector<float> exact(360000u, 0.5f); // 7.5 s at 48 kHz -> exactly 60000 bytes at 8 kHz.
    assert(digiBuildUserSampleClipFromFloatMono(clip, 0u, exact.data(),
                                                (std::uint32_t)exact.size(),
                                                48000u, "exact75", 7u));
    assert(clip.frameCount == kDigiUserSampleMaxFrames);
    assert(!digiWouldTruncateToUserSampleBank((std::uint32_t)exact.size(), 48000u));

    std::vector<float> over(360004u, 0.5f);
    assert(digiWouldTruncateToUserSampleBank((std::uint32_t)over.size(), 48000u));
}

static void truncationPredictionUsesCanonicalD418FramesNotHostFrames() {
    // a short host-rate recording/import must not be flagged TRUNC
    // merely because host PCM frames exceed the canonical 8 kHz persisted frame count.
    assert(!digiWouldTruncateToUserSampleBank(22050u, 44100u)); // 0.5 s -> ~4000 D418 bytes
    assert(!digiWouldTruncateToUserSampleBank(24000u, 48000u)); // 0.5 s -> ~4000 D418 bytes
    assert(!digiWouldTruncateToUserSampleBank(48000u, 44100u)); // ~1.09 s now fits in the long bank
    assert(!digiWouldTruncateToUserSampleBank(50000u, 48000u)); // ~1.04 s now fits in the long bank
    assert(digiWouldTruncateToUserSampleBank(400000u, 48000u)); // >7.5 s -> bank-limit truncation

    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    float shortSrc[22050]{};
    for (std::uint32_t i = 0; i < 22050u; ++i) shortSrc[i] = (i & 1u) ? 0.25f : -0.25f;
    assert(digiLoadUserSampleFromFloatMono(bank, 0u, shortSrc, 22050u, 44100u, "half", 4u));
    assert(bank.clips[0].frameCount == 4000u);
    assert(!digiWouldTruncateToUserSampleBank(22050u, 44100u));

    std::vector<float> longSrc(400000u);
    for (std::uint32_t i = 0; i < 400000u; ++i) longSrc[i] = (i & 1u) ? 0.25f : -0.25f;
    assert(digiLoadUserSampleFromFloatMono(bank, 1u, longSrc.data(), 400000u, 48000u, "long", 4u));
    assert(bank.clips[1].frameCount == kDigiUserSampleMaxFrames);
    assert(digiWouldTruncateToUserSampleBank(400000u, 48000u));
}

static void identityHelperCentralizesHashAndHandleRefresh() {
    DigiSampleBankBlob bank = makeDefaultDigiSampleBankBlob();
    const float src[16] = { -1.0f, -0.8f, -0.6f, -0.4f, -0.2f, 0.0f, 0.2f, 0.4f,
                            0.6f, 0.8f, 1.0f, 0.8f, 0.6f, 0.4f, 0.2f, 0.0f };
    assert(digiLoadUserSampleFromFloatMono(bank, 3u, src, 16u, kDigiUserSampleCanonicalRateHz, "id", 2u));
    DigiUserSampleClip clip = bank.clips[3];
    const std::uint32_t h0 = clip.sourceHash;
    const std::uint32_t handle0 = clip.handle;

    digiCopyDisplayName(clip.displayName, "renamed", 7u);
    digiRefreshUserClipIdentityForSlot(clip, 3u);
    assert(clip.sourceHash == digiComputeUserClipSourceHash(clip));
    assert(clip.sourceHash != h0);
    assert(clip.handle != 0u);
    assert(clip.handle != handle0);

    const std::uint32_t h1 = clip.sourceHash;
    digiRefreshUserClipIdentityForSlot(clip, 4u);
    assert(clip.sourceHash == h1);          // sourceHash is content/name/rate only
    assert(clip.handle != handle0);         // handle is slot-qualified identity
}

int main() {
    defaultBankIsWellFormed();
    loadFloatMonoCreatesStableClip();
    longSampleResamplesIntoPinnedCapacity();
    importDecimatesToC64D418ByteStreamWithoutInterpolation();
    loadAndSanitizePreserveOnlyC64D418Codes();
    sanitizeForcesRestoredClipsToCanonicalD418Rate();
    unsanitizedLegacyRateCannotReachRealtimeLookup();
    unsanitizedCanonicalRateButNonD418PayloadCannotReachRealtimeLookup();
    sanitizeClearsMalformedClip();
    sanitizeClearsInactiveClipPayloadCompletely();
    sanitizeClearsActiveClipHiddenTailAndDisplayStorage();
    repairClearsAllStaleOrZeroUserHandles();
    longRecordSchemaV1LimitIsUnderEightSecondsAndPinned();
    truncationPredictionUsesCanonicalD418FramesNotHostFrames();
    identityHelperCentralizesHashAndHandleRefresh();
    return 0;
}
