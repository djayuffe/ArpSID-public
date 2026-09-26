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

int main() {
    const std::string build = readText("build.sh");

    requireContains(build, "--closure-log-dir", "build.sh must expose closure log directory option");
    requireContains(build, "ARPSID_CLOSURE_LOG_DIR", "build.sh must allow env-driven closure log directory");
    requireContains(build, "release-logs", "build.sh must default logs under release-logs");
    requireContains(build, "macos-closure-$(date +%Y%m%d-%H%M%S).log",
                    "build.sh must create timestamped macOS closure logs");
    requireContains(build, "exec > >(tee -a \"$LOG_FILE\") 2>&1",
                    "build.sh must tee closure output to the log file");
    requireContains(build, "[ArpSID] log started:", "build.sh must stamp the log start");
    requireContains(build, "[ArpSID] command:", "build.sh must record the invoked command");

    std::cout << "RootBuildScriptMacosClosureLoggingV773Tests PASS\n";
    return 0;
}
