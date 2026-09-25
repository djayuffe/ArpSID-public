// SPDX-License-Identifier: BSD-3-Clause
// C64 PSID-CIA render scheduling regression guard.
//
// CIA-timed PSID service advances the PHI2 machine continuously until the
// timer IRQ enters the play routine. Timed SID writes must therefore map from
// the block-start PHI2 cycle directly into the AU buffer. Mapping them through
// PlayBase{cycle, playSample} adds playSample a second time.

#include "arpsid/core/math_utils.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

constexpr double kPalPhi2Hz = 985248.0;
constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 512;

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "C64PsidCiaRenderSchedulingV872Tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

std::string readSourceFile(const char* relativePath) {
#ifndef ARPSID_SOURCE_DIR
#define ARPSID_SOURCE_DIR "."
#endif
    std::ifstream in(std::string(ARPSID_SOURCE_DIR) + "/" + relativePath);
    require(in.good(), "source file must be readable");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool contains(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

int continuousCiaMappedSample(uint64_t deltaCycle, int blockSize = kBlockSize) {
    const uint64_t q32 = ArpSID::ArpSID_cyclesPerSampleQ32(kSampleRate, kPalPhi2Hz);
    return static_cast<int>(ArpSID::ArpSID_sampleForAbsoluteCycleQ32(
        deltaCycle, q32, static_cast<uint32_t>(std::max(0, blockSize - 1))));
}

int playBaseMappedSample(int playSample, uint64_t deltaCycle, int blockSize = kBlockSize) {
    const uint64_t q32 = ArpSID::ArpSID_cyclesPerSampleQ32(kSampleRate, kPalPhi2Hz);
    const uint64_t baseCycle = ArpSID::ArpSID_absoluteCycleAtSampleQ32(
        static_cast<uint64_t>(std::clamp(playSample, 0, std::max(0, blockSize - 1))), q32);
    return static_cast<int>(ArpSID::ArpSID_sampleForAbsoluteCycleQ32(
        baseCycle + deltaCycle, q32, static_cast<uint32_t>(std::max(0, blockSize - 1))));
}

uint64_t cycleDeltaAtHostSample(int sample) {
    const uint64_t q32 = ArpSID::ArpSID_cyclesPerSampleQ32(kSampleRate, kPalPhi2Hz);
    return ArpSID::ArpSID_absoluteCycleAtSampleQ32(static_cast<uint64_t>(sample), q32);
}

void testCiaWritesDoNotReceivePlaySampleTwice() {
    const uint64_t irqToWriteAt180 = cycleDeltaAtHostSample(180);
    const int continuous = continuousCiaMappedSample(irqToWriteAt180);
    const int doubled = playBaseMappedSample(180, irqToWriteAt180);

    require(continuous >= 179 && continuous <= 181,
            "continuous CIA mapping lands at the IRQ/write sample");
    require(doubled >= 359 && doubled <= 361,
            "PlayBase mapping documents the old playSample + CIA offset delay");
    require(doubled - continuous >= 175,
            "old PlayBase path must be visibly later than continuous mapping");

    const uint64_t irqToWriteAt128 = cycleDeltaAtHostSample(128);
    const int continuousSmall = continuousCiaMappedSample(irqToWriteAt128, 256);
    const int doubledSmall = playBaseMappedSample(128, irqToWriteAt128, 256);
    require(continuousSmall >= 127 && continuousSmall <= 129,
            "continuous CIA mapping remains inside a 256-frame block");
    require(doubledSmall == 255,
            "old PlayBase mapping clamps partial-block CIA writes to block end");
}

void testSourceUsesContinuousCiaMappingAndPartialCommit() {
    const std::string kernel = readSourceFile("source/au3/ArpSIDDSPKernel.hpp");
    require(contains(kernel,
                     "const bool continuousCycleMappedRuntime = continuousMachineRuntime || psidCiaServicePath;"),
            "PSID-CIA must opt into continuous cycle mapping");
    require(contains(kernel,
                     "CIA-speed PSID is continuous machine time"),
            "CIA branch must document continuous machine-time scheduling");
    // v893 refresh: v873 added the fatal timed-write-overflow gate alongside the
    // CPU-jam gate; rollback is still jam-gated (jam OR fatal overflow).
    require(contains(kernel, "if (service.cpuJammed || overflowFatal) {"),
            "CIA service rollback must be gated by CPU jam");
    require(contains(kernel,
                     "commitBridgeTransaction_(tx, player);\n"
                     "                        if (service.serviceComplete)"),
            "non-jammed CIA service must commit before checking full completion");
    require(contains(kernel,
                     "c64RsidCiaIncompleteCount_.fetch_add(1u, std::memory_order_relaxed);"),
            "partial CIA service must still be observable");
    require(!contains(kernel, "else if (service.playAddressEntered) {\n"
                              "                        // Play started but didn't complete"),
            "partial CIA service must not roll back solely for missing idle-return proof");

    const std::size_t ciaBranch = kernel.find("CIA-speed PSID is continuous machine time");
    const std::size_t vbiBranch = kernel.find("} else {\n"
                                              "                // Publish only host time",
                                              ciaBranch);
    require(ciaBranch != std::string::npos && vbiBranch != std::string::npos,
            "CIA/VBI branch boundaries must be findable");
    const std::string ciaBody = kernel.substr(ciaBranch, vbiBranch - ciaBranch);
    require(ciaBody.find("playBases[playBaseCount++]") == std::string::npos,
            "CIA branch must not create PlayBase entries");
}

} // namespace

int main() {
    testCiaWritesDoNotReceivePlaySampleTwice();
    testSourceUsesContinuousCiaMappingAndPartialCommit();
    std::cout << "C64PsidCiaRenderSchedulingV872Tests PASS\n";
    return 0;
}
