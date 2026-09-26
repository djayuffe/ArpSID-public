// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void require(bool ok, const std::string& msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    require(static_cast<bool>(f), "cannot open " + p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main() {
    const std::string root = ARPSID_SOURCE_DIR;
    const std::string cmake = readFile(root + "/CMakeLists.txt");
    const std::string guard = readFile(root + "/cmake/ArpSIDSourceTreeGuard.cmake");
    const std::string mac = readFile(root + "/scripts/macos_build_install_validate_auv2.sh");
    const std::string clean = readFile(root + "/scripts/cleanroom_unpack_build_auv2.sh");

    require(cmake.find("include(cmake/ArpSIDSourceTreeGuard.cmake)") != std::string::npos,
            "CMakeLists includes configure-time source-tree guard");
    require(cmake.find("arpsid_verify_source_tree") != std::string::npos,
            "CMakeLists has explicit source-tree guard target");

    for (const auto& bad : {"a.userSlotIndex", "mixByte(a.flags)", "mixByte(a.pad)", "vc.pulseWidth &", "vc.pulseWidth >>"})
        require(guard.find(bad) != std::string::npos, std::string("guard checks stale ref ") + bad);

    require(guard.find("message(FATAL_ERROR") != std::string::npos,
            "CMake guard fails configure on stale refs");
    require(mac.find("arpsid_verify_source_tree") != std::string::npos,
            "macOS script runs CMake guard target");
    require(mac.find("auval -strict -v aumu ArpS ASID") != std::string::npos,
            "macOS script validates canonical AUv2 id");
    require(mac.find("auval -v aumu ArpS ASID || true") == std::string::npos,
            "macOS script fails closed on auval failure");
    require(mac.find("Current source root") != std::string::npos,
            "macOS script prints source root");
    require(clean.find("Clean room") != std::string::npos,
            "cleanroom script exists");
    require(clean.find("verify_source_tree.py") != std::string::npos,
            "cleanroom script runs source guard");

    std::cout << "SourceTreePass49CMakeGuardV636Tests PASS\n";
    return 0;
}
