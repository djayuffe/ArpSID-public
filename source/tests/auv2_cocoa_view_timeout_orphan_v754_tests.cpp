// Copyright (C) 2024-2026 Ulf Bertilsson
// auv2_cocoa_view_timeout_orphan_v754_tests.cpp
//
// audit P0-9 / fix-order #7: the AUv2 Cocoa view factory dispatches the editor
// build to the main thread and waits with a 2s timeout. On timeout it returns nil,
// but the queued build can still run later — which would leave an ORPHAN editor
// (a controller/view connected to the AudioUnit and retained via associations) that
// the host never received.
//
// A deterministic runtime test of the timeout race requires a blocked main thread
// and a mock AU host, which is impractical here; the fix is compile-validated by the
// AUv2 build. This test is a structural regression guard that the orphan-prevention
// coordination remains wired in uiViewForAudioUnit: the build/commit and the
// timeout decision must be mutually exclusive (stateLock), and BOTH the late-build
// path and the raced-timeout path must dispose the abandoned editor.

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

// Extract the body of a brace-delimited region starting at the first '{' after a needle.
static std::string regionAfter(const std::string& src, const std::string& needle) {
    const size_t at = src.find(needle);
    require(at != std::string::npos, needle.c_str());
    size_t brace = src.find('{', at);
    require(brace != std::string::npos, "opening brace after needle");
    int depth = 0; size_t i = brace;
    for (; i < src.size(); ++i) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}') { if (--depth == 0) { ++i; break; } }
    }
    return src.substr(brace, i - brace);
}

int main() {
    const std::string mm = readFile("source/au2/ArpSIDAUv2Component.mm");

    // A dedicated disposal helper exists for abandoned editors.
    require(mm.find("disposeAbandonedAUv2Editor") != std::string::npos,
            "disposeAbandonedAUv2Editor helper exists");
    const std::string disposeBody = regionAfter(mm, "static void disposeAbandonedAUv2Editor(");
    require(disposeBody.find("prepareForFinalEditorDisposal") != std::string::npos,
            "disposal helper disposes the controller");
    require(disposeBody.find("kArpSIDAUv2AudioUnitViewAssociationKey") != std::string::npos &&
            disposeBody.find("kArpSIDAUv2AudioUnitControllerAssociationKey") != std::string::npos,
            "disposal helper clears the view and controller associations");

    // The view factory's off-main path coordinates abandonment vs build commit.
    const std::string uiBody = regionAfter(mm, "- (NSView*)uiViewForAudioUnit:");
    require(uiBody.find("@synchronized (stateLock)") != std::string::npos,
            "off-main path serializes the timeout/commit decision under stateLock");
    require(uiBody.find("callerAbandoned") != std::string::npos &&
            uiBody.find("builtOnMain") != std::string::npos,
            "off-main path tracks caller-abandoned and built-on-main flags");

    // On a late build the block must dispose if the caller already abandoned.
    const size_t buildDispose = uiBody.find("if (abandoned) {");
    require(buildDispose != std::string::npos, "block checks abandonment after building");
    require(uiBody.find("strongAudioUnit_v795", buildDispose) != std::string::npos &&
            uiBody.find("disposeAbandonedAUv2Editor(strongAudioUnit_v795)", buildDispose) != std::string::npos,
            "late build disposes the orphan through the weak-loaded AU when the caller abandoned");

    // The timeout branch sets abandonment and disposes if the build already finished.
    const size_t timeoutAt = uiBody.find("dispatch_semaphore_wait");
    require(timeoutAt != std::string::npos, "off-main path waits with a timeout");
    require(uiBody.find("callerAbandoned = YES", timeoutAt) != std::string::npos,
            "timeout marks the request abandoned");
    require(uiBody.find("disposeAbandonedAUv2Editor", timeoutAt) != std::string::npos,
            "timeout path disposes the orphan when the build already committed");
    require(uiBody.find("return nil", timeoutAt) != std::string::npos,
            "timeout still returns nil to the host");

    std::cout << "Auv2CocoaViewTimeoutOrphanV754Tests PASS\n";
    return 0;
}
