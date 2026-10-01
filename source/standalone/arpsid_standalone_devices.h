// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app: audio and MIDI devices (RtAudio, RtMidi).
//
// Audio: one output stream (stereo, float, non-interleaved) on the chosen
// API and device, optionally duplex with an input device that feeds the DIGI
// capture. Linux offers ALSA and PulseAudio (and JACK when built with it);
// Windows WASAPI and DirectSound.
// MIDI: every input port, one port, or none; on Linux (ALSA sequencer) also
// a virtual "ArpSID" input other programs can connect to.
#pragma once

#include "standalone/arpsid_standalone_engine.h"
#include "standalone/arpsid_standalone_settings.h"

#include <memory>
#include <string>
#include <vector>

class RtAudio;
class RtMidiIn;

namespace ArpSID::Standalone {

struct AudioDevice {
    std::string api;          // RtAudio API name ("alsa", "pulse", "wasapi", ...)
    std::string apiDisplay;   // "ALSA", "PulseAudio", ...
    std::string name;
    unsigned outputs = 0, inputs = 0;
    bool isDefaultOutput = false, isDefaultInput = false;
    std::vector<unsigned> sampleRates;
};

// Audio APIs this build has, in preference order.
std::vector<std::string> compiledAudioApis();
std::string audioApiDisplayName(const std::string& api);
// Devices of one API (or every compiled API when <api> is empty).
std::vector<AudioDevice> listAudioDevices(const std::string& api = {});

class AudioIo {
public:
    explicit AudioIo(Engine& engine);
    ~AudioIo();
    AudioIo(const AudioIo&) = delete;
    AudioIo& operator=(const AudioIo&) = delete;

    // Close any stream, prepare the engine and open + start a stream for
    // <s>. Falls back to the API's default output, then to output only (no
    // input), before failing. False with a reason when nothing could open.
    bool open(const Settings& s, std::string& error);
    void close();

    bool running() const;
    std::string api() const { return api_; }
    std::string outputName() const { return outputName_; }
    std::string inputName() const { return inputName_; }
    unsigned sampleRate() const { return sampleRate_; }
    unsigned bufferFrames() const { return bufferFrames_; }
    double latencyMs() const;

private:
    static int callback_(void* out, void* in, unsigned frames, double, unsigned status, void* user);
    bool tryOpen_(const std::string& api, const std::string& output, const std::string& input, unsigned rate,
                  unsigned frames, std::string& error);

    Engine& engine_;
    std::unique_ptr<RtAudio> rt_;
    std::string api_, outputName_, inputName_;
    unsigned sampleRate_ = 0, bufferFrames_ = 0, outChannels_ = 0, inChannels_ = 0;
};

class MidiIo {
public:
    explicit MidiIo(Engine& engine);
    ~MidiIo();
    MidiIo(const MidiIo&) = delete;
    MidiIo& operator=(const MidiIo&) = delete;

    // Input port names (ArpSID's own virtual port left out).
    std::vector<std::string> ports() const;
    // "*" = every port, "" = none, else the port with that name.
    void open(const std::string& selection);
    void close();
    // Reopen "*" when ports came or went (hot-plug); cheap to call often.
    void rescan();
    const std::vector<std::string>& openPorts() const { return openNames_; }
    bool hasVirtualPort() const { return virtual_ != nullptr; }
    const std::string& lastError() const { return error_; }

private:
    static void callback_(double, std::vector<unsigned char>* message, void* user);

    Engine& engine_;
    std::string selection_ = "*";
    std::vector<std::unique_ptr<RtMidiIn>> ins_;
    std::vector<std::string> openNames_;
    std::vector<std::string> knownPorts_;
    std::unique_ptr<RtMidiIn> virtual_;
    std::string error_;
};

} // namespace ArpSID::Standalone
