#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string readText(const std::string& relative) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + relative,
                         std::ios::binary);
    require(static_cast<bool>(stream), relative.c_str());
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

std::string slice(const std::string& text,
                  const std::string& begin,
                  const std::string& end) {
    const std::size_t first = text.find(begin);
    require(first != std::string::npos, begin.c_str());
    const std::size_t last = text.find(end, first + begin.size());
    require(last != std::string::npos, end.c_str());
    return text.substr(first, last - first);
}

std::size_t countOf(const std::string& text, const std::string& needle) {
    std::size_t count = 0u;
    for (std::size_t pos = 0u;
         (pos = text.find(needle, pos)) != std::string::npos;
         pos += needle.size()) {
        ++count;
    }
    return count;
}

void testPureSidLeaseAndPublication() {
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");
    const std::string pure = slice(
        kernel,
        "void capturePureSid1Q1RecordSource_",
        "template <typename RenderWriteT>");
    const std::string d418 = slice(
        kernel,
        "void captureC64D418PureSid1Q1RecordSource_",
        "void sanitizePureSid1Q1Outputs_");
    const std::string status = slice(
        kernel,
        "void pureSid1Q1RecordCaptureStatus",
        "bool copyAndStopPureSid1Q1RecordCapture");
    const std::string copy = slice(
        kernel,
        "bool copyAndStopPureSid1Q1RecordCapture",
        "void capturePureSid1Q1RecordSource_");

    const std::size_t pureLease =
        pure.find("pureSid1Q1RecCaptureInFlight_.fetch_add");
    require(pureLease != std::string::npos, "PureSID capture acquires an in-flight lease");
    require(pureLease < pure.find("pureSid1Q1RecCaptureActive_.load"),
            "PureSID lease precedes active-state inspection");
    require(pureLease < pure.find("pureSid1Q1RecCaptureCapacity_.load"),
            "PureSID lease precedes capacity inspection");
    require(pure.find("pureSid1Q1RecCaptureMono_.size") == std::string::npos &&
            pure.find("pureSid1Q1RecCaptureMono_.empty") == std::string::npos,
            "PureSID callback never reads vector metadata");

    const std::size_t d418Lease =
        d418.find("pureSid1Q1RecCaptureInFlight_.fetch_add");
    require(d418Lease != std::string::npos, "D418 capture acquires an in-flight lease");
    require(d418Lease < d418.find("pureSid1Q1C64D418Observed_"),
            "D418 lease precedes observed-state access");
    require(d418Lease < d418.find("pureSid1Q1C64D418Held_"),
            "D418 lease precedes held-state access");
    require(d418.find("pureSid1Q1RecCaptureMono_.size") == std::string::npos &&
            d418.find("pureSid1Q1RecCaptureMono_.empty") == std::string::npos,
            "D418 callback never reads vector metadata");

    require(status.find("pureSid1Q1RecCaptureCapacity_.load") != std::string::npos &&
            status.find("pureSid1Q1RecCaptureMono_") == std::string::npos,
            "status uses only the atomic capacity mirror");
    require(copy.find("pureSid1Q1RecCaptureCapacity_.exchange") != std::string::npos &&
            copy.find("pureSid1Q1RecCaptureMono_.size") == std::string::npos,
            "copy-and-stop revokes and uses atomic capacity");
    require(kernel.find("std::atomic<double> pureSid1Q1RecCaptureSampleRate_") !=
                std::string::npos,
            "PureSID capture sample rate is atomic");
    require(kernel.find("pureSid1Q1RecCaptureCapacity_.store(maxFrames") !=
                std::string::npos,
            "successful allocation publishes capacity before activation");
}

void testAudioQueueAndRecordAllocation() {
    const std::string header =
        readText("source/au3/ArpSIDDigiAudioQueueCapture.h");
    const std::string impl =
        readText("source/au3/ArpSIDDigiAudioQueueCapture.mm");
    const std::string gui =
        readText("source/au3/ArpSIDViewController.mm");

    require(header.find("using AQCaptureIngestFn = void (*)") != std::string::npos,
            "AudioQueue ingest ABI is a function pointer");
    require(header.find("std::function<void(const AQCaptureChunk&)>") ==
                std::string::npos,
            "AudioQueue callback path has no broad std::function ingest");
    require(header.find("std::atomic<double> callbackSampleRate_") !=
                std::string::npos,
            "AudioQueue callback sample rate is atomic");
    require(impl.find("callbackSampleRate_.load") != std::string::npos &&
            impl.find("chunk.sampleRate = actualSampleRate_") == std::string::npos,
            "AudioQueue callback never reads control-thread sample rate");
    require(impl.find("ingest_(ingestContext_, chunk)") != std::string::npos,
            "AudioQueue invokes the narrow callback with explicit context");

    require(gui.find("ArpSIDDigiPrepareRecordVector_v326") != std::string::npos &&
            gui.find("catch (...)") != std::string::npos,
            "record-vector preparation handles allocation failure");
    require(gui.find("_digiRecordMonoFloatBuffer_v182_.assign") ==
                std::string::npos,
            "record paths cannot bypass the allocation-failure helper");
}

