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
    const std::string params = readFile(std::string(ARPSID_SOURCE_DIR) + "/source/parameter_ids.h");

    require(gui.find("Range: slots 1 .. 128") == std::string::npos,
            "GUI param tooltip no longer says factory slot range is 1..128");
    require(gui.find("Range: factory slots 1 .. 180") != std::string::npos,
            "GUI bank-slot tooltip says 1..180");
    require(gui.find("Range: factory identity mirror slots 1 .. 180") != std::string::npos,
            "GUI program tooltip documents factory identity mirror");
    require(gui.find("Bank JSON contains more than 128 user-bank v1 slots") != std::string::npos,
            "bank JSON error text distinguishes v1 user-bank slots");
    require(gui.find("outside user-bank v1 range 0..127") != std::string::npos,
            "slot range error text distinguishes user-bank v1 range");
    require(gui.find("SID808 bank. Slots 120..149") != std::string::npos,
            "SID808 tooltip is explicit");
    require(gui.find("DIGI bank. Slots 150..179") != std::string::npos,
            "DIGI tooltip is explicit");
    require(gui.find("DRSID bank. Slots 80..119") != std::string::npos,
            "DRSID tooltip is explicit");
    require(params.find("Discrete program index 0..127") == std::string::npos,
            "stale parameter comment no longer says Program is only 0..127");
    std::cout << "BankUITextAndTooltipV677Tests PASS\n";
    return 0;
}
