// ingress_parity_timing_authority_v910_tests.cpp
//
// v910 ingress-parity / timing-authority closure. Behavioral guards for the
// v909 audit's release blockers:
//
// 1) AU3 midiQueue_ ring vs host events[] parity — the transport-start edge
//    used to flush live ring MIDI (42x quieter same-event audio). Both paths
//    now dispatch through dispatchCanonicalIngressEvent_ and must produce
//    bit-identical audio (the strengthened v687 test guards the base case;
//    here we guard NoteOn+NoteOff and the stopped-transport case).
// 2) Large-block chunking: queued async MIDI intended for a later chunk must
//    land at its exact parent-block offset, not clamp into chunk 1's tail.
// 3) Stopped transport must never suppress live/manual instrument NoteOns
//    (Classic/BitPerfect keyboard play works without the DAW rolling).
// 4) GM DrSID promotion matrix at kernel level (Hybrid default OFF, opt-in
//    ON, Instrument never).
// 5) SID808 selected-kit authority: a hit override carrying the selected
//    factory slot resolves that slot's kit data, exactly like the KIT
//    sequencer path (the kernel's direct-MIDI path now passes it).
// 6) Source-shape guards: single ingress authority, parent-scope async
//    normalization, live-play law, no legacy double/floor timing helpers in
//    Phase2, AU3 tree exposure, v910 package identity.

#include "au3/ArpSIDDSPKernel.hpp"
#include "factory_patch_params.h"
#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/engines/sid808_gm_projection.h"
#include "arpsid/patchbank/factory_sid808_kits.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

using namespace ArpSID;

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "ingress_parity_timing_authority_v910_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}



bool isPreservedPass380ClosureLineageVersion(const std::string& version) {
    // v918: keep old closure tests release-forward. These tests verify that
    // their original contracts are preserved by the current pass380 closure
    // train, not that VERSION.txt remains pinned to an obsolete v90x label.
    return version.find("0.0.690-pass380-v") != std::string::npos &&
           version.find("closure") != std::string::npos;
}

void requireContains(const std::string& s, const char* needle, const char* msg) {
    require(s.find(needle) != std::string::npos, msg);
}

void requireNotContains(const std::string& s, const char* needle, const char* msg) {
    require(s.find(needle) == std::string::npos, msg);
}

std::unique_ptr<ArpSIDDSPKernel> makeKernelForSlot(int slot,
                                                   bool drSid,
                                                   bool analogModel) {
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
    std::array<float, kNumParams> edited{};
    for (int pid = 0; pid < kNumParams; ++pid) edited[(size_t)pid] = sidStateRootParamValue(root, pid);
    edited[(size_t)kParamDrSidEnable] = drSid ? 1.0f : 0.0f;
    edited[(size_t)kParamSynthModeEnable] = 0.0f;
    edited[(size_t)kParamDrSidMachineModel] = analogModel ? 1.0f : 0.0f;
    auto k = std::make_unique<ArpSIDDSPKernel>();
    k->setup(48000.0, 512);
    k->setStickyPresetDisplaySlot(slot);
    k->resetPreservingHostParameterSnapshot(edited.data(), kNumParams, slot, true);
    return k;
}

