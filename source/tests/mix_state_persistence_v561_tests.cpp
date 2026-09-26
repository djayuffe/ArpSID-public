// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// mix_state_persistence_v561_tests.cpp — MIX tab state persistence contract tests (v561).
//
// Tests cover:
// I. Static layout pin (sizeof MixPanelModel == 1264, trivially copyable, schema constant)
// II. Default model is well-formed + sub-field spot-checks
// III. sanitizeMixModel — already well-formed model is unchanged
// IV. sanitizeMixModel — schema mismatch resets to defaults
// V. sanitizeMixModel — bad per-channel field resets the whole model
// VI. Memcpy round-trip — mutate → bytes → restore → sanitize (no-op) → values preserved

#include "arpsid/gui/mix_panel_model.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <array>

using namespace ArpSID::GUI;

// ─── I. Static layout pin ─────────────────────────────────────────────────────
static_assert(kMixSchemaVersion == 1u,           "schema version must be 1");
static_assert(kMixChannelCount  == 16u,          "channel count must be 16");
static_assert(kMixSendBusCount  == 2u,           "send bus count must be 2");
static_assert(sizeof(MixFxSlot)     == 12u,      "MixFxSlot pinned at 12 bytes");
static_assert(sizeof(MixChannel)    == 72u,      "MixChannel pinned at 72 bytes");
static_assert(sizeof(MixSendBus)    == 32u,      "MixSendBus pinned at 32 bytes");
static_assert(sizeof(MixMaster)     == 32u,      "MixMaster pinned at 32 bytes");
static_assert(sizeof(MixPanelModel) == 1264u,    "MixPanelModel pinned at 1264 bytes");
static_assert(std::is_trivially_copyable<MixPanelModel>::value,
              "MixPanelModel trivially copyable");
static_assert(mixModelIsWellFormed(makeDefaultMixModel()),
              "default MixPanelModel must be well-formed at compile time");

// ─── II. Default model ────────────────────────────────────────────────────────
static void testDefaultModel() {
    const MixPanelModel m = makeDefaultMixModel();
    assert(m.schemaVersion   == kMixSchemaVersion);
    assert(m.selectedChannel == 0u);
    assert(mixModelIsWellFormed(m));

    // Every channel should be enabled, not solo'd, not muted.
    for (uint8_t ch = 0; ch < kMixChannelCount; ++ch) {
        assert(m.channels[ch].enabled == 1u);
        assert(m.channels[ch].solo    == 0u);
        assert(m.channels[ch].mute    == 0u);
        assert(m.channels[ch].volume  == 200u);
        assert(m.channels[ch].pan     == 128u);
        // All FX slots default to None.
        for (uint8_t s = 0; s < kMixFxSlotsPerChannel; ++s) {
            assert(m.channels[ch].fxSlots[s].type   == MixFxType::None);
            assert(m.channels[ch].fxSlots[s].bypass == 0u);
        }
    }
    // Master defaults.
    assert(m.master.masterVolume   == 255u);
    assert(m.master.limiterEnabled == 1u);
    assert(m.master.stereoWidth    == 128u);
    assert(m.master.dimMonitor     == 0u);
    // Send buses disabled by default.
    for (uint8_t b = 0; b < kMixSendBusCount; ++b) {
        assert(m.sendBuses[b].enabled == 0u);
    }
}

// ─── III. sanitizeMixModel — well-formed model is unchanged ───────────────────
static void testSanitizeUnchanged() {
    MixPanelModel m = makeDefaultMixModel();
    // Mutate a well-formed field.
    m.channels[3].volume  = 180u;
    m.channels[3].pan     = 64u;
    m.master.masterVolume = 210u;
    const MixPanelModel before = m;
    sanitizeMixModel(m);
    // Should be bit-identical.
    assert(std::memcmp(&m, &before, sizeof(MixPanelModel)) == 0);
}

