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
    const std::string doc = readText("RELEASE_MACOS_CLOSURE_LOG_COMMAND_STATUS.md");
    const std::string handoff = readText("arpsid-fix-list-690.md");
    const std::string audit = readText("AUDIT-FIXES-0.0.686.md");

    requireContains(version, ("0.0.690-pass" + pass).c_str(),
                    "VERSION.txt must carry current pass identity");
    requireContains(header, ("#define ARPSID_BUILD_PASS " + pass).c_str(),
                    "version.h build pass must match current pass");
    require(readme.rfind("# ArpSID 0.0.690 pass" + pass, 0) == 0,
            "README must start with current release note");

    requireContains(build, "ORIGINAL_ARGS=(\"$@\")",
                    "build.sh must snapshot argv before option parsing");
    requireContains(build, "for arg in \"${ORIGINAL_ARGS[@]}\"",
                    "build.sh must reconstruct command from preserved argv");
    requireContains(build, "printf -v ARPSID_QUOTED_ARG '%q' \"$arg\"",
                    "build.sh must shell-quote logged argv items");
    requireContains(build, "[ArpSID] command: ${ORIGINAL_COMMAND}",
                    "build.sh must log the preserved original command");
    requireNotContains(build, "[ArpSID] command: $0 $*",
                       "build.sh must not log post-parse empty argv");

    requireContains(doc, "original argv before option parsing",
                    "release doc must describe the argv preservation fix");
    requireContains(doc, "--macos-closure --closure-log-dir ./release-logs",
                    "release doc must show the exact closure command that should survive in logs");
    requireContains(handoff, "#28: macOS closure log command preservation",
                    "handoff must record fix-order #28");
    requireContains(audit, "Fix-order #28 — macOS closure log command preservation",
                    "audit must record fix-order #28");

    requireNotContains(doc, "AU VALIDATION SUCCEEDED",
                       "doc must not claim auval success");
    requireNotContains(handoff, "AU VALIDATION SUCCEEDED",
                       "handoff must not claim auval success");
    requireNotContains(doc, "Logic runtime success",
                       "doc must not claim Logic runtime success");
    requireNotContains(doc, "notarization succeeded",
                       "doc must not claim notarization success");

    std::cout << "RootBuildScriptClosureLogCommandV775Tests PASS\n";
    return 0;
}
