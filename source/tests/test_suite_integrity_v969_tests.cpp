// SPDX-License-Identifier: BSD-3-Clause
// test_suite_integrity_v969_tests.cpp — v969 closure.
//
// A test-suite audit (v969) found that an over-eager stale-target cleanup had
// orphaned valid test sources from the CMake build (they existed on disk but
// were never compiled or run), and that one concurrency test flaked under a
// loaded parallel CTest. This guard pins the two repairs so they cannot
// silently regress:
//
//  1. auv3_render_scratch_transport_v591 — the AUv3 render-scratch/transport
//     source-contract test — is registered in the CMake build (it was the one
//     genuinely-orphaned source; the dr808_* voice tests were already
//     registered via their own foreach and were never orphaned).
//  2. scope_triple_buffer_multiconsumer_v687 — the multi-consumer telemetry
//     test — ends its run only after BOTH consumers have made progress, so a
//     thread starved during the fixed producer burst under a loaded parallel
//     CTest cannot fail the progress assertion (a scheduling artifact, not a
//     ScopeTripleBuffer defect). Its coherency / no-torn / no-regress
//     invariants are unchanged.
//
// Source-contract test (matches the repo's existing guard style).

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "test_suite_integrity_v969_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), (std::string("cannot open ") + path).c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void requireContains(const std::string& hay, const std::string& needle, const char* msg) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "test_suite_integrity_v969_tests FAIL: " << msg
                  << "\n  missing: " << needle << "\n";
        std::exit(1);
    }
}

} // namespace

int main() {
#ifndef ARPSID_SOURCE_ROOT
#error ARPSID_SOURCE_ROOT must be defined
#endif
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string cmake = readFile(root + "/CMakeLists.txt");

    // (1) The restored orphan is registered (in the restored-tests list) and its
    //     source still exists.
    requireContains(cmake, "auv3_render_scratch_transport_v591",
                    "the AUv3 render-scratch test must stay registered in the build");
    {
        std::ifstream f(root + "/source/tests/auv3_render_scratch_transport_v591_tests.cpp");
        require(static_cast<bool>(f), "auv3 render-scratch test source must exist");
    }
    // Its drifted signature assertion was made linkage-tolerant (constexpr/inline).
    const std::string auv3 = readFile(root + "/source/tests/auv3_render_scratch_transport_v591_tests.cpp");
    requireContains(auv3, "size_t ArpSIDScratchStableRenderFrames() noexcept",
                    "auv3 test must assert the stable-scratch helper by return type + signature");

    // (2) The dr808_* voice tests remain registered via their own foreach (they
    //     were never orphaned; they must not be double-registered either).
    requireContains(cmake, "foreach(_dr808_tgt voice_smoke accent hat_choke determinism peak_headroom)",
                    "the dr808 voice-test foreach registration must remain");

    // (3) The multi-consumer flake fix: the producer waits for both consumers to
    //     progress before ending the run.
    const std::string v687 = readFile(root + "/source/tests/scope_triple_buffer_multiconsumer_v687_tests.cpp");
    requireContains(v687, "readsA.load(std::memory_order_relaxed) > 0 &&",
                    "V687 producer must wait for consumer A progress before ending the run");
    requireContains(v687, "readsB.load(std::memory_order_relaxed) > 0)",
                    "V687 producer must wait for consumer B progress before ending the run");
    // The real correctness invariants must still be asserted.
    requireContains(v687, "no torn snapshot reached either consumer",
                    "V687 must still assert coherency (no torn snapshot)");
    requireContains(v687, "no consumer observed an id going backwards",
                    "V687 must still assert monotonic (no id regression)");

    std::cout << "test_suite_integrity_v969_tests: OK\n";
    return 0;
}
