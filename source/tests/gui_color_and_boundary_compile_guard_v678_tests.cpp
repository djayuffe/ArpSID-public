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
    const std::string gui = readFile(std::string(ARPSID_SOURCE_DIR) + "/source/au3/ArpSIDViewController.mm");

    require(gui.find("c64LtRed()") == std::string::npos,
            "GUI must not call undefined c64LtRed helper");
    require(gui.find("static NSColor* c64Red()") != std::string::npos,
            "c64Red helper is defined");
    require(gui.find("case 5: return c64Red();") != std::string::npos,
            "DRSID bank group uses existing c64Red helper");
    require(gui.find("case 6: return c64Orange();") != std::string::npos,
            "SID808 bank group uses existing c64Orange helper");
    require(gui.find("case 7: return c64LtBlue();") != std::string::npos,
            "DIGI bank group uses existing c64LtBlue helper");

    require(gui.find("stickyBankNorm * 127.0f") == std::string::npos,
            "final GUI has no stickyBankNorm *127 bank decode");
    require(gui.find("slot < 128") == std::string::npos,
            "final GUI has no stale slot < 128 refresh loop");
    std::cout << "GuiColorAndBoundaryCompileGuardV678Tests PASS\n";
    return 0;
}
