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

// VST3-only host bypass parameter (ParameterInfo::kIsBypass). It lives
// outside the shared 0..kNumParams-1 space so AU/Standalone ids are
// unchanged; the plug-in keeps running while bypassed and fades its output.
constexpr int kVst3BypassParamId = 1024;

// VST3 component state layout (see arpsid_vst3_kernel_host.cpp): u32 version
// 5, then chunks of u32 tag, u32 length, payload (all little-endian).
constexpr std::uint32_t kVst3StateVersion = 5u;
constexpr std::uint32_t kVst3StateTagRoot = 0x524F4F54u;   // 'ROOT' canonical state root
// Marks a patch-only state, as saved in the factory .vstpreset files: it
// carries just the ROOT chunk and loading it changes the patch exactly like a
// program selection. Bypass, the loaded .sid tune and the GUI models (MIX,
// KIT, DIGI, settings) are left as they are.
constexpr std::uint32_t kVst3StateTagPreset = 0x50525354u; // 'PRST'

class ArpSIDDSPKernel;
struct TimedEvent;
struct TransportState;
struct SidStateRootV1;

class Vst3KernelHost final {
public:
    // Render chunk limit (the kernel's kMaxFramesPerBlock); the kernel splits
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
    // Patch-only state (kVst3StateTagPreset + ROOT) of a canonical state root,
    // as saved in .vstpreset files; empty for an invalid root.
    static std::vector<std::uint8_t> encodePresetState(const SidStateRootV1& root);
    // True for a patch-only state (kVst3StateTagPreset).
    static bool isPresetState(const std::uint8_t* data, std::size_t size) noexcept;

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
    // The host keeps a copy of the loaded file so the tune (and its subtune)
    // is saved with the project (SIDF state chunk) and subtunes can be
    // switched later, including after a project reload.
    bool loadSidFile(const void* data, std::size_t size, std::uint16_t subtune);
    void unloadSidFile() noexcept;
    // Re-initialise the loaded tune at <subtune> (0-based); false if none.
    bool selectSidSubtune(std::uint16_t subtune);
    std::uint16_t sidSubtune() const noexcept;
    std::size_t sidFileSize() const noexcept;
    // Same numbering as the AU adapter: 1 boot, 2 start, 3 stop, 4 reset,
    // 5 load projection bootstrap, 6/7 VIC fast on/off, 8/9 CPU fast on/off.
    void c64ControlHubCommand(int command) noexcept;
    bool isSidFileLoaded() const noexcept;
    bool c64VicFast() const noexcept;
    bool c64CpuFast() const noexcept;
    void setPureSid1Q1OutputMode(bool on) noexcept;
    bool pureSid1Q1OutputMode() const noexcept;

    // ── DIGI capture from the "DIGI Capture In" audio input ────────────────
    // armDigiCapture (UI thread) prepares a mono buffer of up to
    // GUI::kDigiRecordCaptureMaxFrames and starts recording; the audio thread
    // feeds the input with captureDigiInput; stopDigiCapture ends the take
    // and stores it in <slot> (resampled to 8 kHz 4-bit $D418, <= 7.5 s).
    bool armDigiCapture(int slot);
    bool stopDigiCapture(const char* name);     // false if nothing was recorded
    void cancelDigiCapture() noexcept;
    void captureDigiInput(const float* const* inputs, int numChannels, int frameCount) noexcept; // audio thread
    void setDigiCaptureInputActive(bool active) noexcept { captureInputActive_.store(active, std::memory_order_relaxed); }
    struct DigiCaptureStatus {
        bool armed = false;
        bool inputActive = false;   // the host feeds the capture input bus
        int slot = 0;
        std::uint32_t frames = 0;   // recorded so far (host rate)
        double seconds = 0.0;
        float peak = 0.f;           // input peak of the take
        bool full = false;          // buffer full: stop to keep the take
    };
    DigiCaptureStatus digiCaptureStatus() const noexcept;

    // ── host bypass (kVst3BypassParamId; saved in the state) ───────────────
    void setBypass(bool on) noexcept { bypass_.store(on, std::memory_order_relaxed); }
    bool bypass() const noexcept { return bypass_.load(std::memory_order_relaxed); }
    // Reads the bypass flag from saved state (false when the state has none).
    static bool decodeBypass(const std::uint8_t* data, std::size_t size) noexcept;

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

    // A scheduled root is applied by the kernel at the top of the next render
    // block. Until a block has rendered it, saveState() serializes the pending
    // root, so a host that saves while not processing gets the new patch.
    mutable std::mutex pendingRootMutex_;
    std::unique_ptr<SidStateRootV1> pendingRoot_;
    std::atomic<std::uint64_t> scheduledSeq_{0};
    std::atomic<std::uint64_t> renderedSeq_{0};

    std::unique_ptr<ArpSIDDSPKernel> kernel_;
    // DIGI capture: buffer owned by the UI side, written by the audio thread
    // only while captureArmed_; captureBusy_ brackets each audio-thread write
    // so stop waits for an in-flight block before reading.
    std::vector<float> captureBuffer_;
    std::atomic<bool> captureArmed_{false};
    std::atomic<bool> captureBusy_{false};
    std::atomic<bool> captureInputActive_{false};
    std::atomic<bool> bypass_{false};
    std::atomic<std::uint32_t> captureFrames_{0};
    std::atomic<float> capturePeak_{0.f};
    int captureSlot_ = 0;
    // Loaded .sid file (non-realtime only; guarded by sidMutex_).
    mutable std::mutex sidMutex_;
    std::vector<std::uint8_t> sidFile_;
    std::uint16_t sidSubtune_ = 0;
    mutable std::mutex modelMutex_;
    GUI::SettingsPanelModel settings_;
    GUI::MixPanelModel mix_;
    GUI::KitStateBlob kit_;
    GUI::DigiPanelModel digiModel_;
    std::unique_ptr<GUI::DigiSampleBankBlob> digiBank_;
    std::uint64_t modelGeneration_ = 1;
    double sampleRate_ = 44100.0;
};

} // namespace ArpSID
