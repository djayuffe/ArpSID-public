#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const std::string& rel) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!in) {
        std::cerr << "missing file: " << rel << "\n";
        std::exit(1);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string stripComments(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool line = false, block = false, str = false, chr = false, esc = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const char n = (i + 1 < s.size()) ? s[i + 1] : '\0';
        if (line) {
            if (c == '\n') { line = false; out.push_back(c); }
            continue;
        }
        if (block) {
            if (c == '*' && n == '/') { block = false; ++i; }
            continue;
        }
        if (!str && !chr && c == '/' && n == '/') { line = true; ++i; continue; }
        if (!str && !chr && c == '/' && n == '*') { block = true; ++i; continue; }
        out.push_back(c);
        if (esc) { esc = false; continue; }
        if (c == '\\' && (str || chr)) { esc = true; continue; }
        if (!chr && c == '"') str = !str;
        else if (!str && c == '\'') chr = !chr;
    }
    return out;
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static void requireContains(const std::string& hay, const std::string& needle, const char* msg) {
    require(hay.find(needle) != std::string::npos, msg);
}

int main() {
    const std::string vc = readFile("source/au3/ArpSIDViewController.mm");
    const std::string code = stripComments(vc);
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string audit = readFile("AUDIT-FIXES-0.0.686.md");

    requireContains(code, "@interface ArpSIDDigiAQControllerContext", "DIGI AQ weak context class exists");
    requireContains(code, "@property (atomic, weak) ArpSIDViewController* controller", "DIGI AQ context holds a zeroing weak controller");
    requireContains(code, "ArpSIDDigiAQControllerFromContext_v777", "callbacks resolve controller through weak context helper");
    requireContains(code, "ArpSIDReleaseRetainedObjCContext_v777", "retained ObjC contexts have one release helper");
    requireContains(code, "_digiAQPreviewContext_v777_ = (__bridge_retained void*)previewCtx_v777", "preview context is retained before AudioQueue start");
    requireContains(code, "_digiAQRecordContext_v777_ = (__bridge_retained void*)recordCtx_v777", "record context is retained before AudioQueue start");
    requireContains(code, "_digiAQPreviewContext_v777_,\n            [weakSelf, aqPrevGen]", "preview AudioQueue start receives retained weak box, not self");
    requireContains(code, "_digiAQRecordContext_v777_,\n            [weakSelf, aqRecGen]", "record AudioQueue start receives retained weak box, not self");
    require(code.find("(__bridge void*)self,\n            [weakSelf, aqPrevGen]") == std::string::npos,
            "preview AudioQueue must not pass raw self as context");
    require(code.find("(__bridge void*)self,\n            [weakSelf, aqRecGen]") == std::string::npos,
            "record AudioQueue must not pass raw self as context");
    requireContains(code, "ArpSIDReleaseRetainedObjCContext_v777(_digiAQPreviewContext_v777_);", "preview context is released on stop/failure/teardown paths");
    requireContains(code, "ArpSIDReleaseRetainedObjCContext_v777(_digiAQRecordContext_v777_);", "record context is released on stop/failure/teardown paths");

    requireContains(cmake, "DigiAudioQueueWeakContextV777Tests", "v777 guard registered in CMake");
    requireContains(audit, "Fix-order #31", "audit records fix-order #31");
    requireContains(audit, "AudioQueue", "audit documents AudioQueue context lifetime fix");

    std::cout << "DigiAudioQueueWeakContextV777Tests PASS\n";
    return 0;
}
