// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* rel) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!in) { std::cerr << "missing file: " << rel << "\n"; std::exit(1); }
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string adapter = readFile("source/au3/ArpSIDDSPKernelAdapter.mm");
    const std::string audioUnit = readFile("source/au3/ArpSIDAudioUnit.mm");
    const std::string viewController = readFile("source/au3/ArpSIDViewController.mm");
    const std::string digiBank = readFile("include/arpsid/gui/digi_sample_bank_v596.h");
    const std::string cmake = readFile("CMakeLists.txt");

    require(kernel.find("GuiRealtimeModelSnapshot_ next = makeDefaultGuiRealtimeModelSnapshot_();")
                == std::string::npos,
            "publishGuiRealtimeModels must not allocate the large GUI snapshot on the caller stack");
    require(kernel.find("makeDefaultGuiRealtimeModelSnapshot_()") == std::string::npos,
            "kernel construction must not default the large GUI snapshot via a return-by-value helper");
    require(kernel.find("GuiRealtimeModelSnapshot_ guiRealtimeRender_{};") != std::string::npos,
            "kernel render snapshot must be object-owned storage, reset in place");
    require(kernel.find("GuiRealtimeModelSnapshot_& slot = guiRealtimeMailbox_.producerSlot();")
                != std::string::npos,
            "publishGuiRealtimeModels must fill the mailbox producer slot in place");
    require(kernel.find("resetGuiRealtimeModelSnapshot_(slot);") != std::string::npos,
            "publishGuiRealtimeModels must reset the producer slot in place before filling it");
    require(kernel.find("ArpSID::OwnershipMailbox<ArpSID::GUI::DigiSampleBankBlob> guiRealtimeDigiSampleBankMailbox_{};")
                != std::string::npos,
            "DigiSampleBankBlob must live in a separate realtime mailbox");
    require(kernel.find("GuiRealtimeModelSnapshot_ {\n        ArpSID::GUI::MixPanelModel mix;\n        ArpSID::GUI::KitStateBlob kit;\n        ArpSID::GUI::DigiPanelModel digi;\n        ArpSID::GUI::DigiSampleBankBlob")
                == std::string::npos,
            "large DIGI sample bank must not be embedded in the small GUI realtime model snapshot");
    require(adapter.find("ArpSID::GUI::DigiSampleBankBlob bank;") == std::string::npos,
            "adapter publication must not copy the 480 KB DIGI bank onto the caller stack");
    require(adapter.find("ArpSID::GUI::DigiSampleBankBlob b;") == std::string::npos,
            "adapter combined getter must not copy the 480 KB DIGI bank through a local stack blob");
    require(adapter.find("_digiPair_v596_.bank = ArpSID::GUI::makeDefaultDigiSampleBankBlob();")
                == std::string::npos,
            "adapter defaults must reset the owned DIGI bank in place");
    require(adapter.find("*out = ArpSID::GUI::makeDefaultDigiSampleBankBlob();")
                == std::string::npos,
            "adapter legacy DIGI getter must reset the caller bank in place");
    require(adapter.find("&_digiPair_v596_.bank") != std::string::npos,
            "adapter publication must pass the sanitized DIGI bank from owned storage");
    require(adapter.find("readTelemetry:out includeScopes:includeScopes includeC64Snapshot:YES")
                != std::string::npos,
            "adapter legacy telemetry calls must delegate to the explicit C64 snapshot selector");
    require(adapter.find("if (includeC64Snapshot) {\n        _kernel->noteC64TelemetryRequest();")
                != std::string::npos,
            "adapter light telemetry polls must not always demand heavy C64 snapshots");
    require(adapter.find("const auto t = _kernel->readTelemetry(includeScopes);")
                != std::string::npos,
            "adapter light telemetry polls must skip kernel-level scope copies");
    require(adapter.find("includeC64Snapshot && _kernel->readC64Telemetry(c64Snapshot)")
                != std::string::npos,
            "adapter must skip C64 chip snapshot reads for non-C64 light polls");
    require(audioUnit.find("ArpSID::GUI::DigiSampleBankBlob bank{}") == std::string::npos,
            "AU state restore/save must not allocate the 480 KB DIGI bank on the host stack");
    require(audioUnit.find("std::make_unique<ArpSID::GUI::DigiSampleBankBlob>()")
                != std::string::npos,
            "AU state restore/save must use heap storage for transient DIGI bank copies");
    require(viewController.find("ArpSID::GUI::DigiSampleBankBlob verifyBank{}")
                == std::string::npos,
            "editor DIGI round-trip verify must not allocate the 480 KB bank on the stack");
    require(digiBank.find("inline void resetDigiSampleBankBlob(DigiSampleBankBlob& b) noexcept")
                != std::string::npos,
            "DigiSampleBankBlob must have an in-place reset helper");
    require(digiBank.find("b = makeDefaultDigiSampleBankBlob();") == std::string::npos,
            "DigiSampleBankBlob sanitize/load paths must not default via a large stack temporary");
    require(cmake.find("DspKernelGuiRealtimePublishStackV843Tests") != std::string::npos,
            "v843 stack guard must be registered in CMake");

    std::cout << "DspKernelGuiRealtimePublishStackV843Tests PASS\n";
    return 0;
}
