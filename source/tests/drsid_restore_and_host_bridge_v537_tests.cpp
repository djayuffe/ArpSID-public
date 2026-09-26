// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// drsid_restore_and_host_bridge_v537_tests.cpp
//
// Closes two audit items in one slice:
//
// * Audit #40 — DrSID state restore must respect the serialized
// `drumMachineModel` as ground truth. The legacy code coerced model
// based on `legacyDigitalOverlayAmount > 0`, silently flipping
// SidAuthentic-saved kits to AnalogX0X8 on load. The fix: serialized
// model wins; overlay amount is restored as independent state. Test
// verifies both restore paths + diagnostic counters tick correctly.
//
// * Wire-up of `DrumEngineHostBridge` — the host-side reference
// integration of the engine-split architecture. Test verifies:
// - Bridge loads SID-808 slots through Sid808Engine with the right
// kit config
// - Bridge loads DrSID slots through DrSidEngine
// - Note-on routed via bridge ends up on the right engine
// - processBlock produces audio on the active engine only
// - Diagnostics counters tick per slot/load type

#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/drum_engine_host_bridge.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

// Build a synthetic AnalogueRuntimeSnapshot — only the fields audit #40
// cares about. Other fields default-constructed (zero/empty) so the
// restore path exercises just the model/overlay decision.
ArpSID::DrSidEngine::AnalogueRuntimeSnapshot makeSnapshot(
    float drumMachineModel,
    float legacyDigitalOverlayAmount,
    bool authenticC64DrumMode) noexcept
{
    ArpSID::DrSidEngine::AnalogueRuntimeSnapshot s{};
    s.drumMachineModel = drumMachineModel;
    s.legacyDigitalOverlayAmount = legacyDigitalOverlayAmount;
    s.authenticC64DrumMode = authenticC64DrumMode;
    s.cleanRestartRampEnabled = false;
    s.accentAmount = 0.0f;
    s.outputDrive = 0.0f;
    s.hatMetal = 0.0f;
    s.clapSpread = 0.0f;
    return s;
}

} // namespace

