// state_apply_ownership_split_v814_tests.cpp
//
// Guard for P1-11/P1-12: state-root apply ownership must be split into
// one non-RT preparation authority and one render-side prepared-root apply
// authority. This prevents future changes from reintroducing canonicalize /
// factory-load / semantic-vector work directly in the render mailbox drain.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string readFile(const char* rel) {
    const std::string path = std::string(ARPSID_SOURCE_ROOT) + "/" + rel;
    std::ifstream in(path, std::ios::binary);
    require(in.good(), "could open source file");
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static std::string between(const std::string& s, const std::string& a, const std::string& b) {
    const auto begin = s.find(a);
    require(begin != std::string::npos, "begin marker found");
    const auto end = s.find(b, begin);
    require(end != std::string::npos, "end marker found");
    return s.substr(begin, end - begin);
}

int main() {
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");

    require(kernel.find("void prepareStateRootForApplyNonRealtime_(SidStateRootV1& dst)") != std::string::npos,
            "non-RT state-root preparation helper exists");
    require(kernel.find("void applyPreparedStateRootRT_(SidStateRootV1& preparedRoot) noexcept") != std::string::npos,
            "render prepared-root apply helper exists");

    const std::string scheduleBody = between(kernel,
        "void schedulePendingStateRestore(const SidStateRootV1& root)",
        "void drainPendingStateRestore_() noexcept");
    const std::string drainBody = between(kernel,
        "void drainPendingStateRestore_() noexcept",
        "void prepareStateRootForApplyNonRealtime_(SidStateRootV1& dst)");
    const std::string prepareBody = between(kernel,
        "void prepareStateRootForApplyNonRealtime_(SidStateRootV1& dst)",
        "void applyPreparedStateRootRT_(SidStateRootV1& preparedRoot) noexcept");
    const std::string applyRtBody = between(kernel,
        "void applyPreparedStateRootRT_(SidStateRootV1& preparedRoot) noexcept",
        "// onRenderThread: true when invoked from the audio thread via");
    const std::string applyCanonicalBody = between(kernel,
        "void applyStateRootCanonical(SidStateRootV1& root, bool onRenderThread = false) noexcept",
        "//──────────────────────────────────────────────────────\n    // MIDI — render thread only");

    require(scheduleBody.find("prepareStateRootForApplyNonRealtime_(dst);") != std::string::npos,
            "schedule path delegates all preparation to non-RT helper");
    require(scheduleBody.find("sidCanonicalizeStateRootForApply(dst)") == std::string::npos,
            "schedule body does not inline canonicalization outside helper");
    require(scheduleBody.find("preloadSid808FactorySlotForScheduledRestoreNonRealtime_(dst)") == std::string::npos,
            "schedule body does not inline SID-808 preload outside helper");

    require(prepareBody.find("sidCanonicalizeStateRootForApply(dst)") != std::string::npos,
            "non-RT helper canonicalizes state root");
    require(prepareBody.find("preloadSid808FactorySlotForScheduledRestoreNonRealtime_(dst)") != std::string::npos,
            "non-RT helper owns SID-808 preload");

    require(drainBody.find("applyPreparedStateRootRT_(*root);") != std::string::npos,
            "render drain calls prepared-root RT helper");
    require(drainBody.find("applyStateRootCanonical(*root") == std::string::npos,
            "render drain does not bypass prepared-root helper");
    require(drainBody.find("sidCanonicalizeStateRootForApply") == std::string::npos,
            "render drain does not canonicalize");
    require(drainBody.find("preloadSid808FactorySlotForScheduledRestoreNonRealtime_") == std::string::npos,
            "render drain does not preload SID-808");

    require(applyRtBody.find("applyStateRootCanonical(preparedRoot, /*onRenderThread=*/true);") != std::string::npos,
            "RT helper invokes canonical apply in render mode");
    require(applyRtBody.find("sidCanonicalizeStateRootForApply") == std::string::npos,
            "RT helper does not canonicalize");
    require(applyRtBody.find("preloadSid808FactorySlotForScheduledRestoreNonRealtime_") == std::string::npos,
            "RT helper does not call non-RT SID-808 preload");

    require(applyCanonicalBody.find("const SidStateRootV1& appliedRoot = runtimeModel_.stateRoot();") != std::string::npos,
            "canonical apply reads post-swap applied root");
    require(applyCanonicalBody.find("sidStateRootParamValueFromHydratedValuesRT(appliedRoot") != std::string::npos,
            "canonical apply uses hydrated applied-root values");
    require(applyCanonicalBody.find("restorePrimarySidEnvelopeRuntimeState(root.patch.sid_runtime)") == std::string::npos,
            "canonical apply never restores SID runtime from swapped-out local root");

    std::cout << "StateApplyOwnershipSplitV814Tests PASS\n";
    return 0;
}
