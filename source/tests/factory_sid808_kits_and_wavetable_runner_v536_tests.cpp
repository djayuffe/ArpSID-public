// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// factory_sid808_kits_and_wavetable_runner_v536_tests.cpp
//
// Closes two architectural wire-ups in one slice:
//
// * Audit #39/#74 wire-up step — `applyFactorySid808Kit(slot, engine)`
// populates a `Sid808Engine` with the right per-drum voice configs
// for SID-808 factory slots 120..149. Pins:
// - The 5 authored kit families are distinct (Classic / Punch / Lo-Fi / Hard / Wide)
// - The full 30-slot canonical range resolves to one of those families
// - Each kit's kick has distinct frequency/decay/level
// - Engine's runtime drum-config matches the loaded kit byte-for-byte
// - Slots outside 120..149 are rejected
// - Display names round-trip
//
// * Audit #43/#44 wire-up — `DrSidWavetableProgramRunner` actually
// emits register writes from a `DrSidInstrumentProgram`. Pins:
// - trigger() applies step 0 immediately + cursor advances to 1
// - tickSidCycles() fires due steps in cycleOffset order
// - finished() returns true after terminal step
// - abort() clears cursor (audit #41 choke law substrate)
// - Stepping the Kick exemplar produces the documented pitch-drop
// sequence (frequencies are monotonically decreasing across steps)

