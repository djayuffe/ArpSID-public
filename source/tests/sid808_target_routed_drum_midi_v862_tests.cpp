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
    if (!stream) {
        std::cerr << "FAIL: missing " << rel << "\n";
        std::exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

int main() {
    const std::string shared = readText("include/arpsid/core/sid_runtime_shared_kernel.h");
    const std::string adapter = readText("include/arpsid/core/sid_runtime_target_adapter.h");
    const std::string backend = readText("include/arpsid/core/sid_runtime_backend_impl.h");
    const std::string kernel = readText("source/au3/ArpSIDDSPKernel.hpp");

    require(contains(shared, "runtime.isDrSidModeEnabled()") &&
            contains(shared, "surface.triggerDrumMidi"),
            "DrSID-mode canonical MIDI dispatch still enters the primitive drum surface");
    require(contains(backend, "canonicalTriggerDrumMidi(drsid, note, velocity);"),
            "base backend still documents the direct canonical DrSID fallback path");

    require(contains(adapter, "void triggerDrumMidi(uint8_t note, float velocity) noexcept override"),
            "target adapter overrides drum NoteOn dispatch");
    require(contains(adapter, "target->runtimeTriggerDrSidNote"),
            "target adapter routes drum NoteOn through the kernel target hook");
    require(contains(adapter, "std::clamp(std::isfinite(velocity) ? velocity : 0.0f, 0.0f, 1.0f)"),
            "target adapter sanitizes/clamps routed drum velocity");
    require(contains(adapter, "SidRuntimeBackendImpl::triggerDrumMidi(note, velocity);"),
            "target adapter preserves direct backend fallback when target is absent");

    require(contains(adapter, "void releaseDrumMidi(uint8_t note) noexcept override"),
            "target adapter overrides drum NoteOff dispatch");
    require(contains(adapter, "target->runtimeReleaseDrSidNote"),
            "target adapter routes drum NoteOff through the kernel target hook");
    require(contains(adapter, "SidRuntimeBackendImpl::releaseDrumMidi(note);"),
            "target adapter preserves direct NoteOff fallback when target is absent");

    require(contains(kernel, "void runtimeTriggerDrSidNote(int note, float velocity) noexcept") &&
            contains(kernel, "componentFlavor_ == ArpSID::ComponentFlavor::Sid808") &&
            contains(kernel, "drumEngineBridge_.noteOn") &&
            contains(kernel, "return; // bridge owns the audio"),
            "kernel target hook owns SID808 bridge NoteOn routing");
    require(contains(kernel, "void runtimeReleaseDrSidNote(int note) noexcept") &&
            contains(kernel, "drumEngineBridge_.router().noteOff"),
            "kernel target hook owns SID808 bridge NoteOff routing");

    std::cout << "sid808_target_routed_drum_midi_v862_tests PASS\n";
    return 0;
}
