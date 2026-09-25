// c64_render_transaction_v871_tests.cpp
//
// Guards the v871 C64 render transaction closure:
// - PHI2 machine state is captured/restored by C64Runtime, not omitted.
// - C64RuntimeSidSink and external C64SidBridgeState snapshots include readback
//   and D418/timed-write state, not only primary regs.
// - Dirty-log wrap defers the 64 KB platform sync until commit so rollback
//   cannot overflow the bounded platform journal.
// - Play-call cap diagnostics are exposed instead of silent drops.

#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_sid_bridge.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <type_traits>

#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif

namespace {

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "c64_render_transaction_v871_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::string readSource(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_DIR) + "/" + rel, std::ios::binary);
    require(stream.good(), "source file must be readable");
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    using namespace ArpSID::C64;

    static_assert(std::is_trivially_copyable<C64Phi2Machine::Snapshot>::value,
                  "PHI2 snapshot must remain value-copyable");
    static_assert(std::is_trivially_copyable<C64RuntimeSidSink::Snapshot>::value,
                  "runtime SID sink snapshot must remain value-copyable");
    static_assert(std::is_trivially_copyable<C64SidBridgeState::Snapshot>::value,
                  "SID bridge snapshot must remain value-copyable");

    C64Phi2Machine machine;
    machine.powerOn();
    C64Phi2Machine::Snapshot phi2{};
    machine.captureSnapshot(phi2);
    machine.memory().pokeRam(0x1000u, 0x42u);
    machine.setPhi2Cycle(1234u);
    machine.restoreSnapshot(phi2);
    require(machine.phi2Cycle() == phi2.phi2, "PHI2 restore must restore cycle counter");
    require(machine.memory().peekRam(0x1000u) == phi2.mem.peekRam(0x1000u),
            "PHI2 restore must restore memory matrix");

    C64RuntimeSidSink sink;
    sink.sidWrite(0x18u, 0x0Fu, 99u);
    (void)sink.sidRead(0x1Bu, 100u);
    C64RuntimeSidSink::Snapshot sinkSnap{};
    sink.captureSnapshot(sinkSnap);
    sink.sidWrite(0x04u, 0x11u, 101u);
    sink.restoreSnapshot(sinkSnap);
    require(sink.regs[0x18u] == 0x0Fu, "runtime sink snapshot must restore regs");
    require(sink.lastCycle == 99u, "runtime sink snapshot must restore last cycle");
    require(sink.sidReadApproximationCount == sinkSnap.sidReadApproximationCount,
            "runtime sink snapshot must restore readback counters");

    C64SidBridgeState bridge;
    bridge.sidWrite(0x18u, 0x0Au, 7u);
    C64SidBridgeState::Snapshot bridgeSnap{};
    bridge.captureSnapshot(bridgeSnap);
    bridge.sidWrite(0x04u, 0x21u, 8u);
    bridge.restoreSnapshot(bridgeSnap);
    require(bridge.timedWriteCount == 1u, "bridge snapshot must restore timed write count");
    require(bridge.d418WriteCount == 1u && bridge.lastD418Value == 0x0Au,
            "bridge snapshot must restore D418 state");

    const std::string runtime = readSource("include/arpsid/core/c64_psid_runtime.h");
    const std::string kernel = readSource("source/au3/ArpSIDDSPKernel.hpp");
    require(contains(runtime, "phi2Machine_.captureSnapshot(renderTxPhi2Snapshot_)"),
            "C64Runtime transaction must capture PHI2 state");
    require(contains(runtime, "phi2Machine_.restoreSnapshot(renderTxPhi2Snapshot_)"),
            "C64Runtime transaction must restore PHI2 state");
    require(contains(runtime, "sink_.captureSnapshot(renderTxSinkSnapshot_)"),
            "C64Runtime transaction must capture runtime SID sink state");
    require(contains(runtime, "renderTxDeferredFullRamSync_ = true"),
            "dirty-log wrap must be deferred during active render transaction");
    require(contains(runtime, "syncPlatformRamFromPhi2Writes_(deferredStartPhi2)"),
            "deferred dirty-log wrap must be consumed on commit");
    require(contains(kernel, "c64RenderTransactionRollbackFailureCount_"),
            "kernel must count rollback proof failures");
    require(contains(kernel, "c64PlayCallCapHitCount_"),
            "kernel must count play-call cap hits");
    require(contains(kernel, "c64PlayCallsDroppedByCapLastBlock_"),
            "kernel must publish play calls dropped by cap");

    std::cout << "c64_render_transaction_v871_tests PASS\n";
    return 0;
}
