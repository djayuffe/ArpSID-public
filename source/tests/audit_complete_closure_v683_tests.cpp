#include "arpsid/core/c64_fixed_write_scheduler.h"
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/core/psid_header.h"
#include "arpsid/core/realtime_atomic_contract.h"
#include "arpsid/core/scope_triple_buffer.h"
#include "arpsid/core/sid_chip.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string readText(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

void putBe16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> makeSid(bool rsid,
                                  std::uint16_t initAddress,
                                  const std::vector<std::uint8_t>& code) {
    constexpr std::size_t headerSize = 0x7Cu;
    std::vector<std::uint8_t> image(headerSize + 2u + code.size(), 0u);
    image[0] = rsid ? 'R' : 'P';
    image[1] = 'S';
    image[2] = 'I';
    image[3] = 'D';
    putBe16(image, 0x04u, 2u);
    putBe16(image, 0x06u, static_cast<std::uint16_t>(headerSize));
    putBe16(image, 0x08u, 0u);
    putBe16(image, 0x0Au, initAddress);
    putBe16(image, 0x0Cu, 0u);
    putBe16(image, 0x0Eu, 1u);
    putBe16(image, 0x10u, 1u);
    image[headerSize] = static_cast<std::uint8_t>(initAddress);
    image[headerSize + 1u] = static_cast<std::uint8_t>(initAddress >> 8u);
    std::copy(code.begin(), code.end(), image.begin() + static_cast<std::ptrdiff_t>(headerSize + 2u));
    return image;
}

void testScopeTripleBuffer() {
    struct Snapshot {
        std::uint64_t frame = 0u;
        std::array<std::uint8_t, 5125> payload{};
    };
    static_assert(std::is_trivially_copyable<Snapshot>::value);
    static_assert(alignof(ArpSID::ScopeTripleBuffer<Snapshot>) >= 64u);

    ArpSID::ScopeTripleBuffer<Snapshot> triple;
    Snapshot out{};
    for (std::uint64_t i = 0u; i < 100000u; ++i) {
        triple.clearWriteSlot();
        triple.writeSlot().frame = i;
        triple.publish();
        require(ArpSID::ScopeTripleBufferSpscTestAccess<Snapshot>::tryConsume(triple, out),
                "scope triple buffer publishes every stress iteration");
        require(out.frame == i, "scope triple buffer preserves the published frame");
    }
}

void testInvalidSidAccessAndExactness() {
    using namespace ArpSID::C64;
    constexpr std::uint8_t invalidD418 = static_cast<std::uint8_t>(5u * 32u + 0x18u);

    C64SidBridgeState bridge;
    bridge.sidWrite(invalidD418, 0x0Fu, 1234u);
    require(bridge.invalidSidChipWriteCount == 1u, "invalid bridge write is counted");
    require(bridge.regsByChip[0][0x18u] == 0u, "invalid bridge write never aliases chip zero");
    require(bridge.d418WriteCount == 0u, "invalid bridge write never mutates D418 telemetry");

    require(bridge.sidRead(invalidD418, 1235u) == 0xFFu, "invalid bridge read returns open bus");
    require(bridge.invalidSidChipReadCount == 1u, "invalid bridge read is counted");
    require(bridge.sidOpenBusReadCount == 1u, "invalid bridge read counts SID open bus");
    require(bridge.sidReadApproximationCount == 1u, "invalid bridge read counts approximation");
    (void)bridge.sidRead(0x00u, 1236u);
    require(bridge.sidOpenBusReadCount == 2u, "write-only SID read counts open bus separately");

    C64Runtime runtime;
    runtime.reset(true);
    const auto rsid = makeSid(true, 0x0800u, {0x60u});
    require(runtime.loadPsid(rsid.data(), rsid.size()), "runtime loads exactness fixture");
    runtime.sidSink().sidWrite(invalidD418, 0x0Fu, 1u);
    require(runtime.sidSink().regsByChip[0][0x18u] == 0u, "runtime invalid write never aliases chip zero");
    require(rsidDowngradeHas(runtime.rsidExactnessDowngradeReasons(),
                             RsidExactnessDowngrade::InvalidSidChipAccess),
            "invalid SID chip access sets exactness downgrade");
}

