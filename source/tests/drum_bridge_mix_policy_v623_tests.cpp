// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

    require(kernel.find("componentFlavor_ == ArpSID::ComponentFlavor::Sid808") != std::string::npos,
            "SID808 render policy distinguishes Sid808 flavor");
    require(kernel.find("Sid808 flavor is a drum-authority flavor") != std::string::npos,
            "SID808 flavor replacement policy is documented");
    require(kernel.find("fail-open") != std::string::npos,
            "SID808 replacement policy documents the silent-scratch fallback");
    require(kernel.find("bridgeHasRenderableActivity") != std::string::npos,
            "SID808 replacement is guarded by renderable bridge activity");
    require(kernel.find("bridgePeak > 1.0e-7f") != std::string::npos,
            "SID808 replacement keeps real nonzero bridge buffers authoritative");
    require(kernel.find("activeVoiceCount() > 0u") != std::string::npos,
            "SID808 replacement keeps bridge tails authoritative");
    require(kernel.find("noteOnCount != sid808BridgeLastRenderedNoteOnCount_") != std::string::npos,
            "SID808 replacement keeps first-hit blocks authoritative");
    require(kernel.find("DrumMachine flavor can host other/canonical layers") != std::string::npos,
            "DrumMachine additive SID808 policy is documented");
    require(kernel.find("outputs[0][i] = std::clamp(outputs[0][i] + sliceScratchL_[i]") != std::string::npos,
            "DrumMachine SID808 bridge is additive, not whole-bus replacement");
    std::cout << "DrumBridgeMixPolicyV623Tests PASS\n";
    return 0;
}