// ── 2) Chunked queued-MIDI offset authority ────────────────────────────────
void test_chunked_queued_midi_lands_in_correct_chunk() {
    constexpr int kParentFrames = 3 * ArpSIDDSPKernel::kMaxFramesPerBlock; // 12288
    constexpr int kTargetOffset = ArpSIDDSPKernel::kMaxFramesPerBlock + 2500; // chunk 2, local 2500

    TransportState t{};
    t.isPlaying = true; t.playStateKnown = true; t.sampleRate = 48000.0; t.frameCount = kParentFrames;

    auto renderBig = [&](bool useRing, std::vector<float>& l, std::vector<float>& r) {
        auto k = makeKernelForSlot(120, /*drSid=*/true, /*analog=*/true);
        l.assign((size_t)kParentFrames, 0.0f);
        r.assign((size_t)kParentFrames, 0.0f);
        float* outs[2] = {l.data(), r.data()};
        if (useRing) {
            const uint8_t noteOn[3] = {0x99, 36, 100};
            k->enqueueMidiIntent(noteOn, 3, kTargetOffset, 0); // parent-relative offset
            k->processBlock(outs, 2, kParentFrames, nullptr, 0, t);
        } else {
            TimedEvent ev{};
            ev.sampleOffset = kTargetOffset; ev.kind = EventKind::NoteOn;
            ev.channel = 9; ev.pitch = 36; ev.value = 100.0f / 127.0f;
            k->processBlock(outs, 2, kParentFrames, &ev, 1, t);
        }
    };

    // Threshold sits well above the always-on forensic noise floor (~1e-4)
    // and well below the kick attack (~7e-2), so it finds the drum onset.
    auto firstAudible = [&](const std::vector<float>& l, const std::vector<float>& r) -> long {
        for (int i = 0; i < kParentFrames; ++i) {
            const double v = 0.5 * (l[(size_t)i] + r[(size_t)i]);
            if (std::fabs(v) > 5.0e-3) return i;
        }
        return -1;
    };

    std::vector<float> evL, evR, ringL, ringR;
    renderBig(false, evL, evR);
    renderBig(true, ringL, ringR);
    const long evFirst = firstAudible(evL, evR);
    const long ringFirst = firstAudible(ringL, ringR);
    std::printf("chunked: events first=%ld ring first=%ld target=%d\n", evFirst, ringFirst, kTargetOffset);
    require(evFirst >= kTargetOffset && evFirst < kTargetOffset + 64,
            "events[] chunked NoteOn must land at its parent-block offset");
    require(ringFirst == evFirst,
            "ring chunked NoteOn must land at the SAME sample as the events[] path (no chunk-1 clamp)");
    for (int i = 0; i < kParentFrames; ++i) {
        require(evL[(size_t)i] == ringL[(size_t)i] && evR[(size_t)i] == ringR[(size_t)i],
                "chunked ring/events audio must be bit-identical");
    }
}

// ── 3) Stopped transport must not suppress live instrument NoteOns ─────────
void test_stopped_transport_live_noteon_plays() {
    constexpr int kFrames = 1024;
    TransportState stopped{};
    stopped.isPlaying = false; stopped.playStateKnown = true;
    stopped.sampleRate = 48000.0; stopped.frameCount = kFrames;

    auto renderStopped = [&](bool useRing) -> double {
        auto k = makeKernelForSlot(0, /*drSid=*/false, /*analog=*/false); // Classic/BitPerfect
        std::array<float, kFrames> l{}, r{};
        float* outs[2] = {l.data(), r.data()};
        double sum = 0.0; long n = 0;
        for (int b = 0; b < 6; ++b) {
            if (b == 0) {
                if (useRing) {
                    const uint8_t noteOn[3] = {0x90, 60, 110}; // channel 1 live note
                    k->enqueueMidiIntent(noteOn, 3, 0, 0);
                    k->processBlock(outs, 2, kFrames, nullptr, 0, stopped);
                } else {
                    TimedEvent ev{};
                    ev.sampleOffset = 0; ev.kind = EventKind::NoteOn;
                    ev.channel = 0; ev.pitch = 60; ev.value = 110.0f / 127.0f;
                    k->processBlock(outs, 2, kFrames, &ev, 1, stopped);
                }
            } else {
                k->processBlock(outs, 2, kFrames, nullptr, 0, stopped);
            }
            for (int i = 0; i < kFrames; ++i) {
                const double v = 0.5 * (l[(size_t)i] + r[(size_t)i]);
                sum += v * v; ++n;
            }
        }
        return std::sqrt(sum / (double)std::max(1L, n));
    };

    const double evRms = renderStopped(false);
    const double ringRms = renderStopped(true);
    std::printf("stopped-transport live play: events RMS=%.6f ring RMS=%.6f\n", evRms, ringRms);
    require(evRms > 1.0e-4,
            "Classic live NoteOn (events[]) must play while the transport is stopped");
    require(ringRms > 1.0e-4,
            "Classic live NoteOn (midiQueue_) must play while the transport is stopped");
    // Idle floor is exactly zero here (verified separately), so both paths
    // playing the same note must also be level-equivalent.
    require(std::fabs(evRms - ringRms) <= 0.25 * evRms,
            "stopped-transport live play must be level-equivalent across ingress paths");
}

