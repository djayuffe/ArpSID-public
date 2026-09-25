#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "parameter_ids.h"
#include "arpsid/core/sid_runtime_model.h"

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::cerr << "missing file: " << path << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "SeqDrSidAuthorityV949Tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static void requireContains(const std::string& haystack, const std::string& needle, const char* msg) {
    if (haystack.find(needle) == std::string::npos) {
        std::cerr << "SeqDrSidAuthorityV949Tests missing " << msg << ":\n" << needle << "\n";
        std::exit(1);
    }
}

static std::array<float, ArpSID::kNumParams> params(float synth, float drsid, float arp, float seq) {
    std::array<float, ArpSID::kNumParams> p{};
    p[(size_t)ArpSID::kParamSynthModeEnable] = synth;
    p[(size_t)ArpSID::kParamDrSidEnable] = drsid;
    p[(size_t)ArpSID::kParamArpEnable] = arp;
    p[(size_t)ArpSID::kParamSeqEnable] = seq;
    return p;
}

int main() {
    auto classic = params(0.f, 0.f, 1.f, 1.f);
    auto synth = params(1.f, 0.f, 1.f, 1.f);
    auto drsid = params(0.f, 1.f, 1.f, 1.f);

    require(ArpSID::sidEffectiveArpAuthorityFromLiveParams(classic), "ARP effective in Classic/BitPerfect");
    require(!ArpSID::sidEffectiveArpAuthorityFromLiveParams(synth), "ARP blocked in SynthMode");
    require(!ArpSID::sidEffectiveArpAuthorityFromLiveParams(drsid), "ARP blocked in DrSID/SID808");

    require(ArpSID::sidEffectiveSeqAuthorityFromLiveParams(classic), "SEQ effective in Classic/BitPerfect");
    require(!ArpSID::sidEffectiveSeqAuthorityFromLiveParams(synth), "SEQ blocked in SynthMode");
    require(ArpSID::sidEffectiveSeqAuthorityFromLiveParams(drsid), "SEQ effective in DrSID/SID808 drum sequencer");

    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string model = readFile(root + "/include/arpsid/core/sid_runtime_model.h");
    const std::string kernel = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string phase2 = readFile(root + "/source/arpsid_processor_phase2.cpp");
    const std::string services = readFile(root + "/include/arpsid/core/sid_runtime_parameter_services.h");
    const std::string vc = readFile(root + "/source/au3/ArpSIDViewController.mm");

    requireContains(model,
        "return mode == SidRuntimeRenderMode::BitPerfect ||\n           mode == SidRuntimeRenderMode::DrSid;",
        "shared SEQ helper must allow BitPerfect and DrSID");
    requireContains(kernel,
        "const bool modeBlocksSeq = mode == ArpSID::SidRuntimeRenderMode::SidRegister;",
        "AU3 post-flush must only block SEQ in SynthMode/SID-register");
    requireContains(phase2,
        "const bool modeBlocksSeq = mode == ArpSID::SidRuntimeRenderMode::SidRegister;",
        "Phase2 post-canonical cleanup must only block SEQ in SynthMode/SID-register");
    requireContains(services,
        "enabling DrSID/SID808 must not force SeqEnable off",
        "DrSID special-mode activation must preserve sequencer authority");
    requireContains(kernel,
        "preserve SeqEnable for the SID808 drum sequencer",
        "SID808 flavor enforcement must preserve sequencer enable");
    requireContains(vc,
        "if(modeIndex == 0 || modeIndex == 2)",
        "GUI must present SEQ authority in Classic and DrSID/SID808 modes");

    std::cout << "SeqDrSidAuthorityV949Tests PASS\n";
    return 0;
}
