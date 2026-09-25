// c64_scope_telemetry_fix_closure_v744_tests.cpp
// v744: Regression guard for the 0.0.679 C64 scope/telemetry authority fixes.
//
// This is intentionally source-level because the AU kernel internals are private
// to the wrapper build. It locks the exact failure class that broke C64 scopes:
// audio authority, SIDCORE shadow, bridge register image, C64 bus scope, PAL/NTSC
// telemetry, heavy snapshot chronology, and C64 State tab update cadence must not
// split into separate stale authorities again.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

static std::string slurp(const char* relative) {
    std::ifstream in(std::string(ARPSID_SOURCE_ROOT) + "/" + relative, std::ios::binary);
    require(static_cast<bool>(in), relative);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

static bool containsInOrder(const std::string& haystack,
                            const std::string& a,
                            const std::string& b,
                            const std::string& c = std::string()) {
    const size_t pa = haystack.find(a);
    if (pa == std::string::npos) return false;
    const size_t pb = haystack.find(b, pa + a.size());
    if (pb == std::string::npos) return false;
    if (c.empty()) return true;
    return haystack.find(c, pb + b.size()) != std::string::npos;
}

int main() {
    const std::string kernel = slurp("source/au3/ArpSIDDSPKernel.hpp");
    const std::string vc = slurp("source/au3/ArpSIDViewController.mm");
    const std::string cmake = slurp("CMakeLists.txt");

    require(kernel.find("void reseedSidEnginesFromPlayerImage_(ArpSID::C64::C64Runtime* player) noexcept") != std::string::npos,
            "per-block C64 SID reseed helper exists");
    require(containsInOrder(kernel,
                            "sreg_().write(r, regs[r]);",
                            "mirrorSidCoreShadowWrite_(r, regs[r], 0u, 0u);",
                            "c64SidBridge_.regs[r] = regs[r];"),
            "primary C64 reseed mirrors audio authority into SIDCORE shadow and bridge image");
    require(kernel.find("c64SidBridge_.regsByChip[0][r] = regs[r]") != std::string::npos,
            "primary C64 reseed mirrors primary regsByChip image");
    require(containsInOrder(kernel,
                            "const uint8_t v = banks[ch][r];",
                            "if (SidRegisterEngine* e = sregForChip_(ch)) e->write(r, v);",
                            "c64SidBridge_.regsByChip[ch][r] = v;"),
            "secondary C64 reseed mirrors multi-SID banks into engine and bridge image");

    require(containsInOrder(kernel,
                            "reseedSidEnginesFromPlayerImage_(player);",
                            "const double clockHz = std::max"),
            "C64 reseed happens before play cadence/render loop");

    require(kernel.find("Publish real observed SID write activity to the C64 bus scope") != std::string::npos,
            "render loop publishes real timed SID writes to C64 bus scope");
    require(containsInOrder(kernel,
                            "const bool busScopeWriteSample =",
                            "publishC64RealtimeBusScope_(platform.readOpenBus()",
                            "w.reg"),
            "C64 bus scope uses timed write register/value, not only block latch fallback");
    require(kernel.find("writeCount <= static_cast<uint32_t>(kC64BusScopeLen)") != std::string::npos,
            "C64 bus scope timed-write publishing is bounded by the scope ring length");

    // v893 refresh: the v882 closure factored this into
    // resolveC64ProjectionMirrorPal_() — the live PSID/RSID runtime clock is
    // still consulted first, with the synth SID-clock policy as fallback.
    require(containsInOrder(kernel,
                            "const ArpSID::C64::C64Runtime* psidLive = c64PsidLive_.load",
                            "if (liveClock == ArpSID::C64::kPalPhi2Hz) return true;",
                            "return fallbackPal();"),
            "PAL/NTSC C64 telemetry uses loaded PSID/RSID runtime clock before synth fallback");
    require(kernel.find("return !(runtimePhysicalSidClockHz() > 1000000.0);") != std::string::npos,
            "synth fallback still derives PAL/NTSC from the runtime SID clock");

    require(containsInOrder(kernel,
                            "} else if (mode == SidRuntimeRenderMode::C64Psid) {",
                            "c64PsidLive_.load(std::memory_order_acquire)",
                            "p->platform().sidRegisterImage().data()"),
            "C64 mode telemetrySidRegs uses C64Runtime SID image authority");

    require(containsInOrder(kernel,
                            "const uint32_t liveWp = c64BusScopeLive_.writePos",
                            "const size_t src = (liveWp + i)",
                            "snap.openBusScopeWritePos = 0u;"),
            "heavy C64 snapshot copies bus-scope ring chronologically from live write pointer");
    require(kernel.find("c64BusScopeLive_.sidWritePulse") != std::string::npos &&
            kernel.find("c64BusScopeLive_.phi2") != std::string::npos &&
            kernel.find("c64BusScopeLive_.irqDma") != std::string::npos &&
            kernel.find("telemetryC64BusScopeTriple_") != std::string::npos,
            "C64 bus telemetry exposes decoded write/PHI2/IRQ lanes");

    require(vc.find("if(_c64BusMetalBackdropView && (wantsC64Tab || wantsC64StateTab))") != std::string::npos,
            "C64 State tab updates C64 bus metal backdrop");
    require(vc.find("const BOOL wantsCoreScopes = wantsSidRegScopes || wantsOptionsUI || wantsC64Tab || wantsC64StateTab;") != std::string::npos,
            "C64 State tab requests core scope payloads");

    require(cmake.find("arpsid_c64_scope_telemetry_fix_closure_v744_tests") != std::string::npos,
            "v744 regression guard is wired into CMake");

    std::cout << "C64ScopeTelemetryFixClosureV744Tests PASS\n";
    return 0;
}
