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

    requireContains(build, "--macos-closure", "build.sh must expose the one-command macOS closure mode");
    requireContains(build, "MACOS_CLOSURE=1", "build.sh must track macOS closure as its own mode");
    requireContains(build, "RELEASE_CHECK=1; RUN_TESTS=1; INSTALL_AUV2=1; CLEAR_AU_CACHE=1; VALIDATE_AUV2=1",
                    "macOS closure must combine release-check, full tests, install, cache refresh, and auval");
    requireContains(build, "--macos-closure is macOS-only",
                    "macOS closure must fail explicitly off macOS");
    requireContains(build, "release-check + full CTest + AUv2 install/cache refresh + strict auval",
                    "macOS closure must print an explicit execution summary");
    requireContains(build, "scripts/macos/verify_auv2_component.sh",
                    "macOS closure must still delegate validation to the canonical verifier");
    requireContains(build, "--target arpsid_auv2",
                    "macOS closure must build the AUv2 bundle through the install path");

    std::cout << "RootBuildScriptMacosClosureV772Tests PASS\n";
    return 0;
}
