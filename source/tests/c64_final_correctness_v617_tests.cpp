#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/core/c64_memory_matrix.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
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

static std::string readText(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!stream) {
        std::cerr << "FAIL: missing " << rel << "\n";
        std::exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

int main() {
    using namespace ArpSID::C64;

    C64Phi2Machine m;
    m.powerOn();

    Phi2BusPhase p0{};
    m.memory().cpuWrite(10, 0x1000, 0xAA, p0, false);
    Phi2BusPhase p1{};
    m.memory().cpuWrite(11, 0x1001, 0xBB, p1, true);
    require(m.memory().dirtyWriteCount() == 2u, "dirty write count records all PHI2 writes");
    require(m.memory().dirtyWriteLogSize() == 2u, "dirty write log stores all recent writes");
    require(m.memory().dirtyWriteAt(0).address == 0x1000u, "dirty write log preserves first address");
    require(m.memory().dirtyWriteAt(1).rmwDummy, "dirty write log preserves RMW dummy semantic");

    m.tickPhi2();
    require(m.diagnostics().phi2DirtyWrites >= 2u, "diagnostics expose PHI2 dirty writes");


    // Ring wrap must return chronological order, not raw physical slot order.
    MemoryMatrix mem;
    mem.powerOn(true);
    Phi2BusPhase ph{};
    for (size_t i = 0; i < MemoryMatrix::kDirtyWriteLogSize + 3u; ++i) {
        mem.cpuWrite(1000u + i, static_cast<uint16_t>(0x2000u + (i & 0x0FFFu)), static_cast<uint8_t>(i), ph, (i & 1u) != 0u);
    }
    require(mem.dirtyWriteLogSize() == MemoryMatrix::kDirtyWriteLogSize,
            "dirty write log saturates at ring capacity");
    const auto oldest = mem.dirtyWriteAt(0);
    const auto newest = mem.dirtyWriteAt(MemoryMatrix::kDirtyWriteLogSize - 1u);
    require(oldest.phi2 == 1003u, "dirty write ring returns chronological oldest entry after wrap");
    require(newest.phi2 == 1000u + MemoryMatrix::kDirtyWriteLogSize + 2u,
            "dirty write ring returns chronological newest entry after wrap");
    require(mem.dirtyWriteLogWrapped(), "dirty write ring exposes wrapped state before consumption");
    require(mem.consumeDirtyWriteLogWrapped(), "dirty write ring wrapped state is consumable");
    require(!mem.dirtyWriteLogWrapped(), "dirty write ring wrapped state is not sticky after consumption");
    require(mem.dirtyWriteLogSize() == 3u,
            "dirty write ring preserves post-wrap head entries after consuming the wrapped flag");
    mem.clearDirtyWriteLog();
    require(mem.dirtyWriteLogSize() == 0u && !mem.dirtyWriteLogWrapped(),
            "dirty write log can be cleared after platform RAM sync consumes it");

    // RMW bus event semantic distinction: direct non-RMW writes are not final-RMW events.
    C64Phi2Machine m2;
    m2.powerOn();
    Phi2BusPhase nrm{};
    m2.memory().cpuWrite(20, 0x1002, 0xCC, nrm, false);
    require(nrm.rmwEventKind == RmwBusEventKind::None,
            "normal direct memory write is not marked as RMW final write");
    Phi2BusPhase rmw{};
    m2.memory().cpuWrite(21, 0x1003, 0xDD, rmw, true);
    require(rmw.rmwDummyWrite, "direct RMW dummy write carries rmwDummyWrite");

    C64RuntimeSidSink sidSink;
    // SID read approximation should return open bus for write-only registers,
    // not a mirrored previous write value.
    sidSink.sidWrite(0x05u, 0x77u, 1u);
    require(sidSink.sidReadWithOpenBus(0x05u, 2u, 0xA5u) == 0xA5u,
            "write-only SID read returns supplied open bus");
    sidSink.sidWrite(0x0Eu, 0x12u, 3u);
    sidSink.sidWrite(0x0Fu, 0x34u, 4u);
    sidSink.sidWrite(0x12u, 0x41u, 5u);
    const auto osc = sidSink.sidReadWithOpenBus(0x1Bu, 6u, 0x00u);
    const auto env = sidSink.sidReadWithOpenBus(0x1Cu, 7u, 0x00u);
    require(osc != 0x12u && osc != 0x34u, "OSC3 read is deterministic state, not raw register mirror");
    require(env != 0x41u, "ENV3 read is deterministic state, not raw control-register mirror");





    // Reusing the same Phi2BusPhase object must not leak prior write/RMW/SID
    // metadata into a later read.
    MemoryMatrix readBoundaryMem;
    readBoundaryMem.powerOn(true);
    Phi2BusPhase reusedReadPhase{};
    readBoundaryMem.cpuWrite(400, 0xD400, 0x33, reusedReadPhase, true);
    require(reusedReadPhase.rmwEventKind == RmwBusEventKind::DummyWriteOldValue,
            "read-boundary setup has RMW dummy state");
    const auto readVal = readBoundaryMem.cpuRead(401, 0x1000, reusedReadPhase);
    (void)readVal;
    require(reusedReadPhase.access == BusAccess::Read,
            "read boundary records read access");
    require(!reusedReadPhase.sidWrite,
            "read boundary clears stale sidWrite");
    require(!reusedReadPhase.rmwDummyWrite,
            "read boundary clears stale rmwDummyWrite");
    require(reusedReadPhase.rmwEventKind == RmwBusEventKind::None,
            "read boundary clears stale RMW event kind");
    require(!reusedReadPhase.openBusSource,
            "normal RAM read clears stale openBusSource");

    // Reusing the same Phi2BusPhase object must not leak RMW metadata from a
    // previous write into a later normal write.
    MemoryMatrix phaseBoundaryMem;
    phaseBoundaryMem.powerOn(true);
    Phi2BusPhase reusedPhase{};
    phaseBoundaryMem.cpuWrite(200, 0x3000, 0x11, reusedPhase, true);
    require(reusedPhase.rmwEventKind == RmwBusEventKind::DummyWriteOldValue,
            "reused phase first write records RMW dummy");
    phaseBoundaryMem.cpuWrite(201, 0x3001, 0x22, reusedPhase, false);
    require(reusedPhase.rmwEventKind == RmwBusEventKind::None,
            "normal write clears stale RMW event kind on reused phase");
    require(!reusedPhase.rmwDummyWrite,
            "normal write clears stale rmwDummyWrite on reused phase");
    const auto boundaryLast = phaseBoundaryMem.dirtyWriteAt(phaseBoundaryMem.dirtyWriteLogSize() - 1u);
    require(boundaryLast.rmwEventKind == RmwBusEventKind::None,
            "dirty log does not inherit stale RMW event from reused phase");

    // Full RMW bus-event semantic distinction at the MemoryMatrix boundary:
    // explicit dummy-old and final-new events are logged distinctly.
    MemoryMatrix rmwEventMem;
    rmwEventMem.powerOn(true);
    Phi2BusPhase rmwDummyPhase{};
    rmwEventMem.cpuWrite(300, 0xD400, 0x81u, rmwDummyPhase, true);
    Phi2BusPhase rmwFinalPhase{};
    rmwFinalPhase.rmwFinalWrite = true;
    rmwFinalPhase.rmwEventKind = RmwBusEventKind::FinalWriteNewValue;
    rmwEventMem.cpuWrite(301, 0xD400, 0x02u, rmwFinalPhase, false);
    bool sawDummy = false;
    bool sawFinal = false;
    for (size_t i = 0; i < rmwEventMem.dirtyWriteLogSize(); ++i) {
        const auto e = rmwEventMem.dirtyWriteAt(i);
        if (e.address == 0xD400u && e.rmwEventKind == RmwBusEventKind::DummyWriteOldValue) sawDummy = true;
        if (e.address == 0xD400u && e.rmwEventKind == RmwBusEventKind::FinalWriteNewValue) sawFinal = true;
    }
    require(sawDummy, "RMW SID write logs dummy old-value bus event");
    require(sawFinal, "RMW SID write logs final new-value bus event");

    C64SidBridgeState bridgeState;
    bridgeState.reset();
    bridgeState.sidWrite(0x05u, 0x88u, 8u);
    require(bridgeState.sidRead(0x05u, 9u) == 0xFFu,
            "C64SidBridgeState write-only SID read does not return register mirror");
    bridgeState.sidWrite(0x0Eu, 0x13u, 10u);
    bridgeState.sidWrite(0x0Fu, 0x35u, 11u);
    bridgeState.sidWrite(0x12u, 0x41u, 12u);
    require(bridgeState.sidRead(0x1Bu, 13u) != 0x13u,
            "C64SidBridgeState OSC3 is not raw frequency register mirror");
    require(bridgeState.sidRead(0x1Cu, 14u) != 0x41u,
            "C64SidBridgeState ENV3 is not raw control register mirror");

    const std::string runtimeText = readText("include/arpsid/core/c64_psid_runtime.h");
    const auto capturePos = runtimeText.find("void capturePsidCiaPhi2Edges_");
    const auto nextFuncPos = runtimeText.find("C64PsidCiaRuntimeDriveSnapshot runPsidCiaPhi2PlaybackServiceTicks_", capturePos);
    require(capturePos != std::string::npos && nextFuncPos != std::string::npos,
            "source guard can locate PHI2 edge capture function");
    const std::string captureBody = runtimeText.substr(capturePos, nextFuncPos - capturePos);
    require(captureBody.find("dirtyWriteLogSize()") == std::string::npos &&
            captureBody.find("dirtyWriteAt(") == std::string::npos,
            "PHI2 edge capture must not scan the dirty-write log");
    require(captureBody.find("sink_.lastCycle >= startPhi2") != std::string::npos,
            "PHI2 edge capture observes SID writes through the O(1) SID sink");
    require(runtimeText.find("memory.clearDirtyWriteLog();") != std::string::npos,
            "platform RAM sync clears the dirty-write log after consuming it");

    std::cout << "C64FinalCorrectnessV617Tests PASS\n";
    return 0;
}
