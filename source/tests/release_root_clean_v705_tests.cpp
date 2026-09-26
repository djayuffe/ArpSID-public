// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

static bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

int main() {
    namespace fs = std::filesystem;
    const fs::path root = ARPSID_SOURCE_ROOT;

    // The source root must not accumulate per-pass notes, patches or merge
    // leftovers; history belongs in git and CHANGELOG.md.
    bool ok = true;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const std::string rel = entry.path().filename().string();
        if ((startsWith(rel, "PASS") && (endsWith(rel, ".md") || endsWith(rel, ".diff"))) ||
            startsWith(rel, "AUDIT_") || startsWith(rel, "AUDIT-") ||
            startsWith(rel, "RELEASE_NOTES") || startsWith(rel, "HANDOFF") ||
            rel.find("_pass") != std::string::npos || rel.find("_PASS") != std::string::npos ||
            rel.find("CLOSURE") != std::string::npos ||
            endsWith(rel, ".patch") || endsWith(rel, ".rej") || endsWith(rel, ".orig")) {
            std::cerr << "source root contains patch/history artifact: " << rel << "\n";
            ok = false;
        }
    }

    if (!ok) return 1;
    std::cout << "ReleaseRootCleanV705Tests PASS\n";
    return 0;
}
