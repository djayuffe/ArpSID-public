// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// sid808_engine_and_router_v535_tests.cpp
//
// Closes the engine-split architecture (audit #39 / #74):
//
// * `Sid808Engine` is a CLEAN, FROM-SCRATCH analog x0x projection
// engine separate from DrSidEngine. One SIDChip, three voices, per// drum-family voice reservation (audit #42), choke groups (audit #41).
// * `DrumEngineRouter` dispatches between DrSidEngine and Sid808Engine
// based on the `DrumContext` (audit #5/#39/#74 context-aware routing).
//
// Test surface:
// A. Sid808Engine constructs + prepares at canonical SR
// B. Per-drum voice reservation invariant (audit #42): kick → voice 0,
// hat → voice 2, snare/clap → voice 1
// C. Choke group invariant (audit #41): closed-hat + open-hat share slot
// D. Sid808Engine clock rejection mirrors DrSidEngine (audit #45)
// E. Sid808Engine produces finite, non-trivial rendered audio
// F. setDrumVoiceConfig overrides default per-drum config
// G. DrumEngineRouter dispatches DrSID slot 47 → DrSidEngine
// H. DrumEngineRouter dispatches SID-808 slot 125 → Sid808Engine
// I. DrumEngineRouter unrouted (None context) increments diagnostic counter
// J. Router setActiveIdentityFromFactorySlot uses combined classifier
// K. Router allNotesOff resets BOTH engines
// L. Realtime forensic config applies to SID-808 and router-active engines

