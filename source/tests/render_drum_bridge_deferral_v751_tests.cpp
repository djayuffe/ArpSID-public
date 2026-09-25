// render_drum_bridge_deferral_v751_tests.cpp
//
// audit P0-4: a render-drained state restore must NOT call the non-realtime
// DrumEngineHostBridge::loadFactorySlot() on the audio thread. The render path
// now QUEUES the slot (queueSlotLoadNonRealtime — an RT-safe atomic store) and a
// non-realtime drain (teardownReset → applyQueuedSlotNonRealtime) performs the
// actual load off the render thread.
//
// Part 1 (behavioral): proves the bridge's queue/drain mechanism the fix relies on
//   - queueSlotLoadNonRealtime() does NOT load (no engine/router mutation);
//   - applyQueuedSlotNonRealtime() performs exactly the deferred load;
//   - draining when nothing is queued is a no-op.
// Part 2 (guard, audit P0-4 "prove render cannot call the non-RT loader"): the
//   kernel's render apply branch uses queueSlotLoadNonRealtime, and the non-RT
//   drain lives in the non-realtime teardownReset path.

#include "arpsid/engines/drum_engine_host_bridge.h"

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

// Find the body of applyStateRootCanonical( ... ) so guard checks are scoped to it.
static std::string functionBody(const std::string& src, const std::string& signatureNeedle) {
    const size_t at = src.find(signatureNeedle);
    require(at != std::string::npos, "signature present in source");
    size_t brace = src.find('{', at);
    require(brace != std::string::npos, "opening brace present");
    int depth = 0; size_t i = brace;
    for (; i < src.size(); ++i) {
        if (src[i] == '{') ++depth;
        else if (src[i] == '}') { if (--depth == 0) { ++i; break; } }
    }
    return src.substr(brace, i - brace);
}

static void testBridgeQueueDrain() {
    using namespace ArpSID;
    DrumEngineHostBridge bridge;

    // A SID808 factory slot is normally loadable directly (the non-RT path).
    // Through the QUEUE, it must NOT load until the non-RT drain runs.
    bridge.resetLoadDiagnostics();
    const auto before = bridge.loadDiagnostics();
    bridge.queueSlotLoadNonRealtime(120);
    require(bridge.hasPendingSlotLoad(), "queueSlotLoadNonRealtime marks a pending load");
    const auto afterQueue = bridge.loadDiagnostics();
    require(afterQueue.slotLoadCount == before.slotLoadCount,
            "queueSlotLoadNonRealtime performs NO load (no engine/router mutation)");

    // Non-RT drain performs exactly the deferred load.
    const bool applied = bridge.applyQueuedSlotNonRealtime();
    require(applied, "applyQueuedSlotNonRealtime applies the queued slot");
    require(!bridge.hasPendingSlotLoad(), "no pending load remains after drain");
    const auto afterDrain = bridge.loadDiagnostics();
    require(afterDrain.slotLoadCount == before.slotLoadCount + 1u,
            "drain performs exactly one deferred load");
    require(afterDrain.sid808LoadCount == before.sid808LoadCount + 1u,
            "deferred SID808 slot routed to the SID808 engine");

    // Draining with nothing queued is a harmless no-op.
    require(!bridge.applyQueuedSlotNonRealtime(), "drain with empty queue is a no-op");
    require(bridge.loadDiagnostics().slotLoadCount == afterDrain.slotLoadCount,
            "no-op drain does not load");

    // Re-queueing the newest slot wins (latest-value semantics of the pending slot).
    bridge.queueSlotLoadNonRealtime(121);
    bridge.queueSlotLoadNonRealtime(124);
    require(bridge.applyQueuedSlotNonRealtime(), "latest queued slot is applied");
    require(!bridge.hasPendingSlotLoad(), "queue cleared after applying latest");
}

static void testRenderPathGuard() {
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");

    // The render apply branch must queue, never call the non-RT loader inline.
    const std::string applyBody =
        functionBody(kernel, "applyStateRootCanonical(SidStateRootV1& root");
    require(applyBody.find("queueSlotLoadNonRealtime") != std::string::npos,
            "render apply path queues the drum-bridge slot (RT-safe)");
    require(applyBody.find("onRenderThread") != std::string::npos,
            "apply path branches on onRenderThread for the drum-bridge slot");

    // The actual non-RT load must be reached from the non-realtime teardown path.
    const std::string teardownBody = functionBody(kernel, "void teardownReset() noexcept");
    require(teardownBody.find("applyQueuedSlotNonRealtime") != std::string::npos,
            "non-realtime teardownReset drains the queued drum-bridge slot");

    // The render drain must enter the v814 RT-only prepared-root wrapper, which
    // is the single place that hands onRenderThread=true into the canonical apply.
    require(kernel.find("applyPreparedStateRootRT_(*root)") != std::string::npos,
            "render mailbox drain enters the prepared-root RT apply wrapper");
    require(kernel.find("applyStateRootCanonical(preparedRoot, /*onRenderThread=*/true)") != std::string::npos,
            "prepared-root RT wrapper marks the apply as on-render-thread");
}

int main() {
    testBridgeQueueDrain();
    testRenderPathGuard();
    std::cout << "RenderDrumBridgeDeferralV751Tests PASS\n";
    return 0;
}
