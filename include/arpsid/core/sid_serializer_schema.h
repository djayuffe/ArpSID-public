// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
#include "sid_variant_profile.h"
#include "sid_measured_posterior.h"
#include "sid_mod_matrix_types.h"
#include "sid_dynamic_state.h"
#include "sid_voice_identity.h"
#include "sid_event_queue.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include <array>
#include <vector>
#include <string>

namespace ArpSID {

static constexpr uint32_t kSidStateRootMagic = 0x41535231u; // ASR1
static constexpr uint32_t kSidStateRootSchemaVersion = 1u;

struct SidSemanticParamEntry {
    uint32_t param_id = 0;
    float value = 0.0f;
};

struct SidSerializedState {
    std::array<uint16_t, 3> envelopeRateCounter{};
    std::array<uint8_t, 3> envelopeExponentialCounter{};
    std::array<bool, 3> envelopeAdsrDelayHold{};
};

struct SidParameterBlock {
    std::vector<float> values; // derived runtime snapshot; not canonical on disk
    std::vector<SidSemanticParamEntry> semantic_entries; // canonical persistent parameter surface
};

struct SidMacroAssignments {
    std::array<float, 8> values{};
};

struct SidArpState {
    bool enabled = false;
    float rate = 0.0f;
    bool hold = false;
    bool latch = false;
    int transpose = 0;
};

struct SidSeqState {
    bool enabled = false;
    float tempo = 0.0f;
    int length = 0;
    float swing = 0.0f;
};

struct SidPatchState {
    SidVariantProfile variant_profile{};
    SidMeasuredPosterior posterior{};
    SidParameterBlock parameters{};
    std::vector<SidModRoute> mod_routes{};
    SidMacroAssignments macros{};
    SidArpState arp{};
    SidSeqState seq{};
    PatchStartPolicy start_policy{};
    SidSerializedState sid_runtime{};
    float forensic_temperature_celsius = 35.0f;
    float forensic_supply_voltage = 5.0f;
    uint8_t forensic_revision = 5u; // v909: HMOS-II 8580 R5 class default
    uint32_t forensic_chip_seed = 0xDEADBEEFu;
};

struct SidDocumentState {
    std::string program_name{};
    std::string current_program_ref{};
    bool editor_metadata_present = false;
    std::string editor_layout_blob{};
};

// SidRuntimeEphemera removed — was defined but never instantiated. Runtime
// voice/event/dynamic state is owned directly by SidRuntimeModel fields.

struct SidStateRootV1 {
    uint32_t magic = kSidStateRootMagic;
    uint32_t schema_version = kSidStateRootSchemaVersion;
    SidPatchState patch{};
    SidDocumentState document{};

    bool valid() const noexcept {
        return magic == kSidStateRootMagic && schema_version == kSidStateRootSchemaVersion;
    }
};

} // namespace ArpSID
