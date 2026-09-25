// sid808_restore_preload_drain_v803_tests.cpp
//
// P0 #4: a render-drained SID-808 factory restore must not leave the
// DrumEngineHostBridge kit queued until teardown/reset. The fix resolves the
// SID-808 kit table on the non-RT schedule side, transfers it through an
// ownership mailbox, and lets render activate that already-prepared table at a
// block boundary. The old queue/drain path remains only as a fail-safe for
// direct/unprepared render applies.

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

static std::string functionBody(const std::string& src, const std::string& signatureNeedle) {
    const size_t at = src.find(signatureNeedle);
    require(at != std::string::npos, "signature present");
    const size_t brace = src.find('{', at);
    require(brace != std::string::npos, "opening brace present");
    int depth = 0;
    size_t i = brace;
    for (; i < src.size(); ++i) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}') {
            if (--depth == 0) { ++i; break; }
        }
    }
    return src.substr(brace, i - brace);
}

int main() {
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string adapter = readFile("source/au3/ArpSIDDSPKernelAdapter.mm");
    const std::string vc = readFile("source/au3/ArpSIDViewController.mm");

    require(kernel.find("sid808PreloadedFactorySlotForRestore_v803_") != std::string::npos,
            "kernel tracks the scheduled SID-808 restore slot intent");
    require(kernel.find("preloadSid808FactorySlotForScheduledRestoreNonRealtime_(dst)") != std::string::npos,
            "schedulePendingStateRestore records SID-808 bridge slot intent on the non-RT producer side");
    const std::string preloadBody = functionBody(kernel, "preloadSid808FactorySlotForScheduledRestoreNonRealtime_(const SidStateRootV1& root");
    require(preloadBody.find("drumEngineBridge_.loadFactorySlot") == std::string::npos,
            "preload helper must not mutate the bridge before mailbox ownership settles");
    require(preloadBody.find("sid808PreloadedFactorySlotForRestore_v803_.store(slot") != std::string::npos,
            "preload helper records the intended bridge slot");
    require(preloadBody.find("factorySid808ResolvedKitForSlot(slot)") != std::string::npos,
            "preload helper resolves the SID-808 kit table off the render thread");
    require(preloadBody.find("sid808PreparedBridgeSlotMailbox_.publish()") != std::string::npos,
            "preload helper publishes the prepared bridge kit through ownership mailbox");

    const std::string applyBody = functionBody(kernel, "applyStateRootCanonical(SidStateRootV1& root");
    require(applyBody.find("preloadedSlot == slotInt") != std::string::npos,
            "render apply recognizes a matching non-RT SID-808 preload");
    require(applyBody.find("applyPreparedSid808BridgeSlotRT_(slotInt)") != std::string::npos,
            "render apply activates the prepared SID-808 kit without the non-RT loader");
    require(applyBody.find("queueSlotLoadNonRealtime(slotInt)") != std::string::npos,
            "render apply still has a fail-safe RT-safe queue for unmatched/direct render applies");
    require(applyBody.find("} else {\n                    (void)drumEngineBridge_.loadFactorySlot(slotInt);") != std::string::npos,
            "non-render apply still loads the bridge slot directly off the audio thread");

    require(kernel.find("bool drainQueuedDrumBridgeSlotLoadNonRealtime() noexcept") != std::string::npos,
            "kernel exposes explicit non-RT fail-safe drain");
    require(adapter.find("- (BOOL)drainQueuedDrumBridgeSlotLoadNonRealtime") != std::string::npos,
            "adapter exposes non-RT fail-safe drain to GUI/lifecycle code");
    require(vc.find("@selector(drainQueuedDrumBridgeSlotLoadNonRealtime)") != std::string::npos,
            "GUI poll services the non-RT fail-safe drain when an editor exists");

    std::cout << "Sid808RestorePreloadDrainV803Tests PASS\n";
    return 0;
}
