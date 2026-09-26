// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// audit_followups_v526_tests.cpp
//
// Pins the four follow-up fixes that closed the "ikke gjort i denne skiven"
// items from slices 1, 4, and 5:
//
// 1. DrumContext gate in factory-loader: refuses to apply DrSID defaults
// when the slot's combined-classifier context is SID-808 or Digi.
// (Slice 1 follow-up; pins audit #5/#39/#74 wire-up.)
// 2. DrSidKitCompiler: validates programs, computes register-trace
// fingerprints, builds compiled-kit snapshots, and surfaces
// `findProgramForClass()` lookups. (Slice 4 follow-up.)
// 3. CRC32 is stable across builds (kit cache binary-compat invariant).
// 4. Fingerprint of the three exemplar programs is non-trivial and
// mutually distinct — catches accidental drift between exemplars.
//
// The AUv2 wrapper follow-ups (#4 raw bridged block pointer lifetime and
// #5 scratch-capacity pre-render guard) are exercised at the code level
// but cannot be unit-tested from the core test binary because they live
// inside the AUv2 component (Cocoa-linked). Their compile-time correctness
// is verified by the AUv2 build target passing; their runtime correctness
// is observed via the `renderDrainTimeoutCount` and
// `scratchUnderCapacityCount` impl-side counters in Logic.

