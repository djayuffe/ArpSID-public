// Copyright (C) 2024-2026 Ulf Bertilsson
// cvdisplaylink_mainqueue_weak_continuation_v788_tests.cpp
//
// fix-order #42: the CVDisplayLink callback already uses a zeroing weak-box
// context, but its main-queue continuation must not re-retain the editor from the
// CoreVideo thread before dispatch. Otherwise a queued poll can unnecessarily hold
// the host editor/controller alive during teardown. The continuation should weak-
// capture the controller, strong-load on main if still alive, and release the poll
// gate in @finally for the still-alive case.

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

    const size_t cbAt = mm.find("ArpSIDVCDisplayLinkCallback(CVDisplayLinkRef");
    require(cbAt != std::string::npos, "display-link callback exists");
    const size_t cbBody = mm.find('{', cbAt);
    const size_t weakAt = mm.find("weakForDispatch", cbBody);
    const size_t cbEnd = mm.find("return kCVReturnSuccess;", weakAt);
    require(cbBody != std::string::npos && weakAt != std::string::npos && cbEnd != std::string::npos, "callback body range found");
    const std::string cb = mm.substr(cbBody, cbEnd - cbBody);

    require(cb.find("__weak ArpSIDViewController* weakForDispatch = vc;") != std::string::npos,
            "callback creates a weak main-queue dispatch token");
    require(cb.find("ArpSIDViewController* strongForDispatch = weakForDispatch;") != std::string::npos,
            "main-queue continuation strong-loads from the weak token");
    require(cb.find("if (!strongForDispatch) return;") != std::string::npos,
            "main-queue continuation bails if the controller is gone");
    require(cb.find("ArpSIDViewController* strongForDispatch = vc;") == std::string::npos,
            "callback must not retain the controller before dispatch_async");
    require(cb.find("[strongForDispatch _endPollTick_v246_]") != std::string::npos,
            "still-alive continuation releases the poll gate");

    const size_t v731 = mm.find("weakForDispatch = vc;");
    require(v731 != std::string::npos, "weak dispatch token appears in source");

    std::cout << "CVDisplayLinkMainQueueWeakContinuationV788Tests PASS\n";
    return 0;
}
