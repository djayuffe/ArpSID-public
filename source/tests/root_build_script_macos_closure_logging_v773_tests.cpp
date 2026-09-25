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
    const std::string logging = readText("RELEASE_MACOS_CLOSURE_LOGGING.md");
    const std::string handoff = readText("arpsid-fix-list-690.md");

    requireContains(version, ("0.0.690-pass" + pass).c_str(),
                    "VERSION.txt must carry current pass identity");
    requireContains(header, ("#define ARPSID_BUILD_PASS " + pass).c_str(),
                    "version.h build pass must match current pass");
    require(readme.rfind("# ArpSID 0.0.690 pass" + pass, 0) == 0,
            "README must start with current release note");

    requireContains(build, "--closure-log-dir", "build.sh must expose closure log directory option");
    requireContains(build, "ARPSID_CLOSURE_LOG_DIR", "build.sh must allow env-driven closure log directory");
    requireContains(build, "release-logs", "build.sh must default logs under release-logs");
    requireContains(build, "macos-closure-$(date +%Y%m%d-%H%M%S).log",
                    "build.sh must create timestamped macOS closure logs");
    requireContains(build, "exec > >(tee -a \"$LOG_FILE\") 2>&1",
                    "build.sh must tee closure output to the log file");
    requireContains(build, "[ArpSID] log started:", "build.sh must stamp the log start");
    requireContains(build, "[ArpSID] command:", "build.sh must record the invoked command");

    requireContains(logging, "./build.sh --macos-closure --closure-log-dir ./release-logs",
                    "logging doc must give explicit handoff command");
    requireContains(logging, "macos-closure-*.log",
                    "logging doc must tell user which log to provide");
    requireContains(logging, "release-check", "logging doc must include release-check evidence");
    requireContains(logging, "strict targeted auval", "logging doc must include auval evidence");
    requireContains(handoff, "#26: macOS closure log capture added",
                    "handoff must record fix-order #26");

    requireNotContains(logging, "AU VALIDATION SUCCEEDED",
                       "logging doc must not claim auval success");
    requireNotContains(logging, "Logic runtime success",
                       "logging doc must not claim Logic success");
    requireNotContains(logging, "notarization succeeded",
                       "logging doc must not claim notarization success");

    std::cout << "RootBuildScriptMacosClosureLoggingV773Tests PASS\n";
    return 0;
}
