// Copyright (C) 2024-2026 Ulf Bertilsson
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

    const std::string audioStartup = sliceBetween(src,
        "- (void)_instantiateAudioUnit", "- (void)_teardownAudio");
    require(audioStartup.find("__weak ArpSIDHostAppDelegate* weakSelf_v794_start = self;") != std::string::npos,
            "standalone startup repaint/focus continuation must create weak token");
    require(audioStartup.find("ArpSIDHostAppDelegate* strongSelf_v794 = weakSelf_v794_start;") != std::string::npos,
            "standalone startup continuation must strong-load weak token on main");
    require(audioStartup.find("if (!strongSelf_v794) return;") != std::string::npos,
            "standalone startup continuation must nil-guard delegate");
    require(audioStartup.find("if (!strongSelf_v794->_viewController || !strongSelf_v794->_mainWindow) return;") != std::string::npos,
            "standalone startup continuation must guard view/window before use");
    require(audioStartup.find("[strongSelf_v794->_viewController.view setNeedsDisplay:YES]") != std::string::npos,
            "standalone startup continuation must use weak-loaded controller");
    require(audioStartup.find("[self->_viewController.view setNeedsDisplay:YES]") == std::string::npos,
            "standalone startup continuation must not capture self through viewController ivar");

    const std::string buildWindow = sliceBetween(src,
        "- (void)_buildWindowWithAU:(AUAudioUnit*)au", "// ── 5. Connect AU");
    require(buildWindow.find("__weak ArpSIDHostAppDelegate* weakSelf_v794_window = self;") != std::string::npos,
            "standalone window polish continuation must create weak token");
    require(buildWindow.find("ArpSIDHostAppDelegate* strongSelf_v794 = weakSelf_v794_window;") != std::string::npos,
            "standalone window polish continuation must strong-load weak token on main");
    require(buildWindow.find("if (!strongSelf_v794 || !strongSelf_v794->_mainWindow) return;") != std::string::npos,
            "standalone window polish continuation must nil-guard delegate/window");
    require(buildWindow.find("[strongSelf_v794->_mainWindow makeKeyAndOrderFront:nil]") != std::string::npos,
            "standalone window polish continuation must use weak-loaded window");
    require(buildWindow.find("[self->_mainWindow makeKeyAndOrderFront:nil]") == std::string::npos,
            "standalone window polish continuation must not capture self through mainWindow ivar");
    require(buildWindow.find("self->_mainWindow") == std::string::npos,
            "standalone window polish block must not reference self->_mainWindow after hardening");

    require(cmake.find("StandaloneStartupWindowWeakContinuationsV794Tests") != std::string::npos,
            "v794 guard registered in CMake");
    require(audit.find("fix-order #48") != std::string::npos || audit.find("Fix-order #48") != std::string::npos,
            "audit records fix-order #48");
    require(audit.find("standalone startup/window weak continuations") != std::string::npos,
            "audit documents standalone startup/window weak continuation hardening");

    std::cout << "StandaloneStartupWindowWeakContinuationsV794Tests PASS\n";
    return 0;
}
