// SPDX-License-Identifier: BSD-3-Clause
// digi_d418_stream_engine.h - 4-bit DIGI as PHI2-scheduled $D418 writes.
//
// This engine replaces the legacy DigiSamplerEngine for the standalone/private
// D418 layer. It emits PHI2-scheduled writes to $D418 instead of mixing audio
// directly, then the AU kernel renders those writes through an isolated SID
// register engine so DIGI overlays cannot corrupt the main PSID/RSID runtime.
// The low nibble carries the 4-bit sample value. The high nibble is taken from
// an explicit owner-supplied source when available, otherwise from the local
// SID bridge shadow. When IO is not visible at $D000, writes are blocked and
// counted in telemetry. An optional open-bus latch is driven with each write to
// simulate the last driven bus value.

#pragma once

#include "arpsid/core/c64_bus_event.h"
#include "arpsid/core/c64_open_bus.h"
#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/gui/gui_realtime_projection_v588.h"
#include "arpsid/gui/digi_sample_bank_v596.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ArpSID {

// DIGI render policies. The first two modes are intentionally named as
// AUTH/FAST private D418 authorities: they generate 4-bit $D418
// writes and render those writes through an isolated SID register engine so a
// drum/sample layer cannot corrupt the main PSID/RSID SID runtime. Mode 0 is
// C64-bus-authentic and honors IO/open-bus policy; mode 1 is a fast private
// non-bus preview. The legacy aliases keep existing state blobs and tests
// source-compatible.
enum class DigiAuthMode : std::uint8_t {
    StandaloneD418Layer = 0,
    FastStandaloneD418Layer = 1,
    LegacyFloatLayer = 2,
    C64SidD418 = StandaloneD418Layer,
    FastSidD418 = FastStandaloneD418Layer,
};

// Clock sources for the DIGI streamer. Only Phi2FixedRate is implemented.
// CIA/VBlank values remain reserved compatibility inputs and are sanitized to
// Phi2FixedRate without changing active voices, so an old/corrupt state blob
// cannot imply an unsupported timing authority or chop playback.
enum class DigiClockSource : std::uint8_t {
    Phi2FixedRate = 0,
    CiaTimerA     = 1,
    VBlank        = 2,
};

// User‑configurable parameters for the DIGI stream engine. Rate and
// high‑nibble preservation can be changed at runtime.
struct DigiD418Config final {
    DigiAuthMode     authMode = DigiAuthMode::StandaloneD418Layer;
    DigiClockSource  clockSource = DigiClockSource::Phi2FixedRate;
    std::uint32_t    digiRateHz = 8000u;
    bool respectIoBank    = true;
    bool driveOpenBus     = true;
    bool preserveD418HighNibble = true;
    bool countCollisions  = true;
    // Optional source-of-truth high nibble supplied by the owning SID authority
    // when the bridge shadow is temporary/private. This fixes standalone D418
    // rendering preserving a stale high nibble from a scratch bridge.
    bool useExternalD418HighNibble = false;
    std::uint8_t externalD418HighNibble = 0x00u;
};

// Forensic event logged per write. Used for debugging and timeline views.
struct DigiD418ForensicEvent final {
    std::uint64_t phi2Cycle = 0;
    std::uint32_t hostFrame = 0;
    std::uint8_t  oldD418   = 0;
    std::uint8_t  newD418   = 0;
    std::uint8_t  nibble    = 0;
    std::uint8_t  openBusBefore = 0xFFu;
    std::uint8_t  openBusAfter  = 0xFFu;
    bool ioVisible  = true;
    bool sidAccepted = false;
    // Fully materialized C64 bus record for the same event. This avoids a
    // GUI/forensic split-brain where the HUD reports a D418 write but the
    // low-level debugger cannot reconstruct the bus cycle that caused it.
    ArpSID::C64::C64BusEvent busEvent{};
};

// Telemetry emitted once per audio block. These values are used by the
// render thread to update the GUI and inspector panels.
struct DigiD418Telemetry final {
    std::uint8_t  activeSlotCount = 0;
    std::uint8_t  configuredFactorySlotCount = 0;
    std::uint8_t  configuredUserImportSlotCount = 0;
    std::uint8_t  playingVoiceCount = 0;
    std::uint8_t  peakVoiceCount = 0;
    std::uint8_t  stepIndex = 0;
    std::uint8_t  lastTriggeredSlot = 255u;
    std::uint8_t  lastNibble = 0u;
    std::uint8_t  lastOldD418 = 0u;
    std::uint8_t  lastD418 = 0u;
    std::uint8_t  lastOpenBus = 0xFFu;
    std::uint16_t lastTriggeredFactorySlot = 0u;
    std::uint32_t triggerCount = 0u;
    std::uint32_t midiTriggerCount = 0u;
    std::uint32_t midiIgnoredCount = 0u;
    std::uint8_t  lastMidiNote = 255u;
    std::uint8_t  lastMidiChannel = 255u;
    std::uint32_t d418WriteCount = 0u;
    std::uint32_t d418WritesThisBlock = 0u;
    std::uint32_t d418SidAcceptedWriteCount = 0u;
    std::uint32_t d418SidAcceptedWritesThisBlock = 0u;
    std::uint32_t d418WritesBlockedByIoBank = 0u;
    std::uint32_t d418WriteQueueOverflow = 0u;
    std::uint32_t unavailableUserImportCount = 0u;
    std::uint32_t voiceStealCount = 0u;
    std::uint32_t d418CollisionCount = 0u;
    std::uint32_t openBusDriveCount = 0u;
    // Render timeline discontinuities where the PHI2 block start did not
    // continue exactly from the previous process end. The stream engine
    // resets its pending write scheduler at those boundaries so stale future
    // D418 events cannot be pulled across transport jumps, offline-bounce
    // seeks or host quantum discontinuities.
    std::uint32_t timelineDiscontinuityResetCount = 0u;
    float scopePeak = 0.0f;
};

