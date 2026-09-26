// Copyright (C) 2024-2026 Ulf Bertilsson
// Regression guard for the v838 user-togglable "VIC-II fast" mode.
//
// VIC:FAST trades only VIC-II cycle-exactness (it skips the per-cycle
// sprite/badline matrix-DMA + bus-steal) to cut CPU during SID playback, while
// CPU/CIA/SID stay bit-exact. It must therefore:
//   1. default OFF everywhere (accurate playback is the shipped default),
//   2. stay wired end-to-end: Phi2 config -> machine -> PSID runtime -> kernel
//      -> adapter control command -> GUI toggle button,
//   3. never reach for the legacy instruction-atomic engine (that path is
//      locked out by ARPSID_C64_PHYSICAL_ONLY; VIC-fast is policy-safe because
//      it only changes how the PHI2 machine ticks the VIC).
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readText(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!stream) { std::cerr << "FAIL: missing " << rel << '\n'; std::exit(1); }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

int main() {
    // 1. PHI2 config: default OFF.
    const std::string types = readText("include/arpsid/core/c64_phi2_types.h");
    require(types.find("bool vicFast = false;") != std::string::npos,
            "Phi2MachineConfig::vicFast must exist and default to false (accurate)");

    // 2. PHI2 machine: live setter + the actual fast/accurate tick branch.
    const std::string machine = readText("include/arpsid/core/c64_phi2_machine.h");
    require(machine.find("void setVicFast(bool fast) noexcept") != std::string::npos,
            "C64Phi2Machine must expose setVicFast()");
    require(machine.find("bool vicFast() const noexcept") != std::string::npos,
            "C64Phi2Machine must expose vicFast()");
    require(machine.find("if (cfg_.vicFast) (void)vic_.stepHalfCycles(2u); else (void)vic_.tick();")
                != std::string::npos,
            "tickPhi2 must branch VIC-fast -> stepHalfCycles(2) / accurate -> tick()");
    require(machine.find("const bool aecHigh = cfg_.vicFast || !cfg_.enableVicBusSteal || vic_.aec();")
                != std::string::npos,
            "VIC-fast must force AEC high (CPU keeps the bus, no badline/sprite steal)");

    // 3. PSID runtime: forwards the toggle into the PHI2 machine, nothing else.
    const std::string runtime = readText("include/arpsid/core/c64_psid_runtime.h");
    require(runtime.find("void setVicFast(bool fast) noexcept { phi2Machine_.setVicFast(fast); }")
                != std::string::npos,
            "PSID runtime must forward setVicFast() to the PHI2 machine");
    // VIC-fast must NOT reintroduce the legacy instruction-atomic playback path.
    require(runtime.find("if (fastPlayback_)") == std::string::npos,
            "VIC-fast must not resurrect the instruction-atomic fastPlayback_ routing");

    // 4. DSP kernel: atomic default ON (v873: main CPU-time saving for smooth realtime
    //    playback; CPU/CIA/SID stay bit-exact) + applied to the live player each block.
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");
    require(kernel.find("std::atomic<bool> c64VicFast_{true};") != std::string::npos,
            "kernel c64VicFast_ must default to true (v873: CPU-saving VIC-fast on by default)");
    require(kernel.find("void setC64VicFast(bool on) noexcept") != std::string::npos,
            "kernel must expose setC64VicFast()");
    require(kernel.find("player->setVicFast(c64VicFast_.load(std::memory_order_relaxed));")
                != std::string::npos,
            "kernel must push the VIC-fast flag into the live PSID player per block");

    // 5. Adapter control-hub: command 6 = ON, 7 = OFF.
    const std::string adapter = readText("source/au3/ArpSIDDSPKernelAdapter.mm");
    require(adapter.find("case 6: _kernel->setC64VicFast(true)") != std::string::npos,
            "control command 6 must enable VIC-fast");
    require(adapter.find("case 7: _kernel->setC64VicFast(false)") != std::string::npos,
            "control command 7 must disable VIC-fast (accurate)");

    // 6. GUI: button ships as VIC:FAST (v873 default on) and toggles command 6/7.
    const std::string vc = readText("source/au3/ArpSIDViewController.mm");
    require(vc.find("@{@\"t\":@\"VIC:FAST\", @\"a\":@\"_c64ToggleVicFast:\"}") != std::string::npos,
            "GUI must ship the VIC-fast button labelled VIC:FAST (v873 default on)");
    require(vc.find("[self _performC64ControlHubCommand:(_c64VicFastOn ? 6 : 7)") != std::string::npos,
            "GUI toggle must send control command 6 (on) / 7 (off)");

    std::cout << "C64VicFastToggleV838Tests PASS\n";
    return 0;
}
