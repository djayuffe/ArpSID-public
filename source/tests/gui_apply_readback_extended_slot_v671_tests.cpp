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
    const std::string v = readFile(std::string(ARPSID_SOURCE_DIR) + "/source/au3/ArpSIDViewController.mm");

    const std::string apply = slice(v, "-(void)_applyPatchSelectionNumber:", "-(void)_deferPatchSelectionNumber:");
    require(apply.find("tag > 127") == std::string::npos, "_applyPatchSelectionNumber no longer rejects extended slots");
    require(apply.find("ArpSID::kCanonicalFactoryPatchSlotMax") != std::string::npos, "_applyPatchSelectionNumber uses canonical max");

    const std::string defer = slice(v, "-(void)_deferPatchSelectionNumber:", "-(void)_updatePreset");
    require(defer.find("tag > 127") == std::string::npos, "_deferPatchSelectionNumber no longer rejects extended slots");
    require(defer.find("ArpSID::kCanonicalFactoryPatchSlotMax") != std::string::npos, "_deferPatchSelectionNumber uses canonical max");

    require(v.find("canonicalFactorySlotFromNormalizedBankSlot(bankNorm)") != std::string::npos,
            "GUI bankNorm readback uses 180-slot decoder");
    require(v.find("[_au getParameterValue:ArpSID::kParamBankSlot]*127.f") == std::string::npos,
            "GUI Load Factory no longer decodes current bank slot with *127");
    require(v.find("ArpSIDUIClampInt(tel.bankSlot,0,ArpSID::kCanonicalFactoryPatchSlotMax,0)") != std::string::npos,
            "Drum HUD patch definition lookup clamps to canonical max");
    require(v.find("for (NSInteger slot = 120; slot <= 149; ++slot)") != std::string::npos,
            "GUI SID808 priority covers 120..149");

    std::cout << "GuiApplyReadbackExtendedSlotV671Tests PASS\n";
    return 0;
}