// Authentic DIGI stream engine. Combines sequencer, nibble renderer and
// bus‑write emitter. Use prepare() before render, reset() to clear state.
class DigiD418StreamEngine final {
public:
    static constexpr int kMaxVoices = ArpSID::GUI::kDigiActiveSlotCount;
    static constexpr int kScopeLen = 128;
    static constexpr int kForensicLen = 64;

    void prepare(double hostSampleRate, double c64Phi2Hz) noexcept {
        hostSampleRate_ = sanitizeRate_(hostSampleRate, 44100.0, 8000.0, 384000.0);
        c64Phi2Hz_ = sanitizeRate_(c64Phi2Hz, static_cast<double>(ArpSID::C64::kPalPhi2Hz), 800000.0, 1200000.0);
        reset();
    }

    // Structural PAL/NTSC changes are block/sample-boundary events. Update the
    // PHI2 conversion without killing active sample voices; only the timestamp
    // scheduler anchor is restarted so no write can move backwards in time.
    void updateTimingPreserveVoices(double hostSampleRate, double c64Phi2Hz) noexcept {
        const double nextHost = sanitizeRate_(hostSampleRate, 44100.0, 8000.0, 384000.0);
        const double nextPhi2 = sanitizeRate_(c64Phi2Hz, static_cast<double>(ArpSID::C64::kPalPhi2Hz), 800000.0, 1200000.0);
        if (nextHost == hostSampleRate_ && nextPhi2 == c64Phi2Hz_) return;
        hostSampleRate_ = nextHost;
        c64Phi2Hz_ = nextPhi2;
        resetDigiWriteScheduler_();
        resetEmitCollisionAnchor_();
        lastEmitPhi2_ = ~std::uint64_t{0};
    }

    void reset() noexcept {
        for (auto& v : voices_) v = Voice{};
        telemetry_ = DigiD418Telemetry{};
        telemetry_.lastTriggeredSlot = 255u;
        telemetry_.lastOpenBus = 0xFFu;
        scope_.fill(0.0f);
        forensic_.fill(DigiD418ForensicEvent{});
        scopeWritePos_ = 0u;
        forensicWritePos_ = 0u;
        haveStep_ = false;
        lastStep_ = 255u;
        ageCounter_ = 0u;
        digiPhase_ = 0.0;
        resetDigiWriteScheduler_();
        lastEmitPhi2_ = ~std::uint64_t{0};
        lastEmitValue_ = 0u;
    }

    void setConfig(const DigiD418Config& c) noexcept {
        const DigiD418Config next = sanitizeConfig_(c);
        const bool behaviorChanged =
            next.authMode != config_.authMode ||
            next.clockSource != config_.clockSource ||
            next.digiRateHz != config_.digiRateHz ||
            next.respectIoBank != config_.respectIoBank ||
            next.driveOpenBus != config_.driveOpenBus ||
            next.preserveD418HighNibble != config_.preserveD418HighNibble ||
            next.countCollisions != config_.countCollisions ||
            next.useExternalD418HighNibble != config_.useExternalD418HighNibble;
        config_ = next;
        if (behaviorChanged) {
            // Any behavioral policy change alters the authority contract for
            // already-armed DIGI voices: bus visibility, open-bus side effects,
            // high-nibble-source selection and collision accounting are part of
            // the same C64/SID write semantics. The high-nibble *value* itself
            // is intentionally not a behavior-change trigger: in standalone
            // mode the owner may refresh it every render block from the main
            // SID authority, and killing voices on normal filter/mode nibble
            // changes would chop active DIGI samples.
            for (auto& v : voices_) v.active = false;
            digiPhase_ = 0.0;
            resetDigiWriteScheduler_();
            resetEmitCollisionAnchor_();
            telemetry_.playingVoiceCount = 0u;
            haveStep_ = false;
        }
    }
    const DigiD418Config& config() const noexcept { return config_; }
    const DigiD418Telemetry& telemetry() const noexcept { return telemetry_; }
    std::uint32_t scopeWritePos() const noexcept {
        return scopeWritePos_ & static_cast<std::uint32_t>(kScopeLen - 1);
    }
    std::uint32_t forensicWritePos() const noexcept {
        return forensicWritePos_ & static_cast<std::uint32_t>(kForensicLen - 1);
    }
    const DigiD418ForensicEvent& forensicEvent(std::uint32_t index) const noexcept {
        return forensic_[index & static_cast<std::uint32_t>(kForensicLen - 1)];
    }

    bool isActive() const noexcept {
        for (const auto& v : voices_) if (v.active) return true;
        return false;
    }

    void allNotesOff() noexcept {
        for (auto& v : voices_) v.active = false;
        telemetry_.playingVoiceCount = 0u;
        digiPhase_ = 0.0;
        resetDigiWriteScheduler_();
        resetEmitCollisionAnchor_();
    }

    static constexpr std::uint8_t kDefaultMidiRootNote = 60u; // C4 -> DIGI slot 0, C#4 -> slot 1 ... G4 -> slot 7
    static constexpr std::uint8_t kMaxMidiRootNote =
        static_cast<std::uint8_t>(127u - (ArpSID::GUI::kDigiActiveSlotCount - 1u));

    static std::uint8_t sanitizeMidiRootNote(std::uint8_t rootNote) noexcept {
        // The public engine API must enforce the same root-note contract as
        // the AU GUI, adapter and MIDI CC113 path: all eight chromatic DIGI
        // pads must fit inside the 0..127 MIDI note domain. Without this, a
        // direct engine caller could pass root 127 and expose only one pad
        // while the HUD/UX claims an eight-pad mapping.
        return static_cast<std::uint8_t>(
            std::min<unsigned>(static_cast<unsigned>(rootNote & 0x7Fu),
                               static_cast<unsigned>(kMaxMidiRootNote)));
    }

