// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <cstdint>

namespace ArpSID {

// sid_runtime_state_apply_policy.h — canonical host/wrapper state-apply law.
//
// AU/VST wrappers may name the reason for a state application, but they must not
// own the policy table that decides which authority wins. Keeping the mapping in
// core prevents AU/VST split-brain when transport reset, project restore, factory
// preset selection, or wrapper snapshot restore semantics evolve.

enum class SidStateApplyReason : uint8_t {
    ExplicitPresetSelection,
    ProjectRestore,
    HostTransportReset,
    ImportBankPatch,
    WrapperFullStateRestore,
};

enum class SidStateOverlayPolicy : uint8_t {
    FactoryRootIsAudibleAuthority,
    FactoryRootSeedThenLiveAudioOverlay,
    SerializedStateIsAuthority,
    SnapshotIsAudibleAuthority,
};

constexpr SidStateOverlayPolicy sidStateOverlayPolicyForApplyReason(SidStateApplyReason reason) noexcept {
    switch (reason) {
        case SidStateApplyReason::ExplicitPresetSelection:
        case SidStateApplyReason::ImportBankPatch:
            return SidStateOverlayPolicy::FactoryRootIsAudibleAuthority;
        case SidStateApplyReason::HostTransportReset:
            return SidStateOverlayPolicy::FactoryRootSeedThenLiveAudioOverlay;
        case SidStateApplyReason::ProjectRestore:
            return SidStateOverlayPolicy::SerializedStateIsAuthority;
        case SidStateApplyReason::WrapperFullStateRestore:
            return SidStateOverlayPolicy::SnapshotIsAudibleAuthority;
    }
    return SidStateOverlayPolicy::SnapshotIsAudibleAuthority;
}

} // namespace ArpSID
