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
    const std::string pre = readFile(root + "/scripts/run_full_ctest_preflight.sh");
    const std::string mac = readFile(root + "/scripts/macos_full_build_install_clear_au_logic_cache.sh");
    const std::string auv2 = readFile(root + "/scripts/macos_build_install_validate_auv2.sh");
    const std::string build = readFile(root + "/build.sh");

    require(pre.rfind("#!/usr/bin/env bash", 0) == 0, "preflight script has correct shebang");
    require(pre.find("cmake --build") != std::string::npos, "preflight builds all default targets");
    require(pre.find("ctest --test-dir") != std::string::npos, "preflight runs ctest");
    require(pre.find("--output-on-failure") != std::string::npos, "preflight fails verbosely");
    require(pre.find("FactorySlotEncodingRoundtripV665Tests") != std::string::npos, "preflight includes extended slot sentinel");
    require(pre.find("FactoryNoLegacy127AliasV669Tests") != std::string::npos, "preflight includes no-alias sentinel");
    require(mac.find("run_full_ctest_preflight.sh") != std::string::npos,
            "macOS production entrypoint runs current full preflight");
    require(mac.find("macos_build_install_validate_auv2.sh") != std::string::npos,
            "macOS production entrypoint delegates to canonical AUv2 validator");
    require(auv2.find("auval -strict -v aumu ArpS ASID") != std::string::npos,
            "canonical macOS AUv2 validator runs strict ArpSID auval");
    require(auv2.find("auval -strict -v aumu ArIn ASID") != std::string::npos,
            "canonical macOS AUv2 validator runs strict ArpSID Input auval");
    require(auv2.find("auval -strict -v aumu DrSD ASID") != std::string::npos,
            "canonical macOS AUv2 validator runs strict DrSID auval");
    require(auv2.find("auval -strict -v aumu S808 ASID") != std::string::npos,
            "canonical macOS AUv2 validator runs strict SID808 auval");
    require(auv2.find("auval -strict -v aumu C64P ASID") != std::string::npos,
            "canonical macOS AUv2 validator runs strict C64 player auval");
    require(auv2.find("auval -v aumu ArpS ASID || true") == std::string::npos,
            "canonical macOS AUv2 validator must not ignore auval failure");
    require(build.find("--install-auv2") != std::string::npos,
            "root build.sh exposes AUv2 install path");
    require(build.find("-DARPSID_BUILD_AUV2=") != std::string::npos,
            "root build.sh exposes AUv2 bundle toggle during configure");
    require(build.find("--target arpsid_auv2") != std::string::npos,
            "root build.sh builds AUv2 bundle target before install");
    require(build.find("scripts/macos/install_auv2_component.sh") != std::string::npos,
            "root build.sh uses canonical AUv2 installer");
    require(build.find("ArpSID.component not found under ${BUILD_DIR} after building arpsid_auv2") != std::string::npos,
            "root build.sh reports missing bundle only after attempting AUv2 build");
    require(build.find("--clear-au-cache") != std::string::npos,
            "root build.sh exposes AU cache clear path");
    std::cout << "FullCTestPreflightScriptV670Tests PASS\n";
    return 0;
}