#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/sid808_engine.h"
#include "arpsid/engines/drum_engine_router.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
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

    // ── A. Sid808Engine basic construction + preparation ─────────────────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        eng.setClockFrequency(PAL_CLOCK_FREQ);
        require(eng.sampleRate() == 48000.0,         "Sid808Engine sample rate stored");
        require(eng.clockFrequency() == PAL_CLOCK_FREQ, "Sid808Engine clock stored");
        require(eng.activeVoiceCount() == 0,         "fresh engine has 0 active voices");
    }

    // ── B. Audit #42 — per-drum-family voice reservation ─────────────────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        const std::uint8_t kickVoice  = eng.noteOn(Sid808Drum::Kick, 100);
        require(kickVoice == 0,                     "kick reserved to SID voice 0");
        const std::uint8_t hatVoice   = eng.noteOn(Sid808Drum::ClosedHat, 100);
        require(hatVoice == 2 || hatVoice == 1,
                "closed-hat reserved to voice 2 (or voice 1 if 2 taken)");
        const std::uint8_t snareVoice = eng.noteOn(Sid808Drum::Snare, 100);
        require(snareVoice != kickVoice && snareVoice != hatVoice,
                "snare lands in the remaining voice");
        require(eng.activeVoiceCount() == 3,        "3 voices active");
    }

    // ── C. Audit #41 — HiHat choke group: closed/open hat share slot ─────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        const std::uint8_t closedHat = eng.noteOn(Sid808Drum::ClosedHat, 100);
        const std::uint8_t openHat   = eng.noteOn(Sid808Drum::OpenHat, 100);
        require(closedHat == openHat,
                "audit #41 — open-hat reuses the closed-hat slot (HiHat choke group)");
        require(eng.activeVoiceCount() == 1,
                "choke does not increase active count");
    }

    // ── D. Audit #45 — invalid clock rejection mirrors DrSidEngine ──────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        eng.setClockFrequency(PAL_CLOCK_FREQ);
        eng.resetInvalidClockFrequencyRejectCount();
        eng.setClockFrequency(0.0);
        eng.setClockFrequency(-1.0);
        eng.setClockFrequency(std::numeric_limits<double>::quiet_NaN());
        eng.setClockFrequency(std::numeric_limits<double>::infinity());
        require(eng.invalidClockFrequencyRejectCount() == 4,
                "Sid808Engine rejects 4 invalid clocks (matches audit #45 fix)");
        require(eng.clockFrequency() == PAL_CLOCK_FREQ,
                "valid clock preserved through invalid rejections");
    }

    // ── E. Sid808Engine produces finite non-trivial audio ────────────────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        eng.setClockFrequency(PAL_CLOCK_FREQ);
        eng.noteOn(Sid808Drum::Kick, 120);
        constexpr int N = 4096;
        std::vector<float> outL(N), outR(N);
        eng.processBlock(outL.data(), outR.data(), N);
        double sumSq = 0.0;
        int finiteCount = 0;
        for (int i = 0; i < N; ++i) {
            if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) continue;
            sumSq += (double)outL[i] * outL[i] + (double)outR[i] * outR[i];
            ++finiteCount;
        }
        require(finiteCount == N,                          "all samples finite");
        const double rms = std::sqrt(sumSq / (2.0 * N));
        require(rms > 0.001 && rms < 1.0,
                "kick produces non-trivial bounded RMS energy");
    }

    // ── F. setDrumVoiceConfig overrides default ──────────────────────────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        Sid808VoiceConfig custom{};
        custom.freq           = 0xFFFF;
        custom.pulseWidth     = 0x800;
        custom.waveform       = 0x10;
        custom.attackDecay    = 0x77;
        custom.sustainRelease = 0x44;
        custom.voiceLevel     = 0.50f;
        eng.setDrumVoiceConfig(Sid808Drum::Kick, custom);
        const auto readback = eng.drumVoiceConfig(Sid808Drum::Kick);
        require(readback.freq == 0xFFFF,                "custom freq round-trips");
        require(readback.voiceLevel == 0.50f,           "custom voice level round-trips");
    }

    // ── F2. Realtime forensic config applies to SID-808's SID chip ───────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        ArpSIDForensicConfig cfg{};
        cfg.enable = true;
        cfg.intensity = 0.42f;
        cfg.clockJitterEnabled = true;
        cfg.clockJitter = 0.77f;
        cfg.supplyRippleEnabled = true;
        cfg.supplyRipple = 0.31f;
        cfg.revision = 5u;
        eng.setForensicConfig(cfg);

        require(eng.getForensicConfig().enable, "SID-808 stores forensic enable");
        require(std::abs(eng.getForensicConfig().intensity - 0.42f) < 1.0e-6f,
                "SID-808 forensic intensity round-trips");
        require(std::abs(eng.chip().getForensicConfig().clockJitter - 0.77f) < 1.0e-6f,
                "SID-808 applies forensic config to underlying SID chip");
        eng.setSidModel(SIDModel::MOS6581);
        require(std::abs(eng.chip().getForensicConfig().supplyRipple - 0.31f) < 1.0e-6f,
                "SID-808 preserves forensic config across SID model changes");
    }

    // ── F3. 6581 compensation + live telemetry readback ─────────────────
    {
        Sid808Engine eng;
        eng.prepare(48000.0);
        eng.setSidModel(SIDModel::MOS6581);
        const auto rawKick = eng.drumVoiceConfig(Sid808Drum::Kick);
        const auto compensatedKick = eng.compensatedDrumVoiceConfig(Sid808Drum::Kick);
        require(compensatedKick.freq > rawKick.freq,
                "6581 SID-808 kick runtime pitch is compensated");
        require(compensatedKick.voiceLevel >= rawKick.voiceLevel,
                "6581 SID-808 kick runtime level is compensated");

        const std::uint8_t voice = eng.noteOn(Sid808Drum::Kick, 127, 36);
        require(voice != kNoVoice, "6581 compensated kick allocates a voice");
        require(eng.noteOnCount() == 1, "SID-808 note-on telemetry increments");
        require(eng.lastMidiNote() == 36, "SID-808 last MIDI note telemetry is canonical");
        require(eng.lastDrum() == Sid808Drum::Kick, "SID-808 last drum telemetry is canonical");
        require(eng.lastVoice() == voice, "SID-808 last voice telemetry follows allocator");

        float drumLevels[8]{};
        float voiceLevels[3]{};
        eng.copyDrumLevels(drumLevels, 8);
        eng.copyVoiceLevels(voiceLevels, 3);
        require(drumLevels[static_cast<int>(Sid808Drum::Kick)] > 0.9f,
                "SID-808 drum telemetry publishes kick activity");
        require(voiceLevels[voice] > 0.9f,
                "SID-808 voice telemetry publishes allocated voice activity");

        constexpr int N = 512;
        std::vector<float> outL(N), outR(N);
        eng.processBlock(outL.data(), outR.data(), N);
        eng.copyDrumLevels(drumLevels, 8);
        require(drumLevels[static_cast<int>(Sid808Drum::Kick)] > 0.0f &&
                drumLevels[static_cast<int>(Sid808Drum::Kick)] < 1.0f,
                "SID-808 drum telemetry decays after render");
        eng.allNotesOff();
        eng.copyDrumLevels(drumLevels, 8);
        require(drumLevels[static_cast<int>(Sid808Drum::Kick)] == 0.0f,
                "SID-808 allNotesOff clears drum telemetry");
    }

    // ── G. DrumEngineRouter dispatches DrSID slot 47 → DrSidEngine ───────
    {
        DrSidEngine drsid;
        Sid808Engine sid808;
        drsid.setSampleRate(48000.0);
        sid808.prepare(48000.0);

        DrumEngineRouter router(drsid, sid808);
        router.setActiveIdentityFromFactorySlot(47); // DrSID legacy slot
        require(router.activeIdentity().context == DrumContext::DrSID_C64Wavetable,
                "slot 47 routes to DrSID context");

        router.resetDiagnostics();
        router.noteOn(SidGMDrumClass::Kick, 100);
        require(router.diagnostics().drsidNoteOnCount == 1,
                "DrSID noteOn counter incremented");
        require(router.diagnostics().sid808NoteOnCount == 0,
                "SID-808 engine untouched");
    }

    // ── H. DrumEngineRouter dispatches SID-808 slot 125 → Sid808Engine ───
    {
        DrSidEngine drsid;
        Sid808Engine sid808;
        drsid.setSampleRate(48000.0);
        sid808.prepare(48000.0);

        DrumEngineRouter router(drsid, sid808);
        router.setActiveIdentityFromFactorySlot(125); // canonical SID-808 slot
        require(router.activeIdentity().context == DrumContext::SID808_AnalogProjection,
                "slot 125 routes to SID-808 context");

        router.resetDiagnostics();
        router.noteOn(SidGMDrumClass::Kick, 100);
        router.noteOn(SidGMDrumClass::Snare, 100);
        require(router.diagnostics().sid808NoteOnCount == 2,
                "SID-808 noteOn counter incremented twice");
        require(router.diagnostics().drsidNoteOnCount == 0,
                "DrSID engine untouched");
        require(sid808.activeVoiceCount() == 2,
                "Sid808Engine has 2 voices active");
    }

    // ── I. Unrouted context (None) increments diagnostic counter ─────────
    {
        DrSidEngine drsid;
        Sid808Engine sid808;
        drsid.setSampleRate(48000.0);
        sid808.prepare(48000.0);

        DrumEngineRouter router(drsid, sid808);
        // Slot 64 → DrumContext::None (it's a melodic slot in the legacy bank).
        router.setActiveIdentityFromFactorySlot(64);
        require(router.activeIdentity().context == DrumContext::None,
                "slot 64 has no drum context");

        router.resetDiagnostics();
        router.noteOn(SidGMDrumClass::Kick, 100);
        require(router.diagnostics().unroutedNoteOnCount == 1,
                "unrouted noteOn increments dedicated diagnostic counter");
        require(router.diagnostics().drsidNoteOnCount == 0 &&
                router.diagnostics().sid808NoteOnCount == 0,
                "no engine touched for unrouted context");
    }

    // ── J. Router uses combined classifier ───────────────────────────────
    {
        DrSidEngine drsid;
        Sid808Engine sid808;
        drsid.setSampleRate(48000.0);
        sid808.prepare(48000.0);

        DrumEngineRouter router(drsid, sid808);
        // Slot 120: legacy DrSID projection (112-124) AND new SID-808 range
        // (120-149). Combined classifier resolves to SID-808 (new wins).
        router.setActiveIdentityFromFactorySlot(120);
        require(router.activeIdentity().context == DrumContext::SID808_AnalogProjection,
                "slot 120 (legacy-DrSID + new-SID-808 overlap) routes to SID-808 (new wins)");

        // Slot 47: legacy DrSID only.
        router.setActiveIdentityFromFactorySlot(47);
        require(router.activeIdentity().context == DrumContext::DrSID_C64Wavetable,
                "slot 47 (legacy DrSID only) routes to DrSID");

        // Slot 150: new Digi only.
        router.setActiveIdentityFromFactorySlot(150);
        require(router.activeIdentity().context == DrumContext::Digi4Bit,
                "slot 150 routes to Digi context (unrouted since no Digi engine yet)");
    }

    // ── K. Router allNotesOff resets BOTH engines ────────────────────────
    {
        DrSidEngine drsid;
        Sid808Engine sid808;
        drsid.setSampleRate(48000.0);
        sid808.prepare(48000.0);

        DrumEngineRouter router(drsid, sid808);
        // Drive both engines via direct allocator + router note-on.
        drsid.allocateDrumVoice(36, 100);
        router.setActiveIdentityFromFactorySlot(125);
        router.noteOn(SidGMDrumClass::Kick, 100);
        require(drsid.drumVoiceAllocator().activeCount() > 0,  "DrSID has active voice");
        require(sid808.activeVoiceCount() > 0,                  "SID-808 has active voice");

        router.allNotesOff();
        require(drsid.drumVoiceAllocator().activeCount() == 0, "DrSID allocator reset");
        require(sid808.activeVoiceCount() == 0,                "SID-808 reset");
    }

    // ── L. Router processBlock dispatches to active engine ───────────────
    {
        DrSidEngine drsid;
        Sid808Engine sid808;
        drsid.setSampleRate(48000.0);
        sid808.prepare(48000.0);

        DrumEngineRouter router(drsid, sid808);
        router.setActiveIdentityFromFactorySlot(125); // SID-808
        router.noteOn(SidGMDrumClass::Kick, 100);

        constexpr int N = 1024;
        std::vector<float> outL(N), outR(N);
        router.processBlock(outL.data(), outR.data(), N);

        bool anyNonZero = false;
        for (int i = 0; i < N; ++i) {
            require(std::isfinite(outL[i]) && std::isfinite(outR[i]),
                    "router output is finite");
            if (std::abs(outL[i]) > 1e-6f || std::abs(outR[i]) > 1e-6f) {
                anyNonZero = true;
            }
        }
        require(anyNonZero,
                "router routed through Sid808Engine produces non-trivial audio");
    }

    // ── M. Router forensic projection reaches both drum engines ──────────
    {
        DrSidEngine drsid;
        Sid808Engine sid808;
        drsid.setSampleRate(48000.0);
        sid808.prepare(48000.0);

        DrumEngineRouter router(drsid, sid808);
        ArpSIDForensicConfig cfg{};
        cfg.enable = true;
        cfg.intensity = 0.66f;
        cfg.thermalDriftEnabled = true;
        cfg.thermalDrift = 0.44f;
        router.setForensicConfig(cfg);

        router.setActiveIdentityFromFactorySlot(125);
        require(std::abs(router.activeForensicConfig().thermalDrift - 0.44f) < 1.0e-6f,
                "router exposes active SID-808 forensic config");
        require(std::abs(sid808.chip().getForensicConfig().intensity - 0.66f) < 1.0e-6f,
                "router projected forensic config into SID-808 chip");

        router.setActiveIdentityFromFactorySlot(47);
        require(std::abs(router.activeForensicConfig().intensity - 0.66f) < 1.0e-6f,
                "router exposes active DrSID forensic config after context switch");
        require(std::abs(drsid.getForensicConfig().thermalDrift - 0.44f) < 1.0e-6f,
                "router projected forensic config into DrSID");
    }

    std::cout << "sid808_engine_and_router_v535_tests: engine-split architecture pinned — Sid808Engine + DrumEngineRouter operational\n";
    return 0;
}
