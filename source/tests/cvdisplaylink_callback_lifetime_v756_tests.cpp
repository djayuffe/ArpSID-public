// Copyright (C) 2024-2026 Ulf Bertilsson
// cvdisplaylink_callback_lifetime_v756_tests.cpp
//
// audit P0-11 / fix-order #9: the CVDisplayLink output callback fires on a
// high-priority CoreVideo thread. It previously received `(__bridge void*)self` — a
// raw, unretained controller pointer — so a callback in flight during controller
// teardown could dereference freed memory (use-after-free).
//
// The fix routes a heap context box holding a ZEROING __weak controller reference;
// the callback weak-loads it (nil once the controller deallocates). A runtime UAF
// reproduction needs a real display server and precise teardown timing, so this is
// a structural regression guard that the safe lifetime contract stays wired.

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

    // A dedicated weak-box context type exists and holds a weak controller ref.
    const size_t boxAt = mm.find("@interface ArpSIDDisplayLinkContext");
    require(boxAt != std::string::npos, "ArpSIDDisplayLinkContext box type exists");
    require(mm.find("weak) ArpSIDViewController* controller", boxAt) != std::string::npos ||
            mm.find("weak)ArpSIDViewController* controller", boxAt) != std::string::npos,
            "context box holds a zeroing __weak controller reference");

    // The callback must NOT cast the raw context straight to the controller; it must
    // go through the box and weak-load the controller, bailing if nil.
    const size_t cbAt = mm.find("ArpSIDVCDisplayLinkCallback(CVDisplayLinkRef");
    require(cbAt != std::string::npos, "display link callback present");
    const size_t cbBody = mm.find('{', cbAt);
    const size_t cbEnd = mm.find("\n}", cbBody);
    const std::string cb = mm.substr(cbBody, cbEnd - cbBody);
    require(cb.find("(__bridge ArpSIDDisplayLinkContext*)ctx") != std::string::npos,
            "callback casts the context to the weak-box, not the controller");
    require(cb.find("(__bridge ArpSIDViewController*)ctx") == std::string::npos,
            "callback must not cast the raw context directly to the controller (UAF)");
    require(cb.find("box.controller") != std::string::npos,
            "callback weak-loads the controller from the box");

    // Setup retains the box as the context; teardown transfer-releases it after the
    // link is stopped/released.
    require(mm.find("(__bridge_retained void*)box") != std::string::npos,
            "setup passes a retained weak-box as the callback context");
    const size_t releaseAt = mm.find("-(void)_releaseDisplayLink_v277_");
    require(releaseAt != std::string::npos, "release method present");
    const size_t transferAt = mm.find("(__bridge_transfer ArpSIDDisplayLinkContext*)_displayLinkCtx", releaseAt);
    require(transferAt != std::string::npos,
            "release transfer-releases the weak-box context");
    const size_t linkReleaseAt = mm.find("CVDisplayLinkRelease(_displayLink)", releaseAt);
    require(linkReleaseAt != std::string::npos && linkReleaseAt < transferAt,
            "the display link is released BEFORE the context box is freed");

    std::cout << "CVDisplayLinkCallbackLifetimeV756Tests PASS\n";
    return 0;
}
