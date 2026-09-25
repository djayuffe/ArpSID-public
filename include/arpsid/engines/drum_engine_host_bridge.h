// SPDX-License-Identifier: BSD-3-Clause
// drum_engine_host_bridge.h — Host-side reference wire-up of the
// engine-split architecture for AUv2/AUv3 wrappers.
//
// PURPOSE
// ------// After audit #29/#39/#74 + the factory-kit + wavetable-runner skives,
// ArpSID has all the building blocks of a clean engine-split:
//
// * `DrSidEngine` — legacy DrSID (C64 wavetable + AnalogX0X8 overlay)
// * `Sid808Engine` — new analog x0x projection engine
// * `DrumEngineRouter` — context-aware dispatch between the two
// * `applyFactorySid808Kit(...)` — per-slot kit configuration loader
// * `DrSidWavetableProgramRunner` — register-microprogram runner
//
// The AUv2/AUv3 wrapper needs one glue object that owns all these instances
// together and exposes a single coherent API. This header provides that glue
// as `DrumEngineHostBridge`. The bridge:
//
// 1. References the runtime's canonical `DrSidEngine`, owns one
// `Sid808Engine`, and owns one `DrumEngineRouter`.
// 2. Exposes a single `noteOn/noteOff/processBlock` surface that
// dispatches via the router.
// 3. `loadFactorySlot(slot)` does the right thing per slot:
// - DrSID legacy slots (47, 112..127 minus the SID-808 range) →
// set router identity to DrSID context, leave DrSidEngine
// configured as the active engine.
// - SID-808 canonical slots (120..149) → set router identity to
// SID-808 context AND apply the kit config via
// `applyFactorySid808Kit`.
// 4. Diagnostics: forwards router diagnostics + per-engine counters.
//
// USAGE FROM AUv2/AUv3 wrapper
// ---------------------------// * Host owns one `DrumEngineHostBridge` per instance.
// * On factory-preset-change: `bridge.loadFactorySlot(slot);`
// * On note-on/off: `bridge.noteOn(SidGMDrumClass::Kick, vel);`
// * On render: `bridge.processBlock(outL, outR, numSamples);`
//
// The wrapper does NOT need to know which engine is active — the
// bridge dispatches transparently. This is the audit-correct
// "single coherent API over a split engine architecture" the
// production engine path requires.
//
// PRODUCTION ROUTER POLICY
// -----------------------// `DrumEngineHostBridge` is a NEW class. Existing AUv2/AUv3 sessions
// that explicitly disable the router can still drive `DrSidEngine` directly.
// New sessions enable the bridge by default so SID-808 factory slots use the
// authored split-engine path immediately.

#ifndef ARPSID_ENGINES_DRUM_ENGINE_HOST_BRIDGE_H
#define ARPSID_ENGINES_DRUM_ENGINE_HOST_BRIDGE_H

#include "arpsid/core/drum_context.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/sid808_engine.h"
#include "arpsid/engines/drum_engine_router.h"
#include "arpsid/patchbank/factory_sid808_kits.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cmath>

namespace ArpSID {

struct Sid808BridgeOutputTelemetry {
    std::uint64_t routedHitCount = 0u;
    int configuredKitSlot = -1;
    int lastRoutedDrumClass = 255;
    int lastRoutedMidiNote = -1;
    float lastRoutedVelocity = 0.0f;
    float outputPeak = 0.0f;
    std::uint8_t activeVoiceCount = 0u;
    std::uint64_t silentActiveBlockCount = 0u;
    std::uint64_t zeroPeakWithActiveVoiceCount = 0u;
    bool silentActiveSinceLastHit = false;
    float lastSnareSnapPeak = 0.0f;
    float lastSnareSnapRms = 0.0f;
    float lastSnareBodyPeak = 0.0f;
    float lastSnareBodyRms = 0.0f;
    std::uint64_t snareMicroStageAppliedCount = 0u;
    std::uint64_t snareMicroStageLateCount = 0u;
    bool sid808ContextActive = false;
};

class DrumEngineHostBridge {
public:
    DrumEngineHostBridge() noexcept
      : sid808_()
      , router_(sid808_) {}
    explicit DrumEngineHostBridge(DrSidEngine& canonicalDrSid) noexcept
      : sid808_()
      , router_(canonicalDrSid, sid808_) {}

