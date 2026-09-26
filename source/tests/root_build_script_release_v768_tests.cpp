// Copyright (C) 2024-2026 Ulf Bertilsson
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    const std::filesystem::path root = ARPSID_SOURCE_ROOT;
    const std::filesystem::path build = root / "build.sh";

    if (!std::filesystem::exists(build)) {
        std::cerr << "missing source-root build.sh\n";
        return 1;
    }

    struct stat st {};
    if (::stat(build.string().c_str(), &st) != 0) {
        std::cerr << "stat failed for build.sh\n";
        return 1;
    }
    if ((st.st_mode & S_IXUSR) == 0) {
        std::cerr << "build.sh must be executable by owner\n";
        return 1;
    }

    const std::string s = readFile(build);
    const char* required[] = {
        "#!/usr/bin/env bash",
        "set -euo pipefail",
        "cmake",
        "ctest",
        "--install-auv2",
        "--clear-au-cache",
        "--no-tests",
        "ARPSID_BUILD_DIR",
        "scripts/macos/install_auv2_component.sh",
        "--install-auv2 is macOS-only"
    };
    for (const char* needle : required) {
        if (!contains(s, needle)) {
            std::cerr << "build.sh missing required token: " << needle << "\n";
            return 1;
        }
    }

    const char* forbidden[] = {
        "rm -rf /",
        "sudo ",
        "killall -9 Logic",
        "~/Library/Audio/Plug-Ins/Components/ArpSID.component" // must use quoted $HOME form instead
    };
    for (const char* needle : forbidden) {
        if (contains(s, needle)) {
            std::cerr << "build.sh contains forbidden risky token: " << needle << "\n";
            return 1;
        }
    }

    return 0;
}