// ── 1) NoteOn+NoteOff held-mirror parity across ingress paths ──────────────
void test_noteon_noteoff_ring_events_parity() {
    constexpr int kFrames = 1024;
    TransportState t{};
    t.isPlaying = true; t.playStateKnown = true; t.sampleRate = 48000.0; t.frameCount = kFrames;

    auto renderSeq = [&](bool useRing, std::vector<float>& all) {
        auto k = makeKernelForSlot(0, /*drSid=*/false, /*analog=*/false);
        std::array<float, kFrames> l{}, r{};
        float* outs[2] = {l.data(), r.data()};
        all.clear();
        for (int b = 0; b < 8; ++b) {
            const TimedEvent* evPtr = nullptr;
            int evCount = 0;
            TimedEvent ev{};
            if (b == 0) {
                if (useRing) {
                    const uint8_t noteOn[3] = {0x90, 64, 100};
                    k->enqueueMidiIntent(noteOn, 3, 0, 0);
                } else {
                    ev.sampleOffset = 0; ev.kind = EventKind::NoteOn;
                    ev.channel = 0; ev.pitch = 64; ev.value = 100.0f / 127.0f;
                    evPtr = &ev; evCount = 1;
                }
            } else if (b == 3) {
                if (useRing) {
                    const uint8_t noteOff[3] = {0x80, 64, 0};
                    k->enqueueMidiIntent(noteOff, 3, 0, 0);
                } else {
                    ev.sampleOffset = 0; ev.kind = EventKind::NoteOff;
                    ev.channel = 0; ev.pitch = 64; ev.value = 0.0f;
                    evPtr = &ev; evCount = 1;
                }
            }
            k->processBlock(outs, 2, kFrames, evPtr, evCount, t);
            for (int i = 0; i < kFrames; ++i)
                all.push_back(0.5f * (l[(size_t)i] + r[(size_t)i]));
        }
    };

    std::vector<float> evAudio, ringAudio;
    renderSeq(false, evAudio);
    renderSeq(true, ringAudio);
    require(evAudio.size() == ringAudio.size(), "same render length");
    double sum = 0.0;
    for (size_t i = 0; i < evAudio.size(); ++i) {
        require(evAudio[i] == ringAudio[i],
                "NoteOn+NoteOff sequence must be bit-identical through ring and events[] "
                "(held-mirror / release parity)");
        sum += (double)evAudio[i] * (double)evAudio[i];
    }
    require(std::sqrt(sum / (double)evAudio.size()) > 1.0e-4,
            "the parity sequence must actually produce audio");
}

// ── 4) GM DrSID promotion matrix at kernel level ────────────────────────────
void test_kernel_gm_promotion_matrix() {
    constexpr int kFrames = 512;
    TransportState t{};
    t.isPlaying = true; t.playStateKnown = true; t.sampleRate = 48000.0; t.frameCount = kFrames;

    auto sendCh10Kick = [&](ArpSIDDSPKernel& k) {
        std::array<float, kFrames> l{}, r{};
        float* outs[2] = {l.data(), r.data()};
        TimedEvent ev{};
        ev.sampleOffset = 0; ev.kind = EventKind::NoteOn;
        ev.channel = 9; ev.pitch = 36; ev.value = 0.8f;
        k.processBlock(outs, 2, kFrames, &ev, 1, t);
        k.processBlock(outs, 2, kFrames, nullptr, 0, t);
    };

    {   // Hybrid (Classic) default: channel-10 note must NOT arm DrSID.
        auto k = makeKernelForSlot(0, /*drSid=*/false, /*analog=*/false);
        require(k->getParameter((int)kParamDrSidEnable) < 0.5f, "setup: DrSID off");
        sendCh10Kick(*k);
        require(k->getParameter((int)kParamDrSidEnable) < 0.5f,
                "Hybrid default: GM ch10 note 36 must not auto-arm DrSID");
    }
    {   // Hybrid with explicit opt-in: promotion works.
        auto k = makeKernelForSlot(0, /*drSid=*/false, /*analog=*/false);
        k->setParameter((int)kParamAutoGmDrumPromotion, 1.0f);
        sendCh10Kick(*k);
        require(k->getParameter((int)kParamDrSidEnable) > 0.5f,
                "Hybrid + Auto GM Drum Promotion ON: GM ch10 note 36 must arm DrSID");
    }
    {   // Instrument flavor: never promotes, even with the param on.
        auto k = makeKernelForSlot(0, /*drSid=*/false, /*analog=*/false);
        k->setComponentFlavor((int)ComponentFlavor::Instrument);
        k->setParameter((int)kParamAutoGmDrumPromotion, 1.0f);
        sendCh10Kick(*k);
        require(k->getParameter((int)kParamDrSidEnable) < 0.5f,
                "Instrument flavor must never auto-arm DrSID");
    }
}

