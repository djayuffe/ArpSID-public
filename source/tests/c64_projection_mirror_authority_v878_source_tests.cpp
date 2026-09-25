#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
std::string readText(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}
}

int main() {
    const std::filesystem::path testFile = std::filesystem::path(__FILE__);
    const std::filesystem::path root = testFile.parent_path().parent_path().parent_path();
    const auto kernel = readText(root / "source/au3/ArpSIDDSPKernel.hpp");
    int failures = 0;
    auto require = [&](bool ok, const char* msg) {
        if (!ok) {
            std::cerr << "C64ProjectionMirrorAuthorityV878SourceTests FAIL: " << msg << "\n";
            ++failures;
        }
    };

    require(!kernel.empty(), "failed to read ArpSIDDSPKernel.hpp");
    require(contains(kernel, "void ensureC64ProjectionMirrorClockReady_(bool pal) noexcept"),
            "projection mirror clock-ready helper missing");
    require(contains(kernel, "bool beginC64TelemetryDemandBlock_(int /*numFrames*/) noexcept {\n        ensureC64ProjectionMirrorClockReady_();"),
            "observer block must make PAL/NTSC mirror clock-ready before arming observer");
    require(contains(kernel, "c64Platform_.reset(pal);\n            c64Platform_.bootFromResetVector();\n            c64Platform_.startRealtimeSidCore();"),
            "clock-ready helper must perform full reset/boot/start when PAL/NTSC target changes");
    require(contains(kernel, "c64ProjectionMirrorQueuedWritesThisBlock_ = 0u;"),
            "clock reset must clear queued-write accounting so stale first-burst state cannot be committed");
    require(contains(kernel, "ensureC64ProjectionMirrorClockReady_(pal);"),
            "publisher must reuse clock-ready helper instead of open-coding a post-observer reset");
    require(!contains(kernel, "if (c64Platform_.clockHz() != (pal ? ArpSID::C64::kPalPhi2Hz : ArpSID::C64::kNtscPhi2Hz)) {\n            c64Platform_.reset"),
            "publisher must not contain the old post-observer reset block that could drop queued projection writes");

    if (failures != 0) return 1;
    std::cout << "C64ProjectionMirrorAuthorityV878SourceTests PASS\n";
    return 0;
}
