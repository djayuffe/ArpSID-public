// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app (Linux, Windows): entry point.
//
//   ArpSID                      run the synth
//   ArpSID --list-devices       print audio APIs, devices and MIDI inputs
//   ArpSID --no-audio           run without an audio device
//   ArpSID --no-midi            run without MIDI input
//   ArpSID --config-dir DIR     keep settings and session in DIR
//   ArpSID --quit-after MS      close after MS milliseconds (tests)
//   ArpSID --screenshot FILE    save the window as PNG before quitting
//   ArpSID --version

#include "standalone/arpsid_standalone_app.h"
#include "standalone/arpsid_standalone_platform.h"

#include "arpsid/version.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void attachConsole() {
#if defined(_WIN32)
    // A GUI program: print to the console it was started from, if any.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
#endif
}

void usage() {
    std::printf("ArpSID %s standalone\n\n"
                "usage: ArpSID [options]\n"
                "  --list-devices       print audio APIs, devices and MIDI inputs, then exit\n"
                "  --no-audio           run without an audio device\n"
                "  --no-midi            run without MIDI input\n"
                "  --config-dir DIR     keep settings and the session in DIR\n"
                "  --quit-after MS      close the window after MS milliseconds\n"
                "  --screenshot FILE    save the window as PNG before quitting (with --quit-after)\n"
                "  --version            print the version\n",
                ARPSID_PLUGIN_VERSION);
}

void setEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

int listDevices() {
    using namespace ArpSID::Standalone;
    std::printf("Audio APIs:");
    for (const auto& a : compiledAudioApis()) std::printf(" %s (%s)", a.c_str(), audioApiDisplayName(a).c_str());
    std::printf("\n");
    for (const AudioDevice& d : listAudioDevices())
        std::printf("  [%s] %s  out %u  in %u%s%s\n", d.api.c_str(), d.name.c_str(), d.outputs, d.inputs,
                    d.isDefaultOutput ? "  (default output)" : "", d.isDefaultInput ? "  (default input)" : "");
    Engine engine;
    MidiIo midi(engine);
    const auto ports = midi.ports();
    std::printf("MIDI inputs: %zu\n", ports.size());
    for (const auto& p : ports) std::printf("  %s\n", p.c_str());
    std::printf("Config folder: %s\n", ArpSID::Presets::pathToUtf8(configFolder()).c_str());
    std::printf("Preset folder: %s\n", ArpSID::Presets::pathToUtf8(ArpSID::Presets::userPresetFolder()).c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    ArpSID::Standalone::Options opts;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--list-devices") list = true;
        else if (a == "--no-audio") opts.audio = false;
        else if (a == "--no-midi") opts.midi = false;
        else if (a == "--config-dir") setEnv("ARPSID_STANDALONE_CONFIG_DIR", next());
        else if (a == "--quit-after") opts.quitAfterMs = std::atoi(next().c_str());
        else if (a == "--screenshot") opts.screenshotPath = next();
        else if (a == "--version") {
            attachConsole();
            std::printf("ArpSID %s\n", ARPSID_PLUGIN_VERSION);
            return 0;
        } else if (a == "--help" || a == "-h") {
            attachConsole();
            usage();
            return 0;
        } else {
            attachConsole();
            std::fprintf(stderr, "unknown option: %s\n\n", a.c_str());
            usage();
            return 2;
        }
    }
    if (list || opts.quitAfterMs >= 0) attachConsole();
    if (list) return listDevices();

    ArpSID::Standalone::App app(opts);
    std::string error;
    if (!app.start(error)) {
        std::fprintf(stderr, "ArpSID: %s\n", error.c_str());
        return 1;
    }
    const int rc = ArpSID::Standalone::runWindow(app);
    app.shutdown();
    return rc;
}
