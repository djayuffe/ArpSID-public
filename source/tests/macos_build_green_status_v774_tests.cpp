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
    const std::string cmake = readText("CMakeLists.txt");
    const std::string version = readText("VERSION.txt");
    const std::string pass = extractCurrentPass(version);
    const std::string header = readText("include/arpsid/version.h");
    const std::string readme = readText("README.md");
    const std::string status = readText("arpsid-fix-list-690.md");
    const std::string audit = readText("AUDIT-FIXES-0.0.686.md");
    const std::string buildGreen = readText("RELEASE_MACOS_BUILD_GREEN_STATUS.md");
    const std::string closureLogging = readText("RELEASE_MACOS_CLOSURE_LOGGING.md");

    requireContains(version, ("0.0.690-pass" + pass).c_str(),
                    "VERSION.txt must carry current pass identity");
    requireContains(header, ("#define ARPSID_BUILD_PASS " + pass).c_str(),
                    "version.h build pass must match current pass");
    require(readme.rfind("# ArpSID 0.0.690 pass" + pass, 0) == 0,
            "README must start with current release note");

    requireContains(cmake, "MacosBuildGreenStatusV774Tests",
                    "CMake must register v774 build-green guard");
    requireContains(status, "#27: macOS build-green handoff recorded",
                    "handoff must record fix-order #27");
    requireContains(audit, "Fix-order #27 — macOS build-green handoff status",
                    "audit must record fix-order #27");

    requireContains(buildGreen, "Det bygget fint.",
                    "build-green status must cite the user-confirmed build handoff");
    requireContains(buildGreen, "macOS build after pass348: PASS (user-confirmed)",
                    "build-green status must mark only the macOS build as confirmed");
    requireContains(buildGreen, "strict targeted auval: PENDING",
                    "build-green status must keep auval pending");
    requireContains(buildGreen, "Logic runtime validation: PENDING",
                    "build-green status must keep Logic pending");
    requireContains(buildGreen, "VST3 SDK/toolchain validation: PENDING",
                    "build-green status must keep VST3 pending");
    requireContains(buildGreen, "notarization: PENDING",
                    "build-green status must keep notarization pending");
    requireContains(buildGreen, "./build.sh --macos-closure --closure-log-dir ./release-logs",
                    "build-green status must point to the next closure command");
    requireContains(closureLogging, "macos-closure-*.log",
                    "closure logging doc must still name the expected handoff log");

    requireNotContains(buildGreen, "AU VALIDATION SUCCEEDED",
                       "build-green status must not claim auval success");
    requireNotContains(buildGreen, "Logic validation succeeded",
                       "build-green status must not claim Logic runtime success");
    requireNotContains(buildGreen, "notarization succeeded",
                       "build-green status must not claim notarization success");
    requireNotContains(status, "AU VALIDATION SUCCEEDED",
                       "handoff status must not claim auval success");

    std::cout << "MacosBuildGreenStatusV774Tests PASS\n";
    return 0;
}
