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

    require(gui.find("_factoryBankSeg.segmentCount=8;") != std::string::npos,
            "factory bank segmented control exposes all 8 canonical groups");
    require(gui.find("for(NSInteger group=0; group<8; ++group)") != std::string::npos,
            "factory bank segmented control labels all 8 groups");
    require(gui.find("full 180-slot factory grid") != std::string::npos,
            "factory bank segmented control tooltip says 180-slot grid");
    require(gui.find("std::clamp(seg.selectedSegment, (NSInteger)0, (NSInteger)7)") != std::string::npos ||
            gui.find("std::clamp(seg.selectedSegment,(NSInteger)0,(NSInteger)7)") != std::string::npos,
            "factory bank click handler allows group 7 DIGI");
    require(gui.find("ArpSIDFactoryBankGroupTooltip(7)") != std::string::npos,
            "factory bank segmented tooltips include DIGI group");

    const std::string label = slice(gui, "static inline NSString* ArpSIDFactoryBankGroupLabel", "static inline NSColor* ArpSIDFactoryBankGroupColor");
    require(label.find("PADS") == std::string::npos,
            "PADS group removed from canonical factory selector to avoid DRSID overlap");
    require(label.find("case 5: return @\"DRSID\";") != std::string::npos,
            "group 5 is DRSID");
    require(label.find("case 6: return @\"SID808\";") != std::string::npos,
            "group 6 is SID808");
    require(label.find("case 7: return @\"DIGI\";") != std::string::npos,
            "group 7 is DIGI");

    const std::string range = slice(gui, "static inline NSRange ArpSIDFactoryBankGroupRange", "static inline NSString* ArpSIDFactoryBankGroupLabel");
    require(range.find("case 5: return NSMakeRange(80, 40);") != std::string::npos,
            "DRSID range is 80..119");
    require(range.find("case 6: return NSMakeRange(120, 30);") != std::string::npos,
            "SID808 range is 120..149");
    require(range.find("case 7: return NSMakeRange(150, 30);") != std::string::npos,
            "DIGI range is 150..179");
    require(range.find("NSMakeRange(80, 16)") == std::string::npos,
            "no PADS 80..95 overlap remains");

    const std::string groupForSlot = slice(gui, "static inline NSInteger ArpSIDFactoryBankGroupForSlot", "static inline NSRange ArpSIDFactoryBankGroupRange");
    require(groupForSlot.find("slot >= 80 && slot <= 119) return 5") != std::string::npos,
            "slot classifier maps DRSID 80..119 to group 5");
    require(groupForSlot.find("slot >= 120 && slot <= 149) return 6") != std::string::npos,
            "slot classifier maps SID808 120..149 to group 6");
    require(groupForSlot.find("slot >= 150 && slot <= 179) return 7") != std::string::npos,
            "slot classifier maps DIGI 150..179 to group 7");

    std::cout << "GuiBankSegmentCanonicalLayoutV680Tests PASS\n";
    return 0;
}