// ─── IV. sanitizeMixModel — schema mismatch resets to defaults ────────────────
static void testSanitizeSchemaMismatch() {
    MixPanelModel m = makeDefaultMixModel();
    m.channels[0].volume = 42u;      // non-default, should be wiped
    m.schemaVersion = 0u;
    assert(!mixModelIsWellFormed(m));
    sanitizeMixModel(m);
    assert(mixModelIsWellFormed(m));
    assert(m.schemaVersion == kMixSchemaVersion);
    // All channels back to defaults.
    const MixPanelModel def = makeDefaultMixModel();
    assert(std::memcmp(&m, &def, sizeof(MixPanelModel)) == 0);
}

// ─── V. sanitizeMixModel — bad per-channel field resets whole model ────────────
static void testSanitizeBadChannelField() {
    // enabled > 1.
    {
        MixPanelModel m = makeDefaultMixModel();
        m.channels[7].enabled = 2u;
        assert(!mixModelIsWellFormed(m));
        sanitizeMixModel(m);
        assert(mixModelIsWellFormed(m));
        assert(m.channels[7].enabled == 1u);
    }
    // FX type out of range (> 5).
    {
        MixPanelModel m = makeDefaultMixModel();
        m.channels[2].fxSlots[1].type = static_cast<MixFxType>(255u);
        assert(!mixModelIsWellFormed(m));
        sanitizeMixModel(m);
        assert(mixModelIsWellFormed(m));
    }
    // selectedChannel out of range.
    {
        MixPanelModel m = makeDefaultMixModel();
        m.selectedChannel = kMixChannelCount;    // one past last
        assert(!mixModelIsWellFormed(m));
        sanitizeMixModel(m);
        assert(m.selectedChannel == 0u);
        assert(mixModelIsWellFormed(m));
    }
}

// ─── VI. Memcpy round-trip ────────────────────────────────────────────────────
static void testMemcpyRoundTrip() {
    MixPanelModel src = makeDefaultMixModel();
    src.selectedChannel        = 5u;
    src.channels[5].volume     = 150u;
    src.channels[5].pan        = 200u;
    src.channels[5].solo       = 1u;
    src.channels[5].mute       = 0u;
    src.channels[9].fxSlots[0].type = MixFxType::Compressor;
    src.channels[9].fxSlots[0].params[0] = 180u;
    src.master.masterVolume    = 230u;
    src.master.dimMonitor      = 1u;
    src.sendBuses[0].enabled   = 1u;
    src.sendBuses[0].returnLevel = 200u;
    assert(mixModelIsWellFormed(src));

    // Serialize to raw bytes.
    std::array<uint8_t, sizeof(MixPanelModel)> buf{};
    std::memcpy(buf.data(), &src, sizeof(MixPanelModel));

    // Deserialize.
    MixPanelModel dst{};
    std::memcpy(&dst, buf.data(), sizeof(MixPanelModel));
    sanitizeMixModel(dst);  // must be no-op on clean data
    assert(mixModelIsWellFormed(dst));

    // Bit-identical.
    assert(std::memcmp(&src, &dst, sizeof(MixPanelModel)) == 0);

    // Spot-check fields.
    assert(dst.selectedChannel              == 5u);
    assert(dst.channels[5].volume           == 150u);
    assert(dst.channels[5].pan              == 200u);
    assert(dst.channels[5].solo             == 1u);
    assert(dst.channels[9].fxSlots[0].type  == MixFxType::Compressor);
    assert(dst.channels[9].fxSlots[0].params[0] == 180u);
    assert(dst.master.masterVolume          == 230u);
    assert(dst.master.dimMonitor            == 1u);
    assert(dst.sendBuses[0].enabled         == 1u);
    assert(dst.sendBuses[0].returnLevel     == 200u);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testDefaultModel();
    testSanitizeUnchanged();
    testSanitizeSchemaMismatch();
    testSanitizeBadChannelField();
    testMemcpyRoundTrip();
    return 0;
}
