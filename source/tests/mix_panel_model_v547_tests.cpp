// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// mix_panel_model_v547_tests.cpp
//
// Pins:
// * v547 MIX tab data model + FX chain architecture (TAB_ARCHITECTURE.md §5)
// * Layout pins: MixChannel=72, MixSendBus=32, MixMaster=32, MixPanelModel<1.5KB
// * Defaults: 16 channels, 2 send buses, audit-correct master limiter on
// * Validator: well-formed defaults; invalid input rejected
// * Helpers: channelVolumeDb, channelPanLinear, channelIsAudible (solo law)
// * FX-slot configuration via setMixFxSlot
//
// A. Layout invariants
// B. Default model is well-formed
// C. FX slot type-name domain
// D. setMixFxSlot applies type-specific defaults
// E. Channel solo law: solo'd channel cuts all unsolo'd
// F. Channel audible/mute interaction
// G. Volume + pan helpers stay in sane ranges
// H. Schema version matches header constant

#include "arpsid/gui/mix_panel_model.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

} // namespace

int main() {
    using namespace ArpSID;
    using namespace ArpSID::GUI;

    // ── A. Layout invariants ──────────────────────────────────────────────
    {
        require(sizeof(MixFxSlot) == 12,                "MixFxSlot 12 bytes");
        require(sizeof(MixChannel) == 72,               "MixChannel 72 bytes");
        require(sizeof(MixSendBus) == 32,               "MixSendBus 32 bytes");
        require(sizeof(MixMaster) == 32,                "MixMaster 32 bytes");
        require(sizeof(MixPanelModel) == 16 + 1152 + 64 + 32,
                "MixPanelModel total 1264 bytes");
        require(sizeof(MixPanelModel) < 1536,
                "MixPanelModel under 1.5 KB envelope");
        require(kMixChannelCount == 16,                  "16 channels");
        require(kMixSendBusCount == 2,                   "2 send buses");
        require(kMixFxSlotsPerChannel == 5,              "5 FX slots per channel");
        require(kMixFxParamsPerSlot == 8,                "8 params per FX slot");
    }

    // ── B. Default model is well-formed ───────────────────────────────────
    {
        const auto m = makeDefaultMixModel();
        require(mixModelIsWellFormed(m),
                "default model passes mixModelIsWellFormed");
        require(m.schemaVersion == kMixSchemaVersion,
                "schemaVersion matches header constant");
        require(m.selectedChannel == 0,
                "default selectedChannel = 0");
        require(m.master.limiterEnabled == 1,
                "master limiter ON by default (audit-correct safety)");
        require(m.master.masterVolume == 255,
                "master at unity by default");
        for (std::uint8_t i = 0; i < kMixChannelCount; ++i) {
            require(m.channels[i].enabled == 1,
                    "every channel enabled by default");
            require(m.channels[i].pan == 128,
                    "every channel centered by default");
            require(m.channels[i].volume == 200,
                    "every channel volume ~ -1.2 dB by default");
        }
        for (std::uint8_t i = 0; i < kMixSendBusCount; ++i) {
            require(m.sendBuses[i].enabled == 0,
                    "send buses default OFF (opt-in for production)");
        }
    }

    // ── C. FX type-name domain ────────────────────────────────────────────
    {
        const char* names[] = {
            mixFxTypeName(MixFxType::None),
            mixFxTypeName(MixFxType::Eq3Band),
            mixFxTypeName(MixFxType::Transient),
            mixFxTypeName(MixFxType::Compressor),
            mixFxTypeName(MixFxType::Saturator),
            mixFxTypeName(MixFxType::Bitcrusher),
        };
        for (const char* n : names) {
            require(n != nullptr && n[0] != 0, "FX type name non-empty");
        }
        // None must differ from real FX names.
        require(std::string(mixFxTypeName(MixFxType::None)) != std::string(mixFxTypeName(MixFxType::Eq3Band)),
                "None and Eq3Band names differ");
    }

    // ── D. setMixFxSlot applies type-specific defaults ────────────────────
    {
        MixFxSlot slot{};
        setMixFxSlot(slot, MixFxType::Eq3Band);
        require(slot.type == MixFxType::Eq3Band, "EQ slot type set");
        require(slot.bypass == 0,                "EQ slot starts engaged");
        require(slot.params[0] == 128,           "EQ low shelf gain at unity");
        require(slot.params[2] == 128,           "EQ mid gain at unity");

        setMixFxSlot(slot, MixFxType::Compressor);
        require(slot.type == MixFxType::Compressor, "switched to compressor");
        require(slot.params[0] == 200,           "comp threshold -6 dB");
        require(slot.params[1] == 100,           "comp ratio 4:1");

        setMixFxSlot(slot, MixFxType::Saturator);
        require(slot.type == MixFxType::Saturator, "switched to saturator");
        require(slot.params[0] == 64,            "saturator drive moderate");

        setMixFxSlot(slot, MixFxType::Bitcrusher);
        require(slot.params[0] == 255,           "bitcrusher bits transparent");
        require(slot.params[1] == 255,           "bitcrusher rate transparent");

        setMixFxSlot(slot, MixFxType::None);
        require(slot.type == MixFxType::None,    "switched to None");
        require(slot.params[0] == 0,             "None params cleared");
    }

    // ── E. Channel solo law: solo'd channel cuts all unsolo'd ─────────────
    {
        auto m = makeDefaultMixModel();
        m.channels[3].solo = 1u; // solo channel 3
        const bool anySoloed = true;

        require( channelIsAudible(m.channels[3], anySoloed),
                "solo'd channel is audible");
        require(!channelIsAudible(m.channels[0], anySoloed),
                "unsolo'd channel 0 is silenced when any soloed");
        require(!channelIsAudible(m.channels[7], anySoloed),
                "unsolo'd channel 7 is silenced when any soloed");
    }

    // ── F. Channel audible/mute interaction ───────────────────────────────
    {
        auto m = makeDefaultMixModel();
        // No solo, all default-audible.
        for (std::uint8_t i = 0; i < kMixChannelCount; ++i) {
            require(channelIsAudible(m.channels[i], /*soloed=*/false),
                    "default channels are audible when nothing soloed");
        }
        // Mute channel 5.
        m.channels[5].mute = 1u;
        require(!channelIsAudible(m.channels[5], /*soloed=*/false),
                "muted channel is silent");
        // Disable channel 9.
        m.channels[9].enabled = 0u;
        require(!channelIsAudible(m.channels[9], /*soloed=*/false),
                "disabled channel is silent");
    }

    // ── G. Volume + pan helpers ───────────────────────────────────────────
    {
        MixChannel c{};
        c.volume = 0u;
        require(channelVolumeDb(c) < -100.0f, "volume=0 → effectively -inf");
        c.volume = 200u;
        const float dbAt200 = channelVolumeDb(c);
        require(dbAt200 > -3.0f && dbAt200 < 3.0f,
                "volume=200 → near 0 dB (audit-doc says -1.2 dB)");
        c.pan = 128u;
        require(std::abs(channelPanLinear(c)) < 0.01f,
                "pan=128 → center");
        c.pan = 0u;
        require(channelPanLinear(c) < -0.99f,
                "pan=0 → full L");
        c.pan = 255u;
        require(channelPanLinear(c) > 0.99f,
                "pan=255 → full R");
    }

    // ── H. mixFxParamNorm/Byte round-trip ─────────────────────────────────
    {
        for (int v = 0; v < 256; v += 17) {
            const std::uint8_t b = static_cast<std::uint8_t>(v);
            const float norm = mixFxParamNorm(b);
            const std::uint8_t round = mixFxParamByte(norm);
            require(round == b, "byte → norm → byte round-trip");
        }
        require(mixFxParamByte(-0.5f) == 0,    "negative norm clamps to 0");
        require(mixFxParamByte(1.5f) == 255,   "over-1 norm clamps to 255");
    }

    // ── I. Validator rejects malformed input ──────────────────────────────
    {
        MixPanelModel m = makeDefaultMixModel();
        require(mixModelIsWellFormed(m), "default well-formed");

        m.schemaVersion = 99;
        require(!mixModelIsWellFormed(m), "schema drift rejected");

        m = makeDefaultMixModel();
        m.selectedChannel = 50; // out of range
        require(!mixModelIsWellFormed(m), "out-of-range channel rejected");

        m = makeDefaultMixModel();
        m.channels[0].enabled = 5; // out of bool domain
        require(!mixModelIsWellFormed(m), "out-of-bool enabled rejected");
    }

    // ── J. FX-type validity is enum-count-derived, not a magic number ─────
    {
        require(kMixFxTypeCount == static_cast<std::uint8_t>(MixFxType::Bitcrusher) + 1u,
                "kMixFxTypeCount tracks the MixFxType enum");
        require(mixFxTypeIsValid(0) && mixFxTypeIsValid(kMixFxTypeCount - 1u),
                "defined FX types are valid");
        require(!mixFxTypeIsValid(kMixFxTypeCount),
                "one-past-last FX type is invalid");

        MixPanelModel m = makeDefaultMixModel();
        m.channels[0].fxSlots[0].type = static_cast<MixFxType>(kMixFxTypeCount); // corrupt/forward value
        require(!mixModelIsWellFormed(m), "out-of-range FX type rejected");
        sanitizeMixModel(m);
        require(mixModelIsWellFormed(m), "sanitize resets a model with an out-of-range FX type");

        m = makeDefaultMixModel();
        m.channels[0].fxSlots[0].type = MixFxType::Bitcrusher; // highest valid
        require(mixModelIsWellFormed(m), "highest valid FX type stays well-formed");
        sanitizeMixModel(m);
        require(m.channels[0].fxSlots[0].type == MixFxType::Bitcrusher,
                "sanitize preserves a valid FX type");
    }

    std::cout << "mix_panel_model_v547_tests: MIX tab data model + FX chain architecture pinned\n";
    return 0;
}