    void bindCanonicalDrSidEngine(DrSidEngine& engine) noexcept {
        router_.bindDrSidEngine(engine);
    }
    bool hasCanonicalDrSidEngine() const noexcept {
        return router_.hasDrSidEngine();
    }

    // ── Lifecycle / setup (non-RT) ──────────────────────────────────────────
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        router_.prepare(sampleRate);
    }
    void setClockFrequency(double hz) noexcept {
        router_.setClockFrequency(hz);
    }
    void setSidModel(SIDModel model) noexcept {
        router_.setSidModel(model);
    }
    void setForensicConfig(const ArpSIDForensicConfig& cfg) noexcept {
        router_.setForensicConfig(cfg);
    }
    const ArpSIDForensicConfig& activeForensicConfig() const noexcept {
        return router_.activeForensicConfig();
    }

    // ── Factory-slot load — NON-REALTIME ONLY ────────────────────────────────
    // CORRECT USAGE: call prepareFactorySlotNonRealtime() from the non-RT
    // preset-change path (configuration/reset thread), then call
    // applyPreparedSlotNonRealtime() after suspending render. The render
    // thread must NEVER call loadFactorySlot() or any method that modifies
    // router/engine state; it only calls processBlock() on the already    // configured bridge.
    //
    // Returns true iff the slot is a recognized drum slot and the bridge
    // configured itself to play it. Slots outside the canonical drum
    // ranges return false (and leave the bridge's previous state intact).
    bool loadFactorySlot(int slot) noexcept {
        router_.setActiveIdentityFromFactorySlot(slot);
        const DrumContext ctx = router_.activeIdentity().context;
        switch (ctx) {
            case DrumContext::SID808_AnalogProjection: {
                const bool applied = router_.applySid808FactorySlot(slot);
                ++lastLoadDiag_.slotLoadCount;
                if (applied) ++lastLoadDiag_.sid808LoadCount;
                loadedSid808Slot_.store(applied ? slot : -1, std::memory_order_release);
                sid808ContextActive_.store(applied ? 1u : 0u, std::memory_order_release);
                return applied;
            }
            case DrumContext::DrSID_C64Wavetable: {
                // Legacy DrSidEngine path. We don't reconfigure DrSidEngine
                // here — the host's factory-preset loader does that via the
                // existing factory_patch_params.h overlay (`applyFactoryDrSidDefaults`).
                ++lastLoadDiag_.slotLoadCount;
                ++lastLoadDiag_.drsidLoadCount;
                loadedSid808Slot_.store(-1, std::memory_order_release);
                sid808ContextActive_.store(0u, std::memory_order_release);
                return true;
            }
            case DrumContext::Digi4Bit:
            case DrumContext::None:
            default:
                ++lastLoadDiag_.slotLoadCount;
                ++lastLoadDiag_.unroutedLoadCount;
                loadedSid808Slot_.store(-1, std::memory_order_release);
                sid808ContextActive_.store(0u, std::memory_order_release);
                return false;
        }
    }

    // ── Off-thread prebuild / atomic-swap pattern ────────────────────────────
    // Usage model to avoid ANY loadFactorySlot() call on the render thread:
    //
    // Non-RT thread:
    // bridge.queueSlotLoadNonRealtime(slot); // store pending slot
    //
    // Configuration thread (after render is suspended):
    // bridge.applyQueuedSlotNonRealtime(); // configures engines
    //
    // Render thread (processBlock): NEVER calls loadFactorySlot. The render
    // thread reads the already-configured state atomically.
    //
    // Queue a slot for deferred off-thread loading. The pending slot is
    // consumed by applyQueuedSlotNonRealtime(). A value of -1 means "no
    // pending load."
    void queueSlotLoadNonRealtime(int slot) noexcept {
        pendingSlot_.store(slot, std::memory_order_release);
    }

    // Apply the pending slot if one has been queued. Must be called
    // off-thread while the render thread is suspended/drained. Returns
    // true if a slot was pending and was applied.
    bool applyQueuedSlotNonRealtime() noexcept {
        const int slot = pendingSlot_.exchange(-1, std::memory_order_acq_rel);
        if (slot < 0) return false;
        loadFactorySlot(slot);
        return true;
    }