    static bool midiNoteMapsToSlot(std::uint8_t note,
                                   std::uint8_t rootNote,
                                   std::uint8_t& slotOut) noexcept {
        const std::uint8_t safeRoot = sanitizeMidiRootNote(rootNote);
        const int rel = static_cast<int>(note & 0x7Fu) - static_cast<int>(safeRoot);
        if (rel < 0 || rel >= ArpSID::GUI::kDigiActiveSlotCount) return false;
        slotOut = static_cast<std::uint8_t>(rel);
        return true;
    }

    // MIDI pad trigger: maps a chromatic octave of MIDI notes onto the eight
    // DIGI slots. This is intentionally a one-shot trigger; NoteOff does not
    // kill a sample unless the caller uses allNotesOff()/panic.
    bool triggerMidiNoteAt(std::uint8_t channel,
                           std::uint8_t note,
                           std::uint8_t velocity,
                           const ArpSID::GUI::DigiSampleBankBlob& sampleBank,
                           const ArpSID::GUI::GuiRealtimeDigiProjection& projection,
                           int sampleOffset = 0,
                           std::uint8_t rootNote = kDefaultMidiRootNote,
                           int channelFilter = -1) noexcept {
        if (config_.authMode == DigiAuthMode::LegacyFloatLayer) { ++telemetry_.midiIgnoredCount; return false; }
        if (velocity == 0u) { ++telemetry_.midiIgnoredCount; return false; }
        const int safeCh = static_cast<int>(channel & 0x0Fu);
        if (channelFilter >= 0 && channelFilter <= 15 && safeCh != channelFilter) { ++telemetry_.midiIgnoredCount; return false; }
        std::uint8_t slot = 0u;
        if (!midiNoteMapsToSlot(note, rootNote, slot)) { ++telemetry_.midiIgnoredCount; return false; }
        const std::uint32_t before = telemetry_.triggerCount;
        triggerSlotAt(slot, velocity, sampleBank, projection, sampleOffset);
        if (telemetry_.triggerCount == before) { ++telemetry_.midiIgnoredCount; return false; }
        ++telemetry_.midiTriggerCount;
        telemetry_.lastMidiNote = static_cast<std::uint8_t>(note & 0x7Fu);
        telemetry_.lastMidiChannel = static_cast<std::uint8_t>(channel & 0x0Fu);
        return true;
    }

    // Immediately trigger a specific slot. Equivalent to a programmatic
    // one‑shot or MIDI note. Velocity zero is ignored.
    void triggerSlotAt(std::uint8_t slotIndex,
                       std::uint8_t velocity,
                       const ArpSID::GUI::DigiSampleBankBlob& sampleBank,
                       const ArpSID::GUI::GuiRealtimeDigiProjection& projection,
                       int sampleOffset = 0) noexcept {
        if (config_.authMode == DigiAuthMode::LegacyFloatLayer) return;
        if (velocity == 0u || slotIndex >= ArpSID::GUI::kDigiActiveSlotCount) return;
        const auto& slot = projection.slots[slotIndex];
        if (slot.volume == 0u) return;
        ArpSID::GUI::GuiRealtimeDigiProjection one{};
        one.stepIndex = projection.stepIndex;
        one.activeSlot = slotIndex;
        one.activeSlotCount = static_cast<std::uint8_t>(std::min<int>(slotIndex + 1, ArpSID::GUI::kDigiActiveSlotCount));
        one.slots[slotIndex] = slot;
        one.slots[slotIndex].activeAtStep = true;
        one.slots[slotIndex].stepVelocity = velocity;
        pendingTriggerOffset_ = std::max(0, sampleOffset);
        triggerStep_(one, sampleBank);
        pendingTriggerOffset_ = 0;
    }