// ── 5) SID808 selected-kit authority (hit override resolves the slot) ──────
void test_sid808_selected_slot_override_resolves_kit_data() {
    DrumEngineHostBridge bridge;
    bridge.prepare(48000.0);
    bridge.router().setActiveIdentityFromFactorySlot(120);
    require(applyFactorySid808Kit(120, bridge.sid808Engine()), "engine holds slot 120 kit");

    // Direct hit with the selected-slot override (what the kernel's direct
    // MIDI path now sends) must resolve slot 137's kit data even though the
    // engine's live config is slot 120.
    Sid808HitOverride ov{};
    ov.selectedFactorySlot = 137u;
    ov.hasSelectedFactorySlot = true;
    bridge.noteOnWithOverride(SidGMDrumClass::Kick, 100u, 36u, ov);
    const Sid808VoiceConfig withOverride = bridge.sid808Engine().lastAppliedConfig();

    bridge.sid808Engine().allNotesOff();
    bridge.noteOn(SidGMDrumClass::Kick, 100u, 36u);
    const Sid808VoiceConfig without = bridge.sid808Engine().lastAppliedConfig();

    const Sid808KitConfigTable kit137 = factorySid808ResolvedKitForSlot(137);
    const Sid808KitConfigTable kit120 = factorySid808ResolvedKitForSlot(120);
    const auto k137 = kit137[(size_t)Sid808Drum::Kick];
    const auto k120 = kit120[(size_t)Sid808Drum::Kick];
    require(k137.freq != k120.freq, "slots 120 and 137 must differ (test premise)");
    // The applied config passes through the kick attack transform, so compare
    // the scaled relationship: same transform applied to different bases must
    // produce different applied frequencies, and the override base must track
    // slot 137, not the engine's slot-120 config.
    require(withOverride.freq != without.freq,
            "selected-slot override must change the resolved kick config");
    const double ratio137 = (double)withOverride.freq / (double)k137.freq;
    const double ratio120 = (double)withOverride.freq / (double)k120.freq;
    require(std::fabs(ratio137 - 2.85) < std::fabs(ratio120 - 2.85),
            "override hit must resolve from the SELECTED slot's kit table (kick attack ratio)");
}

// ── DrSID tom default alignment (audit #22) ─────────────────────────────────
void test_drsid_tom_default_alignment() {
    DrSidEngine eng;
    const float paramDefault = defaultNormalizedParamValue((int)kParamDrSidTomDecay);
    require(std::fabs(paramDefault - 0.48f) < 1.0e-4f, "param default is the v909 tom decay");
    // The engine's internal default must equal the projection of the param
    // default (setTomDecay mapping), so a fresh engine and a projected one
    // sound identical before any parameter touch.
    require(std::fabs(eng.tomDecayNormalized() - paramDefault) < 0.02f,
            "DrSidEngine internal tom decay default must match the projected param default");
}