#include "arpsid/core/drum_context.h"
#include "arpsid/core/drsid_kit_compiler.h"
#include "../factory_patch_params.h"

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
    using namespace ArpSID::Drsid;

    // ── A. DrumContext gate in factory-loader: SID-808 slot must NOT
    // inherit DrSID defaults even with a DrSID-flagged PatchDefinition ──
    {
        std::array<float, static_cast<size_t>(kNumParams)> params{};
        // Pre-populate every parameter to a sentinel that DrSID's defaults
        // would definitely overwrite (DrSID writes 0..1 values; the sentinel
        // -1 cannot survive applyFactoryDrSidDefaults).
        for (auto& p : params) p = -1.0f;
        PatchDefinition def{};
        def.staticState.drSidMode = true;
        def.usage.role = PatchRole::Drum;

        // Slot 125 is in the canonical SID-808 range under the combined
        // classifier — the new gate must refuse DrSID defaults here even
        // though the legacy predicate would happily apply them.
        require(factorySlotContext(125) == DrumContext::SID808_AnalogProjection,
                "slot 125 is SID-808 under combined classifier");
        applyFactoryDrSidDefaults(def, 125, params);
        bool anyOverwritten = false;
        for (const auto& p : params) {
            if (p != -1.0f) { anyOverwritten = true; break; }
        }
        require(!anyOverwritten,
                "applyFactoryDrSidDefaults refused to touch SID-808 slot 125 (audit #5/#39/#74 gate held)");
    }

    // ── B. DrumContext gate ALLOWS DrSID defaults on canonical DrSID slot ──
    {
        std::array<float, static_cast<size_t>(kNumParams)> params{};
        for (auto& p : params) p = -1.0f;
        PatchDefinition def{};
        def.staticState.drSidMode = true;
        def.usage.role = PatchRole::Drum;

        require(factorySlotContext(47) == DrumContext::DrSID_C64Wavetable,
                "slot 47 is DrSID under combined classifier");
        applyFactoryDrSidDefaults(def, 47, params);
        bool anyOverwritten = false;
        for (const auto& p : params) {
            if (p != -1.0f) { anyOverwritten = true; break; }
        }
        require(anyOverwritten,
                "applyFactoryDrSidDefaults applied to DrSID slot 47 (gate did not over-refuse)");
    }

    // ── C. DrumContext gate refuses Digi-range slot too ─────────────────────
    {
        std::array<float, static_cast<size_t>(kNumParams)> params{};
        for (auto& p : params) p = -2.0f;
        PatchDefinition def{};
        def.staticState.drSidMode = true;
        def.usage.role = PatchRole::Drum;

        require(factorySlotContext(150) == DrumContext::Digi4Bit,
                "slot 150 is Digi under combined classifier");
        applyFactoryDrSidDefaults(def, 150, params);
        bool anyOverwritten = false;
        for (const auto& p : params) {
            if (p != -2.0f) { anyOverwritten = true; break; }
        }
        require(!anyOverwritten,
                "applyFactoryDrSidDefaults refused to touch Digi slot 150");
    }

    // ── D. CRC32 sanity — known fixed vector ─────────────────────────────────
    {
        // Standard test: CRC32 of "123456789" is 0xCBF43926.
        const std::uint8_t input[] = "123456789";
        const std::uint32_t crc = crc32Update(0u, input, 9);
        require(crc == 0xCBF43926u,
                "CRC32 of \"123456789\" matches the IEEE 802.3 test vector");
    }

    // ── E. DrSidKitCompiler: compile a single exemplar program ──────────────
    {
        const auto kick = makeExemplarKick();
        DrSidInstrumentProgram compiled{};
        require(compileProgram(kick, compiled),
                "compileProgram succeeds for well-formed Kick");
        require(compiled.fingerprint.expectedStepCount == kick.stepCount,
                "compiled fingerprint records step count");
        require(compiled.fingerprint.registerTraceHash != 0u,
                "compiled fingerprint has a non-trivial CRC");
        require(compiled.fingerprint.firstStepWord != 0u,
                "first-step spot-check word is populated");
    }

    // ── F. compileProgram REJECTS malformed input ───────────────────────────
    {
        DrSidInstrumentProgram bad = makeExemplarKick();
        bad.stepCount = 0;          // forces programIsWellFormed → false
        DrSidInstrumentProgram out = makeExemplarKick();  // initialize so we can verify untouched
        const auto sentinelFp = out.fingerprint;
        require(!compileProgram(bad, out),
                "compileProgram refuses malformed input");
        require(out.fingerprint.registerTraceHash == sentinelFp.registerTraceHash,
                "compileProgram leaves `out` unchanged on rejection");
    }

    // ── G. Fingerprints of the three exemplars are mutually distinct ────────
    {
        DrSidInstrumentProgram k{}, h{}, c{};
        require(compileProgram(makeExemplarKick(),      k), "kick compiles");
        require(compileProgram(makeExemplarClosedHat(), h), "hat compiles");
        require(compileProgram(makeExemplarClap(),      c), "clap compiles");
        require(k.fingerprint.registerTraceHash != h.fingerprint.registerTraceHash,
                "Kick vs ClosedHat fingerprint distinct");
        require(k.fingerprint.registerTraceHash != c.fingerprint.registerTraceHash,
                "Kick vs Clap fingerprint distinct");
        require(h.fingerprint.registerTraceHash != c.fingerprint.registerTraceHash,
                "ClosedHat vs Clap fingerprint distinct");
        require(k.fingerprint.firstStepWord != h.fingerprint.firstStepWord,
                "first-step spot check is also distinct kick↔hat");
        require(k.fingerprint.firstStepWord != c.fingerprint.firstStepWord,
                "first-step spot check is also distinct kick↔clap");
    }

    // ── H. compileKit: 3-program kit produces a coherent snapshot ───────────
    {
        const DrSidInstrumentProgram programs[3] = {
            makeExemplarKick(),
            makeExemplarClosedHat(),
            makeExemplarClap(),
        };
        CompiledDrSidKit kit{};
        require(compileKit(programs, 3, /*kitId=*/42u, kit),
                "compileKit succeeds on 3 well-formed programs");
        require(kit.instrumentCount == 3,                          "kit holds 3 instruments");
        require(kit.kitId == 42u,                                   "kit ID round-trips");
        require(kit.generation == 1u,                               "kit generation starts at 1");
        require(kit.schemaVersion == kProgramSchemaVersion,         "kit carries the build's schema version");

        // findProgramForClass discovers each drum by its class tag.
        require(findProgramForClass(kit, SidGMDrumClass::Kick)      != nullptr,
                "findProgramForClass locates Kick");
        require(findProgramForClass(kit, SidGMDrumClass::ClosedHat) != nullptr,
                "findProgramForClass locates ClosedHat");
        require(findProgramForClass(kit, SidGMDrumClass::Clap)      != nullptr,
                "findProgramForClass locates Clap");
        require(findProgramForClass(kit, SidGMDrumClass::Snare)     == nullptr,
                "findProgramForClass returns null for unmapped class");
    }

    // ── I. compileKit rejects kit with even one malformed program ───────────
    {
        DrSidInstrumentProgram programs[3] = {
            makeExemplarKick(),
            makeExemplarClosedHat(),
            makeExemplarClap(),
        };
        // Sabotage the middle program.
        programs[1].stepCount = 0;

        CompiledDrSidKit kit{};
        kit.instrumentCount = 7u;
        require(!compileKit(programs, 3, /*kitId=*/9u, kit),
                "compileKit refuses on any malformed program");
        require(kit.instrumentCount == 7u,
                "compileKit failure is atomic and leaves the output snapshot unchanged");
    }

    // ── J. compileKit rejects oversized kit and null pointer ────────────────
    {
        DrSidInstrumentProgram dummy = makeExemplarKick();
        CompiledDrSidKit kit{};
        require(!compileKit(nullptr, 1, 0u, kit),
                "compileKit refuses null program pointer");
        require(!compileKit(&dummy, 0u, 0u, kit),
                "compileKit refuses zero count");
        require(!compileKit(&dummy, kMaxInstrumentsPerKit + 1, 0u, kit),
                "compileKit refuses over-capacity count");
    }

    std::cout << "audit_followups_v526_tests: factory-loader gate + DrSidKitCompiler + fingerprint contract pinned\n";
    return 0;
}