    // Render a block into the SID bridge. Does not touch the output buffers.
    void processToSidBridge(const ArpSID::GUI::GuiRealtimeDigiProjection& projection,
                            const ArpSID::GUI::DigiSampleBankBlob& sampleBank,
                            ArpSID::C64::C64SidBridgeState& sidBridge,
                            ArpSID::C64::OpenBusLatch& openBus,
                            std::uint64_t blockStartPhi2,
                            int frames,
                            bool sequencerEnabled,
                            bool allowTriggers,
                            int triggerSampleOffset = 0,
                            std::uint8_t port01 = 0x37u,
                            std::uint64_t exactBlockEndPhi2 = 0u) noexcept {
        telemetry_.d418WritesThisBlock = 0u;
        telemetry_.d418SidAcceptedWritesThisBlock = 0u;
        if (frames <= 0) return;
        const double frameToPhi2   = c64Phi2Hz_ / hostSampleRate_;
        const double writesPerFrame = static_cast<double>(config_.digiRateHz) / hostSampleRate_;
        const std::uint64_t localBlockEndPhi2 = blockStartPhi2 +
            static_cast<std::uint64_t>(std::floor(static_cast<double>(frames) * frameToPhi2));
        // The AU kernel owns the absolute DIGI PHI2 timeline and keeps the
        // fractional PAL-PHI2 remainder across host render quanta. If this
        // engine recomputes the block end with a plain floor, it can disagree
        // with the next blockStartPhi2 by one PHI2 cycle whenever the kernel
        // remainder crosses an integer boundary. That is not a transport
        // discontinuity; it is normal fractional-clock continuity. Accept an
        // owner-supplied exact end so timeline reconciliation, deferred writes
        // and forensic block boundaries all share the same clock authority.
        const std::uint64_t blockEndPhi2 = (exactBlockEndPhi2 > blockStartPhi2)
            ? exactBlockEndPhi2
            : localBlockEndPhi2;
        telemetry_.stepIndex = projection.stepIndex;
        telemetry_.activeSlotCount = projection.activeSlotCount;
        telemetry_.configuredFactorySlotCount = 0u;
        telemetry_.configuredUserImportSlotCount = 0u;
        for (const auto& slot : projection.slots) {
            if (slot.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::FactorySlot))
                ++telemetry_.configuredFactorySlotCount;
            else if (slot.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::UserImport))
                ++telemetry_.configuredUserImportSlotCount;
        }
        // LegacyFloatLayer is intentionally not a bus/SID authority mode.
        // The AU kernel routes that mode to DigiSamplerEngine::process(), so
        // this stream engine must remain side-effect free if a caller
        // accidentally invokes it while configured for legacy direct-float
        // layering. This prevents stray $D418 writes, open-bus updates or
        // forensic events from a non-authentic mode.
        if (config_.authMode == DigiAuthMode::LegacyFloatLayer) {
            // Leaving authentic/fast authority must not leave armed D418
            // voices parked inside the stream engine. Otherwise a later mode
            // switch back to AUTH C64-bus D418 could resume stale samples and emit
            // bus writes that were never re-triggered by the sequencer.
            for (auto& v : voices_) v.active = false;
            digiPhase_ = 0.0;
            resetDigiWriteScheduler_();
            telemetry_.playingVoiceCount = 0u;
            telemetry_.scopePeak *= 0.86f;
            haveStep_ = false;
            resetEmitCollisionAnchor_();
            markProcessEnd_(blockEndPhi2);
            return;
        }
        reconcileTimelineBoundary_(blockStartPhi2);

        prepareTriggers_(projection, sampleBank, frames, sequencerEnabled, allowTriggers, triggerSampleOffset);
        if (!hasActiveVoice_()) {
            digiPhase_ = 0.0;
            resetDigiWriteScheduler_();
            telemetry_.playingVoiceCount = 0u;
            telemetry_.scopePeak *= 0.86f;
            resetEmitCollisionAnchor_();
            markProcessEnd_(blockEndPhi2);
            return;
        }
        const bool standaloneBusMode = (config_.authMode == DigiAuthMode::StandaloneD418Layer);
        const bool ioVisible = !standaloneBusMode || !config_.respectIoBank ||
                               ArpSID::C64::c64D000IoVisibleFromPort01(port01);
        float blockPeak = 0.0f;
        for (int frame = 0; frame < frames; ++frame) {
            if (!hasActiveVoice_()) {
                digiPhase_ = 0.0;
                resetDigiWriteScheduler_();
                resetEmitCollisionAnchor_();
                break;
            }
            // Host-triggered starts may be sample-accurate inside the block.
            // Do not let the DIGI clock phase run before the first ready voice;
            // otherwise a trigger at sample N could immediately drain queued
            // phase and emit stale neutral $D418 writes before the intended
            // start point. This mirrors a real player routine: no bus write is
            // performed until the sample streamer is actually armed.
            if (!hasRenderableVoiceAtFrame_(frame)) {
                continue;
            }
            const std::uint64_t phi2 = blockStartPhi2 + static_cast<std::uint64_t>(std::floor(static_cast<double>(frame) * frameToPhi2));
            // If a physical $D418 write was deferred from the previous render
            // quantum, digiPhase_ is already >= 1.0. Do not add the current
            // host frame's interval before draining that pending C64 bus cycle;
            // otherwise a write exactly at the new block boundary gains one
            // extra frame of phase and the stream slowly runs fast across
            // quantum boundaries.
            if (digiPhase_ < 1.0) {
                digiPhase_ += writesPerFrame;
            }
            while (digiPhase_ >= 1.0) {
                if (!hasRenderableVoiceAtFrame_(frame)) {
                    digiPhase_ = 0.0;
                    break;
                }
                std::uint64_t writePhi2 = 0u;
                if (!tryScheduleD418WritePhi2_(phi2, blockEndPhi2, writePhi2)) {
                    // The next physical C64 write would land outside this
                    // render quantum. Keep the DIGI phase and voice position
                    // untouched so the write is emitted sample-accurately in
                    // the following block instead of leaking a future-timestamp
                    // SID event into the current block.
                    break;
                }
                digiPhase_ -= 1.0;
                const int writeFrame = hostFrameForPhi2_(writePhi2, blockStartPhi2, frameToPhi2, frames);
                if (!hasRenderableVoiceAtFrame_(writeFrame)) {
                    // A pending PHI2 write can be carried from the previous
                    // quantum while a new sample-accurate trigger is armed
                    // later in this block. Do not consume sample data before
                    // the host-frame that physically contains the SID write.
                    digiPhase_ += 1.0;
                    break;
                }
                const std::uint8_t nibble = renderNextNibble_(sampleBank, writeFrame);
                blockPeak = std::max(blockPeak, std::abs((static_cast<float>(nibble) / 7.5f) - 1.0f));
                emitD418_(sidBridge, openBus, writePhi2, static_cast<std::uint32_t>(writeFrame), nibble, ioVisible);
            }
        }
        // Sample-accurate start offsets are block-local. Once this render
        // quantum has elapsed, any still-active voice must continue from the
        // beginning of the next block rather than waiting for the same host
        // frame number again.
        settleBlockStartOffsets_(frames);
        std::uint8_t active = 0u;
        for (const auto& v : voices_) if (v.active) ++active;
        telemetry_.playingVoiceCount = active;
        telemetry_.peakVoiceCount = std::max(telemetry_.peakVoiceCount, active);
        telemetry_.scopePeak = std::max(blockPeak, telemetry_.scopePeak * 0.86f);
        if (active == 0u && telemetry_.d418WritesThisBlock == 0u) {
            resetDigiWriteScheduler_();
            resetEmitCollisionAnchor_();
        }
        markProcessEnd_(blockEndPhi2);
    }

    // Copy the scope buffer to the destination. The newest sample is always
    // written at scopeWritePos_ so this performs a circular copy.
    void copyScope(float* dst, int maxSamples) const noexcept {
        if (!dst || maxSamples <= 0) return;
        const int n = std::min(maxSamples, kScopeLen);
        const std::uint32_t wp = scopeWritePos_ & static_cast<std::uint32_t>(kScopeLen - 1);
        for (int i = 0; i < n; ++i) {
            const std::uint32_t rp = (wp + static_cast<std::uint32_t>(i)) & static_cast<std::uint32_t>(kScopeLen - 1);
            dst[i] = scope_[rp];
        }
    }

