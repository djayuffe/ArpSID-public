// Copyright (C) 2024-2026 Ulf Bertilsson
// Regression guard for the Logic "AU host crashes loading ArpSID" bug (v836).
//
// The simple 64-byte misalignment theory was disproven empirically: make_unique,
// make_shared, and shared_ptr(new) all produced 64-byte-aligned ArpSIDDSPKernel
// objects in the local harness. Keep this guard's old filename for CMake
// compatibility, but test the corrected boundary: Logic's sandboxed/OOP bridge is
// sensitive to the ownership/allocation/lifetime shape change.
// The adapter must stay single-owner and must not reintroduce shared kernel
// ownership, shared construction, or async local shared kernel capture.
#include <cassert>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <new>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!f) { std::cerr << "missing file: " << rel << "\n"; std::abort(); }
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

static void require(bool cond, const char* msg) {
    if (!cond) { std::cerr << "FAIL: " << msg << "\n"; std::abort(); }
}

int main() {
    const std::string adapter = readFile("source/au3/ArpSIDDSPKernelAdapter.mm");

    // 1. Keep the known-good single-owner allocation shape.
    require(adapter.find("std::unique_ptr<ArpSID::ArpSIDDSPKernel> _kernel;")
                != std::string::npos,
            "ArpSIDDSPKernelAdapter must store the DSP kernel as unique_ptr");
    require(adapter.find("std::make_unique<ArpSID::ArpSIDDSPKernel>()")
                != std::string::npos,
            "ArpSIDDSPKernelAdapter must construct the DSP kernel with make_unique");

    // 2. Block the corrected bad shape, not just the old make_shared spelling.
    require(adapter.find("std::shared_ptr<ArpSID::ArpSIDDSPKernel> _kernel")
                == std::string::npos,
            "ArpSIDDSPKernelAdapter must not shared-own the DSP kernel");
    require(adapter.find("make_shared<ArpSID::ArpSIDDSPKernel>") == std::string::npos,
            "ArpSIDDSPKernelAdapter must not make_shared the DSP kernel");
    require(adapter.find("allocate_shared<ArpSID::ArpSIDDSPKernel>") == std::string::npos,
            "ArpSIDDSPKernelAdapter must not allocate_shared the DSP kernel");
    require(adapter.find("std::shared_ptr<ArpSID::ArpSIDDSPKernel> kernel = _kernel")
                == std::string::npos,
            "ArpSIDDSPKernelAdapter must not locally capture a shared DSP kernel");

    // 3. Keep PSID loading on the synchronous known-good path.
    require(adapter.find("return _kernel->loadPsidData(bytes,") != std::string::npos,
            "PSID file loading must call loadPsidData synchronously");
    require(adapter.find("loadPsidDataForRequest(payload->data(), payload->size(), requestedSubtune, requestTicket)")
                == std::string::npos,
            "PSID file loading must not use the async request-ticket path");

    std::cout << "auv2_kernel_overaligned_no_make_shared_v836_tests: PASS\n";
    return 0;
}
