// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app: audio and MIDI devices (see arpsid_standalone_devices.h).

#include "standalone/arpsid_standalone_devices.h"

#include "RtAudio.h"
#include "RtMidi.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace ArpSID::Standalone {

namespace {

// ALSA sequencer port names end in " <client>:<port>", numbers that change
// between sessions; compare without them so a saved choice survives.
std::string stablePortName(const std::string& n) {
    std::size_t end = n.size();
    std::size_t i = end;
    while (i > 0 && std::isdigit(static_cast<unsigned char>(n[i - 1]))) --i;
    if (i == end || i == 0 || n[i - 1] != ':') return n;
    std::size_t j = i - 1;
    while (j > 0 && std::isdigit(static_cast<unsigned char>(n[j - 1]))) --j;
    if (j == i - 1 || j == 0 || n[j - 1] != ' ') return n;
    return n.substr(0, j - 1);
}

bool isOwnPort(const std::string& n) { return n.find("ArpSID") != std::string::npos; }

RtAudio::Api apiByName(const std::string& api) {
    return api.empty() ? RtAudio::UNSPECIFIED : RtAudio::getCompiledApiByName(api);
}

std::unique_ptr<RtAudio> makeRtAudio(RtAudio::Api api) {
    // Errors come back as return values; keep RtAudio quiet on stderr.
    auto rt = std::make_unique<RtAudio>(api, [](RtAudioErrorType, const std::string&) {});
    rt->showWarnings(false);
    return rt;
}

} // namespace

std::vector<std::string> compiledAudioApis() {
    std::vector<RtAudio::Api> apis;
    RtAudio::getCompiledApi(apis);
    std::vector<std::string> out;
    for (RtAudio::Api a : apis)
        if (a != RtAudio::RTAUDIO_DUMMY) out.push_back(RtAudio::getApiName(a));
    return out;
}

std::string audioApiDisplayName(const std::string& api) {
    const RtAudio::Api a = apiByName(api);
    return a == RtAudio::UNSPECIFIED ? api : RtAudio::getApiDisplayName(a);
}

std::vector<AudioDevice> listAudioDevices(const std::string& api) {
    std::vector<AudioDevice> out;
    const std::vector<std::string> apis = api.empty() ? compiledAudioApis() : std::vector<std::string>{api};
    for (const std::string& name : apis) {
        const RtAudio::Api a = apiByName(name);
        if (a == RtAudio::UNSPECIFIED) continue;
        auto rt = makeRtAudio(a);
        for (unsigned id : rt->getDeviceIds()) {
            const RtAudio::DeviceInfo info = rt->getDeviceInfo(id);
            if (info.name.empty()) continue;
            AudioDevice d;
            d.api = name;
            d.apiDisplay = RtAudio::getApiDisplayName(a);
            d.name = info.name;
            d.outputs = info.outputChannels;
            d.inputs = info.inputChannels;
            d.isDefaultOutput = info.isDefaultOutput;
            d.isDefaultInput = info.isDefaultInput;
            d.sampleRates = info.sampleRates;
            out.push_back(std::move(d));
        }
    }
    return out;
}

// ── audio ────────────────────────────────────────────────────────────────────

AudioIo::AudioIo(Engine& engine) : engine_(engine) {}

AudioIo::~AudioIo() { close(); }

bool AudioIo::running() const { return rt_ && rt_->isStreamRunning(); }

double AudioIo::latencyMs() const {
    if (!rt_ || !rt_->isStreamOpen() || sampleRate_ == 0) return 0.0;
    const long frames = rt_->getStreamLatency();
    return 1000.0 * static_cast<double>(frames > 0 ? frames : static_cast<long>(bufferFrames_)) / sampleRate_;
}

void AudioIo::close() {
    if (!rt_) return;
    if (rt_->isStreamRunning()) rt_->stopStream();
    if (rt_->isStreamOpen()) rt_->closeStream();
    rt_.reset();
    outChannels_ = inChannels_ = 0;
}

