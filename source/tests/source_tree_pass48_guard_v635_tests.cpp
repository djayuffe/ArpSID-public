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
    const std::string kernel = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");

    const std::vector<std::string> bad = {
        "a.userSlotIndex",
        "mixByte(a.flags)",
        "mixByte(a.pad)",
        "vc.pulseWidth &",
        "vc.pulseWidth >>",
    };
    for (const auto& s : bad)
        require(kernel.find(s) == std::string::npos, "stale kernel ref present: " + s);

    const std::vector<std::string> good = {
        "a.factorySlotIndex",
        "a.reserved[0]",
        "a.reserved[1]",
        "a.reserved[2]",
        "vc.pulseWidthLo",
        "vc.pulseWidthHi",
    };
    for (const auto& s : good)
        require(kernel.find(s) != std::string::npos, "correct kernel ref missing: " + s);

    const std::string script = readFile(root + "/scripts/macos_build_install_validate_auv2.sh");
    const std::string wrapper = readFile(root + "/scripts/install_auv2_component.sh");
    require(script.find("verify_source_tree.py") != std::string::npos,
            "macOS AUv2 script runs source-tree guard before build");
    require(script.find("refusing install") != std::string::npos,
            "macOS AUv2 script refuses install after failed build");
    require(script.find("scripts/macos/install_auv2_component.sh") != std::string::npos ||
                script.find("macos/install_auv2_component.sh") != std::string::npos,
            "macOS AUv2 script uses canonical installer");
    require(script.find("auval -v aumu ArpS ASID || true") == std::string::npos,
            "macOS AUv2 script must not ignore primary auval failure");
    require(script.find("auval -strict -v aumu ArpS ASID") != std::string::npos,
            "macOS AUv2 script validates primary ArpSID AU id");
    require(script.find("auval -strict -v aumu ArIn ASID") != std::string::npos,
            "macOS AUv2 script validates input flavor AU id");
    require(script.find("auval -strict -v aumu DrSD ASID") != std::string::npos,
            "macOS AUv2 script validates DrSID AU id");
    require(script.find("auval -strict -v aumu S808 ASID") != std::string::npos,
            "macOS AUv2 script validates SID808 AU id");
    require(script.find("aufx") == std::string::npos && script.find("UlfB") == std::string::npos,
            "macOS AUv2 script does not advertise stale effect/manufacturer id");
    require(wrapper.find("macos/install_auv2_component.sh") != std::string::npos,
            "root install_auv2_component wrapper delegates to canonical macOS installer");

    std::cout << "SourceTreePass48GuardV635Tests PASS\n";
    return 0;
}
