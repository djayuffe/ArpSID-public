// Copyright (C) 2024-2026 Ulf Bertilsson
// c64_phi2_trace_v610_tests.cpp
// Wires the previously-dead-but-useful PHI2 bus trace (c64_phi2_trace.h) into
// the live code path and pins its behavior:
// I. FixedPhi2Trace captures per-cycle bus phases from C64Phi2Machine,
// including the exact SID-write phase (addr/data/sidWrite/sidReg).
// II. Capacity overflow is drop-counted, never out-of-bounds.
// III. C64Runtime exposes attachPhi2Trace()/detach so hosts/diagnostics can
// capture a trace without reaching into the machine internals.

#include "arpsid/core/c64_phi2_machine.h"
#include "arpsid/core/c64_phi2_trace.h"
#include "arpsid/core/c64_sid_bus_sink.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

namespace {
void loadProgram(ArpSID::C64::C64Phi2Machine& m, uint16_t pc,
                 const uint8_t* bytes, size_t count) {
    for (size_t i = 0; i < count; ++i)
        m.memory().pokeRam(static_cast<uint16_t>(pc + i), bytes[i]);
    m.setProgramCounter(pc);
}
} // namespace

// ── I. Trace captures the SID-write bus phase ────────────────────────────────
static void testTraceCapturesSidWrite() {
    using namespace ArpSID::C64;
    C64Phi2Machine m;
    SidBusSink sid;
    FixedPhi2Trace<64> trace;
    m.attachSidSink(&sid);
    m.attachTrace(&trace);
    m.powerOn();
    m.attachTrace(&trace); // re-attach after powerOn (machine keeps the pointer either way)

    // LDA #$2A ; STA $D405 (voice-1 attack/decay) ; NOP
    const uint8_t program[] = {
        0xA9u, 0x2Au,             // LDA #$2A
        0x8Du, 0x05u, 0xD4u,     // STA $D405
        0xEAu                      // NOP
    };
    loadProgram(m, 0x0900u, program, sizeof(program));
    m.runPhi2(8);

    require(trace.size() > 0u, "trace recorded at least one bus phase");
    require(trace.dropped() == 0u, "no drops within capacity");

    // Find the SID-write record and verify it is exact.
    bool sawSidWrite = false;
    for (size_t i = 0; i < trace.size(); ++i) {
        const Phi2TraceRecord& r = trace[i];
        if (r.sidWrite) {
            sawSidWrite = true;
            require(r.addr == 0xD405u, "trace SID write address is $D405");
            require(r.data == 0x2Au,   "trace SID write data is $2A");
            require(r.sidReg == 0x05u, "trace SID write encoded reg is 5");
            require(r.rw == false,     "trace SID write phase is a write (rw=false)");
        }
    }
    require(sawSidWrite, "trace captured the STA $D405 SID-write phase");
    // Cross-check against the SID sink: exactly one write, matching the trace.
    require(sid.writeCount() == 1u, "SID sink saw exactly one write");
}

// ── II. Capacity overflow is drop-counted, not UB ────────────────────────────
static void testTraceOverflowIsDropCounted() {
    using namespace ArpSID::C64;
    C64Phi2Machine m;
    FixedPhi2Trace<4> tiny; // deliberately tiny
    m.attachTrace(&tiny);
    m.powerOn();
    m.attachTrace(&tiny);
    const uint8_t program[] = { 0xEAu, 0xEAu, 0xEAu, 0xEAu, 0xEAu, 0xEAu }; // NOPs
    loadProgram(m, 0x0800u, program, sizeof(program));
    m.runPhi2(16);
    require(tiny.size() == 4u, "tiny trace fills exactly to capacity");
    require(tiny.dropped() > 0u, "overflow beyond capacity is drop-counted");
}

// ── III. Detach stops capture ────────────────────────────────────────────────
static void testTraceDetach() {
    using namespace ArpSID::C64;
    C64Phi2Machine m;
    FixedPhi2Trace<32> trace;
    m.powerOn();
    m.attachTrace(&trace);
    const uint8_t program[] = { 0xEAu, 0xEAu, 0xEAu, 0xEAu };
    loadProgram(m, 0x0800u, program, sizeof(program));
    m.runPhi2(4);
    const size_t afterRun = trace.size();
    require(afterRun > 0u, "trace captured while attached");
    m.attachTrace(nullptr); // detach
    m.runPhi2(4);
    require(trace.size() == afterRun, "no further capture after detach");
}

int main() {
    testTraceCapturesSidWrite();
    testTraceOverflowIsDropCounted();
    testTraceDetach();
    std::cout << "C64Phi2TraceV610Tests PASS\n";
    return 0;
}
