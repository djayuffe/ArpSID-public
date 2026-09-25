#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); }
}
static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string pre = readFile(root + "/scripts/run_full_ctest_preflight.sh");
    const std::string mac = readFile(root + "/scripts/macos_full_build_install_clear_au_logic_cache.sh");
    const std::string cmake = readFile(root + "/CMakeLists.txt");
    const std::string audit = readFile(root + "/AUDIT_PROGRESSION.md");

    require(pre.find("parallel build failed; retrying serial build") != std::string::npos,
            "pass83 preflight has serial retry for readable compiler errors");
    require(pre.find("VoicePolicyAllNotesOffChannelBoundsV696Tests") != std::string::npos,
            "pass83 preflight closure sentinel includes V696");
    require(mac.find("run_full_ctest_preflight.sh") != std::string::npos,
            "macOS production script calls pass83 preflight");
    require(cmake.find("full 128-slot factory bank") == std::string::npos,
            "CMake comments no longer describe factory as full 128-slot");
    require(cmake.find("canonical 180-slot factory bank") != std::string::npos,
            "CMake comments describe canonical 180-slot factory bank");
    require(audit.find("128-slot factory bank sweep.") == std::string::npos,
            "audit progression no longer has unqualified stale 128-slot factory wording");

    std::cout << "FullPreflightPass83AndStale128V697Tests PASS\n";
    return 0;
}