    // True if a slot load is queued and not yet applied.
    bool hasPendingSlotLoad() const noexcept {
        return pendingSlot_.load(std::memory_order_acquire) >= 0;
    }

    // Apply a kit table that was resolved off-thread and handed to the render
    // thread through an ownership mailbox. This is RT-safe: no allocation, no
    // factory lookup, and no non-RT loader call.
    bool activatePreparedSid808SlotRealtime(int slot,
                                            const Sid808KitConfigTable& kit) noexcept {
        if (!isSid808FactorySlot(slot)) return false;
        router_.setActiveIdentityFromFactorySlot(slot);
        if (router_.activeIdentity().context != DrumContext::SID808_AnalogProjection) return false;
        for (std::size_t i = 0; i < kit.size(); ++i) {
            sid808_.setDrumVoiceConfig(static_cast<Sid808Drum>(i), kit[i]);
        }
        loadedSid808Slot_.store(slot, std::memory_order_release);
        sid808ContextActive_.store(1u, std::memory_order_release);
        pendingSlot_.store(-1, std::memory_order_release);
        return true;
    }

    // ── Note dispatch (host / render-thread sequencer) ─────────────────────
    // Dispatches via `DrumEngineRouter`. The host knows nothing about which
    // engine is active.
    void noteOn(SidGMDrumClass drumClass, std::uint8_t velocity, std::uint8_t midiNoteHint = 0) noexcept {
        router_.noteOn(drumClass, velocity, midiNoteHint);
        publishSid808RoutedHitIfActive_(drumClass, velocity, midiNoteHint);
    }
    void noteOnWithOverride(SidGMDrumClass drumClass,
                            std::uint8_t velocity,
                            std::uint8_t midiNoteHint,
                            const Sid808HitOverride& ov) noexcept {
        router_.noteOnWithOverride(drumClass, velocity, midiNoteHint, &ov);
        publishSid808RoutedHitIfActive_(drumClass, velocity, midiNoteHint);
    }

    // Render-thread scheduled ingress. Events with offset 0 are fired
    // immediately; future offsets are consumed by processBlock() which chunks
    // rendering around the event boundary. Fixed-capacity and insertion-sort
    // keep this deterministic on the render thread; this is not a cross-thread
    // GUI/host producer queue.
    void noteOnAt(int sampleOffset,
                  SidGMDrumClass drumClass,
                  std::uint8_t velocity,
                  std::uint8_t midiNoteHint = 0) noexcept {
        Sid808HitOverride empty{};
        noteOnAtWithOverride(sampleOffset, drumClass, velocity, midiNoteHint, empty, false);
    }
    void noteOnAtWithOverride(int sampleOffset,
                              SidGMDrumClass drumClass,
                              std::uint8_t velocity,
                              std::uint8_t midiNoteHint,
                              const Sid808HitOverride& ov,
                              bool hasOverride = true) noexcept {
        if (sampleOffset <= 0) {
            if (hasOverride) noteOnWithOverride(drumClass, velocity, midiNoteHint, ov);
            else noteOn(drumClass, velocity, midiNoteHint);
            return;
        }
        if (scheduledNoteCount_ >= kMaxScheduledNotes) {
            ++scheduledNoteOverflowCount_;
            return;
        }
        ScheduledNote ev{};
        ev.sampleOffset = sampleOffset;
        ev.drumClass = drumClass;
        ev.velocity = velocity;
        ev.midiNoteHint = midiNoteHint;
        ev.override = ov;
        ev.hasOverride = hasOverride;
        std::size_t pos = scheduledNoteCount_++;
        while (pos > 0 && scheduledNotes_[pos - 1u].sampleOffset > ev.sampleOffset) {
            scheduledNotes_[pos] = scheduledNotes_[pos - 1u];
            --pos;
        }
        scheduledNotes_[pos] = ev;
    }

    void noteOff(SidGMDrumClass drumClass) noexcept {
        router_.noteOff(drumClass);
    }
    void allNotesOff() noexcept {
        scheduledNoteCount_ = 0u;
        router_.allNotesOff();
    }

