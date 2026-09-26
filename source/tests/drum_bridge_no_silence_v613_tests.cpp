// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string readFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string kernel = readFile((root + "/source/au3/ArpSIDDSPKernel.hpp").c_str());
    const std::string bridge = readFile((root + "/include/arpsid/engines/drum_engine_host_bridge.h").c_str());

    require(contains(kernel, "DrSID/DrumMachine audio is owned by canonical engineBank_.drSid"),
            "renderDrumBridgeIfActive_ documents canonical DrSID ownership");
    require(contains(kernel, "context None and DrSID_C64Wavetable both preserve canonical audio"),
            "DrSID bridge context preserves canonical output instead of replacing it");
    require(contains(kernel, "SID808 replacement render only"),
            "bridge replacement is restricted to SID808");
    require(contains(kernel, "configureDefaultDrumBridgeIdentityNonRealtime_();"),
            "default drum bridge identity is configured on non-RT setup/sample-rate/flavor paths");
    require(contains(kernel, "componentFlavor_ == ArpSID::ComponentFlavor::Sid808"),
            "bridge GM note path is SID808-only");
    require(contains(bridge, "bool hasActiveRenderableContext() const noexcept"),
            "bridge exposes renderable-context check");
    require(contains(bridge, "void activateDefaultIdentityForContext(DrumContext ctx) noexcept"),
            "bridge exposes RT-safe identity-only repair without loadFactorySlot");


    require(contains(kernel, "drumEngineBridge_.processBlock(sliceScratchL_, sliceScratchR_, numFrames);"),
            "bridge renders into scratch before replacing canonical output");
    require(contains(kernel, "const bool bridgeHasRenderableActivity"),
            "SID808 bridge replacement is guarded by renderable activity");
    require(contains(kernel, "bridgePeak > 1.0e-7f"),
            "SID808 bridge peak participates in output ownership");
    require(contains(kernel, "activeVoiceCount() > 0u"),
            "SID808 tails keep bridge output ownership");
    require(contains(kernel, "noteOnCount()") &&
            contains(kernel, "sid808BridgeLastRenderedNoteOnCount_"),
            "SID808 first-hit blocks keep bridge output ownership even before peak appears");
    require(contains(kernel, "if (bridgeHasRenderableActivity)") &&
            contains(kernel, "std::memcpy(outputs[0], sliceScratchL_"),
            "SID808 bridge replaces output only when it has renderable activity");
    require(contains(kernel, "fail-open"),
            "silent SID808 bridge scratch must fail open to canonical fallback audio");


    // v859 contract: Sid808 flavor dispatches ONLY to the bridge once its
    // SID-808 identity is active (the bridge replaces the whole bus, so a
    // parallel canonical trigger was a literal double render thrown away every
    // block). The canonical engine remains the FAIL-OPEN path so notes can
    // never fall silent when the bridge identity cannot be established.
    require(contains(kernel, "return; // bridge owns the audio — no canonical double render"),
            "Sid808 bridge dispatch returns before the canonical trigger (no double render)");
    require(contains(kernel, "fail-open"),
            "canonical DrSID documented as the fail-open authority");
    require(contains(kernel, "if (drs_()) drs_()->triggerMidiNote(note, velocity);"),
            "canonical DrSID trigger remains as the fail-open/other-flavor path");
    require(contains(kernel, "if (drs_()) drs_()->noteOffMidi(note);"),
            "canonical DrSID release remains as the fail-open/other-flavor path");
    require(contains(kernel, "sid808IdentityRepairSlot_"),
            "SID808 identity repair is slot-aware (loaded kit, not hard-coded 120)");
    require(!contains(kernel, "loadFactorySlot(47)"),
            "DrSID factory slot is not auto-loaded into bridge production path");
    require(contains(kernel, "if (ctx != ArpSID::DrumContext::SID808_AnalogProjection)"),
            "SID808 default identity does not clobber an existing SID808 kit context");


    require(contains(kernel, "ctx != ArpSID::DrumContext::SID808_AnalogProjection"),
            "render bridge returns unless active context is SID808");
    require(contains(kernel, "DrSID bridge-owned engine is never used"),
            "production render does not use bridge-owned DrSID as audio authority");
    require(contains(kernel, "Deliberately do not load DrSID factory slots into the bridge"),
            "state/preset path blocks DrSID bridge loading");


    require(!contains(kernel, "drumEngineBridge_.drsidEngine()"),
            "kernel does not project or render through bridge-owned DrSID engine");


    require(!contains(kernel, "drumEngineBridge_.drsidEngine()"),
            "kernel does not project or render through bridge-owned DrSID engine");
    require(!contains(kernel, "loadFactorySlot(47)"),
            "kernel does not auto-load DrSID factory slot into bridge");
    require(!contains(kernel, "ArpSID::isDrSidFactorySlot(slotInt)"),
            "sticky-slot bridge load is not DrSID-capable");

    std::cout << "drum bridge no-silence routing regression passed\n";
    return 0;
}
