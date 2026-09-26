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
    const std::string v520 = readFile(root + "/source/tests/drum_context_separation_v520_tests.cpp");
    const std::string v527 = readFile(root + "/source/tests/forensic_engine_sanity_v527_tests.cpp");

    require(v520.find("canonical DrSID 80..111 is authored") != std::string::npos,
            "V520 knows canonical DrSID 80..111 is authored");
    require(v520.find("canonical DrSID 80..111 is not legacy-only") != std::string::npos,
            "V520 separates legacy-only from canonical-authored DrSID");
    require(v520.find("canonical SID808 120..124 is not DrSID-authored") != std::string::npos,
            "V520 documents legacy DrSID 120..124 reclassified to canonical SID808");
    require(v520.find("canonical 120..124 maps to SID808 despite legacy DrSID compatibility") != std::string::npos,
            "V520 asserts canonical SID808 context for 120..124");

    require(v527.find("Digi Drum-role factory slot must not carry drSidMode=true") != std::string::npos,
            "V527 allows Digi Drum-role without drSidMode");
    require(v527.find("DrSID/SID808 Drum-role factory slot carries drSidMode=true") != std::string::npos,
            "V527 still requires DrSID/SID808 drum slots to carry drSidMode");
    require(v527.find("every Drum-role factory slot carries drSidMode=true") == std::string::npos,
            "V527 no longer uses obsolete Drum-role == DrSID invariant");

    std::cout << "LegacyDrumTestsContextAwareV690Tests PASS\n";
    return 0;
}
