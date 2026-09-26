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

void requireNotContains(const std::string& text, const char* needle, const char* message) {
    require(text.find(needle) == std::string::npos, message);
}

} // namespace


static std::string extractCurrentPass(const std::string& version) {
    const std::string prefix = "0.0.690-pass";
    const std::size_t passPos = version.find(prefix);
    require(passPos != std::string::npos, "VERSION.txt must carry a 0.0.690-passNNN identity");
    std::size_t digits = passPos + prefix.size();
    std::size_t end = digits;
    while (end < version.size() && version[end] >= '0' && version[end] <= '9') ++end;
    require(end > digits, "VERSION.txt pass identity must include digits");
    return version.substr(digits, end - digits);
}

int main() {
    const std::string build = readText("build.sh");
    const std::string version = readText("VERSION.txt");
    const std::string pass = extractCurrentPass(version);
    const std::string header = readText("include/arpsid/version.h");
    const std::string readme = readText("README.md");
    const std::string auvalStatus = readText("RELEASE_AUVAL_COMMAND_STATUS.md");
    const std::string macosClosure = readText("RELEASE_MACOS_CLOSURE_COMMAND.md");
    const std::string handoff = readText("arpsid-fix-list-690.md");

    requireContains(version, ("0.0.690-pass" + pass).c_str(),
                    "VERSION.txt must carry current pass identity");
    requireContains(header, ("#define ARPSID_BUILD_PASS " + pass).c_str(),
                    "version.h build pass must match current pass");
    require(readme.rfind("# ArpSID 0.0.690 pass" + pass, 0) == 0,
            "README must start with current release note");

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

    requireContains(macosClosure, "./build.sh --macos-closure",
                    "macOS closure status must document the exact command");
    requireContains(macosClosure, "release-check", "macOS closure doc must include release-check");
    requireContains(macosClosure, "full CTest", "macOS closure doc must include full CTest");
    requireContains(macosClosure, "AUv2 install", "macOS closure doc must include AUv2 install");
    requireContains(macosClosure, "AU cache refresh", "macOS closure doc must include AU cache refresh");
    requireContains(macosClosure, "auval: PENDING", "macOS closure doc must keep auval pending");
    requireContains(macosClosure, "Logic runtime: PENDING", "macOS closure doc must keep Logic pending");
    requireContains(auvalStatus, "./build.sh --macos-closure",
                    "auval handoff must point at the full closure command");
    requireContains(handoff, "#25: full macOS closure command added",
                    "handoff must record fix-order #25");

    requireNotContains(macosClosure, "AU VALIDATION SUCCEEDED",
                       "package must not claim auval success yet");
    requireNotContains(macosClosure, "Logic validation succeeded",
                       "package must not claim Logic success yet");
    requireNotContains(macosClosure, "notarization succeeded",
                       "package must not claim notarization success yet");

    std::cout << "RootBuildScriptMacosClosureV772Tests PASS\n";
    return 0;
}
