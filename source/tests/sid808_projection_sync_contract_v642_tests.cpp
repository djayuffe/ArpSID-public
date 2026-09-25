#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string k = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string name = "void syncDrumBridgeProjectionFromRenderParams_() noexcept";
    const auto start = k.find(name);
    require(start != std::string::npos, "sync function exists");
    const auto end = k.find("    void setupNonRealtime", start);
    require(end != std::string::npos, "sync function end found");
    const std::string body = k.substr(start, end - start);

    require(body.find("setSidModel") != std::string::npos, "sync projects SID model");
    require(body.find("setClockFrequency") != std::string::npos, "sync projects clock");
    require(body.find("setForensicConfig") != std::string::npos, "sync projects forensic config");

    require(body.find("loadFactorySlot") == std::string::npos, "sync does not reload global factory slot");
    require(body.find("setActiveIdentityFromFactorySlot") == std::string::npos, "sync does not reset active identity");
    require(body.find("activateDefaultIdentityForContext") == std::string::npos, "sync does not activate default identity");
    require(body.find("noteOn") == std::string::npos, "sync does not alter scheduled/per-hit note events");
    require(body.find("allNotesOff") == std::string::npos, "sync does not clear scheduled/per-hit notes");

    std::cout << "Sid808ProjectionSyncContractV642Tests PASS\n";
    return 0;
}
