#include <fstream>
#include <iostream>
#include <string>

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    std::ifstream f("source/au3/ArpSIDDSPKernel.hpp");
    if (!f) f.open("../source/au3/ArpSIDDSPKernel.hpp");
    if (!f) {
        std::cerr << "cannot open ArpSIDDSPKernel.hpp\n";
        return 1;
    }
    const std::string src((std::istreambuf_iterator<char>(f)), {});

    auto require = [&](bool ok, const char* msg) {
        if (!ok) {
            std::cerr << "FAIL: " << msg << "\n";
            std::exit(1);
        }
    };

    require(contains(src, "uint64_t c64PsidAudioHostSampleCursor_ = 0;"),
            "dedicated audible C64 SIDPLAY host-sample cursor exists");
    require(contains(src, "c64PsidAudioHostSampleCursor_ = 0;"),
            "audible cursor resets on C64 handoff");
    require(contains(src, "const uint64_t blockStartHostSample = c64PsidAudioHostSampleCursor_;"),
            "C64 SIDPLAY write/sample bucketization uses audible cursor");
    require(contains(src, "c64PsidAudioHostSampleCursor_ += static_cast<uint64_t>(std::max(0, numFrames));"),
            "audible cursor advances in the real C64 audio render path");
    require(contains(src, "const uint64_t blockStartSample = c64TelemetryHostSampleCursor_;"),
            "telemetry cursor remains cosmetic-only for mirror publisher");
    require(contains(src, "renderC64SidplayPathIfActive_(") && contains(src, "cancelC64TelemetryDemandBlock_();\n            return;"),
            "C64 SIDPLAY early return still closes telemetry scope");
    require(contains(src, "currentPlayPeriod") && contains(src, "c64PsidPlayPeriodSamplesCache_ = playPeriod;"),
            "chunk wrapper derives current cadence before the first inner render");
    if (contains(src, "const uint64_t blockStartHostSample = c64TelemetryHostSampleCursor_;")) {
        std::cerr << "FAIL: telemetry cursor is still used as C64 SIDPLAY audio timing authority\n";
        return 1;
    }
    return 0;
}
