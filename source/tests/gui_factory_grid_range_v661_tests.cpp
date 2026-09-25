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
    const std::string v = readFile(root + "/source/au3/ArpSIDViewController.mm");
    require(v.find("slot<ArpSID::kFactoryPatchSlotCount") != std::string::npos,
            "GUI bank grid iterates canonical factory slot count");
    require(v.find("for(int slot=0;slot<128;++slot)") == std::string::npos,
            "GUI bank grid is no longer hardcoded to 128 slots");
    require(v.find("rows=(ArpSID::kFactoryPatchSlotCount + cols - 1) / cols") != std::string::npos,
            "GUI bank grid rows derive from canonical count");
    std::cout << "GuiFactoryGridRangeV661Tests PASS\n";
    return 0;
}