    // ── Render (delegated to the router) ───────────────────────────────────
    void processBlock(float* outL, float* outR, int numSamples) noexcept {
        if (!outL || numSamples <= 0) {
            scheduledNoteCount_ = 0u;
            publishSid808OutputPeak_(nullptr, nullptr, 0);
            return;
        }
        sid808ContextActive_.store(
            router_.activeIdentity().context == DrumContext::SID808_AnalogProjection ? 1u : 0u,
            std::memory_order_release);
        if (scheduledNoteCount_ == 0u) {
            router_.processBlock(outL, outR, numSamples);
            publishSid808OutputPeak_(outL, outR, numSamples);
            return;
        }

        int rendered = 0;
        std::size_t idx = 0u;
        while (idx < scheduledNoteCount_) {
            const int off = std::clamp(scheduledNotes_[idx].sampleOffset, 0, numSamples);
            if (off > rendered) {
                router_.processBlock(outL + rendered,
                                     outR ? (outR + rendered) : nullptr,
                                     off - rendered);
                rendered = off;
            }
            while (idx < scheduledNoteCount_ &&
                   std::clamp(scheduledNotes_[idx].sampleOffset, 0, numSamples) == rendered) {
                if (scheduledNotes_[idx].hasOverride)
                    noteOnWithOverride(scheduledNotes_[idx].drumClass,
                                       scheduledNotes_[idx].velocity,
                                       scheduledNotes_[idx].midiNoteHint,
                                       scheduledNotes_[idx].override);
                else
                    noteOn(scheduledNotes_[idx].drumClass,
                           scheduledNotes_[idx].velocity,
                           scheduledNotes_[idx].midiNoteHint);
                ++idx;
            }
        }
        if (rendered < numSamples) {
            router_.processBlock(outL + rendered,
                                 outR ? (outR + rendered) : nullptr,
                                 numSamples - rendered);
        }
        scheduledNoteCount_ = 0u;
        publishSid808OutputPeak_(outL, outR, numSamples);
    }

    // ── Diagnostics / introspection (test + host-tooling) ───────────────────
    struct LoadDiagnostics {
        std::uint64_t slotLoadCount     = 0;
        std::uint64_t drsidLoadCount    = 0;
        std::uint64_t sid808LoadCount   = 0;
        std::uint64_t unroutedLoadCount = 0;
    };
    LoadDiagnostics loadDiagnostics() const noexcept { return lastLoadDiag_; }
    void resetLoadDiagnostics() noexcept { lastLoadDiag_ = LoadDiagnostics{}; }
    std::uint64_t scheduledNoteOverflowCount() const noexcept { return scheduledNoteOverflowCount_; }
    void resetScheduledNoteDiagnostics() noexcept { scheduledNoteOverflowCount_ = 0u; }

