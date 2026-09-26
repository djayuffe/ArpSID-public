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
    const std::string installStatus = readText("RELEASE_MACOS_INSTALL_STATUS.md");

    requireContains(version, ("0.0.690-pass" + pass).c_str(),
                    "VERSION.txt must carry current pass identity");
    requireContains(header, ("#define ARPSID_BUILD_PASS " + pass).c_str(),
                    "version.h build pass must match current pass");
    require(readme.rfind("# ArpSID 0.0.690 pass" + pass, 0) == 0,
            "README must start with current release note");

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

    requireContains(auvalStatus, "./build.sh --validate-auv2",
                    "auval status must document the executable validation command");
    requireContains(auvalStatus, "./build.sh --install-auv2 --clear-au-cache --validate-auv2",
                    "auval status must document the full install+validate command");
    requireContains(auvalStatus, "auval -strict -v aumu ArpS ASID",
                    "auval status must list the canonical strict ArpSID command");
    requireContains(auvalStatus, "auval: PENDING",
                    "auval status must keep auval pending until a Mac log is captured");
    requireContains(installStatus, "auval: PENDING",
                    "previous install status must still keep auval pending");
    requireNotContains(auvalStatus, "AU VALIDATION SUCCEEDED",
                       "package must not claim auval success yet");
    requireNotContains(auvalStatus, "Logic validation succeeded",
                       "package must not claim Logic success yet");

    std::cout << "RootBuildScriptAuvalModeV771Tests PASS\n";
    return 0;
}
