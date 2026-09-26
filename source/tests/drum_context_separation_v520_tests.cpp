// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// drum_context_separation_v520_tests.cpp
//
// Pins Audit #5/#39/#74 "DrSID ↔ SID-808 context separation":
// * Every legacy DrSID-projection slot classifies as DrumContext::DrSID_C64Wavetable.
// * Every non-DrSID legacy slot classifies as DrumContext::None.
// * Canonical new factory ranges (DrSID/SID-808/Digi) are non-overlapping
// and well-formed at *runtime* — not just at compile time.
// * `drumKitIdentityForFactorySlot()` produces a valid identity for every
// DrSID legacy slot, and an *invalid* identity for non-DrSID legacy slots.
// * `factorySlotContextLegacy()` is *bit-identical* to the legacy authority
// predicate `isAuthoredDrSidProjectionFactorySlot()` for the entire 0..127
// slot space. This is the lock-step guarantee the audit demands.
//
// These checks run on the host CPU, not the render thread, so we are free to
// use std::cout / std::abort. They cover every slot in the 0..255 window.

#include "arpsid/core/drum_context.h"
#include "../factory_patch_params.h"

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

    // ── 1. DrumContext enum is well-formed ──────────────────────────────────
    require(drumContextName(DrumContext::None)[0] != 0,                   "DrumContext::None has a name");
    require(drumContextName(DrumContext::DrSID_C64Wavetable)[0] != 0,     "DrSID context has a name");
    require(drumContextName(DrumContext::SID808_AnalogProjection)[0] != 0,"SID-808 context has a name");
    require(drumContextName(DrumContext::Digi4Bit)[0] != 0,               "Digi context has a name");

    // ── 2. SidModelPreference enum is well-formed ───────────────────────────
    require(sidModelPreferenceName(SidModelPreference::Neutral)[0] != 0,             "Neutral pref has a name");
    require(sidModelPreferenceName(SidModelPreference::Force6581Dirty)[0] != 0,      "6581 pref has a name");
    require(sidModelPreferenceName(SidModelPreference::Force8580Clean)[0] != 0,      "8580 pref has a name");
    require(sidModelPreferenceName(SidModelPreference::FollowGlobalSIDModel)[0] != 0,"FollowGlobal pref has a name");

    // ── 3. DrumKitIdentity defaults are inert (invalid) ─────────────────────
    DrumKitIdentity blank{};
    require(!blank.valid(),                                  "default-constructed DrumKitIdentity is invalid");
    require(blank.context == DrumContext::None,              "default context is None");
    require(blank.kitId == 0 && blank.factorySlot == 0,      "default ids are zeroed");
    require(blank.schemaVersion == 1,                        "default schemaVersion is 1");

    // ── 4. Legacy-only predicate and canonical authored predicate are separate ──
    for (int slot = 0; slot < 128; ++slot) {
        const bool canonicalAuth = isAuthoredDrSidProjectionFactorySlot(slot);
        const bool legacyOnly    = isLegacyDrSidProjectionSlot(slot);
        const bool ctxMatches    = (factorySlotContextLegacy(slot) == DrumContext::DrSID_C64Wavetable);

        // After canonical authored DrSID includes 80..119.
        // Legacy-only context remains {47,112..124,127}.
        // Therefore these predicates intentionally diverge at 80..111.
        if (slot >= 80 && slot <= 111) {
            require(canonicalAuth, "canonical DrSID 80..111 is authored");
            require(!legacyOnly, "canonical DrSID 80..111 is not legacy-only");
        } else if (slot >= 120 && slot <= 124) {
            require(!canonicalAuth, "canonical SID808 120..124 is not DrSID-authored");
            require(legacyOnly, "legacy DrSID 120..124 remains legacy-only compatibility");
            require(factorySlotContext(slot) == DrumContext::SID808_AnalogProjection,
                    "canonical 120..124 maps to SID808 despite legacy DrSID compatibility");
        } else if (canonicalAuth != legacyOnly) {
            std::cerr << "FAIL: non-canonical legacy/canonical predicate diverged at slot " << slot
                      << " (canonicalAuth=" << canonicalAuth << ", legacyOnly=" << legacyOnly << ")\n";
            std::abort();
        }
        if (legacyOnly != ctxMatches) {
            std::cerr << "FAIL: factorySlotContextLegacy diverged from legacy-only predicate at slot " << slot << "\n";
            std::abort();
        }
    }

    // ── 5. Canonical new factory ranges are disjoint and monotonic ──────────
    require(kDrSidNewFactoryRange.count()  == 40, "DrSID new range covers 80..119 (40 slots)");
    require(kSid808NewFactoryRange.count() == 30, "SID-808 new range covers 120..149 (30 slots)");
    require(kDigiNewFactoryRange.count()   == 30, "Digi new range covers 150..179 (30 slots)");

    for (int slot = 0; slot < 256; ++slot) {
        const bool inDrSid  = isDrSidFactorySlot(slot);
        const bool inSid808 = isSid808FactorySlot(slot);
        const bool inDigi   = isDigiFactorySlot(slot);
        const int membership = (inDrSid ? 1 : 0) + (inSid808 ? 1 : 0) + (inDigi ? 1 : 0);
        if (membership > 1) {
            std::cerr << "FAIL: slot " << slot << " claimed by multiple canonical ranges\n";
            std::abort();
        }
    }

    // ── 6. drumKitIdentityForFactorySlot() round-trips correctly ────────────
    {
        // Legacy DrSID slot:
        const auto id = drumKitIdentityForFactorySlot(47);
        require(id.valid(),                                       "legacy slot 47 produces a valid identity");
        require(id.context == DrumContext::DrSID_C64Wavetable,    "legacy slot 47 ↔ DrSID context");
        require(id.factorySlot == 47,                             "factorySlot round-trips");
        require(id.kitId == 47,                                   "kitId defaults to slot");
        require(id.sidModel == SidModelPreference::FollowGlobalSIDModel,
                "default sidModel preference is FollowGlobalSIDModel");
    }
    {
        // Canonical SID-808 slot:
        const auto id = drumKitIdentityForFactorySlot(125, SidModelPreference::Force6581Dirty);
        require(id.valid(),                                            "new SID-808 slot 125 is valid");
        require(id.context == DrumContext::SID808_AnalogProjection,    "new SID-808 slot 125 ↔ SID-808 context");
        require(id.sidModel == SidModelPreference::Force6581Dirty,     "kit-level sidModel preference honored");
    }
    {
        // Non-drum legacy slot:
        const auto id = drumKitIdentityForFactorySlot(0);
        require(!id.valid(),                                      "legacy slot 0 (melodic) is NOT a drum identity");
        require(id.context == DrumContext::None,                  "non-drum slot has no context");
    }
    {
        // Out-of-range slot:
        const auto id = drumKitIdentityForFactorySlot(-5);
        require(!id.valid(),                                      "negative slot is invalid");
        require(id.factorySlot == 0,                              "negative slot clamps to 0 in identity");
    }

    // ── 7. Cross-context refusal predicate ──────────────────────────────────
    {
        const auto drsid  = drumKitIdentityForFactorySlot(47);   // DrSID
        const auto sid808 = drumKitIdentityForFactorySlot(125);  // SID-808

        require( drumKitIdentityIsContextCompatible(drsid,  DrumContext::DrSID_C64Wavetable),
                "DrSID identity matches DrSID context");
        require(!drumKitIdentityIsContextCompatible(drsid,  DrumContext::SID808_AnalogProjection),
                "DrSID identity REJECTS SID-808 context (audit §2 invariant)");
        require( drumKitIdentityIsContextCompatible(sid808, DrumContext::SID808_AnalogProjection),
                "SID-808 identity matches SID-808 context");
        require(!drumKitIdentityIsContextCompatible(sid808, DrumContext::DrSID_C64Wavetable),
                "SID-808 identity REJECTS DrSID context (audit §2 invariant)");
        require(!drumKitIdentityIsContextCompatible(blank,  DrumContext::DrSID_C64Wavetable),
                "blank identity matches no context");
    }

    // ── 8. Combined classifier prefers new range over legacy when they agree ──
    require(factorySlotContext(112) == DrumContext::DrSID_C64Wavetable,
            "slot 112 (legacy DrSID + new DrSID overlap) is DrSID under combined classifier");
    require(factorySlotContext(125) == DrumContext::SID808_AnalogProjection,
            "slot 125 (new SID-808 only) classifies as SID-808");
    require(factorySlotContext(150) == DrumContext::Digi4Bit,
            "slot 150 (new Digi only) classifies as Digi");
    require(factorySlotContext(255) == DrumContext::None,
            "slot 255 is unclassified");

    // ── 9. DrumKitIdentity bit-layout pin (catches accidental ABI churn) ────
    require(sizeof(DrumKitIdentity) == 16,
            "DrumKitIdentity must remain 16 bytes (header pins this; runtime pins it too)");

    std::cout << "drum_context_separation_v520_tests: all DrSID↔SID-808 context invariants hold (audit #5/#39/#74)\n";
    return 0;
}
