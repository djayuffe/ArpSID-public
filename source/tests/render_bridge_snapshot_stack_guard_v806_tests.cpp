#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readText(const char* rel) {
    std::ifstream stream(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!stream) { std::cerr << "FAIL: missing " << rel << '\n'; std::exit(1); }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}

static std::string structBody(const std::string& text, const std::string& name) {
    const std::size_t pos = text.find(name);
    require(pos != std::string::npos, "BridgeTransactionSnapshot not found");
    const std::size_t open = text.find('{', pos);
    require(open != std::string::npos, "BridgeTransactionSnapshot open not found");
    const std::size_t close = text.find("};", open);
    require(close != std::string::npos, "BridgeTransactionSnapshot close not found");
    return text.substr(open, close - open + 2);
}

int main() {
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");
    const std::string platform = readText("include/arpsid/core/c64_platform.h");
    const std::string runtime = readText("include/arpsid/core/c64_psid_runtime.h");
    const std::string body = structBody(kernel, "struct BridgeTransactionSnapshot final");
    require(body.find("C64Platform") == std::string::npos,
            "BridgeTransactionSnapshot must not contain full C64Platform on stack");
    require(body.find("C64Phi2Machine::Snapshot") == std::string::npos,
            "BridgeTransactionSnapshot must not contain full PHI2 machine on stack");
    require(body.find("C64SidBridgeState::Snapshot") == std::string::npos,
            "BridgeTransactionSnapshot must not contain full bridge state on stack");
    require(kernel.find("c64BridgeRollbackPlatformScratch_") == std::string::npos,
            "DSP kernel must not own a full C64Platform rollback copy");
    require(kernel.find("beginRenderTransaction()") != std::string::npos,
            "beginBridgeTransaction_ must start the C64Runtime render transaction");
    require(runtime.find("beginRenderMutationJournal()") != std::string::npos,
            "C64Runtime render transaction must start C64Platform render mutation journal");
    require(runtime.find("rollbackRenderMutationJournal()") != std::string::npos,
            "C64Runtime render transaction must restore through C64Platform render mutation journal");
    require(runtime.find("commitRenderMutationJournal()") != std::string::npos,
            "complete render transactions must commit the render mutation journal");
    require(runtime.find("C64Phi2Machine::Snapshot renderTxPhi2Snapshot_") != std::string::npos,
            "C64Runtime must keep PHI2 transaction snapshots off the audio stack");
    require(platform.find("struct RenderMutationJournal final") != std::string::npos,
            "C64Platform must own the bounded render mutation journal");
    require(platform.find("std::array<DirtyByteEntry, kMaxDirtyRam>") != std::string::npos,
            "render mutation journal must use bounded RAM dirty-cell storage");
    require(platform.find("std::array<DirtyByteEntry, kMaxDirtyColor>") != std::string::npos,
            "render mutation journal must use bounded ColorRAM dirty-cell storage");

    std::cout << "RenderBridgeSnapshotStackGuardV806Tests PASS\n";
    return 0;
}
