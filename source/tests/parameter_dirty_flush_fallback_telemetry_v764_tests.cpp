// fix-order #17: parameter automation diagnostics must distinguish a timed
// param-intent queue drop from the dirty-flush fallback that still applies the
// latest parameter value at the next block boundary. v952 strengthens this:
// fallback uses the ordinary side-effect path and supersedes older generations.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!f) {
        std::cerr << "cannot open " << rel << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    const std::string k = readFile("source/au3/ArpSIDDSPKernel.hpp");

    require(k.find("paramIntentDirtyFlushFallbackTelemetry_") != std::string::npos,
            "kernel owns dedicated parameter dirty-flush fallback telemetry");
    require(k.find("paramIntentDirtyFlushFallbackCount()") != std::string::npos,
            "kernel exposes fallback telemetry accessor");
    require(k.find("dirty-flush fallback") != std::string::npos,
            "source documents queue-drop versus block-boundary dirty fallback distinction");

    const std::string pushNeedle =
        "const bool queued = paramIntentQueue_.push(static_cast<uint32_t>(pid), generation,";
    const auto pushPos = k.find(pushNeedle);
    require(pushPos != std::string::npos, "param intent queue push/result assignment found");
    const auto branchEnd = k.find("return queued;", pushPos);
    require(branchEnd != std::string::npos, "param intent drop branch end found");
    const std::string branch = k.substr(pushPos, branchEnd - pushPos);

    require(branch.find("if (!queued)") != std::string::npos,
            "queue-drop telemetry branch is gated by failed enqueue result");

    require(branch.find("ingressDropTelemetry_.fetch_add") != std::string::npos,
            "queue drop still increments generic ingress-drop telemetry");
    require(branch.find("paramIntentDirtyFlushFallbackTelemetry_.fetch_add") != std::string::npos,
            "queue drop also increments dedicated dirty-flush fallback telemetry");
    require(branch.find("publishParamIntentFallback_(pid, generation, clean)") != std::string::npos,
            "failed enqueue publishes a coherent generation-tagged fallback");
    require(k.find("dirty_[(size_t)pid].store(true, std::memory_order_release);") != std::string::npos,
            "fallback publisher marks only failed intents dirty for block-boundary apply");
    require(k.find("runtimeExecutionOwner_->applyProjectedNormalizedParameter(") != std::string::npos,
            "fallback applies through the ordinary parameter side-effect owner");

    require(k.find("paramIntentDirtyFlushFallbackTelemetry_.store(0") != std::string::npos,
            "telemetry reset path clears fallback counter");

    std::cout << "parameter_dirty_flush_fallback_telemetry_v764_tests: PASS\n";
    return 0;
}