    const DrumKitIdentity&             activeIdentity()  const noexcept { return router_.activeIdentity(); }
    bool hasActiveRenderableContext() const noexcept {
        const DrumContext ctx = router_.activeIdentity().context;
        return ctx == DrumContext::DrSID_C64Wavetable ||
               ctx == DrumContext::SID808_AnalogProjection;
    }
    void activateDefaultIdentityForContext(DrumContext ctx) noexcept {
        // RT-safe identity repair only. This deliberately does NOT call
        // loadFactorySlot() or applyFactorySid808Kit(); actual kit loading stays
        // in setup/state/preset non-RT paths. It prevents a context==None router
        // from swallowing valid GM drum notes after MIDI auto-promotion.
        if (ctx == DrumContext::SID808_AnalogProjection)
            router_.setActiveIdentityFromFactorySlot(120);
        else if (ctx == DrumContext::DrSID_C64Wavetable)
            router_.setActiveIdentityFromFactorySlot(47);
    }
    const DrumEngineRouterDiagnostics& routerDiagnostics() const noexcept { return router_.diagnostics(); }
    Sid808BridgeOutputTelemetry sid808OutputTelemetry() const noexcept {
        Sid808BridgeOutputTelemetry t{};
        t.routedHitCount = sid808RoutedHitCount_.load(std::memory_order_acquire);
        t.configuredKitSlot = loadedSid808Slot_.load(std::memory_order_acquire);
        t.lastRoutedDrumClass = sid808LastRoutedDrumClass_.load(std::memory_order_acquire);
        t.lastRoutedMidiNote = sid808LastRoutedMidiNote_.load(std::memory_order_acquire);
        t.lastRoutedVelocity = sanitizeUnit_(sid808LastRoutedVelocity_.load(std::memory_order_acquire));
        t.outputPeak = sanitizeUnit_(sid808OutputPeak_.load(std::memory_order_acquire));
        t.activeVoiceCount = sid808ActiveVoiceCount_.load(std::memory_order_acquire);
        t.silentActiveBlockCount = sid808SilentActiveBlockCount_.load(std::memory_order_acquire);
        t.zeroPeakWithActiveVoiceCount = sid808ZeroPeakWithActiveVoiceCount_.load(std::memory_order_acquire);
        t.silentActiveSinceLastHit = sid808SilentActiveSinceLastHit_.load(std::memory_order_acquire) != 0u;
        t.lastSnareSnapPeak = sanitizeUnit_(sid808LastSnareSnapPeak_.load(std::memory_order_acquire));
        t.lastSnareSnapRms = sanitizeUnit_(sid808LastSnareSnapRms_.load(std::memory_order_acquire));
        t.lastSnareBodyPeak = sanitizeUnit_(sid808LastSnareBodyPeak_.load(std::memory_order_acquire));
        t.lastSnareBodyRms = sanitizeUnit_(sid808LastSnareBodyRms_.load(std::memory_order_acquire));
        t.snareMicroStageAppliedCount = sid808SnareMicroStageAppliedCount_.load(std::memory_order_acquire);
        t.snareMicroStageLateCount = sid808SnareMicroStageLateCount_.load(std::memory_order_acquire);
        t.sid808ContextActive = sid808ContextActive_.load(std::memory_order_acquire) != 0u;
        return t;
    }
    DrSidEngine*        canonicalDrSidEngine()       noexcept { return router_.drsidEngine(); }
    const DrSidEngine*  canonicalDrSidEngine() const noexcept { return router_.drsidEngine(); }
    Sid808Engine&       sid808Engine()      noexcept { return sid808_; }
    const Sid808Engine& sid808Engine() const noexcept { return sid808_; }
    DrumEngineRouter&   router()            noexcept { return router_; }
    double              sampleRate()  const noexcept { return sampleRate_; }
    int                 loadedSid808Slot() const noexcept {
        return loadedSid808Slot_.load(std::memory_order_acquire);
    }

private:
    static float sanitizeUnit_(float value) noexcept {
        if (!std::isfinite(value)) return 0.0f;
        return std::clamp(value, 0.0f, 1.0f);
    }

    void publishSid808RoutedHitIfActive_(SidGMDrumClass drumClass,
                                         std::uint8_t velocity,
                                         std::uint8_t midiNoteHint) noexcept {
        if (velocity == 0u) return;
        if (router_.activeIdentity().context != DrumContext::SID808_AnalogProjection) return;
        const Sid808Drum drum = sid808DrumFromGmClass(drumClass);
        if (drum == Sid808Drum::Count) return;
        const int midiNote = (midiNoteHint > 0u && midiNoteHint <= 127u)
            ? static_cast<int>(midiNoteHint)
            : static_cast<int>(sid808CanonicalMidiNoteForDrum(drum));
        sid808LastRoutedDrumClass_.store(static_cast<int>(drum), std::memory_order_release);
        sid808LastRoutedMidiNote_.store(midiNote, std::memory_order_release);
        sid808LastRoutedVelocity_.store(sanitizeUnit_(static_cast<float>(velocity) * (1.0f / 127.0f)),
                                        std::memory_order_release);
        sid808ContextActive_.store(1u, std::memory_order_release);
        sid808SilentActiveSinceLastHit_.store(0u, std::memory_order_release);
        sid808RoutedHitCount_.fetch_add(1u, std::memory_order_acq_rel);
    }

