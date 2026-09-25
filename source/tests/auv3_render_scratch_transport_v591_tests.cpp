// auv3_render_scratch_transport_v591_tests.cpp
//
// Pins the v591 AUv3 render-path audit guard:
// * planar render scratch is reserved at the hard max-frame ceiling so
// setMaximumFramesToRender / allocation refreshes do not reallocate under
// an already-returned internalRenderBlock;
// * interleaved scratch use checks the captured scratch generation before
// and after use;
// * both planar buffers are validated before std::fill/interleave;
// * chunk beat positions are derived from the original block transport; and
// * transport seqlock reads retry more than the old 3-attempt budget.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#error "ARPSID_SOURCE_ROOT must be defined by CMake for this source-shape test"
#endif

namespace {

std::string readTextFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "FAIL: could not open " << path << "\n";
        std::abort();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

void requireContains(const std::string& text, const std::string& needle, const char* msg) {
    require(text.find(needle) != std::string::npos, msg);
}

void requireAbsent(const std::string& text, const std::string& needle, const char* msg) {
    require(text.find(needle) == std::string::npos, msg);
}

void requireAfter(const std::string& text,
                  const std::string& first,
                  const std::string& second,
                  const char* msg) {
    const auto a = text.find(first);
    const auto b = text.find(second);
    require(a != std::string::npos, msg);
    require(b != std::string::npos, msg);
    require(b > a, msg);
}

} // namespace

int main() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string auPath = root + "/source/au3/ArpSIDAudioUnit.mm";
    const std::string transportPath = root + "/include/arpsid/core/host_transport_snapshot.h";
    const std::string au = readTextFile(auPath);
    const std::string transport = readTextFile(transportPath);

    // v965+: the helper is now `static constexpr` (was `static inline`), backed
    // by compile-time static_asserts. Match on the return type + signature so the
    // stable-capacity-helper contract holds under either linkage spelling.
    requireContains(au, "size_t ArpSIDScratchStableRenderFrames() noexcept",
                    "AUv3 has a stable render-scratch capacity helper");
    requireContains(au, "return ArpSIDScratchReserveFramesForMaxFrames(kArpSIDHardMaxFrames);",
                    "stable render scratch is sized from the hard max-frame ceiling");
    requireAfter(au,
                 "ArpSIDEnsureFloatScratchCapacity(_renderPlanarL, ArpSIDScratchStableRenderFrames());",
                 "self.maximumFramesToRender = kArpSIDDefaultMaxFrames;",
                 "init reserves stable planar scratch before the first max-frame setter");
    requireContains(au, "const size_t reserveFrames = ArpSIDScratchStableRenderFrames();",
                    "setMaximumFramesToRender repairs to stable scratch capacity");
    requireContains(au, "const size_t allocFrames = ArpSIDScratchStableRenderFrames();",
                    "allocation and standalone refresh use stable scratch capacity");
    requireAbsent(au, "ArpSIDScratchReserveFramesForMaxFrames(clamped)",
                  "setMaximumFramesToRender no longer sizes scratch to transient host max");
    requireAbsent(au, "ArpSIDScratchReserveFramesForMaxFrames((AUAudioFrameCount)maxFrames)",
                  "allocateRenderResources no longer sizes scratch to transient host max");
    requireAbsent(au, "ArpSIDScratchReserveFramesForMaxFrames(safeMaxFrames)",
                  "prepareStandalone no longer sizes scratch to transient host max");

    // audit P0.2/P0.3: scratch validity is now based on entry/exit epoch STABILITY,
    // not equality to a block-creation-time captured epoch (which went permanently
    // silent after allocateRenderResources bumped the epoch).
    requireContains(au, "const uint64_t scratchEpochAtEntry = scratchEpoch->load(std::memory_order_acquire);",
                    "render loads the scratch epoch at entry");
    requireContains(au, "((scratchEpochAtEntry & 1u) == 0u)",
                    "render refuses scratch use while a resize epoch is in progress");
    requireContains(au, "planarL && planarR && planarCap >= frameCount",
                    "interleaved path validates both planar scratch buffers");
    requireContains(au, "const uint64_t scratchEpochAtExit = scratchEpoch->load(std::memory_order_acquire);",
                    "interleaved path rechecks scratch epoch before copying to host output");
    requireContains(au, "scratchEpochAtExit != scratchEpochAtEntry",
                    "interleaved path fails closed if scratch epoch changed during render");
    // Forbidden: the old creation-time captured-epoch model must be gone (audit P0.2).
    requireAbsent(au, "capturedScratchEpoch",
                  "creation-time captured scratch epoch must be removed (audit P0.2)");

    requireContains(au, "chunkTransport.beatPosition = transport.beatPosition + (double)chunkStart * beatsPerSample;",
                    "chunk beat position is derived from the original block transport");
    requireAbsent(au, "chunkTransport.beatPosition += (double)chunkStart * beatsPerSample;",
                  "chunk beat position no longer uses incremental += form");

    requireContains(au, "for (int retry = 0; retry < 8; ++retry)",
                    "AUv3 render transport snapshot retries exceed the old 3-attempt budget");
    requireContains(transport, "for (int retry = 0; retry < 8; ++retry)",
                    "shared HostTransportSnapshotSeqlock retries exceed the old 3-attempt budget");

    std::cout << "auv3_render_scratch_transport_v591_tests: render scratch and transport audit pins passed\n";
    return 0;
}
