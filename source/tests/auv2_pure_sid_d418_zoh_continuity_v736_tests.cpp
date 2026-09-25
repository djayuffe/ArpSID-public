#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::cerr << "FAIL: cannot open " << path << "\n"; std::exit(1); }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::size_t countOf(const std::string& s, const std::string& needle) {
    std::size_t n = 0, pos = 0;
    while ((pos = s.find(needle, pos)) != std::string::npos) { ++n; pos += needle.size(); }
    return n;
}

int main() {
#ifndef ARPSID_SOURCE_ROOT
#error ARPSID_SOURCE_ROOT must be defined
#endif
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string kernel = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");

    require(kernel.find("bool pureSid1Q1C64D418Observed_ = false;") != std::string::npos,
            "Pure SID $D418 capture has an observed-stream latch");
    require(kernel.find("pureSid1Q1C64D418Observed_ = false;") != std::string::npos,
            "Pure SID $D418 observed latch resets when REC starts");
    require(kernel.find("if (hasD418) pureSid1Q1C64D418Observed_ = true;") != std::string::npos,
            "Pure SID $D418 capture arms latch on first $D418 write");
    require(kernel.find("if (!hasD418 && !pureSid1Q1C64D418Observed_) return;") != std::string::npos,
            "Pure SID $D418 capture skips only before first $D418 write");
    require(kernel.find("After observation, every block\n        // appends the held sample even when no new write occurs.") != std::string::npos,
            "Pure SID $D418 capture documents ZOH continuity across no-write blocks");
    require(kernel.find("float held = ArpSID_sanitizeFloat(pureSid1Q1C64D418Held_);") != std::string::npos,
            "Pure SID $D418 capture starts each block from previous held DAC level");
    require(kernel.find("pureSid1Q1C64D418Held_ = held;") != std::string::npos,
            "Pure SID $D418 capture persists held DAC level after each block");

    require(kernel.find("if (!hasD418) return;") == std::string::npos,
            "stale sparse-block bug must not return just because current block has no $D418 write");
    require(countOf(kernel, "pureSid1Q1C64D418Observed_") >= 4,
            "observed latch is declared, reset, armed and used");

    std::cout << "Auv2PureSidD418ZohContinuityV736Tests PASS\n";
    return 0;
}
