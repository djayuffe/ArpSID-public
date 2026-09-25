// embedded_vst_presentation_weak_continuation_v792_tests.cpp
//
// fix-order #46: prepareForEmbeddedVSTPresentation posts a main-queue
// first-responder/fullscreen-preparation continuation. That delayed block must not
// capture the editor/controller strongly through `self`, because embedded VST hosts
// can attach/detach the Cocoa view while the queued block is pending. It should use
// a zeroing weak controller token and strong-load only on the main queue.

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

    const size_t fnAt = mm.find("-(void)prepareForEmbeddedVSTPresentation");
    require(fnAt != std::string::npos, "prepareForEmbeddedVSTPresentation exists");
    const size_t fnEnd = mm.find("-(NSView*)embeddedVSTPreferredFirstResponder", fnAt);
    require(fnEnd != std::string::npos, "function end found");
    const std::string fn = mm.substr(fnAt, fnEnd - fnAt);

    require(fn.find("__weak ArpSIDViewController* weakSelf_v792 = self;") != std::string::npos,
            "embedded VST presentation creates a weak continuation token");
    require(fn.find("ArpSIDViewController* strongSelf_v792 = weakSelf_v792;") != std::string::npos,
            "main-queue continuation strong-loads from weak token");
    require(fn.find("if(!strongSelf_v792) return;") != std::string::npos,
            "main-queue continuation bails if editor is gone");
    require(fn.find("NSWindow* w=strongSelf_v792.view.window;") != std::string::npos,
            "window lookup uses weak-loaded controller");
    require(fn.find("[strongSelf_v792 _prepareWindowForUserFullscreen]") != std::string::npos,
            "fullscreen preparation still runs when controller is alive");
    require(fn.find("NSWindow* w=self.view.window;") == std::string::npos,
            "delayed block must not capture self for window lookup");
    require(fn.find("[self _prepareWindowForUserFullscreen]") == std::string::npos,
            "delayed block must not call fullscreen preparation through self");
    require(fn.find("[self embeddedVSTPreferredFirstResponder]") == std::string::npos,
            "delayed block must not call first responder through self");

    std::cout << "EmbeddedVSTPresentationWeakContinuationV792Tests PASS\n";
    return 0;
}