    void publishSid808OutputPeak_(const float* outL, const float* outR, int numSamples) noexcept {
        float peak = 0.0f;
        const bool sid808ActiveContext =
            router_.activeIdentity().context == DrumContext::SID808_AnalogProjection;
        if (sid808ActiveContext && outL && numSamples > 0) {
            for (int i = 0; i < numSamples; ++i) {
                peak = std::max(peak, std::fabs(outL[i]));
                if (outR) peak = std::max(peak, std::fabs(outR[i]));
            }
        }
        const std::uint8_t activeVoices = sid808ActiveContext ? sid808_.activeVoiceCount() : 0u;
        sid808ActiveVoiceCount_.store(activeVoices, std::memory_order_release);
        sid808LastSnareSnapPeak_.store(sanitizeUnit_(sid808_.lastSnareSnapPeak()), std::memory_order_release);
        sid808LastSnareSnapRms_.store(sanitizeUnit_(sid808_.lastSnareSnapRms()), std::memory_order_release);
        sid808LastSnareBodyPeak_.store(sanitizeUnit_(sid808_.lastSnareBodyPeak()), std::memory_order_release);
        sid808LastSnareBodyRms_.store(sanitizeUnit_(sid808_.lastSnareBodyRms()), std::memory_order_release);
        sid808SnareMicroStageAppliedCount_.store(sid808_.snareMicroStageAppliedCount(), std::memory_order_release);
        sid808SnareMicroStageLateCount_.store(sid808_.snareMicroStageLateCount(), std::memory_order_release);
        constexpr float kZeroPeakEpsilon = 1.0e-7f;
        constexpr float kSilentActivePeakFloor = 0.005f;
        if (sid808ActiveContext && activeVoices > 0u && peak <= kSilentActivePeakFloor) {
            sid808SilentActiveBlockCount_.fetch_add(1u, std::memory_order_acq_rel);
            if (peak <= kZeroPeakEpsilon)
                sid808ZeroPeakWithActiveVoiceCount_.fetch_add(1u, std::memory_order_acq_rel);
            sid808SilentActiveSinceLastHit_.store(1u, std::memory_order_release);
        } else if (peak > kSilentActivePeakFloor || activeVoices == 0u) {
            sid808SilentActiveSinceLastHit_.store(0u, std::memory_order_release);
        }
        sid808OutputPeak_.store(sanitizeUnit_(peak), std::memory_order_release);
    }

    Sid808Engine       sid808_;
    DrumEngineRouter   router_;
    LoadDiagnostics    lastLoadDiag_{};
    double             sampleRate_ = 44100.0;
    // Atomic pending slot for the off-thread prebuild / atomic-swap pattern.
    // -1 means no pending load. Written by queueSlotLoadNonRealtime() on
    // the configuration thread; read and cleared by applyQueuedSlotNonRealtime().
    // The render thread never reads or writes this field.
    std::atomic<int>   pendingSlot_{-1};
    std::atomic<int>   loadedSid808Slot_{-1};
    std::atomic<std::uint64_t> sid808RoutedHitCount_{0u};
    std::atomic<int>   sid808LastRoutedDrumClass_{255};
    std::atomic<int>   sid808LastRoutedMidiNote_{-1};
    std::atomic<float> sid808LastRoutedVelocity_{0.0f};
    std::atomic<float> sid808OutputPeak_{0.0f};
    std::atomic<std::uint8_t> sid808ActiveVoiceCount_{0u};
    std::atomic<std::uint64_t> sid808SilentActiveBlockCount_{0u};
    std::atomic<std::uint64_t> sid808ZeroPeakWithActiveVoiceCount_{0u};
    std::atomic<std::uint8_t> sid808SilentActiveSinceLastHit_{0u};
    std::atomic<float> sid808LastSnareSnapPeak_{0.0f};
    std::atomic<float> sid808LastSnareSnapRms_{0.0f};
    std::atomic<float> sid808LastSnareBodyPeak_{0.0f};
    std::atomic<float> sid808LastSnareBodyRms_{0.0f};
    std::atomic<std::uint64_t> sid808SnareMicroStageAppliedCount_{0u};
    std::atomic<std::uint64_t> sid808SnareMicroStageLateCount_{0u};
    std::atomic<uint8_t> sid808ContextActive_{0u};

    struct ScheduledNote {
        int sampleOffset = 0;
        SidGMDrumClass drumClass = SidGMDrumClass::Unsupported;
        std::uint8_t velocity = 0u;
        std::uint8_t midiNoteHint = 0u;
        Sid808HitOverride override{};
        bool hasOverride = false;
    };
    static constexpr std::size_t kMaxScheduledNotes = 96u;
    std::array<ScheduledNote, kMaxScheduledNotes> scheduledNotes_{};
    std::size_t scheduledNoteCount_ = 0u;
    std::uint64_t scheduledNoteOverflowCount_ = 0u;
};

} // namespace ArpSID

#endif // ARPSID_ENGINES_DRUM_ENGINE_HOST_BRIDGE_H