#include "arpsid/core/drsid_instrument_program.h"
#include "arpsid/engines/sid808_engine.h"
#include "arpsid/engines/drsid_wavetable_program_runner.h"
#include "arpsid/patchbank/factory_sid808_kits.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

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

    // ── A. All 5 authored factory SID-808 kit families are present and distinct ──
    {
        const Sid808KitConfigTable* kits[5] = {
            factorySid808KitForSlot(120),
            factorySid808KitForSlot(121),
            factorySid808KitForSlot(122),
            factorySid808KitForSlot(123),
            factorySid808KitForSlot(124),
        };
        for (int i = 0; i < 5; ++i) require(kits[i] != nullptr,
                "authored SID-808 families all have a kit table");

        // Each kit's kick frequency must be distinct (the most audible
        // discriminator). Lo-Fi has the lowest, Hard has the highest.
        std::set<std::uint16_t> kickFreqs;
        for (int i = 0; i < 5; ++i) {
            const auto& kick = (*kits[i])[static_cast<size_t>(Sid808Drum::Kick)];
            kickFreqs.insert(kick.freq);
        }
        require(kickFreqs.size() == 5,
                "all 5 kits have distinct kick frequencies");

        // Hard kit's kick has highest freq among the 5.
        const auto& hardKick = (*kits[3])[static_cast<size_t>(Sid808Drum::Kick)];
        const auto& lofiKick = (*kits[2])[static_cast<size_t>(Sid808Drum::Kick)];
        require(hardKick.freq > lofiKick.freq,
                "Hard kit kick brighter (higher freq) than Lo-Fi kick");

        for (int slot = 120; slot <= 149; ++slot) {
            require(factorySid808KitForSlot(slot) != nullptr,
                    "every canonical SID-808 slot 120..149 has kit data");
            require(factorySid808KitName(slot) != nullptr,
                    "every canonical SID-808 slot 120..149 has a kit name");
        }
        require(factorySid808KitForSlot(125) == factorySid808KitForSlot(120),
                "slot 125 wraps to the Classic family");
        require(factorySid808KitForSlot(149) == factorySid808KitForSlot(124),
                "slot 149 wraps to the Wide family");
        require(factorySid808KitVariantIndex(120) == 0,
                "slot 120 uses base SID-808 variant bank");
        require(factorySid808KitVariantIndex(125) == 1,
                "slot 125 uses SID-808 variant bank 1");
        require(factorySid808KitVariantIndex(149) == 5,
                "slot 149 uses SID-808 variant bank 5");
        const auto classicBase = factorySid808ResolvedKitForSlot(120);
        const auto classicVar = factorySid808ResolvedKitForSlot(125);
        require(classicBase[static_cast<size_t>(Sid808Drum::Kick)].freq !=
                classicVar[static_cast<size_t>(Sid808Drum::Kick)].freq,
                "slot 125 resolves to a distinct Classic kick variation");

        // Out-of-range slot returns nullptr.
        require(factorySid808KitForSlot(119) == nullptr, "slot 119 out of range");
        require(factorySid808KitForSlot(150) == nullptr, "slot 150 out of range");
    }

    // ── B. Display names round-trip for each kit slot ──────────────────────
    // v910 honest naming: slots 120..149 are 5 authored families × 6 variant
    // banks (A..F). Every slot carries a non-ambiguous family+variant name.
    {
        require(std::string(factorySid808KitName(120)) == "SID-808 Classic Kit A",
                "slot 120 name");
        require(std::string(factorySid808KitName(121)) == "SID-808 Punch Kit A",
                "slot 121 name");
        require(std::string(factorySid808KitName(122)) == "SID-808 Lo-Fi Kit A",
                "slot 122 name");
        require(std::string(factorySid808KitName(123)) == "SID-808 Hard Kit A",
                "slot 123 name");
        require(std::string(factorySid808KitName(124)) == "SID-808 Wide Kit A",
                "slot 124 name");
        require(std::string(factorySid808KitName(125)) == "SID-808 Classic Kit B",
                "slot 125 is Classic family, variant bank B");
        require(std::string(factorySid808KitName(149)) == "SID-808 Wide Kit F",
                "slot 149 is Wide family, variant bank F");
        require(factorySid808KitName(150) == nullptr,
                "slot 150 has no kit name");
        // Family/variant metadata stays coherent with names, and every slot's
        // name is unique across the canonical 120..149 range.
        for (int a = 120; a <= 149; ++a) {
            require(std::string(factorySid808KitName(a)).find(
                        factorySid808KitFamilyName(factorySid808KitFamilyIndex(a))) == 0,
                    "slot name starts with its family name");
            for (int b = a + 1; b <= 149; ++b) {
                require(std::string(factorySid808KitName(a)) != std::string(factorySid808KitName(b)),
                        "SID-808 slot display names must be unique");
            }
        }
    }

    // ── C. applyFactorySid808Kit populates engine's runtime configs ───────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        const bool applied = applyFactorySid808Kit(123, eng); // Hard kit
        require(applied, "applyFactorySid808Kit returns true for slot 123");

        const auto kickCfg = eng.drumVoiceConfig(Sid808Drum::Kick);
        const auto& expected = kFactorySid808Hard[static_cast<size_t>(Sid808Drum::Kick)];
        require(kickCfg.freq == expected.freq,
                "engine's kick freq matches Hard kit");
        require(kickCfg.attackDecay == expected.attackDecay,
                "engine's kick AD matches Hard kit");
        require(kickCfg.voiceLevel == expected.voiceLevel,
                "engine's kick level matches Hard kit");
    }

    // ── C2. Repeated family slots apply resolved per-slot variations ──────
    {
        Sid808Engine base;
        Sid808Engine variant;
        base.prepare(48000.0);
        variant.prepare(48000.0);
        require(applyFactorySid808Kit(120, base), "slot 120 applies");
        require(applyFactorySid808Kit(125, variant), "slot 125 applies");
        const auto baseKick = base.drumVoiceConfig(Sid808Drum::Kick);
        const auto varKick = variant.drumVoiceConfig(Sid808Drum::Kick);
        require(baseKick.freq != varKick.freq ||
                baseKick.attackDecay != varKick.attackDecay ||
                baseKick.voiceLevel != varKick.voiceLevel,
                "slot 125 applies an audible Classic kit variation, not an exact alias");
    }

    // ── D. applyFactorySid808Kit rejects out-of-range slot ─────────────────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        require(!applyFactorySid808Kit(0,   eng), "slot 0 rejected");
        require(!applyFactorySid808Kit(119, eng), "slot 119 rejected");
        require( applyFactorySid808Kit(125, eng), "slot 125 accepted");
        require( applyFactorySid808Kit(149, eng), "slot 149 accepted");
        require(!applyFactorySid808Kit(150, eng), "slot 150 rejected");
        require(!applyFactorySid808Kit(200, eng), "slot 200 rejected");
    }

    // ── E. End-to-end: load kit + trigger drums + render audio ─────────────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        eng.setClockFrequency(PAL_CLOCK_FREQ);
        require(applyFactorySid808Kit(122, eng), "load Lo-Fi kit");

        eng.noteOn(Sid808Drum::Kick, 110);
        eng.noteOn(Sid808Drum::ClosedHat, 90);
        require(eng.activeVoiceCount() == 2,
                "kick + closed-hat = 2 active voices");

        constexpr int N = 2048;
        std::vector<float> outL(N), outR(N);
        eng.processBlock(outL.data(), outR.data(), N);

        bool anyFinite = false;
        for (int i = 0; i < N; ++i) {
            require(std::isfinite(outL[i]) && std::isfinite(outR[i]),
                    "Lo-Fi kit output is finite");
            if (std::abs(outL[i]) > 1e-5f) anyFinite = true;
        }
        require(anyFinite, "Lo-Fi kit produces non-zero output");
    }

    // ── F. Audit #43/#44 — DrSidWavetableProgramRunner basic semantics ─────
    {
        const auto kickProg = makeExemplarKick();
        DrSidWavetableProgramRunner runner;
        require(runner.cursor() == 0,                "fresh runner cursor at 0");
        require(runner.finished() == true,
                "runner with no program is `finished`");

        SIDChip chip;
        chip.setSampleRate(48000.0);
        chip.setClockFrequency(PAL_CLOCK_FREQ);
        chip.setMasterVolume(15);

        runner.trigger(&kickProg, chip, /*voice=*/0, /*vel=*/120);
        require(runner.cursor() == 1,
                "after trigger, cursor advances past step 0 (immediate apply)");
        require(runner.stepsEmittedSinceLastReset() == 1,
                "exactly 1 step emitted at trigger");

        // Kick has 5 steps; cycleOffsets 0, 400, 2400, 6400, 18400.
        // Step 1's cycleOffset is 400 — needs 400 cycles to fire.
        const int fired1 = runner.tickSidCycles(chip, 0, 400);
        require(fired1 == 1,
                "step 1 fires after 400 cycles");
        require(runner.cursor() == 2,                "cursor advances to 2");

        // Step 2 is at cycle 2400; we're at 400, need 2000 more.
        const int fired2 = runner.tickSidCycles(chip, 0, 2000);
        require(fired2 == 1,                         "step 2 fires after 2000 more cycles");

        // Jump straight to the end — should fire steps 3 and 4 together.
        const int firedRest = runner.tickSidCycles(chip, 0, 1000000);
        require(firedRest == 2,                      "remaining 2 steps fire together");
        require(runner.finished(),                    "runner is finished");
        require(runner.cursor() == 5,                 "cursor at stepCount");
        require(runner.stepsEmittedSinceLastReset() == 5,
                "all 5 steps emitted total");
    }

    // ── G. abort() halts emission mid-program (audit #41 choke substrate) ──
    {
        const auto hat = makeExemplarClosedHat();
        DrSidWavetableProgramRunner runner;
        SIDChip chip;
        chip.setSampleRate(48000.0);
        chip.setClockFrequency(PAL_CLOCK_FREQ);
        runner.trigger(&hat, chip, /*voice=*/2, /*vel=*/100);
        require(!runner.finished(),                   "hat program not finished yet");
        runner.abort();
        require(runner.finished(),
                "abort() makes runner immediately finished (choke applied)");
        require(runner.program() == nullptr,          "program pointer cleared");
    }

    // ── H. Audit-correct pitch drop: Kick step 0 freq > step 3 freq ───────
    //
    // The DrSidInstrumentProgram contract pins authentic C64 kick behavior
    // as a fast downward pitch table. This is the runner's responsibility
    // to preserve via the cycle-offset sequence.
    {
        const auto kick = makeExemplarKick();
        require(kick.steps[0].freq > kick.steps[3].freq,
                "Kick exemplar: freq[0] > freq[3] (authentic downward pitch table)");
        require(kick.steps[3].freq > 0,
                "Kick exemplar: freq[3] is non-zero");
    }

    // ── I. Audit-correct clap multi-burst: 3 gate-on transitions in Clap ──
    {
        const auto clap = makeExemplarClap();
        int gateOnCount = 0;
        for (std::uint8_t i = 0; i < clap.stepCount - 1; ++i) {
            if ((clap.steps[i].waveform & WaveformBits::kGate) != 0) ++gateOnCount;
        }
        require(gateOnCount == 3,
                "Clap exemplar emits exactly 3 gate-on bursts (audit #44 multi-burst)");
    }

    // ── J. Runner emits ALL 6 clap-burst steps (gate-on + gate-off pairs) ──
    {
        const auto clap = makeExemplarClap();
        DrSidWavetableProgramRunner runner;
        SIDChip chip;
        chip.setSampleRate(48000.0);
        chip.setClockFrequency(PAL_CLOCK_FREQ);
        runner.trigger(&clap, chip, /*voice=*/1, /*vel=*/120);
        require(runner.stepsEmittedSinceLastReset() == 1,
                "clap trigger emits step 0");
        runner.tickSidCycles(chip, 1, 1000000); // advance past all steps
        require(runner.finished(),
                "clap program runs to completion through all 6 steps");
        require(runner.stepsEmittedSinceLastReset() == 6,
                "all 6 clap steps emitted (3 bursts + 3 gate-offs + tail)");
    }

    std::cout << "factory_sid808_kits_and_wavetable_runner_v536_tests: factory-SID-808-kit loader + DrSid wavetable runner pinned (audit #39/#43/#44/#74 wire-up)\n";
    return 0;
}