int main() {
    using namespace ArpSID;

    // ── A. Audit #40 — serialized SidAuthentic + overlay=0 stays SidAuthentic
    {
        DrSidEngine eng;
        eng.setSampleRate(48000.0);
        eng.resetStateRestoreDiagnostics();
        const auto snap = makeSnapshot(/*model=SidAuthentic=*/0.0f,
                                       /*overlay=*/0.0f,
                                       /*authentic=*/true);
        eng.restoreAnalogueRuntimeState(snap);
        require(eng.stateRestoreModelDecisionsCount() == 1,
                "audit #40 — restore decisions counter ticks");
        require(eng.stateRestoreOverlayAuthoredAuthenticCount() == 0,
                "audit #40 — overlay==0 with SidAuthentic does NOT increment authored counter");
    }

    // ── B. Audit #40 — serialized SidAuthentic + overlay > 0 KEEPS
    // SidAuthentic (the legacy bug would have flipped to AnalogX0X8)
    {
        DrSidEngine eng;
        eng.setSampleRate(48000.0);
        eng.resetStateRestoreDiagnostics();
        const auto snap = makeSnapshot(/*model=SidAuthentic=*/0.0f,
                                       /*overlay=*/0.35f,
                                       /*authentic=*/true);
        eng.restoreAnalogueRuntimeState(snap);
        require(eng.stateRestoreOverlayAuthoredAuthenticCount() == 1,
                "audit #40 — SidAuthentic with overlay>0 is the authored mix (not silently rewritten)");
        // We can't directly inspect drumMachineModel_ from outside, but the
        // counter tick proves the audit-correct branch ran (it only ticks
        // when the model was SidAuthentic AND overlay > 0).
    }

    // ── C. Audit #40 — serialized AnalogX0X8 + overlay > 0 stays AnalogX0X8
    {
        DrSidEngine eng;
        eng.setSampleRate(48000.0);
        eng.resetStateRestoreDiagnostics();
        const auto snap = makeSnapshot(/*model=AnalogX0X8=*/1.0f,
                                       /*overlay=*/0.50f,
                                       /*authentic=*/false);
        eng.restoreAnalogueRuntimeState(snap);
        require(eng.stateRestoreOverlayAuthoredAnalogCount() == 1,
                "audit #40 — AnalogX0X8 with overlay>0 tracked");
    }

    // ── D. Audit #40 — multiple restores accumulate decision counter ──────
    {
        DrSidEngine eng;
        eng.setSampleRate(48000.0);
        eng.resetStateRestoreDiagnostics();
        for (int i = 0; i < 5; ++i) {
            eng.restoreAnalogueRuntimeState(
                makeSnapshot(0.0f, 0.0f, true));
        }
        require(eng.stateRestoreModelDecisionsCount() == 5,
                "5 restores → 5 decision ticks");
    }

    // ── E. DrumEngineHostBridge construction + prepare ─────────────────────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        bridge.setClockFrequency(PAL_CLOCK_FREQ);
        require(bridge.sampleRate() == 48000.0,
                "bridge prepare stores sample rate");
        // Active identity defaults to None until loadFactorySlot is called.
        require(bridge.activeIdentity().context == DrumContext::None,
                "fresh bridge has DrumContext::None");
    }

    // ── F. Bridge load SID-808 slot 121 → Punch kit + SID-808 context ─────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        bridge.resetLoadDiagnostics();
        const bool ok = bridge.loadFactorySlot(121);
        require(ok, "slot 121 loads successfully");
        require(bridge.activeIdentity().context == DrumContext::SID808_AnalogProjection,
                "slot 121 routes to SID-808 context");
        require(bridge.loadDiagnostics().slotLoadCount == 1,
                "slotLoad ticked");
        require(bridge.loadDiagnostics().sid808LoadCount == 1,
                "SID-808 load specifically ticked");
        require(bridge.loadDiagnostics().drsidLoadCount == 0,
                "DrSID load did NOT tick");

        // Sid808Engine drum-config now matches Punch kit byte-for-byte.
        const auto cfg = bridge.sid808Engine().drumVoiceConfig(Sid808Drum::Kick);
        const auto& expected = kFactorySid808Punch[static_cast<size_t>(Sid808Drum::Kick)];
        require(cfg.freq == expected.freq, "Punch kick freq propagated to engine");
        require(cfg.voiceLevel == expected.voiceLevel, "Punch kick voiceLevel propagated");
    }

    // ── F2. Bridge projects forensic realtime effects into active engine ──
    {
        DrSidEngine canonicalDrSid;
        DrumEngineHostBridge bridge(canonicalDrSid);
        bridge.prepare(48000.0);
        require(bridge.loadFactorySlot(121), "load SID-808 Punch kit");

        ArpSIDForensicConfig cfg{};
        cfg.enable = true;
        cfg.intensity = 0.58f;
        cfg.voiceCrosstalkEnabled = true;
        cfg.voiceCrosstalk = 0.37f;
        bridge.setForensicConfig(cfg);

        require(std::abs(bridge.activeForensicConfig().voiceCrosstalk - 0.37f) < 1.0e-6f,
                "bridge exposes SID-808 active forensic config");
        require(std::abs(bridge.sid808Engine().chip().getForensicConfig().intensity - 0.58f) < 1.0e-6f,
                "bridge projected forensic config into SID-808 chip");

        require(bridge.loadFactorySlot(47), "switch to DrSID kit");
        require(std::abs(bridge.activeForensicConfig().intensity - 0.58f) < 1.0e-6f,
                "bridge keeps forensic config live after DrSID context switch");
        require(bridge.canonicalDrSidEngine() == &canonicalDrSid,
                "bridge references the runtime-owned canonical DrSID engine");
        require(std::abs(canonicalDrSid.getForensicConfig().voiceCrosstalk - 0.37f) < 1.0e-6f,
                "bridge projected forensic config into canonical DrSID engine");
    }

    // ── G. Bridge load DrSID slot 47 → DrSID context ──────────────────────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        bridge.resetLoadDiagnostics();
        const bool ok = bridge.loadFactorySlot(47);
        require(ok, "slot 47 loads successfully");
        require(bridge.activeIdentity().context == DrumContext::DrSID_C64Wavetable,
                "slot 47 routes to DrSID context");
        require(bridge.loadDiagnostics().drsidLoadCount == 1,
                "DrSID load ticked");
        require(bridge.loadDiagnostics().sid808LoadCount == 0,
                "SID-808 load did NOT tick");
    }

    // ── H. Bridge load melodic slot → unrouted ─────────────────────────────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        bridge.resetLoadDiagnostics();
        const bool ok = bridge.loadFactorySlot(64); // melodic
        require(!ok, "slot 64 (melodic) NOT loaded as drum kit");
        require(bridge.loadDiagnostics().unroutedLoadCount == 1,
                "unrouted load ticked");
        require(bridge.loadDiagnostics().slotLoadCount == 1, "slotLoad still ticked once");
    }

    // ── I. End-to-end: bridge load SID-808 + noteOn + render → audio ──────
    {
        DrSidEngine canonicalDrSid;
        DrumEngineHostBridge bridge(canonicalDrSid);
        bridge.prepare(48000.0);
        bridge.setClockFrequency(PAL_CLOCK_FREQ);
        require(bridge.loadFactorySlot(120), "load Classic kit"); // SID-808 Classic

        bridge.noteOn(SidGMDrumClass::Kick, 120);
        bridge.noteOn(SidGMDrumClass::Snare, 100);

        // SID-808 engine should hold the active voices (DrSID untouched).
        require(bridge.sid808Engine().activeVoiceCount() == 2,
                "Sid808Engine has 2 active voices");
        require(canonicalDrSid.drumVoiceAllocator().activeCount() == 0,
                "canonical DrSidEngine allocator is dormant");

        constexpr int N = 2048;
        std::vector<float> outL(N), outR(N);
        bridge.processBlock(outL.data(), outR.data(), N);

        bool anyNonZero = false;
        for (int i = 0; i < N; ++i) {
            require(std::isfinite(outL[i]) && std::isfinite(outR[i]),
                    "bridge output finite");
            if (std::abs(outL[i]) > 1e-5f || std::abs(outR[i]) > 1e-5f) {
                anyNonZero = true;
            }
        }
        require(anyNonZero, "bridge routed via SID-808 produces non-zero audio");
    }

    // ── J. End-to-end: bridge router diagnostics tick per note ────────────
    {
        DrumEngineHostBridge bridge;
        bridge.prepare(48000.0);
        bridge.loadFactorySlot(122); // Lo-Fi
        const auto before = bridge.routerDiagnostics();
        bridge.noteOn(SidGMDrumClass::Kick, 100);
        bridge.noteOn(SidGMDrumClass::ClosedHat, 90);
        const auto after = bridge.routerDiagnostics();
        require(after.sid808NoteOnCount == before.sid808NoteOnCount + 2,
                "router sid808NoteOnCount ticked twice");
        require(after.drsidNoteOnCount == before.drsidNoteOnCount,
                "DrSID counter unchanged");
    }

    // ── K. Bridge allNotesOff resets both engines ─────────────────────────
    {
        DrSidEngine canonicalDrSid;
        DrumEngineHostBridge bridge(canonicalDrSid);
        bridge.prepare(48000.0);
        bridge.loadFactorySlot(124);  // Wide kit
        bridge.noteOn(SidGMDrumClass::Kick, 100);
        bridge.noteOn(SidGMDrumClass::OpenHat, 100);

        bridge.loadFactorySlot(47);  // switch to DrSID context
        const auto beforeWavetableTriggers = canonicalDrSid.wavetableTriggerCount();
        bridge.noteOn(SidGMDrumClass::Snare, 100);
        require(canonicalDrSid.wavetableTriggerCount() == beforeWavetableTriggers + 1u,
                "DrSID wavetable authority triggered after slot switch + noteOn");
        require(bridge.sid808Engine().activeVoiceCount() > 0,
                "SID-808 voices still live (not cleared by mode switch)");

        bridge.allNotesOff();
        require(bridge.sid808Engine().activeVoiceCount() == 0,
                "allNotesOff clears SID-808 voices");
        require(canonicalDrSid.drumVoiceAllocator().activeCount() == 0,
                "allNotesOff clears DrSID allocator");
    }

    std::cout << "drsid_restore_and_host_bridge_v537_tests: audit #40 restore-fix + DrumEngineHostBridge wire-up pinned\n";
    return 0;
}
