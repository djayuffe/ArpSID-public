// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string slurp(const char* rel) {
    std::string path = std::string(ARPSID_SOURCE_ROOT) + "/" + rel;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "cannot open " << path << "\n";
        std::exit(2);
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    const std::string gui = slurp("source/au3/ArpSIDViewController.mm");

    const std::string decl = "static inline CGFloat ArpSIDUIDynamicSideWidth(CGFloat width) noexcept";
    const auto declPos = gui.find(decl);
    require(declPos != std::string::npos, "dynamic side-width helper exists");

    const std::string call = "ArpSIDUIDynamicSideWidth(r.size.width)";
    size_t count = 0;
    size_t pos = 0;
    while ((pos = gui.find(call, pos)) != std::string::npos) {
        ++count;
        pos += call.size();
    }
    require(count >= 2, "dynamic side-width helper is used by at least two real layout panels");
    require(gui.find("const CGFloat sideW  = 76.0f;") == std::string::npos,
            "DIGI panel must not keep stale hard-coded sidebar width");
    require(gui.find("const CGFloat sideW = 74.0f;    // drum class sidebar width") == std::string::npos,
            "KIT panel must not keep stale hard-coded sidebar width");
    require(gui.find("__attribute__((unused)) static inline CGFloat ArpSIDUIDynamicSideWidth") == std::string::npos,
            "warning must be fixed by real use, not by unused suppression");

    std::cout << "Auv2ViewWarningFreeV732Tests PASS\n";
    return 0;
}
