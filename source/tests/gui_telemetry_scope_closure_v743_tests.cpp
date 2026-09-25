#include "arpsid/core/c64_telemetry.h"
#include "arpsid/gui/tab_architecture.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

static std::string slurp(const char* relative) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + relative, std::ios::binary);
    require(static_cast<bool>(in), relative);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

int main() {
    using namespace ArpSID::C64;
    using namespace ArpSID::GUI;

    require(kTabCount == 17u, "shared 17-tab production inventory");
    require(!isVisibleProductionTab(ArpSIDTab::LegacySidProjection), "legacy tab hidden");
    require(!isVisibleProductionTab(ArpSIDTab::C64State), "C64 STATE diagnostics tab hidden");

    C64Platform platform;
    platform.reset(true);
    platform.cia1().write(0x04u, 0x34u);
    platform.cia1().write(0x05u, 0x12u);
    platform.cia1().write(0x0Du, 0x81u);
    platform.cia1().write(0x0Eu, 0x11u);
    platform.cia1().step(3u);
    platform.cia2().write(0x06u, 0x78u);
    platform.cia2().write(0x07u, 0x56u);
    platform.cpuWrite(0x2000u, 0xA5u);
    platform.runCycles(2048u);
    (void)platform.cpuRead(0xD800u);
    (void)platform.cpuRead(0xD419u);

    const auto snapshot = c64BuildTelemetrySnapshot(platform, true, 7u, 1u, 50.0f);
    require(snapshot.cia1Phase.latchA == 0x1234u, "CIA1 timer-A latch published");
    require(snapshot.cia1Phase.timerA <= 0x1234u, "CIA1 live timer published");
    require(snapshot.cia1IrqMask == 0x01u, "CIA1 ICR mask published");
    require(snapshot.cia2Phase.latchB == 0x5678u, "CIA2 timer-B latch published");
    require(snapshot.vicMemoryBank == platform.vic().memoryBank(), "VIC bank published");
    require(snapshot.vicFetchBase == platform.vic().fetchBase(), "VIC fetch base published");
    require(snapshot.openBusDecayMask == platform.openBusDecayMask(), "open-bus decay mask reaches telemetry snapshot");
    require(snapshot.openBusAgePhi2 == platform.openBusAgePhi2(), "open-bus age reaches telemetry snapshot");
    require(snapshot.openBusLastDrivenPhi2 == platform.openBusLastDrivenPhi2(), "open-bus last-driven cycle reaches telemetry snapshot");
    require(snapshot.openBusDrivenWithinPersistence == platform.openBusDrivenWithinPersistence(), "open-bus hold state reaches telemetry snapshot");
    require(snapshot.colorRamOpenBusReadCount == platform.colorRamHighNibbleOpenBusReadCount(), "Color RAM open-bus read count reaches telemetry snapshot");
    require(snapshot.sidOpenBusReadCount == platform.sidNoSinkOpenBusReadCount(), "SID no-sink open-bus read count reaches telemetry snapshot");
    require(snapshot.potxyOpenBusReadCount == platform.sidNoSinkPotxyReadCount(), "POTX/POTY no-sink read count reaches telemetry snapshot");

    const std::string vc = slurp("source/au3/ArpSIDViewController.mm");
    const std::string kernel = slurp("source/au3/ArpSIDDSPKernel.hpp");
    const std::string adapter = slurp("source/au3/ArpSIDDSPKernelAdapter.mm");
    const std::string telemetryHeader = slurp("source/common/arpsid_telemetry_snapshot.h");
    const std::string vstBridge = slurp("source/gui/arpsid_vst_cocoa_bridge.mm");
    const std::string vstEditor = slurp("source/gui/arpsid_vstgui_editor.h");
    const std::string cmake = slurp("CMakeLists.txt");

    require(vc.find("typedef NS_ENUM(NSInteger,ArpSIDTab)") == std::string::npos,
            "no controller-local tab enum");
    require(vc.find("_settingsSyncSourcePopup_v551_") == std::string::npos,
            "dead one-item sync popup removed");
    require(vc.find("_settingsMidiMappingPopup_v551_") == std::string::npos,
            "dead one-item MIDI popup removed");
    require(vc.find("_settingsRouterToggle_v551_") == std::string::npos,
            "dead disabled routing checkbox removed");
    require(vc.find("_applyThemeButtonPressed_v552_") == std::string::npos,
            "redundant reapply-theme button removed");
    require(vc.find("ArpSIDFinalizeControlUX(root);") != std::string::npos,
            "all constructed controls receive accessibility/tool-tip finalization");
    require(vc.find("_c64PlayerInfoLabels") != std::string::npos,
            "duplicate C64 status labels use a multicast collection");
    require(vc.find("readVCOScope:presentationVcoScope") == std::string::npos,
            "VCO snapshot is not fetched twice per frame");
    require(vc.find("MIN((NSInteger)ArpSIDTabDigiV563") != std::string::npos,
            "notification navigation reaches the full tab ID range");
    require(vc.find("v1.5.2") == std::string::npos &&
            vc.find("ARPSID_PLUGIN_VERSION") != std::string::npos,
            "GUI version label consumes canonical build metadata");
    require(vstEditor.find("public Steinberg::Vst::VSTGUIEditor") == std::string::npos &&
            vstEditor.find("Steinberg::Vst::EditorView") != std::string::npos,
            "VST3 hosts the canonical Cocoa GUI without a duplicate fallback UI");
    require(cmake.find("source/au3/ArpSIDFileBankBridge.mm") != std::string::npos &&
            cmake.find("source/arpsid_file_bank.cpp") != std::string::npos,
            "all native wrappers link the file-bank implementation used by the GUI");

    require(adapter.find("if (includeScopes) {\n        const uint32_t wp = t.c64BusScopeWritePos") != std::string::npos,
            "C64 scope copies are demand-gated and chronological");
    require(adapter.find("voiceRaw[v][(wp + static_cast<uint32_t>(i)) & 255u]") != std::string::npos,
            "presentation voice scopes are chronological");
    require(telemetryHeader.find("c64CiaTimerA[2]") != std::string::npos,
            "CIA timer telemetry reaches GUI ABI");
    require(telemetryHeader.find("psidCiaTicksToPlay") != std::string::npos,
            "PSID-CIA lifecycle telemetry reaches GUI ABI");
    require(telemetryHeader.find("c64VicActiveSpriteMask") != std::string::npos,
            "VIC sprite DMA telemetry reaches GUI ABI");
    require(telemetryHeader.find("c64OpenBusDecayMask") != std::string::npos &&
            telemetryHeader.find("c64OpenBusAgePhi2") != std::string::npos &&
            telemetryHeader.find("c64SidOpenBusReadCount") != std::string::npos,
            "open-bus age/decay/read counters reach GUI ABI");
    require(adapter.find("out->c64OpenBusDecayMask = c64Snapshot.openBusDecayMask") != std::string::npos &&
            adapter.find("out->c64PotxyOpenBusReadCount = c64Snapshot.potxyOpenBusReadCount") != std::string::npos,
            "AU adapter copies open-bus detail from C64 snapshot");
    require(kernel.find("telemetryC64OpenBusDecayMask_") != std::string::npos &&
            kernel.find("t.c64OpenBusDecayMask = telemetryC64OpenBusDecayMask_.load") != std::string::npos &&
            kernel.find("telemetryC64SidOpenBusReadCount_.store") != std::string::npos,
            "light C64 telemetry publishes real open-bus detail every render block");
    require(kernel.find("openDecayGain") != std::string::npos &&
            kernel.find("openMix") == std::string::npos,
            "open-bus scope plots the decayed latch value instead of synthetic XOR eye candy");
    require(adapter.find("out->c64OpenBusDecayMask = t.c64OpenBusDecayMask") != std::string::npos &&
            adapter.find("out->c64PotxyOpenBusReadCount = t.c64PotxyOpenBusReadCount") != std::string::npos,
            "AU adapter fallback copies real open-bus detail from light telemetry");
    require(adapter.find("out->c64OpenBusDecayMask = 0xFFu;") == std::string::npos &&
            adapter.find("out->c64SidOpenBusReadCount = 0u;") == std::string::npos,
            "AU adapter must not fake open-bus hold/counter fallback values");
    require(vc.find("PHI2 6510 MICROCORE") != std::string::npos &&
            vc.find("OPENBUS READS  SID") != std::string::npos &&
            vc.find("obs S/C/P") != std::string::npos,
            "GUI visualizes PHI2 CPU and expanded open-bus diagnostics");
    require(vstBridge.find("arpGetLatestFullTelemetry") != std::string::npos,
            "VST Cocoa bridge consumes canonical full telemetry");

    std::cout << "GuiTelemetryScopeClosureV743Tests PASS\n";
    return 0;
}
