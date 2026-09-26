// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
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
    const std::string root = ARPSID_SOURCE_ROOT;
    std::ifstream mf(root + "/RELEASE_CONTENTS.sha256");
    if (!mf) {
        std::cerr << "Cannot open RELEASE_CONTENTS.sha256\n";
        return 2;
    }

    bool ok = true;
    std::string hash;
    std::string rel;
    while (mf >> hash >> rel) {
        const bool inRoot = rel.find('/') == std::string::npos;
        if (!inRoot) continue;

        if ((startsWith(rel, "PASS") && (endsWith(rel, ".md") || endsWith(rel, ".diff"))) ||
            startsWith(rel, "AUDIT_") ||
            rel.find("_pass") != std::string::npos || rel.find("_PASS") != std::string::npos ||
            endsWith(rel, ".patch") || endsWith(rel, ".rej") || endsWith(rel, ".orig")) {
            std::cerr << "release root contains patch/history artifact: " << rel << "\n";
            ok = false;
        }
    }

    if (!ok) return 1;
    std::cout << "ReleaseRootCleanV705Tests PASS\n";
    return 0;
}
