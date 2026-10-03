// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app (Linux, Windows): persisted settings.
//
// A small "key = value" text file, one setting per line, in the per-user
// config folder:
//   Linux    $XDG_CONFIG_HOME/ArpSID/ (default ~/.config/ArpSID/)
//   Windows  %APPDATA%\ArpSID
// Unknown keys are ignored and missing keys keep their defaults, so files
// from other versions load. The session (the sound: patch, models, loaded
// tune) is saved next to it as session.arpsidstate.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

#include "arpsid_preset_paths.h"

namespace ArpSID::Standalone {

struct Settings {
    std::string audioApi;          // RtAudio API name ("alsa", "pulse", "jack", "wasapi", "ds"); empty = default
    std::string audioOutput;       // output device name; empty = the system default
    std::string audioInput;        // input device for DIGI capture; empty = none
    unsigned sampleRate = 48000;   // Hz
    unsigned bufferFrames = 256;   // frames per callback
    std::string midiInput = "*";   // "*" = every MIDI input, "" = none, else one port's name
    int midiChannel = 0;           // 0 = omni, 1..16
    double bpm = 120.0;            // internal clock tempo
    double zoom = 1.0;             // window scale (1.0 = 1200 x 834)
    int tab = 0;                   // editor tab
};

inline constexpr unsigned kSampleRates[] = {44100, 48000, 88200, 96000};
inline constexpr unsigned kBufferSizes[] = {64, 128, 256, 512, 1024, 2048};

inline Settings sanitize(Settings s) {
    if (s.sampleRate < 8000 || s.sampleRate > 384000) s.sampleRate = 48000;
    if (s.bufferFrames < 16 || s.bufferFrames > 8192) s.bufferFrames = 256;
    s.midiChannel = std::clamp(s.midiChannel, 0, 16);
    if (!std::isfinite(s.bpm)) s.bpm = 120.0;
    s.bpm = std::clamp(s.bpm, 20.0, 300.0);
    if (!std::isfinite(s.zoom)) s.zoom = 1.0;
    s.zoom = std::clamp(s.zoom, 0.5, 3.0);
    s.tab = std::max(0, s.tab);
    return s;
}

inline std::string serialize(const Settings& s) {
    std::ostringstream o;
    o << "# ArpSID standalone settings\n";
    o << "audio_api = " << s.audioApi << "\n";
    o << "audio_output = " << s.audioOutput << "\n";
    o << "audio_input = " << s.audioInput << "\n";
    o << "sample_rate = " << s.sampleRate << "\n";
    o << "buffer_frames = " << s.bufferFrames << "\n";
    o << "midi_input = " << s.midiInput << "\n";
    o << "midi_channel = " << s.midiChannel << "\n";
    o << "bpm = " << s.bpm << "\n";
    o << "zoom = " << s.zoom << "\n";
    o << "tab = " << s.tab << "\n";
    return o.str();
}

inline Settings parse(const std::string& text) {
    Settings s;
    std::istringstream in(text);
    std::string line;
    auto trim = [](std::string v) {
        const auto b = v.find_first_not_of(" \t\r");
        if (b == std::string::npos) return std::string();
        const auto e = v.find_last_not_of(" \t\r");
        return v.substr(b, e - b + 1);
    };
    auto toU = [](const std::string& v, unsigned def) {
        char* end = nullptr;
        const unsigned long x = std::strtoul(v.c_str(), &end, 10);
        return (end && end != v.c_str()) ? static_cast<unsigned>(x) : def;
    };
    auto toI = [](const std::string& v, int def) {
        char* end = nullptr;
        const long x = std::strtol(v.c_str(), &end, 10);
        return (end && end != v.c_str()) ? static_cast<int>(x) : def;
    };
    auto toD = [](const std::string& v, double def) {
        char* end = nullptr;
        const double x = std::strtod(v.c_str(), &end);
        return (end && end != v.c_str()) ? x : def;
    };
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "audio_api") s.audioApi = val;
        else if (key == "audio_output") s.audioOutput = val;
        else if (key == "audio_input") s.audioInput = val;
        else if (key == "sample_rate") s.sampleRate = toU(val, s.sampleRate);
        else if (key == "buffer_frames") s.bufferFrames = toU(val, s.bufferFrames);
        else if (key == "midi_input") s.midiInput = val;
        else if (key == "midi_channel") s.midiChannel = toI(val, s.midiChannel);
        else if (key == "bpm") s.bpm = toD(val, s.bpm);
        else if (key == "zoom") s.zoom = toD(val, s.zoom);
        else if (key == "tab") s.tab = toI(val, s.tab);
    }
    return sanitize(s);
}

// Per-user config folder (empty when unknown). ARPSID_STANDALONE_CONFIG_DIR
// overrides it (tests, portable installs).
inline std::filesystem::path configFolder() {
    const auto overrideDir = Presets::detail::envPath("ARPSID_STANDALONE_CONFIG_DIR");
    if (!overrideDir.empty()) return overrideDir;
#if defined(_WIN32)
    const auto appData = Presets::detail::envPath("APPDATA");
    return appData.empty() ? std::filesystem::path("ArpSID") : appData / "ArpSID";
#else
    const auto xdg = Presets::detail::envPath("XDG_CONFIG_HOME");
    if (!xdg.empty()) return xdg / "ArpSID";
    const auto home = Presets::detail::envPath("HOME");
    return home.empty() ? std::filesystem::path(".config/ArpSID") : home / ".config" / "ArpSID";
#endif
}

inline std::filesystem::path settingsFile() {
    const auto d = configFolder();
    return d.empty() ? d : d / "standalone.conf";
}

inline std::filesystem::path sessionFile() {
    const auto d = configFolder();
    return d.empty() ? d : d / "session.arpsidstate";
}

} // namespace ArpSID::Standalone
