// Copyright (C) 2024-2026 Ulf Bertilsson
// sidplay_register_engine_v867_tests.cpp
//
// v867 guards the C64 SIDPLAY register-engine fixes:
// - PW=$FFF is a comparator spike, not constant-low;
// - $D418 volume-DAC emulation is enabled by the C64 SIDPLAY path;
// - subphase queue overflow remains visible through telemetry.

#include "arpsid/engines/sid_register_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "sidplay_register_engine_v867_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

std::string readFile(const std::string& rel) {
    const std::string path = std::string(ARPSID_SOURCE_DIR) + "/" + rel;
    std::ifstream in(path);
    require(in.good(), ("missing source file: " + path).c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string sliceBetween(const std::string& text, const std::string& begin, const std::string& end) {
    const std::size_t b = text.find(begin);
    require(b != std::string::npos, "slice begin marker missing");
    const std::size_t e = text.find(end, b + begin.size());
    require(e != std::string::npos, "slice end marker missing");
    return text.substr(b, e - b);
}

float renderD418Peak(bool enableDac) {
    ArpSID::SidRegisterEngine sid;
    sid.prepare(48000.0);
    sid.writeSystemByte(0x02u);
    sid.setD418VolumeDacEmulation(enableDac);

    float l[256]{};
    float r[256]{};
    float peak = 0.0f;
    for (int i = 0; i < 256; ++i) {
        sid.write(0x18u, static_cast<std::uint8_t>((i & 1) ? 0x0Fu : 0x00u));
        sid.renderBlock(&l[i], &r[i], 1);
        peak = std::max(peak, std::fabs(l[i]));
        peak = std::max(peak, std::fabs(r[i]));
    }
    return peak;
}

void testPulseComparatorEdges() {
    using namespace ArpSID;
    require(sidRegisterPulseComparator12(0x000u, 0x000u, SIDModel::MOS8580) == 0x0FFFu,
            "PW=$000 remains constant-high");
    require(sidRegisterPulseComparator12(0x7FFu, 0x800u, SIDModel::MOS8580) == 0x0000u,
            "PW=$800 is low before the midpoint comparator");
    require(sidRegisterPulseComparator12(0x800u, 0x800u, SIDModel::MOS8580) == 0x0FFFu,
            "PW=$800 is high at the midpoint comparator");
    require(sidRegisterPulseComparator12(0x0FFEu, 0x0FFFu, SIDModel::MOS8580) == 0x0000u,
            "PW=$FFF is low before the final comparator step");
    require(sidRegisterPulseComparator12(0x0FFFu, 0x0FFFu, SIDModel::MOS8580) == 0x0FFFu,
            "PW=$FFF has the final one-step high spike");
    require(sidRegisterPulseComparator12(0x0FFFu, 0x0FFFu, SIDModel::MOS6581) == 0x0FFFu,
            "PW=$FFF spike is retained under 6581 edge bias");
}

void testD418DacAudibilityAndC64Wiring() {
    const float disabled = renderD418Peak(false);
    const float enabled = renderD418Peak(true);
    require(disabled < 1.0e-6f, "disabled D418 DAC must not invent audio without voices");
    require(enabled > 1.0e-5f, "enabled D418 DAC must make volume-register digi audible");

    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string c64Render = sliceBetween(kernel,
        "bool renderC64PsidBlockIfActive_",
        "// Fix #7: chunk wrapper for huge offline blocks");
    require(c64Render.find("setD418VolumeDacEmulation(true)") != std::string::npos,
            "C64 SIDPLAY render path must enable D418 volume-DAC emulation on register engines");

    const std::string sidRegisterEngine = readFile("include/arpsid/engines/sid_register_engine.h");
    require(sidRegisterEngine.find("if (rawPw == 0xFFFu) return 0x0000u;") == std::string::npos,
            "SidRegisterEngine must not keep the old PW=$FFF constant-low special case");
}

void testSubphaseOverflowTelemetry() {
    ArpSID::SidRegisterEngine sid;
    sid.prepare(48000.0);
    sid.resetSubphaseWriteTelemetry();
    for (int i = 0; i < ArpSID::SidRegisterEngine::kMaxSubphaseWrites + 16; ++i) {
        sid.queueSubphaseWrite(static_cast<std::uint32_t>(i),
                               0u,
                               static_cast<std::uint8_t>(i % ArpSID::kSidLiveWritableRegCount),
                               static_cast<std::uint8_t>(i));
    }
    require(sid.subphaseWriteOverflowCount() > 0u,
            "subphase queue overflow must be visible to SIDPLAY diagnostics");
    require((sid.subphaseWriteCoalescedCount() + sid.subphaseWriteDroppedOldestCount()) > 0u,
            "subphase queue pressure must report coalesced or dropped writes");
}

} // namespace

int main() {
    testPulseComparatorEdges();
    testD418DacAudibilityAndC64Wiring();
    testSubphaseOverflowTelemetry();
    std::cout << "sidplay_register_engine_v867_tests PASS\n";
    return 0;
}
