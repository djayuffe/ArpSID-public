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
    const std::string mix = readFile(root + "/source/tests/mix_panel_tab_v548_tests.cpp");
    const std::string sid = readFile(root + "/source/tests/sidcore_feed_v550_tests.cpp");

    require(mix.find("arrayContains16") == std::string::npos,
            "V548 unused helper removed");
    require(mix.find("bool anySolo") == std::string::npos,
            "V548 no set-but-unused anySolo");
    require(mix.find("for (auto p : slot.params) assert") == std::string::npos,
            "V548 params loop not assert-only");
    require(mix.find("int diff =") == std::string::npos || mix.find("requireV548(diff") != std::string::npos,
            "V548 diff is used outside assert");
    require(mix.find("requireV548(m.master.masterVolume") != std::string::npos,
            "V548 default model checks are runtime require calls and warning-clean");

    require(sid.find("if (n != 5u) std::abort();") != std::string::npos,
            "V550 ring drain count used outside assert");
    require(sid.find("if (n != 16u) std::abort();") != std::string::npos,
            "V550 overflow drain count used outside assert");
    require(sid.find("if (n != 3u) std::abort();") != std::string::npos,
            "V550 model drain count used outside assert");
    require(sid.find("if (n != 0u) std::abort();") != std::string::npos,
            "V550 empty drain count used outside assert");

    std::cout << "WarningCleanTestContractV681Tests PASS\n";
    return 0;
}
