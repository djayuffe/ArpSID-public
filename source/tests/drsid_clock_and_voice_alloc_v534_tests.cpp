// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// drsid_clock_and_voice_alloc_v534_tests.cpp
//
// Closes two DrSidEngine audit items in the smallest-responsible-slice
// approach (the full engine-split is deferred — see audit-progression doc):
//
// * Audit #45 — DrSidEngine::setClockFrequency no longer silently falls
// back to PAL on invalid input. The new behavior is "reject + keep
// last valid clock + tick diagnostic counter". A misbehaving upstream
// can no longer re-pitch all drums by ~3.8 % without leaving an
// observable trace.
//
// * Audit #41/#42 — DrSidEngine exposes a typed 12-voice
// SidVoiceAllocator with explicit stealing policy + choke groups,
// alongside the legacy "lowest envelope" overlay allocator. Host /
// sequencer code can opt into deterministic voice allocation now;
// the full engine-split that REPLACES the legacy path is the
// follow-up multi-session work.

#include "arpsid/engines/drsid_engine.h"
#include "arpsid/core/sid_voice_allocator.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>

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

    // ── A. Audit #45 — valid clock-frequency arrives without rejection ────
    {
        DrSidEngine eng;
        eng.setSampleRate(48000.0);
        eng.resetInvalidClockFrequencyRejectCount();
        eng.setClockFrequency(NTSC_CLOCK_FREQ);
        require(eng.invalidClockFrequencyRejectCount() == 0,
                "valid NTSC clock is accepted without counter tick");
        eng.setClockFrequency(PAL_CLOCK_FREQ);
        require(eng.invalidClockFrequencyRejectCount() == 0,
                "valid PAL clock is accepted without counter tick");
    }

    // ── B. Audit #45 — invalid clock is REJECTED, counter ticks ───────────
    {
        DrSidEngine eng;
        eng.setSampleRate(48000.0);
        eng.setClockFrequency(NTSC_CLOCK_FREQ); // establish a known good clock
        eng.resetInvalidClockFrequencyRejectCount();

        // Zero — must be rejected.
        eng.setClockFrequency(0.0);
        require(eng.invalidClockFrequencyRejectCount() == 1,
                "zero clock is rejected (audit #45 fix)");

        // Negative — must be rejected.
        eng.setClockFrequency(-1.0);
        require(eng.invalidClockFrequencyRejectCount() == 2,
                "negative clock is rejected");

        // NaN — must be rejected.
        eng.setClockFrequency(std::numeric_limits<double>::quiet_NaN());
        require(eng.invalidClockFrequencyRejectCount() == 3,
                "NaN clock is rejected");

        // Infinity — must be rejected.
        eng.setClockFrequency(std::numeric_limits<double>::infinity());
        require(eng.invalidClockFrequencyRejectCount() == 4,
                "infinity clock is rejected");

        // After all rejections, valid clock still accepted.
        eng.setClockFrequency(PAL_CLOCK_FREQ);
        require(eng.invalidClockFrequencyRejectCount() == 4,
                "valid clock after rejections is accepted (counter unchanged)");
    }

    // ── C. Audit #45 — cold-start with invalid clock falls back to PAL once ─
    //
    // The contract for a freshly-constructed engine: clockFreq member is
    // initialized to PAL_CLOCK_FREQ (constructor default), so even if the
    // first setClockFrequency call is invalid, we still operate at PAL.
    {
        DrSidEngine eng;
        eng.setSampleRate(48000.0);
        eng.resetInvalidClockFrequencyRejectCount();
        eng.setClockFrequency(0.0); // invalid first call
        require(eng.invalidClockFrequencyRejectCount() == 1,
                "first invalid clock still tick the counter");
        // Engine should still render fine at PAL fallback.
    }

    // ── D. Audit #41/#42 — DrSidEngine exposes typed voice allocator ─────
    {
        DrSidEngine eng;
        auto& a = eng.drumVoiceAllocator();
        require(a.activeCount() == 0,                            "fresh allocator has 0 active voices");
        require(DrSidEngine::DrumVoiceAllocator::kVoiceSlotCount == 12,
                "DrSidEngine allocator has 12 slots (audit #42 — matches legacy overlay pool size)");

        // Allocate 12 voices (fill the pool).
        for (int i = 0; i < 12; ++i) {
            const std::uint8_t v = eng.allocateDrumVoice(
                static_cast<std::uint8_t>(36 + i), 100);
            require(v == static_cast<std::uint8_t>(i),
                    "voices fill slots 0..11 in order");
        }
        require(a.activeCount() == 12, "all 12 slots in use");

        // 13th voice triggers stealing (StealOldest policy by default).
        const std::uint8_t stolen = eng.allocateDrumVoice(50, 100);
        require(stolen != kNoVoice,
                "13th voice triggers stealing (audit #41 — explicit policy, not 'lowest envelope')");
        require(a.activeCount() == 12,
                "still exactly 12 active voices after stealing (audit #42 — pool cap honored)");
    }

    // ── E. Audit #41 — choke groups work via the new allocator ───────────
    {
        DrSidEngine eng;
        eng.drumVoiceAllocatorReset();

        // Closed hat allocated to slot 0.
        const std::uint8_t hatA = eng.allocateDrumVoice(42, 100, Drsid::ChokeGroup::HiHat);
        require(hatA == 0, "closed-hat allocated to slot 0");

        // Unrelated kick allocated to slot 1.
        const std::uint8_t kick = eng.allocateDrumVoice(36, 100, Drsid::ChokeGroup::None);
        require(kick == 1, "kick allocated to slot 1");

        // Open hat with same choke group — must reuse slot 0 (the HiHat choke).
        const std::uint8_t hatB = eng.allocateDrumVoice(46, 100, Drsid::ChokeGroup::HiHat);
        require(hatB == hatA,
                "open-hat in HiHat choke group reuses the closed-hat slot (audit #41 — explicit choke)");
        require(eng.drumVoiceAllocator().activeCount() == 2,
                "choke does not increase active count");
        require(eng.drumVoiceAllocator().slot(hatA).midiNote == 46,
                "choked slot plays the NEW hat note");
    }

    // ── F. Audit #41 — Refuse policy is available ────────────────────────
    {
        DrSidEngine eng;
        eng.drumVoiceAllocatorReset();
        eng.setDrumVoiceStealingPolicy(VoiceStealingPolicy::Refuse);
        for (int i = 0; i < 12; ++i) {
            eng.allocateDrumVoice(static_cast<std::uint8_t>(36 + i), 100);
        }
        const std::uint8_t refused = eng.allocateDrumVoice(50, 100);
        require(refused == kNoVoice,
                "Refuse policy returns kNoVoice when pool is full");
        require(eng.drumVoiceAllocator().activeCount() == 12,
                "existing voices untouched after Refuse");
    }

    // ── G. Audit #41 — release/freeReleased two-phase semantics ──────────
    {
        DrSidEngine eng;
        eng.drumVoiceAllocatorReset();
        eng.allocateDrumVoice(36, 100);
        const std::uint8_t v = eng.releaseDrumVoice(36);
        require(v == 0,                                      "release returns voice index");
        require(eng.drumVoiceAllocator().slot(0).isReleasing,"slot marked releasing");
        require(eng.drumVoiceAllocator().slot(0).inUse,      "slot still inUse until freeReleased");

        eng.freeReleasedDrumVoice(0);
        require(!eng.drumVoiceAllocator().slot(0).inUse,
                "freeReleased clears the slot");
    }

    std::cout << "drsid_clock_and_voice_alloc_v534_tests: audit #45 reject-and-counter + #41/#42 typed allocator pinned\n";
    return 0;
}
