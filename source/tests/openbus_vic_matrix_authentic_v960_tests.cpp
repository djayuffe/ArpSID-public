// v960 OpenBus VIC/Matrix authentic-telemetry closure.
//
// The SIDCORE / C64 SID-bus Metal backdrop (ArpSIDSidCoreMetalBackdropView) now
// drives its VIC-II / open-bus visuals from REAL emulator telemetry rather than
// approximations:
//   - badline uniform  <- c64VicBadline (was the raster%8==0 guess);
//   - vicBeamX          <- c64VicCycle/63 (the within-line raster beam column);
//   - spriteDma         <- c64VicSpriteDma;
//   - busDecayMask      <- c64OpenBusDecayMask (per-bit open-bus DRAM charge).
// The shader (V385) adds vicRasterBeam() + openBusDecayLanes() and composites an
// authentic raster beam, badline DMA band, and per-bit open-bus decay lanes in the
// C64 SID-bus surface (busMode > 0.5).
//
// This is a source-text pin so a future edit cannot silently regress the wiring
// back to approximations or drop the uniform plumbing (which would leave the
// MSL/C++ uniform structs mismatched and the shader unable to build at runtime).

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "openbus_vic_matrix_authentic_v960_tests FAIL: " << message << "\n"; std::exit(1); }
}

static std::string readFile(const char* relativePath) {
    std::ifstream file(std::string(ARPSID_SOURCE_ROOT) + "/" + relativePath, std::ios::binary);
    require(static_cast<bool>(file), relativePath);
    std::ostringstream out; out << file.rdbuf(); return out.str();
}

// Count "float " occurrences inside the first {...} following `anchor`.
static int floatFieldsAfter(const std::string& s, const std::string& anchor) {
    auto a = s.find(anchor);
    if (a == std::string::npos) return -1;
    auto open = s.find('{', a);
    auto close = s.find('}', open);
    if (open == std::string::npos || close == std::string::npos) return -1;
    const std::string body = s.substr(open, close - open);
    int c = 0; size_t p = 0;
    while ((p = body.find("float ", p)) != std::string::npos) { ++c; p += 6; }
    return c;
}

int main() {
    const std::string gui = readFile("source/au3/ArpSIDViewController.mm");

    // Shader version was bumped and the old one retired.
    require(gui.find("kArpSIDSidCoreMetalShaderSourceV385") != std::string::npos,
            "V385 SIDCORE Metal shader must exist");
    require(gui.find("kArpSIDSidCoreMetalShaderSourceV384") == std::string::npos,
            "old V384 shader symbol must be fully retired");

    // Authentic VIC/open-bus shader helpers + busMode composite.
    require(gui.find("vicRasterBeam(") != std::string::npos,
            "shader must define the authentic VIC-II raster beam helper");
    require(gui.find("openBusDecayLanes(") != std::string::npos,
            "shader must define the per-bit open-bus DRAM-decay lanes helper");
    require(gui.find("col += openBusDecayLanes(uv, u.openBus, u.busDecayMask);") != std::string::npos,
            "bus surface must composite the open-bus decay lanes from real telemetry");

    // Uniforms driven from REAL emulator telemetry (not approximations).
    require(gui.find("_telemetry.c64VicBadline ? 0.90f : 0.10f") != std::string::npos,
            "badline uniform must use the real c64VicBadline flag");
    require(gui.find("(_telemetry.c64VicRaster % 8u) == 0u ? 0.75f") == std::string::npos,
            "the raster%8 badline approximation must be gone");
    require(gui.find("(float)_telemetry.c64OpenBusDecayMask") != std::string::npos,
            "busDecayMask uniform must carry the real per-bit open-bus decay mask");
    require(gui.find("_telemetry.c64VicSpriteDma ? 1.0f : 0.0f") != std::string::npos,
            "spriteDma uniform must carry real VIC sprite-DMA state");
    require(gui.find("ArpSIDUIClamp01((float)_telemetry.c64VicCycle / 63.0f)") != std::string::npos,
            "vicBeamX uniform must carry the real within-line VIC beam column");

    // MSL uniform struct and the C++ mirror must agree in width (memcpy layout).
    const int msl = floatFieldsAfter(gui, "struct SidCoreUniforms {");
    const int cpp = floatFieldsAfter(gui, "struct ArpSIDSidCoreMetalUniformsV385");
    std::printf("uniform floats: MSL=%d  C++=%d\n", msl, cpp);
    require(msl == cpp, "MSL and C++ uniform structs must have identical float counts");
    require(msl >= 43, "uniform struct must include the three new VIC/open-bus fields");
    require(gui.find("float busDecayMask;") != std::string::npos &&
            gui.find("float spriteDma;") != std::string::npos &&
            gui.find("float vicBeamX;") != std::string::npos,
            "C++ uniform struct must declare the new VIC/open-bus fields");

    std::printf("openbus_vic_matrix_authentic_v960_tests PASS\n");
    return 0;
}
