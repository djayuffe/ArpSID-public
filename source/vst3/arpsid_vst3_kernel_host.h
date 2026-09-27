// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VST3 host for the shared ArpSIDDSPKernel.
//
// The VST3 plug-in runs the same DSP kernel as AUv2, AUv3 and the Standalone
// app. This class is the platform-neutral C++ counterpart of the AU
// ArpSIDDSPKernelAdapter: it owns the kernel plus the GUI model state that is
// not expressed as parameters (SETTINGS, MIX, KIT, DIGI model + sample bank)
// and publishes that state to the render thread through the kernel's
// ownership mailboxes.
//
// Threading
//   render():                        audio thread only.
//   everything else:                 any non-realtime thread (host UI/main
//                                    thread, setState, editor). Model access
//                                    is mutex-protected; the kernel APIs used
//                                    here are the ones the AU wrappers call
//                                    from their GUI threads.
#pragma once

#include "arpsid/gui/digi_panel_model.h"
#include "arpsid/gui/digi_sample_bank_v596.h"
#include "arpsid/gui/kit_state_blob.h"
#include "arpsid/gui/mix_panel_model.h"
#include "arpsid/gui/settings_panel_model.h"
#include "common/arpsid_telemetry_snapshot.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace ArpSID {

class ArpSIDDSPKernel;
struct TimedEvent;
struct TransportState;
struct SidStateRootV1;

class Vst3KernelHost final {
public:
    // Render chunk limit (the kernel's kMaxFramesPerBlock); render() splits
    // larger host blocks internally.
    static int maxKernelFrames() noexcept;

    Vst3KernelHost();
    ~Vst3KernelHost();
    Vst3KernelHost(const Vst3KernelHost&) = delete;
    Vst3KernelHost& operator=(const Vst3KernelHost&) = delete;

    // ── lifecycle (non-realtime) ────────────────────────────────────────────
    void setup(double sampleRate, int maxFrames);
    void reset() noexcept;

    // ── render (audio thread) ───────────────────────────────────────────────
    // events: sample-stamped, sorted by sampleOffset (offsets relative to the
    // host block). outputs: numChannels planar buffers of frameCount samples.
    void render(float** outputs, int numChannels, int frameCount,
                const TimedEvent* events, int eventCount,
                const TransportState& transport) noexcept;

    // ── parameters ──────────────────────────────────────────────────────────
    void setParameterNonRealtime(int paramId, float normalized) noexcept;
    float parameter(int paramId) const noexcept;

    // ── patches / state ─────────────────────────────────────────────────────
    // Queue factory patch <slot> (applied at the top of the next render block).
    bool loadFactorySlot(int slot);
    // Queue an arbitrary canonical state root (applied at the next render block).
    void scheduleStateRoot(const SidStateRootV1& root);
    // Current canonical state root (for the controller's parameter mirror).
    void currentStateRoot(SidStateRootV1& out) const noexcept;

    // Full VST3 component state: canonical root + every GUI model.
    std::vector<std::uint8_t> saveState() const;
    // Accepts this format and the legacy Phase2 layouts (v1..v4).
    bool loadState(const std::uint8_t* data, std::size_t size);
    // Decode only the canonical state root of a component state (any version);
    // used by the edit controller to mirror parameters.
    static bool decodeStateRoot(const std::uint8_t* data, std::size_t size, SidStateRootV1& out);

    // ── GUI models ──────────────────────────────────────────────────────────
    GUI::SettingsPanelModel settings() const;
    void setSettings(const GUI::SettingsPanelModel& m);
    GUI::MixPanelModel mix() const;
    void setMix(const GUI::MixPanelModel& m);
    GUI::KitStateBlob kit() const;
    void setKit(const GUI::KitStateBlob& b);
    // DIGI model and sample bank are one persistence/publication unit.
    void digi(GUI::DigiPanelModel& model, GUI::DigiSampleBankBlob& bank) const;
    void setDigi(const GUI::DigiPanelModel& model, const GUI::DigiSampleBankBlob& bank);
    bool setDigiUserSample(int slot, const float* samples, std::uint32_t frameCount,
                           double sampleRate, const char* name);
    void setDigiD418RuntimeMode(std::uint8_t mode, std::uint32_t rateHz) noexcept;
    void digiD418RuntimeMode(std::uint8_t& mode, std::uint32_t& rateHz) const noexcept;
    void setDigiMidiPadMapping(std::uint8_t rootNote, std::uint8_t channelFilter) noexcept;
    void digiMidiPadMapping(std::uint8_t& rootNote, std::uint8_t& channelFilter) const noexcept;
    void triggerDigiPad(std::uint8_t slot, std::uint8_t velocity) noexcept;
    void clearDigiD418Telemetry() noexcept;

    // ── C64 SID player / chip commands ──────────────────────────────────────
    bool loadSidFile(const void* data, std::size_t size, std::uint16_t subtune);
    void unloadSidFile() noexcept;
    // Same numbering as the AU adapter: 1 boot, 2 start, 3 stop, 4 reset,
    // 5 load projection bootstrap, 6/7 VIC fast on/off, 8/9 CPU fast on/off.
    void c64ControlHubCommand(int command) noexcept;
    bool isSidFileLoaded() const noexcept;
    bool c64VicFast() const noexcept;
    bool c64CpuFast() const noexcept;
    void setPureSid1Q1OutputMode(bool on) noexcept;
    bool pureSid1Q1OutputMode() const noexcept;

    // ── MIDI from the editor (non-realtime; queued into the kernel) ─────────
    void injectMidi(const std::uint8_t* data, std::uint8_t length) noexcept;

    // ── telemetry (editor) ──────────────────────────────────────────────────
    void readTelemetry(ArpSIDTelemetry& out, bool includeScopes, bool includeC64Snapshot) const noexcept;

    // Must be polled from a non-realtime thread (editor timer / controller):
    // performs drum-bridge factory slot loads that render deferred.
    void pollNonRealtime() noexcept;

    // Monotonic counter bumped whenever model state changes (editor refresh).
    std::uint64_t modelGeneration() const noexcept;

    ArpSIDDSPKernel& kernel() noexcept { return *kernel_; }

private:
    void publishModelsLocked_(bool includeDigiSampleBank) noexcept;
    void scheduleRoot_(const SidStateRootV1& root);
    void renderBlocks_(float** outputs, int numChannels, int frameCount, const TimedEvent* events,
                       int eventCount, const TransportState& transport) noexcept;

    // A scheduled root is applied by the kernel at the top of the next render
    // block. Until a block has rendered it, saveState() serializes the pending
    // root, so a host that saves while not processing gets the new patch.
    mutable std::mutex pendingRootMutex_;
    std::unique_ptr<SidStateRootV1> pendingRoot_;
    std::atomic<std::uint64_t> scheduledSeq_{0};
    std::atomic<std::uint64_t> renderedSeq_{0};

    std::unique_ptr<ArpSIDDSPKernel> kernel_;
    mutable std::mutex modelMutex_;
    GUI::SettingsPanelModel settings_;
    GUI::MixPanelModel mix_;
    GUI::KitStateBlob kit_;
    GUI::DigiPanelModel digiModel_;
    std::unique_ptr<GUI::DigiSampleBankBlob> digiBank_;
    // Render-thread scratch for splitting oversized host blocks (preallocated).
    std::unique_ptr<std::vector<TimedEvent>> chunkEvents_;
    std::uint64_t modelGeneration_ = 1;
    double sampleRate_ = 44100.0;
    int maxFrames_ = 1024;
};

} // namespace ArpSID
