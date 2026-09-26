// Copyright (C) 2024-2026 Ulf Bertilsson
// Regression guard for the v839 user-togglable "6510 fast" mode.
//
// 6510:FAST keeps the CPU/CIA/VIC/SID emulation bit-exact — it is NOT the
// policy-locked instruction-atomic engine. The only thing it skips is the
// per-PHI2-cycle diagnostics snapshot (two CIA timer-phase snapshots + the
// ~40-field telemetry copy), which exists purely for the GUI/HUD. The exactness
// ledger reads its inputs from live sources, so gating the snapshot is
// audio-safe. This guard locks: default OFF everywhere, the gated tick path, the
// full GUI -> kernel -> PSID runtime -> PHI2 wiring, and that the gate never
// touches the actual ++phi2_ cycle advance.
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
    require(types.find("bool cpuFast = false;") != std::string::npos,
            "Phi2MachineConfig::cpuFast must exist and default to false (accurate)");

    // 2. PHI2 machine: live setter + the diagnostics-snapshot gate, with the
    //    cycle counter advanced regardless of the gate.
    const std::string machine = readText("include/arpsid/core/c64_phi2_machine.h");
    require(machine.find("void setCpuFast(bool fast) noexcept") != std::string::npos,
            "C64Phi2Machine must expose setCpuFast()");
    require(machine.find("if (!cfg_.cpuFast) {") != std::string::npos,
            "tickPhi2 must gate the per-cycle diagnostics snapshot behind !cpuFast");
    const std::size_t gate = machine.find("if (!cfg_.cpuFast) {");
    const std::size_t endGate = machine.find("} // end !cfg_.cpuFast diagnostics snapshot", gate);
    const std::size_t advance = machine.find("++phi2_;", gate);
    require(endGate != std::string::npos, "diagnostics gate must be explicitly closed");
    require(advance != std::string::npos && advance > endGate,
            "the ++phi2_ cycle advance must stay OUTSIDE the cpuFast gate");

    // 3. PSID runtime forwards to the PHI2 machine.
    const std::string runtime = readText("include/arpsid/core/c64_psid_runtime.h");
    require(runtime.find("void setCpuFast(bool fast) noexcept { phi2Machine_.setCpuFast(fast); }")
                != std::string::npos,
            "PSID runtime must forward setCpuFast() to the PHI2 machine");

    // 4. DSP kernel: atomic default ON (v873: smooth realtime playback; 6510-fast is
    //    audio-neutral) + applied to the live player each block.
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");
    require(kernel.find("std::atomic<bool> c64CpuFast_{true};") != std::string::npos,
            "kernel c64CpuFast_ must default to true (v873: audio-neutral CPU saving on by default)");
    require(kernel.find("void setC64CpuFast(bool on) noexcept") != std::string::npos,
            "kernel must expose setC64CpuFast()");
    require(kernel.find("player->setCpuFast(c64CpuFast_.load(std::memory_order_relaxed));")
                != std::string::npos,
            "kernel must push the 6510-fast flag into the live PSID player per block");

    // 5. Adapter control-hub: command 8 = ON, 9 = OFF.
    const std::string adapter = readText("source/au3/ArpSIDDSPKernelAdapter.mm");
    require(adapter.find("case 8: _kernel->setC64CpuFast(true)") != std::string::npos,
            "control command 8 must enable 6510-fast");
    require(adapter.find("case 9: _kernel->setC64CpuFast(false)") != std::string::npos,
            "control command 9 must disable 6510-fast (accurate)");

    // 6. GUI: button ships as 6510:FAST (v873 default on) and toggles command 8/9.
    const std::string vc = readText("source/au3/ArpSIDViewController.mm");
    require(vc.find("@{@\"t\":@\"6510:FAST\", @\"a\":@\"_c64Toggle6510Fast:\"}") != std::string::npos,
            "GUI must ship the 6510-fast button labelled 6510:FAST (v873 default on)");
    require(vc.find("[self _performC64ControlHubCommand:(_c64CpuFastOn ? 8 : 9)") != std::string::npos,
            "GUI toggle must send control command 8 (on) / 9 (off)");

    std::cout << "C646510FastToggleV839Tests PASS\n";
    return 0;
}
