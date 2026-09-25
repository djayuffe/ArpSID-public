#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string readFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

static size_t posOf(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle);
}

int main() {
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    const std::string kernel = readFile(root / "source/au3/ArpSIDDSPKernel.hpp");
    auto require = [](bool ok, const char* msg) {
        if (!ok) {
            std::cerr << "C64ProjectionMirrorAuthorityV880SourceTests FAIL: " << msg << "\n";
            std::exit(1);
        }
    };

    require(!kernel.empty(), "failed to read ArpSIDDSPKernel.hpp");
    require(contains(kernel, "bool resolveC64ProjectionMirrorPal_() const noexcept"),
            "shared PAL/NTSC resolver must exist");
    require(contains(kernel, "ensureC64ProjectionMirrorClockReady_(resolveC64ProjectionMirrorPal_());"),
            "begin helper must use the shared PAL/NTSC resolver before observer arming");
    require(contains(kernel, "const bool pal = resolveC64ProjectionMirrorPal_();"),
            "publisher must use the same PAL/NTSC resolver as begin");
    require(contains(kernel, "uint32_t mirrorClock = c64Platform_.clockHz();"),
            "projection observer scheduling must read the prepared mirror platform clock");
    require(contains(kernel, "const double phi2 = static_cast<double>(mirrorClock);"),
            "projection observer scheduling must convert host samples using mirrorClock");
    require(!contains(kernel, "const double sidClock = runtimePhysicalSidClockHz();\n        const bool pal = !(sidClock > 1000000.0);\n        const double phi2"),
            "old split-brain runtimePhysicalSidClockHz PHI2 conversion must not remain in mirrorSidProjectionWriteToC64_");

    const size_t mirrorFn = posOf(kernel, "bool mirrorSidProjectionWriteToC64_");
    const size_t readClock = posOf(kernel, "uint32_t mirrorClock = c64Platform_.clockHz();");
    const size_t callBridge = posOf(kernel, "projectSidHostTimedWriteThroughC64Bus(c64Platform_");
    require(mirrorFn != std::string::npos && readClock != std::string::npos && callBridge != std::string::npos &&
            mirrorFn < readClock && readClock < callBridge,
            "mirror clock must be selected before scheduling observer write through the bridge");

    std::cout << "C64ProjectionMirrorAuthorityV880SourceTests PASS\n";
    return 0;
}
