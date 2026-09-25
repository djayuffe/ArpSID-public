#include <cstdlib>
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
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string manifest = root + "/RELEASE_CONTENTS.sha256";
    std::ifstream mf(manifest);
    if (!mf) {
        std::cerr << "Cannot open RELEASE_CONTENTS.sha256 at " << manifest << "\n";
        return 2;
    }

    bool ok = true;
    std::string hash, rel;
    while (mf >> hash >> rel) {
        if (!shouldScan(rel)) continue;
        std::ifstream f(root + "/" + rel);
        if (!f) continue;
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
