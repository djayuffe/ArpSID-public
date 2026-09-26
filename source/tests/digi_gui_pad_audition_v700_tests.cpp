// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// v700 - GUI DIGI audition pad wiring contract guard.
// This is intentionally a source-level guard because the full AU GUI/adapter
// translation units require Apple SDK. It catches the exact contract that made
// look wired while lacking test coverage: GUI -> adapter -> kernel queue
// -> render-thread consumption -> accepted/ignored telemetry.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif

static std::string readFile(const char* rel) {
    const std::string path = std::string(ARPSID_SOURCE_DIR) + "/" + rel;
    std::ifstream in(path, std::ios::binary);
    if (!in) std::abort();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void requireContains(const std::string& s, const char* needle) {
    if (s.find(needle) == std::string::npos) {
        std::cerr << "FAIL: missing source contract: " << needle << '\n';
        std::abort();
    }
}

int main() {
    const std::string kernel = readFile("source/au3/ArpSIDDSPKernel.hpp");
    const std::string adapterH = readFile("source/au3/ArpSIDDSPKernelAdapter.h");
    const std::string adapterMM = readFile("source/au3/ArpSIDDSPKernelAdapter.mm");
    const std::string telemetry = readFile("source/common/arpsid_telemetry_snapshot.h");
    const std::string gui = readFile("source/au3/ArpSIDViewController.mm");

    // GUI and adapter entrypoints must exist and remain render-safe queued APIs.
    requireContains(gui, "_digiGuiPadAudition_v124_");
    requireContains(gui, "_digiSlotHasPlayableSource_v218_");
    requireContains(gui, "DIGI PAD AUDITION EMPTY SLOT");
    requireContains(gui, "if (pad_v219) pad_v219.enabled = [self _digiSlotHasPlayableSource_v218_:(std::uint8_t)i_v219]");
    requireContains(gui, "b.enabled = [self _digiSlotHasPlayableSource_v218_:(std::uint8_t)i]");
    requireContains(gui, "triggerDigiPadSlot:uSlot_v218 velocity:_digiGuiPadVelocityControl_v131_");
    requireContains(adapterH, "triggerDigiPadSlot:(uint8_t)slot velocity:(uint8_t)velocity");
    requireContains(adapterMM, "_kernel->triggerDigiPadForGui(slot, velocity)");
    requireContains(kernel, "void triggerDigiPadForGui(std::uint8_t slot, std::uint8_t velocity) noexcept");
    requireContains(kernel, "digiGuiPadTriggerMask_.fetch_or");
    requireContains(kernel, "consumeGuiDigiPadTriggers_");
    requireContains(kernel, "digiGuiPadTriggerMask_.exchange(0u");

    // telemetry contract: local GUI pads are separated from external MIDI.
    requireContains(kernel, "telemetryDigiGuiPadAcceptedCount_");
    requireContains(kernel, "telemetryDigiGuiPadIgnoredCount_");
    requireContains(kernel, "telemetryDigiGuiPadLastSlot_");
    requireContains(kernel, "telemetryDigiGuiPadLastVelocity_");
    requireContains(kernel, "telemetryDigiGuiPadLastAccepted_");
    requireContains(telemetry, "digiGuiPadAcceptedCount");
    requireContains(adapterMM, "out->digiGuiPadAcceptedCount");
    requireContains(gui, "PAD %u/%u");

    // UX contract: GUI audition pads must explicitly explain that the MIDI
    // channel filter is for external MIDI only, avoiding the ambiguity.
    requireContains(gui, "GUI pads bypass the MIDI channel filter; external MIDI note");
    requireContains(gui, "_digiGuiPadVelocityChanged_v131_");
    requireContains(gui, "_digiGuiPadVelocityPreset_v131_");
    requireContains(gui, "PAD %d %@  VEL %d  A%u/I%u");

    return 0;
}