void testBrkSentinelLedger() {
    using namespace ArpSID::C64;
    const auto rsid = makeSid(true, 0x0800u, {
        0xA9u, 0x0Fu,       // LDA #$0F
        0x8Du, 0x18u, 0xD4u,// STA $D418
        0x00u,              // BRK compatibility sentinel
    });

    C64Runtime strict;
    strict.reset(true);
    strict.setRsidPlaybackMode(RsidPlaybackMode::Strict);
    require(strict.loadPsid(rsid.data(), rsid.size()), "strict BRK sentinel RSID loads");
    require(!strict.runInit(1u, 4096u), "strict RSID init rejects BRK compatibility sentinel");
    require(strict.initBrkSentinelCount() == 0u, "strict BRK is never counted as sentinel completion");

    C64Runtime compatible;
    compatible.reset(true);
    compatible.setRsidPlaybackMode(RsidPlaybackMode::Compatible);
    require(compatible.loadPsid(rsid.data(), rsid.size()), "compatible BRK sentinel RSID loads");
    require(compatible.runInit(1u, 65536u), "compatible BRK sentinel init completes");
    require(compatible.initBrkSentinelCount() == 1u, "compatible BRK sentinel completion is counted");
    require(rsidDowngradeHas(compatible.rsidExactnessDowngradeReasons(),
                             RsidExactnessDowngrade::InitBrkSentinel),
            "BRK sentinel completion downgrades exactness");

    compatible.enablePhi2Machine(true);
    ArpSID::C64::Phi2BusPhase phase{};
    (void)compatible.phi2Machine().memory().cpuRead(
        compatible.phi2Machine().phi2Cycle(), 0xD400u, phase);
    require(compatible.sidReadApproximationCount() == 2u,
            "combined PHI2 bridge and sink SID-read telemetry is summed");
    require(compatible.sidOpenBusReadCount() == 2u,
            "combined PHI2 bridge and sink open-bus telemetry is summed");
}

void testPsidMetadata32Bytes() {
    auto image = makeSid(false, 0x0800u, {0x60u});
    const char title[32] = {
        '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F',
        'G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V'
    };
    std::memcpy(image.data() + 0x16u, title, sizeof(title));
    ArpSID::PsidHeader header{};
    require(ArpSID::psidParse(image.data(), static_cast<std::uint32_t>(image.size()), header) ==
                ArpSID::PsidParseResult::OK,
            "32-byte metadata PSID parses");
    require(std::strlen(header.name) == 32u, "all 32 PSID metadata bytes survive");
    require(header.name[31] == 'V' && header.name[32] == '\0',
            "metadata uses a dedicated 33rd terminator byte");
}

struct ScheduledWrite {
    int sample = 0;
    std::uint16_t cycle = 0;
    std::uint16_t cyclesInSample = 0;
    std::uint32_t ordinal = 0;
    std::uint8_t reg = 0;
    std::uint8_t value = 0;
    std::uint8_t chip = 0;
};

bool sameWrite(const ScheduledWrite& a, const ScheduledWrite& b) {
    return a.sample == b.sample && a.cycle == b.cycle && a.ordinal == b.ordinal;
}

void testFixedScheduler() {
    constexpr std::size_t capacity = 4096u;
    std::array<ScheduledWrite, capacity> writes{};
    std::array<ScheduledWrite, capacity> scratch{};
    std::uint32_t state = 0xC001D00Du;
    for (std::uint32_t i = 0u; i < capacity; ++i) {
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        writes[i].sample = static_cast<int>(state & 1023u);
        writes[i].cycle = static_cast<std::uint16_t>((state >> 10u) & 63u);
        writes[i].ordinal = i;
    }
    const auto original = writes;
    auto expected = writes;
    std::stable_sort(expected.begin(), expected.end(),
        [](const ScheduledWrite& a, const ScheduledWrite& b) {
            if (a.sample != b.sample) return a.sample < b.sample;
            return a.cycle < b.cycle;
        });
    ArpSID::C64::stableScheduleC64RenderWrites(writes, scratch, capacity);
    for (std::size_t i = 0u; i < capacity; ++i) {
        require(sameWrite(writes[i], expected[i]), "4096-write fixed scheduler matches stable ordering");
    }

    std::vector<ScheduledWrite> chunked;
    chunked.reserve(capacity);
    for (int start = 0; start < 1024; start += 64) {
        std::array<ScheduledWrite, capacity> local{};
        std::array<ScheduledWrite, capacity> localScratch{};
        std::uint32_t count = 0u;
        for (const ScheduledWrite& write : original) {
            if (write.sample >= start && write.sample < start + 64) {
                local[count] = write;
                local[count].sample -= start;
                ++count;
            }
        }
        ArpSID::C64::stableScheduleC64RenderWrites(local, localScratch, count);
        for (std::uint32_t i = 0u; i < count; ++i) {
            local[i].sample += start;
            chunked.push_back(local[i]);
        }
    }
    require(chunked.size() == capacity, "chunked scheduling retains all writes");
    for (std::size_t i = 0u; i < capacity; ++i) {
        require(sameWrite(chunked[i], writes[i]), "one-block and 16x64 scheduling are deterministic");
    }
}

