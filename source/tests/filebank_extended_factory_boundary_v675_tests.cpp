// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string gui = readFile(root + "/source/au3/ArpSIDViewController.mm");
    const std::string fb = readFile(root + "/source/au3/ArpSIDFileBankBridge.mm");

    require(gui.find("stickyBankNorm * 127.0f") == std::string::npos, "GUI bank persistence has no stickyBankNorm *127 decode");
    require(gui.find("lround(stickyBankNorm * 127.0f)") == std::string::npos, "GUI bank persistence has no lround sticky *127");
    require(gui.find("canonicalFactorySlotFromNormalizedBankSlot(stickyBankNorm)") != std::string::npos,
            "GUI bank persistence decodes sticky bank via canonical helper");
    require(gui.find("factorySlot >= 0 && factorySlot < (NSInteger)ArpSID::kFileBankMaxSlots") != std::string::npos,
            "GUI bank export/load explicitly checks v1 user-bank boundary");
    require(gui.find("const NSInteger userBankSlot = (factorySlot >= 0 && factorySlot < (NSInteger)ArpSID::kFileBankMaxSlots) ? factorySlot : 0;") != std::string::npos,
            "GUI bank load uses explicit fallback for extended factory slot");

    require(fb.find("std::clamp(tel.bankSlot, 0, ArpSID::kFileBankMaxSlots - 1)") == std::string::npos,
            "FileBank bridge no longer aliases extended telemetry to slot 127");
    require(fb.find("slot >= 0 && slot < ArpSID::kFileBankMaxSlots") != std::string::npos,
            "FileBank bridge writes live overlay only inside v1 user-bank range");
    require(fb.find("Extended factory") != std::string::npos || fb.find("128..179") != std::string::npos,
            "FileBank bridge documents extended factory slots are not v1 bank slots");
    std::cout << "FileBankExtendedFactoryBoundaryV675Tests PASS\n";
    return 0;
}