int AudioIo::callback_(void* out, void* in, unsigned frames, double, unsigned status, void* user) {
    auto* self = static_cast<AudioIo*>(user);
    if (status & (RTAUDIO_OUTPUT_UNDERFLOW | RTAUDIO_INPUT_OVERFLOW)) self->engine_.noteXrun();
    // Non-interleaved: channel c starts at c * frames.
    float* o = static_cast<float*>(out);
    float* outs[8] = {};
    const int nOut = static_cast<int>(std::min(self->outChannels_, 8u));
    for (int c = 0; c < nOut; ++c) outs[c] = o + static_cast<std::size_t>(c) * frames;
    const float* ins[2] = {};
    const int nIn = in ? static_cast<int>(std::min(self->inChannels_, 2u)) : 0;
    for (int c = 0; c < nIn; ++c) ins[c] = static_cast<const float*>(in) + static_cast<std::size_t>(c) * frames;
    self->engine_.process(outs, nOut, nIn ? ins : nullptr, nIn, static_cast<int>(frames));
    return 0;
}

bool AudioIo::tryOpen_(const std::string& api, const std::string& output, const std::string& input, unsigned rate,
                       unsigned frames, std::string& error) {
    close();
    auto rt = makeRtAudio(apiByName(api));
    if (rt->getCurrentApi() == RtAudio::RTAUDIO_DUMMY) {
        error = "no audio API is available";
        return false;
    }
    unsigned outId = 0, inId = 0;
    unsigned outCh = 0, inCh = 0;
    std::string outName, inName;
    for (unsigned id : rt->getDeviceIds()) {
        const RtAudio::DeviceInfo info = rt->getDeviceInfo(id);
        if (info.outputChannels > 0 && ((output.empty() && info.isDefaultOutput) || info.name == output) && !outCh) {
            outId = id;
            outCh = std::min(info.outputChannels, 2u);
            outName = info.name;
        }
        if (!input.empty() && info.inputChannels > 0 && info.name == input && !inCh) {
            inId = id;
            inCh = std::min(info.inputChannels, 2u);
            inName = info.name;
        }
    }
    if (!outCh && output.empty()) {
        // No device flagged default: take the first output.
        for (unsigned id : rt->getDeviceIds()) {
            const RtAudio::DeviceInfo info = rt->getDeviceInfo(id);
            if (info.outputChannels > 0) {
                outId = id;
                outCh = std::min(info.outputChannels, 2u);
                outName = info.name;
                break;
            }
        }
    }
    if (!outCh) {
        error = output.empty() ? "no audio output device" : "audio output \"" + output + "\" not found";
        return false;
    }
    RtAudio::StreamParameters op;
    op.deviceId = outId;
    op.nChannels = outCh;
    RtAudio::StreamParameters ip;
    ip.deviceId = inId;
    ip.nChannels = inCh;
    RtAudio::StreamOptions opts;
    opts.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_SCHEDULE_REALTIME | RTAUDIO_MINIMIZE_LATENCY;
    opts.streamName = "ArpSID";
    unsigned bufferFrames = frames;
    // The engine must be ready for the rate before the callback can run.
    engine_.prepare(static_cast<double>(rate), static_cast<int>(std::max(frames, 4096u)));
    outChannels_ = outCh;
    inChannels_ = inCh;
    if (rt->openStream(&op, inCh ? &ip : nullptr, RTAUDIO_FLOAT32, rate, &bufferFrames, &AudioIo::callback_, this,
                       &opts) != RTAUDIO_NO_ERROR) {
        error = rt->getErrorText();
        if (error.empty()) error = "cannot open the audio device";
        outChannels_ = inChannels_ = 0;
        return false;
    }
    const unsigned actualRate = rt->getStreamSampleRate();
    if (actualRate != 0 && actualRate != rate) engine_.prepare(static_cast<double>(actualRate),
                                                              static_cast<int>(std::max(bufferFrames, 4096u)));
    if (rt->startStream() != RTAUDIO_NO_ERROR) {
        error = rt->getErrorText();
        if (error.empty()) error = "cannot start the audio stream";
        rt->closeStream();
        outChannels_ = inChannels_ = 0;
        return false;
    }
    rt_ = std::move(rt);
    api_ = RtAudio::getApiName(rt_->getCurrentApi());
    outputName_ = outName;
    inputName_ = inName;
    sampleRate_ = actualRate ? actualRate : rate;
    bufferFrames_ = bufferFrames;
    return true;
}

