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
static std::string slice(const std::string& s, const std::string& a, const std::string& b) {
    const auto i=s.find(a); if(i==std::string::npos) return {};
    const auto j=s.find(b,i+a.size()); if(j==std::string::npos) return s.substr(i);
    return s.substr(i,j-i);
}
int main() {
    const std::string gui = readFile(std::string(ARPSID_SOURCE_DIR) + "/source/au3/ArpSIDViewController.mm");
    const std::string refresh = slice(gui, "-(void)_refreshBankSlotLabels", "-(void)_clearLoadedBankCache");
    require(refresh.find("slot < 128") == std::string::npos, "refresh labels no longer iterates only 128 slots");
    require(refresh.find("slot <= (NSInteger)ArpSID::kCanonicalFactoryPatchSlotMax") != std::string::npos,
            "refresh labels iterates canonical 0..179");
    require(refresh.find("loadedUserBankSlot") != std::string::npos,
            "refresh labels only uses loaded-bank names for v1 user-bank slots");
    require(refresh.find("_factoryDisplayNameForSlot:slot") != std::string::npos,
            "refresh labels uses factory names for extended slots");
    std::cout << "GuiBankLabelRefreshV676Tests PASS\n";
    return 0;
}
