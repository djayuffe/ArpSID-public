// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {

std::string readText(const char* relative) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + relative,
                         std::ios::binary);
    if (!stream) {
        std::cerr << "missing file: " << relative << '\n';
        std::exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void requireContains(const std::string& text, const char* needle, const char* message) {
    require(text.find(needle) != std::string::npos, message);
}

} // namespace

int main() {
    const std::string build = readText("build.sh");

    requireContains(build, "--validate-auv2", "build.sh must expose the auval validation mode");
    requireContains(build, "--strict-auval", "build.sh must expose the strict-auval alias");
    requireContains(build, "VALIDATE_AUV2=1", "validate mode must have a dedicated control flag");
    requireContains(build, "scripts/macos/verify_auv2_component.sh",
                    "validate mode must delegate to the canonical AUv2 verifier");
    requireContains(build, "--validate-auv2 is macOS-only",
                    "validate mode must fail clearly off macOS");
    requireContains(build, "ArpSID.component",
                    "validate mode must target the installed ArpSID.component");
    requireContains(build, "-DARPSID_BUILD_AUV2=ON",
                    "validate mode must configure with AUv2 enabled");

    std::cout << "RootBuildScriptAuvalModeV771Tests PASS\n";
    return 0;
}