// ── 6) Source-shape guards ──────────────────────────────────────────────────
void test_source_contracts() {
    const std::string kernel = readFile(ARPSID_SOURCE_ROOT "/source/au3/ArpSIDDSPKernel.hpp");
    // Single ingress authority + shared translation + parent-scope chunk drain.
    requireContains(kernel, "void dispatchCanonicalIngressEvent_(const TimedEvent& te",
                    "kernel must own one shared ingress dispatch helper");
    requireContains(kernel, "bool translateRawMidiRingEvent_(",
                    "kernel must own one shared raw-MIDI translation helper");
    requireContains(kernel, "void translateParamIntentEvent_(",
                    "kernel must own one shared param-intent translation helper");
    requireContains(kernel, "void drainAsyncIngressToParentScope_(",
                    "kernel must drain async rings at parent-block scope before chunking");
    requireContains(kernel, "drainAsyncIngressToParentScope_(numFrames, parentAsyncEventScratch_);",
                    "the stereo chunking branch must use parent-scope async normalization");
    // Transport edges must not flush live ingress queues.
    requireContains(kernel, "v910 ring-ingress fix: do NOT flush the live midiQueue_",
                    "transport start must keep live MIDI/param ingress");
    // Live-play law consumed by the kernel note gates.
    requireContains(kernel, "hostBlock.suppressLiveInstrumentNoteOns",
                    "kernel note gates must consume the live-instrument law");
    // SID808 direct MIDI passes the selected slot override.
    requireContains(kernel, "slotOverride.hasSelectedFactorySlot = true;",
                    "SID808 direct MIDI must activate the selected factory kit data");

    const std::string hostBlock = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/core/sid_runtime_host_block.h");
    requireContains(hostBlock, "suppressTransportSequencerNoteOns",
                    "host block law must carry the transport-sequencer gate");
    requireContains(hostBlock, "s.suppressLiveInstrumentNoteOns = false;",
                    "live instrument input is constitutionally never suppressed");

    // Phase2 legacy double/floor timing helpers must stay dead (audit #13).
    const std::string phase2 = readFile(ARPSID_SOURCE_ROOT "/source/arpsid_processor_phase2.cpp");
    requireNotContains(phase2, "void ArpSIDProcessorPhase2::pushSidWriteDelayed",
                       "pushSidWriteDelayed must not return as a production timing path");
    requireNotContains(phase2, "uint64_t ArpSIDProcessorPhase2::absoluteSidCycleAtSampleStart_",
                       "absolute double/floor cycle mapping must not return");
    requireNotContains(phase2, "sidCyclesPerSampleFloor() const {",
                       "floor cycles-per-sample law must not return");


    // v911 remaining-issue closure guards from the v910 source audit.
    requireContains(kernel, "v911 accepted-only held mirror",
                    "ring NoteOn held mirror must happen only after queue acceptance");
    requireContains(kernel, "if (midiQueue_.push(data, len, hostTime, sampleOffset))",
                    "enqueue path must branch on primary queue acceptance before mirroring");
    requireContains(kernel, "v911 malformed-MIDI closure",
                    "raw MIDI translation must reject truncated short messages");
    requireContains(kernel, "if (ev.len < 3) return false;",
                    "3-byte MIDI statuses must reject short messages");
    requireContains(kernel, "case 0xC0u: // Program Change metadata is intentionally not a render event.",
                    "Program Change must be rejected at enqueue as non-render metadata");
    requireContains(kernel, "return false;\n            case 0xD0u: // Channel pressure",
                    "Program Change must not enter midiQueue_ as a dead event");
    requireNotContains(kernel, "pendingProgramDirty_",
                       "dead Program Change pending ledger must not remain after enqueue rejection");
    requireContains(kernel, "parentChunkAsyncDrainActive_",
                    "recursive chunks must disable raw async ring drains after parent normalization");
    requireContains(kernel, "chunkEvents.sort();",
                    "merged host/async chunk events must be globally sorted before dispatch side effects");
    requireContains(kernel, "if (runtimeExecutionOwner_)\n                runtimeExecutionOwner_->applyProjectedNormalizedParameter(kParamMasterVolume, norm);",
                    "CC11 master-volume projection must guard runtimeExecutionOwner_");


    // v913 final closure: Phase2 must obey the same runtime-owner null-guard
    // law as AU3 for teardown/partial-init/test harness paths.
    const std::string phase2Runtime = readFile(ARPSID_SOURCE_ROOT "/source/arpsid_processor_phase2.cpp");
    requireContains(phase2Runtime, "if (runtimeExecutionOwner_) runtimeExecutionOwner_->syncTempoLinkedControllers(numSamples);",
                    "Phase2 tempo-linked sync must guard runtimeExecutionOwner_");
    requireContains(phase2Runtime, "if (runtimeExecutionOwner_) runtimeExecutionOwner_->applyProjectedNormalizedParameter(kParamMasterVolume, norm);",
                    "Phase2 CC11 projection must guard runtimeExecutionOwner_");
    requireContains(phase2Runtime, "if (runtimeExecutionOwner_) runtimeExecutionOwner_->applyProjectedNormalizedParameter(static_cast<uint32_t>(ev.id), ev.value);",
                    "Phase2 timed param projection must guard runtimeExecutionOwner_");

    // AU3 exposes the promotion opt-in in the parameter tree.
    const std::string au3 = readFile(ARPSID_SOURCE_ROOT "/source/au3/ArpSIDAudioUnit.mm");
    requireContains(au3, "@(ArpSID::kParamAutoGmDrumPromotion)",
                    "AU3 parameter tree must expose Auto GM Drum Promotion");

    // Honest SID808 kit naming.
    const std::string kits = readFile(ARPSID_SOURCE_ROOT "/include/arpsid/patchbank/factory_sid808_kits.h");
    requireContains(kits, "SID-808 Punch Kit C",
                    "SID808 slots must carry family+variant display names");
}

