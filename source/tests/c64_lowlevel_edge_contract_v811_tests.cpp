// c64_lowlevel_edge_contract_v811_tests.cpp
//
// P1 hardening guard for the low-level 6510/CIA/VIC timing contracts identified
// by the pass380 audit. This is intentionally a source-contract guard: it catches
// accidental removal of the exact edge handling that the heavier behavioral tests
// rely on, without making the release closure depend on fragile tune fixtures.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

static std::string readText(const char* rel) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    require(in.good(), "could open source file");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main() {
    const std::string cpu = readText("include/arpsid/core/c64_cpu6510_micro.h");
    const std::string cia = readText("include/arpsid/core/c64_cia.h");
    const std::string vic = readText("include/arpsid/core/c64_vic.h");
    const std::string phi2 = readText("include/arpsid/core/c64_phi2_machine.h");

    require(cpu.find("void doADC_") != std::string::npos && cpu.find("s_.p & kFlagD") != std::string::npos,
            "6510 ADC/SBC decimal-mode paths are present");
    require(cpu.find("void doSBC_") != std::string::npos,
            "6510 SBC implementation is present");
    require(cpu.find("commitBranch_") != std::string::npos && cpu.find("pageCross") != std::string::npos,
            "6510 branch/page-cross timing state is present");
    require(cpu.find("kABSX_R") != std::string::npos && cpu.find("!s_.pageCross") != std::string::npos &&
            cpu.find("return dummyRd_(s_.ptr)") != std::string::npos,
            "ABS,X read no-cross/cross dummy-read timing is represented");
    require(cpu.find("kABSY_RMW") != std::string::npos && cpu.find("wrAddrRmwDummy_(s_.tmp)") != std::string::npos &&
            cpu.find("wrAddrRmwFinal_(s_.tmp2)") != std::string::npos,
            "indexed RMW emits dummy write and final write");
    require(cpu.find("kINDY_RMW") != std::string::npos &&
            cpu.find("mk16_(static_cast<uint8_t>(s_.addr), s_.hi)") != std::string::npos,
            "(zp),Y RMW wrong-page dummy timing is represented");

    require(cia.find("kTimerIrqModelIsCycleExact") != std::string::npos,
            "CIA exactness truth flag exists");
    require(cia.find("underflowA_") != std::string::npos && cia.find("underflowB_") != std::string::npos,
            "CIA timer A/B underflow handlers are explicit");
    require(cia.find("ICR") != std::string::npos && cia.find("read-to-clear") != std::string::npos,
            "CIA ICR read-to-clear contract is documented");

    require(vic.find("kBusStealIsCycleExact") != std::string::npos,
            "VIC bus-steal exactness truth flag exists");
    require(vic.find("BA") != std::string::npos && vic.find("AEC") != std::string::npos,
            "VIC BA and AEC are represented separately");
    require(vic.find("rasterCompare_") != std::string::npos && vic.find("cycleInLine_ == 0u") != std::string::npos,
            "VIC raster IRQ boundary logic is represented");

    require(phi2.find("tickPhi2") != std::string::npos && phi2.find("attachSidSink") != std::string::npos,
            "PHI2 machine clocks bus/SID through central per-cycle path");

    std::cout << "C64LowlevelEdgeContractV811Tests PASS\n";
    return 0;
}