private:
    struct Voice final {
        bool        active = false;
        std::uint8_t sourceType = 0;
        std::uint8_t slotIndex = 0;
        std::uint8_t factorySlotIndex = 0;
        std::uint8_t userSampleIndex = 0;
        std::uint32_t userSampleHandle = 0;
        std::uint16_t sampleLength = 1;
        float       pos = 0.0f;
        float       inc = 1.0f;
        float       gain = 0.0f;
        std::uint16_t start = 0;
        std::uint16_t end   = 1;
        std::uint8_t flags = 0u;
        std::uint32_t age = 0u;
        int        startSampleOffset = 0;
    };

    double hostSampleRate_ = 44100.0;
    double c64Phi2Hz_      = static_cast<double>(ArpSID::C64::kPalPhi2Hz);
    double digiPhase_      = 0.0;
    DigiD418Config config_{};
    std::array<Voice, kMaxVoices> voices_{};
    DigiD418Telemetry telemetry_{};
    std::array<float, kScopeLen> scope_{};
    std::array<DigiD418ForensicEvent, kForensicLen> forensic_{};
    std::uint32_t scopeWritePos_ = 0u;
    std::uint32_t forensicWritePos_ = 0u;
    std::uint32_t ageCounter_ = 0u;
    std::uint8_t lastStep_ = 255u;
    bool haveStep_ = false;
    int pendingTriggerOffset_ = 0;
    bool haveNextWritePhi2_ = false;
    double nextWritePhi2_ = 0.0;
    bool haveLastProcessEndPhi2_ = false;
    std::uint64_t lastProcessEndPhi2_ = 0u;
    std::uint64_t lastEmitPhi2_ = ~std::uint64_t{0};
    std::uint8_t lastEmitValue_ = 0u;

    static double sanitizeRate_(double v, double fallback, double lo, double hi) noexcept {
        return (std::isfinite(v) && v >= lo && v <= hi) ? v : fallback;
    }
    static DigiD418Config sanitizeConfig_(DigiD418Config c) noexcept {
        c.digiRateHz = std::clamp<std::uint32_t>(c.digiRateHz, 1000u, 32000u);
        if (c.clockSource != DigiClockSource::Phi2FixedRate) {
            c.clockSource = DigiClockSource::Phi2FixedRate;
        }
        c.externalD418HighNibble &= 0xF0u;
        if (!c.preserveD418HighNibble) {
            c.useExternalD418HighNibble = false;
            c.externalD418HighNibble = 0u;
        }
        return c;
    }

    void prepareTriggers_(const ArpSID::GUI::GuiRealtimeDigiProjection& projection,
                          const ArpSID::GUI::DigiSampleBankBlob& sampleBank,
                          int frames,
                          bool sequencerEnabled,
                          bool allowTriggers,
                          int triggerSampleOffset) noexcept {
        if (!sequencerEnabled) {
            haveStep_ = false;
            return;
        }
        const std::uint8_t step = static_cast<std::uint8_t>(projection.stepIndex % ArpSID::GUI::kDigiStepCount);
        const bool stepEdge = (!haveStep_ || step != lastStep_);
        if (stepEdge && allowTriggers) {
            pendingTriggerOffset_ = (triggerSampleOffset > 0 && triggerSampleOffset < frames) ? triggerSampleOffset : 0;
            triggerStep_(projection, sampleBank);
            pendingTriggerOffset_ = 0;
        }
        lastStep_ = step;
        haveStep_ = true;
    }

    void triggerStep_(const ArpSID::GUI::GuiRealtimeDigiProjection& projection,
                      const ArpSID::GUI::DigiSampleBankBlob& sampleBank) noexcept {
        for (std::uint8_t slotIndex = 0; slotIndex < ArpSID::GUI::kDigiActiveSlotCount; ++slotIndex) {
            const auto& slot = projection.slots[slotIndex];
            if (!slot.activeAtStep || slot.stepVelocity == 0u || slot.volume == 0u) continue;
            const bool isFactory = slot.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::FactorySlot);
            const bool isUser    = slot.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::UserImport);
            if (!isFactory && !isUser) continue;
            std::uint8_t factoryIndex = 0u;
            std::uint16_t length = 1u;
            float sourceRate = static_cast<float>(ArpSID::GUI::kDigiUserSampleCanonicalRateHz);
            if (isUser) {
                const auto* clip = ArpSID::GUI::digiFindUserSampleClipRealtime(sampleBank, slot.userSampleIndex, slot.userSampleHandle);
                if (!clip) { ++telemetry_.unavailableUserImportCount; continue; }
                length = clip->frameCount;
                sourceRate = static_cast<float>(clip->sourceSampleRateHz);
            } else {
                factoryIndex = static_cast<std::uint8_t>(std::min<int>(slot.factorySlotIndex, ArpSID::GUI::kKitDigiSlotCount - 1));
                length = factoryLength_(factoryIndex);
                sourceRate = static_cast<float>(ArpSID::GUI::kDigiUserSampleCanonicalRateHz);
            }
            Voice* v = allocateVoice_();
            if (!v) return;
            const std::uint16_t start = static_cast<std::uint16_t>(std::min<int>(length - 1u, (int(length) * int(slot.startOffset)) / 256));
            const std::uint16_t remaining = static_cast<std::uint16_t>(std::max<int>(1, int(length) - int(start)));
            const std::uint16_t scaled = (slot.lengthScale == 0u)
                ? remaining
                : static_cast<std::uint16_t>(std::max<int>(32, (int(remaining) * int(slot.lengthScale)) / 255));
            const std::uint16_t end = static_cast<std::uint16_t>(std::min<int>(length, int(start) + int(scaled)));
            const bool reverse = (slot.flags & ArpSID::GUI::kDigiFlagReverse) != 0u;
            const float pitch = std::pow(2.0f, static_cast<float>(slot.tuneShift) / 12.0f);
            const float inc = std::max(0.035f, (sourceRate / static_cast<float>(std::max(1u, config_.digiRateHz))) * pitch);
            *v = Voice{};
            v->active = true;
            v->sourceType = slot.sourceType;
            v->slotIndex = slotIndex;
            v->factorySlotIndex = factoryIndex;
            v->userSampleIndex = slot.userSampleIndex;
            v->userSampleHandle = slot.userSampleHandle;
            v->sampleLength = length;
            v->start = start;
            v->end = std::max<std::uint16_t>(static_cast<std::uint16_t>(start + 1u), end);
            v->pos = reverse ? static_cast<float>(v->end - 1u) : static_cast<float>(v->start);
            v->inc = reverse ? -inc : inc;
            v->gain = (static_cast<float>(slot.stepVelocity) / 127.0f) * (static_cast<float>(slot.volume) / 255.0f);
            v->flags = slot.flags;
            v->age = ++ageCounter_;
            v->startSampleOffset = pendingTriggerOffset_;
            telemetry_.lastTriggeredSlot = slotIndex;
            telemetry_.lastTriggeredFactorySlot = isFactory ? slot.absoluteFactorySlot : 0u;
            ++telemetry_.triggerCount;
        }
    }

    Voice* allocateVoice_() noexcept {
        for (auto& v : voices_) if (!v.active) return &v;
        ++telemetry_.voiceStealCount;
        // Match the legacy DIGI sampler policy: prefer stealing a quiet or
        // nearly-finished voice instead of blindly killing the oldest one.
        Voice* best = &voices_[0];
        float bestScore = 1.0e30f;
        for (auto& v : voices_) {
            const float remaining = (v.inc >= 0.0f)
                ? std::max(0.0f, static_cast<float>(v.end) - v.pos)
                : std::max(0.0f, v.pos - static_cast<float>(v.start));
            const float score = remaining * std::max(0.05f, v.gain)
                              - static_cast<float>(v.age) * 1.0e-6f;
            if (score < bestScore) {
                bestScore = score;
                best = &v;
            }
        }
        return best;
    }

    void resetEmitCollisionAnchor_() noexcept {
        lastEmitPhi2_ = ~std::uint64_t{0};
        lastEmitValue_ = 0u;
    }

    void resetDigiWriteScheduler_() noexcept {
        haveNextWritePhi2_ = false;
        nextWritePhi2_ = 0.0;
        haveLastProcessEndPhi2_ = false;
        lastProcessEndPhi2_ = 0u;
    }

    void reconcileTimelineBoundary_(std::uint64_t blockStartPhi2) noexcept {
        if (!haveLastProcessEndPhi2_) return;
        if (blockStartPhi2 == lastProcessEndPhi2_) return;
        // Any non-contiguous PHI2 boundary is a transport/bounce/scheduler
        // discontinuity for this standalone DIGI authority. Reset pending
        // write timing and residual phase instead of carrying a future D418
        // event from the old timeline into the new block.
        ++telemetry_.timelineDiscontinuityResetCount;
        digiPhase_ = 0.0;
        resetDigiWriteScheduler_();
        resetEmitCollisionAnchor_();
    }

    void markProcessEnd_(std::uint64_t blockEndPhi2) noexcept {
        lastProcessEndPhi2_ = blockEndPhi2;
        haveLastProcessEndPhi2_ = true;
    }

    static int hostFrameForPhi2_(std::uint64_t phi2,
                                 std::uint64_t blockStartPhi2,
                                 double frameToPhi2,
                                 int frames) noexcept {
        if (frames <= 1 || frameToPhi2 <= 0.0) return 0;
        if (phi2 <= blockStartPhi2) return 0;
        const double rel = static_cast<double>(phi2 - blockStartPhi2) / frameToPhi2;
        const int f = static_cast<int>(std::floor(rel));
        return std::clamp(f, 0, frames - 1);
    }

    bool tryScheduleD418WritePhi2_(std::uint64_t framePhi2,
                                   std::uint64_t blockEndPhi2,
                                   std::uint64_t& out) noexcept {
        const double framePhi2D = static_cast<double>(framePhi2);
        const double phi2PerWrite = c64Phi2Hz_ / static_cast<double>(std::max<std::uint32_t>(1u, config_.digiRateHz));
        if (!haveNextWritePhi2_ || nextWritePhi2_ < framePhi2D) {
            nextWritePhi2_ = framePhi2D;
            haveNextWritePhi2_ = true;
        }
        std::uint64_t candidate = static_cast<std::uint64_t>(std::llround(nextWritePhi2_));
        if (lastEmitPhi2_ != ~std::uint64_t{0} && candidate <= lastEmitPhi2_) {
            candidate = lastEmitPhi2_ + 1u;
        }
        if (candidate >= blockEndPhi2) {
            // Do not commit the write interval yet. The pending scheduler
            // position remains available for the next render quantum, while
            // voice position and DIGI phase stay unadvanced because no C64 bus
            // cycle has happened inside the current quantum.
            nextWritePhi2_ = static_cast<double>(candidate);
            return false;
        }
        out = candidate;
        nextWritePhi2_ = static_cast<double>(candidate) + phi2PerWrite;
        return true;
    }

    bool hasActiveVoice_() const noexcept {
        for (const auto& v : voices_) if (v.active) return true;
        return false;
    }

    bool hasRenderableVoiceAtFrame_(int hostFrame) const noexcept {
        for (const auto& v : voices_) {
            if (v.active && hostFrame >= v.startSampleOffset) return true;
        }
        return false;
    }

    void settleBlockStartOffsets_(int frames) noexcept {
        if (frames <= 0) return;
        for (auto& v : voices_) {
            if (!v.active || v.startSampleOffset <= 0) continue;
            v.startSampleOffset = std::max(0, v.startSampleOffset - frames);
        }
    }

    std::uint8_t renderNextNibble_(const ArpSID::GUI::DigiSampleBankBlob& sampleBank,
                                   int hostFrame) noexcept {
        float mix = 0.0f;
        std::uint8_t active = 0u;
        for (auto& v : voices_) {
            if (!v.active || hostFrame < v.startSampleOffset) continue;
            ++active;
            const float raw = (v.sourceType == static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::FactorySlot))
                ? factoryD418SampleAt_(v.factorySlotIndex, v.pos, v.sampleLength)
                : userSampleAt_(sampleBank, v.userSampleIndex, v.userSampleHandle, v.pos);
            mix += raw * v.gain;
            advanceVoice_(v);
        }
        telemetry_.playingVoiceCount = active;
        const std::uint8_t nibble = pcmToD418Nibble_(sanitizeUnit_(mix));
        scope_[scopeWritePos_++ & static_cast<std::uint32_t>(kScopeLen - 1)] = (static_cast<float>(nibble) / 7.5f) - 1.0f;
        telemetry_.lastNibble = nibble;
        return nibble;
    }

    void emitD418_(ArpSID::C64::C64SidBridgeState& sidBridge,
                   ArpSID::C64::OpenBusLatch& openBus,
                   std::uint64_t phi2,
                   std::uint32_t hostFrame,
                   std::uint8_t nibble,
                   bool ioVisible) noexcept {
        const std::uint8_t old = sidBridge.regs[0x18u];
        const std::uint8_t highNibbleSource = config_.useExternalD418HighNibble
            ? config_.externalD418HighNibble
            : old;
        const std::uint8_t value = config_.preserveD418HighNibble
            ? static_cast<std::uint8_t>((highNibbleSource & 0xF0u) | (nibble & 0x0Fu))
            : static_cast<std::uint8_t>(nibble & 0x0Fu);
        const std::uint8_t openBefore = openBus.value();
        if (config_.countCollisions && lastEmitPhi2_ == phi2 && lastEmitValue_ != value) {
            ++telemetry_.d418CollisionCount;
        }
        lastEmitPhi2_ = phi2;
        lastEmitValue_ = value;
        if (config_.driveOpenBus && config_.authMode == DigiAuthMode::StandaloneD418Layer) {
            openBus.drive(value, phi2);
            ++telemetry_.openBusDriveCount;
        }
        bool accepted = false;
        if (ioVisible) {
            // C64SidBridgeState::sidWrite() updates its register shadow before
            // it discovers timed-write queue overflow. For AUTH DIGI that is
            // too late: an unqueued $D418 write must not be treated as having
            // reached the SID, and it must not poison the high-nibble source for
            // the next write. Preflight the bridge queue so overflow is a pure
            // dropped bus/SID-accept event: attempts and forensic state are still
            // recorded, but the SID shadow remains bit-exactly as it was.
            if (sidBridge.timedWriteCount < ArpSID::C64::C64SidBridgeState::kMaxTimedWrites) {
                sidBridge.sidWrite(0x18u, value, phi2);
                accepted = true;
            } else {
                ++sidBridge.timedWriteOverflow;
                ++telemetry_.d418WriteQueueOverflow;
            }
        } else {
            ++telemetry_.d418WritesBlockedByIoBank;
        }
        ++telemetry_.d418WriteCount;
        ++telemetry_.d418WritesThisBlock;
        if (accepted) {
            ++telemetry_.d418SidAcceptedWriteCount;
            ++telemetry_.d418SidAcceptedWritesThisBlock;
        }
        telemetry_.lastOldD418 = old;
        telemetry_.lastD418 = value;
        telemetry_.lastOpenBus = openBus.value();
        const bool openBusDriven = (config_.driveOpenBus && config_.authMode == DigiAuthMode::StandaloneD418Layer);
        recordForensic_(phi2, hostFrame, old, value, nibble, openBefore, openBus.value(),
                        ioVisible, accepted, openBusDriven);
    }

    void recordForensic_(std::uint64_t phi2, std::uint32_t hostFrame,
                         std::uint8_t oldD418, std::uint8_t newD418, std::uint8_t nibble,
                         std::uint8_t openBefore, std::uint8_t openAfter,
                         bool ioVisible, bool accepted, bool openBusDriven) noexcept {
        ArpSID::C64::C64BusEvent bus{};
        bus.phi2Cycle = phi2;
        bus.address = 0xD418u;
        bus.data = newD418;
        bus.access = ArpSID::C64::C64BusAccess::Write;
        bus.source = ArpSID::C64::C64BusSource::DigiVirtualDevice;
        bus.ioVisible = ioVisible;
        bus.vicStolen = false;
        bus.openBusDriven = openBusDriven;
        bus.sidAccepted = accepted;
        forensic_[forensicWritePos_++ & static_cast<std::uint32_t>(kForensicLen - 1)] =
            DigiD418ForensicEvent{phi2, hostFrame, oldD418, newD418, nibble,
                                  openBefore, openAfter, ioVisible, accepted, bus};
    }

    static float sanitizeUnit_(float v) noexcept {
        if (!std::isfinite(v)) return 0.0f;
        return std::clamp(v, -1.0f, 1.0f);
    }

    static std::uint8_t pcmToD418Nibble_(float v) noexcept {
        const float clamped = sanitizeUnit_(v);
        const int code = static_cast<int>(std::lround((clamped + 1.0f) * 7.5f));
        return static_cast<std::uint8_t>(std::clamp(code, 0, 15));
    }

    // Factory sample lengths used by the default ArpSID kit. Matches
    // digi_sampler_engine.h.
    static std::uint16_t factoryLength_(std::uint8_t idx) noexcept {
        static constexpr std::uint16_t lengths[10] = {4600u,5200u,7000u,2800u,3900u,2200u,3100u,8200u,3600u,5800u};
        return static_cast<std::uint16_t>(lengths[idx % 10u] + static_cast<std::uint16_t>((idx / 10u) * 420u));
    }

    // Procedurally generates factory samples. Copied from DigiSamplerEngine.
    static float fastNoise_(std::uint32_t x) noexcept {
        x ^= x >> 16u;
        x *= 0x7feb352du;
        x ^= x >> 15u;
        x *= 0x846ca68bu;
        x ^= x >> 16u;
        return (static_cast<float>(x & 0xFFFFu) / 32767.5f) - 1.0f;
    }
    static float square_(float x) noexcept {
        const float f = x - std::floor(x);
        return f < 0.5f ? 1.0f : -1.0f;
    }
    static float factorySampleAt_(std::uint8_t idx, float pos, std::uint16_t length) noexcept {
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float safePos = std::isfinite(pos) ? pos : 0.0f;
        const float len = static_cast<float>(std::max<std::uint16_t>(length, 1u));
        const float t   = std::clamp(safePos / len, 0.0f, 1.0f);
        const float inv = 1.0f - t;
        const float envFast = inv * inv * inv;
        const float envMed  = inv * inv;
        const std::uint8_t family = static_cast<std::uint8_t>(idx % 10u);
        const float variant = static_cast<float>(idx / 10u);
        const float noise = fastNoise_(static_cast<std::uint32_t>(safePos) + 0x9E3779B9u * (idx + 1u));
        switch (family) {
            case 0: {
                const float phase = (30.0f + 4.0f * variant) * t - (18.0f + 2.0f * variant) * t * t;
                return std::sin(kTwoPi * phase) * envFast;
            }
            case 1: {
                const float body = std::sin(kTwoPi * (18.0f + variant * 2.0f) * t) * envMed * 0.50f;
                return body + noise * envFast * 0.72f;
            }
            case 2: {
                const bool burst = (t < 0.10f) || (t > 0.18f && t < 0.28f) || (t > 0.36f && t < 0.47f);
                return burst ? noise * (0.35f + 0.65f * envMed) : noise * envFast * 0.18f;
            }
            case 3:
                return (noise - fastNoise_(static_cast<std::uint32_t>(safePos) + 19u)) * 0.45f * inv;
            case 4: {
                const float phase = (22.0f + 3.0f * variant) * t - (9.0f + variant) * t * t;
                return std::sin(kTwoPi * phase) * envMed;
            }
            case 5:
                return (square_(t * (58.0f + variant * 5.0f)) * 0.45f +
                        std::sin(kTwoPi * (78.0f + variant * 7.0f) * t) * 0.55f) * envFast;
            case 6:
                return (square_(t * (23.0f + variant * 2.0f)) +
                        square_(t * (31.0f + variant * 2.0f))) * 0.40f * envMed;
            case 7:
                return noise * (0.9f - 0.35f * t) * inv;
            case 8:
                return (std::sin(kTwoPi * (48.0f + variant * 4.0f) * t) * 0.5f + noise * 0.5f) * envMed;
            default:
                return (square_(t * (12.0f + variant * 3.0f)) * 0.35f + noise * 0.65f) * envMed;
        }
    }

    // factory DIGI playback is also a held 4-bit $D418 byte stream.
    // The waveform recipe is used only to choose the nearest nibble for the
    // current 8 kHz DIGI tick; the realtime layer never exposes interpolated
    // factory PCM as an audible sampler source.
    static float factoryD418SampleAt_(std::uint8_t idx, float pos, std::uint16_t length) noexcept {
        const std::uint16_t safeLength = std::max<std::uint16_t>(1u, length);
        const float safePos = std::isfinite(pos) ? pos : 0.0f;
        const std::uint16_t i = static_cast<std::uint16_t>(std::clamp(safePos, 0.0f, static_cast<float>(safeLength - 1u)));
        const float raw = factorySampleAt_(idx, static_cast<float>(i), safeLength);
        const std::uint8_t nibble = ArpSID::GUI::digiFloatToD418Nibble(raw);
        return (static_cast<float>(nibble) / 7.5f) - 1.0f;
    }

    static float userSampleAt_(const ArpSID::GUI::DigiSampleBankBlob& sampleBank,
                               std::uint8_t index,
                               std::uint32_t handle,
                               float pos) noexcept {
        const auto* clip = ArpSID::GUI::digiFindUserSampleClipRealtime(sampleBank, index, handle);
        if (!clip || clip->frameCount == 0u) return 0.0f;
        const float maxPos = static_cast<float>(clip->frameCount - 1u);
        const float safePos = std::isfinite(pos) ? pos : 0.0f;
        const float p = std::clamp(safePos, 0.0f, maxPos);
        const std::uint32_t i0 = static_cast<std::uint32_t>(p);
        const std::uint32_t i1 = std::min<std::uint32_t>(i0 + 1u, clip->frameCount - 1u);
        const float frac = p - static_cast<float>(i0);
        (void)i1;
        (void)frac;
        // C64-auth DIGI playback is a stepped $D418 volume-DAC stream. Do not
        // interpolate between stored user-sample points; a real player writes
        // one 4-bit value per DIGI tick and holds it until the next write.
        const std::uint8_t nibble = ArpSID::GUI::digiPcm8ToD418Nibble(clip->pcm[i0]);
        return (static_cast<float>(nibble) / 7.5f) - 1.0f;
    }
    // Advances a voice one sample. Handles looping and reverse playback.
    void advanceVoice_(Voice& v) noexcept {
        const bool loop = (v.flags & ArpSID::GUI::kDigiFlagLoop) != 0u;
        if (!std::isfinite(v.pos)) v.pos = static_cast<float>(v.start);
        if (!std::isfinite(v.inc) || v.inc == 0.0f) { v.active = false; return; }
        v.pos += v.inc;
        if (!std::isfinite(v.pos)) { v.active = false; return; }
        if (v.inc >= 0.0f) {
            if (v.pos < static_cast<float>(v.end)) return;
            if (loop && v.end > v.start + 1u) {
                const float len = static_cast<float>(v.end - v.start);
                v.pos = static_cast<float>(v.start) + std::fmod(v.pos - static_cast<float>(v.start), len);
            } else {
                v.active = false;
            }
        } else {
            if (v.pos >= static_cast<float>(v.start)) return;
            if (loop && v.end > v.start + 1u) {
                const float len = static_cast<float>(v.end - v.start);
                while (v.pos < static_cast<float>(v.start)) v.pos += len;
            } else {
                v.active = false;
            }
        }
    }
};

} // namespace ArpSID