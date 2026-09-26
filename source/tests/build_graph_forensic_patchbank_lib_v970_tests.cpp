// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// build_graph_forensic_patchbank_lib_v970_tests.cpp — v970 closure.
//
// Handoff 22.7 (test build graph): forensic_patch_bank.cpp is a heavyweight
// translation unit that 14 separate test executables were each recompiling
// from source. v970 compiles it once into a static library
// (arpsid_forensic_patchbank) that those tests link, cutting duplicate
// compilation and peak build memory without weakening test isolation.
//
// This source-contract test pins the build-graph invariant so the duplication
// cannot silently return:
//   * the static library is defined from the single forensic TU;
//   * every test target that needs the forensic patch bank links that library
//     rather than re-adding source/forensic_patch_bank.cpp to its sources;
//   * production wrappers (VST3, the AUv2 GUI smoke) are unchanged — they may
//     still compile their own copy, which is ODR-safe across separate binaries.

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "build_graph_forensic_patchbank_lib_v970_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), (std::string("cannot open ") + path).c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void requireContains(const std::string& hay, const std::string& needle, const char* msg) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "build_graph_forensic_patchbank_lib_v970_tests FAIL: " << msg
                  << "\n  missing: " << needle << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
#ifndef ARPSID_SOURCE_ROOT
#error ARPSID_SOURCE_ROOT must be defined
#endif
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string cm = readFile(root + "/CMakeLists.txt");

    // (1) The shared static library is defined from the single forensic TU.
    requireContains(cm, "add_library(arpsid_forensic_patchbank STATIC source/forensic_patch_bank.cpp)",
                    "the forensic patch bank must be compiled once into arpsid_forensic_patchbank");

    // (2) Every *_tests target that links the shared library must NOT also list
    //     source/forensic_patch_bank.cpp in its own add_executable (that would
    //     both recompile the TU and duplicate-link it). Walk each add_executable
    //     block; if it is a *_tests target that links arpsid_forensic_patchbank,
    //     assert the block's source list does not contain the forensic TU.
    std::istringstream in(cm);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) lines.push_back(line);

    int checkedLinked = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::smatch m;
        if (!std::regex_search(lines[i], m, std::regex(R"(add_executable\(\s*([A-Za-z0-9_]+))"))) continue;
        const std::string target = m[1];
        if (target.size() < 6 || target.substr(target.size() - 6) != "_tests") continue;

        // Collect the add_executable source span (until balanced close paren).
        std::string block = lines[i];
        int depth = 0;
        for (char c : lines[i]) { if (c == '(') ++depth; else if (c == ')') --depth; }
        std::size_t j = i;
        while (depth > 0 && ++j < lines.size()) {
            block += "\n" + lines[j];
            for (char c : lines[j]) { if (c == '(') ++depth; else if (c == ')') --depth; }
        }
        // Does this target link the shared lib (search a small window after it)?
        std::string window;
        for (std::size_t k = j; k < lines.size() && k < j + 12; ++k) {
            if (lines[k].find("add_executable(") != std::string::npos && k != i) break;
            window += lines[k] + "\n";
        }
        const bool linksLib =
            window.find("target_link_libraries(" + target + " PRIVATE arpsid_forensic_patchbank") != std::string::npos ||
            window.find("arpsid_forensic_patchbank") != std::string::npos;
        if (!linksLib) continue;
        ++checkedLinked;
        require(block.find("source/forensic_patch_bank.cpp") == std::string::npos,
                (std::string("test target links the shared forensic lib but also recompiles the TU: ") + target).c_str());
    }
    require(checkedLinked >= 10,
            "expected the shared forensic patch-bank library to be linked by the converted test targets");

    std::cout << "build_graph_forensic_patchbank_lib_v970_tests: OK (" << checkedLinked
              << " test targets link the shared forensic lib without recompiling it)\n";
    return 0;
}
