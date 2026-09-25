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
    const std::string v238a = readFile(root + "/source/tests/factory_drsid_kit_bank_sweep_v238_tests.cpp");
    const std::string v238b = readFile(root + "/source/tests/logic_stop_start_drsid_kit_v238_tests.cpp");
    const std::string v240  = readFile(root + "/source/tests/state_apply_reason_policy_v240_tests.cpp");

    require(v238a.find(std::string("canonicalNormalizedProgramValue(") + "(uint8_t)") == std::string::npos,
            "FactoryDrsidKitBankSweepV238 no longer expects 7-bit Program");
    require(v238b.find(std::string("canonicalNormalizedProgramValue(") + "(uint8_t)") == std::string::npos,
            "LogicStopStartDrsidKitV238 no longer expects 7-bit Program");
    require(v240.find(std::string("canonicalNormalizedProgramValue(") + "(uint8_t)") == std::string::npos,
            "StateApplyReasonPolicyV240 no longer expects 7-bit Program");

    require(v238a.find("canonicalNormalizedFactoryProgramValue(slot)") != std::string::npos,
            "FactoryDrsidKitBankSweepV238 expects factory Program mirror");
    require(v238b.find("canonicalNormalizedFactoryProgramValue(selectedSlot)") != std::string::npos,
            "LogicStopStartDrsidKitV238 expects factory Program mirror");
    require(v240.find("canonicalNormalizedFactoryProgramValue(slot)") != std::string::npos,
            "StateApplyReasonPolicyV240 expects factory Program mirror");

    std::cout << "LegacyProgramExpectationClosedV679Tests PASS\n";
    return 0;
}