bool AudioIo::open(const Settings& s, std::string& error) {
    std::string e1, e2, e3;
#if defined(__linux__)
    // No API chosen yet: on desktops the sound server (PulseAudio or
    // PipeWire's Pulse service) usually owns the hardware, so try it first.
    if (s.audioApi.empty()) {
        const auto apis = compiledAudioApis();
        if (std::find(apis.begin(), apis.end(), "pulse") != apis.end() &&
            tryOpen_("pulse", s.audioOutput, s.audioInput, s.sampleRate, s.bufferFrames, e2))
            return true;
    }
#endif
    if (tryOpen_(s.audioApi, s.audioOutput, s.audioInput, s.sampleRate, s.bufferFrames, e1)) return true;
    // The saved device may be gone: the API's default output, same input.
    if (!s.audioOutput.empty() && tryOpen_(s.audioApi, {}, s.audioInput, s.sampleRate, s.bufferFrames, e2)) {
        error = e1;
        return true;
    }
    // Duplex may not be possible with this pair: output only.
    if (!s.audioInput.empty() && tryOpen_(s.audioApi, {}, {}, s.sampleRate, s.bufferFrames, e3)) {
        error = e1;
        return true;
    }
    // Any API.
    if (!s.audioApi.empty() && tryOpen_({}, {}, {}, s.sampleRate, s.bufferFrames, e3)) {
        error = e1;
        return true;
    }
    error = e1;
    return false;
}

// ── MIDI ─────────────────────────────────────────────────────────────────────

MidiIo::MidiIo(Engine& engine) : engine_(engine) {}

MidiIo::~MidiIo() { close(); }

void MidiIo::callback_(double, std::vector<unsigned char>* message, void* user) {
    if (!message || message->empty()) return;
    static_cast<MidiIo*>(user)->engine_.midiIn(message->data(), message->size());
}

std::vector<std::string> MidiIo::ports() const {
    std::vector<std::string> out;
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, "ArpSID probe");
        const unsigned n = probe.getPortCount();
        for (unsigned i = 0; i < n; ++i) {
            const std::string name = probe.getPortName(i);
            if (!name.empty() && !isOwnPort(name)) out.push_back(stablePortName(name));
        }
    } catch (...) {
    }
    return out;
}

void MidiIo::close() {
    for (auto& in : ins_) {
        try {
            in->cancelCallback();
            in->closePort();
        } catch (...) {
        }
    }
    ins_.clear();
    openNames_.clear();
    if (virtual_) {
        try {
            virtual_->cancelCallback();
            virtual_->closePort();
        } catch (...) {
        }
        virtual_.reset();
    }
}

void MidiIo::open(const std::string& selection) {
    close();
    selection_ = selection;
    error_.clear();
    knownPorts_ = ports();
#if defined(__linux__)
    // A virtual input other programs (a DAW, aconnect) can connect to.
    try {
        virtual_ = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "ArpSID");
        virtual_->ignoreTypes(true, true, true);
        virtual_->setCallback(&MidiIo::callback_, this);
        virtual_->openVirtualPort("ArpSID MIDI In");
    } catch (...) {
        virtual_.reset();
    }
#endif
    if (selection.empty()) return;
    try {
        RtMidiIn probe(RtMidi::UNSPECIFIED, "ArpSID probe");
        const unsigned n = probe.getPortCount();
        for (unsigned i = 0; i < n; ++i) {
            const std::string raw = probe.getPortName(i);
            const std::string name = stablePortName(raw);
            if (raw.empty() || isOwnPort(raw)) continue;
            if (selection != "*" && name != selection) continue;
            try {
                auto in = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "ArpSID");
                // Sysex and active sensing are ignored; timing (Start/Stop/Continue) passes.
                in->ignoreTypes(true, false, true);
                in->setCallback(&MidiIo::callback_, this);
                in->openPort(i, "ArpSID In");
                ins_.push_back(std::move(in));
                openNames_.push_back(name);
            } catch (const RtMidiError& e) {
                error_ = e.getMessage();
            }
        }
    } catch (const RtMidiError& e) {
        error_ = e.getMessage();
    } catch (...) {
        error_ = "MIDI is not available";
    }
    if (selection != "*" && openNames_.empty() && error_.empty()) error_ = "MIDI input \"" + selection + "\" not found";
}

void MidiIo::rescan() {
    if (selection_.empty()) return;
    const std::vector<std::string> now = ports();
    if (now == knownPorts_) return;
    open(selection_);
}

} // namespace ArpSID::Standalone