void test_v910_identity() {
    const std::string version = readFile(ARPSID_SOURCE_ROOT "/VERSION.txt");
    require(isPreservedPass380ClosureLineageVersion(version),
            "VERSION.txt must identify the preserved pass380 closure lineage");
    const std::string status = readFile(ARPSID_SOURCE_ROOT "/STATUS.md");
    require(status.find("v918 final source closure complete") != std::string::npos ||
            status.find("v917 final closure completion") != std::string::npos ||
            status.find("v916 final remaining-issues closure") != std::string::npos ||
            status.find("v915 source cleanup closure") != std::string::npos,
            "STATUS.md must document the preserved cleanup/remaining-issues closure");
    const std::string notes918 = readFile(ARPSID_SOURCE_ROOT "/RELEASE_NOTES_V918.md");
    requireContains(notes918, "zip payload root directory", "v918 notes must document package-root drift closure");
    requireContains(notes918, "v916 source-tree name", "v918 notes must document obsolete root-name source");
    const std::string notes915 = readFile(ARPSID_SOURCE_ROOT "/RELEASE_NOTES_V915.md");
    requireContains(notes915, "stale disabled-gate comments", "v915 notes must document stale CMake cleanup");
    requireContains(notes915, "Source-side closure: COMPLETE", "v915 notes must document TODO release-guard marker preservation");
    const std::string cmakeText = readFile(ARPSID_SOURCE_ROOT "/CMakeLists.txt");
    requireNotContains(cmakeText, "disabled stale/dead closure gate", "CMake must not ship stale disabled release-gate comments");
    const std::string todoText = readFile(ARPSID_SOURCE_ROOT "/TODO.md");
    requireContains(todoText, "v915 source cleanup closure", "TODO must open with current v915 closure ledger");
    requireContains(todoText, "Source-side closure: COMPLETE", "TODO must preserve release-dead-file guard marker");
    const std::string notes914 = readFile(ARPSID_SOURCE_ROOT "/RELEASE_NOTES_V914.md");
    requireContains(notes914, "Program Change metadata", "v914 notes must document the dead MIDI metadata queue cleanup");
    requireContains(notes914, "v827.py` through `v830.py", "v914 notes must document removed superseded bootstrap probes");
    require(!std::filesystem::exists(std::filesystem::path(ARPSID_SOURCE_ROOT) / "tools/audit_logic_auv2_bootstrap_v827.py"),
            "superseded v827 Logic AUv2 bootstrap audit probe must not ship");
    require(!std::filesystem::exists(std::filesystem::path(ARPSID_SOURCE_ROOT) / "tools/audit_logic_auv2_bootstrap_v828.py"),
            "superseded v828 Logic AUv2 bootstrap audit probe must not ship");
    require(!std::filesystem::exists(std::filesystem::path(ARPSID_SOURCE_ROOT) / "tools/audit_logic_auv2_bootstrap_v829.py"),
            "superseded v829 Logic AUv2 bootstrap audit probe must not ship");
    require(!std::filesystem::exists(std::filesystem::path(ARPSID_SOURCE_ROOT) / "tools/audit_logic_auv2_bootstrap_v830.py"),
            "superseded v830 Logic AUv2 bootstrap audit probe must not ship");
    const std::string notes913 = readFile(ARPSID_SOURCE_ROOT "/RELEASE_NOTES_V913.md");
    requireContains(notes913, "Phase2", "v913 notes must document Phase2 owner hardening");
    const std::string notes = readFile(ARPSID_SOURCE_ROOT "/RELEASE_NOTES_V910.md");
    requireContains(notes, "midiQueue_", "v910 notes must document the ring-parity fix");
    requireContains(notes, "kMaxFramesPerBlock", "v910 notes must document chunk normalization");
    requireContains(notes, "stopped", "v910 notes must document the live-play law");
    const std::string sweep = readFile(ARPSID_SOURCE_ROOT "/scripts/run_timing_music_contract_sweep.sh");
    requireContains(sweep, "IngressParityTimingAuthorityV910Tests",
                    "timing/music sweep must run the v910 closure tests");
}

} // namespace

int main() {
    prewarmAllSidTables();
    test_chunked_queued_midi_lands_in_correct_chunk();
    test_stopped_transport_live_noteon_plays();
    test_noteon_noteoff_ring_events_parity();
    test_kernel_gm_promotion_matrix();
    test_sid808_selected_slot_override_resolves_kit_data();
    test_drsid_tom_default_alignment();
    test_source_contracts();
    test_v910_identity();
    std::cout << "ingress_parity_timing_authority_v910_tests PASS\n";
    return 0;
}
