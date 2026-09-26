// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/sid_runtime_mod_ops.h"
#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/gui/settings_panel_model.h"
#include "parameter_ids.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

static std::string readFile(const char* relativePath) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + relativePath,
                     std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int main() {
    using namespace ArpSID;

    // Serialized patch state owns route topology. Live render parameters own
    // current knob/automation values. A stale state root must never overwrite
    // same-block automation while modulation is projected.
    SidStateRootV1 root{};
    sidSetStateRootParamValue(root, kParamFilterCutoff, 0.10f);
    sidSetStateRootParamValue(root, kParamMacro1, 0.10f);
    SidModRoute route{};
    route.source = SidModSource::Macro1;
    route.target = SidModTarget::FilterCutoff;
    route.depth = 0.25f;
    route.bipolar = true;
    root.patch.mod_routes.push_back(route);
    sanitizePersistentStateRootForSerialization(root);

    SidRuntimeModel runtime{};
    runtime.applyStateRoot(root);

    std::array<float, static_cast<size_t>(kNumParams)> live{};
    for (int i = 0; i < kNumParams; ++i)
        live[(size_t)i] = kParamInfos[(size_t)i].defaultNorm;
    live[(size_t)kParamFilterCutoff] = 0.60f;
    live[(size_t)kParamMacro1] = 0.90f;

    const float liveMacro =
        canonicalModSourceBipolar(runtime, live, SidModSource::Macro1);
    require(std::fabs(liveMacro - 0.80f) < 1.0e-6f,
            "macro modulation source reads live render parameter authority");

    const SidTypedModProjectionValues projected =
        computeTypedModProjectionValues(runtime, live);
    require(std::fabs(projected.cutoff - 0.80f) < 1.0e-6f,
            "typed modulation starts from live cutoff and applies live macro");

    const SidTypedModProjectionValues unmodulated = [&]() {
        SidRuntimeModel noRoutes{};
        SidStateRootV1 noRouteRoot = root;
        noRouteRoot.patch.mod_routes.clear();
        noRoutes.applyStateRoot(noRouteRoot);
        return computeTypedModProjectionValues(noRoutes, live);
    }();
    require(std::fabs(unmodulated.cutoff - 0.60f) < 1.0e-6f,
            "state-root cutoff mirror cannot overwrite live cutoff without routes");

    // The host bridge must reference the runtime-owned DrSID engine. Its
    // default SID808-only form owns no hidden DrSID instance.
    DrumEngineHostBridge sid808Only{};
    require(!sid808Only.hasCanonicalDrSidEngine(),
            "default drum bridge has no duplicate DrSID engine");

    DrSidEngine canonicalDrSid{};
    DrumEngineHostBridge boundBridge{canonicalDrSid};
    require(boundBridge.hasCanonicalDrSidEngine(),
            "bound bridge has canonical DrSID reference");
    require(boundBridge.canonicalDrSidEngine() == &canonicalDrSid,
            "bridge DrSID identity is pointer-identical to runtime authority");

    // Topology selection, note allocation, rendering, and panic must all point
    // at the same physical engine.
    BitPerfectEngine topology{};
    topology.setSampleRate(48000.0);
    topology.setSidChipTopologyMode(
        BitPerfectEngine::SidChipTopologyMode::SingleChip3Voice);
    topology.noteOn(60, 1.0f, 2, 77);
    require(topology.getActiveVoiceCount() == 1,
            "single-SID topology routes public noteOn into its rendered allocator");
    std::array<float, 64> singleL{};
    std::array<float, 64> singleR{};
    float* singleOut[2] = {singleL.data(), singleR.data()};
    topology.processBlock(singleOut, static_cast<int>(singleL.size()));
    bool singleAudible = false;
    for (float sample : singleL) singleAudible = singleAudible || std::fabs(sample) > 1.0e-7f;
    require(singleAudible, "single-SID public note authority is audibly rendered");
    topology.allNotesOff();
    require(topology.getActiveVoiceCount() == 0,
            "panic clears the active topology authority");

    GUI::SettingsPanelModel staleSettings = GUI::makeDefaultSettings();
    staleSettings.audioEngineMode = GUI::AudioEngineMode::DualSid6Voice;
    staleSettings.hostTempoSyncSource = GUI::HostTempoSyncSource::MidiClock;
    staleSettings.midiMappingPreset = GUI::MidiMappingPreset::Custom;
    staleSettings.drumEngineRouterOptIn = 0u;
    staleSettings.hostTempoSyncEnabled = 0u;
    const GUI::SettingsPanelModel canonicalSettings =
        GUI::sanitizeSettings(staleSettings);
    require(canonicalSettings.audioEngineMode == GUI::AudioEngineMode::BitPerfect,
            "unimplemented dual-SID metadata cannot become a fake render authority");
    require(canonicalSettings.hostTempoSyncSource == GUI::HostTempoSyncSource::HostTempo &&
            canonicalSettings.hostTempoSyncEnabled == 1u,
            "tempo settings resolve to the implemented host-tempo authority");
    require(canonicalSettings.midiMappingPreset == GUI::MidiMappingPreset::GM,
            "MIDI mapping resolves to the implemented GM authority");
    require(canonicalSettings.drumEngineRouterOptIn == 1u,
            "legacy router byte cannot disable flavor-owned routing");

    const std::string bridge =
        readFile("include/arpsid/engines/drum_engine_host_bridge.h");
    require(bridge.find("DrSidEngine        drsid_") == std::string::npos,
            "host bridge contains no owned DrSID member");
    require(bridge.find("bridgeDiagnosticDrsidEngine") == std::string::npos,
            "diagnostic duplicate-DrSID accessor is removed");

    const std::string processor =
        readFile("source/arpsid_processor_phase2.h");
    require(processor.find("enum class RenderMode") == std::string::npos,
            "VST processor has no second render-mode enum");
    require(processor.find("activeRenderMode_") == std::string::npos,
            "VST processor has no stale render-mode cache");

    const std::string kernel =
        readFile("source/au3/ArpSIDDSPKernel.hpp");
    const size_t createAt = kernel.find("createEngines_();");
    const size_t bridgePrepareAt =
        kernel.find("drumEngineBridge_.prepare(sampleRate_);", createAt);
    require(createAt != std::string::npos &&
            bridgePrepareAt != std::string::npos &&
            createAt < bridgePrepareAt,
            "AU kernel creates and binds canonical DrSID before bridge prepare");
    require(kernel.find("bindCanonicalDrSidEngine(*drs_())") != std::string::npos,
            "AU kernel binds bridge directly to engine-bank DrSID authority");
    require(kernel.find("useDrumEngineRouter_") == std::string::npos,
            "AU kernel has no second drum-routing enable authority");
    require(kernel.find("requestAudioEngineMode") != std::string::npos &&
            kernel.find("applyPendingAudioEngineMode_();") != std::string::npos,
            "audio topology crosses GUI/render boundary as one block-boundary request");

    const std::string adapter =
        readFile("source/au3/ArpSIDDSPKernelAdapter.mm");
    require(adapter.find("_kernel->requestAudioEngineMode(") != std::string::npos,
            "settings adapter publishes topology into the canonical kernel");

    const std::string cmake = readFile("CMakeLists.txt");
    require(cmake.find("retired legacy Mos6510 compatibility playback paths unreachable\" ON)") != std::string::npos,
            "production build keeps the retired legacy 6510 execution plane unreachable");

    const std::string c64 =
        readFile("include/arpsid/core/c64_psid_runtime.h");
    require(c64.find("ok = runRsidInitViaPhi2_(phi2Budget);") != std::string::npos &&
            c64.find("runPsidVbiPlayViaPhi2_") != std::string::npos,
            "PSID and RSID init/play use PHI2 execution without a compatibility fallback");
    require(c64.find("platform_.runPsidCiaPlaybackServiceTicks(maxTicks)") == std::string::npos &&
            c64.find("platform_.runPsidCiaPlaybackIrqTicks(maxTicks)") == std::string::npos,
            "PSID-CIA runtime service does not re-enter the retired platform service");

    std::cout << "SplitBrainAuthorityClosureV742Tests PASS\n";
    return 0;
}
