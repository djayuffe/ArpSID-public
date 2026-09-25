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
    const std::string version = readText("VERSION.txt");
    const std::string pass = extractCurrentPass(version);
    const std::string versionHeader = readText("include/arpsid/version.h");
    const std::string readme = readText("README.md");
    const std::string status = readText("arpsid-fix-list-690.md");
    const std::string install = readText("RELEASE_MACOS_INSTALL_STATUS.md");

    requireContains(version, ("0.0.690-pass" + pass).c_str(),
                    "VERSION.txt must carry current pass identity");
    requireContains(versionHeader, ("#define ARPSID_BUILD_PASS " + pass).c_str(),
                    "version.h build pass must match current pass");
    require(readme.rfind("# ArpSID 0.0.690 pass" + pass, 0) == 0,
            "README must start with current release note");

    requireContains(status, ("# ArpSID 0.0.690 pass" + pass).c_str(),
                    "handoff status must be current auval command handoff");
    requireContains(status, "313/313", "status must record the Mac full-suite result");
    requireContains(status, "AUv2 bundle build/install path completed",
                    "status must record AUv2 install completion");
    requireContains(status, "MacosInstallStatusHandoffV770Tests",
                    "status must name the v770 guard");

    requireContains(install, "source suite: PASS (313/313 on macOS)",
                    "install status must record full CTest pass");
    requireContains(install, "AUv2 install: PASS", "install status must record AUv2 install pass");
    requireContains(install, "AU cache refresh: PASS", "install status must record cache refresh pass");
    requireContains(install, "auval: PENDING", "install status must keep auval pending");
    requireContains(install, "Logic runtime: PENDING", "install status must keep Logic pending");
    requireContains(install, "auval -v aumu ArpS ASID", "install status must provide next auval command");

    requireNotContains(status, "AU VALIDATION SUCCEEDED",
                       "status must not claim auval success yet");
    requireNotContains(install, "AU VALIDATION SUCCEEDED",
                       "install status must not claim auval success yet");
    requireNotContains(install, "Logic validation succeeded",
                       "install status must not claim Logic success yet");

    std::cout << "MacosInstallStatusHandoffV770Tests PASS\n";
    return 0;
}
