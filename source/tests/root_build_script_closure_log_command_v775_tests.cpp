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

    std::cout << "RootBuildScriptClosureLogCommandV775Tests PASS\n";
    return 0;
}
