// Copyright (C) 2024-2026 Ulf Bertilsson
// drum_user_kit_save_weak_continuation_v796_tests.cpp
//
// fix-order #50: _drumSaveUserKit: snapshots URL/name but its final main-queue
// publish block must not retain the handler-local strong controller `s`. The
// delayed status/library reload continuation should weak-load from the existing
// zeroing `ws` token, then touch the bank panel only through the weak-loaded
// controller.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    require(static_cast<bool>(f), rel);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string mm = readFile("source/au3/ArpSIDViewController.mm");
    const std::string needleMethod = "-(void)_drumSaveUserKit:(id)sender";
    const size_t m = mm.find(needleMethod);
    require(m != std::string::npos, "_drumSaveUserKit: method exists");
    const size_t next = mm.find("\n-(void)_drumRefreshUserKitLibrary:", m);
    require(next != std::string::npos, "method boundary found");
    const std::string method = mm.substr(m, next - m);

    require(method.find("NSURL* kitURL = [sp.URL copy];") != std::string::npos,
            "kit save snapshots the panel URL");
    require(method.find("NSString* kitFileName = [kitURL.lastPathComponent copy];") != std::string::npos,
            "kit save snapshots display filename");

    require(method.find("\n                s->_loadedDrumUserKitURL=kitURL") == std::string::npos,
            "delayed user-kit publish must not retain/use handler-local strong controller `s`");
    require(method.find("\n                [s _reloadDrumUserKitLibrarySelectingPath:kitURL.path]") == std::string::npos,
            "delayed user-kit reload must not message handler-local strong controller `s`");
    require(method.find("\n            if(s->_pBank) [s _bankSetStatus:msg panel:s->_pBank]") == std::string::npos,
            "delayed user-kit status must not retain/use handler-local strong controller `s`");

    const std::string weakBlock =
        "dispatch_async(dispatch_get_main_queue(), ^{\n"
        "            ArpSIDViewController* ss = ws; if(!ss) return;\n"
        "            if(res==ArpSIDFileBankResultOK){\n"
        "                ss->_loadedDrumUserKitURL=kitURL;\n"
        "                [ss _reloadDrumUserKitLibrarySelectingPath:kitURL.path];\n"
        "            }\n"
        "            if(ss->_pBank) [ss _bankSetStatus:msg panel:ss->_pBank];\n"
        "        });";
    require(method.find(weakBlock) != std::string::npos,
            "user-kit save publish weak-loads controller on main before updating state/status");

    std::cout << "DrumUserKitSaveWeakContinuationV796Tests PASS\n";
    return 0;
}
