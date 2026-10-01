// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app (Linux, Windows): the engine side.
//
// Wraps the shared Vst3KernelHost (the same kernel the plug-ins run) with
// what a standalone app adds around it, independent of any audio or MIDI
// library so it can be tested headless:
//   - process(): the audio callback body (render + DIGI capture input), with
//     an internal clock (tempo, play/stop, beat position) as the transport;
//   - midiIn(): MIDI from any thread, with an input channel filter; MIDI
//     Start / Continue / Stop drive the clock;
//   - the current patch's name (factory or user) for the editor header;
//   - the session (the sound plus the patch name) as one blob, saved on exit
//     and restored on launch.
#pragma once

#include "vst3/arpsid_vst3_kernel_host.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ArpSID {
struct SidStateRootV1;
}

namespace ArpSID::Standalone {

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    Vst3KernelHost& host() noexcept { return *host_; }

    // Non-realtime, with audio stopped: sample rate and largest block.
    void prepare(double sampleRate, int maxFrames);
    double sampleRate() const noexcept { return sampleRate_; }

    // Audio thread: render <frames> into <numOut> planar outputs (1 or 2)
    // and feed <numIn> planar inputs (0..2) to the DIGI capture. Outputs past
    // the second are cleared.
    void process(float* const* outputs, int numOut, const float* const* inputs, int numIn, int frames) noexcept;

    // MIDI from any thread (one complete message). Channel messages outside
    // the channel filter are dropped; 0xFA / 0xFB / 0xFC start, continue and
    // stop the clock; other system messages are ignored.
    void midiIn(const std::uint8_t* data, std::size_t length) noexcept;
    // MIDI Program Change selects a factory patch, as a host's program list
    // does for the plug-in: program N (+ 128 x Bank Select MSB) = slot.
    // midiIn() queues it; the UI thread applies it here (true if one came).
    bool applyPendingProgramChange();
    // MIDI from the editor's keyboard: never filtered.
    void uiMidi(const std::uint8_t* data, std::size_t length) noexcept;
    void setMidiChannel(int channel) noexcept; // 0 = omni, 1..16
    int midiChannel() const noexcept { return midiChannel_.load(std::memory_order_relaxed); }
    // Note-off for everything on every channel (sustain released).
    void panic() noexcept;
    // Time of the last accepted MIDI message (steady clock, ms; 0 = never).
    std::uint64_t lastMidiMs() const noexcept { return lastMidiMs_.load(std::memory_order_relaxed); }

    // Internal clock.
    void setTempo(double bpm) noexcept;
    double tempo() const noexcept { return bpm_.load(std::memory_order_relaxed); }
    void setPlaying(bool playing) noexcept;
    bool playing() const noexcept { return playing_.load(std::memory_order_relaxed); }
    double beatPosition() const noexcept { return beat_.load(std::memory_order_relaxed); }

    // Patches (UI thread).
    void selectFactoryPatch(int slot);
    void loadPatch(const SidStateRootV1& root, const std::string& name);
    int currentFactorySlot() const;  // the playing patch's bank slot
    bool isUserPatch() const;        // a non-factory patch is playing
    std::string patchName() const;
    void setUserPatchName(const std::string& name); // after "save preset as"

    // Session: the full engine state plus the patch name.
    std::vector<std::uint8_t> saveSession() const;
    bool loadSession(const std::uint8_t* data, std::size_t size);

    // Load meter: render time / block time (0..1+), and audio callbacks that
    // reported an underrun / overrun (RtAudio status). Audio thread writes.
    float load() const noexcept { return load_.load(std::memory_order_relaxed); }
    void noteXrun() noexcept { xruns_.fetch_add(1, std::memory_order_relaxed); }
    std::uint32_t xruns() const noexcept { return xruns_.load(std::memory_order_relaxed); }

private:
    bool channelAccepted_(std::uint8_t status) const noexcept;

    std::unique_ptr<Vst3KernelHost> host_;
    double sampleRate_ = 48000.0;
    std::atomic<int> midiChannel_{0};
    std::atomic<double> bpm_{120.0};
    std::atomic<bool> playing_{false};
    std::atomic<double> beat_{0.0};
    std::atomic<bool> rewind_{false};
    std::atomic<float> load_{0.0f};
    std::atomic<std::uint32_t> xruns_{0};
    std::atomic<std::uint64_t> lastMidiMs_{0};
    std::atomic<int> pendingProgram_{-1};
    std::atomic<int> bankMsb_[16] = {};
    // Patch identity (UI thread): the user patch's name and the bank slot it
    // was loaded with; a different bank slot (e.g. a MIDI program change)
    // means a factory patch replaced it.
    std::string userName_;
    int userSlot_ = -1;
};

} // namespace ArpSID::Standalone
