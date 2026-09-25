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
    if (!in) { std::cerr << "missing file: " << rel << "\n"; std::exit(1); }
    std::ostringstream ss; ss << in.rdbuf(); return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::string sliceBetween(const std::string& s, const std::string& begin, const std::string& end) {
    const auto b = s.find(begin);
    require(b != std::string::npos, "begin marker missing");
    const auto e = s.find(end, b + begin.size());
    require(e != std::string::npos, "end marker missing");
    return s.substr(b, e - b);
}

int main() {
    const std::string src = readFile("source/au3/ArpSIDHostAppDelegate.mm");
    const std::string cmake = readFile("CMakeLists.txt");
    const std::string audit = readFile("AUDIT-FIXES-0.0.686.md");

    const std::string notify = sliceBetween(src,
        "static void ArpSIDMIDINotifyProc", "- (void)_setupMIDI");
    require(notify.find("__weak ArpSIDHostAppDelegate* weakDelegate = ArpSIDHostDelegateFromMIDIContext_v778(refCon);") != std::string::npos,
            "CoreMIDI notify proc must convert the weak-box refCon into a weak continuation token");
    require(notify.find("ArpSIDHostAppDelegate* strongDelegate = weakDelegate;") != std::string::npos,
            "CoreMIDI notify continuation must strong-load the weak delegate on main");
    require(notify.find("if (!strongDelegate) return;") != std::string::npos,
            "CoreMIDI notify continuation must nil-guard the weak-loaded delegate");
    require(notify.find("[strongDelegate _reconnectMIDISources]") != std::string::npos,
            "CoreMIDI notify continuation must use the weak-loaded delegate");
    require(notify.find("ArpSIDHostAppDelegate* delegate = ArpSIDHostDelegateFromMIDIContext_v778(refCon);") == std::string::npos,
            "CoreMIDI notify proc must not strong-capture delegate before dispatch");
    require(notify.find("[delegate _reconnectMIDISources]") == std::string::npos,
            "CoreMIDI notify continuation must not use stale strong delegate capture");

    const std::string next = sliceBetween(src,
        "- (void)_menuNextPreset:(id)sender", "- (void)_menuPrevPreset:(id)sender");
    const std::string prev = sliceBetween(src,
        "- (void)_menuPrevPreset:(id)sender", "@end");
    for (const auto* block : {&next, &prev}) {
        require(block->find("__weak ArpSIDHostAppDelegate* weakSelf = self;") != std::string::npos,
                "preset menu continuation must create weakSelf before main dispatch");
        require(block->find("ArpSIDHostAppDelegate* strongSelf = weakSelf;") != std::string::npos,
                "preset menu continuation must strong-load weakSelf on main");
        require(block->find("if (!strongSelf || !strongSelf->_audioUnit) return;") != std::string::npos,
                "preset menu continuation must nil-guard delegate and AU");
        require(block->find("[strongSelf->_audioUnit setCurrentPreset:p]") != std::string::npos,
                "preset menu continuation must set the preset through the weak-loaded delegate");
        require(block->find("[_audioUnit setCurrentPreset:p]") == std::string::npos,
                "preset menu continuation must not implicitly capture self through ivar access");
    }

    require(cmake.find("StandaloneMenuMIDINotifyWeakContinuationsV790Tests") != std::string::npos,
            "v790 guard registered in CMake");
    require(audit.find("fix-order #44") != std::string::npos || audit.find("Fix-order #44") != std::string::npos,
            "audit records fix-order #44");
    require(audit.find("standalone CoreMIDI notify/menu weak continuations") != std::string::npos,
            "audit documents the v790 continuation hardening");

    std::cout << "StandaloneMenuMIDINotifyWeakContinuationsV790Tests PASS\n";
    return 0;
}
