// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static bool containsAnyCodeTokenAfterCommentSeparator(const std::string& line) {
    const std::size_t slash = line.find("//");
    if (slash == std::string::npos) return false;
    const std::string tail = line.substr(slash + 2u);
    if (tail.find("---") == std::string::npos) return false;

    // A separator comment may be followed by prose, but never by declarations,
    // statements, Objective-C method declarations, preprocessor directives, or
    // known restoration-sensitive code tokens on the same physical line. This
    // catches the exact failure class that broke math_utils.h and later hid
    // ObjC++ declarations such as getDigiModel:sampleBank:.
    static const char* kCodeTokens[] = {
        " static ", "static inline", " inline ", "constexpr ", " const ",
        " return ", " if (", "if(", " for (", "while (", " switch (",
        " enum ", " struct ", " class ", " namespace ", " using ",
        " void ", " int ", " float ", " double ", " auto ", "std::",
        "#include", "#define", "#if", "#endif",
        "- (", "+ (", "@interface", "@implementation",
        ".store(", ".load(", "flushMerge", "consumePending", "setDigi", "getDigi"
    };

    std::string padded = " " + tail + " ";
    for (const char* tok : kCodeTokens) {
        if (padded.find(tok) != std::string::npos) return true;
    }
    return false;
}

static bool shouldScan(const std::string& path) {
    const char* exts[] = {".h", ".hpp", ".c", ".cpp", ".m", ".mm", ".inc"};
    for (const char* ext : exts) {
        const std::string e(ext);
        if (path.size() >= e.size() && path.compare(path.size() - e.size(), e.size(), e) == 0) return true;
    }
    return false;
}

int main() {
    namespace fs = std::filesystem;
    const fs::path root = ARPSID_SOURCE_ROOT;

    // Scan every first-party C/C++/ObjC++ source that ships in the tree.
    std::vector<fs::path> files;
    for (const char* dir : {"include", "source", "external"}) {
        const fs::path base = root / dir;
        if (!fs::exists(base)) continue;
        for (const auto& entry : fs::recursive_directory_iterator(base)) {
            if (entry.is_regular_file() && shouldScan(entry.path().string())) files.push_back(entry.path());
        }
    }
    if (files.empty()) {
        std::cerr << "no sources found under " << root << "\n";
        return 2;
    }

    bool ok = true;
    for (const auto& path : files) {
        std::ifstream f(path);
        if (!f) continue;
        const std::string rel = fs::relative(path, root).generic_string();
        std::string line;
        int lineNo = 0;
        while (std::getline(f, line)) {
            ++lineNo;
            if (containsAnyCodeTokenAfterCommentSeparator(line)) {
                std::cerr << rel << ":" << lineNo
                          << ": separator comment shares a line with code-like token: "
                          << line << "\n";
                ok = false;
            }
        }
    }

    if (!ok) return 1;
    std::cout << "SourceCommentCodeMergeGuardV704Tests PASS\n";
    return 0;
}