void testCalibrationAndSourcePolicies() {
    require(ArpSID::sidSelectableFilterCalibrationsValid(),
            "all selectable SID revisions have live calibration");
    for (std::size_t revision = 2u; revision <= 5u; ++revision) {
        const auto calibration = ArpSID::kSidFilterCalibrationByRevision[revision];
        require(calibration.cutoffScale > 0.0 &&
                calibration.resonanceScale > 0.0 &&
                calibration.distortionFactor > 0.0,
                "selectable SID calibration is non-zero");
    }

    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string kernel = readText(root + "/source/au3/ArpSIDDSPKernel.hpp");
    const std::string aq = readText(root + "/source/au3/ArpSIDDigiAudioQueueCapture.mm");
    const std::string aqHeader = readText(root + "/source/au3/ArpSIDDigiAudioQueueCapture.h");
    const std::string gui = readText(root + "/source/au3/ArpSIDViewController.mm");
    const std::string cmake = readText(root + "/CMakeLists.txt");

    require(kernel.find("stableScheduleC64RenderWrites") != std::string::npos,
            "kernel uses fixed C64 scheduler");
    require(kernel.find("std::sort(writes") == std::string::npos,
            "kernel has no C64 render comparison sort");
    require(kernel.find("RenderWrite writes[") == std::string::npos,
            "kernel has no large C64 render stack array");
    require(kernel.find("return componentFlavor_ == ArpSID::ComponentFlavor::Hybrid;\n        return") ==
                std::string::npos,
            "duplicate unreachable return remains removed");

    require(aq.find("std::lock_guard<std::mutex> lk(queueMutex_)") != std::string::npos,
            "AudioQueue start/stop share one lifecycle mutex");
    require(aq.find("stopLocked(true)") != std::string::npos,
            "AudioQueue start failure cleanup is non-recursive");
    require(aq.find("buffer->mUserData") != std::string::npos,
            "AudioQueue callback uses direct buffer context");
    require(aq.find("buffers_[i] == buffer") == std::string::npos,
            "AudioQueue callback has no vector scan");
    require(aqHeader.find("AQCaptureLifecycle::Stopped") != std::string::npos &&
            aq.find("AQCaptureLifecycle::Stopping") != std::string::npos &&
            aq.find("AQCaptureLifecycle::Draining") != std::string::npos,
            "AudioQueue lifecycle exposes stopping, draining, and stopped states");
    require(aqHeader.find("using AQCaptureIngestFn = void (*)") != std::string::npos,
            "AudioQueue ingest uses a narrow function-pointer callback ABI");
    require(aqHeader.find("std::function<void(const AQCaptureChunk&)>") == std::string::npos,
            "AudioQueue ingest no longer uses std::function on the callback thread");
    require(aq.find("callbackSampleRate_.load") != std::string::npos &&
            aq.find("chunk.sampleRate = actualSampleRate_") == std::string::npos,
            "AudioQueue callback reads atomically-published sample rate");

    require(gui.find("INIT_BRK_SENTINEL") != std::string::npos &&
            gui.find("INVALID_SID_CHIP_ACCESS") != std::string::npos,
            "GUI maps both new exactness bits");
    require(gui.find("SID READ APPROX") != std::string::npos &&
            gui.find("INVALID CHIP R/W") != std::string::npos,
            "retained C64 cockpit surfaces SID exactness counters");
    require(gui.find("wantsC64Tab || wantsC64StateTab") != std::string::npos,
            "exactness HUD is wired to the retained C64 tab");

    require(cmake.find("-msse4.2") == std::string::npos,
            "CMake no longer forces SSE4.2 globally");
    require(cmake.find("option(ARPSID_BUILD_TESTS \"Build forensic acceptance tests\" OFF)") !=
                std::string::npos,
            "consumer builds default tests off");
    require(cmake.find("if(NOT ARPSID_BUILD_TESTS)\n  return()") != std::string::npos,
            "consumer builds stop before the legacy test-only inventory");
    require(cmake.find("ARPSID_RELEASE_WARNING_GATE") != std::string::npos &&
            cmake.find("-Werror=stringop-overflow") != std::string::npos,
            "release warning gate catches memory-significant warnings");
}

} // namespace

int main() {
    testScopeTripleBuffer();
    testInvalidSidAccessAndExactness();
    testBrkSentinelLedger();
    testPsidMetadata32Bytes();
    testFixedScheduler();
    testCalibrationAndSourcePolicies();
    std::cout << "AuditCompleteClosureV683Tests PASS\n";
    return 0;
}
