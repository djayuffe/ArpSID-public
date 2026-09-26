// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* path) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + path, std::ios::binary);
    if (!in) {
        std::cerr << "failed to open " << path << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    const std::string src = readFile("source/gui/arpsid_vst_cocoa_bridge.mm");

    require(src.find("weakParent_v793") != std::string::npos,
            "embedded VST focus continuation snapshots parent weakly");
    require(src.find("weakChild_v793") != std::string::npos,
            "embedded VST focus continuation snapshots child weakly");
    require(src.find("weakController_v793") != std::string::npos,
            "embedded VST focus continuation snapshots controller weakly");
    require(src.find("if (!parent_v793 || !child_v793 || !controller_v793) return;") != std::string::npos,
            "embedded VST focus continuation fail-closes after weak-load");
    require(src.find("NSWindow* w = parent_v793.window;") != std::string::npos,
            "embedded VST focus continuation uses weak-loaded parent");
    require(src.find("NSView* first = child_v793;") != std::string::npos,
            "embedded VST focus continuation uses weak-loaded child");
    require(src.find("[controller_v793 embeddedVSTPreferredFirstResponder]") != std::string::npos,
            "embedded VST focus continuation uses weak-loaded controller");

    const std::string old = "NSWindow* w = parent.window;\n            if (w) {\n                [w recalculateKeyViewLoop];\n                NSView* first = child;\n                if ([handle.controllerVC respondsToSelector:@selector(embeddedVSTPreferredFirstResponder)])";
    require(src.find(old) == std::string::npos,
            "old strong parent/child/handle focus continuation is removed");

    return 0;
}