void testTelemetryTimingAndSortClassification() {
    const std::string runtime =
        readText("include/arpsid/core/c64_psid_runtime.h");
    const std::string kernel =
        readText("source/au3/ArpSIDDSPKernel.hpp");

    require(runtime.find("return std::max(phi2SidBridge_") == std::string::npos,
            "combined C64 telemetry no longer undercounts with max");
    require(countOf(runtime, "return satAdd32_(phi2SidBridge_") >= 5u,
            "combined C64 telemetry uses saturating sums");
    require(kernel.find("platform.runCycles(playPhi2Cycles") == std::string::npos,
            "VBI path cannot unconditionally advance a future frame");
    require(kernel.find("accruePassiveHostTimeToFrame") != std::string::npos &&
            kernel.find("std::min<uint64_t>(c64PsidPassiveCycleDebt_, playPhi2Cycles)") !=
                std::string::npos,
            "VBI advancement is host-time accrued and debt bounded");
    require(kernel.find("continuousTimelineBaseCycle") != std::string::npos &&
            kernel.find("playBases[playBaseCount++] = PlayBase{ baseCycle, 0 }") ==
                std::string::npos,
            "continuous RSID timing has a separate block timeline");
    require(runtime.find("const bool allowInitBrkSentinel") != std::string::npos &&
            runtime.find("!loaded_.header.rsid || rsidPlaybackMode_ == RsidPlaybackMode::Compatible") !=
                std::string::npos,
            "strict RSID init cannot enable the BRK sentinel");

    const std::vector<std::string> sortFiles = {
        "source/au3/ArpSIDCanonicalEvents.h",
        "include/arpsid/engines/arpeggiator.h",
        "include/arpsid/engines/bitperfect_engine.h",
        "include/arpsid/core/sid_analogue_calibration.h",
        "include/arpsid/engines/sid_register_engine.h",
        "include/arpsid/core/sid_runtime_held_replay.h",
        "include/arpsid/core/c64_bus.h",
        "include/arpsid/core/sid_ingress_merge.h",
        "include/arpsid/core/sid_event_queue.h",
    };
    std::size_t sorts = 0u;
    std::size_t classifications = 0u;
    for (const std::string& file : sortFiles) {
        const std::string text = readText(file);
        sorts += countOf(text, "std::sort(");
        classifications += countOf(text, "ARPSID_RT_SORT_CLASSIFICATION");
    }
    require(sorts == 9u && classifications == sorts,
            "every production sort is explicitly realtime-classified");
}

void testReleaseAndKnobUxGates() {
    const std::string gui =
        readText("source/au3/ArpSIDViewController.mm");
    const std::string package =
        readText("scripts/macos/package_complete_release.sh");
    const std::string verify =
        readText("scripts/macos/verify_logic_auv3.sh");

    require(gui.find("kArpSIDKnobVisualScale=0.70f") != std::string::npos,
            "knob visuals are reduced to 70 percent");
    require(gui.find("(p.x-_ds.x)*.35f") != std::string::npos &&
            gui.find("case 116:") != std::string::npos &&
            gui.find("case 121:") != std::string::npos,
            "knobs support horizontal drag and large keyboard steps");
    require(package.find("ARPSID_AUV3_REGISTRATION_VERIFIED=1") !=
                std::string::npos &&
            package.find("ARPSID_ALLOW_UNREGISTERED_AUV3") != std::string::npos,
            "release packaging requires a registration receipt by default");
    require(verify.find("RECEIPT_PATH") != std::string::npos &&
            verify.find("pluginkit does not list") != std::string::npos,
            "Logic verifier emits a receipt only after PlugInKit assertion");
}

} // namespace

int main() {
    testPureSidLeaseAndPublication();
    testAudioQueueAndRecordAllocation();
    testTelemetryTimingAndSortClassification();
    testReleaseAndKnobUxGates();
    std::cout << "RemainingAuditClosureV747Tests PASS\n";
    return 0;
}
