// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << "Cannot open " << path << "\n";
        std::exit(2);
    }
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static bool lineExistsUncommented(const std::string& text, const std::string& needle) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        const std::size_t comment = line.find("//");
        const std::size_t hit = line.find(needle);
        if (hit != std::string::npos && (comment == std::string::npos || hit < comment)) return true;
        pos = end + 1;
    }
    return false;
}

static void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string mm = readFile(root + "/source/au3/ArpSIDDSPKernelAdapter.mm");
    const std::string h  = readFile(root + "/source/au3/ArpSIDDSPKernelAdapter.h");

    require(lineExistsUncommented(mm, "std::mutex _digiPairMutex_v596_;"),
            "adapter must declare the DIGI pair mutex as code, not hidden inside a comment");
    require(lineExistsUncommented(mm, "DigiModelBankPair_v596 _digiPair_v596_;"),
            "adapter must declare the paired DIGI model/bank state as code, not hidden inside a comment");
    require(lineExistsUncommented(h, "#include \"arpsid/gui/digi_panel_model.h\""),
            "adapter header must include digi_panel_model.h, not leave the include commented out");

    require(mm.find("adapter shadow nor publish to the realtime kernel.     ArpSID::GUI::DigiPanelModel") == std::string::npos,
            "DIGI model member declaration must not be merged onto a comment line");
    require(mm.find("callers must use getDigiModel:sampleBank:.     *out =") == std::string::npos,
            "legacy DIGI split getters must not hide assignments inside comments");
    require(mm.find("setDigiModel:sampleBank:. }") == std::string::npos,
            "legacy DIGI split setters must not hide closing braces inside comments");
    require(mm.find("setDigiSampleBank:. }") == std::string::npos,
            "legacy DIGI sample-bank setter must not hide closing braces inside comments");

    require(mm.find("- (void)getDigiSampleBank:(ArpSID::GUI::DigiSampleBankBlob*)out") != std::string::npos,
            "getDigiSampleBank: implementation must remain visible");
    require(mm.find("- (void)setDigiSampleBank:(const ArpSID::GUI::DigiSampleBankBlob*)bank") != std::string::npos,
            "setDigiSampleBank: implementation must remain visible");
    require(mm.find("- (BOOL)setDigiUserSampleForSlot:(NSInteger)slot") != std::string::npos,
            "setDigiUserSampleForSlot implementation must remain visible");

    std::cout << "Auv2DigiAdapterCompileGuardV730Tests PASS\n";
    return 0;
}
