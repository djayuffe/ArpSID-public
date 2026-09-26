// Copyright (C) 2024-2026 Ulf Bertilsson
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string readText(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    const std::string kernel = readText(root / "source/au3/ArpSIDDSPKernel.hpp");
    require(contains(kernel, "const auto fallbackPal = [this]() noexcept"),
            "resolver must have explicit fallback PAL/NTSC authority");
    require(contains(kernel, "const uint32_t liveClock = psidLive->platform().clockHz();"),
            "resolver must inspect live PSID platform clock once");
    require(contains(kernel, "if (liveClock == ArpSID::C64::kPalPhi2Hz) return true;"),
            "resolver must accept explicit PAL live clock");
    require(contains(kernel, "if (liveClock == ArpSID::C64::kNtscPhi2Hz) return false;"),
            "resolver must accept explicit NTSC live clock");
    require(contains(kernel, "return fallbackPal();\n    }\n\n    void ensureC64ProjectionMirrorClockReady_(bool pal)"),
            "unknown live clock must fall back instead of defaulting to NTSC");
    require(!contains(kernel, "return psidLive\n            ? (psidLive->platform().clockHz() == ArpSID::C64::kPalPhi2Hz)\n            : !(runtimePhysicalSidClockHz() > 1000000.0);"),
            "old unknown-clock-as-NTSC resolver must not remain");
    std::cout << "PASS c64_projection_mirror_authority_v882_source_tests\n";
    return 0;
}
