// ArpSIDDSPKernel.hpp
// ArpSID AUv3 — Realtime C++ DSP Kernel
//
// Self-contained C++ wrapper around the ArpSID synthesis engines.
// No VST3 SDK headers — safe to include from ObjC++ .mm translation units.
//
// Thread-safety contract
// ──────────────────────
// setParameter() / getParameter() → any thread (ingress only, no direct runtime mutation)
// process() / handleNote*() → render thread only, never concurrent
// setup() / reset() → must NOT overlap with process()
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#pragma once
#include "ArpSIDCanonicalEvents.h"
#include "ArpSIDStateSerializer.h"
#include "arpsid/core/sid_runtime_voice_policy.h"
#include "ArpSIDModMatrix.h"
#include "ArpSIDSequencerEngine.h"
#include "ArpSIDSidRegMapper.h"
#include "ArpSIDParityTrace.h"

#include "arpsid/core/sid_chip.h"
#include "arpsid/core/sid_audio_processors.h"
#include "arpsid/core/sid_variant_ops.h"
#include "ArpSIDComponentFlavor.h"
#include "arpsid/core/math_utils.h"
#include "arpsid/engines/bitperfect_engine.h"
#include "arpsid/engines/arpeggiator.h"
#include "arpsid/engines/digi_sampler_engine.h"
#include "arpsid/engines/drsid_engine.h"
#include "arpsid/engines/sid_register_engine.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "../factory_patch_params.h"
#include "arpsid/core/sid_mod_matrix_types.h"
#include "arpsid/core/sid_runtime_model.h"
#include "arpsid/core/sid_runtime_execution.h"
#include "arpsid/core/sid_postfx_timeline.h"
#include "arpsid/core/sid_render_pipeline_capabilities.h"
#include "arpsid/core/sid_runtime_audio_kernel.h"
#include "arpsid/gui/digi_record_limits.h"
#include "arpsid/core/sid_runtime_target_adapter.h"
#include "arpsid/core/sid_runtime_host_ops.h"
#include "arpsid/core/sid_runtime_midi_ops.h"
#include "arpsid/core/sid_runtime_mod_ops.h"
#include "arpsid/core/sid_runtime_engine_ops.h"
#include "arpsid/core/sid_runtime_register_ops.h"
#include "arpsid/core/sid_ownership_mailbox.h"
#include "arpsid/core/sid_state_blob_slot.h"
#include "arpsid/core/sid_runtime_register_shadow.h"
#include "arpsid/core/sid_runtime_register_shadow_ops.h"
#include "arpsid/core/sid_runtime_shared_kernel.h"
#include "arpsid/core/sid_runtime_engine_bank.h"
#include "arpsid/core/sid_runtime_render_host.h"
#include "arpsid/core/sid_hifi_transcendence.h"
#include "arpsid/core/sid_midi_cc_mapping.h"
#include "arpsid/core/sid_runtime_fractional_render.h"
#include "arpsid/core/sid_runtime_kernel_dispatch.h"
#include "arpsid/core/sid_runtime_render_surface.h"
#include "arpsid/core/sid_runtime_note_surface.h"
#include "arpsid/core/sid_runtime_backend_projection.h"
#include "arpsid/core/sid_runtime_synth_register_scheduler.h"
#include "arpsid/core/sid_runtime_synth_performance.h"
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_embedded_rom_loader.h"
#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/core/c64_sid_bridge.h"
#include "arpsid/core/c64_sid_mix.h"
#include "arpsid/core/c64_fixed_write_scheduler.h"
#include "arpsid/core/c64_d418_capture.h"
#include "arpsid/core/c64_sid_projection_bridge.h"
#include "arpsid/core/c64_timing_math.h"
#include "arpsid/core/c64_telemetry.h"
#include "arpsid/core/host_sample_rate.h"
// Authentic DIGI $D418 stream engine and C64 open-bus helpers. These headers
// provide the DigiD418StreamEngine class, the OpenBusLatch for modelling
// bus‑driven last‑value propagation and C64 bus helpers used by the DIGI
// engine. They are intentionally included after the core timing headers.
#include "arpsid/engines/digi_d418_stream_engine.h"
#include "arpsid/core/c64_open_bus.h"
#include "arpsid/core/c64_bus_event.h"
// Pass O: canonical policy files — AU wrapper must use these, not own the laws
#include "arpsid/core/sid_runtime_reset_policy.h"
#include "arpsid/core/sid_runtime_event_materializer.h"
#include "arpsid/core/sid_runtime_host_policy.h"
#include "arpsid/core/sid_runtime_host_block.h"
#include "arpsid/core/sid_runtime_state_apply_policy.h"
#include "arpsid/core/sid_runtime_held_replay.h"
#include "arpsid/core/sid_realtime_guard.h"
#include "arpsid/core/realtime_atomic_contract.h"
#include "arpsid/core/scope_triple_buffer.h"
#include "arpsid/core/audited_mutex.h"
#include "arpsid/audio/mix_fx_processors.h"
#include "arpsid/engines/drum_engine_host_bridge.h"
#include "arpsid/engines/drum_stem_mixer.h"
#include "arpsid/gui/kit_sequencer.h"
#include "arpsid/gui/kit_sid808_class_map.h"
#include "arpsid_bounded_mpsc_ring.h"
#include "arpsid/modulation/lfo.h"
#include "parameter_ids.h"
#include "arpsid/gui/diagnostic_snapshot.h"
#include "arpsid/gui/gui_realtime_projection_v588.h"
#include "arpsid/gui/sidcore_panel_model.h"

#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <cmath>
#include <new>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif
#if defined(__SSE__)
#include <xmmintrin.h>
#endif


static constexpr double kArpSIDDefaultSampleRate = ArpSID::kCanonicalDefaultSampleRate;
static constexpr double kArpSIDMinSampleRate = ArpSID::kCanonicalMinSampleRate;
static constexpr double kArpSIDMaxSampleRate = ArpSID::kCanonicalMaxSampleRate;

static inline double ArpSIDSanitizeHostSampleRate(double sr) noexcept {
    return ArpSID::canonicalizeHostSampleRate(sr, kArpSIDDefaultSampleRate);
}

static inline bool ArpSIDIsCanonicalHostSampleRate(double sr) noexcept {
    return ArpSID::isCanonicalHostSampleRate(sr);
}

static inline float ArpSIDSanitizeTelemetryUnitFloat(float v) noexcept {
    return std::clamp(ArpSID::ArpSID_sanitizeFloat(v), 0.0f, 1.0f);
}

static inline float ArpSIDSanitizeTelemetryBipolarFloat(float v) noexcept {
    return std::clamp(ArpSID::ArpSID_sanitizeFloat(v), -1.0f, 1.0f);
}

static inline double ArpSIDSanitizeTelemetryTempo(double v, double fallback = 120.0) noexcept {
    if (!std::isfinite(v) || v < 1.0 || v > 1000.0) return fallback;
    return v;
}

static inline double ArpSIDSanitizeTelemetryBeat(double v) noexcept {
    if (!std::isfinite(v) || v < 0.0) return 0.0;
    return v;
}

static inline float ArpSIDDigiSanitizeMixSample_(float v) noexcept {
    if (!std::isfinite(v)) return 0.0f;
    return std::clamp(v, -1.25f, 1.25f);
}

// ─── Ingress rings — bounded lock-free MPSC (audit #8/#9/#10/#11) ───────────────
// Multiple producers (host MIDI, CoreMIDI, GUI piano/knobs, host parameter
// automation), single consumer (the render thread). Backed by BoundedMpscRing
// (Vyukov bounded MPSC): each producer reserves a slot with one CAS and publishes
// with a release store; the consumer reads with acquire. This makes the public
// "push a MIDI/parameter intent from ANY thread" contract genuinely correct — no
// data race, and no lossy try-lock contention (a push only fails when the ring is
// truly full). The previous design was a single-consumer try-lock ring that
// claimed lock-free/any-thread but actually dropped events on producer contention;
// that contradiction is now resolved.
//
// clear() is a CONSUMER-SIDE drainAll(): the single consumer pops everything
// currently queued. It is safe even with concurrent producers (MPSC), unlike the
// old raw tail_ mutation — concurrently-pushed events simply remain for the next
// drain. droppedContention is retained for ABI/telemetry but is always 0 for MPSC.
struct ArpSIDMidiRingBuffer {
    struct Event {
        uint8_t  data[4];
        uint8_t  len;
        uint64_t hostTime;
        int32_t  explicitSampleOffset;
    };
    static constexpr size_t kCap = 8192;  // power-of-two; MPSC ring uses all kCap slots
    // audit #8/#9/#10/#11: correct bounded MPSC ring — multiple producers (host
    // MIDI / CoreMIDI / GUI), single render consumer, lock-free, lossless except
    // genuine fullness (no try-lock contention loss). This makes the public
    // "push from any thread" contract actually correct.
    ArpSID::BoundedMpscRing<Event, kCap> ring{};
    std::atomic<uint32_t> dropped{0};                 // total push failures (== droppedFull for MPSC)
    std::atomic<uint32_t> droppedFull{0};             // ring genuinely full
    std::atomic<uint32_t> droppedContention{0};       // always 0 for MPSC (kept for ABI/telemetry)

    bool push(const uint8_t* d, uint8_t n, uint64_t hostTime = 0, int32_t explicitSampleOffset = -1) noexcept {
        if (n == 0 || n > 4 || d == nullptr) return false;
        Event ev{};
        ev.len = n;
        ev.hostTime = hostTime;
        ev.explicitSampleOffset = explicitSampleOffset;
        std::memcpy(ev.data, d, n);
        if (!ring.push(ev)) {
            dropped.fetch_add(1, std::memory_order_relaxed);
            droppedFull.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    bool pop(Event& out) noexcept { return ring.pop(out); }

    // Factory/root ingress clear (audit #1): SEMANTICALLY-ATOMIC. Discards exactly
    // the events producers had committed by this call (a clean before/after
    // boundary) instead of the open-ended drainAll() that could also swallow notes
    // pushed live during the scrub. Single-consumer only.
    void clear() noexcept {
        ring.clearEnqueuedBeforeNow();
        dropped.store(0u, std::memory_order_relaxed);
        droppedFull.store(0u, std::memory_order_relaxed);
        droppedContention.store(0u, std::memory_order_relaxed);
    }
};

struct ArpSIDParamIntentRingBuffer {
    struct Event {
        uint32_t paramID;
        uint32_t generation;
        float    value;
        uint64_t hostTime;
        int32_t  explicitSampleOffset;
    };
    static constexpr size_t kCap = 8192;
    // audit #8/#9/#10/#11: correct bounded MPSC ring (see MIDI ring above).
    ArpSID::BoundedMpscRing<Event, kCap> ring{};
    std::atomic<uint32_t> dropped{0};
    std::atomic<uint32_t> droppedFull{0};
    std::atomic<uint32_t> droppedContention{0};       // always 0 for MPSC (kept for ABI/telemetry)

    bool push(uint32_t paramID, uint32_t generation, float value,
              uint64_t hostTime = 0, int32_t explicitSampleOffset = -1) noexcept {
        Event ev{};
        ev.paramID = paramID;
        ev.generation = generation;
        ev.value   = value;
        ev.hostTime = hostTime;
        ev.explicitSampleOffset = explicitSampleOffset;
        if (!ring.push(ev)) {
            dropped.fetch_add(1, std::memory_order_relaxed);
            droppedFull.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    bool pop(Event& out) noexcept { return ring.pop(out); }

    void clear() noexcept {
        ring.clearEnqueuedBeforeNow();  // audit #1: semantically-atomic ingress clear
        dropped.store(0u, std::memory_order_relaxed);
        droppedFull.store(0u, std::memory_order_relaxed);
        droppedContention.store(0u, std::memory_order_relaxed);
    }
};


namespace ArpSID {

class ArpSIDDSPKernel;
// Forward declarations for canonical block-processing helpers used by the
// standalone host adapter path (processBlock / processBlockMono).
// The legacy process() / ArpSIDKernelPrepareCanonicalRender /
// ArpSIDKernelFinalizeRender / ArpSIDKernelProcessLargeBlock entry points
// have been removed — all paths now route through processBlock().

//──────────────────────────────────────────────────────────────────────────────
/// Maps a MIDI note number to a DrSidEngine::DrumType.
/// GM mapping: 36=Kick, 38/40=Snare, 42=CHH, 46=OHH, 39/75=Clap,
/// 56=Cowbell, 41/43/45/47/48/50=Tom, 37/76=Rim.
static inline DrSidEngine::DrumType midiNoteToDrumType(int note) noexcept {
    return DrSidEngine::drumTypeForMidiNote(note);
}

//──────────────────────────────────────────────────────────────────────────────
class ArpSIDDSPKernel {
public:
    static constexpr int kMaxFramesPerBlock = 4096;

private:
    // No legacy process() friend declarations — that path is removed.
    struct RuntimeKernelCore {
        SidRuntimeEngineBank engineBank{};
        ArpSID::SidRuntimeProjectionState projection{};
        SidRuntimeModel runtimeModel{};
    } runtimeCore_{};
    // Shared runtime-owned engine bank. The AU kernel bridges host ingress to
    // one shared engine bundle instead of directly owning independent engines.
    SidRuntimeEngineBank&             engineBank_ = runtimeCore_.engineBank;
    BitPerfectEngine* bpe_() noexcept { return engineBank_.bitPerfect.get(); }
    const BitPerfectEngine* bpe_() const noexcept { return engineBank_.bitPerfect.get(); }
    Arpeggiator* arp_() noexcept { return engineBank_.arp.get(); }
    const Arpeggiator* arp_() const noexcept { return engineBank_.arp.get(); }
    DrSidEngine* drs_() noexcept { return engineBank_.drSid.get(); }
    const DrSidEngine* drs_() const noexcept { return engineBank_.drSid.get(); }
    LFOBank* lfos_() noexcept { return engineBank_.lfo.get(); }
    const LFOBank* lfos_() const noexcept { return engineBank_.lfo.get(); }
    SidRegisterEngine& sreg_() noexcept { return engineBank_.sidRegister; }
    const SidRegisterEngine& sreg_() const noexcept { return engineBank_.sidRegister; }
    SidRegisterEngine& sreg2_() noexcept { return engineBank_.sidRegister2; }
    SidRegisterEngine& sreg3_() noexcept { return engineBank_.sidRegister3; }
    SidRegisterEngine& sreg4_() noexcept { return engineBank_.sidRegister4; }
    SidRegisterEngine& sreg5_() noexcept { return engineBank_.sidRegister5; }
    // Map chip index 0..4 to its render engine (nullptr if out of range).
    SidRegisterEngine* sregForChip_(uint8_t chip) noexcept {
        switch (chip) {
            case 0u: return &engineBank_.sidRegister;
            case 1u: return &engineBank_.sidRegister2;
            case 2u: return &engineBank_.sidRegister3;
            case 3u: return &engineBank_.sidRegister4;
            case 4u: return &engineBank_.sidRegister5;
            default: return nullptr;
        }
    }
    static constexpr uint8_t kMaxRenderedSidChips = 5u;
    double                             sampleRate_ = 44100.0;

    std::array<std::atomic<float>, kNumParams> params_;
    // AUv2 host writable-parameter retention overlay.
    //
    // SidStateRootV1 is the canonical project/patch serialization authority, but
    // AUv2 has an additional host contract: writable parameters must retain their
    // last host-written values across reset/initialize. Some writable controls are
    // deliberately runtime-only/transient and therefore are not persisted in
    // SidStateRootV1 (for example Virtual Note/Gate). Without this overlay, a
    // pending StateRoot restore can correctly rebuild the patch but incorrectly
    // stomp those host-retained AUv2 parameters back to defaults after auval has
    // restored the parameter snapshot.
    //
    // Program and BankSlot are explicitly excluded when the overlay is applied:
    // they are preset readback/display metadata, not host-write audio authority.
    std::array<std::atomic<float>, kNumParams> hostParamRetentionOverlay_{};
    std::atomic<bool> hostParamRetentionOverlayPending_{false};

    // Serializable state template transfer (render thread → UI thread).
    //
    // audit P0-2 FIX: this used to be a version-counter two-slot seqlock. The render
    // thread encoded into slot ((v0+1)>>1)&1; the UI thread decoded from the last
    // completed slot and rejected torn results via a post-decode version recheck.
    // The recheck stopped a torn result from being *accepted*, but the UI still
    // *decoded from a slot the render writer could concurrently overwrite*: with only
    // two slots, a render thread that published twice during one (slow) UI decode
    // wrote the very slot being decoded — a data race on the blob bytes (UB / TSan
    // failure), and a torn length/count field could drive an out-of-bounds or huge
    // allocation mid-decode before the recheck rejected it.
    //
    // Replaced with ArpSID::OwnershipMailbox<FixedBlobSlot<...>>: the render producer
    // memcpys the encoded blob into its OWN buffer and publishes ownership; the UI
    // consumer takes ownership of the latest buffer and decodes from it. The producer
    // never touches the buffer the consumer owns, so the decode can never race or
    // tear. Producer side (encode + publish) stays wait-free and allocation-free.
    //
    // serializableStateTemplateMutex_ still serializes the (potentially multiple)
    // non-RT getState/UI consumers and guards serializableStateTemplate_; the render
    // producer never takes it. See sid_ownership_mailbox.h / sid_state_blob_slot.h.
    mutable ArpSID::AuditedSerializationMutex serializableStateTemplateMutex_{};
    mutable SidStateRootV1 serializableStateTemplate_{};   // cached — UI consumers only
    static constexpr size_t kSerialBlobSlotSize_ = ArpSID::kStateBufferSize + 65536u;
    mutable ArpSID::OwnershipMailbox<ArpSID::FixedBlobSlot<kSerialBlobSlotSize_>>
        templateBlobMailbox_{};
    std::array<float, kNumParams>              renderParams_{};
    uint8_t                                    sidSysByteCached_ = 0x02u;
    // P2 FIX: Use the shared timestamped register shadow instead of a plain byte
    // array so sample/cycle provenance is tracked and time-aware comparisons
    // (registerShadowMatchesAtOrBefore) work identically to VST.
    ArpSID::SidRuntimeRegisterShadow             sidQueuedShadow_{};
    std::array<std::atomic<bool>,  kNumParams> dirty_;
    ArpSIDMidiRingBuffer midiQueue_;
    ArpSIDParamIntentRingBuffer paramIntentQueue_;
    // A queue-full fallback must be an ordered parameter intent, not merely a
    // value mirror. Each parameter gets a generation. The packed fallback is
    // coherent across multiple UI/host producers and lets the render consumer
    // reject older queued intents after applying the newest dropped intent via
    // the same execution-owner path as ordinary automation.
    std::array<std::atomic<uint32_t>, kNumParams> paramIntentGeneration_{};
    std::array<std::atomic<uint64_t>, kNumParams> paramIntentFallbackPacked_{};
    std::array<std::atomic<uint32_t>, kNumParams> paramIntentAppliedFallbackGeneration_{};
    std::atomic<uint64_t> renderHostTime_{0};
    std::atomic<uint32_t> ingressDropTelemetry_{0};
    // fix-order #17 / audit #17: parameter automation queue drops are not always
    // audible loss because enqueueParameterIntent() also marks the parameter dirty
    // before attempting the timed queue push. When the queue is full, the render
    // thread still applies the latest value via flushDirtyParams_() at the block
    // boundary. Track that distinct dirty-flush fallback separately so diagnostics
    // do not conflate sample-accurate queue loss with value-loss.
    std::atomic<uint32_t> paramIntentDirtyFlushFallbackTelemetry_{0};
    // audit P2.13: per-session canonical event-queue overflow telemetry. The
    // overflow policy (drop lowest-priority / replace) was previously invisible,
    // so audibly missing NoteOns under dense automation had no diagnostic. These
    // are render-published cumulative counts; host tooling / validation logs /
    // the AU GUI read them through the accessors below.
    std::atomic<uint32_t> eventOverflowDroppedNoteOn_{0};
    std::atomic<uint32_t> eventOverflowDroppedNoteOff_{0};
    std::atomic<uint32_t> eventOverflowDroppedAutomation_{0};
    std::atomic<uint32_t> eventOverflowDroppedController_{0};
    std::atomic<uint32_t> eventOverflowDroppedTransport_{0};
    std::atomic<uint32_t> eventOverflowReplacedLowerPriority_{0};
    std::atomic<uint32_t> eventOverflowDroppedTotal_{0};
    // A raw MIDI note-off must never disappear when midiQueue_ is saturated.
    // Counts preserve overlapping same-note releases; host times retain their
    // order relative to the raw events drained from the primary queue.
    std::array<std::array<std::atomic<uint8_t>, 128>, 16> pendingDroppedNoteOffCount_{};
    std::array<std::array<std::atomic<uint64_t>, 128>, 16> pendingDroppedNoteOffHostTime_{};
    std::array<std::atomic<uint8_t>, 16> pendingAllNotesOff_{};
    std::array<std::atomic<uint8_t>, 16> pendingAllSoundOff_{};
    std::array<std::atomic<uint8_t>, 16> pendingSustainOff_{};
    std::array<std::atomic<uint8_t>, 16> pendingSostenutoOff_{};
    // Overflow fallbacks below are level-triggered latches: they preserve the
    // latest controller state per channel, not edge counts, which is the correct
    // semantic for these pending MIDI mirrors when the primary ingress ring fills.
    std::array<std::atomic<uint8_t>, 16> pendingSustain_{};
    std::array<std::atomic<uint8_t>, 16> pendingSustainDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingSostenuto_{};
    std::array<std::atomic<uint8_t>, 16> pendingSostenutoDirty_{};
    // physical pedal-state mirror preserved across factory queue scrub.
    std::array<std::atomic<uint8_t>, 16> liveSustainPedalDown_{};
    std::array<std::atomic<uint8_t>, 16> liveSostenutoPedalDown_{};
    std::array<std::atomic<uint16_t>, 16> pendingPitchBend14_{};
    std::array<std::atomic<uint8_t>, 16> pendingPitchBendDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingChannelPressure_{};
    std::array<std::atomic<uint8_t>, 16> pendingChannelPressureDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingModWheel_{};
    std::array<std::atomic<uint8_t>, 16> pendingModWheelDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingExpression_{};
    std::array<std::atomic<uint8_t>, 16> pendingExpressionDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingChannelVolume_{};
    std::array<std::atomic<uint8_t>, 16> pendingChannelVolumeDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingBankMsb_{};
    std::array<std::atomic<uint8_t>, 16> pendingBankMsbDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingBankLsb_{};
    std::array<std::atomic<uint8_t>, 16> pendingBankLsbDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingRpnMsb_{};
    std::array<std::atomic<uint8_t>, 16> pendingRpnMsbDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingRpnLsb_{};
    std::array<std::atomic<uint8_t>, 16> pendingRpnLsbDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingNrpnMsb_{};
    std::array<std::atomic<uint8_t>, 16> pendingNrpnMsbDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingNrpnLsb_{};
    std::array<std::atomic<uint8_t>, 16> pendingNrpnLsbDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingDataEntryMsb_{};
    std::array<std::atomic<uint8_t>, 16> pendingDataEntryMsbDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingDataEntryLsb_{};
    std::array<std::atomic<uint8_t>, 16> pendingDataEntryLsbDirty_{};
    std::array<std::atomic<uint8_t>, 16> pendingResetControllers_{};
    std::array<std::array<std::atomic<uint8_t>, 128>, 16> pendingPolyPressure_{};
    std::array<std::array<std::atomic<uint8_t>, 128>, 16> pendingPolyPressureDirty_{};

    // Channel-clear generation counters.
    // clearHeldIngressChannel_ used to zero 128×5 = 640 atomics per channel,
    // and clearAllPendingMidiFallbacks_ zeroed ~12 288 atomics total — an O(N)
    // stall on the audio thread. Instead, each per-channel pending state now
    // carries a "valid generation" number; entries are treated as stale when their
    // generation != pendingChannelGen_[ch]. Incrementing the counter effectively
    // clears all pending state for that channel in O(1).
    std::array<std::atomic<uint8_t>, 16> pendingChannelGen_{};

    // Pass O: arrival_order is now assigned exclusively by sid_ingress_merge.
    // This counter is kept ONLY for the held-replay local sort (order preservation).
    // It must NOT be used to stamp canonical arrival_order on events — merge owns that.

    ArpSID::SidRuntimeProjectionState& runtimeProjectionState_ = runtimeCore_.projection;
    bool  prevVirtualGate_ = false;
    float prevProgram_     = -1.0f;
    bool  prevSynthMode_   = false;
    int   prevVirtualNote_ = 60;


    ModMatrix       modMatrix_;
    SequencerEngine seqEngine_;
    static constexpr int kMaxSeqStepBoundariesPerBlock_ = 256;
    std::array<SequencerEngine::StepBoundary, kMaxSeqStepBoundariesPerBlock_> seqStepBoundaries_{};
    int seqStepBoundaryCount_ = 0;
    ParityTracer    parityTracer_;
    double          seqInternalBeatPosition_ = 0.0;
    bool            seqTransportRestartPending_ = false;
    double          seqTransportRestartBeat_ = 0.0;
    bool            prevSeqEnabled_ = false;
    bool            prevSeqFollowHost_ = true;
    int             prevSeqLength_ = 8;
    int             prevSeqModeIndex_ = 0;
    ArpSID::ComponentFlavor componentFlavor_ = ArpSID::ComponentFlavor::Hybrid;
    SidRuntimeModel& runtimeModel_ = runtimeCore_.runtimeModel;
    std::unique_ptr<ArpSID::SidRuntimeExecutionOwner<ArpSIDDSPKernel>> runtimeExecutionOwner_{};
    float** runtimeCanonicalBlockOutputs_{};
    float* runtimeCanonicalScratchL_{};
    float* runtimeCanonicalScratchR_{};
    int runtimeCurrentRenderOffset_ = 0;
    uint64_t        blockIndex_  = 0;                     // host-local only: render block counter

    // Render-owned C64 platform mirror. It follows the same PAL/NTSC SID clock
    // authority as the audio core and mirrors SID register images through the
    // deterministic 6510/CIA/VIC/SID bus surface. GUI telemetry only receives
    // atomically-published snapshots from this object.
    ArpSID::C64::C64Platform c64Platform_{};
    // PSID bridge rollback is now owned by C64Platform::begin/rollbackRenderMutationJournal().
    // No full C64Platform copy is kept in the DSP kernel.
    ArpSID::SidCycleClockState c64PlatformClock_{};
    uint64_t c64RealtimeCycleDebt_ = 0;
    ArpSID::SidCycleClockState c64PsidPassiveClock_{};
    uint64_t c64PsidPassiveCycleDebt_ = 0;
    bool c64BypassedPerformanceStateCleared_ = false;
    double c64PsidPlaySampleCountdown_ = 0.0;
    // v873 (timing audit P0-1): adaptive per-tune VBI play instruction budget. Starts
    // at the static default and doubles (up to a hard ceiling) whenever a play frame
    // is dropped purely for exhausting its budget — so an occasionally-heavy tune
    // (Hubbard/Tel-class play routines) stops permanently dropping frames. Reset to the
    // default only on tune change (resetC64SampleCursorsForHandoff_), never per block.
    static constexpr uint32_t kC64PsidAdaptivePlayBudgetCeiling = 65536u;
    uint32_t c64AdaptivePlayBudget_ = kC64PsidMaxInstructionsPerPlay;
    // v886: audible C64 SIDPLAY host-sample authority. This is deliberately
    // separate from c64TelemetryHostSampleCursor_: the audio path can return
    // before cosmetic telemetry publishing, so using the telemetry cursor for
    // SID write bucketization caused split-clock block-boundary drift.
    uint64_t c64PsidAudioHostSampleCursor_ = 0;
    uint64_t c64TelemetryHostSampleCursor_ = 0;
    uint64_t c64NextHeavyTelemetrySample_ = 0;
    uint64_t c64LastHeavyTelemetryBlock_ = UINT64_MAX;
    uint64_t c64LastHeavyTelemetryPhi2_ = UINT64_MAX;
    uint64_t c64LastHeavyTelemetryAudioPhaseSample_ = 0;
    uint32_t c64LastHeavyTelemetrySidHash_ = 0;
    uint8_t c64LastHeavyTelemetryPsidActive_ = 0;
    uint64_t c64HeavyTelemetryPeriodSamplesLast_ = 0;
    uint64_t c64TelemetrySkippedSnapshotCount_ = 0;
    uint64_t c64TelemetryForcedSnapshotCount_ = 0;
    uint64_t c64TelemetryCatchupClampCount_ = 0;
    bool c64HeavyTelemetryDemandGated_ = false;
    // PSID play() is a video/CIA-rate routine invoked only at the PAL/NTSC cadence,
    // not every AU render block. The audio thread performs bounded passive PHI2
    // advancement and fires the 6510 play trampoline when the video deadline is due.
    // Fix #7: kC64PsidMaxPlayCallsPerAudioBlock limits play calls per call to
    // renderC64PsidBlockIfActive_. For huge offline blocks (DAW bounce at
    // frame rates > 8 × playPeriodSamples per block), the render path should
    // chunk the block internally so every play event fires. The constant
    // bounds the PlayBase array size; the chunking logic uses it as the chunk
    // budget and re-invokes the render path with sub-blocks when the primary
    // block is too large. With PAL 44100 Hz: playPeriodSamples ≈ 882; a 512    // frame block fits ~0.58 plays; a 4096-frame block fits ~4.6 plays (fine);
    // a 65536-frame block (DAW offline) fits ~74 plays (requires chunking).
    static constexpr uint32_t kC64PsidMaxPlayCallsPerAudioBlock = 8u;
    static constexpr uint32_t kC64PsidMaxInstructionsPerPlay = 16384u; // v574: raised from 2048; v577: raised from 4096 — complex PSID tunes (Rob Hubbard, Jeroen Tel) need up to 8000+ instr/play; 4096 caused budget exhaustion → CPU left mid-play → continuation during passive → contaminated machine state → wrong notes
    static constexpr uint32_t kC64PsidLoaderInitMaxInstructions = 65536u;
    static constexpr uint32_t kC64PsidCiaLoaderInitMaxInstructions = 262144u;
    static constexpr uint32_t kC64RsidLoaderInitMaxInstructions = 262144u;
    static constexpr uint32_t kC64RsidMaxInstructionsPerAudioBlock = 0u;
    static constexpr uint32_t kC64HeavyTelemetryHz = 60u;
    static constexpr uint64_t kC64PsidMaxPassiveCatchupCyclesPerPlay = 8192ull;
    // 8 PAL frames covers the largest common DAW block size (4096 samples at 44.1 kHz ≈ 91 k cycles).
    static constexpr uint64_t kC64PsidMaxPassiveDebtCycles = ArpSID::C64::C64TimingMath::palVicFrameCycles() * 8ull;
    // C64 mirror is GUI telemetry only, not audible authority. Cap per-block
    // replay so a slow/missed block cannot cause a burst spike on the audio thread.
    static constexpr uint64_t kC64RealtimeMaxCyclesPerAudioBlock = 768ull;
    static constexpr uint64_t kC64RealtimeMaxCycleDebt = 768ull;
    struct C64RenderWrite final {
        int sample = 0;
        uint16_t cycle = 0;
        uint16_t cyclesInSample = 0;
        uint32_t ordinal = 0;
        uint8_t reg = 0;
        uint8_t value = 0;
        uint8_t chip = 0;
    };
    ArpSID::C64::C64SidBridgeState c64SidBridge_{};
    ArpSID::C64::C64SidBridgeState::Snapshot c64SidBridgeTransactionSnapshot_{};
    std::array<C64RenderWrite, ArpSID::C64::C64SidBridgeState::kMaxTimedWrites>
        c64RenderWriteScratch_{};
    std::array<C64RenderWrite, ArpSID::C64::C64SidBridgeState::kMaxTimedWrites>
        c64RenderWriteRadixScratch_{};
    ArpSID::C64::C64TelemetryGate c64TelemetryGate_{};
    std::atomic<ArpSID::C64::C64Runtime*> c64PsidIncoming_{nullptr};
    std::atomic<ArpSID::C64::C64Runtime*> c64PsidLive_{nullptr};
    // v873: default the two CPU-saving SIDPLAY modes ON so realtime playback is
    // smooth out of the box (user reported stutters/dropouts on all tunes with the
    // old full-cost defaults). Both keep CPU/CIA/SID bit-exact: 6510-fast skips only
    // the per-cycle GUI diagnostics snapshot (audio-neutral, ~6% CPU), and VIC-fast
    // skips per-cycle sprite/badline DMA + bus-steal (the main CPU saving) so only the
    // VIC's sub-cycle bus-steal timing goes approximate — inaudible for SID music,
    // relevant only to raster-exact visual demos. Both remain live-reversible from the
    // GUI (VIC:FAST⇄VIC:ACC, 6510:FAST⇄6510:ACC) for cycle-exact needs.
    std::atomic<bool> c64VicFast_{true};        // v838: VIC-II fast toggle (v873: default on)
    std::atomic<bool> c64CpuFast_{true};        // v839: 6510 fast toggle (v873: default on, audio-neutral)
    // v840: render-path stall instrumentation. The render thread publishes maxes;
    // the GUI thread reads them in _poll. These exist to localize the C64P
    // "play-stop-play-stop" choppiness — measurement showed the emulation runs at
    // ~12x realtime, so the cause is a periodic render-path stall, not compute.
    // Perspectives captured: render-side spike (block µs / overrun), host-side
    // jitter (gap between deliveries), and debt-burst (catch-up cycles / leftover).
    std::atomic<uint32_t> c64CallbackLastBlockMicros_{0};   // full C64 processBlock callback wall µs
    std::atomic<uint32_t> c64CallbackMaxBlockMicros_{0};    // peak full C64 callback wall µs
    std::atomic<uint32_t> c64CallbackOverrunCount_{0};      // full callback µs exceeded the audio deadline
    std::atomic<uint32_t> c64PreC64LastBlockMicros_{0};     // work before the C64 player branch
    std::atomic<uint32_t> c64PreC64MaxBlockMicros_{0};      // peak pre-C64 branch work
    std::atomic<uint32_t> c64RenderLastBlockMicros_{0};     // last C64 render-block wall µs
    std::atomic<uint32_t> c64RenderMaxBlockMicros_{0};      // peak C64 render-block wall µs
    std::atomic<uint32_t> c64RenderOverrunCount_{0};        // blocks whose C64 render µs exceeded the audio deadline
    std::atomic<uint32_t> c64PostC64LastBlockMicros_{0};    // post-C64 audio/finalize work
    std::atomic<uint32_t> c64PostC64MaxBlockMicros_{0};     // peak post-C64 audio/finalize work
    std::atomic<uint32_t> c64TelemetryLastBlockMicros_{0};  // C64 light telemetry publication
    std::atomic<uint32_t> c64TelemetryMaxBlockMicros_{0};   // peak C64 light telemetry publication
    std::atomic<uint32_t> c64MaxHostGapMicros_{0};          // peak wall gap between consecutive C64 blocks (host jitter)
    std::atomic<uint64_t> c64MaxCatchupCycles_{0};          // aggregate peak PHI2 catch-up
    std::atomic<uint64_t> c64MaxContinuousCatchupCycles_{0};
    std::atomic<uint64_t> c64MaxCiaCatchupCycles_{0};
    std::atomic<uint64_t> c64MaxVbiCatchupCycles_{0};
    std::atomic<uint64_t> c64LastPassiveDebtCycles_{0};     // passive-cycle debt left after the last block (~0 if healthy)
    std::chrono::steady_clock::time_point c64LastRenderWall_{};  // render-thread-only
    bool c64LastRenderWallValid_ = false;                       // render-thread-only
    static void atomicMaxU32_(std::atomic<uint32_t>& a, uint32_t v) noexcept {
        uint32_t cur = a.load(std::memory_order_relaxed);
        while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
    static void atomicMaxU64_(std::atomic<uint64_t>& a, uint64_t v) noexcept {
        uint64_t cur = a.load(std::memory_order_relaxed);
        while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
    enum class C64CatchupPath_ : uint8_t {
        Continuous,
        Cia,
        Vbi,
    };
    void noteC64Catchup_(C64CatchupPath_ path, uint64_t catchupCycles) noexcept {
        atomicMaxU64_(c64MaxCatchupCycles_, catchupCycles);
        switch (path) {
            case C64CatchupPath_::Continuous:
                atomicMaxU64_(c64MaxContinuousCatchupCycles_, catchupCycles);
                break;
            case C64CatchupPath_::Cia:
                atomicMaxU64_(c64MaxCiaCatchupCycles_, catchupCycles);
                break;
            case C64CatchupPath_::Vbi:
                atomicMaxU64_(c64MaxVbiCatchupCycles_, catchupCycles);
                break;
        }
    }
    static uint32_t elapsedMicros_(std::chrono::steady_clock::time_point t0,
                                   std::chrono::steady_clock::time_point t1) noexcept {
        const long long us =
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        return static_cast<uint32_t>(std::max<long long>(0, std::min<long long>(us, UINT32_MAX)));
    }
    void noteC64PhaseMicros_(std::chrono::steady_clock::time_point t0,
                             std::chrono::steady_clock::time_point t1,
                             std::atomic<uint32_t>& last,
                             std::atomic<uint32_t>& max) noexcept {
        const uint32_t u = elapsedMicros_(t0, t1);
        last.store(u, std::memory_order_relaxed);
        atomicMaxU32_(max, u);
    }
    void resetC64RenderInstrumentation_() noexcept {
        c64CallbackLastBlockMicros_.store(0u, std::memory_order_relaxed);
        c64CallbackMaxBlockMicros_.store(0u, std::memory_order_relaxed);
        c64CallbackOverrunCount_.store(0u, std::memory_order_relaxed);
        c64PreC64LastBlockMicros_.store(0u, std::memory_order_relaxed);
        c64PreC64MaxBlockMicros_.store(0u, std::memory_order_relaxed);
        c64RenderLastBlockMicros_.store(0u, std::memory_order_relaxed);
        c64RenderMaxBlockMicros_.store(0u, std::memory_order_relaxed);
        c64RenderOverrunCount_.store(0u, std::memory_order_relaxed);
        c64PostC64LastBlockMicros_.store(0u, std::memory_order_relaxed);
        c64PostC64MaxBlockMicros_.store(0u, std::memory_order_relaxed);
        c64TelemetryLastBlockMicros_.store(0u, std::memory_order_relaxed);
        c64TelemetryMaxBlockMicros_.store(0u, std::memory_order_relaxed);
        c64MaxHostGapMicros_.store(0u, std::memory_order_relaxed);
        c64MaxCatchupCycles_.store(0u, std::memory_order_relaxed);
        c64MaxContinuousCatchupCycles_.store(0u, std::memory_order_relaxed);
        c64MaxCiaCatchupCycles_.store(0u, std::memory_order_relaxed);
        c64MaxVbiCatchupCycles_.store(0u, std::memory_order_relaxed);
        c64LastPassiveDebtCycles_.store(0u, std::memory_order_relaxed);
        c64LastRenderWallValid_ = false;
    }
    // v872 P1-5: clear the C64 run-failure / dropped-play diagnostic counters on a
    // tune handoff so they read "since load". resetC64RenderInstrumentation_() only
    // clears timing maxes; these failure/drop counters were session-cumulative, so
    // after Tune A hit a rollback, budget or play-cap event the GUI kept showing
    // Tune A's totals against freshly loaded Tune B — making a healthy tune look
    // broken and hiding whether the *current* tune is actually dropping frames.
    void resetC64FailureCountersForHandoff_() noexcept {
        c64RsidRunBudgetHitCount_.store(0u, std::memory_order_relaxed);
        c64RsidCpuJamCount_.store(0u, std::memory_order_relaxed);
        c64RsidCiaIncompleteCount_.store(0u, std::memory_order_relaxed);
        c64CiaSidWritesDuringIncompleteCount_.store(0u, std::memory_order_relaxed);
        c64RsidPlayRollbackCount_.store(0u, std::memory_order_relaxed);
        c64RenderTransactionRollbackFailureCount_.store(0u, std::memory_order_relaxed);
        c64RenderContaminationRecoveryCount_.store(0u, std::memory_order_relaxed);
        c64RenderContaminated_.store(0u, std::memory_order_relaxed);
        c64PlayBaseOverflowCount_.store(0u, std::memory_order_relaxed);
        c64NegativePlayBaseDeltaCount_.store(0u, std::memory_order_relaxed);
        c64FutureWriteClampCount_.store(0u, std::memory_order_relaxed);
        c64TimedWriteOverflowTotal_.store(0u, std::memory_order_relaxed);
        c64TimedWriteOverflowLastBlock_.store(0u, std::memory_order_relaxed);
        c64MaxTimedWritesUsed_.store(0u, std::memory_order_relaxed);
        c64PlayCallCapHitCount_.store(0u, std::memory_order_relaxed);
        c64PlayCallsDroppedByCapTotal_.store(0u, std::memory_order_relaxed);
        c64PlayCallsDroppedByCapLastBlock_.store(0u, std::memory_order_relaxed);
        c64PlayCallsDroppedByCapMaxBlock_.store(0u, std::memory_order_relaxed);
        c64ContinuousBudgetHitCount_.store(0u, std::memory_order_relaxed);
        c64ContinuousCpuJamCount_.store(0u, std::memory_order_relaxed);
        c64ContinuousUnsupportedOpcodeCount_.store(0u, std::memory_order_relaxed);
        c64ContinuousIncompleteRunCount_.store(0u, std::memory_order_relaxed);
        c64CiaLatchClampCount_.store(0u, std::memory_order_relaxed);
        c64LastClampedCiaLatch_.store(0u, std::memory_order_relaxed);
    }
    void resetC64SampleCursorsForHandoff_() noexcept {
        c64PsidPassiveClock_.reset();
        c64PsidPassiveCycleDebt_ = 0;
        c64BypassedPerformanceStateCleared_ = false;
        c64PsidPlaySampleCountdown_ = 0.0;
        c64AdaptivePlayBudget_ = kC64PsidMaxInstructionsPerPlay;  // v873 audit P0-1: decay per-tune
        c64PsidAudioHostSampleCursor_ = 0;
        c64TelemetryHostSampleCursor_ = 0;
        c64NextHeavyTelemetrySample_ = 0;
        c64LastHeavyTelemetryBlock_ = UINT64_MAX;
        c64LastHeavyTelemetryPhi2_ = UINT64_MAX;
        c64LastHeavyTelemetryAudioPhaseSample_ = 0;
        c64LastHeavyTelemetrySidHash_ = 0u;
        c64LastHeavyTelemetryPsidActive_ = 0u;
        c64HeavyTelemetryPeriodSamplesLast_ = 0;
        c64TelemetrySkippedSnapshotCount_ = 0;
        c64TelemetryForcedSnapshotCount_ = 0;
        c64TelemetryCatchupClampCount_ = 0;
        c64HeavyTelemetryDemandGated_ = false;
        sidCoreSampleCursor_ = 0u;
        sidCoreBlockSampleBase_ = 0u;
        sidCoreBlockIndex_ = 0u;
    }
    void noteC64CallbackTiming_(std::chrono::steady_clock::time_point t0,
                                std::chrono::steady_clock::time_point t1,
                                int numFrames) noexcept {
        const uint32_t u = elapsedMicros_(t0, t1);
        c64CallbackLastBlockMicros_.store(u, std::memory_order_relaxed);
        atomicMaxU32_(c64CallbackMaxBlockMicros_, u);
        const double deadlineUs =
            static_cast<double>(numFrames) * 1.0e6 / std::max(1.0, sampleRate_);
        if (deadlineUs > 0.0 && static_cast<double>(u) > deadlineUs)
            c64CallbackOverrunCount_.fetch_add(1u, std::memory_order_relaxed);
        if (c64LastRenderWallValid_) {
            const long long gap =
                std::chrono::duration_cast<std::chrono::microseconds>(t0 - c64LastRenderWall_).count();
            if (gap > 0)
                atomicMaxU32_(c64MaxHostGapMicros_,
                              static_cast<uint32_t>(std::min<long long>(gap, UINT32_MAX)));
        }
        c64LastRenderWall_ = t0;
        c64LastRenderWallValid_ = true;
        c64LastPassiveDebtCycles_.store(c64PsidPassiveCycleDebt_, std::memory_order_relaxed);
    }
    void noteC64RenderTiming_(std::chrono::steady_clock::time_point t0,
                              std::chrono::steady_clock::time_point t1,
                              int numFrames) noexcept {
        const uint32_t u = elapsedMicros_(t0, t1);
        c64RenderLastBlockMicros_.store(u, std::memory_order_relaxed);
        atomicMaxU32_(c64RenderMaxBlockMicros_, u);
        const double deadlineUs =
            static_cast<double>(numFrames) * 1.0e6 / std::max(1.0, sampleRate_);
        if (deadlineUs > 0.0 && static_cast<double>(u) > deadlineUs)
            c64RenderOverrunCount_.fetch_add(1u, std::memory_order_relaxed);
        c64LastPassiveDebtCycles_.store(c64PsidPassiveCycleDebt_, std::memory_order_relaxed);
    }
    // audit P1.10: bounded retired-player ring. The render thread never deletes a
    // C64Runtime; it parks the outgoing player in a free slot here and the non-RT
    // collectRetiredPsidPlayer_ drains and frees ALL slots. The previous single
    // retired slot leaked the previous occupant on a rapid second handoff. With a
    // ring drained on every load/unload/eject, overflow is effectively impossible;
    // if it ever happens we flag it (retireOverflowCount_) and leak as a last
    // resort rather than calling free() on the audio thread.
    static constexpr int kRetiredPsidSlots = 8;
    std::atomic<ArpSID::C64::C64Runtime*> c64PsidRetired_[kRetiredPsidSlots]{};
    std::atomic<uint32_t> retireDropCount_{0};       // non-zero = retired-ring overflow (leak) diagnostic
    std::atomic<uint32_t> retireOverflowCount_{0};   // distinct fatal-overflow counter for release tooling
#if defined(__APPLE__)
    mach_timebase_info_data_t hostTimebase_{1, 1}; // pre-warmed in constructor; never initialized on render thread
#endif
    std::atomic<uint8_t> c64PsidUnloadRequested_{0};
    std::atomic<uint8_t> c64PsidHardResetRequested_{0};
    std::atomic<uint16_t> c64PsidCurrentSubtune_{0};
    std::atomic<uint64_t> c64PsidLoadGeneration_{0};
    // Audit #65 — every PSID load that fell back to the UI's video-standard
    // setting (file's vs bits were 0 or 3) increments this counter. Lets
    // host tooling observe how often users may be hearing wrong-tempo /
    // wrong-pitch playback because of an ambiguous file header.
    std::atomic<uint64_t> c64PsidVideoStandardFallbackCount_{0};
    // audit P0.2 — render-published mirror of the active variant's video
    // standard (0 = PAL/unknown -> usePal, 1 = NTSC). loadPsidDataForRequest()
    // can run on an async UI/loader thread; reading render-owned
    // runtimeModel_.variantProfile() from there is a data race. The render
    // thread publishes this scalar each block (updateTelemetry_) and the loader
    // reads the atomic instead of touching runtimeModel_.
    std::atomic<uint8_t>  c64VideoStandardFallbackAtomic_{0};
    // Audit #66 — every render-side PSID handoff (the pointer-swap +
    // bridge-attach inside drainPendingPsidHandoff_) increments this
    // counter. Diagnostic only; the swap itself is O(1) work.
    std::atomic<uint64_t> c64PsidRenderHandoffCount_{0};
    // RSID strict/physical telemetry status for the legacy diagnostic field:
    // 0=no active strict-PHI2 RSID path, 1=known-downgrade-free physical claim,
    // 2=strict-PHI2 path active with known downgrades.
    std::atomic<uint8_t>  c64RsidStrictStatusCode_{0}; // explicit strict status code; legacy snapshot alias mirrors this
    std::atomic<uint8_t>  c64RsidPlaybackModeCode_{0};
    std::atomic<uint32_t> c64RsidExactnessDowngradeMask_{0};
    // audit P1.11: render-published physical-exactness blocker mask (distinct from
    // the observed-downgrade ledger above) so the GUI can honestly show what blocks
    // true C64 cycle-physical execution.
    std::atomic<uint32_t> c64RsidPhysicalBlockerMask_{0};
    std::atomic<uint32_t> c64RsidLegacyInitFallbackCount_{0};
    // Per-render approximate/unsupported opcode totals — updated each render block
    // alongside c64RsidStrictStatusCode_ so GUI can read without dereferencing
    // the live C64Runtime pointer.
    std::atomic<uint64_t> c64RsidApproximateOpcodeTotal_{0};
    std::atomic<uint64_t> c64RsidUnsupportedOpcodeTotal_{0};
    std::atomic<uint64_t> c64InitBrkSentinelCount_{0};
    std::atomic<uint64_t> c64SidReadApproximationCount_{0};
    std::atomic<uint64_t> c64SidOpenBusReadCount_{0};
    std::atomic<uint64_t> c64InvalidSidChipReadCount_{0};
    std::atomic<uint64_t> c64InvalidSidChipWriteCount_{0};
    std::atomic<uint64_t> c64SidHoleWriteCount_{0};
    std::atomic<uint64_t> c64RmwSidWriteCount_{0};
    // Fix #3: Separate run-failure diagnostic counters.
    // Each counter corresponds to a specific failure mode so host diagnostics
    // can distinguish "budget exhausted" from "CPU jammed" from "CIA incomplete."
    std::atomic<uint64_t> c64RsidRunBudgetHitCount_{0};    ///< instruction budget exhausted in runPlay
    std::atomic<uint64_t> c64RsidCpuJamCount_{0};          ///< KIL/JAM opcode hit during play
    std::atomic<uint64_t> c64RsidCiaIncompleteCount_{0};   ///< CIA play started but IRQ not ack'd
    std::atomic<uint64_t> c64CiaSidWritesDuringIncompleteCount_{0}; ///< v873: incomplete CIA service that still wrote SID (audible-risk)
    std::atomic<uint64_t> c64RsidPlayRollbackCount_{0};    ///< bridge transaction rolled back
    std::atomic<uint64_t> c64RenderTransactionRollbackFailureCount_{0}; ///< bounded platform journal could not prove full rollback
    // v872 P1-4: latched contamination flag. Set when a render-transaction rollback
    // could not prove a full platform-mirror restore (journal overflow) AND the
    // in-place resync recovery ran. Latched until the next tune handoff so the GUI
    // can surface "C64 RENDER TX ROLLBACK RECOVERED" and downgrade the strict/clean
    // status. 0 = clean, 1 = a rollback-failure recovery occurred since load.
    std::atomic<uint8_t>  c64RenderContaminated_{0};
    std::atomic<uint64_t> c64RenderContaminationRecoveryCount_{0}; ///< resync recoveries performed
    std::atomic<uint64_t> c64PlayBaseOverflowCount_{0};    ///< play calls exceeded kC64PsidMaxPlayCallsPerAudioBlock
    std::atomic<uint64_t> c64PlayCallCapHitCount_{0};      ///< play-call loop reached kC64PsidMaxPlayCallsPerAudioBlock
    std::atomic<uint64_t> c64PlayCallsDroppedByCapTotal_{0};
    std::atomic<uint32_t> c64PlayCallsDroppedByCapLastBlock_{0};
    std::atomic<uint32_t> c64PlayCallsDroppedByCapMaxBlock_{0};
    // v872 P2-1: continuous-machine (RSID / playAddress==0 free-running) diagnostics.
    // The discrete VBI/CIA paths already expose budget/jam counters; the continuous
    // path silently committed partial runs. RSID partial commit is expected (there is
    // no discrete play() frame to roll back), but it must be *visible* so choppy
    // free-running playback can be attributed instead of looking like a clean run.
    std::atomic<uint64_t> c64ContinuousBudgetHitCount_{0};       ///< instruction budget reached mid-block
    std::atomic<uint64_t> c64ContinuousCpuJamCount_{0};          ///< KIL/JAM during continuous run
    std::atomic<uint64_t> c64ContinuousUnsupportedOpcodeCount_{0}; ///< unsupported opcode during continuous run
    std::atomic<uint64_t> c64ContinuousIncompleteRunCount_{0};   ///< did not consume the requested cycle budget (partial commit)
    // v873 P1-7: CIA-timed scheduler cadence-downgrade visibility. A live CIA1
    // Timer A latch below the [1000, 65535] scheduling floor cannot drive the
    // per-block service cadence (it would schedule a per-sample service storm), so
    // the scheduler keeps the default 50/60 Hz cadence. That fallback was silent;
    // surfacing it lets a fast-CIA / high-rate-digi tune playing at the wrong
    // cadence be attributed instead of looking clean.
    std::atomic<uint64_t> c64CiaLatchClampCount_{0};     ///< blocks where a live CIA latch < 1000 was clamped to the default cadence
    std::atomic<uint32_t> c64LastClampedCiaLatch_{0};    ///< last sub-1000 raw latch value observed
    // Render-mapping anomaly diagnostics (audit items 15/16).
    std::atomic<uint64_t> c64NegativePlayBaseDeltaCount_{0};   // writes before PlayBase
    // v873 (timing audit P1-3): SID writes whose PHI2 cycle lands beyond the current
    // audio block get clamped to the last sample, producing a block-edge burst. This
    // counts them so mapping jitter / oversized catch-up windows are attributable
    // (a climbing value means catch-up/service windows exceed the block span).
    std::atomic<uint64_t> c64FutureWriteClampCount_{0};        // writes clamped to block end
    std::atomic<uint64_t> c64TimedWriteOverflowTotal_{0};      // dropped timed writes (cumulative)
    std::atomic<uint32_t> c64TimedWriteOverflowLastBlock_{0};  // dropped in most recent block
    std::atomic<uint32_t> c64MaxTimedWritesUsed_{0};           // high-water mark of timed writes
    uint32_t c64BlockPlayCalls_ = 0;
    // Deferred state restore — RT-safe ownership mailbox (audit P0-1 fix).
    //
    // The previous design was a version-counter seqlock whose RT reader *swapped*
    // shared double-buffer slots. That made the reader a writer of shared memory:
    // if a non-RT writer "lapped" a stalled render reader (two publishes while the
    // reader was preempted between selecting a slot and swapping it), the writer's
    // slot assignment and the reader's swap touched the SAME SidStateRootV1 object
    // concurrently — a torn-buffer / ABA data race. The post-swap version re-read
    // only detected the lap AFTER the racing access had already occurred (UB).
    //
    // Replaced with ArpSID::OwnershipMailbox<SidStateRootV1>: a three-buffer
    // single-producer / single-consumer "latest value wins" mailbox that transfers
    // buffer OWNERSHIP through one atomic word. The producer and consumer never
    // access the same buffer at once, so a producer lapping a stalled consumer can
    // only ping-pong between its own buffer and the parked one — it can never touch
    // the buffer the render thread currently owns. See sid_ownership_mailbox.h.
    //
    // RT-safety: the consumer side (tryConsume) is wait-free, allocation-free, and
    // performs no shared-buffer writes. The producer side may allocate (it copies a
    // heap-owning SidStateRootV1) and is serialized below by
    // pendingStateRestoreWriterMutex_ so there is exactly one producer at a time.
    ArpSID::OwnershipMailbox<SidStateRootV1> stateRootMailbox_{};
    ArpSID::AuditedStateRestoreWriterMutex pendingStateRestoreWriterMutex_; // non-RT writer serialization only; never taken by render

    // Pre-allocated replay-entries array replaces the 256 KB stack VLA in
    // applyStateRootCanonical (16*128*8 entries × 16 bytes ≈ 262 KB). Allocated at
    // kernel construction; never resized.
    static constexpr int kMaxReplayEntries_ =
        16 * 128 * static_cast<int>(ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes);
    std::unique_ptr<ArpSID::SidHeldReplayEntry[]> replayBuf_{
        std::make_unique<ArpSID::SidHeldReplayEntry[]>(static_cast<size_t>(kMaxReplayEntries_))};

    static constexpr float kLFODivisions_[4] = { 4.0f, 2.0f, 1.0f, 0.5f };

    // seqLastNoteId_ removed — was used only by the deleted appendSequencerCanonicalEvents_.
    // Sequencer note-id management is now owned by seqEngine_ (SequencerEngine).

    uint8_t defaultEventChannel_ = 0u; // host-local only: default ingress channel; 0 = CH1 (P1-3 fix: was kSidUnresolvedChannel which silenced virtual gate on fresh instances)
    int32_t prevVirtualNoteId_   = -1;                    // host-local only: virtual-key bridge note id
    int32_t virtualNoteIdCounter_ = 1;                    // host-local only: virtual-key id generator

    float  lastNoteVelocity_   = 0.5f;                    // host-local only: UI/virtual-note velocity mirror

    std::atomic<float>   telemetryPeakL_  {0.f};
    std::atomic<float>   telemetryPeakR_  {0.f};
    std::atomic<float>   telemetryRmsL_   {0.f};
    std::atomic<float>   telemetryRmsR_   {0.f};
    float telemetryPeakDecayPerSample_ = 0.99984337f; // 60 dB/sec at 44.1 kHz; refreshed on SR changes.
    std::atomic<int>     telemetryVoices_ {0};
    std::atomic<int>     telemetryArpStep_{0};
    std::atomic<int>     telemetryLastNote_{-1};

    // Telemetry invariant: the UI thread must not read live mutable runtime
    // model fields. Render publishes a POD/atomic snapshot here; readTelemetry()
    // only consumes this snapshot.
    static constexpr std::array<int, 19> kTelemetryParamIds_ = {
        kParamArpEnable,
        kParamSeqEnable,
        kParamVoiceMode,
        kParamDrSidVolume,
        kParamDrSidMachineModel,
        kParamDrSidAccentAmount,
        kParamDrSidOutputDrive,
        kParamDrSidHatMetal,
        kParamDrSidClapSpread,
        kParamDrSidKickTune,
        kParamDrSidKickDecay,
        kParamDrSidSnareTone,
        kParamDrSidSnareSnap,
        kParamDrSidHatTune,
        kParamDrSidHatDecay,
        kParamDrSidCowbellTune,
        kParamDrSidCowbellDecay,
        kParamDrSidTomTune,
        kParamDrSidTomDecay,
    };
    std::array<std::atomic<float>, kNumParams> telemetryParamSnapshot_{};
    std::atomic<double>  telemetryHostTempo_{120.0};
    std::atomic<double>  telemetryHostBeat_{0.0};
    std::atomic<uint8_t> telemetryHostPlaying_{0};
    std::atomic<uint64_t> telemetryHostSampleStart_{0u};
    std::atomic<uint64_t> telemetryHostSampleEnd_{0u};
    std::atomic<uint8_t> telemetryRenderMode_{0};
    std::atomic<uint8_t> telemetrySynthMode_{0};
    std::atomic<uint8_t> telemetryDrSidMode_{0};
    std::atomic<uint8_t> telemetryPsidActive_{0};
    std::atomic<uint8_t> telemetryDrSidPlaybackMode_{0};
    std::atomic<uint8_t> telemetryArpEnabled_{0};
    std::atomic<uint8_t> telemetrySeqEnabled_{0};
    std::atomic<uint64_t> telemetryFrameId_{0u};
    std::atomic<uint8_t> telemetryArpFollowHost_{0};
    std::atomic<uint8_t> telemetrySeqFollowHost_{0};
    std::atomic<int>     telemetrySeqStep_{0};
    std::atomic<float>   telemetrySeqTempoBpm_{120.0f};
    std::atomic<int>     telemetrySidModel_{0};
    std::atomic<float>   telemetryEnv1Level_{0.0f};
    std::atomic<float>   telemetryLastNoteVelocity_{0.0f};
    std::atomic<float>   telemetryModWheelNorm_{0.0f};
    std::atomic<float>   telemetryFocusedPitchBend_{0.0f};
    std::atomic<float>   telemetryFocusedChannelPressure_{0.0f};
    std::atomic<float>   telemetryFocusedPolyPressure_{0.0f};
    std::atomic<float>   telemetryRandomValue_{0.0f};
    std::atomic<float>   telemetryForensicActivity_{0.0f};
    std::atomic<float>   telemetryForensicIntensity_{0.0f};
    std::atomic<uint8_t> telemetryForensicEnabled_{0};
    std::atomic<float>   telemetryForensicClockJitter_{0.0f};
    std::atomic<float>   telemetryForensicSupplyRipple_{0.0f};
    std::atomic<float>   telemetryForensicThermalDrift_{0.0f};
    std::atomic<float>   telemetryForensicVoiceCrosstalk_{0.0f};
    std::atomic<float>   telemetryForensicExternalBleed_{0.0f};
    std::atomic<float>   telemetryForensicFilterOhmic_{0.0f};
    std::atomic<float>   telemetryForensicSystemNoise_{0.0f};
    std::atomic<float>   telemetryForensicD418Asymmetry_{0.0f};
    std::atomic<float>   telemetryForensicEnvelopeTDM_{0.0f};
    std::atomic<float>   telemetryForensicMotherboard_{0.0f};
    std::atomic<float>   telemetryForensicADCBleed_{0.0f};
    std::atomic<float>   telemetryForensicBusCollision_{0.0f};
    std::atomic<float>   telemetryForensicPotInput_{0.0f};
    std::atomic<uint8_t> telemetryForensicDigifix8580_{0};
    // render-published HI-FI audible-authority telemetry. These values
    // are written only after the actual post-SID chain has processed the audio
    // buffer, so the GUI can prove whether HI-FI is really affecting sound.
    std::atomic<uint8_t> telemetryHiFiEnabled_{0};
    std::atomic<uint8_t> telemetryHiFiQuality_{0};
    std::atomic<uint8_t> telemetryHiFiOversampling_{1};
    std::atomic<float>   telemetryHiFiDryPeak_{0.0f};
    std::atomic<float>   telemetryHiFiWetPeak_{0.0f};
    std::atomic<float>   telemetryHiFiDeltaPeak_{0.0f};
    std::atomic<float>   telemetryHiFiMonoCorrelation_{1.0f};
    std::atomic<float>   telemetryHiFiSafetyGain_{1.0f};

    // Render-owned deterministic C64 platform mirror. These atomics are the
    // only GUI-visible copy; readTelemetry() must not synthesize artificial C64
    // state from host beat once the mirror is integrated.
    std::atomic<uint8_t>  telemetryC64PlatformEnabled_{0};
    std::atomic<uint8_t>  telemetryC64MirrorEnabled_{0};
    std::atomic<uint8_t>  telemetryC64PsidRuntimeActive_{0};
    std::atomic<uint8_t>  telemetryC64ProjectionOnly_{0};
    std::atomic<uint8_t>  telemetryC64Pal_{1};
    // Fix #8: PAL/NTSC and SID-model provenance — distinguishes file-derived
    // from UI-fallback values so the GUI can show "PAL (from file)" vs "PAL (UI fallback)".
    std::atomic<uint8_t>  telemetryC64VideoFromFile_{0};   ///< 1 if PAL/NTSC came from header
    std::atomic<uint8_t>  telemetryC64SidModelFromFile_{0}; ///< 1 if SID model came from header
    std::atomic<uint8_t>  telemetryC64SidModel_{0};         ///< 0=6581, 1=8580, 2=both/unknown
    std::atomic<uint8_t>  telemetryC64PsidLastParseResult_{0};
    std::atomic<uint8_t>  telemetryC64PsidLastLoadFailure_{0};
    std::atomic<uint8_t>  telemetryC64RealtimeRunning_{0};
    // Fidelity ratio: cycles actually executed / cycles due this block.
    // Values near 1.0 = full fidelity. Values below 0.95 indicate the C64
    // mirror is operating in degraded/lossy mode (GUI telemetry only).
    std::atomic<float>    telemetryC64MirrorFidelity_{1.0f};
    // v875 audit closure: render-thread gate telling the applied-write observer whether
    // to mirror THIS block's projected SID writes into the C64 telemetry bus. It is set
    // at render-block start from cockpit demand, not from the previous block's audio
    // activity, so first-note gate/frequency/ADSR bursts are captured. The cosmetic C64
    // CPU advance remains separately gated; if that advance is skipped, queued mirror
    // writes are discarded at block end instead of replaying stale later.
    std::atomic<uint8_t>  c64MirrorObserverActive_{0};
    uint32_t c64ProjectionMirrorQueuedWritesThisBlock_ = 0u;
    std::atomic<uint8_t>  telemetryC64Booted_{0};
    std::atomic<uint64_t> telemetryC64Phi2Cycle_{0};
    std::atomic<uint64_t> telemetryC64BlockIndex_{0};
    std::atomic<uint32_t> telemetryC64PlayCalls_{0};
    std::atomic<uint64_t> telemetryC64PlayCallCapHits_{0};
    std::atomic<uint64_t> telemetryC64PlayCallsDroppedByCapTotal_{0};
    std::atomic<uint32_t> telemetryC64PlayCallsDroppedByCapLastBlock_{0};
    std::atomic<uint32_t> telemetryC64PlayCallsDroppedByCapMaxBlock_{0};
    std::atomic<uint64_t> telemetryC64RenderTransactionRollbackFailureCount_{0};
    std::atomic<uint8_t>  telemetryC64RenderContaminated_{0};             // v872 P1-4: latched rollback-failure recovery
    std::atomic<uint64_t> telemetryC64RenderContaminationRecoveryCount_{0};
    std::atomic<uint64_t> telemetryC64ContinuousBudgetHitCount_{0};       // v872 P2-1: continuous-run health
    std::atomic<uint64_t> telemetryC64ContinuousCpuJamCount_{0};
    std::atomic<uint64_t> telemetryC64ContinuousUnsupportedOpcodeCount_{0};
    std::atomic<uint64_t> telemetryC64ContinuousIncompleteRunCount_{0};
    std::atomic<float>    telemetryC64PlayRateHz_{0.0f};
    std::atomic<uint16_t> telemetryC64CpuPc_{0};
    std::atomic<uint8_t>  telemetryC64CpuA_{0};
    std::atomic<uint8_t>  telemetryC64CpuX_{0};
    std::atomic<uint8_t>  telemetryC64CpuY_{0};
    std::atomic<uint8_t>  telemetryC64CpuSp_{0};
    std::atomic<uint8_t>  telemetryC64CpuStatus_{0};
    std::atomic<uint8_t>  telemetryC64CpuJammed_{0};
    std::atomic<uint8_t>  telemetryC64IrqLine_{0};
    std::atomic<uint8_t>  telemetryC64NmiLine_{0};
    std::atomic<uint8_t>  telemetryC64TrapBrkAsJam_{0};
    std::atomic<uint8_t>  telemetryC64ProcessorPort_{0xFF};
    std::atomic<uint16_t> telemetryC64VicRaster_{0};
    std::atomic<uint8_t>  telemetryC64VicCycle_{0};
    std::atomic<uint8_t>  telemetryC64VicBadline_{0};
    std::atomic<uint8_t>  telemetryC64VicBa_{1};
    std::atomic<uint8_t>  telemetryC64VicAec_{1};
    std::atomic<uint8_t>  telemetryC64VicSpriteDma_{0};
    std::atomic<uint8_t>  telemetryC64VicHalfCycle_{0};
    std::atomic<uint64_t> telemetryC64VicFrame_{0};
    std::atomic<uint32_t> telemetryC64VicTotalStolen_{0};
    std::atomic<uint8_t>  telemetryC64OpenBus_{0};
    std::atomic<uint8_t>  telemetryC64OpenBusDecayMask_{0xFFu};
    std::atomic<uint8_t>  telemetryC64OpenBusDrivenWithinPersistence_{1u};
    std::atomic<uint64_t> telemetryC64OpenBusAgePhi2_{0};
    std::atomic<uint64_t> telemetryC64OpenBusLastDrivenPhi2_{0};
    std::atomic<uint32_t> telemetryC64SidOpenBusReadCount_{0};
    std::atomic<uint32_t> telemetryC64ColorRamOpenBusReadCount_{0};
    std::atomic<uint32_t> telemetryC64PotxyOpenBusReadCount_{0};
    std::atomic<uint8_t>  telemetryC64LastRead_{0};
    std::atomic<uint8_t>  telemetryC64LastSidReg_{0};
    std::atomic<uint8_t>  telemetryC64LastSidValue_{0};
    std::atomic<uint64_t> telemetryC64LastSidWriteCycle_{0};
    static constexpr int kC64BusScopeLen = 128;
    struct C64BusScopeSnapshot {
        float openBus[kC64BusScopeLen]{};
        float sidBus[kC64BusScopeLen]{};
        float sidReg[kC64BusScopeLen]{};
        float sidValue[kC64BusScopeLen]{};
        float sidWritePulse[kC64BusScopeLen]{};
        float phi2[kC64BusScopeLen]{};
        float irqDma[kC64BusScopeLen]{};
        float chip[kC64BusScopeLen]{};
        uint32_t writePos = 0u;
        uint64_t frameId = 0u;
    };
    C64BusScopeSnapshot c64BusScopeLive_{};
    ArpSID::ScopeTripleBuffer<C64BusScopeSnapshot> telemetryC64BusScopeTriple_{};
    std::atomic<uint8_t>  telemetryC64Cia1Irq_{0};
    std::atomic<uint8_t>  telemetryC64Cia2Irq_{0};
    std::atomic<uint8_t>  telemetryC64Cia1IrqLine_{0};
    std::atomic<uint8_t>  telemetryC64Cia2IrqLine_{0};
    std::atomic<uint8_t>  telemetryC64IecAtn_{1};
    std::atomic<uint8_t>  telemetryC64IecClk_{1};
    std::atomic<uint8_t>  telemetryC64IecData_{1};
    std::atomic<uint8_t>  telemetryC64IecSrq_{1};
    std::atomic<uint8_t>  telemetryC64TapeMotor_{0};
    std::atomic<uint8_t>  telemetryC64TapeSense_{1};
    std::atomic<uint8_t>  telemetryC64TapeWrite_{0};
    std::atomic<uint8_t>  telemetryC64TapeRead_{1};
    std::atomic<uint64_t> telemetryC64TapePulseCount_{0};

    std::atomic<int>     telemetryActiveTokenCount_{0};
    std::atomic<uint32_t> telemetryGeneration_{0};
    std::array<std::atomic<float>, 4> telemetryLfoValue_{};
    std::array<std::atomic<float>, 4> telemetryLfoPhase_{};
    // Preset-authority invariant: visible patch identity is not read from
    // kParamBankSlot/kParamProgram mirrors. AU sticky preset metadata publishes the
    // display slot here; telemetry/readback consumes this independent authority.
    std::atomic<int>     telemetryPresetSlot_{0};
    struct TelemetryTokenAtomics {
        std::atomic<uint64_t> token{0};
        std::atomic<int> note{0};
        std::atomic<int> channel{0};
        std::atomic<float> velocity{0.0f};
        std::atomic<float> polyPressure{0.0f};
        std::atomic<uint8_t> sustained{0};
        std::atomic<uint8_t> sostenuto{0};
        std::atomic<uint8_t> focused{0};
    };
    std::array<TelemetryTokenAtomics, 8> telemetryTokens_{};

    std::array<std::atomic<uint8_t>, 30> telemetrySidRegs_{};
    std::array<std::atomic<float>, 3> telemetryVoiceEnvLevel_{};
    std::array<std::atomic<float>, DrSidEngine::kDrumTypeCount> telemetryDrumLevel_{};
    std::array<std::atomic<float>, 3> telemetryDrumVoiceLevel_{};
    std::array<std::atomic<float>, DrSidEngine::kGMDrumNoteCount> telemetryGMDrumNoteLevel_{};
    std::atomic<float> telemetrySid808OutputPeak_{0.0f};
    std::atomic<float> telemetrySid808RawPeakBeforeDc_{0.0f};
    std::atomic<float> telemetrySid808RawMeanBeforeDc_{0.0f};
    std::atomic<float> telemetrySid808PostDcPeak_{0.0f};
    std::atomic<float> telemetrySid808PostDcMean_{0.0f};
    std::atomic<float> telemetrySid808DcBlockerR_{0.0f};
    std::atomic<std::uint64_t> telemetrySid808DcBlockerResetCount_{0u};
    std::atomic<uint8_t> telemetrySid808BridgeReplacedOutput_{0};
    std::atomic<uint8_t> telemetryDigiActiveSlots_{0};
    std::atomic<uint8_t> telemetryDigiConfiguredFactorySlots_{0};
    std::atomic<uint8_t> telemetryDigiConfiguredUserImportSlots_{0};
    std::atomic<uint8_t> telemetryDigiPlayingVoices_{0};
    std::atomic<uint8_t> telemetryDigiPeakVoices_{0};
    std::atomic<uint8_t> telemetryDigiStepIndex_{0};
    std::atomic<uint8_t> telemetryDigiLastSlot_{255};
    std::atomic<uint16_t> telemetryDigiLastFactorySlot_{0};
    std::atomic<uint32_t> telemetryDigiTriggerCount_{0};
    std::atomic<uint32_t> telemetryDigiMidiTriggerCount_{0};
    std::atomic<uint32_t> telemetryDigiMidiIgnoredCount_{0};
    std::atomic<uint8_t>  telemetryDigiLastMidiNote_{255};
    std::atomic<uint8_t>  telemetryDigiLastMidiChannel_{255};
    std::atomic<uint32_t> telemetryDigiUnavailableUserImports_{0};
    std::atomic<float> telemetryDigiOutputPeak_{0.0f};
    struct DigiScopeSnapshot {
        float scope[DigiSamplerEngine::kScopeLen]{};
        uint32_t writePos = 0u;
        uint64_t frameId = 0u;
    };
    ArpSID::ScopeTripleBuffer<DigiScopeSnapshot> telemetryDigiScopeTriple_{};
    // ── Authentic DIGI D418 telemetry (render-thread written) ─────────────
    // These counters mirror the DigiD418StreamEngine::telemetry() values and
    // are refreshed once per render block. They record the total number of
    // $D418 writes, writes in the current block, writes blocked by the IO
    // bank, queue overflow, collision events, open bus drive count and the
    // last nibble, last D418 value and last open bus value observed.
    std::atomic<uint32_t> telemetryDigiD418WriteCount_{0};
    std::atomic<uint32_t> telemetryDigiD418WritesThisBlock_{0};
    std::atomic<uint32_t> telemetryDigiD418SidAcceptedWriteCount_{0};
    std::atomic<uint32_t> telemetryDigiD418SidAcceptedWritesThisBlock_{0};
    std::atomic<uint32_t> telemetryDigiD418WritesBlockedByIo_{0};
    std::atomic<uint32_t> telemetryDigiD418WriteQueueOverflow_{0};
    std::atomic<uint32_t> telemetryDigiD418CollisionCount_{0};
    std::atomic<uint32_t> telemetryDigiD418OpenBusDriveCount_{0};
    std::atomic<uint32_t> telemetryDigiD418TimelineDiscontinuityResetCount_{0};
    std::atomic<uint32_t> telemetryDigiD418ForensicWritePos_{0};
    std::atomic<uint32_t> telemetryDigiD418LastHostFrame_{0};
    std::atomic<uint32_t> telemetryDigiD418LastPhi2Low_{0};
    std::atomic<uint8_t>  telemetryDigiAuthMode_{0};
    std::atomic<uint8_t>  telemetryDigiD418LastNibble_{0};
    std::atomic<uint8_t>  telemetryDigiD418LastOldD418_{0};
    std::atomic<uint8_t>  telemetryDigiD418LastD418_{0};
    std::atomic<uint8_t>  telemetryDigiD418LastOpenBus_{0xFF};
    std::atomic<uint8_t>  telemetryDigiD418LastIoVisible_{1};
    std::atomic<uint8_t>  telemetryDigiD418LastSidAccepted_{0};
    // GUI audition pads are written from the main thread and consumed
    // at render-block boundary. One bit per DIGI slot; velocity is latched per
    // slot. No GUI thread ever mutates DigiSamplerEngine/DigiD418StreamEngine
    // state directly.
    std::atomic<uint32_t> digiGuiPadTriggerMask_{0};
    std::array<std::atomic<uint8_t>, ArpSID::GUI::kDigiActiveSlotCount> digiGuiPadTriggerVelocity_{};
    // GUI audition pad observability. These counters are separate
    // from external MIDI counters so the UI can distinguish a non-destructive
    // local pad click from an incoming MIDI pad note.
    std::atomic<uint32_t> telemetryDigiGuiPadAcceptedCount_{0};
    std::atomic<uint32_t> telemetryDigiGuiPadIgnoredCount_{0};
    std::atomic<uint8_t>  telemetryDigiGuiPadLastSlot_{255};
    std::atomic<uint8_t>  telemetryDigiGuiPadLastVelocity_{0};
    std::atomic<uint8_t>  telemetryDigiGuiPadLastAccepted_{0};
    std::array<std::atomic<float>, 8> telemetryMpkKnobValue_{};
    std::atomic<int> telemetryLastMappedCC_{-1};
    std::atomic<float> telemetryLastMappedCCValue_{0.0f};
    std::atomic<int> telemetryLastDrumNote_{-1};
    std::atomic<int> telemetryLastDrumClass_{255};
    std::atomic<float> telemetryLastDrumVelocity_{0.0f};
    std::atomic<uint32_t> telemetryPresentationScopeSerial_{0};
    std::atomic<uint32_t> telemetryC64SnapshotDemandSerial_{0};
    static constexpr double kTelemetryPresentationScopeHoldSeconds = 0.125;
    static constexpr double kTelemetryC64SnapshotHoldSeconds = 0.250;
    uint32_t renderPresentationScopeSerial_ = 0u;
    uint32_t renderPresentationScopeFramesRemaining_ = 0u;
    uint32_t renderC64SnapshotDemandSerial_ = 0u;
    uint32_t renderC64SnapshotFramesRemaining_ = 0u;
    bool renderC64SnapshotDemandThisBlock_ = false;

    struct TelemetryScopeSnapshot {
        float voiceScope[8][256]{};
        float oscScope[3][256]{};
        float filterScope[2][256]{};
        uint8_t activeMask = 0;
        uint32_t writePos = 0;
        uint64_t frameId = 0u;
    };
    ArpSID::ScopeTripleBuffer<TelemetryScopeSnapshot> telemetryScopeTriple_{};

    struct MainOscScopeSnapshot {
        float mono[512]{};
        uint32_t writePos = 0u;
        uint64_t frameId = 0u;
    };
    float                mainOscLive_[512]{};
    uint32_t             mainOscLiveWritePos_ = 0u;
    ArpSID::ScopeTripleBuffer<MainOscScopeSnapshot> mainOscScopeTriple_{};
    float                monoScratchR_[kMaxFramesPerBlock]{};
    EventBuffer          chunkEventScratch_{};        // heap/object-owned chunk scratch; never stack-allocate 4096 TimedEvents in render
    // v910 parent-scope chunk normalization: async ring events drained once at
    // parent-block scope (correct hostTime→offset against the FULL block),
    // then sliced per chunk together with host events[].
    EventBuffer          parentAsyncEventScratch_{};
    // v911 parent-chunk ingress guard: once oversized-block async rings have
    // been normalized at parent scope, recursive chunk processBlock() calls
    // must not drain freshly-arrived raw rings with chunk-local timing. Events
    // arriving during the chunk loop belong to the next host block.
    bool                 parentChunkAsyncDrainActive_ = false;
    float                sliceScratchL_[kMaxFramesPerBlock]{};
    float                sliceScratchR_[kMaxFramesPerBlock]{};
    // processScratchL_/R_ removed — were used only by the deleted
    // ArpSIDKernelPrepareCanonicalRender / ArpSIDKernelFinalizeRender.
    // Pre-allocated scratch buffers — avoids large stack allocations on the audio thread.
    // audit P0.3: explicit backing storage — the default ctor is now non-allocating.
    ArpSID::SidTimedEventQueue canonicalQueueScratch_{ArpSID::SidTimedEventQueue::AllocateStorage{}};
    EventBuffer                seqEvtsScratch_{};      // ~655 KB: sequencer events per block
    // ingressScratch_ removed — was used only by the deleted appendQueuedIngressEventsToBuffer_.
    // MIDI ingress is now handled exclusively through the midiQueue_ SPSC ring buffer.
    EventBuffer                canonicalEventsScratch_{};  // ~655 KB: canonical event conversion
    float                runtimeFractionalAccumL_[kMaxFramesPerBlock]{};
    float                runtimeFractionalAccumR_[kMaxFramesPerBlock]{};
    uint32_t             runtimeFractionalWeight_[kMaxFramesPerBlock]{};
    uint8_t              runtimeFractionalActive_[kMaxFramesPerBlock]{};  // 0=inactive, 1=active; uint8_t avoids reinterpret_cast aliasing

    struct GuiRealtimeModelSnapshot_ {
        ArpSID::GUI::MixPanelModel mix;
        ArpSID::GUI::KitStateBlob kit;
        ArpSID::GUI::DigiPanelModel digi;
    };

    static void resetGuiRealtimeModelSnapshot_(GuiRealtimeModelSnapshot_& s) noexcept {
        s.mix = ArpSID::GUI::makeDefaultMixModel();
        s.kit = ArpSID::GUI::makeDefaultKitStateBlob();
        s.digi = ArpSID::GUI::makeDefaultDigiPanelModel();
    }

    // A2: DrumEngineHostBridge — references canonical engineBank_.drSid and
    // owns only the distinct SID808 engine/router state. Component flavor is
    // the sole routing authority; there is no second mutable enable flag.
    ArpSID::DrumEngineHostBridge drumEngineBridge_;
    struct Sid808PreparedBridgeSlot_ {
        int slot = -1;
        ArpSID::Sid808KitConfigTable kit{};
    };
    ArpSID::OwnershipMailbox<Sid808PreparedBridgeSlot_> sid808PreparedBridgeSlotMailbox_{};
    // v803/P0/v861: render-drained SID-808 restores cannot call the non-RT
    // bridge loader. Non-RT schedule resolves the kit table into this mailbox;
    // render activates the matching prepared table, with queue/drain retained
    // only as a fail-safe for direct/unprepared render applies.
    std::atomic<int> sid808PreloadedFactorySlotForRestore_v803_{-1};
    // SETTINGS audio topology is published from GUI/main as an atomic request
    // and consumed only by the render thread at a block boundary.
    std::atomic<std::uint8_t> requestedAudioEngineMode_{0u};
    std::uint8_t audioEngineModeRender_ = 0xFFu;
    // A8: Compiled drum kit sequencer (render-owned, atomically swapped on kit change).
    ArpSID::GUI::CompiledKitSequencer compiledKitSequencer_{
        ArpSID::GUI::makeDefaultCompiledKitSequencer()};

    ArpSID::OwnershipMailbox<GuiRealtimeModelSnapshot_> guiRealtimeMailbox_{};
    ArpSID::OwnershipMailbox<ArpSID::GUI::DigiSampleBankBlob> guiRealtimeDigiSampleBankMailbox_{};
    GuiRealtimeModelSnapshot_ guiRealtimeRender_{};
    ArpSID::GUI::DigiSampleBankBlob guiRealtimeDigiSampleBankRender_{};
    ArpSID::GUI::GuiRealtimeProjection guiRealtimeProjectionRender_{};
    uint32_t guiRealtimeRenderVersion_ = 0u;
    DigiSamplerEngine digiSampler_{};

    // ── Authentic DIGI D418 state ───────────────────────────────────────────
    // When the kernel resolves DigiAuthMode::StandaloneD418Layer or DigiAuthMode::FastStandaloneD418Layer,
    // this engine schedules PHI2‑synchronous writes to $D418 instead of
    // mixing float audio. The OpenBusLatch mirrors the last driven bus
    // value when driveOpenBus=true. digiPhi2Counter_ tracks the absolute
    // PHI2 cycle number for write scheduling across render blocks. The
    // Legacy float sampler remains allocated only for explicit debug builds
    // compiled with ARPSID_ENABLE_LEGACY_FLOAT_DIGI. Release mode is limited
    // to AUTH C64-bus D418 and FAST private D418.
    DigiD418StreamEngine digiD418_{};
    ArpSID::C64::OpenBusLatch digiOpenBus_{};
    // Private scratch bridge used only by the standalone/auth DIGI renderer.
    // Never route DIGI $D418 writes through c64SidBridge_: that bridge is the
    // PSID/RSID authority and may have engine/mirror sinks installed.
    ArpSID::C64::C64SidBridgeState digiD418Bridge_{};
    std::uint64_t digiPhi2Counter_ = 0u;
    double digiPhi2Remainder_ = 0.0;
    SidRegisterEngine digiD418Sid_{};
    float digiD418ScratchL_[kMaxFramesPerBlock]{};
    float digiD418ScratchR_[kMaxFramesPerBlock]{};
    // GUI/UX controlled authentic DIGI runtime policy. These are
    // non-allocating atomics read by render; GUI writes through adapter methods.
    std::atomic<std::uint8_t>  digiAuthModeControl_{0u};      // release-safe: 0=auth bus D418, 1=fast private SID; legacy float is debug-only and not MIDI-selectable
    std::atomic<std::uint32_t> digiD418RateHzControl_{8000u}; // sanitized to 1000..32000
    // MIDI pad mapping for DIGI. Defaults preserve the historical C4..G4 map
    // on all channels, but CC/GUI can retarget the eight pads without touching
    // the audio/D418 policy. Channel filter is 0..15 or 16=OMNI.
    std::atomic<std::uint8_t>  digiMidiRootNoteControl_{ArpSID::DigiD418StreamEngine::kDefaultMidiRootNote};
    std::atomic<std::uint8_t>  digiMidiChannelFilterControl_{16u};
    // Incremented by the GUI/adapter whenever mode or rate changes. The render
    // thread consumes it at block boundaries and performs the actual engine
    // reset there, avoiding GUI-thread mutation of DSP state while also
    // preventing stale AUTH/FAST/debug-legacy voices from crossing policy changes.
    std::atomic<std::uint32_t> digiD418RuntimePolicyGeneration_{1u};
    std::uint32_t digiD418RuntimePolicyGenerationRender_{0u};

    // ── v550: SIDCORE panel model — render-thread write path ─────────────────
    // Atomic pointer so the GUI thread can wire/unwire the model at any time.
    // The render thread loads with acquire each block; no lock required.
    std::atomic<ArpSID::GUI::SidCorePanelModel*> sidCorePanelModel_{nullptr};
    // Monotonic SIDCORE timeline state (render thread only). v580 replaced the
    // old hard-coded 512-sample block stride with a real sample cursor so write
    // stamps stay ordered across hosts that render 64, 512, 1024, or 4096 frames.
    uint64_t sidCoreBlockIndex_ = 0u;
    uint64_t sidCoreSampleCursor_ = 0u;
    uint64_t sidCoreBlockSampleBase_ = 0u;

public:
    // Audit #65 + #66 diagnostic accessors — diagnostic tooling can call
    // these from host-side debug paths to surface PSID-load anomalies
    // without changing the live render path.
    uint64_t c64PsidVideoStandardFallbackCount() const noexcept {
        return c64PsidVideoStandardFallbackCount_.load(std::memory_order_acquire);
    }
    uint64_t c64PsidRenderHandoffCount() const noexcept {
        return c64PsidRenderHandoffCount_.load(std::memory_order_acquire);
    }
    void resetC64PsidDiagnosticCounters() noexcept {
        c64PsidVideoStandardFallbackCount_.store(0u, std::memory_order_release);
        c64PsidRenderHandoffCount_.store(0u, std::memory_order_release);
    }

    void setDigiD418RuntimePolicy(std::uint8_t mode, std::uint32_t rateHz) noexcept {
#if defined(ARPSID_ENABLE_LEGACY_FLOAT_DIGI) && ARPSID_ENABLE_LEGACY_FLOAT_DIGI
        const std::uint8_t safeMode = (mode <= 2u) ? mode : 0u;
#else
        const std::uint8_t safeMode = (mode <= 1u) ? mode : 0u;
#endif
        const std::uint32_t safeRate = std::clamp<std::uint32_t>(rateHz, 1000u, 32000u);
        const std::uint8_t oldMode = digiAuthModeControl_.exchange(safeMode, std::memory_order_acq_rel);
        const std::uint32_t oldRate = digiD418RateHzControl_.exchange(safeRate, std::memory_order_acq_rel);
        if (oldMode != safeMode || oldRate != safeRate) {
            digiD418RuntimePolicyGeneration_.fetch_add(1u, std::memory_order_acq_rel);
        }
    }

    std::uint8_t digiD418RuntimeMode() const noexcept {
        const std::uint8_t mode = digiAuthModeControl_.load(std::memory_order_acquire);
#if defined(ARPSID_ENABLE_LEGACY_FLOAT_DIGI) && ARPSID_ENABLE_LEGACY_FLOAT_DIGI
        return (mode <= 2u) ? mode : 0u;
#else
        return (mode <= 1u) ? mode : 0u;
#endif
    }

    std::uint32_t digiD418RuntimeRateHz() const noexcept {
        return std::clamp<std::uint32_t>(digiD418RateHzControl_.load(std::memory_order_acquire), 1000u, 32000u);
    }

    void setDigiMidiPadMapping(std::uint8_t rootNote, std::uint8_t channelFilter) noexcept {
        const std::uint8_t safeRoot = static_cast<std::uint8_t>(std::min<std::uint8_t>(rootNote & 0x7Fu, 120u));
        const std::uint8_t safeChan = (channelFilter <= 15u) ? channelFilter : 16u;
        digiMidiRootNoteControl_.store(safeRoot, std::memory_order_release);
        digiMidiChannelFilterControl_.store(safeChan, std::memory_order_release);
    }

    std::uint8_t digiMidiRootNote() const noexcept {
        return static_cast<std::uint8_t>(std::min<std::uint8_t>(digiMidiRootNoteControl_.load(std::memory_order_acquire) & 0x7Fu, 120u));
    }

    std::uint8_t digiMidiChannelFilter() const noexcept {
        const std::uint8_t ch = digiMidiChannelFilterControl_.load(std::memory_order_acquire);
        return (ch <= 15u) ? ch : 16u;
    }

    void clearDigiD418RuntimeTelemetryForGui() noexcept {
        clearDigiD418TelemetryAtomics_(digiD418RuntimeMode());
    }

    void triggerDigiPadForGui(std::uint8_t slot, std::uint8_t velocity) noexcept {
        if (slot >= ArpSID::GUI::kDigiActiveSlotCount) return;
        const std::uint8_t vel = static_cast<std::uint8_t>(std::clamp<int>(velocity, 1, 127));
        digiGuiPadTriggerVelocity_[static_cast<std::size_t>(slot)].store(vel, std::memory_order_release);
        const std::uint32_t bit = (1u << slot);
        digiGuiPadTriggerMask_.fetch_or(bit, std::memory_order_acq_rel);
    }

    void publishGuiRealtimeModels(const ArpSID::GUI::MixPanelModel* mix,
                                  const ArpSID::GUI::KitStateBlob* kit,
                                  const ArpSID::GUI::DigiPanelModel* digi,
                                  const ArpSID::GUI::DigiSampleBankBlob* digiSamples = nullptr) noexcept {
        // Logic's AUHostingService calls this from a small XPC worker stack.
        // Fill the heap-owned mailbox producer slot directly: a local snapshot
        // contains the 480 KB DIGI sample bank and can hit the stack guard.
        GuiRealtimeModelSnapshot_& slot = guiRealtimeMailbox_.producerSlot();
        resetGuiRealtimeModelSnapshot_(slot);
        if (mix) {
            slot.mix = *mix;
            ArpSID::GUI::sanitizeMixModel(slot.mix);
        }
        if (kit) {
            slot.kit = *kit;
            ArpSID::GUI::kitStateBlobSanitize(slot.kit);
        }
        if (digi) {
            slot.digi = *digi;
            ArpSID::GUI::sanitizeDigiPanelModel(slot.digi);
        }
        if (digiSamples) {
            ArpSID::GUI::DigiSampleBankBlob& bankSlot =
                guiRealtimeDigiSampleBankMailbox_.producerSlot();
            bankSlot = *digiSamples;
            ArpSID::GUI::sanitizeDigiSampleBankBlob(bankSlot);
            guiRealtimeDigiSampleBankMailbox_.publish();
        }

        guiRealtimeMailbox_.publish();
    }

    // ── v549: Collect all kernel-accessible diagnostic counters into a snapshot.
    // Non-realtime only (called from GUI thread). Reads atomics with
    // memory_order_acquire; does not modify kernel state.
    // The caller is responsible for overlaying AUv2/AUv3 host-layer counters
    // (renderEpoch, notifyCallbackViolation, etc.) on the same snapshot    // those live outside the kernel in ArpSIDAUv2Instance / ArpSIDAudioUnit.
    void collectDiagnosticCounters(ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot& out) const noexcept {
        out.c64PsidVideoStandardFallbackCount =
            c64PsidVideoStandardFallbackCount_.load(std::memory_order_acquire);
        out.c64PsidRenderHandoffCount =
            c64PsidRenderHandoffCount_.load(std::memory_order_acquire);
        // BitPerfectEngine: zero-cycle + output-stage noise (audit #33/#36)
        if (const BitPerfectEngine* bpe = bpe_()) {
            out.zeroCycleSampleCount       = bpe->totalZeroCycleSampleCount();
            out.outputStageNoiseSampleCount = bpe->outputStageNoiseSampleCount();
        }
        // SidRegisterEngine: filter abs-clamp hit (audit #38)
        out.filterAbsClampHitCount = sreg_().filterAbsClampHitCount();
        // DrSidEngine: invalid clock frequency reject (audit #45)
        if (const DrSidEngine* drs = drs_()) {
            out.invalidClockFrequencyRejectCount = drs->invalidClockFrequencyRejectCount();
        }
        // SidRuntimeModel: pending events drain dropped + ingress fallback overflow
        out.pendingEventsDrainDroppedCount   = runtimeModel_.pendingEventsDrainDroppedCount();
        out.ingressFallbackEdgeOverflowCount = runtimeModel_.ingressFallbackEdgeOverflowCount();
        // Reserved stateRestoreOverlay counters intentionally report zero.
        // RSID exactness counters (v605): read render-updated atomics — never
        // dereference c64PsidLive_ from the GUI thread (the pointer can be
        // swapped out concurrently by the render thread).
        {
            const uint8_t strictStatus = c64RsidStrictStatusCode_.load(std::memory_order_acquire);
            const uint8_t modeCode = c64RsidPlaybackModeCode_.load(std::memory_order_acquire);
            const uint32_t downgradeMask = c64RsidExactnessDowngradeMask_.load(std::memory_order_acquire);
            const uint64_t approxTotal = c64RsidApproximateOpcodeTotal_.load(std::memory_order_acquire);
            const uint64_t unsupTotal  = c64RsidUnsupportedOpcodeTotal_.load(std::memory_order_acquire);
            // The render thread stores the already-normalized strict status:
            // 0=no active strict-PHI2 RSID path, 1=known-downgrade-free,
            // 2=strict-PHI2 active but downgraded. Do not reclassify it from
            // opcode counters alone: HLE ROMs, CIA/VIC approximations, SID-read
            // approximations and open-bus reads are ledger downgrades too.
            const uint64_t normalizedStrictStatus = strictStatus <= 2u ? strictStatus : 0u;
            out.rsidStrictStatusCode = normalizedStrictStatus;
            out.rsidPlaybackModeCode = (modeCode == 1u || modeCode == 2u) ? modeCode : 0u;
            out.rsidExactnessDowngradeMask = downgradeMask;
            out.rsidPhysicalBlockerMask = c64RsidPhysicalBlockerMask_.load(std::memory_order_acquire);
            out.rsidExactPlaybackActive = normalizedStrictStatus;
            out.phi2ApproximateOpcodeTotal = approxTotal;
            out.phi2UnsupportedOpcodeTotal = unsupTotal;
            out.initBrkSentinelCount =
                c64InitBrkSentinelCount_.load(std::memory_order_acquire);
            out.sidReadApproximationCount =
                c64SidReadApproximationCount_.load(std::memory_order_acquire);
            out.sidOpenBusReadCount =
                c64SidOpenBusReadCount_.load(std::memory_order_acquire);
            out.invalidSidChipReadCount =
                c64InvalidSidChipReadCount_.load(std::memory_order_acquire);
            out.invalidSidChipWriteCount =
                c64InvalidSidChipWriteCount_.load(std::memory_order_acquire);
            out.sidHoleWriteCount =
                c64SidHoleWriteCount_.load(std::memory_order_acquire);
            out.rmwSidWriteCount =
                c64RmwSidWriteCount_.load(std::memory_order_acquire);
        }
        // v840/v841: C64P render-path stall instrumentation.
        out.c64CallbackLastBlockMicros    = c64CallbackLastBlockMicros_.load(std::memory_order_relaxed);
        out.c64CallbackMaxBlockMicros     = c64CallbackMaxBlockMicros_.load(std::memory_order_relaxed);
        out.c64CallbackOverrunCount       = c64CallbackOverrunCount_.load(std::memory_order_relaxed);
        out.c64PreC64LastBlockMicros      = c64PreC64LastBlockMicros_.load(std::memory_order_relaxed);
        out.c64PreC64MaxBlockMicros       = c64PreC64MaxBlockMicros_.load(std::memory_order_relaxed);
        out.c64RenderLastBlockMicros      = c64RenderLastBlockMicros_.load(std::memory_order_relaxed);
        out.c64RenderMaxBlockMicros       = c64RenderMaxBlockMicros_.load(std::memory_order_relaxed);
        out.c64RenderOverrunCount         = c64RenderOverrunCount_.load(std::memory_order_relaxed);
        out.c64PostC64LastBlockMicros     = c64PostC64LastBlockMicros_.load(std::memory_order_relaxed);
        out.c64PostC64MaxBlockMicros      = c64PostC64MaxBlockMicros_.load(std::memory_order_relaxed);
        out.c64TelemetryLastBlockMicros   = c64TelemetryLastBlockMicros_.load(std::memory_order_relaxed);
        out.c64TelemetryMaxBlockMicros    = c64TelemetryMaxBlockMicros_.load(std::memory_order_relaxed);
        out.c64MaxHostGapMicros           = c64MaxHostGapMicros_.load(std::memory_order_relaxed);
        out.c64MaxCatchupCycles           = c64MaxCatchupCycles_.load(std::memory_order_relaxed);
        out.c64MaxContinuousCatchupCycles = c64MaxContinuousCatchupCycles_.load(std::memory_order_relaxed);
        out.c64MaxCiaCatchupCycles        = c64MaxCiaCatchupCycles_.load(std::memory_order_relaxed);
        out.c64MaxVbiCatchupCycles        = c64MaxVbiCatchupCycles_.load(std::memory_order_relaxed);
        out.c64LastPassiveDebtCycles      = c64LastPassiveDebtCycles_.load(std::memory_order_relaxed);
    }

    // ── v550: Wire or unwire the SIDCORE GUI model. ──────────────────────────
    // Called from the GUI/main thread (via the adapter) when the SIDCORE panel
    // is shown or the AU is torn down. Atomic release so the render thread sees
    // the new pointer at the next processBlock boundary.
    void setSidCorePanelModel(ArpSID::GUI::SidCorePanelModel* m) noexcept {
        sidCorePanelModel_.store(m, std::memory_order_release);
    }

    void requestAudioEngineMode(std::uint8_t mode) noexcept {
        requestedAudioEngineMode_.store(mode == 1u ? 1u : 0u, std::memory_order_release);
    }

    std::uint8_t renderedAudioEngineMode() const noexcept {
        return audioEngineModeRender_ <= 1u ? audioEngineModeRender_ : 0u;
    }

    ArpSIDDSPKernel() {
        resetGuiRealtimeModelSnapshot_(guiRealtimeRender_);
        ArpSID::GUI::resetDigiSampleBankBlob(guiRealtimeDigiSampleBankRender_);
        ArpSID::prewarmAllSidTables();
        // Load the embedded verified stock C64 ROMs into the realtime SID-core
        // platform once at construction. C64Platform::reset() preserves external
        // ROMs (resetDeterministicIfNoExternalRoms), so this survives every later
        // reset and makes the direct C64-bus path run ROM-backed/physically exact.
        ArpSID::C64::c64LoadEmbeddedStockRoms(c64Platform_);
#if defined(__APPLE__)
        // Prewarm mach_timebase on construction (non-RT). This prevents a
        // function-local static from being initialized on the render thread,
        // which would acquire a once-lock and violate RT safety.
        mach_timebase_info(&hostTimebase_);
#endif
        runtimeModel_.prepareRealtimeParameterStorage();
        runtimeExecutionOwner_ = std::make_unique<ArpSID::SidRuntimeExecutionOwner<ArpSIDDSPKernel>>(*this);
        for (int i = 0; i < kNumParams; ++i) {
            const float def = ArpSID::defaultNormalizedParamValue(i);
            params_[i].store(def, std::memory_order_relaxed);
            hostParamRetentionOverlay_[i].store(def, std::memory_order_relaxed);
            renderParams_[i] = def;
            telemetryParamSnapshot_[i].store(def, std::memory_order_relaxed);
            dirty_[i].store(true, std::memory_order_release);
            paramIntentGeneration_[i].store(0u, std::memory_order_relaxed);
            paramIntentFallbackPacked_[i].store(0u, std::memory_order_relaxed);
            paramIntentAppliedFallbackGeneration_[i].store(0u, std::memory_order_relaxed);
        }
    }

    ~ArpSIDDSPKernel() {
        delete c64PsidIncoming_.exchange(nullptr, std::memory_order_acq_rel);
        delete c64PsidLive_.exchange(nullptr, std::memory_order_acq_rel);
        // audit P1.10: free every retired-ring slot, not just one.
        collectRetiredPsidPlayer_();
    }
    ArpSIDDSPKernel(const ArpSIDDSPKernel&) = delete;
    ArpSIDDSPKernel& operator=(const ArpSIDDSPKernel&) = delete;

    uint64_t beginPsidLoadRequest() noexcept {
        // Monotonic ticket used by async UI loaders. If the user unloads or
        // starts another load while a background PSID parse is in flight, the
        // old worker must not be allowed to publish a stale incoming player.
        return c64PsidLoadGeneration_.fetch_add(1u, std::memory_order_acq_rel) + 1u;
    }

    bool loadPsidDataForRequest(const void* data, size_t size, uint16_t subtune, uint64_t requestTicket) noexcept {
        collectRetiredPsidPlayer_();
        if (requestTicket == 0u || requestTicket != c64PsidLoadGeneration_.load(std::memory_order_acquire)) return false;
        if (!data || size == 0u) { unloadPsid(); return false; }
        // v873 audit item 4: parse ONCE here and hand the header to the runtime via
        // loadPsidParsed() (no second parse inside loadPsid). An unparseable file is
        // rejected before we allocate a runtime or touch clock/model telemetry, so a
        // bad load can never pollute the video-standard/SID-model fallback counters.
        ArpSID::PsidHeader parsedHeader{};
        const ArpSID::PsidParseResult parseResult = ArpSID::psidParse(
            static_cast<const uint8_t*>(data), static_cast<uint32_t>(size), parsedHeader,
            ArpSID::PsidParsePolicy::SidTuneCompatible);
        telemetryC64PsidLastParseResult_.store(static_cast<uint8_t>(parseResult), std::memory_order_relaxed);
        if (parseResult != ArpSID::PsidParseResult::OK) {
            telemetryC64PsidLastLoadFailure_.store(
                static_cast<uint8_t>(ArpSID::C64::C64Runtime::loadFailureForParseResultPublic(parseResult)),
                std::memory_order_relaxed);
            return false;
        }
        auto* player = new (std::nothrow) ArpSID::C64::C64Runtime();
        if (!player) return false;
        // Auto-detect video standard from PSID/RSID v2+ header flags bits 2-3:
        // 1=PAL only, 2=NTSC only, 0/3=unknown or both (fall back to UI setting).
        // Using the wrong standard causes wrong tempo (20% error) and wrong pitch (3.8%).
        //
        // Audit #65 fix: the legacy fall-back path was silent. When a PSID
        // file has vs=0 (unknown) or vs=3 (both) the user might be hearing
        // wrong-tempo / wrong-pitch playback and not know why. The fix:
        // track every fallback in a diagnostic counter so host tooling can
        // observe how often this happens. The actual fallback strategy is
        // unchanged (UI setting) — refusing to load ambiguous files would
        // break legacy workflows.
        // audit P0.2: loadPsidDataForRequest can run on an async UI/loader
        // thread. Read the render-published atomic mirror instead of the
        // render-owned runtimeModel_.variantProfile(), which would be a data
        // race. usePal is true unless the active variant is explicitly NTSC.
        // v873 audit item 9: derive clock + SID-model provenance from the CORE parser's
        // normalized output (parsedHeader, parsed once above) rather than a second
        // hand-rolled flag decode, so the AU layer can never disagree with psidParse()
        // (video standard / model / version-gated fields).
        bool usePal = c64VideoStandardFallbackAtomic_.load(std::memory_order_acquire) != 1u;
        bool videoStandardFromFile = false;
        if (parsedHeader.clock == ArpSID::PsidClock::PAL)       { usePal = true;  videoStandardFromFile = true; }
        else if (parsedHeader.clock == ArpSID::PsidClock::NTSC) { usePal = false; videoStandardFromFile = true; }
        if (!videoStandardFromFile) {
            c64PsidVideoStandardFallbackCount_.fetch_add(1u, std::memory_order_relaxed);
        }
        const bool fallbackIs6581 =
            params_[(size_t)kParamSidModel].load(std::memory_order_acquire) < 0.5f;
        bool sidModelFromFile = false;
        uint8_t sidModelValue = fallbackIs6581 ? 0u : 1u;
        if (parsedHeader.sidModel[0] == ArpSID::PsidSidModel::MOS6581)      { sidModelValue = 0u; sidModelFromFile = true; }
        else if (parsedHeader.sidModel[0] == ArpSID::PsidSidModel::MOS8580) { sidModelValue = 1u; sidModelFromFile = true; }
        telemetryC64VideoFromFile_.store(videoStandardFromFile ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64SidModelFromFile_.store(sidModelFromFile ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64SidModel_.store(sidModelValue, std::memory_order_relaxed);
        player->reset(usePal);
        // Load the embedded verified stock ROMs into this PSID/RSID player AFTER
        // reset (so reset's deterministic fill does not clobber them) and BEFORE
        // loadPsid/runInit (RSID init executes real KERNAL/BASIC code). This gives
        // the player a complete verified stock ROM set, clearing MissingRealRoms /
        // RomIdentityUnverified and enabling true ROM-backed RSID execution.
        ArpSID::C64::c64LoadEmbeddedStockRoms(player->platform());
        if (!player->loadPsidParsed(parsedHeader)) {
            telemetryC64PsidLastLoadFailure_.store(static_cast<uint8_t>(player->lastLoadFailure()), std::memory_order_relaxed);
            delete player; return false;
        }
        telemetryC64PsidLastLoadFailure_.store(0u, std::memory_order_relaxed);
        // Default AU RSID policy is strict/physical PHI2. Users/tests may choose
        // Compatible explicitly, but the plugin must never label compatible PHI2
        // execution as strict.
        if (player->isRsid()) {
            player->setRsidPlaybackMode(ArpSID::C64::RsidPlaybackMode::Strict);
        }
        if (requestTicket != c64PsidLoadGeneration_.load(std::memory_order_acquire)) { delete player; return false; }
        const uint16_t songCount = std::max<uint16_t>(1u, player->image().header.songs);
        const uint16_t defaultSong = player->image().header.startSong ? player->image().header.startSong : 1u;
        const uint16_t requestedSong = subtune == 0u ? defaultSong : subtune;
        const uint16_t clampedSong = std::min<uint16_t>(std::max<uint16_t>(1u, requestedSong), songCount);
        const uint32_t initBudget = player->isRsid()
            ? kC64RsidLoaderInitMaxInstructions
            : (player->usesCiaTimingForSong(clampedSong) ? kC64PsidCiaLoaderInitMaxInstructions
                                                          : kC64PsidLoaderInitMaxInstructions);
        bool initOk = player->runInit(clampedSong, initBudget);
        if (!initOk && player->isRsid()) {
            delete player;
            player = new (std::nothrow) ArpSID::C64::C64Runtime();
            if (!player) return false;
            player->reset(usePal);
            ArpSID::C64::c64LoadEmbeddedStockRoms(player->platform());
            if (!player->loadPsidParsed(parsedHeader)) {
                telemetryC64PsidLastLoadFailure_.store(static_cast<uint8_t>(player->lastLoadFailure()), std::memory_order_relaxed);
                delete player; return false;
            }
            player->setRsidPlaybackMode(ArpSID::C64::RsidPlaybackMode::Compatible);
            initOk = player->runInit(clampedSong, initBudget);
        }
        if (!initOk) { delete player; return false; }
        // RSID uses the bus-cycle-accurate PHI2 machine when init completed on
        // that path. If RSID init had to fall back to the legacy instruction-
        // atomic core, playback is NOT bus-cycle-exact and the strict status is
        // downgraded. The GUI field stores a compact status code from the full
        // exactness ledger, not just approximate/unsupported opcode counters.
        player->enablePhi2Machine(true);
        publishC64ExactnessDiagnostics_(player);
        if (requestTicket != c64PsidLoadGeneration_.load(std::memory_order_acquire)) { delete player; return false; }
        c64PsidCurrentSubtune_.store(clampedSong, std::memory_order_release);
        delete c64PsidIncoming_.exchange(player, std::memory_order_acq_rel);
        return true;
    }

    bool loadPsidData(const void* data, size_t size, uint16_t subtune = 0) noexcept {
        const uint64_t ticket = beginPsidLoadRequest();
        return loadPsidDataForRequest(data, size, subtune, ticket);
    }

    void unloadPsid() noexcept {
        collectRetiredPsidPlayer_();
        c64PsidLoadGeneration_.fetch_add(1u, std::memory_order_acq_rel);
        delete c64PsidIncoming_.exchange(nullptr, std::memory_order_acq_rel);
        c64PsidCurrentSubtune_.store(0u, std::memory_order_release);
        clearC64ExactnessDiagnostics_();
        c64PsidUnloadRequested_.store(1u, std::memory_order_release);
    }

    void resetC64SidPlayerForEject() noexcept {
        collectRetiredPsidPlayer_();
        c64PsidLoadGeneration_.fetch_add(1u, std::memory_order_acq_rel);
        delete c64PsidIncoming_.exchange(nullptr, std::memory_order_acq_rel);
        c64PsidCurrentSubtune_.store(0u, std::memory_order_release);
        clearC64ExactnessDiagnostics_();
        c64PsidHardResetRequested_.store(1u, std::memory_order_release);
        c64PsidUnloadRequested_.store(1u, std::memory_order_release);
    }

    // v838: live, user-togglable "VIC-II fast" CPU-saving mode for C64 SIDPLAY.
    // v873: Default ON (main CPU-time saving for smooth realtime playback). Set from
    // the GUI button via the adapter; read once per block in
    // renderC64PsidBlockIfActive_ and pushed onto the live player (render thread is the
    // only mutator there). Keeps CPU/CIA/SID bit-exact — only VIC bus-steal sub-cycle
    // timing goes approximate (inaudible for SID music). Does NOT re-enable the
    // policy-locked legacy fast playback path.
    void setC64VicFast(bool on) noexcept { c64VicFast_.store(on, std::memory_order_relaxed); }
    bool c64VicFast() const noexcept { return c64VicFast_.load(std::memory_order_relaxed); }
    // v839: "6510 fast" — CPU/CIA/VIC/SID stay bit-exact; only the per-cycle
    // diagnostics snapshot is skipped (measured ~6% less PHI2 CPU). v873: default on
    // (audio-neutral; per-cycle GUI diagnostics do not belong in the audio thread).
    void setC64CpuFast(bool on) noexcept { c64CpuFast_.store(on, std::memory_order_relaxed); }
    bool c64CpuFast() const noexcept { return c64CpuFast_.load(std::memory_order_relaxed); }
    // v840: render-path stall instrumentation readouts (GUI thread).
    uint32_t c64CallbackLastBlockMicros() const noexcept { return c64CallbackLastBlockMicros_.load(std::memory_order_relaxed); }
    uint32_t c64CallbackMaxBlockMicros() const noexcept { return c64CallbackMaxBlockMicros_.load(std::memory_order_relaxed); }
    uint32_t c64CallbackOverrunCount() const noexcept { return c64CallbackOverrunCount_.load(std::memory_order_relaxed); }
    uint32_t c64PreC64LastBlockMicros() const noexcept { return c64PreC64LastBlockMicros_.load(std::memory_order_relaxed); }
    uint32_t c64PreC64MaxBlockMicros() const noexcept { return c64PreC64MaxBlockMicros_.load(std::memory_order_relaxed); }
    uint32_t c64RenderLastBlockMicros() const noexcept { return c64RenderLastBlockMicros_.load(std::memory_order_relaxed); }
    uint32_t c64RenderMaxBlockMicros() const noexcept { return c64RenderMaxBlockMicros_.load(std::memory_order_relaxed); }
    uint32_t c64RenderOverrunCount() const noexcept { return c64RenderOverrunCount_.load(std::memory_order_relaxed); }
    uint32_t c64PostC64LastBlockMicros() const noexcept { return c64PostC64LastBlockMicros_.load(std::memory_order_relaxed); }
    uint32_t c64PostC64MaxBlockMicros() const noexcept { return c64PostC64MaxBlockMicros_.load(std::memory_order_relaxed); }
    uint32_t c64TelemetryLastBlockMicros() const noexcept { return c64TelemetryLastBlockMicros_.load(std::memory_order_relaxed); }
    uint32_t c64TelemetryMaxBlockMicros() const noexcept { return c64TelemetryMaxBlockMicros_.load(std::memory_order_relaxed); }
    uint32_t c64MaxHostGapMicros() const noexcept { return c64MaxHostGapMicros_.load(std::memory_order_relaxed); }
    uint64_t c64MaxCatchupCycles() const noexcept { return c64MaxCatchupCycles_.load(std::memory_order_relaxed); }
    uint64_t c64MaxContinuousCatchupCycles() const noexcept { return c64MaxContinuousCatchupCycles_.load(std::memory_order_relaxed); }
    uint64_t c64MaxCiaCatchupCycles() const noexcept { return c64MaxCiaCatchupCycles_.load(std::memory_order_relaxed); }
    uint64_t c64MaxVbiCatchupCycles() const noexcept { return c64MaxVbiCatchupCycles_.load(std::memory_order_relaxed); }
    uint64_t c64LastPassiveDebtCycles() const noexcept { return c64LastPassiveDebtCycles_.load(std::memory_order_relaxed); }
    uint64_t c64CiaLatchClampCount() const noexcept { return c64CiaLatchClampCount_.load(std::memory_order_relaxed); }
    uint64_t c64FutureWriteClampCount() const noexcept { return c64FutureWriteClampCount_.load(std::memory_order_relaxed); }
    uint64_t c64CiaSidWritesDuringIncompleteCount() const noexcept { return c64CiaSidWritesDuringIncompleteCount_.load(std::memory_order_relaxed); }
    uint32_t c64AdaptivePlayBudget() const noexcept { return c64AdaptivePlayBudget_; }
    uint32_t c64LastClampedCiaLatch() const noexcept { return c64LastClampedCiaLatch_.load(std::memory_order_relaxed); }

    bool isPsidLoaded() const noexcept {
        return c64PsidLive_.load(std::memory_order_acquire) != nullptr ||
               c64PsidIncoming_.load(std::memory_order_acquire) != nullptr;
    }

    bool readC64Telemetry(ArpSID::C64::C64ChipSnapshot& out) const noexcept {
        if (c64TelemetryGate_.read(out)) return true;
        out = {};
        out.valid = false;
        return false;
    }

    bool hasC64Telemetry() const noexcept { return c64TelemetryGate_.hasSnapshot(); }

private:
    VoiceAllocator* runtimeVoicePolicy_() noexcept { return &engineBank_.voicePolicy; }
    const VoiceAllocator* runtimeVoicePolicy_() const noexcept { return &engineBank_.voicePolicy; }
    ArpSID::SidRuntimeHostSurface& runtimeHostSurface_() noexcept { return engineBank_.hostSurface; }
    const ArpSID::SidRuntimeHostSurface& runtimeHostSurface_() const noexcept { return engineBank_.hostSurface; }
    double currentSidClockHz_() const noexcept {
        return ArpSID::sidVariantClockHz(runtimeModel_.variantProfile());
    }

    void publishC64ExactnessDiagnostics_(const ArpSID::C64::C64Runtime* player) noexcept {
        if (!player) {
            clearC64ExactnessDiagnostics_();
            return;
        }
        c64RsidStrictStatusCode_.store(player->rsidStrictStatusCode(), std::memory_order_relaxed);
        c64RsidPlaybackModeCode_.store(player->rsidPlaybackModeCode(), std::memory_order_relaxed);
        c64RsidExactnessDowngradeMask_.store(player->rsidExactnessDowngradeMask(), std::memory_order_relaxed);
        c64RsidPhysicalBlockerMask_.store(player->physicalExactnessBlockerMask(), std::memory_order_relaxed);
        c64RsidLegacyInitFallbackCount_.store(player->rsidLegacyInitFallbackCount(), std::memory_order_relaxed);
        c64RsidApproximateOpcodeTotal_.store(player->phi2ApproximateOpcodeCount(), std::memory_order_relaxed);
        c64RsidUnsupportedOpcodeTotal_.store(player->phi2UnsupportedOpcodeCount(), std::memory_order_relaxed);
        c64InitBrkSentinelCount_.store(player->initBrkSentinelCount(), std::memory_order_relaxed);
        c64SidReadApproximationCount_.store(player->sidReadApproximationCount(), std::memory_order_relaxed);
        c64SidOpenBusReadCount_.store(player->sidOpenBusReadCount(), std::memory_order_relaxed);
        c64InvalidSidChipReadCount_.store(player->invalidSidChipReadCount(), std::memory_order_relaxed);
        c64InvalidSidChipWriteCount_.store(player->invalidSidChipWriteCount(), std::memory_order_relaxed);
        c64SidHoleWriteCount_.store(player->sidHoleWriteCount(), std::memory_order_relaxed);
        c64RmwSidWriteCount_.store(player->rmwSidWriteCount(), std::memory_order_relaxed);
    }

    void clearC64ExactnessDiagnostics_() noexcept {
        c64RsidStrictStatusCode_.store(0u, std::memory_order_relaxed);
        c64RsidPlaybackModeCode_.store(0u, std::memory_order_relaxed);
        c64RsidExactnessDowngradeMask_.store(0u, std::memory_order_relaxed);
        c64RsidPhysicalBlockerMask_.store(0u, std::memory_order_relaxed);
        c64RsidLegacyInitFallbackCount_.store(0u, std::memory_order_relaxed);
        c64RsidApproximateOpcodeTotal_.store(0u, std::memory_order_relaxed);
        c64RsidUnsupportedOpcodeTotal_.store(0u, std::memory_order_relaxed);
        c64InitBrkSentinelCount_.store(0u, std::memory_order_relaxed);
        c64SidReadApproximationCount_.store(0u, std::memory_order_relaxed);
        c64SidOpenBusReadCount_.store(0u, std::memory_order_relaxed);
        c64InvalidSidChipReadCount_.store(0u, std::memory_order_relaxed);
        c64InvalidSidChipWriteCount_.store(0u, std::memory_order_relaxed);
        c64SidHoleWriteCount_.store(0u, std::memory_order_relaxed);
        c64RmwSidWriteCount_.store(0u, std::memory_order_relaxed);
    }

    void collectRetiredPsidPlayer_() noexcept {
        // audit P1.10: drain and free EVERY retired slot (non-RT thread only).
        for (int i = 0; i < kRetiredPsidSlots; ++i) {
            delete c64PsidRetired_[i].exchange(nullptr, std::memory_order_acq_rel);
        }
    }

    void clearPresentationScopeSnapshot_() noexcept {
        telemetryScopeTriple_.clearWriteSlot();
        telemetryScopeTriple_.publish();
        telemetryDigiScopeTriple_.clearWriteSlot();
        telemetryDigiScopeTriple_.publish();
        std::fill(std::begin(mainOscLive_), std::end(mainOscLive_), 0.0f);
        mainOscLiveWritePos_ = 0u;
        mainOscScopeTriple_.clearWriteSlot();
        mainOscScopeTriple_.publish();
        c64BusScopeLive_ = C64BusScopeSnapshot{};
        telemetryC64BusScopeTriple_.writeSlot() = c64BusScopeLive_;
        telemetryC64BusScopeTriple_.publish();
        renderPresentationScopeFramesRemaining_ = 0u;
        renderPresentationScopeSerial_ = telemetryPresentationScopeSerial_.load(std::memory_order_acquire);
        renderC64SnapshotFramesRemaining_ = 0u;
        renderC64SnapshotDemandSerial_ = telemetryC64SnapshotDemandSerial_.load(std::memory_order_acquire);
        renderC64SnapshotDemandThisBlock_ = false;
        c64MirrorObserverActive_.store(0u, std::memory_order_relaxed);
        c64ProjectionMirrorQueuedWritesThisBlock_ = 0u;
        resetC64SampleCursorsForHandoff_();
    }

    // RT-safe retire: stores old player into the retired slot for deletion by
    // the non-RT collectRetiredPsidPlayer_. Never calls delete on the render
    // thread. If the retired slot is unexpectedly occupied (should not happen
    // in normal load/unload sequencing), we overwrite it and accept a one-time
    // leak rather than calling the allocator from the audio thread.
    void retirePsidPlayer_(ArpSID::C64::C64Runtime* old) noexcept {
        if (!old) return;
        // audit P1.10: park the outgoing player in the first free retired slot.
        // Never delete on the render thread. With kRetiredPsidSlots slots drained
        // on every load/unload/eject this never fills in practice.
        for (int i = 0; i < kRetiredPsidSlots; ++i) {
            ArpSID::C64::C64Runtime* expected = nullptr;
            if (c64PsidRetired_[i].compare_exchange_strong(expected, old,
                    std::memory_order_acq_rel, std::memory_order_relaxed)) {
                return; // parked for the non-RT collector
            }
        }
        // Ring full: a fatal/diagnostic condition (the non-RT collector has not
        // run for kRetiredPsidSlots consecutive handoffs). Flag it loudly and leak
        // this one rather than freeing from the audio thread.
        retireOverflowCount_.fetch_add(1u, std::memory_order_relaxed);
        retireDropCount_.fetch_add(1u, std::memory_order_relaxed);
    }

    // ── v583/v871 BridgeTransaction ─────────────────────────────────────────
    // Lightweight stack token for a render-owned play transaction. The large
    // PHI2/bridge/sink snapshots live in persistent kernel/runtime storage, so
    // the audio stack never carries full C64 RAM or 4096-entry bridge copies.
    struct BridgeTransactionSnapshot final {
        bool bridgeCaptured = false;
        ArpSID::C64::C64Runtime::C64RenderTransaction runtimeTx{};
    };

    BridgeTransactionSnapshot beginBridgeTransaction_(ArpSID::C64::C64Runtime* player = nullptr) noexcept {
        BridgeTransactionSnapshot tx;
        c64SidBridge_.captureSnapshot(c64SidBridgeTransactionSnapshot_);
        tx.bridgeCaptured = true;
        if (player) {
            tx.runtimeTx = player->beginRenderTransaction();
        }
        return tx;
    }

    void rollbackBridgeTransaction_(BridgeTransactionSnapshot& tx,
                                    ArpSID::C64::C64Runtime* player = nullptr) noexcept {
        if (tx.bridgeCaptured) {
            c64SidBridge_.restoreSnapshot(c64SidBridgeTransactionSnapshot_);
        }
        if (player && tx.runtimeTx.active) {
            const bool rollbackOk = player->rollbackRenderTransaction(tx.runtimeTx);
            if (!rollbackOk) {
                // v872 P1-4 recovery policy. A false return means the bounded
                // platform mutation journal overflowed and could not prove a full
                // restore of the platform inspection/bootstrap mirror. The
                // authoritative PHI2 machine and runtime SID sink were still fully
                // restored (value snapshots, not bounded journals), so the fix is to
                // re-seed the stale platform mirror from that authoritative state
                // instead of continuing on partially-restored bootstrap RAM. Latch a
                // contamination flag and recovery count, published to telemetry
                // (c64RenderContaminated / c64RenderContaminationRecoveryCount) so the
                // GUI can surface a "rollback recovered" warning and stop treating the
                // C64 status as clean until the next tune handoff clears the flag.
                c64RenderTransactionRollbackFailureCount_.fetch_add(1u, std::memory_order_relaxed);
                player->resyncPlatformFromAuthoritativePhi2();
                c64RenderContaminationRecoveryCount_.fetch_add(1u, std::memory_order_relaxed);
                c64RenderContaminated_.store(1u, std::memory_order_relaxed);
            }
        }
    }

    bool bridgeTimedWriteOverflowedSinceTransaction_() const noexcept {
        return c64SidBridge_.timedWriteOverflow != c64SidBridgeTransactionSnapshot_.timedWriteOverflow;
    }

    void commitBridgeTransaction_(BridgeTransactionSnapshot& tx,
                                  ArpSID::C64::C64Runtime* player = nullptr) noexcept {
        if (player && tx.runtimeTx.active) {
            player->commitRenderTransaction(tx.runtimeTx);
        }
    }
    // ────────────────────────────────────────────────────────────────────────

    void drainPendingPsidHandoff_() noexcept {
        if (c64PsidUnloadRequested_.exchange(0u, std::memory_order_acq_rel) != 0u) {
            ArpSID::C64::c64SidBridgeRemove(c64Platform_, c64SidBridge_);
            retirePsidPlayer_(c64PsidLive_.exchange(nullptr, std::memory_order_acq_rel));
            c64SidBridge_.reset();
            if (c64PsidHardResetRequested_.exchange(0u, std::memory_order_acq_rel) != 0u) {
                c64Platform_.reset(runtimeModel_.variantProfile().video_standard != ArpSID::SidVideoStandard::NTSC);
                sreg_().reset();
                sreg_().resetIntervalCursor();
            }
            c64BlockPlayCalls_ = 0;
            resetC64SampleCursorsForHandoff_();
            resetC64RenderInstrumentation_();  // v840
            resetC64FailureCountersForHandoff_();  // v872 P1-5: failure/drop counters read since-load
            c64TelemetryGate_.clear();
            clearPresentationScopeSnapshot_();
        }
        if (ArpSID::C64::C64Runtime* incoming = c64PsidIncoming_.exchange(nullptr, std::memory_order_acq_rel)) {
            // Audit #66: track every actual render-side handoff swap. The
            // load is non-RT (new C64Runtime + loadPsid happen off-thread)
            // but the pointer-swap + bridge-attach below DOES happen on
            // the render thread. The counter lets diagnostic tooling
            // observe how many handoffs occurred per session.
            c64PsidRenderHandoffCount_.fetch_add(1u, std::memory_order_relaxed);
            c64PsidHardResetRequested_.store(0u, std::memory_order_release);
            retirePsidPlayer_(c64PsidLive_.exchange(incoming, std::memory_order_acq_rel));
            // v874 audit P0-3: hard-reset EVERY SID engine (chips 0..4) on a fresh
            // PSID/RSID handoff. Chip 0 (the shared primary engine) was previously
            // skipped, so its internal oscillator phase / envelope pipeline / filter
            // integrator / noise LFSR survived from the prior tune — reseeding the
            // registers below (line ~1946) restores the register latches but NOT that
            // internal DSP state, producing a load-order-dependent startup transient.
            // A real C64 resets the SID on tune load, so reset chip 0 too; the register
            // reseed right after makes it symmetric with chips 1..4.
            for (uint8_t ch = 0u; ch < kMaxRenderedSidChips; ++ch) {
                if (SidRegisterEngine* e = sregForChip_(ch)) {
                    e->reset();
                    e->resetIntervalCursor();
                    e->setD418VolumeDacEmulation(true);
                    e->resetD418VolumeDacEmulationState();
                }
            }
            // A PSID runtime must drive the bridge, not its construction-time sink.
            // Reset the bridge register image before attaching so a newly loaded SID
            // cannot inherit stale register values from the previous tune/subtune.
            c64SidBridge_.reset();
            incoming->platform().attachSid(&c64SidBridge_);
            resetC64SampleCursorsForHandoff_();
            resetC64RenderInstrumentation_();  // v840: fresh maxes per loaded tune
            resetC64FailureCountersForHandoff_();  // v872 P1-5: failure/drop counters read since-load
            ArpSID::C64::c64SidBridgeInstallWithSink(incoming->platform(),
                                                     c64SidBridge_,
                                                     &sreg_(),
                                                     &incoming->sidSink());
            // v579: seed the audio engine and bridge register image from
            // init-time SID register state. The PSID init routine runs
            // off-thread via C64RuntimeSidSink (telemetry only); sreg_()
            // never receives those writes. Without seeding sreg_() starts
            // with ADSR=0: zero sustain means the envelope decays
            // immediately to silence after gate-on, so every note produces
            // a brief click → audible choppiness for all ordinary PSID
            // tunes that set ADSR only in init (most of them).
            //
            // platform().sidRegisterImage() holds the complete post-init
            // values: writeMapped_() always stores every SID write to
            // sidRegs_[] regardless of which sidSink_ is active, so this
            // array is always authoritative after runInit() completes.
            //
            // Seeding via sreg_().write() bypasses the bridge's
            // timed-write buffer (direct call) — no contamination of the
            // PlayBase-relative sample offset logic. The first play call
            // at sample 0 will overwrite any per-frame registers normally.
            {
                const auto& initRegs  = incoming->platform().sidRegisterImage();
                const auto& initBanks = incoming->platform().sidRegisterBanks();
                for (uint8_t r = 0u; r < 32u; ++r) {
                    if (ArpSID::C64::c64SidRegWriteable(r)) {
                        sreg_().write(r, initRegs[r]);
                        c64SidBridge_.regs[r] = initRegs[r];
                        mirrorSidCoreShadowWrite_(r, initRegs[r], 0u, 0u);
                        // Seed every secondary engine (chips 1..4) from its bank.
                        for (uint8_t ch = 1u; ch < kMaxRenderedSidChips; ++ch) {
                            if (initBanks.size() > ch) {
                                if (SidRegisterEngine* e = sregForChip_(ch)) e->write(r, initBanks[ch][r]);
                            }
                        }
                    }
                }
                // Seed the bridge's PER-CHIP register banks too, so telemetry /
                // readback for secondary SIDs reflects the post-init image
                // immediately (not stale until the first play write). Covers
                // chip 0 as well, keeping regsByChip[0] coherent with regs[].
                const std::size_t bankCount = std::min(initBanks.size(),
                                                       c64SidBridge_.regsByChip.size());
                for (std::size_t ch = 0u; ch < bankCount; ++ch) {
                    for (uint8_t r = 0u; r < 32u; ++r) {
                        c64SidBridge_.regsByChip[ch][r] = initBanks[ch][r];
                    }
                }
            }
        }
    }

    // Re-establish the C64 SID register engines from the live player's
    // persistent post-init register image at the start of every render block.
    //
    // The shared SidRegisterEngine (sreg_) is also the synth/DrSID backend. On
    // every processBlock the synth runtime projects its own ADSR/filter/control
    // state onto that engine (projectStateToBackends → projectRuntimeStateToBackends).
    // For a loaded PSID/RSID tune that projection clobbers the tune's register
    // state — ADSR/filter/control are typically written only by the SID init
    // routine, not re-written every frame — leaving the voice registers zeroed
    // and the player silent (only the master-volume register $D418, which the
    // synth projection does not touch, survived). Seeding once at handoff (v579)
    // is insufficient because the clobber recurs every block.
    //
    // Re-seeding here, after the synth projection has run and before this block's
    // play routine executes, makes the C64 player authoritative over the shared
    // engine: this is the correct sample-0 baseline (registers are persistent
    // hardware latches), and this block's timed play writes are layered on top at
    // their sample offsets during the per-sample render below.
    void reseedSidEnginesFromPlayerImage_(ArpSID::C64::C64Runtime* player) noexcept {
        if (!player) return;
        const auto& regs  = player->platform().sidRegisterImage();
        const auto& banks = player->platform().sidRegisterBanks();
        for (uint8_t r = 0u; r < 32u; ++r) {
            if (!ArpSID::C64::c64SidRegWriteable(r)) continue;
            sreg_().write(r, regs[r]);
            // Keep the SIDCORE/live GUI shadow aligned with the real C64 player
            // authority, not only with timed play writes. This matters after any
            // synth/backend projection touches the shared SidRegisterEngine and for
            // sparse/init-only tunes where the play routine does not rewrite ADSR,
            // pulse width, filter routing, or control registers every frame.
            mirrorSidCoreShadowWrite_(r, regs[r], 0u, 0u);
            c64SidBridge_.regs[r] = regs[r];
            if (!c64SidBridge_.regsByChip.empty()) c64SidBridge_.regsByChip[0][r] = regs[r];
            for (uint8_t ch = 1u; ch < kMaxRenderedSidChips; ++ch) {
                if (banks.size() > ch) {
                    const uint8_t v = banks[ch][r];
                    if (SidRegisterEngine* e = sregForChip_(ch)) e->write(r, v);
                    if (ch < c64SidBridge_.regsByChip.size()) c64SidBridge_.regsByChip[ch][r] = v;
                }
            }
        }
    }

    bool renderC64PsidBlockIfActive_(float** outputs, int numFrames) noexcept {
        ArpSID::C64::C64Runtime* player = c64PsidLive_.load(std::memory_order_acquire);
        if (!player || !outputs || !outputs[0] || numFrames <= 0) return false;
        // v838: push the live VIC-II-fast toggle onto the player (render-thread only).
        player->setVicFast(c64VicFast_.load(std::memory_order_relaxed));
        // v839: push the live 6510-fast toggle (skips per-cycle diag snapshot).
        player->setCpuFast(c64CpuFast_.load(std::memory_order_relaxed));
        float* left = outputs[0];
        float* right = outputs[1] ? outputs[1] : outputs[0];
        c64BlockPlayCalls_ = 0;

        auto& platform = player->platform();
        const bool pal = platform.clockHz() == ArpSID::C64::kPalPhi2Hz;
        // Sync SID engine's clock bit (bit 0) and model bit (bit 1) to the loaded
        // tune. The player's clock is authoritative (set from PSID/RSID flags at
        // load time); the UI system byte may disagree and must not cause pitch
        // errors in player mode.
        //
        // SID model (system-byte bit 1: set=8580, clear=6581) follows each
        // PSID v2NG chip hint independently. Ambiguous 0/3 hints follow the
        // user's selected global model; silently forcing 8580 made untagged 6581
        // tunes sound too clean/bright and applied the wrong filter response.
        {
            // v873 audit item 5: consume the parser's NORMALIZED per-chip model (which
            // already resolved SID1-inheritance) instead of re-decoding raw PSID flags.
            const auto& hdr = player->image().header;
            const bool fallbackIs6581 = runtimeModel_.variantProfile().is6581();
            for (uint8_t ch = 0u; ch < kMaxRenderedSidChips; ++ch) {
                SidRegisterEngine* e = sregForChip_(ch);
                if (!e) continue;
                const ArpSID::PsidSidModel model = ch < 5u ? hdr.sidModel[ch]
                                                           : ArpSID::PsidSidModel::Unknown;
                const bool want6581 =
                    ArpSID::C64::psidSidModelWants6581(model, fallbackIs6581);
                const uint8_t cur = e->currentSystemByte();
                uint8_t wanted = pal ? static_cast<uint8_t>(cur & ~0x01u)
                                     : static_cast<uint8_t>(cur | 0x01u);
                if (want6581) wanted = static_cast<uint8_t>(wanted & ~0x02u);
                else          wanted = static_cast<uint8_t>(wanted | 0x02u);
                if (e->currentSystemByte() != wanted) e->writeSystemByte(wanted);
                e->setClockFrequency(static_cast<double>(platform.clockHz()));
                e->setD418VolumeDacEmulation(true);
            }
        }
        // Restore the C64 tune's persistent SID register state onto the shared
        // render engine(s) before this block's play routine runs. The synth
        // backend projection (projectStateToBackends) clobbers these registers
        // every processBlock; without this re-seed the tune's init-time ADSR /
        // filter / control state is lost each block and the player is silent.
        reseedSidEnginesFromPlayerImage_(player);
        const double clockHz = std::max(1.0, static_cast<double>(platform.clockHz()));
        const double sr = ArpSIDSanitizeHostSampleRate(sampleRate_);
        const bool psidCiaServicePath =
            player->usesCiaTiming() &&
            player->psidCiaPlaybackBootstrapSnapshot().installed;
        // Derive PSID play cadence from VIC frame geometry or CIA Timer A.
        // CIA-speed tunes use Timer A latch cycles; VBI tunes use VIC frame
        // geometry (PAL 63×312, NTSC 65×263) with no hard 50/60 Hz render law.
        //
        // v856 missing-logic fix (multi-speed CIA tunes): the cadence for the
        // CIA path must come from the LIVE CIA1 Timer A latch, not the fixed
        // 50/60 Hz default. Many tunes reprogram $DC04/$DC05 in init or play to
        // run at 2x/4x (e.g. latch 9852 ≈ 100 Hz PAL) or at tracker-specific
        // tempos. Scheduling service calls at the fixed 19705-cycle default made
        // those tunes' play routines fire at half/quarter of their intended rate
        // — audibly choppy/wrong on exactly that tune class. The latch is
        // re-read every block, so tunes that change tempo mid-song follow too.
        // Clamped to [1000, 65535] so a degenerate tiny latch cannot schedule a
        // per-sample service storm (play-call count stays bounded by
        // kC64PsidMaxPlayCallsPerAudioBlock regardless).
        uint64_t ciaLatchCycles = static_cast<uint64_t>(ArpSID::C64::C64TimingMath::psidCiaTimerALatch(pal));
        if (psidCiaServicePath && player->phi2MachineReady()) {
            const uint16_t liveLatch = player->phi2Machine().cia1().latchA();
            if (liveLatch >= 1000u) {
                ciaLatchCycles = liveLatch;
            } else {
                // Sub-1000 latch cannot drive the scheduler (per-sample service
                // storm); keep the default cadence set above, but surface the
                // downgrade rather than clamping silently (v873 P1-7).
                c64LastClampedCiaLatch_.store(liveLatch, std::memory_order_relaxed);
                c64CiaLatchClampCount_.fetch_add(1u, std::memory_order_relaxed);
            }
        }
        const uint64_t playPhi2Cycles = psidCiaServicePath
            ? ciaLatchCycles
            : ArpSID::C64::C64TimingMath::psidVbiFrameCycles(pal);
        const double playPeriodSamples = ArpSID::C64::C64TimingMath::psidPlayPeriodSamplesFromCycles(
            playPhi2Cycles, sr, static_cast<uint32_t>(platform.clockHz()));
        c64PsidPlayPeriodSamplesCache_ = std::max(1.0, playPeriodSamples);  // Fix #7: cache for chunking
        if (!(std::isfinite(c64PsidPlaySampleCountdown_) &&
              c64PsidPlaySampleCountdown_ >= 0.0 &&
              c64PsidPlaySampleCountdown_ < playPeriodSamples * 4.0)) {
            c64PsidPlaySampleCountdown_ = 0.0;
        }

        c64PsidPassiveClock_.configure(sr, clockHz);
        uint32_t passiveHostFramesAccrued = 0u;
        uint64_t passiveCyclesAccruedThisBlock = 0u;
        const auto accruePassiveHostTimeToFrame = [&](uint32_t frame) noexcept {
            const uint32_t target = std::min<uint32_t>(
                frame, static_cast<uint32_t>(std::max(0, numFrames)));
            if (target <= passiveHostFramesAccrued) return;
            const uint32_t deltaFrames = target - passiveHostFramesAccrued;
            const uint64_t added =
                c64PsidPassiveClock_.cyclesForNextHostBlock(deltaFrames);
            c64PsidPassiveCycleDebt_ = std::min<uint64_t>(
                c64PsidPassiveCycleDebt_ + added,
                kC64PsidMaxPassiveDebtCycles);
            passiveCyclesAccruedThisBlock = std::min<uint64_t>(
                passiveCyclesAccruedThisBlock + added,
                kC64PsidMaxPassiveDebtCycles);
            passiveHostFramesAccrued = target;
        };

        // Clear stale bridge entries from the previous block. audit P1 (output-tap):
        // resetTimedWrites() already zeroes timedWrites[0..timedWriteCount) BEFORE
        // resetting the counter (it snapshots n = timedWriteCount first), so the
        // previous explicit pre-clear loop here cleared the exact same range a
        // second time every render block. Removed — a single clear is sufficient
        // and crash dumps/release guards still never see old bus events.
        c64SidBridge_.resetTimedWrites();
        // Continuous machine-runtime mode: the CPU runs free (driven by VIC/CIA
        // interrupts) rather than being called once per VBI at a fixed play
        // address. This is true for RSID files AND for PSID files that declare
        // no play address (playAddress == 0) — the latter are NOT RSID but still
        // require the free-running path, so the name is deliberately not "rsid".
        const bool continuousMachineRuntime =
            player->image().header.rsid || player->image().header.playAddress == 0u;

        struct PlayBase final { uint64_t cycle = 0; int sample = 0; };
        PlayBase playBases[kC64PsidMaxPlayCallsPerAudioBlock]{};
        uint32_t playBaseCount = 0u;
        uint32_t playCalls = 0u;
        uint64_t continuousTimelineBaseCycle = 0u;
        bool continuousTimelineBaseValid = false;
        const bool continuousCycleMappedRuntime = continuousMachineRuntime || psidCiaServicePath;
        if (continuousCycleMappedRuntime) {
            // Continuous RSID/playAddress==0 execution has one block timeline,
            // and PSID-CIA service also advances the PHI2 machine continuously
            // until the CIA IRQ/play entry occurs. Keep their cycle origin
            // separate from PlayBase so discrete VBI play-call assumptions
            // cannot add a second sample offset to timed SID writes.
            continuousTimelineBaseCycle =
                (player->phi2MachineEnabled() && player->phi2MachineReady())
                    ? player->phi2Machine().phi2Cycle()
                    : platform.phi2Cycle();
            continuousTimelineBaseValid = true;
        }
        if (continuousMachineRuntime) {
            accruePassiveHostTimeToFrame(static_cast<uint32_t>(numFrames));
            // Continuous machine runtime has no discrete PlayBase; timed SID writes
            // are mapped from continuousTimelineBaseCycle into THIS AU buffer. Running
            // more PHI2 time than the current buffer spans cannot be represented: those
            // "future" writes clamp to the last sample and become a block-edge burst.
            // Cap the render catch-up to the current audio block's PHI2 span. If older
            // debt exists after a successful run, drop it as unrecoverable realtime
            // backlog instead of compressing it into the present block.
            const uint64_t continuousAudioBlockCycles =
                std::max<uint64_t>(1u, passiveCyclesAccruedThisBlock);
            const uint64_t debtBeforeContinuousRun = c64PsidPassiveCycleDebt_;
            const bool continuousBacklog = debtBeforeContinuousRun > continuousAudioBlockCycles;
            const uint64_t catchup =
                std::min<uint64_t>(debtBeforeContinuousRun, continuousAudioBlockCycles);
            noteC64Catchup_(C64CatchupPath_::Continuous, debtBeforeContinuousRun);  // v840/v841: expose true pre-cap backlog
            if (catchup > 0u) {
                const auto rsidResult = player->runContinuousMachineCycles(catchup, kC64RsidMaxInstructionsPerAudioBlock, &c64SidBridge_);
                publishC64ExactnessDiagnostics_(player);
                if (rsidResult.executedInstructions > 0u) ++c64BlockPlayCalls_;
                // v872 P2-1: surface continuous-run health. Partial commit is
                // expected for the free-running machine (no discrete frame to roll
                // back), but must be observable rather than silently swallowed.
                if (rsidResult.instructionBudgetHit)
                    c64ContinuousBudgetHitCount_.fetch_add(1u, std::memory_order_relaxed);
                if (rsidResult.cpuJammed)
                    c64ContinuousCpuJamCount_.fetch_add(1u, std::memory_order_relaxed);
                if (rsidResult.unsupportedOpcodeHit)
                    c64ContinuousUnsupportedOpcodeCount_.fetch_add(1u, std::memory_order_relaxed);
                if (!rsidResult.completedCycleBudget)
                    c64ContinuousIncompleteRunCount_.fetch_add(1u, std::memory_order_relaxed);
                // The legacy runRealtimeSidCoreCycles unconditionally passive-advances
                // PHI2 to the full budget (completedCycleBudget=true always for
                // the legacy path). For the PHI2 machine path, an early stop due to
                // an unsupported opcode or instruction budget means the machine did
                // not consume all catchup cycles. Only debit what was actually
                // consumed so the unexecuted remainder can be retried next block.
                // For the normal case (completedCycleBudget=true), consumed==catchup
                // and debt returns to exactly zero.
                const uint64_t consumed = rsidResult.executedCycles + rsidResult.passiveCycles;
                uint64_t remainingDebt = (debtBeforeContinuousRun > consumed)
                    ? debtBeforeContinuousRun - consumed
                    : 0u;
                if (rsidResult.completedCycleBudget && continuousBacklog) {
                    remainingDebt = 0u;
                } else if (remainingDebt > continuousAudioBlockCycles) {
                    remainingDebt = continuousAudioBlockCycles;
                }
                c64PsidPassiveCycleDebt_ = remainingDebt;
            }
        } else {
            // PSID-CIA accrues the full block once, then maps bridge writes from
            // the continuous PHI2 timeline captured above. VBI playback remains
            // a sequence of discrete play() calls and accrues host time
            // progressively at each play deadline below.
            if (psidCiaServicePath)
                accruePassiveHostTimeToFrame(static_cast<uint32_t>(numFrames));
            while (playCalls < kC64PsidMaxPlayCallsPerAudioBlock &&
                      c64PsidPlaySampleCountdown_ < static_cast<double>(numFrames)) {
            // Block-local play sample is quantized from the fractional cadence accumulator once per play call.
            // The accumulator stays double-precision across blocks to avoid integer block-rate drift.
            const int playSample = std::clamp(static_cast<int>(std::floor(c64PsidPlaySampleCountdown_ + 1.0e-9)), 0, std::max(0, numFrames - 1));

            if (psidCiaServicePath) {
                (void)playSample;
                // CIA-speed PSID is continuous machine time: the service call
                // advances from block-start PHI2 until the CIA timer IRQ enters
                // play. Mapping through PlayBase{cycle, playSample} double-counts
                // that time as playSample + CIA offset and pushes writes late.
                const uint64_t catchup = std::min<uint64_t>(
                    c64PsidPassiveCycleDebt_,
                    std::max<uint64_t>(playPhi2Cycles + 2048ull, kC64PsidMaxPassiveCatchupCyclesPerPlay));
                noteC64Catchup_(C64CatchupPath_::Cia, catchup);
                if (catchup > 0u) {
                    // PSID-CIA is continuous machine execution, not an atomic
                    // VBI play() frame. Partial service can legitimately cross a
                    // small AU buffer boundary; commit non-jammed progress and
                    // reserve rollback for real CPU failure.
                    BridgeTransactionSnapshot tx = beginBridgeTransaction_(player);
                    // v855 P0: pass the audio timed-write bridge so play-routine SID
                    // writes land at their exact PHI2-derived sample offsets instead
                    // of only reseeding the register image at block edges.
                    const uint32_t ciaWritesBefore = c64SidBridge_.timedWriteCount;
                    const auto service = player->runPsidCiaPlaybackServiceTicks(catchup, &c64SidBridge_);
                    const uint64_t consumed = std::min<uint64_t>(c64PsidPassiveCycleDebt_, service.ticksExecuted);
                    c64PsidPassiveCycleDebt_ -= consumed;
                    const bool overflowFatal = bridgeTimedWriteOverflowedSinceTransaction_();
                    if (service.cpuJammed || overflowFatal) {
                        rollbackBridgeTransaction_(tx, player);
                        if (overflowFatal) {
                            c64TimedWriteOverflowTotal_.fetch_add(1u, std::memory_order_relaxed);
                            if (player) player->notifyTimedWriteOverflow(1u);
                        }
                        platform.cpu().state().jammed = true;
                        c64RsidPlayRollbackCount_.fetch_add(1u, std::memory_order_relaxed);
                        if (service.cpuJammed) c64RsidCpuJamCount_.fetch_add(1u, std::memory_order_relaxed);
                        else c64RsidRunBudgetHitCount_.fetch_add(1u, std::memory_order_relaxed);
                    } else {
                        commitBridgeTransaction_(tx, player);
                        if (service.serviceComplete) {
                            ++c64BlockPlayCalls_;
                        } else if (service.playAddressEntered) {
                            c64RsidCiaIncompleteCount_.fetch_add(1u, std::memory_order_relaxed);
                            // v873 (timing audit P0-3): the audible-risk subset — a play
                            // routine entered but did not return to idle, AND committed
                            // SID register writes this window. Those partial register
                            // updates are the ones that make CIA tunes sound uneven; the
                            // plain "entered but not complete" case above is harmless when
                            // no writes occurred.
                            if (c64SidBridge_.timedWriteCount > ciaWritesBefore)
                                c64CiaSidWritesDuringIncompleteCount_.fetch_add(1u, std::memory_order_relaxed);
                        }
                    }
                    // If !playAddressEntered: CIA timer hasn't fired in this
                    // window. If entered but not complete, the next block
                    // continues the authoritative PHI2 machine state.
                }
            } else {
                // Publish only host time that has actually elapsed up to this VBI
                // deadline. This prevents the first play in a large host block
                // from borrowing cycles belonging to later samples.
                accruePassiveHostTimeToFrame(static_cast<uint32_t>(playSample));
                // VBI path: advance passive PHI2 first, THEN record PlayBase.
                // v575 fix: recording base before passive advancement caused all
                // play-routine SID writes to carry a kC64PsidMaxPassiveCatchupCyclesPerPlay
                // offset (8192 cycles ≈ 367 audio samples at 44.1 kHz PAL) into
                // the sample-offset calculation:
                // sampleOffset = playSample + round(8192 × sr/clockHz) + play_offset
                // ≈ playSample + 367 + small
                // Notes started/stopped 8.3 ms too late every VBI frame. When
                // playSample + 367 exceeded numFrames the writes were clamped to
                // the last sample, producing irregular update intervals (656, 1024
                // instead of the correct 882 samples) and audible choppiness.
                // With base recorded post-passive:
                // sampleOffset = playSample + round(play_offset × sr/clockHz)
                // ≈ playSample (correct: notes update at play-fire time)
                //
                // v576 fix: advance a full VBI frame (playPhi2Cycles) per play call,
                // not the arbitrary 8192-cycle cap (kC64PsidMaxPassiveCatchupCyclesPerPlay).
                // With only 8192 cycles of passive advancement, CIA Timer A and VIC
                // counters advance just 41.7% of a PAL VBI period between play calls.
                // Any tune using CIA timers for arpeggio, vibrato, or internal tempo
                // effects inside the play routine sees the timer at the wrong phase,
                // producing wrong rates or missed timer events. Running one full VBI
                // frame (playPhi2Cycles = 19656 PAL / 17095 NTSC) of passive cycles
                // before each play call matches real C64 VBI-interrupt timing:
                // - CIA Timer A completes exactly one period → correct arpeggio rate
                // - VIC raster position at play entry matches a real VBI interrupt
                // - Passive-cycle debt stays bounded near 0 (add ≈ consume per VBI)
                // The CIA path already uses this principle: it runs playPhi2Cycles+2048
                // cycles so the CIA timer fires at the correct cycle within the window.
                //
                // v577 fix A — jam CPU before passive to prevent continuation of an
                // unfinished play routine. If runPlay() returned false (budget hit),
                // jammed=false and the 6510 PC is still mid-play. Without this jam,
                // the passive platform.runCycles() below would continue executing the
                // remainder of the play routine: those SID writes carry phi2Cycle <
                // PlayBase.cycle so the sample-offset guard maps them all to playSample
                // (deltaCycle = 0), producing a burst of note events at the wrong
                // sample. Worse, the explicit runPlay() that follows re-executes from
                // scratch with contaminated machine state (arp/vibrato pointers already
                // advanced by the continuation), producing notes from the wrong frame.
                // Setting jammed=true here makes platform.runCycles() skip CPU
                // instruction execution entirely (CIA/VIC still tick correctly).
                // bootFromResetVectorPreservingMachine() inside runPlay() clears jammed
                // so the real play call executes normally.
                platform.cpu().state().jammed = true;
                // Never advance beyond accrued host time. With progressive
                // accrual, two VBI calls in one 1024-frame block each receive the
                // cycles elapsed since the preceding deadline instead of the
                // first call consuming the entire block or borrowing a frame.
                const uint64_t catchup =
                    std::min<uint64_t>(c64PsidPassiveCycleDebt_, playPhi2Cycles);
                noteC64Catchup_(C64CatchupPath_::Vbi, catchup);
                uint64_t consumed = 0u;
                if (catchup > 0u) {
                    if (player->phi2MachineReady()) {
                        const auto passive = player->runPsidVbiPassivePhi2Cycles(catchup, &c64SidBridge_);
                        consumed = std::min<uint64_t>(
                            c64PsidPassiveCycleDebt_,
                            passive.executedCycles + passive.passiveCycles);
                    } else {
                        platform.runCycles(catchup, &c64SidBridge_);
                        consumed = std::min<uint64_t>(c64PsidPassiveCycleDebt_, catchup);
                    }
                }
                c64PsidPassiveCycleDebt_ -= consumed;
                if (playBaseCount < kC64PsidMaxPlayCallsPerAudioBlock) {
                    const uint64_t baseCycle = player->phi2MachineReady()
                        ? player->phi2Machine().phi2Cycle()
                        : platform.phi2Cycle();
                    playBases[playBaseCount++] = PlayBase{ baseCycle, playSample };
                } else {
                    // Fix #3: play-base array full — count overflow for diagnostics.
                    c64PlayBaseOverflowCount_.fetch_add(1u, std::memory_order_relaxed);
                }
                // v583: transactional VBI play — if runPlay() exhausts its
                // instruction budget or the CPU halts unexpectedly, the bridge
                // register image and timed-write count are rolled back to the
                // pre-play snapshot. Only complete play frames are committed.
                // The jammed flag is set on failure so the passive runCycles()
                // on the next block does not continue executing the mid-play
                // CPU state (v577 jam rule still applies; this rollback is
                // complementary — it also restores the bridge state).
                {
                    BridgeTransactionSnapshot tx = beginBridgeTransaction_(player);
                    // v855 P0: pass the audio timed-write bridge so VBI play-routine
                    // SID writes land at their exact PHI2-derived sample offsets
                    // instead of only reseeding the register image at block edges.
                    if (player->runPlay(c64AdaptivePlayBudget_, &c64SidBridge_) &&
                        !bridgeTimedWriteOverflowedSinceTransaction_()) {
                        commitBridgeTransaction_(tx, player);
                        ++c64BlockPlayCalls_;
                    } else if (bridgeTimedWriteOverflowedSinceTransaction_()) {
                        // v891: timed-write overflow is fatal to the current play frame.
                        // Roll back the PHI2/runtime/bridge transaction so a truncated
                        // SID-write list can never be rendered as if it were complete.
                        // The next block retries from the restored machine state.
                        rollbackBridgeTransaction_(tx, player);
                        c64TimedWriteOverflowTotal_.fetch_add(1u, std::memory_order_relaxed);
                        if (player) player->notifyTimedWriteOverflow(1u);
                        c64RsidPlayRollbackCount_.fetch_add(1u, std::memory_order_relaxed);
                        c64RsidRunBudgetHitCount_.fetch_add(1u, std::memory_order_relaxed);
                    } else {
                        // Fix #3: distinguish budget hit from CPU jam before
                        // forcing the post-failure safety jam. The previous code
                        // set jammed=true first, making the budget-hit branch unreachable.
                        // v872: a VBI play runs on the PHI2 machine, so read ITS jam
                        // state (before rollback restores it), not the legacy platform
                        // CPU which runPlay() never executed — otherwise real KIL/JAM
                        // failures were miscounted as instruction-budget hits.
                        const bool playCpuJammed = player->phi2MachineReady()
                            ? player->phi2Machine().cpu().state().jammed
                            : platform.cpu().state().jammed;
                        rollbackBridgeTransaction_(tx, player);
                        platform.cpu().state().jammed = true; // prevent passive continuation of failed play
                        c64RsidPlayRollbackCount_.fetch_add(1u, std::memory_order_relaxed);
                        if (playCpuJammed) {
                            c64RsidCpuJamCount_.fetch_add(1u, std::memory_order_relaxed);
                        } else {
                            c64RsidRunBudgetHitCount_.fetch_add(1u, std::memory_order_relaxed);
                            // v873 (audit P0-1): a frame dropped purely for budget
                            // exhaustion — grow the per-tune budget (bounded) so this
                            // heavy play routine can complete next time instead of
                            // permanently dropping frames. A genuine runaway/infinite
                            // play routine still hits the ceiling and rolls back.
                            c64AdaptivePlayBudget_ = std::min<uint32_t>(
                                c64AdaptivePlayBudget_ * 2u, kC64PsidAdaptivePlayBudgetCeiling);
                        }
                    }
                }
            }
            ++playCalls;
            c64PsidPlaySampleCountdown_ += playPeriodSamples;
        }
            if (!psidCiaServicePath)
                accruePassiveHostTimeToFrame(static_cast<uint32_t>(numFrames));
        }
        if (!continuousMachineRuntime &&
            playCalls >= kC64PsidMaxPlayCallsPerAudioBlock &&
            c64PsidPlaySampleCountdown_ < static_cast<double>(numFrames) &&
            playPeriodSamples > 0.0) {
            const double remainingSamples =
                static_cast<double>(numFrames) - c64PsidPlaySampleCountdown_;
            const uint32_t droppedDueCalls = remainingSamples > 0.0
                ? static_cast<uint32_t>(1u + static_cast<uint32_t>(
                      std::floor(std::max(0.0, remainingSamples - 1.0e-9) / playPeriodSamples)))
                : 1u;
            c64PlayCallCapHitCount_.fetch_add(1u, std::memory_order_relaxed);
            c64PlayBaseOverflowCount_.fetch_add(std::max<uint32_t>(1u, droppedDueCalls),
                                                std::memory_order_relaxed);
            c64PlayCallsDroppedByCapTotal_.fetch_add(droppedDueCalls, std::memory_order_relaxed);
            c64PlayCallsDroppedByCapLastBlock_.store(droppedDueCalls, std::memory_order_relaxed);
            uint32_t hi = c64PlayCallsDroppedByCapMaxBlock_.load(std::memory_order_relaxed);
            while (droppedDueCalls > hi &&
                   !c64PlayCallsDroppedByCapMaxBlock_.compare_exchange_weak(
                       hi, droppedDueCalls, std::memory_order_relaxed, std::memory_order_relaxed)) {}
        } else {
            c64PlayCallsDroppedByCapLastBlock_.store(0u, std::memory_order_relaxed);
        }
        c64PsidPlaySampleCountdown_ = std::max(0.0, c64PsidPlaySampleCountdown_ - static_cast<double>(numFrames));

        const uint32_t nWrites = std::min<uint32_t>(c64SidBridge_.timedWriteCount,
            static_cast<uint32_t>(ArpSID::C64::C64SidBridgeState::kMaxTimedWrites));
        // Promote the bridge's per-block timed-write overflow into render-level
        // diagnostics so dropped writes (very write-heavy digi/SID abuse) are
        // visible instead of being silently swallowed by the bridge ring.
        {
            const uint32_t overflowThisBlock = c64SidBridge_.timedWriteOverflow;
            if (overflowThisBlock != 0u) {
                c64TimedWriteOverflowTotal_.fetch_add(overflowThisBlock, std::memory_order_relaxed);
                // Fix #1: notify runtime player so it can downgrade exactness.
                if (player) player->notifyTimedWriteOverflow(overflowThisBlock);
            }
            c64TimedWriteOverflowLastBlock_.store(overflowThisBlock, std::memory_order_relaxed);
            const uint32_t used = c64SidBridge_.timedWriteCount;
            uint32_t hi = c64MaxTimedWritesUsed_.load(std::memory_order_relaxed);
            if (used > hi) c64MaxTimedWritesUsed_.store(used, std::memory_order_relaxed);
        }
        C64RenderWrite* writes = c64RenderWriteScratch_.data();
        uint32_t writeCount = 0u;
        const uint64_t cyclesPerSampleQ32 = ArpSID_cyclesPerSampleQ32(sr, clockHz);
        const uint64_t blockStartHostSample = c64PsidAudioHostSampleCursor_;
        // Host cycle at the first sample PAST this audible C64 SIDPLAY block. The
        // cursor is advanced by renderC64PsidBlockIfActive_ itself, not by the
        // cosmetic mirror publisher, so PHI2/sample bucketization is continuous
        // even when the SIDPLAY path returns before telemetry runs. A timed write at or beyond
        // this cycle cannot be placed in-block and is clamped to the last sample
        // (block-edge burst); count those so oversized catch-up/service windows are
        // attributable (v873 timing audit P1-3).
        const uint64_t blockEndHostCycle = (cyclesPerSampleQ32 != 0ull)
            ? ArpSID_absoluteCycleAtSampleQ32(
                  blockStartHostSample + static_cast<uint64_t>(std::max(0, numFrames)),
                  cyclesPerSampleQ32)
            : 0ull;
        uint32_t droppedMultiSidWritesThisBlock = 0u;
        int hostCycleSampleCursor = 0;
        uint64_t lastMappedHostCycle = 0u;
        bool lastMappedHostCycleValid = false;
        const auto hostSampleForCycle = [&](uint64_t hostCycle) noexcept -> int {
            if (cyclesPerSampleQ32 == 0ull || numFrames <= 1) return 0;
            const int lastFrame = std::max(0, numFrames - 1);
            if (!lastMappedHostCycleValid || hostCycle < lastMappedHostCycle) {
                hostCycleSampleCursor = 0;
            }
            lastMappedHostCycle = hostCycle;
            lastMappedHostCycleValid = true;
            while (hostCycleSampleCursor < lastFrame &&
                   ArpSID_absoluteCycleAtSampleQ32(blockStartHostSample + static_cast<uint64_t>(hostCycleSampleCursor + 1),
                                                   cyclesPerSampleQ32) <= hostCycle) {
                ++hostCycleSampleCursor;
            }
            while (hostCycleSampleCursor > 0 &&
                   ArpSID_absoluteCycleAtSampleQ32(blockStartHostSample + static_cast<uint64_t>(hostCycleSampleCursor),
                                                   cyclesPerSampleQ32) > hostCycle) {
                --hostCycleSampleCursor;
            }
            return std::clamp(hostCycleSampleCursor, 0, lastFrame);
        };
        for (uint32_t wi = 0; wi < nWrites; ++wi) {
            const auto& w = c64SidBridge_.timedWrites[wi];
            if (!ArpSID::C64::c64SidRegWriteable(w.reg)) continue;
            // Chips 0..4 (up to 5 SIDs) are rendered through dedicated engines.
            // Writes for chip >= kMaxRenderedSidChips are preserved in
            // C64SidBridgeState::regsByChip[] for telemetry but produce no audio.
            if (w.chip >= kMaxRenderedSidChips) {
                ++droppedMultiSidWritesThisBlock;
                continue;
            }

            int sampleOffset = 0;
            uint16_t cycleInSample = 0u;
            uint16_t cyclesThisSample = 1u;
            if (continuousTimelineBaseValid) {
                if (w.phi2Cycle < continuousTimelineBaseCycle)
                    c64NegativePlayBaseDeltaCount_.fetch_add(1u, std::memory_order_relaxed);
                const uint64_t deltaCycle =
                    w.phi2Cycle >= continuousTimelineBaseCycle
                        ? w.phi2Cycle - continuousTimelineBaseCycle
                        : 0u;
                if (cyclesPerSampleQ32 != 0ull) {
                    const uint64_t baseHostCycle =
                        ArpSID_absoluteCycleAtSampleQ32(blockStartHostSample,
                                                        cyclesPerSampleQ32);
                    const uint64_t writeHostCycle = baseHostCycle + deltaCycle;
                    sampleOffset = hostSampleForCycle(writeHostCycle);
                    if (blockEndHostCycle != 0ull && writeHostCycle >= blockEndHostCycle)
                        c64FutureWriteClampCount_.fetch_add(1u, std::memory_order_relaxed);
                    const uint64_t sampleHostCycle = ArpSID_absoluteCycleAtSampleQ32(
                        blockStartHostSample + static_cast<uint64_t>(sampleOffset),
                        cyclesPerSampleQ32);
                    cyclesThisSample = ArpSID_cyclesInHostSampleQ32(
                        blockStartHostSample + static_cast<uint64_t>(sampleOffset),
                        cyclesPerSampleQ32);
                    if (cyclesThisSample == 0u) cyclesThisSample = 1u;
                    const uint64_t localCycle = writeHostCycle >= sampleHostCycle
                        ? writeHostCycle - sampleHostCycle
                        : 0u;
                    cycleInSample = static_cast<uint16_t>(
                        std::min<uint64_t>(localCycle,
                            static_cast<uint64_t>(cyclesThisSample - 1u)));
                }
            } else if (playBaseCount > 0u) {
                const PlayBase* base = &playBases[0];
                for (uint32_t bi = 1; bi < playBaseCount; ++bi) {
                    if (playBases[bi].cycle <= w.phi2Cycle) base = &playBases[bi];
                }
                // A write timestamped before the selected PlayBase is clamped to
                // delta 0 (mapped to the play sample). This is safe but hides a
                // timing/ordering anomaly, so count it for diagnostics instead of
                // letting it pass silently.
                if (w.phi2Cycle < base->cycle) {
                    c64NegativePlayBaseDeltaCount_.fetch_add(1u, std::memory_order_relaxed);
                }
                const uint64_t deltaCycle = (w.phi2Cycle >= base->cycle) ? (w.phi2Cycle - base->cycle) : 0u;
                if (cyclesPerSampleQ32 != 0ull) {
                    const uint64_t baseHostSample = blockStartHostSample +
                        static_cast<uint64_t>(std::clamp(base->sample, 0, std::max(0, numFrames - 1)));
                    const uint64_t baseHostCycle = ArpSID_absoluteCycleAtSampleQ32(baseHostSample,
                                                                                  cyclesPerSampleQ32);
                    const uint64_t writeHostCycle = baseHostCycle + deltaCycle;
                    sampleOffset = hostSampleForCycle(writeHostCycle);
                    if (blockEndHostCycle != 0ull && writeHostCycle >= blockEndHostCycle)
                        c64FutureWriteClampCount_.fetch_add(1u, std::memory_order_relaxed);
                    const uint64_t sampleHostCycle = ArpSID_absoluteCycleAtSampleQ32(
                        blockStartHostSample + static_cast<uint64_t>(sampleOffset),
                        cyclesPerSampleQ32);
                    cyclesThisSample = ArpSID_cyclesInHostSampleQ32(
                        blockStartHostSample + static_cast<uint64_t>(sampleOffset),
                        cyclesPerSampleQ32);
                    if (cyclesThisSample == 0u) cyclesThisSample = 1u;
                    const uint64_t localCycle = writeHostCycle >= sampleHostCycle
                        ? writeHostCycle - sampleHostCycle
                        : 0u;
                    cycleInSample = static_cast<uint16_t>(
                        std::min<uint64_t>(localCycle, static_cast<uint64_t>(cyclesThisSample - 1u)));
                } else {
                    sampleOffset = base->sample;
                    cycleInSample = 0u;
                }
            }
            sampleOffset = std::clamp(sampleOffset, 0, std::max(0, numFrames - 1));
            C64RenderWrite rw{sampleOffset,
                              cycleInSample,
                              cyclesThisSample,
                              wi,
                              w.reg,
                              w.value,
                              w.chip};
            writes[writeCount] = rw;
            ++writeCount;
        }
        ArpSID::C64::stableScheduleC64RenderWrites(
            c64RenderWriteScratch_, c64RenderWriteRadixScratch_, writeCount);
        // Internal C64/PSID Pure-SID REC: when the C64 bus produced $D418
        // volume-register PCM, capture the exact 4-bit bus stream as ZOH samples
        // from the same timestamped bridge writes used for SID rendering. This is
        // host-permission-free and preserves repeated identical nibbles.
        captureC64D418PureSid1Q1RecordSource_(writes, writeCount, numFrames);

        // Determine how many chip engines are active for this tune (1..5).
        const uint8_t activeSidChips = player
            ? static_cast<uint8_t>(std::clamp<int>(player->image().header.sidChipCount,
                                                   1, kMaxRenderedSidChips))
            : 1u;
        const auto sidMixGains =
            ArpSID::C64::c64SidMixGains(activeSidChips, kMaxRenderedSidChips);
        for (uint8_t ch = 1u; ch < activeSidChips; ++ch) {
            if (SidRegisterEngine* e = sregForChip_(ch)) e->resetIntervalCursor();
        }

        uint32_t wi = 0u;
        for (int sample = 0; sample < numFrames; ++sample) {
            uint16_t cyclesThisSample = cyclesPerSampleQ32 != 0ull
                ? ArpSID_cyclesInHostSampleQ32(blockStartHostSample + static_cast<uint64_t>(sample),
                                               cyclesPerSampleQ32)
                : 0u;
            if (cyclesThisSample == 0u) cyclesThisSample = 1u;
            sreg_().resetIntervalCursor();
            for (uint8_t ch = 1u; ch < activeSidChips; ++ch) {
                if (SidRegisterEngine* e = sregForChip_(ch)) e->resetIntervalCursor();
            }

            while (wi < writeCount && writes[wi].sample == sample) {
                const C64RenderWrite& w = writes[wi];
                const uint16_t boundedCycle = static_cast<uint16_t>(
                    std::min<uint16_t>(w.cycle, static_cast<uint16_t>(cyclesThisSample - 1u)));
                // Publish real observed SID write activity to the C64 bus scope.
                // The once-per-block latch publisher below is only an idle fallback;
                // scopes need the write stream itself or sparse PSID/RSID tunes look
                // frozen even while audio is correct.
                const bool busScopeWriteSample =
                    writeCount <= static_cast<uint32_t>(kC64BusScopeLen) ||
                    ((wi % std::max<uint32_t>(1u, (writeCount + kC64BusScopeLen - 1u) / kC64BusScopeLen)) == 0u);
                if (busScopeWriteSample) {
                    publishC64RealtimeBusScope_(platform.readOpenBus(),
                                                platform.lastReadValue(),
                                                w.reg,
                                                w.value,
                                                true,
                                                platform.irqLine() || platform.nmiLine() ||
                                                    platform.vic().badline() || platform.vic().spriteDma(),
                                                w.chip,
                                                platform.openBusDecayMask(),
                                                platform.openBusDrivenWithinPersistence());
                }
                // Route write to the correct SID engine by chip index.
                if (w.chip == 0u) {
                    sreg_().queueSubphaseWrite(boundedCycle, 0u, w.reg, w.value);
                    // v837b: decimate the per-write SIDCORE event stream the same way as
                    // the bus scope. A busy SID tune issues thousands of register writes/sec;
                    // building+publishing a GUI event for every one scales with tune activity
                    // and was a measurable per-block cost during SIDPLAY. The GUI still gets
                    // current state from the shadow mirror below and the once-per-block live
                    // snapshot, so only the fine-grained event log is sampled. Audio is a pure
                    // telemetry no-op here, so this cannot change playback.
                    if (busScopeWriteSample)
                        publishSidCoreRegWrite_(w.reg, w.value, sidCoreWriteStamp_(sample));
                    mirrorSidCoreShadowWrite_(w.reg,
                                              w.value,
                                              static_cast<uint16_t>(sample),
                                              boundedCycle);
                } else if (w.chip < activeSidChips) {
                    if (SidRegisterEngine* e = sregForChip_(w.chip))
                        e->queueSubphaseWrite(boundedCycle, 0u, w.reg, w.value);
                } else {
                    ++droppedMultiSidWritesThisBlock;
                }
                ++wi;
            }

            ArpSID::SidRenderInterval iv{};
            iv.beginCycle = 0u;
            iv.beginSubphase = 0u;
            iv.endCycle = cyclesThisSample;
            iv.endSubphase = 0u;

            float outL = 0.0f;
            float outR = 0.0f;
            sreg_().renderIntervalAccurate(iv, outL, outR);
            sreg_().clearSubphaseWrites();
            outL *= sidMixGains.primary;
            outR *= sidMixGains.primary;

            // Mix secondary SID chips (1..activeSidChips-1) into the output.
            for (uint8_t ch = 1u; ch < activeSidChips; ++ch) {
                SidRegisterEngine* e = sregForChip_(ch);
                if (!e) continue;
                float ls = 0.0f, rs = 0.0f;
                e->renderIntervalAccurate(iv, ls, rs);
                e->clearSubphaseWrites();
                outL += ls * sidMixGains.secondary;
                outR += rs * sidMixGains.secondary;
            }

            left[sample] = ArpSID_sanitizeFloat(outL);
            right[sample] = ArpSID_sanitizeFloat(outR);
        }
        if (droppedMultiSidWritesThisBlock != 0u && player) {
            player->notifyDroppedMultiSidWrites(droppedMultiSidWritesThisBlock);
        }
        // v886: advance the audible C64 host-sample authority on every rendered
        // SIDPLAY block/chunk. Telemetry has its own cosmetic cursor; it must not
        // be the source of truth for audio-side PHI2/sample mapping.
        c64PsidAudioHostSampleCursor_ += static_cast<uint64_t>(std::max(0, numFrames));
        return true;
    }

    // Fix #7: chunk wrapper for huge offline blocks (DAW bounce).
    // If the block is small enough to fit within kC64PsidMaxPlayCallsPerAudioBlock
    // play cycles, call the inner function directly. Otherwise split into
    // sub-blocks so every play event fires correctly.
    bool renderC64PsidBlockChunkedIfActive_(float** outputs, int numFrames) noexcept {
        ArpSID::C64::C64Runtime* player = c64PsidLive_.load(std::memory_order_acquire);
        if (!player) return false;
        if (numFrames <= 0) return false;
        // v886: derive the chunk limit from the current live cadence before the
        // first inner render. The old code used c64PsidPlayPeriodSamplesCache_,
        // which is updated inside renderC64PsidBlockIfActive_ and therefore could
        // be one block stale after a CIA/VBI cadence change.
        const bool pal = player->platform().clockHz() == ArpSID::C64::kPalPhi2Hz;
        const bool psidCiaServicePath =
            player->usesCiaTiming() && player->psidCiaPlaybackBootstrapSnapshot().installed;
        uint64_t chunkPlayPhi2Cycles = psidCiaServicePath
            ? static_cast<uint64_t>(ArpSID::C64::C64TimingMath::psidCiaTimerALatch(pal))
            : ArpSID::C64::C64TimingMath::psidVbiFrameCycles(pal);
        if (psidCiaServicePath && player->phi2MachineReady()) {
            const uint16_t liveLatch = player->phi2Machine().cia1().latchA();
            if (liveLatch >= 1000u) chunkPlayPhi2Cycles = liveLatch;
        }
        const double sr = ArpSIDSanitizeHostSampleRate(sampleRate_);
        const double currentPlayPeriod = ArpSID::C64::C64TimingMath::psidPlayPeriodSamplesFromCycles(
            chunkPlayPhi2Cycles, sr, static_cast<uint32_t>(player->platform().clockHz()));
        const double playPeriod = std::max(1.0, currentPlayPeriod);
        c64PsidPlayPeriodSamplesCache_ = playPeriod;
        const int maxChunk = std::max(1, static_cast<int>(
            std::floor(playPeriod * static_cast<double>(kC64PsidMaxPlayCallsPerAudioBlock))));
        if (numFrames <= maxChunk) {
            return renderC64PsidBlockIfActive_(outputs, numFrames);
        }
        // Chunk: split into sub-blocks.
        float* chunkOutputs[2] = { outputs[0], outputs[1] };
        int offset = 0;
        bool anyActive = false;
        while (offset < numFrames) {
            const int chunkFrames = std::min(maxChunk, numFrames - offset);
            float* L = outputs[0] ? outputs[0] + offset : nullptr;
            float* R = (outputs[1] && outputs[1] != outputs[0]) ? outputs[1] + offset : nullptr;
            chunkOutputs[0] = L; chunkOutputs[1] = R;
            if (renderC64PsidBlockIfActive_(chunkOutputs, chunkFrames)) anyActive = true;
            offset += chunkFrames;
        }
        return anyActive;
    }
    double c64PsidPlayPeriodSamplesCache_ = 882.0;  // updated each render block

    bool renderC64SidPlayerIdleIfNeeded_(float** outputs, int numFrames) noexcept {
        if (componentFlavor_ != ArpSID::ComponentFlavor::C64SidPlayer) return false;
        if (!outputs || !outputs[0] || numFrames <= 0) return true;
        std::memset(outputs[0], 0, static_cast<size_t>(numFrames) * sizeof(float));
        if (outputs[1] && outputs[1] != outputs[0]) {
            std::memset(outputs[1], 0, static_cast<size_t>(numFrames) * sizeof(float));
        }
        c64BlockPlayCalls_ = 0;
        c64TelemetryGate_.clear();
        return true;
    }

    void clearC64BypassedPerformanceState_() noexcept {
        clearLivePerformanceMirrors_(true);
        runtimeModel_.clearTransientEvents();
        runtimeModel_.clearLiveMidiState();
        runtimeModel_.clearIdentityMirrors();
        if (bpe_() && bpe_()->hasActiveVoices()) bpe_()->allNotesOff();
        if (drs_() && drs_()->isActive()) drs_()->allNotesOff();
        if (arp_()) arp_()->allNotesOff();
        if (auto* vp = runtimeVoicePolicy_()) vp->clearAllNotesOffStateOnly();
        for (auto& v : engineBank_.synthVoices) v.reset();
        engineBank_.sidWriteQueue.clear();
        engineBank_.pedalState.clear();
        clearDrumBridgePerformanceState_();
        if (digiSampler_.isActive()) digiSampler_.allNotesOff();
        if (digiD418_.isActive()) digiD418_.allNotesOff();
    }

    // v855 P1.6 realtime cleanliness: a steady_clock wall-clock read is a
    // syscall-adjacent operation and does not belong on the audio render
    // thread in a shipping build. The v840 stall-instrumentation plumbing is
    // kept, but the actual clock sampling is compiled out unless
    // ARPSID_ENABLE_RENDER_PROFILING=1 is defined; when disabled every phase
    // duration reads as 0 µs (honest "profiling gated" telemetry).
#ifndef ARPSID_ENABLE_RENDER_PROFILING
#define ARPSID_ENABLE_RENDER_PROFILING 0
#endif
    static std::chrono::steady_clock::time_point renderProfileNow_() noexcept {
#if ARPSID_ENABLE_RENDER_PROFILING
        return std::chrono::steady_clock::now();
#else
        return std::chrono::steady_clock::time_point{};
#endif
    }

    bool renderC64SidplayPathIfActive_(float** outputs,
                                       int numFrames,
                                       std::chrono::steady_clock::time_point callbackT0,
                                       std::chrono::steady_clock::time_point preC64T1) noexcept {
        const auto c64T0 = preC64T1;
        const bool rendered = renderC64PsidBlockChunkedIfActive_(outputs, numFrames);
        const bool idle = rendered ? false : renderC64SidPlayerIdleIfNeeded_(outputs, numFrames);
        if (!rendered && !idle) return false;

        const auto c64T1 = renderProfileNow_();
        noteC64PhaseMicros_(callbackT0, preC64T1,
                             c64PreC64LastBlockMicros_, c64PreC64MaxBlockMicros_);
        noteC64RenderTiming_(c64T0, c64T1, numFrames);

        const auto postT0 = c64T1;
        if (!c64BypassedPerformanceStateCleared_) {
            clearC64BypassedPerformanceState_();
            c64BypassedPerformanceStateCleared_ = true;
        }
        // Keep C64 SID Player as a pure player path while debugging Logic-side
        // distortion/sluggishness. The normal synth/drum/SIDCore path still runs
        // HiFi when enabled; C64P publishes an explicit bypass state instead.
        publishHiFiBypassTelemetry_();
        finishSidCoreBlockTimeline_(numFrames, rendered);
        const auto postT1 = renderProfileNow_();
        noteC64PhaseMicros_(postT0, postT1,
                             c64PostC64LastBlockMicros_, c64PostC64MaxBlockMicros_);

        const auto telemetryT0 = postT1;
        updateC64SidplayTelemetryLight_(outputs, numFrames);
        const auto telemetryT1 = renderProfileNow_();
        noteC64PhaseMicros_(telemetryT0, telemetryT1,
                             c64TelemetryLastBlockMicros_, c64TelemetryMaxBlockMicros_);
        noteC64CallbackTiming_(callbackT0, telemetryT1, numFrames);
        return true;
    }

    // v966: sequencer tempo/length laws live in math_utils.h so host display
    // and text parsing invert the identical mapping the kernel renders with.
    static int seqLengthFromNorm_(float norm) noexcept {
        return ArpSID_normToSeqSteps(norm);
    }
    static float seqTempoBpmFromNorm_(float norm) noexcept {
        return ArpSID_normToSeqTempoBpm(norm);
    }
    static SeqTraversalMode seqTraversalModeFromNorm_(float norm) noexcept {
        const int idx = std::clamp((int)std::lround(std::clamp(std::isfinite(norm) ? norm : 0.0f, 0.0f, 1.0f) * 3.0f), 0, 3);
        switch (idx) {
            case 1: return SeqTraversalMode::Reverse;
            case 2: return SeqTraversalMode::PingPong;
            case 3: return SeqTraversalMode::Random;
            default: return SeqTraversalMode::Forward;
        }
    }
    bool enforceComponentFlavorPolicy_() noexcept {
        bool changed = false;
        const auto forceParam = [&](int pid, float target) noexcept {
            const size_t idx = static_cast<size_t>(pid);
            const float clean = ArpSID::sanitizeNormalizedParamValue(pid,
                                                                     target,
                                                                     ArpSID::defaultNormalizedParamValue(pid));
            const float atomicValue = params_[idx].load(std::memory_order_relaxed);
            const float modelValue = ArpSID::sidStateRootParamValue(runtimeModel_.stateRoot(), pid);
            if (std::fabs(atomicValue - clean) <= 1.0e-6f &&
                std::fabs(renderParams_[idx] - clean) <= 1.0e-6f &&
                std::fabs(modelValue - clean) <= 1.0e-6f) {
                return;
            }
            // v943: flavor enforcement is a structural authority write, not a
            // render-array patch. Stage through the canonical helper so params_,
            // renderParams_ and runtimeModel_.stateRoot() cannot split-brain.
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(pid), clean);
            changed = true;
        };
        switch (componentFlavor_) {
            case ArpSID::ComponentFlavor::Instrument:
                // Dedicated instrument flavor is the classic Synth/SidRegister
                // instrument surface, not BitPerfect fallback. Keep this
                // structural mode stable across host reset snapshots.
                forceParam(kParamDrSidEnable, 0.0f);
                forceParam(kParamSynthModeEnable, 1.0f);
                forceParam(kParamArpEnable, 0.0f);
                forceParam(kParamSeqEnable, 0.0f);
                break;
            case ArpSID::ComponentFlavor::DrumMachine:
                forceParam(kParamSynthModeEnable, 0.0f);
                forceParam(kParamDrSidEnable, 1.0f);
                forceParam(kParamArpEnable, 0.0f);
                // v949: dedicated drum flavor owns DrSID/SID808 pattern
                // transport through SeqEnable. Do not clear it in flavor
                // enforcement; factory/root defaults still load it off.
                break;
            case ArpSID::ComponentFlavor::Sid808:
                forceParam(kParamSynthModeEnable, 0.0f);
                forceParam(kParamDrSidEnable, 1.0f);
                forceParam(kParamArpEnable, 0.0f);
                // v949: preserve SeqEnable for the SID808 drum sequencer.
                forceParam(kParamDrSidMachineModel, 1.0f);
                {
                    const int slot = ArpSID::canonicalFactorySlotFromNormalizedBankSlot(
                        renderParams_[(size_t)kParamBankSlot]);
                    if (!ArpSID::isSid808FactorySlot(slot)) {
                        forceParam(kParamBankSlot, ArpSID::canonicalNormalizedBankSlotValue(120));
                        forceParam(kParamProgram, ArpSID::canonicalNormalizedFactoryProgramValue(120));
                    }
                }
                break;
            case ArpSID::ComponentFlavor::C64SidPlayer:
                // Dedicated .sid player flavor: never fall through to the live synth,
                // DrSID, arp or sequencer authorities when no file is loaded.
                forceParam(kParamSynthModeEnable, 0.0f);
                forceParam(kParamDrSidEnable, 0.0f);
                forceParam(kParamArpEnable, 0.0f);
                forceParam(kParamSeqEnable, 0.0f);
                break;
            case ArpSID::ComponentFlavor::Hybrid:
            default:
                break;
        }
        return changed;
    }
    bool componentFlavorAllowsDrSidAutoPromotion_() const noexcept {
        // v909 Classic-mode authority: dedicated drum flavors always allow GM
        // promotion; the Hybrid (Classic) flavor requires the explicit
        // kParamAutoGmDrumPromotion opt-in so a user-selected Classic render
        // mode can never be hijacked by channel-10 notes; other flavors never.
        return ArpSID::sidCanonicalGMDrumAutoPromotionAllowed(
            ArpSID::componentFlavorIsDedicatedDrum(componentFlavor_),
            componentFlavor_ == ArpSID::ComponentFlavor::Hybrid,
            renderParams_[(size_t)kParamAutoGmDrumPromotion] > 0.5f);
    }
    uint32_t currentSequencerPatternSeed_() const noexcept {
        uint32_t seed = 0xA511E9B3u;
        const int length = seqLengthFromNorm_(renderParams_[(size_t)kParamSeqLength]);
        for (int step = 0; step < length; ++step) {
            const size_t base = (size_t)kParamSeqStep1Note + (size_t)step * 3u;
            const int note = std::clamp((int)std::lround(renderParams_[base] * 127.0f), 0, 127);
            const int vel = std::clamp((int)std::lround(renderParams_[base + 1] * 127.0f), 0, 127);
            const int gate = std::clamp((int)std::lround(renderParams_[base + 2] * 127.0f), 0, 127);
            seed ^= (uint32_t)(note + (vel << 8) + (gate << 16) + (step << 24));
            if (seed == 0u) seed = 0x6D2B79F5u;
            ArpSID_xorshift32(seed);
        }
        return seed == 0u ? 0x6D2B79F5u : seed;
    }
    void syncSequencerPatternFromParams_() noexcept {
        SeqPattern pattern{};
        pattern.length = seqLengthFromNorm_(renderParams_[(size_t)kParamSeqLength]);
        pattern.seed = currentSequencerPatternSeed_();
        for (int step = 0; step < kMaxSeqSteps; ++step) {
            const size_t base = (size_t)kParamSeqStep1Note + (size_t)step * 3u;
            SeqStep seqStep{};
            seqStep.midiNote = (int16_t)std::clamp((int)std::lround(renderParams_[base] * 127.0f), 0, 127);
            seqStep.velocity = std::clamp(renderParams_[base + 1], 0.0f, 1.0f);
            seqStep.gate = std::clamp(renderParams_[base + 2], 0.0f, 1.0f);
            seqStep.active = seqStep.gate > 0.01f;
            seqStep.accent = seqStep.velocity >= 0.92f;
            pattern.steps[step] = seqStep;
        }
        pattern.sanitize();
        seqEngine_.setPattern(pattern);
        seqEngine_.setTraversalMode(seqTraversalModeFromNorm_(renderParams_[(size_t)kParamSeqMode]));
        seqEngine_.setStepsPerBeat(4.0f);
    }
    void syncSequencerCursorToBeat_(double beatPosition) noexcept {
        const double safeBeat = (std::isfinite(beatPosition) && beatPosition >= 0.0) ? beatPosition : 0.0;
        const double syncBeat = sid808QuantizedSequencerSyncBeat_(safeBeat);
        seqEngine_.syncToBeatPosition(syncBeat);
        runtimeModel_.setSeqStep(seqEngine_.currentStep());
    }
    void armSequencerTransportRestart_(double beatPosition) noexcept {
        const double safeBeat = (std::isfinite(beatPosition) && beatPosition >= 0.0) ? beatPosition : 0.0;
        const double syncBeat = sid808QuantizedSequencerSyncBeat_(safeBeat);
        seqInternalBeatPosition_ = syncBeat;
        seqEngine_.syncToBeatPosition(syncBeat);
        runtimeModel_.setSeqStep(seqEngine_.currentStep());
        seqTransportRestartBeat_ = syncBeat;
        seqTransportRestartPending_ = true;
    }
    void clearSequencerTransportRestart_() noexcept {
        seqTransportRestartPending_ = false;
        seqTransportRestartBeat_ = 0.0;
    }
    double sid808QuantizedSequencerSyncBeat_(double beatPosition) const noexcept {
        const double safeBeat = (std::isfinite(beatPosition) && beatPosition >= 0.0) ? beatPosition : 0.0;
        if (componentFlavor_ != ArpSID::ComponentFlavor::Sid808) return safeBeat;
        constexpr double kSid808StepGridBeats = 0.25; // 16th-note x0x grid at 4 steps/beat.
        return std::floor((safeBeat + 1.0e-9) / kSid808StepGridBeats) * kSid808StepGridBeats;
    }
    void resetTransientRenderState_(bool clearPendingIngress) noexcept {
        if (clearPendingIngress) clearQueuedFactoryPatchIngress_();
        kitSequencerLastStep_ = -1;
        runtimeModel_.clearTransientEvents();
        runtimeModel_.setSeqLastNote(-1);
        prevVirtualGate_ = false;
        prevVirtualNoteId_ = -1;
        auLimiter_.reset();
        auReverb_.reset();
        hifiTranscendence_.reset();
        smoothedReverbMix_ = std::clamp(ArpSID_sanitizeFloat(auReverbMix_), 0.0f, 1.0f);
        smoothedLimiterWet_ = auLimiterEnabled_ ? 1.0f : 0.0f;
        smoothedLimiterThreshold_ = std::clamp(ArpSID_sanitizeFloat(auLimiterThreshold_), 0.05f, 1.5f);
        auPostFxSilentFrames_ = 0u;
        engineBank_.sidWriteQueue.clear();
        std::memset(runtimeFractionalAccumL_, 0, sizeof(runtimeFractionalAccumL_));
        std::memset(runtimeFractionalAccumR_, 0, sizeof(runtimeFractionalAccumR_));
        std::memset(runtimeFractionalWeight_, 0, sizeof(runtimeFractionalWeight_));
        std::memset(runtimeFractionalActive_, 0, sizeof(runtimeFractionalActive_));
    }
    void clearRuntimeStateForTransportStart_() noexcept {
        // Transport Play is a hard runtime boundary, not a preset boundary.
        // Clear render-visible transient state, pending ingress, held/voice mirrors,
        // FX tails and queued SID writes so Stop→Play cannot replay stale notes,
        // CC fallbacks, pedal state or post-FX history. Do not touch persistent
        // patch/program/bank authority or host parameter values.
        runtimeApplyAllNotesOffPerformanceReset_(-1, true);
        neutralizeTransportTransientHostControls_();
        // v910 ring-ingress fix: do NOT flush the live midiQueue_/paramIntentQueue_
        // here (clearPendingIngress=false). Those rings hold fresh live host/UI
        // input from at most the last block; flushing them on the Play edge
        // silently ate any note or knob move that arrived as the transport
        // started (the events[] path was unaffected — a 42x ingress-path audio
        // mismatch). Factory-root restore still scrubs the queues via
        // clearQueuedFactoryPatchIngress_() on its own path.
        resetTransientRenderState_(false);
        for (auto& v : engineBank_.synthVoices) v.reset();
        // Transport Play boundary: release notes but do NOT hard-reset the DrSID
        // engine, which would silence a cowbell/tom hit landing on the same block
        // (fractional-render zeroing of just-reset choke-family swept voices).
        ArpSID::runtimeRenderHostResetEngines(*this, /*resetDrSid=*/false);
        syncSidSystemModelFromParams_(true);
        syncSidQueuedShadowFromLive_();
        sanitizeSynthModeQueuedShadow_();
        prevVirtualGate_ = false;
        prevVirtualNoteId_ = -1;
    }

    void resetRenderModeTransitionRuntime_() noexcept {
        resetTransientRenderState_(false);
        // v940: mode transitions are authority boundaries; scrub inactive SEQ
        // cursor/countdown state even if raw SeqEnable later becomes effective.
        runtimeModel_.setSeqLastNote(-1);
        runtimeModel_.setSeqSamplesUntilStep(-1.0);
        runtimeModel_.setSeqStep(0);
        seqEngine_.resetPhase();
        prevSeqEnabled_ = false;
        const bool effectiveSeqAfterTransition =
            ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_);
        if (effectiveSeqAfterTransition &&
            runtimeHostSurface_().transportPlaying && runtimeModel_.followHostTempoSeq()) {
            const double hostBeat = (std::isfinite(runtimeHostSurface_().hostBeatPosition) &&
                                     runtimeHostSurface_().hostBeatPosition >= 0.0)
                                        ? runtimeHostSurface_().hostBeatPosition
                                        : 0.0;
            armSequencerTransportRestart_(hostBeat);
        } else {
            if (!runtimeHostSurface_().transportPlaying) seqInternalBeatPosition_ = 0.0;
            clearSequencerTransportRestart_();
            syncSequencerCursorToBeat_(seqInternalBeatPosition_);
            if (!runtimeHostSurface_().transportPlaying) runtimeModel_.setSeqStep(0);
        }
    }
    int synthModeVoiceMode_() const noexcept {
        return ArpSID::canonicalVoiceModeIndexFromNormalized(renderParams_[(size_t)kParamVoiceMode]);
    }
    void syncSidSystemModelFromParams_(bool force = false) noexcept {
        const auto variant = runtimeModel_.variantProfile();
        const bool selected6581 = ArpSID::sidChipRevisionSelectorIs6581(renderParams_[(size_t)kParamSidChipRevision]);
        const bool adsrBug = selected6581 || renderParams_[(size_t)kParamSidAdsrBug6581] > 0.5f;
        uint8_t sysb = ArpSID::sidSystemByteFromVariantProfile(variant, adsrBug);
        if (selected6581) sysb &= static_cast<uint8_t>(~0x02u); else sysb |= 0x02u;
        if (force || sysb != sidSysByteCached_) {
            sidSysByteCached_ = sysb;
            runtimeModel_.importPseudoSystemSnapshot(sysb);
            if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(false);
        }
        if (0x19u < ArpSID::kSidRuntimeRegisterCount) {
            sidQueuedShadow_.value[0x19u] = sysb;
            sidQueuedShadow_.valid[0x19u] = 1u;
        }
    }
    void syncSidQueuedShadowFromLive_() noexcept {
        // P2 FIX: Use the shared shadow-ops helper instead of raw array assignment.
        ArpSID::syncRegisterShadowFromLive(sreg_(), sidQueuedShadow_);
    }
    void mirrorSidCoreShadowWrite_(uint8_t reg,
                                   uint8_t value,
                                   uint16_t sampleOffset,
                                   uint16_t cycleOffset) noexcept {
        const size_t idx = static_cast<size_t>(reg);
        if (idx >= sidQueuedShadow_.value.size()) return;
        sidQueuedShadow_.value[idx] = value;
        sidQueuedShadow_.valid[idx] = 1u;
        sidQueuedShadow_.sample[idx] = sampleOffset;
        sidQueuedShadow_.cycle[idx] = cycleOffset;
    }
    void beginSidCoreBlockTimeline_(int /*numFrames*/) noexcept {
        sidCoreBlockSampleBase_ = sidCoreSampleCursor_;
    }
    uint64_t sidCoreWriteStamp_(int sampleOffset = 0) const noexcept {
        const uint64_t local = sampleOffset > 0 ? static_cast<uint64_t>(sampleOffset) : 0u;
        return (local > UINT64_MAX - sidCoreBlockSampleBase_)
            ? UINT64_MAX
            : (sidCoreBlockSampleBase_ + local);
    }
    void finishSidCoreBlockTimeline_(int numFrames, bool publishSnapshot = true) noexcept {
        ++sidCoreBlockIndex_;
        const uint64_t frames = numFrames > 0 ? static_cast<uint64_t>(numFrames) : 0u;
        sidCoreSampleCursor_ = (frames > UINT64_MAX - sidCoreSampleCursor_)
            ? UINT64_MAX
            : (sidCoreSampleCursor_ + frames);
        if (publishSnapshot) publishSidCoreLiveSnapshot_();
    }
    void sanitizeSynthModeQueuedShadow_() noexcept {
        for (int v = 0; v < 3; ++v) {
            const int base = v * 7;
            const bool gateOn = ((size_t)(base + 4) < sidQueuedShadow_.value.size()) &&
                                (sidQueuedShadow_.value[(size_t)(base + 4)] & 0x01u) != 0u;
            if ((size_t)(base + 4) < sidQueuedShadow_.value.size()) {
                sidQueuedShadow_.value[(size_t)(base + 4)] =
                    ArpSID::synthModeControlByteForVoice(renderParams_.data(), nullptr, v, gateOn);
                sidQueuedShadow_.valid[(size_t)(base + 4)] = 1u;
            }
        }
        if (0x19u < ArpSID::kSidRuntimeRegisterCount) {
            sidQueuedShadow_.value[0x19u] = sidSysByteCached_;
            sidQueuedShadow_.valid[0x19u] = 1u;
        }
    }
    bool mirrorSidProjectionWriteToC64_(uint8_t reg, uint8_t value, uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
        // v880 closure: convert host-sample offsets against the already-prepared
        // cosmetic C64 mirror clock, not a parallel runtimePhysicalSidClockHz()
        // inference. beginC64TelemetryDemandBlock_() calls
        // ensureC64ProjectionMirrorClockReady_() before opening the observer, so
        // c64Platform_.clockHz() is the exact authority that will later advance and
        // apply these tagged bus events. This prevents PAL/NTSC split-brain where
        // observer writes are scheduled with one PHI2 rate and consumed by another.
        uint32_t mirrorClock = c64Platform_.clockHz();
        if (mirrorClock != ArpSID::C64::kPalPhi2Hz && mirrorClock != ArpSID::C64::kNtscPhi2Hz) {
            mirrorClock = resolveC64ProjectionMirrorPal_() ? ArpSID::C64::kPalPhi2Hz
                                                           : ArpSID::C64::kNtscPhi2Hz;
        }
        const double phi2 = static_cast<double>(mirrorClock);
        // Preserve both host sample and cycle offsets when mirroring projection intent
        // into the C64 bus layer. Earlier versions only used cycleOffset, which made
        // same-block HUD/C64 mirror timing collapse events from later samples onto the
        // current PHI2 slot. The audio queue remains audible authority; this mirror is
        // a timed C64 bus reflection of the same intent.
        return ArpSID::C64::projectSidHostTimedWriteThroughC64Bus(c64Platform_,
                                                                  reg,
                                                                  value,
                                                                  sampleOffset,
                                                                  cycleOffset,
                                                                  std::max(1.0, sampleRate_),
                                                                  phi2);
    }

    void pushSidWriteTimed_(uint8_t reg, uint8_t value, uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
        ArpSID::pushRegisterShadowWrite(engineBank_.sidWriteQueue, sidQueuedShadow_, reg, value, sampleOffset, cycleOffset);
        // The C64 telemetry mirror is NO LONGER fed from here. Mirroring only a subset of the
        // projection call sites (this idempotent seed path) left the scheduler/performance
        // writes (note-on/off, glide, pitch bend, aftertouch) out of the mirror. It is now
        // driven from the applied-write observer of renderSidRegisterQueueToStereo(), so the
        // mirror reflects EXACTLY the writes the audio engine consumed — complete and
        // single-sourced (v874 audit: projection write-authority mirror completeness). The
        // audio queue still owns exact sample/cycle dispatch and is the audible authority.
    }
    void pushSidWriteTimedIfChanged_(uint8_t reg, uint8_t value, uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
        // P2 FIX: Only skip the write when the shadow entry is valid and matches.
        if ((size_t)reg < sidQueuedShadow_.value.size() &&
            sidQueuedShadow_.valid[(size_t)reg] &&
            sidQueuedShadow_.value[(size_t)reg] == value) return;
        pushSidWriteTimed_(reg, value, sampleOffset, cycleOffset);
    }
    uint8_t synthModeVoiceCtrlNoGate_(int voiceIndex) const noexcept {
        return ArpSID::resolveSynthModeControlNoGate(renderParams_.data(), sidQueuedShadow_.value.data(), voiceIndex);
    }
    void pushSynthModeVoiceSeedRealtime_(int voiceIndex, bool gateOn, int sampleOffset, uint16_t cycleOffset) noexcept {
        if (voiceIndex < 0 || voiceIndex >= 3) return;
        const int base = voiceIndex * 7;
        const auto& sv = engineBank_.synthVoices[(size_t)voiceIndex];
        const uint8_t ctrlNoGate = ArpSID::resolveSynthModeControlNoGate(renderParams_.data(), sidQueuedShadow_.value.data(), voiceIndex);
        const uint8_t ctrl = static_cast<uint8_t>(ctrlNoGate | (gateOn ? 0x01u : 0x00u));
        const uint16_t freqReg = (sv.currentSidFreqReg != 0u) ? sv.currentSidFreqReg : sv.targetSidFreqReg;
        if (freqReg != 0u) {
            pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 0), static_cast<uint8_t>(freqReg & 0xFFu), static_cast<uint16_t>(sampleOffset), cycleOffset);
            pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 1), static_cast<uint8_t>(freqReg >> 8), static_cast<uint16_t>(sampleOffset), cycleOffset);
        }
        const int pwPid = (voiceIndex == 0) ? kParamVCO1PulseWidth : (voiceIndex == 1) ? kParamVCO2PulseWidth : kParamVCO3PulseWidth;
        const float pwNorm = std::isfinite(renderParams_[(size_t)pwPid]) ? std::clamp(renderParams_[(size_t)pwPid], 0.0f, 1.0f) : 0.5f;
        const uint16_t pw12 = static_cast<uint16_t>(std::clamp((int)std::lround(pwNorm * 4095.0f), 0, 4095));
        pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 2), static_cast<uint8_t>(pw12 & 0xFFu), static_cast<uint16_t>(sampleOffset), cycleOffset);
        pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 3), static_cast<uint8_t>((pw12 >> 8) & 0x0Fu), static_cast<uint16_t>(sampleOffset), cycleOffset);
        const uint8_t attNib = (uint8_t)std::clamp((int)std::lround(std::isfinite(renderParams_[(size_t)kParamAttack]) ? renderParams_[(size_t)kParamAttack] * 15.f : 0.f), 0, 15);
        const uint8_t decNib = (uint8_t)std::clamp((int)std::lround(std::isfinite(renderParams_[(size_t)kParamDecay]) ? renderParams_[(size_t)kParamDecay] * 15.f : 0.f), 0, 15);
        const uint8_t susNib = (uint8_t)std::clamp((int)std::lround(std::isfinite(renderParams_[(size_t)kParamSustain]) ? renderParams_[(size_t)kParamSustain] * 15.f : 0.f), 0, 15);
        const uint8_t relNib = (uint8_t)std::clamp((int)std::lround(std::isfinite(renderParams_[(size_t)kParamRelease]) ? renderParams_[(size_t)kParamRelease] * 15.f : 0.f), 0, 15);
        pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 5), static_cast<uint8_t>((attNib << 4) | decNib), static_cast<uint16_t>(sampleOffset), cycleOffset);
        pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 6), static_cast<uint8_t>((susNib << 4) | relNib), static_cast<uint16_t>(sampleOffset), cycleOffset);
        pushSidWriteTimedIfChanged_(static_cast<uint8_t>(base + 4), ctrl, static_cast<uint16_t>(sampleOffset), cycleOffset);
    }
    uint8_t computeSynthModeFilterRouteLowNibble_() const noexcept {
        return runtimeModel_.synthFilterRouteLowNibble();
    }
    void pushSynthModeFilterRegsRealtime_(int sampleOffset, uint16_t cycleOffset) noexcept {
        const auto normOrZero = [](float v) noexcept -> float {
            return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
        };
        const float cutNorm = normOrZero(renderParams_[(size_t)kParamFilterCutoff]);
        const float resNorm = normOrZero(renderParams_[(size_t)kParamFilterResonance]);
        const float volNorm = normOrZero(renderParams_[(size_t)kParamMasterVolume]);
        const float modNorm = normOrZero(renderParams_[(size_t)kParamFilterMode]);
        const uint16_t fc = (uint16_t)std::clamp((int)std::lround(cutNorm * 2047.f), 0, 2047);
        pushSidWriteTimedIfChanged_(0x15u, (uint8_t)(fc & 0x07u), (uint16_t)sampleOffset, cycleOffset);
        pushSidWriteTimedIfChanged_(0x16u, (uint8_t)((fc >> 3) & 0xFFu), (uint16_t)sampleOffset, cycleOffset);
        const uint8_t resNib = (uint8_t)std::clamp((int)std::lround(resNorm * 15.f), 0, 15);
        pushSidWriteTimedIfChanged_(0x17u, (uint8_t)((resNib << 4) | computeSynthModeFilterRouteLowNibble_()), (uint16_t)sampleOffset, cycleOffset);
        const uint8_t volNib = (uint8_t)std::clamp((int)std::lround(volNorm * 15.f), 0, 15);
        const int modeIdx = (int)std::lround(modNorm * 2.f);
        const uint8_t modeBits = (modeIdx == 0) ? 0x10u : (modeIdx == 1) ? 0x20u : 0x40u;
        pushSidWriteTimedIfChanged_(0x18u, (uint8_t)(modeBits | volNib), (uint16_t)sampleOffset, cycleOffset);
    }
    void reseedSynthModeRealtimeState_(int sampleOffset, uint16_t cycleOffset, bool activeOnly) noexcept {
        syncSidSystemModelFromParams_(true);
        syncSidQueuedShadowFromLive_();
        sanitizeSynthModeQueuedShadow_();
        pushSynthModeFilterRegsRealtime_(sampleOffset, cycleOffset);
        for (int v = 0; v < 3; ++v) {
            const auto& sv = engineBank_.synthVoices[(size_t)v];
            if (activeOnly && !sv.active) continue;
            const bool gateOn = activeOnly ? (sv.active && (sv.keyDown || sv.sustained || sv.sostenutoLatched)) : false;
            pushSynthModeVoiceSeedRealtime_(v, gateOn, sampleOffset, cycleOffset);
        }
    }

    void loadC64SidProjectionBootstrap_() noexcept {
        // Deterministic tiny C64 program used by the GUI Control Hub. It is not
        // a second audio authority: it exercises the same 6510 -> $D400 ->
        // C64Platform::sidRegisterImage() path used by PSID/RSID and SID
        // projection telemetry.
        const bool pal = runtimePhysicalSidClockHz() <= 1000000.0;
        c64Platform_.reset(pal);
        c64PlatformClock_.reset();
        c64RealtimeCycleDebt_ = 0;
        // 0801: LDA #$11 ; STA $D400 ; LDA #$0F ; STA $D418 ; JMP $0801
        const uint16_t pc = 0x0801u;
        const uint8_t program[] = {
            0xA9u, 0x11u, 0x8Du, 0x00u, 0xD4u,
            0xA9u, 0x0Fu, 0x8Du, 0x18u, 0xD4u,
            0x4Cu, 0x01u, 0x08u
        };
        for (uint16_t i = 0; i < static_cast<uint16_t>(sizeof(program)); ++i) {
            c64Platform_.pokeMemory(static_cast<uint16_t>(pc + i), program[i]);
        }
        c64Platform_.roms().pokeKernal(0xFFFCu, static_cast<uint8_t>(pc & 0xFFu));
        c64Platform_.roms().pokeKernal(0xFFFDu, static_cast<uint8_t>(pc >> 8u));
    }

    void hardSynthModeVoiceOff_(int voiceIndex, uint16_t sampleOff, uint16_t cycleOff, bool clearTracking) noexcept {
        if (voiceIndex < 0 || voiceIndex >= 3) return;
        if (clearTracking) engineBank_.synthVoices[(size_t)voiceIndex].reset();
        const int base = voiceIndex * 7;
        pushSidWriteTimed_(static_cast<uint8_t>(base + 4), synthModeVoiceCtrlNoGate_(voiceIndex), sampleOff, cycleOff);
    }

    void reconcileSynthModeUnheldVoices_() noexcept {
        auto* vp = runtimeVoicePolicy_();
        if (!vp) return;
        const bool directSynth =
            ArpSID::sidResolveRenderModeFromLiveParams(renderParams_) == ArpSID::SidRuntimeRenderMode::SidRegister;
        if (!directSynth) {
            vp->resetOrphanReconcile();
            return;
        }

        auto& hs = runtimeHostSurface_();
        (void)vp->reconcileUnheldVoices(
            [&hs](int ch, int note) noexcept -> bool {
                if (ch < 0 || ch > 15 || note < 0 || note > 127) return true;
                const uint32_t cg = hs.heldIngressChannelGeneration[(size_t)ch].load(std::memory_order_acquire);
                const uint32_t ng = hs.heldIngressNoteGeneration[(size_t)ch][(size_t)note].load(std::memory_order_acquire);
                const uint8_t d = hs.heldIngressDepth[(size_t)ch][(size_t)note].load(std::memory_order_acquire);
                return (ng == cg) && d > 0u;
            },
            [this](int ch) noexcept -> bool {
                if (ch < 0 || ch > 15) return false;
                return liveSustainPedalDown_[(size_t)ch].load(std::memory_order_acquire) != 0u ||
                       liveSostenutoPedalDown_[(size_t)ch].load(std::memory_order_acquire) != 0u ||
                       engineBank_.pedalState.sustainDown(ch) ||
                       engineBank_.pedalState.sostenutoByChannel[(size_t)ch] != 0u;
            },
            [this](int voiceIdx) noexcept {
                if (voiceIdx >= 0 && voiceIdx < static_cast<int>(engineBank_.synthVoices.size())) {
                    auto& sv = engineBank_.synthVoices[(size_t)voiceIdx];
                    sv.keyDown = false;
                    sv.sustained = false;
                    sv.sostenutoLatched = false;
                }
                hardSynthModeVoiceOff_(voiceIdx, 0u, 0u, false);
            });
    }

public:

    enum class C64ControlHubCommand : uint8_t {
        Boot = 1,
        Start = 2,
        Stop = 3,
        Reset = 4,
        LoadProjectionBootstrap = 5
    };

    void performC64ControlHubCommand(C64ControlHubCommand command) noexcept {
        switch (command) {
            case C64ControlHubCommand::Boot:
                c64Platform_.bootFromResetVector();
                break;
            case C64ControlHubCommand::Start:
                c64Platform_.startRealtimeSidCore();
                break;
            case C64ControlHubCommand::Stop:
                c64Platform_.stopRealtimeSidCore();
                break;
            case C64ControlHubCommand::Reset:
                c64Platform_.reset(runtimePhysicalSidClockHz() <= 1000000.0);
                c64PlatformClock_.reset();
        c64RealtimeCycleDebt_ = 0;
                c64Platform_.bootFromResetVector();
                break;
            case C64ControlHubCommand::LoadProjectionBootstrap:
                loadC64SidProjectionBootstrap_();
                c64Platform_.bootFromResetVector();
                c64Platform_.startRealtimeSidCore();
                break;
        }
    }


    //──────────────────────────────────────────────────────
    // Setup / teardown — NON-REALTIME ONLY; never concurrent with process().
    // All methods in this section allocate, delete, or copy heap-owning state.
    // Callers must ensure the render thread is not active (drain/suspend first).
    //──────────────────────────────────────────────────────
    // Fix 1.4: flush denormals to zero for ARM (FPCR) and x86 (MXCSR)
    static void flushDenormalsToZero_() noexcept {
#if defined(__aarch64__) || defined(__arm64__)
        uint64_t fpcr; __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
        fpcr |= (1ULL<<24);  // FZ — flush-to-zero
        fpcr |= (1ULL<<19);  // DN — default NaN
        __asm__ volatile("msr fpcr, %0" :: "r"(fpcr));
#elif defined(__SSE__)
        _mm_setcsr(_mm_getcsr() | 0x8040);  // DAZ | FTZ
#endif
    }

    static float realtimePowUnit_(float base, int exponent) noexcept {
        float result = 1.0f;
        float factor = std::clamp(ArpSID_sanitizeFloat(base, 1.0f), 0.0f, 1.0f);
        int n = std::max(0, exponent);
        while (n > 0) {
            if (n & 1) result *= factor;
            factor *= factor;
            n >>= 1;
        }
        return std::clamp(ArpSID_sanitizeFloat(result, 1.0f), 0.0f, 1.0f);
    }

    void refreshTelemetryPeakDecay_() noexcept {
        const double sr = std::max(1.0, ArpSIDSanitizeHostSampleRate(sampleRate_));
        telemetryPeakDecayPerSample_ = std::clamp(
            static_cast<float>(std::exp(std::log(0.001) / sr)),
            0.0f,
            1.0f);
    }


    void configureDefaultDrumBridgeIdentityNonRealtime_() noexcept {
        // No DrSID bridge identity in production. DrSID/DrumMachine audio is
        // owned only by canonical engineBank_.drSid. The bridge is the
        // production render authority only for SID808.
        const auto ctx = drumEngineBridge_.activeIdentity().context;
        if (componentFlavor_ == ArpSID::ComponentFlavor::Sid808 &&
            ctx != ArpSID::DrumContext::SID808_AnalogProjection) {
            const int slot = ArpSID::canonicalFactorySlotFromNormalizedBankSlot(
                renderParams_[(size_t)kParamBankSlot]);
            (void)drumEngineBridge_.loadFactorySlot(
                ArpSID::isSid808FactorySlot(slot) ? slot : 120);
        }
    }

    void syncDrumBridgeProjectionFromRenderParams_() noexcept {
        // SID808 is bridge-owned in production. Project only SID808-relevant
        // clock/chip-model/forensic settings into the bridge. Do not touch the
        // bridge-owned DrSidEngine here; DrSID/DrumMachine production audio has
        // one authority only: canonical engineBank_.drSid.
        const bool selectorIs6581 = ArpSID::sidChipRevisionSelectorIs6581(renderParams_[(size_t)kParamSidChipRevision]);
        const std::uint8_t selectorRevision = ArpSID::sidChipRevisionSelectorRevision(renderParams_[(size_t)kParamSidChipRevision]);
        const ArpSID::SIDModel selectedSidModel = selectorIs6581 ? ArpSID::SIDModel::MOS6581 : ArpSID::SIDModel::MOS8580;

        drumEngineBridge_.setSidModel(selectedSidModel);
        drumEngineBridge_.setClockFrequency(currentSidClockHz_());

        ArpSID::ArpSIDForensicConfig fc = ArpSID::buildEffectiveForensicConfigFromParams(
            [&](ParamID pid) noexcept -> float { return renderParams_[(size_t)pid]; },
            runtimeModel_.variantProfile(),
            runtimeModel_.staticParams());
        fc.revision = selectorRevision;
        drumEngineBridge_.setForensicConfig(fc);
    }

    void setupNonRealtime(double sampleRate, int maxFrames) {
        setup(sampleRate, maxFrames);
    }
    void setup(double sampleRate, int /*maxFrames*/) {
        flushDenormalsToZero_();
        sampleRate_ = ArpSIDSanitizeHostSampleRate(sampleRate);
        refreshTelemetryPeakDecay_();
        seqInternalBeatPosition_ = 0.0;
        kitSequencerLastStep_ = -1;
        clearSequencerTransportRestart_();
        prevSeqEnabled_ = false;
        prevSeqFollowHost_ = true;
        prevSeqLength_ = 8;
        prevSeqModeIndex_ = 0;
        digiSampler_.prepare(sampleRate_);
        // Prepare authentic DIGI engine and open-bus latch from the active
        // variant clock so PAL and NTSC share one physical timing authority.
        digiD418_.prepare(sampleRate_, currentSidClockHz_());
        digiD418Sid_.prepare(sampleRate_);
        digiD418Sid_.setD418VolumeDacEmulation(true);
        digiD418Sid_.writeSystemByte(sreg_().currentSystemByte());
        digiPhi2Counter_ = 0u;
        digiPhi2Remainder_ = 0.0;
        digiD418Bridge_.reset();
        digiD418Bridge_.engine = nullptr;
        digiD418Bridge_.mirrorSink = nullptr;
        digiD418Bridge_.deferEngineWrites = true;
        digiOpenBus_.powerOn();
        digiD418RuntimePolicyGenerationRender_ = digiD418RuntimePolicyGeneration_.load(std::memory_order_acquire);
        createEngines_();
        drumEngineBridge_.prepare(sampleRate_);  // A2: init bridge after canonical DrSID exists
        configureDefaultDrumBridgeIdentityNonRealtime_();
        c64Platform_.reset(true);
        c64Platform_.bootFromResetVector();
        c64Platform_.startRealtimeSidCore();
        c64PlatformClock_.reset();
        c64RealtimeCycleDebt_ = 0;
        setSampleRate(sampleRate_);
        applyPendingAudioEngineMode_();
        if (auto* vp = runtimeVoicePolicy_()) {
            vp->setSustainGateOffCallback([](void* ctx, int voiceIdx) noexcept {
                if (!ctx) return;
                auto* self = static_cast<ArpSIDDSPKernel*>(ctx);
                self->hardSynthModeVoiceOff_(voiceIdx, 0u, 0u, false);
            }, this);
        }
        runtimeModel_.setVariantProfile(sidDefaultVariantProfile());
        // audit P0.2: seed the loader-visible video-standard mirror at reset so a
        // PSID load issued before the first render block still sees the correct
        // PAL/NTSC fallback (steady-state updates come from updateTelemetry_).
        c64VideoStandardFallbackAtomic_.store(
            (runtimeModel_.variantProfile().video_standard == ArpSID::SidVideoStandard::NTSC) ? 1u : 0u,
            std::memory_order_release);
        (void)enforceComponentFlavorPolicy_();
        flushDirtyParams_(true);
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(false);
    }

    void setComponentFlavor(int flavor) noexcept {
        const int clamped = std::clamp(flavor, 0, 4);
        componentFlavor_ = static_cast<ArpSID::ComponentFlavor>(clamped);
        (void)enforceComponentFlavorPolicy_();
        configureDefaultDrumBridgeIdentityNonRealtime_();
    }

    void setSampleRateNonRealtime(double sr) { setSampleRate(sr); }
    void setSampleRate(double sr) {
        flushDenormalsToZero_();
        sampleRate_ = ArpSIDSanitizeHostSampleRate(sr);
        refreshTelemetryPeakDecay_();
        if (bpe_())  bpe_()->setSampleRate(sampleRate_);
        if (arp_())  arp_()->setSampleRate(sampleRate_);
        if (drs_())  drs_()->setSampleRate(sampleRate_);
        if (lfos_()) lfos_()->setSampleRate(sampleRate_);
        engineBank_.prepare(sampleRate_);
        digiSampler_.prepare(sampleRate_);
        // Reinitialise authentic DIGI engine and open bus latch on rate change
        digiD418_.prepare(sampleRate_, currentSidClockHz_());
        digiD418Sid_.prepare(sampleRate_);
        digiD418Sid_.setD418VolumeDacEmulation(true);
        digiD418Sid_.writeSystemByte(sreg_().currentSystemByte());
        digiPhi2Counter_ = 0u;
        digiPhi2Remainder_ = 0.0;
        digiD418Bridge_.reset();
        digiD418Bridge_.engine = nullptr;
        digiD418Bridge_.mirrorSink = nullptr;
        digiD418Bridge_.deferEngineWrites = true;
        digiOpenBus_.powerOn();
        digiD418RuntimePolicyGenerationRender_ = digiD418RuntimePolicyGeneration_.load(std::memory_order_acquire);
        syncSidQueuedShadowFromLive_();
        ArpSID::prewarmAllSidTables();
        // P2 FIX: Re-init reverb and limiter whenever sample rate changes.
        auReverb_.init(sampleRate_);
        syncHiFiTranscendenceForBlock_();
        auLimiter_.setAttackMs(auLimiterAttackMs_, sampleRate_);
        auLimiter_.setReleaseMs(auLimiterReleaseMs_, sampleRate_);
        auPostFxSilentFrames_ = 0u;
        prepareMixFxProcessors_();  // Fix #8: re-prepare at each sample-rate change
        drumEngineBridge_.prepare(sampleRate_);  // A2: bridge follows sample rate
        configureDefaultDrumBridgeIdentityNonRealtime_();
        syncDrumBridgeProjectionFromRenderParams_();
    }

    void teardownResetNonRealtime() noexcept { teardownReset(); }
    void teardownReset() noexcept {
        ArpSID::runtimeRenderHostResetEngines(*this);
        // Host/AU reset must preserve the loaded patch. Clear only transient runtime
        // state, then force a backend reprojection of the current presentation state.
        runtimeModel_.clearTransientEvents();
        runtimeModel_.clearLiveMidiState();
        runtimeModel_.setTransportPlayingFlag(false);
        auLimiter_.reset();
        auReverb_.reset();
        hifiTranscendence_.reset();
        digiSampler_.reset();
        // Reset the authentic DIGI engine and open bus latch. Also clear
        // the running PHI2 cycle counter and telemetry so the next block
        // starts fresh.
        digiD418_.reset();
        digiD418Sid_.reset();
        digiD418Sid_.prepare(sampleRate_);
        digiD418Sid_.setD418VolumeDacEmulation(true);
        digiD418Sid_.writeSystemByte(sreg_().currentSystemByte());
        digiPhi2Counter_ = 0u;
        digiPhi2Remainder_ = 0.0;
        digiD418Bridge_.reset();
        digiD418Bridge_.engine = nullptr;
        digiD418Bridge_.mirrorSink = nullptr;
        digiD418Bridge_.deferEngineWrites = true;
        digiOpenBus_.powerOn();
        digiD418RuntimePolicyGenerationRender_ = digiD418RuntimePolicyGeneration_.load(std::memory_order_acquire);
        telemetryDigiD418WriteCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418WritesThisBlock_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418SidAcceptedWriteCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418SidAcceptedWritesThisBlock_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418WritesBlockedByIo_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418WriteQueueOverflow_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418CollisionCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418OpenBusDriveCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418TimelineDiscontinuityResetCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiAuthMode_.store(static_cast<std::uint8_t>(ArpSID::DigiAuthMode::StandaloneD418Layer), std::memory_order_relaxed);
        telemetryDigiD418LastNibble_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418LastOldD418_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418LastD418_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418LastOpenBus_.store(0xFFu, std::memory_order_relaxed);
        smoothedReverbMix_ = std::clamp(ArpSID_sanitizeFloat(auReverbMix_), 0.0f, 1.0f);
        smoothedLimiterWet_ = auLimiterEnabled_ ? 1.0f : 0.0f;
        smoothedLimiterThreshold_ = std::clamp(ArpSID_sanitizeFloat(auLimiterThreshold_), 0.05f, 1.5f);
        auPostFxSilentFrames_ = 0u;
        engineBank_.sidWriteQueue.clear();
        for (auto& v : engineBank_.synthVoices) v.reset();
        if (auto* vp = runtimeVoicePolicy_()) {
            vp->setSustainGateOffCallback([](void* ctx, int voiceIdx) noexcept {
                if (!ctx) return;
                auto* self = static_cast<ArpSIDDSPKernel*>(ctx);
                self->hardSynthModeVoiceOff_(voiceIdx, 0u, 0u, false);
            }, this);
        }
        clearAllPendingMidiFallbacks_();
        // audit P0.1: clearAllPendingMidiFallbacks_() already invalidates the
        // param-intent ring epoch; a second cross-thread clear() here would be
        // both redundant and an unsafe tail_ mutation on a live ring.
        ingressDropTelemetry_.store(0, std::memory_order_relaxed);
        paramIntentDirtyFlushFallbackTelemetry_.store(0, std::memory_order_relaxed);
        runtimeHostSurface_().resetIngress();
        runtimeHostSurface_().transportPlaying = false;
        c64Platform_.reset(runtimePhysicalSidClockHz() <= 1000000.0);
        c64Platform_.bootFromResetVector();
        c64Platform_.startRealtimeSidCore();
        c64PlatformClock_.reset();
        c64RealtimeCycleDebt_ = 0;
        runtimeHostSurface_().lastTransportPlaying = false;
        runtimeHostSurface_().hostBeatPosition = 0.0;
        seqInternalBeatPosition_ = 0.0;
        kitSequencerLastStep_ = -1;
        clearSequencerTransportRestart_();
        prevSeqEnabled_ = false;
        prevSeqFollowHost_ = runtimeModel_.followHostTempoSeq();
        prevSeqLength_ = seqLengthFromNorm_(renderParams_[(size_t)kParamSeqLength]);
        prevSeqModeIndex_ = std::clamp((int)std::lround(renderParams_[(size_t)kParamSeqMode] * 3.0f), 0, 3);
        seqEngine_.resetPhase();
        runtimeModel_.setSeqStep(0);
        prevVirtualGate_ = false;
        prevVirtualNoteId_ = -1;
        runtimeProjectionState_.firstApply = true;
        runtimeProjectionState_.lastFamily = runtimeModel_.variantProfile().family;
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(true);
        syncSidSystemModelFromParams_(true);
        syncSidQueuedShadowFromLive_();
        sanitizeSynthModeQueuedShadow_();
        // audit P0-4: drain any drum-bridge factory slot that a render-drained state
        // restore queued via queueSlotLoadNonRealtime(). teardownReset() is a
        // non-realtime / render-suspended path (reset/setup only), so this is the
        // safe place to perform the actual loadFactorySlot the render thread is
        // forbidden to call. No-op when nothing is queued.
        (void)drumEngineBridge_.applyQueuedSlotNonRealtime();
    }

    // audit P0-5: renamed from realtimeEngineResetPreserveIngress(), whose name was
    // a false promise — it does NOT preserve ingress. It hard-resets the realtime
    // engines AND performs a full runtimeModel_.reset(), which clears the model's
    // in-flight ingress (merge lanes + pending events) and wipes state_root_.
    //
    // This is exactly the right behavior for the deferred state-apply it serves
    // (applyStateRootCanonical): a full patch swap follows immediately via
    // applyStateRootBySwap (which also resets ingress), so the new state owns
    // ingress and stale in-flight events from the previous patch must not leak in.
    //
    // Held-note CONTINUITY is preserved separately and explicitly by the caller:
    // applyStateRootCanonical snapshots the host-surface held-ingress identity lanes
    // BEFORE this reset and re-arms them AFTER via sidReplayHeldNotes(). That host
    // surface is NOT touched here (runtimeRenderHostResetEngines resets engines/voice
    // policy only; runtimeModel_.reset() resets the model, not the host surface).
    void realtimeEngineHardResetForStateApply_() noexcept {
        ArpSID::runtimeRenderHostResetEngines(*this);
        runtimeModel_.reset();
        kitSequencerLastStep_ = -1;
    }

    using StateApplyReason = ArpSID::SidStateApplyReason;

    int sid808FactorySlotForRootNonRealtime_(const SidStateRootV1& root) const noexcept {
        const int slot = ArpSID::canonicalFactorySlotFromNormalizedBankSlot(
            ArpSID::sidStateRootParamValue(root, kParamBankSlot));
        if (componentFlavor_ != ArpSID::ComponentFlavor::Sid808) return -1;
        return ArpSID::isSid808FactorySlot(slot) ? slot : 120;
    }

    void preloadSid808FactorySlotForScheduledRestoreNonRealtime_(const SidStateRootV1& root) noexcept {
        const int slot = sid808FactorySlotForRootNonRealtime_(root);
        sid808PreloadedFactorySlotForRestore_v803_.store(slot, std::memory_order_release);
        Sid808PreparedBridgeSlot_& prepared = sid808PreparedBridgeSlotMailbox_.producerSlot();
        prepared.slot = slot;
        prepared.kit = (slot >= 0)
            ? ArpSID::factorySid808ResolvedKitForSlot(slot)
            : ArpSID::Sid808KitConfigTable{};
        sid808PreparedBridgeSlotMailbox_.publish();
    }

    bool applyPreparedSid808BridgeSlotRT_(int expectedSlot) noexcept {
        if (componentFlavor_ != ArpSID::ComponentFlavor::Sid808 ||
            !ArpSID::isSid808FactorySlot(expectedSlot)) {
            return false;
        }
        Sid808PreparedBridgeSlot_* prepared = sid808PreparedBridgeSlotMailbox_.tryConsume();
        if (!prepared || prepared->slot != expectedSlot) return false;
        return drumEngineBridge_.activatePreparedSid808SlotRealtime(prepared->slot, prepared->kit);
    }

    bool drainQueuedDrumBridgeSlotLoadNonRealtime() noexcept {
        sid808PreloadedFactorySlotForRestore_v803_.store(-1, std::memory_order_release);
        return drumEngineBridge_.applyQueuedSlotNonRealtime();
    }

    bool hasQueuedDrumBridgeSlotLoadNonRealtime() const noexcept {
        return drumEngineBridge_.hasPendingSlotLoad();
    }

    using StateOverlayPolicy = ArpSID::SidStateOverlayPolicy;

    void resetNonRealtime() { teardownReset(); }
    void reset() { teardownReset(); }

    void resetPreservingHostParameterSnapshotNonRealtime(const float* values,
                                                         int count,
                                                         int factorySlot,
                                                         bool hasExplicitFactorySlot) noexcept {
        resetPreservingHostParameterSnapshot(values, count, factorySlot, hasExplicitFactorySlot);
    }
    void resetPreservingHostParameterSnapshot(const float* values,
                                              int count,
                                              int factorySlot,
                                              bool hasExplicitFactorySlot) noexcept {
        // capture DrSID/SID-808 transport authority from both renderParams_.
        // AudioUnitReset is also issued by Logic at Stop/Play boundaries, so this
        // path must preserve audible DrSID edits instead of treating reset like a
        // fresh preset selection.
        // and the atomic AU/control image. Logic Stop->Play can arrive while one
        // side of the AUv2/AUv3 bridge is stale: renderParams_ is the audible
        // render-thread image, while params_ is the latest control/GUI image. A
        // stale host reset snapshot must not demote either one. Prefer the
        // audible render image when both sides are meaningful; otherwise keep the
        // non-default side instead of falling back to Logic's default snapshot.
        const auto captureTransportAuthorityParam = [this](int pid) noexcept -> float {
            const size_t idx = static_cast<size_t>(pid);
            const float def = kParamInfos[idx].defaultNorm;
            const float renderValue = ArpSID::sanitizeNormalizedParamValue(pid, renderParams_[idx], def);
            const float atomicValue = ArpSID::sanitizeNormalizedParamValue(pid,
                params_[idx].load(std::memory_order_relaxed), def);
            const bool renderMeaningful = std::fabs(renderValue - def) > 1.0e-6f;
            const bool atomicMeaningful = std::fabs(atomicValue - def) > 1.0e-6f;
            if (renderMeaningful) return renderValue;
            if (atomicMeaningful) return atomicValue;
            return renderValue;
        };
        const float renderDrSidEnableAuthorityMarker = renderParams_[(size_t)kParamDrSidEnable];
        (void)renderDrSidEnableAuthorityMarker;
        const float preResetDrSidEnableNorm = captureTransportAuthorityParam(kParamDrSidEnable);
        const float preResetSynthModeEnableNorm = captureTransportAuthorityParam(kParamSynthModeEnable);
        const float preResetDrSidModelFallbackNorm =
            ArpSID::sanitizeNormalizedParamValue(kParamDrSidMachineModel,
                                                 renderParams_[(size_t)kParamDrSidMachineModel],
                                                 kParamInfos[(size_t)kParamDrSidMachineModel].defaultNorm);
        float preResetDrSidMachineModelNorm = captureTransportAuthorityParam(kParamDrSidMachineModel);
        if (!std::isfinite(preResetDrSidMachineModelNorm)) preResetDrSidMachineModelNorm = preResetDrSidModelFallbackNorm;
        const int preResetStickyBankSlot = stickyPresetDisplaySlot();

        // capture the entire live DrSID/SID-808 audible authority, not
        // just the structural model. Logic's AudioUnitReset snapshot can be
        // stale for GUI-internal kit edits, so restoring only MachineModel still
        // leaves kick/snare/hat/cowbell/tom/accent/drive controls vulnerable to
        // reset-to-default on Stop->Play. This fixed-size image is stack-only
        // and reset-thread owned; it is replayed only when live pre-reset DrSID
        // authority is active.
        static constexpr std::array<int, 17> kDrSidTransportAuthorityParams{{
            kParamDrSidMachineModel,
            kParamDrSidKickTune,
            kParamDrSidKickDecay,
            kParamDrSidSnareTone,
            kParamDrSidSnareSnap,
            kParamDrSidHatTune,
            kParamDrSidHatDecay,
            kParamDrSidClapDecay,
            kParamDrSidCowbellTune,
            kParamDrSidCowbellDecay,
            kParamDrSidTomTune,
            kParamDrSidTomDecay,
            kParamDrSidVolume,
            kParamDrSidAccentAmount,
            kParamDrSidOutputDrive,
            kParamDrSidHatMetal,
            kParamDrSidClapSpread
        }};
        std::array<float, kDrSidTransportAuthorityParams.size()> preResetDrSidAuthorityValues{};
        for (size_t ai = 0; ai < kDrSidTransportAuthorityParams.size(); ++ai) {
            const int pid = kDrSidTransportAuthorityParams[ai];
            preResetDrSidAuthorityValues[ai] =
                captureTransportAuthorityParam(pid);
        }
        static constexpr std::size_t kDrSidSeqTransportAuthorityCount =
            static_cast<std::size_t>(kParamSeqStep32Gate - kParamSeqEnable + 1);
        std::array<float, kDrSidSeqTransportAuthorityCount> preResetDrSidSeqAuthorityValues{};
        for (int pid = static_cast<int>(kParamSeqEnable);
             pid <= static_cast<int>(kParamSeqStep32Gate); ++pid) {
            preResetDrSidSeqAuthorityValues[static_cast<std::size_t>(pid - kParamSeqEnable)] =
                captureTransportAuthorityParam(pid);
        }
        const auto snapshotParamValue = [&](int pid) noexcept -> float {
            if (!values || pid < 0 || pid >= count || pid >= kNumParams) return kParamInfos[(size_t)pid].defaultNorm;
            return ArpSID::sanitizeNormalizedParamValue(pid, values[pid], kParamInfos[(size_t)pid].defaultNorm);
        };
        // v922: transport-reset host snapshots are overlay data, not structural
        // mode authority, when an explicit factory/sticky root is being restored.
        // Logic/AU can replay stale retained mode bits (for example DrSID=1 from
        // an old state while the selected root is Classic Synth). Trust the live
        // pre-reset render/control images and the explicit factory root; use
        // snapshot mode authority only for no-explicit-root reset/full-state cases.
        const bool snapshotModeAuthorityAllowed = !hasExplicitFactorySlot;
        const bool snapshotDrSidAuthority =
            snapshotModeAuthorityAllowed &&
            values && count > static_cast<int>(kParamDrSidEnable) &&
            snapshotParamValue(kParamDrSidEnable) > 0.5f &&
            snapshotParamValue(kParamSynthModeEnable) <= 0.5f;
        const bool snapshotSynthAuthority =
            snapshotModeAuthorityAllowed &&
            values && count > static_cast<int>(kParamSynthModeEnable) &&
            snapshotParamValue(kParamSynthModeEnable) > 0.5f &&
            snapshotParamValue(kParamDrSidEnable) <= 0.5f;
        if (snapshotDrSidAuthority) {
            for (size_t ai = 0; ai < kDrSidTransportAuthorityParams.size(); ++ai) {
                const int pid = kDrSidTransportAuthorityParams[ai];
                preResetDrSidAuthorityValues[ai] = snapshotParamValue(pid);
            }
            for (int pid = static_cast<int>(kParamSeqEnable);
                 pid <= static_cast<int>(kParamSeqStep32Gate); ++pid) {
                preResetDrSidSeqAuthorityValues[static_cast<std::size_t>(pid - kParamSeqEnable)] =
                    snapshotParamValue(pid);
            }
            preResetDrSidMachineModelNorm = snapshotParamValue(kParamDrSidMachineModel);
        }
        const bool preResetDrSidAuthority =
            snapshotDrSidAuthority || (preResetDrSidEnableNorm > 0.5f && preResetSynthModeEnableNorm <= 0.5f);
        const bool preResetSynthAuthority =
            snapshotSynthAuthority || (preResetSynthModeEnableNorm > 0.5f && preResetDrSidEnableNorm <= 0.5f);
        const bool preResetDrSidSeqAuthority =
            preResetDrSidAuthority &&
            preResetDrSidSeqAuthorityValues[static_cast<std::size_t>(kParamSeqEnable - kParamSeqEnable)] > 0.5f;
        const bool preResetLooksLikeDrSidEdit =
            preResetDrSidAuthority || preResetDrSidMachineModelNorm > 0.5f;

        // Host reset / Logic transport-start must be a transient cleanup only.
        // A plain teardownReset() clears/reinitializes engines; if the next
        // host snapshot or pending root is stale slot 0, the audible backends can
        // be re-projected as patch 0 while AU/GUI metadata remains on the user's
        // selected patch. Preserve the actual audible authority explicitly:
        // 1) clear transient engine state,
        // 2) if the user has explicitly selected a factory slot, re-apply that
        // factory root as the structural audio authority,
        // 3) re-overlay the live host parameter image so edited controls and
        // auval-retained runtime/transient writable params survive reset.
        teardownReset();

        const int slot = ArpSID::canonicalFactorySlotForRoot(factorySlot);
        bool factoryRootApplied = false;
        bool factoryRootDrSidAuthority = false;
        bool factoryRootSynthModeAuthority = false;
        bool factoryRootArpAuthority = false;
        bool factoryRootSeqAuthority = false;
        std::array<float, kDrSidTransportAuthorityParams.size()> factoryRootDrSidAuthorityValues{};
        for (size_t ai = 0; ai < factoryRootDrSidAuthorityValues.size(); ++ai) {
            const int pid = kDrSidTransportAuthorityParams[ai];
            factoryRootDrSidAuthorityValues[ai] = ArpSID::defaultNormalizedParamValue(pid);
        }
        std::array<float, kDrSidSeqTransportAuthorityCount> factoryRootDrSidSeqAuthorityValues{};
        for (int pid = static_cast<int>(kParamSeqEnable);
             pid <= static_cast<int>(kParamSeqStep32Gate); ++pid) {
            factoryRootDrSidSeqAuthorityValues[static_cast<std::size_t>(pid - kParamSeqEnable)] =
                ArpSID::defaultNormalizedParamValue(pid);
        }
        float factoryRootDrSidMachineModel = ArpSID::defaultNormalizedParamValue((int)kParamDrSidMachineModel);
        if (hasExplicitFactorySlot && slot >= 0) {
            SidStateRootV1 root = ArpSID::makeFactoryPatchStateRootForSlot(slot);
            if (root.valid()) {
                factoryRootDrSidAuthority =
                    ArpSID::sidStateRootParamValue(root, kParamDrSidEnable) > 0.5f;
                factoryRootSynthModeAuthority =
                    ArpSID::sidStateRootParamValue(root, kParamSynthModeEnable) > 0.5f;
                factoryRootArpAuthority =
                    ArpSID::sidStateRootParamValue(root, kParamArpEnable) > 0.5f;
                factoryRootSeqAuthority =
                    ArpSID::sidStateRootParamValue(root, kParamSeqEnable) > 0.5f;
                factoryRootDrSidMachineModel =
                    ArpSID::sidStateRootParamValue(root, kParamDrSidMachineModel);
                for (size_t ai = 0; ai < kDrSidTransportAuthorityParams.size(); ++ai) {
                    const int pid = kDrSidTransportAuthorityParams[ai];
                    factoryRootDrSidAuthorityValues[ai] =
                        ArpSID::sanitizeNormalizedParamValue(pid,
                                                             ArpSID::sidStateRootParamValue(root, pid),
                                                             kParamInfos[(size_t)pid].defaultNorm);
                }
                for (int pid = static_cast<int>(kParamSeqEnable);
                     pid <= static_cast<int>(kParamSeqStep32Gate); ++pid) {
                    factoryRootDrSidSeqAuthorityValues[static_cast<std::size_t>(pid - kParamSeqEnable)] =
                        ArpSID::sanitizeNormalizedParamValue(pid,
                                                             ArpSID::sidStateRootParamValue(root, pid),
                                                             kParamInfos[(size_t)pid].defaultNorm);
                }
                setStickyPresetDisplaySlot(slot);
                applyStateRootCanonical(root);
                factoryRootApplied = true;
            }
        }

        // AudioUnitReset at Logic Stop/Start is not the same operation as
        // an explicit factory preset switch. A factory root is still re-applied
        // first so stale slot-0 ClassInfo/PresentPreset replay cannot become
        // structural authority, but the live AU parameter image must then be
        // overlaid for persistent audio parameters. Otherwise DrSID kits and
        // edited sound controls fall back to the factory root while GUI/sticky
        // metadata still show the selected preset.
        const StateOverlayPolicy resetOverlayPolicy = factoryRootApplied
            ? ArpSID::sidStateOverlayPolicyForApplyReason(ArpSID::SidStateApplyReason::HostTransportReset)
            : ArpSID::sidStateOverlayPolicyForApplyReason(ArpSID::SidStateApplyReason::WrapperFullStateRestore);

        bool transportResetOverlayApplied = false;
        if (values && count > 0) {
            switch (resetOverlayPolicy) {
                case ArpSID::SidStateOverlayPolicy::FactoryRootSeedThenLiveAudioOverlay:
                    // AudioUnitReset at Stop/Play is a transport/runtime boundary,
                    // not a user factory-preset switch. The factory root gives the
                    // backends a structurally valid SID/DrSID/register base, but the
                    // live AU parameter image remains the audible kit/patch authority.
                    // Re-overlay only persistent audio parameters so DrSID kit/model,
                    // levels, filter, FX, sequencer and edited sound controls survive
                    // Logic reset without allowing stale Program/BankSlot or transient
                    // MIDI/host-control mirrors to become preset authority.
                    restoreTransportResetAudioSnapshotImmediate(values, count);
                    transportResetOverlayApplied = true;
                    break;
                case ArpSID::SidStateOverlayPolicy::SnapshotIsAudibleAuthority:
                    restoreHostParameterSnapshotImmediate(values, count);
                    break;
                case ArpSID::SidStateOverlayPolicy::FactoryRootIsAudibleAuthority:
                case ArpSID::SidStateOverlayPolicy::SerializedStateIsAuthority:
                    break;
            }
        } else if (runtimeExecutionOwner_) {
            runtimeProjectionState_.firstApply = true;
            if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(true);
            syncSidSystemModelFromParams_(true);
            syncSidQueuedShadowFromLive_();
            sanitizeSynthModeQueuedShadow_();
        }

        // AudioUnitReset at Logic Stop/Play must never allow a stale host
        // snapshot to demote the structural render mode selected by the explicit
        // factory root or by the AU component flavor. This is especially important
        // for DrSID / SID-808 flavors, where Logic may replay a default retained
        // parameter image while the sticky preset and GUI still show the correct
        // kit. The reset overlay may restore normal sound controls, but structural
        // mode authority is reasserted from: live pre-reset DrSID, explicit factory
        // root, and immutable component flavor policy.
        const bool flavorForcesDrSid =
            componentFlavor_ == ArpSID::ComponentFlavor::DrumMachine ||
            componentFlavor_ == ArpSID::ComponentFlavor::Sid808;
        const bool flavorForcesC64 =
            componentFlavor_ == ArpSID::ComponentFlavor::C64SidPlayer;
        const bool flavorForcesInstrumentSynth =
            componentFlavor_ == ArpSID::ComponentFlavor::Instrument;
        const bool explicitFactoryStructuralModeAuthority =
            factoryRootApplied && (factoryRootDrSidAuthority || factoryRootSynthModeAuthority);
        const bool structuralDrSidAuthority =
            flavorForcesDrSid ||
            (explicitFactoryStructuralModeAuthority
                ? factoryRootDrSidAuthority
                : (preResetDrSidAuthority || factoryRootDrSidAuthority));
        const bool structuralSynthModeAuthority =
            !flavorForcesDrSid &&
            !flavorForcesC64 &&
            !structuralDrSidAuthority &&
            (flavorForcesInstrumentSynth ||
             (explicitFactoryStructuralModeAuthority
                ? factoryRootSynthModeAuthority
                : (preResetSynthAuthority || factoryRootSynthModeAuthority)));
        const bool factoryRootBitPerfectAuthority =
            factoryRootApplied &&
            !factoryRootDrSidAuthority &&
            !factoryRootSynthModeAuthority &&
            !factoryRootArpAuthority &&
            !factoryRootSeqAuthority;
        const bool structuralBitPerfectAuthority =
            !flavorForcesDrSid &&
            !flavorForcesC64 &&
            !structuralDrSidAuthority &&
            !structuralSynthModeAuthority &&
            factoryRootBitPerfectAuthority;
        // v925: if the transport reset overlay just applied the host/live
        // snapshot, do not replay the earlier captured/factory DrSID kit image
        // over it. That replay clobbered live Kick/Snare/etc edits back to
        // factory defaults in Logic Stop/Start. Structural mode bits are still
        // asserted below exactly once; non-mode kit/audio params remain owned by
        // the sanitized transport snapshot.
        const bool replayLiveDrSidAuthority =
            !transportResetOverlayApplied &&
            (preResetDrSidAuthority || preResetLooksLikeDrSidEdit || flavorForcesDrSid);
        if (structuralDrSidAuthority) {
            const int preservedDrSidSlot = ArpSID::canonicalFactorySlotForRoot(
                (factoryRootApplied && factoryRootDrSidAuthority && !factoryRootSynthModeAuthority)
                    ? slot
                    : preResetStickyBankSlot);
            setStickyPresetDisplaySlot(preservedDrSidSlot);
            const float preservedDrSidBankSlotNorm = ArpSID::canonicalNormalizedBankSlotValue(preservedDrSidSlot);
            const float preservedDrSidProgramNorm = ArpSID::canonicalNormalizedFactoryProgramValue(preservedDrSidSlot);
            params_[(size_t)kParamBankSlot].store(preservedDrSidBankSlotNorm, std::memory_order_relaxed);
            renderParams_[(size_t)kParamBankSlot] = preservedDrSidBankSlotNorm;
            (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(kParamBankSlot), preservedDrSidBankSlotNorm);
            params_[(size_t)kParamProgram].store(preservedDrSidProgramNorm, std::memory_order_relaxed);
            renderParams_[(size_t)kParamProgram] = preservedDrSidProgramNorm;
            (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(kParamProgram), preservedDrSidProgramNorm);
            // v924: structural mode bits are staged exactly once in this
            // authority block. DrSID kit/model replay below is intentionally
            // limited to non-mode sound parameters, so it must not re-stage
            // kParamSynthModeEnable/kParamDrSidEnable and reopen ordering drift.
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSynthModeEnable), 0.0f);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamDrSidEnable), 1.0f);
            // v950: DrSID structural authority clears ARP, but preserves SEQ
            // when DrSID/SID808 owned drum-pattern transport before reset or an
            // explicit factory/root owns sequencer transport. SEQ is no longer a
            // BitPerfect-only secondary after v949.
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);
            runtimeModel_.setArpActiveFlag(false);
            dirty_[(size_t)kParamArpEnable].store(false, std::memory_order_relaxed);
            const bool restoreDrSidSeqAuthority =
                preResetDrSidSeqAuthority ||
                (factoryRootApplied && factoryRootDrSidAuthority && !factoryRootSynthModeAuthority && factoryRootSeqAuthority);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable),
                                                restoreDrSidSeqAuthority ? 1.0f : 0.0f);
            dirty_[(size_t)kParamSeqEnable].store(false, std::memory_order_relaxed);
            if (!restoreDrSidSeqAuthority) {
                runtimeModel_.setSeqLastNote(-1);
                runtimeModel_.setSeqSamplesUntilStep(-1.0);
                runtimeModel_.setSeqStep(0);
                seqEngine_.resetPhase();
                prevSeqEnabled_ = false;
            }
            if (replayLiveDrSidAuthority) {
                for (size_t ai = 0; ai < kDrSidTransportAuthorityParams.size(); ++ai) {
                    const int pid = kDrSidTransportAuthorityParams[ai];
                    const float v = preResetDrSidAuthorityValues[ai];
                    if (std::isfinite(v)) {
                        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(pid), v);
                    }
                }
                if (preResetDrSidSeqAuthority) {
                    for (int pid = static_cast<int>(kParamSeqEnable);
                         pid <= static_cast<int>(kParamSeqStep32Gate); ++pid) {
                        const float v = preResetDrSidSeqAuthorityValues[static_cast<std::size_t>(pid - kParamSeqEnable)];
                        if (std::isfinite(v)) {
                            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(pid), v);
                        }
                    }
                }
            } else if (!transportResetOverlayApplied &&
                factoryRootApplied && factoryRootDrSidAuthority &&
                !factoryRootSynthModeAuthority) {
                for (size_t ai = 0; ai < kDrSidTransportAuthorityParams.size(); ++ai) {
                    const int pid = kDrSidTransportAuthorityParams[ai];
                    const float v = factoryRootDrSidAuthorityValues[ai];
                    if (std::isfinite(v)) {
                        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(pid), v);
                    }
                }
                if (factoryRootSeqAuthority) {
                    for (int pid = static_cast<int>(kParamSeqEnable);
                         pid <= static_cast<int>(kParamSeqStep32Gate); ++pid) {
                        const float v = factoryRootDrSidSeqAuthorityValues[static_cast<std::size_t>(pid - kParamSeqEnable)];
                        if (std::isfinite(v)) {
                            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(pid), v);
                        }
                    }
                }
                if (std::isfinite(factoryRootDrSidMachineModel)) {
                    runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamDrSidMachineModel),
                                                        factoryRootDrSidMachineModel);
                }
            }
            if (preResetDrSidAuthority && std::isfinite(preResetDrSidMachineModelNorm)) {
                runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamDrSidMachineModel), preResetDrSidMachineModelNorm);
            }
            if (componentFlavor_ == ArpSID::ComponentFlavor::Sid808) {
                // Mode bits were already asserted above by the structural DrSID
                // authority write. SID-808 flavor only adds the model override.
                runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamDrSidMachineModel), 1.0f);
            }
            dirty_[(size_t)kParamBankSlot].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamProgram].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamSynthModeEnable].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidEnable].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidMachineModel].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidKickTune].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidKickDecay].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidSnareTone].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidSnareSnap].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidHatTune].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidHatDecay].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidClapDecay].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidCowbellTune].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidCowbellDecay].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidTomTune].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidTomDecay].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidVolume].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidAccentAmount].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidOutputDrive].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidHatMetal].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidClapSpread].store(false, std::memory_order_relaxed);
            runtimeProjectionState_.firstApply = true;
            if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(true);
            syncSidSystemModelFromParams_(true);
            syncSidQueuedShadowFromLive_();
            sanitizeSynthModeQueuedShadow_();
        } else if (structuralSynthModeAuthority) {
            const int preservedSlot = ArpSID::canonicalFactorySlotForRoot(
                (factoryRootApplied && factoryRootSynthModeAuthority) ? slot : preResetStickyBankSlot);
            setStickyPresetDisplaySlot(preservedSlot);
            const float preservedBankSlotNorm = ArpSID::canonicalNormalizedBankSlotValue(preservedSlot);
            const float preservedProgramNorm = ArpSID::canonicalNormalizedFactoryProgramValue(preservedSlot);
            params_[(size_t)kParamBankSlot].store(preservedBankSlotNorm, std::memory_order_relaxed);
            renderParams_[(size_t)kParamBankSlot] = preservedBankSlotNorm;
            (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(kParamBankSlot), preservedBankSlotNorm);
            params_[(size_t)kParamProgram].store(preservedProgramNorm, std::memory_order_relaxed);
            renderParams_[(size_t)kParamProgram] = preservedProgramNorm;
            (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(kParamProgram), preservedProgramNorm);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamDrSidEnable), 0.0f);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSynthModeEnable), 1.0f);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);
            runtimeModel_.setArpActiveFlag(false);
            dirty_[(size_t)kParamBankSlot].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamProgram].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidEnable].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamSynthModeEnable].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamArpEnable].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamSeqEnable].store(false, std::memory_order_relaxed);
            runtimeProjectionState_.firstApply = true;
            if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(true);
            syncSidSystemModelFromParams_(true);
            syncSidQueuedShadowFromLive_();
            sanitizeSynthModeQueuedShadow_();
        } else if (structuralBitPerfectAuthority) {
            const int preservedSlot = ArpSID::canonicalFactorySlotForRoot(slot >= 0 ? slot : preResetStickyBankSlot);
            setStickyPresetDisplaySlot(preservedSlot);
            const float preservedBankSlotNorm = ArpSID::canonicalNormalizedBankSlotValue(preservedSlot);
            const float preservedProgramNorm = ArpSID::canonicalNormalizedFactoryProgramValue(preservedSlot);
            params_[(size_t)kParamBankSlot].store(preservedBankSlotNorm, std::memory_order_relaxed);
            renderParams_[(size_t)kParamBankSlot] = preservedBankSlotNorm;
            (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(kParamBankSlot), preservedBankSlotNorm);
            params_[(size_t)kParamProgram].store(preservedProgramNorm, std::memory_order_relaxed);
            renderParams_[(size_t)kParamProgram] = preservedProgramNorm;
            (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(kParamProgram), preservedProgramNorm);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSynthModeEnable), 0.0f);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamDrSidEnable), 0.0f);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);
            runtimeModel_.setArpActiveFlag(false);
            runtimeModel_.setSeqLastNote(-1);
            runtimeModel_.setSeqSamplesUntilStep(-1.0);
            if (BitPerfectEngine* bpe = bpe_()) bpe->allNotesOff();
            dirty_[(size_t)kParamBankSlot].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamProgram].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamSynthModeEnable].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamDrSidEnable].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamArpEnable].store(false, std::memory_order_relaxed);
            dirty_[(size_t)kParamSeqEnable].store(false, std::memory_order_relaxed);
            runtimeProjectionState_.firstApply = true;
            if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(true);
            syncSidSystemModelFromParams_(true);
            syncSidQueuedShadowFromLive_();
            sanitizeSynthModeQueuedShadow_();
        } else if (enforceComponentFlavorPolicy_()) {
            runtimeProjectionState_.firstApply = true;
            if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(true);
            syncSidSystemModelFromParams_(true);
            syncSidQueuedShadowFromLive_();
            sanitizeSynthModeQueuedShadow_();
        }
    }

    void clearHeldIngressChannel_(uint8_t ch) noexcept {
        // Realtime invariant: held-ingress clear is genuinely O(1).
        // The channel generation is the authority; stale note/lane atomics are
        // ignored until overwritten by a note-on in the current generation.
        const size_t c = static_cast<size_t>(ch & 0x0Fu);
        pendingChannelGen_[c].fetch_add(1u, std::memory_order_acq_rel);
        runtimeHostSurface_().heldIngressChannelGeneration[c].fetch_add(1u, std::memory_order_acq_rel);
    }

    void clearLivePedalMirrorsChannel_(uint8_t ch) noexcept {
        const size_t c = static_cast<size_t>(ch & 0x0Fu);
        liveSustainPedalDown_[c].store(0u, std::memory_order_release);
        liveSostenutoPedalDown_[c].store(0u, std::memory_order_release);
    }

    void clearLivePerformanceMirrors_(bool clearPedals) noexcept {
        for (uint8_t ch = 0; ch < 16u; ++ch) {
            clearHeldIngressChannel_(ch);
            if (clearPedals) clearLivePedalMirrorsChannel_(ch);
        }
        if (clearPedals) engineBank_.pedalState.clear();
    }

    void neutralizeTransportTransientHostControls_() noexcept {
        // Host MIDI controller mirror params are transport/transient authority, not
        // preset authority. Clear them together with the runtime MIDI mirrors so a
        // stale CC64/66 value cannot be redispatched immediately after Play-clear.
        for (uint8_t ch = 0; ch < 16u; ++ch) {
            const size_t c = static_cast<size_t>(ch);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlSustainBase) + ch), 0.0f);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlSostenutoBase) + ch), 0.0f);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlChannelPressureBase) + ch), 0.0f);
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlPitchBendBase) + ch), 0.5f);
            runtimeHostSurface_().prevSustain[c] = 0.0f;
            runtimeHostSurface_().prevChannelPressure[c] = 0.0f;
            runtimeHostSurface_().prevPitchBend[c] = 0.5f;
        }
        runtimeHostSurface_().channelPressure = 0.0f;
        runtimeHostSurface_().pitchBendNorm = 0.5f;
    }

    void clearPendingControllerFallbacksForChannel_(uint8_t ch) noexcept {
        // Single generation increment invalidates all pending controller
        // state for this channel in O(1) instead of O(N) atomic stores.
        const size_t c = static_cast<size_t>(ch & 0x0Fu);
        pendingChannelGen_[c].fetch_add(1u, std::memory_order_acq_rel);
    }

    // audit P2.13: accumulate this block's canonical-queue overflow ledger into
    // the cumulative render-published counters surfaced via the eventOverflow*
    // accessors. Render-thread only; relaxed atomics (diagnostic, not ordering).
    void publishEventOverflowTelemetry_(const ArpSID::SidTimedEventOverflowTelemetry& t) noexcept {
        if (!t.overflowed) return;
        if (t.droppedNoteOn)          eventOverflowDroppedNoteOn_.fetch_add(t.droppedNoteOn, std::memory_order_relaxed);
        if (t.droppedNoteOff)         eventOverflowDroppedNoteOff_.fetch_add(t.droppedNoteOff, std::memory_order_relaxed);
        if (t.droppedAutomation)      eventOverflowDroppedAutomation_.fetch_add(t.droppedAutomation, std::memory_order_relaxed);
        if (t.droppedController)      eventOverflowDroppedController_.fetch_add(t.droppedController, std::memory_order_relaxed);
        if (t.droppedTransport)       eventOverflowDroppedTransport_.fetch_add(t.droppedTransport, std::memory_order_relaxed);
        if (t.replacedLowerPriority)  eventOverflowReplacedLowerPriority_.fetch_add(t.replacedLowerPriority, std::memory_order_relaxed);
        if (t.droppedTotal)           eventOverflowDroppedTotal_.fetch_add(t.droppedTotal, std::memory_order_relaxed);
    }

    // v912 parent/chunk EventBuffer overflow closure: parent-scope async
    // normalization and chunk merge buffers can overflow before events reach the
    // canonical SidTimedEventQueue. Publish those drops too so no ingress loss is
    // silent merely because it happened in the pre-canonical AU buffer.
    void publishEventOverflowTelemetry_(const ArpSID::EventOverflowTelemetry& t) noexcept {
        if (!t.overflowed) return;
        if (t.droppedNoteOn)          eventOverflowDroppedNoteOn_.fetch_add(t.droppedNoteOn, std::memory_order_relaxed);
        if (t.droppedNoteOff)         eventOverflowDroppedNoteOff_.fetch_add(t.droppedNoteOff, std::memory_order_relaxed);
        if (t.droppedAutomation)      eventOverflowDroppedAutomation_.fetch_add(t.droppedAutomation, std::memory_order_relaxed);
        if (t.droppedController)      eventOverflowDroppedController_.fetch_add(t.droppedController, std::memory_order_relaxed);
        if (t.droppedTransport)       eventOverflowDroppedTransport_.fetch_add(t.droppedTransport, std::memory_order_relaxed);
        if (t.replacedLowerPriority)  eventOverflowReplacedLowerPriority_.fetch_add(t.replacedLowerPriority, std::memory_order_relaxed);
        if (t.droppedTotal)           eventOverflowDroppedTotal_.fetch_add(t.droppedTotal, std::memory_order_relaxed);
    }

    void clearQueuedFactoryPatchIngress_() noexcept {
        // Factory-root guard: pending host/UI parameter intents and queued
        // MIDI-controller fallbacks were captured before the factory SidStateRootV1
        // became authority. If drained after applyStateRootCanonical(), they can
        // replay stale mixer/filter/LFO/FX/controller state over the restored patch.
        // Held note identity is already mirrored synchronously in runtimeHostSurface_()
        // by pushMidi(), so clearing the raw MIDI queue does not lose canonical
        // held-note replay state. Do not clear held ingress here.
        paramIntentQueue_.clear();
        for (int i = 0; i < kNumParams; ++i) {
            paramIntentFallbackPacked_[(size_t)i].store(0u, std::memory_order_relaxed);
            paramIntentAppliedFallbackGeneration_[(size_t)i].store(
                paramIntentGeneration_[(size_t)i].load(std::memory_order_acquire),
                std::memory_order_release);
        }
        midiQueue_.clear();
        for (uint8_t ch = 0; ch < 16u; ++ch) {
            const size_t c = static_cast<size_t>(ch);
            pendingAllSoundOff_[c].store(0u, std::memory_order_relaxed);
            pendingAllNotesOff_[c].store(0u, std::memory_order_relaxed);
            pendingResetControllers_[c].store(0u, std::memory_order_relaxed);
            pendingSustainDirty_[c].store(0u, std::memory_order_relaxed);
            pendingSustainOff_[c].store(0u, std::memory_order_relaxed);
            pendingSostenutoDirty_[c].store(0u, std::memory_order_relaxed);
            pendingSostenutoOff_[c].store(0u, std::memory_order_relaxed);
            // factory roots reset runtime pedal policy; re-emit only live
            // physical pedal-down state after queue scrub, not arbitrary stale CCs.
            if (liveSustainPedalDown_[c].load(std::memory_order_acquire) != 0u) {
                pendingSustain_[c].store(127u, std::memory_order_release);
                pendingSustainDirty_[c].store(1u, std::memory_order_release);
            }
            if (liveSostenutoPedalDown_[c].load(std::memory_order_acquire) != 0u) {
                pendingSostenuto_[c].store(127u, std::memory_order_release);
                pendingSostenutoDirty_[c].store(1u, std::memory_order_release);
            }
            pendingModWheelDirty_[c].store(0u, std::memory_order_relaxed);
            pendingExpressionDirty_[c].store(0u, std::memory_order_relaxed);
            pendingChannelVolumeDirty_[c].store(0u, std::memory_order_relaxed);
            pendingBankMsbDirty_[c].store(0u, std::memory_order_relaxed);
            pendingBankLsbDirty_[c].store(0u, std::memory_order_relaxed);
            pendingRpnMsbDirty_[c].store(0u, std::memory_order_relaxed);
            pendingRpnLsbDirty_[c].store(0u, std::memory_order_relaxed);
            pendingNrpnMsbDirty_[c].store(0u, std::memory_order_relaxed);
            pendingNrpnLsbDirty_[c].store(0u, std::memory_order_relaxed);
            pendingDataEntryMsbDirty_[c].store(0u, std::memory_order_relaxed);
            pendingDataEntryLsbDirty_[c].store(0u, std::memory_order_relaxed);
            pendingChannelPressureDirty_[c].store(0u, std::memory_order_relaxed);
            pendingPitchBendDirty_[c].store(0u, std::memory_order_relaxed);
            for (int note = 0; note < 128; ++note) {
                pendingDroppedNoteOffCount_[c][(size_t)note].store(0u, std::memory_order_relaxed);
                pendingDroppedNoteOffHostTime_[c][(size_t)note].store(0u, std::memory_order_relaxed);
                pendingPolyPressureDirty_[c][(size_t)note].store(0u, std::memory_order_relaxed);
            }
        }
    }

    void clearAllPendingMidiFallbacks_() noexcept {
        // Panic/all-notes-off invalidates each channel with O(16) generation
        // bumps. It must not zero held-note note/lane arrays on the audio
        // thread. v168: this is a hard panic/reset path, not a physical
        // pedal-preserving factory scrub, so live pedal mirrors must be
        // cleared too. Otherwise a later factory-root restore can re-arm
        // sustain/sostenuto from stale physical mirrors after panic killed the
        // performance state.
        clearQueuedFactoryPatchIngress_();
        clearLivePerformanceMirrors_(true);
    }
    void drainPendingCriticalMidiFallbacks_(int frames) noexcept {
        const int boundedFrames = std::max(0, frames);
        for (int ch = 0; ch < 16; ++ch) {
            // Generation counter note: clear paths invalidate held ingress in O(1).
            // Dirty controller flags are consumed by exchange below; stale held-note
            // replay is rejected by heldIngressNoteGeneration checks.
            auto emitCc = [&](uint8_t cc, float norm = 0.0f) noexcept {
                TimedEvent te{};
                te.kind = EventKind::ControlChange;
                te.channel = static_cast<uint8_t>(ch);
                te.ccNum = cc;
                te.value = norm;
                te.value_f32 = norm;
                ArpSID::sidWrapperPushEvent(runtimeModel_, te.toCanonical(), boundedFrames);
            };
            const bool resetControllers = pendingResetControllers_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u;
            if (resetControllers) {
                clearPendingControllerFallbacksForChannel_(static_cast<uint8_t>(ch));
                clearLivePedalMirrorsChannel_(static_cast<uint8_t>(ch));
            }
            if (pendingAllSoundOff_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) emitCc(120u, 0.0f);
            if (pendingAllNotesOff_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) emitCc(123u, 0.0f);
            if (pendingSustainDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) {
                const uint8_t v = pendingSustain_[(size_t)ch].load(std::memory_order_acquire);
                emitCc(64u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f));
            }
            else if (pendingSustainOff_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) emitCc(64u, 0.0f);
            if (pendingSostenutoDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) {
                const uint8_t v = pendingSostenuto_[(size_t)ch].load(std::memory_order_acquire);
                emitCc(66u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f));
            }
            else if (pendingSostenutoOff_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) emitCc(66u, 0.0f);
            if (pendingModWheelDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) {
                const uint8_t v = pendingModWheel_[(size_t)ch].load(std::memory_order_acquire);
                emitCc(1u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f));
            }
            if (pendingExpressionDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) {
                const uint8_t v = pendingExpression_[(size_t)ch].load(std::memory_order_acquire);
                emitCc(11u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f));
            }
            if (pendingChannelVolumeDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) {
                const uint8_t v = pendingChannelVolume_[(size_t)ch].load(std::memory_order_acquire);
                emitCc(7u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f));
            }
            if (pendingBankMsbDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) {
                const uint8_t v = pendingBankMsb_[(size_t)ch].load(std::memory_order_acquire);
                emitCc(0u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f));
            }
            if (pendingBankLsbDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) {
                const uint8_t v = pendingBankLsb_[(size_t)ch].load(std::memory_order_acquire);
                emitCc(32u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f));
            }
            if (pendingRpnMsbDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) { const uint8_t v = pendingRpnMsb_[(size_t)ch].load(std::memory_order_acquire); emitCc(101u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f)); }
            if (pendingRpnLsbDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) { const uint8_t v = pendingRpnLsb_[(size_t)ch].load(std::memory_order_acquire); emitCc(100u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f)); }
            if (pendingNrpnMsbDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) { const uint8_t v = pendingNrpnMsb_[(size_t)ch].load(std::memory_order_acquire); emitCc(99u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f)); }
            if (pendingNrpnLsbDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) { const uint8_t v = pendingNrpnLsb_[(size_t)ch].load(std::memory_order_acquire); emitCc(98u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f)); }
            if (pendingDataEntryMsbDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) { const uint8_t v = pendingDataEntryMsb_[(size_t)ch].load(std::memory_order_acquire); emitCc(6u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f)); }
            if (pendingDataEntryLsbDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) { const uint8_t v = pendingDataEntryLsb_[(size_t)ch].load(std::memory_order_acquire); emitCc(38u, std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f)); }
            if (resetControllers) emitCc(121u, 0.0f);
            if (pendingChannelPressureDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) {
                TimedEvent te{};
                te.kind = EventKind::ChannelPressure;
                te.channel = static_cast<uint8_t>(ch);
                const uint8_t v = pendingChannelPressure_[(size_t)ch].load(std::memory_order_acquire);
                te.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f);
                te.value_f32 = te.value;
                ArpSID::sidWrapperPushEvent(runtimeModel_, te.toCanonical(), boundedFrames);
            }
            if (pendingPitchBendDirty_[(size_t)ch].exchange(0u, std::memory_order_acq_rel) != 0u) {
                TimedEvent te{};
                te.kind = EventKind::PitchBend;
                te.channel = static_cast<uint8_t>(ch);
                te.data14 = pendingPitchBend14_[(size_t)ch].load(std::memory_order_acquire) & 0x3FFFu;
                ArpSID::sidWrapperPushEvent(runtimeModel_, te.toCanonical(), boundedFrames);
            }
            for (int note = 0; note < 128; ++note) {
                if (pendingPolyPressureDirty_[(size_t)ch][(size_t)note].exchange(0u, std::memory_order_acq_rel) != 0u) {
                    TimedEvent te{};
                    te.kind = EventKind::PolyPressure;
                    te.channel = static_cast<uint8_t>(ch);
                    te.pitch = static_cast<int16_t>(note);
                    const uint8_t v = pendingPolyPressure_[(size_t)ch][(size_t)note].load(std::memory_order_acquire);
                    te.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f);
                    te.value_f32 = te.value;
                    ArpSID::sidWrapperPushEvent(runtimeModel_, te.toCanonical(), boundedFrames);
                }
            }
            // Raw/channel Program Change is rejected at enqueue as non-render metadata;
            // there is no pending program ledger in the render authority.
        }
    }

    void latchDroppedNoteOff_(uint8_t ch, uint8_t note, uint64_t hostTime) noexcept {
        const size_t c=static_cast<size_t>(ch & 0x0Fu);
        const size_t n=static_cast<size_t>(note & 0x7Fu);
        auto& count=pendingDroppedNoteOffCount_[c][n];
        uint8_t observed=count.load(std::memory_order_relaxed);
        while(observed<8u &&
              !count.compare_exchange_weak(observed, static_cast<uint8_t>(observed+1u),
                                           std::memory_order_release,
                                           std::memory_order_relaxed)){}
        // Eight unresolved releases for one key in one render quantum are
        // already pathological; escalate to channel cleanup rather than
        // creating an unbounded render-thread loop.
        if(observed>=8u) pendingAllNotesOff_[c].store(1u, std::memory_order_release);
        pendingDroppedNoteOffHostTime_[c][n].store(hostTime ? hostTime : 1u,
                                                   std::memory_order_release);
    }

    void drainPendingDroppedNoteOffs_(int frames, uint64_t blockStartHostTime) noexcept {
        const int boundedFrames=std::max(1,frames);
        for(int ch=0;ch<16;++ch){
            for(int note=0;note<128;++note){
                const size_t c=static_cast<size_t>(ch);
                const size_t n=static_cast<size_t>(note);
                const uint8_t count=pendingDroppedNoteOffCount_[c][n].exchange(
                    0u,std::memory_order_acq_rel);
                if(count==0u) continue;
                const uint64_t hostTime=pendingDroppedNoteOffHostTime_[c][n].exchange(
                    0u,std::memory_order_acq_rel);
                int sampleOffset=boundedFrames-1;
                if(hostTime>1u && blockStartHostTime!=0u && hostTime>=blockStartHostTime){
                    const double deltaSec=hostTicksToSeconds_(hostTime-blockStartHostTime);
                    if(deltaSec>=0.0){ // v855: negative = no platform timebase -> leave unresolved
                        sampleOffset=ArpSID::canonicalHostSampleOffsetFromSeconds(
                            deltaSec,sampleRate_,boundedFrames);
                    }
                }
                for(uint8_t i=0;i<count;++i){
                    TimedEvent te{};
                    te.kind=EventKind::NoteOff;
                    te.channel=static_cast<uint8_t>(ch);
                    te.pitch=static_cast<int16_t>(note);
                    te.sampleOffset=sampleOffset;
                    auto canonical=te.toCanonical();
                    finalizeCanonicalTiming_(canonical,boundedFrames);
                    ArpSID::sidWrapperPushEvent(runtimeModel_,canonical,boundedFrames);
                }
            }
        }
    }

    //──────────────────────────────────────────────────────
    // Parameter access — any thread. Ingress only enqueues intent; actual engine mutation
    // remains render-thread owned when the canonical event queue is drained.
    static uint64_t packParamIntentFallback_(uint32_t generation, float value) noexcept {
        uint32_t bits = 0u;
        std::memcpy(&bits, &value, sizeof(bits));
        return (static_cast<uint64_t>(generation) << 32u) | static_cast<uint64_t>(bits);
    }

    static float unpackParamIntentFallbackValue_(uint64_t packed) noexcept {
        const uint32_t bits = static_cast<uint32_t>(packed);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    static uint32_t unpackParamIntentFallbackGeneration_(uint64_t packed) noexcept {
        return static_cast<uint32_t>(packed >> 32u);
    }

    void publishParamIntentFallback_(int pid, uint32_t generation, float value) noexcept {
        auto& slot = paramIntentFallbackPacked_[(size_t)pid];
        const uint64_t desired = packParamIntentFallback_(generation, value);
        uint64_t observed = slot.load(std::memory_order_relaxed);
        while (unpackParamIntentFallbackGeneration_(observed) < generation &&
               !slot.compare_exchange_weak(observed, desired,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {}
        dirty_[(size_t)pid].store(true, std::memory_order_release);
    }

    bool paramIntentWasSupersededByFallback_(const ArpSIDParamIntentRingBuffer::Event& ev) const noexcept {
        if (ev.paramID >= static_cast<uint32_t>(kNumParams) || ev.generation == 0u) return false;
        const uint32_t applied = paramIntentAppliedFallbackGeneration_[(size_t)ev.paramID].load(
            std::memory_order_acquire);
        return applied != 0u && static_cast<int32_t>(ev.generation - applied) <= 0;
    }

    bool enqueueParameterIntent(int pid, float value, int32_t sampleOffset = -1, uint64_t hostTime = 0) noexcept {
        if ((unsigned)pid >= (unsigned)kNumParams) return false;
        // host-facing Program/BankSlot parameter writes are never live
        // patch identity authority. Explicit preset loads enter through
        // schedulePendingStateRestore()/applyStateRootCanonical(), not this queue.
        if (pid == kParamProgram || pid == kParamBankSlot) return false;
        const float clean = ArpSID::sanitizeNormalizedParamValue(
            pid,
            value,
            ArpSID::defaultNormalizedParamValue(pid));
        const float previous = params_[(size_t)pid].load(std::memory_order_relaxed);
        if (!ArpSID::isFactorySnapshotMetadataOrTransientParam(pid) &&
            std::fabs(previous - clean) <= 1.0e-6f) {
            return true;
        }
        // v948: async/UI ingress uses the same param-specific sanitize law as
        // render-thread staging. Generic pre-clamp made params_ clean-enough for
        // 0..1 but could still bypass semantic defaults for non-trivial params.
        params_[(size_t)pid].store(clean, std::memory_order_relaxed);
        // Block-start routing can consult the atomic mirror before canonical
        // timed events are dispatched (Auto-GM promotion is one example).
        // Pre-stage the value mirror for both successful and failed intents;
        // successful side effects still land at their canonical event offset,
        // while only queue failures publish the full-side-effect fallback.
        dirty_[(size_t)pid].store(true, std::memory_order_release);
        const uint32_t generation =
            paramIntentGeneration_[(size_t)pid].fetch_add(1u, std::memory_order_relaxed) + 1u;
        const bool queued = paramIntentQueue_.push(static_cast<uint32_t>(pid), generation,
                                                   clean, hostTime, sampleOffset);
        if (!queued) {
            ingressDropTelemetry_.fetch_add(1, std::memory_order_relaxed);
            // Preserve the newest failed intent coherently. flushDirtyParams_()
            // applies it through the normal execution owner (all side effects),
            // and generation filtering prevents older queued values from
            // overwriting it later in the same block.
            publishParamIntentFallback_(pid, generation, clean);
            paramIntentDirtyFlushFallbackTelemetry_.fetch_add(1, std::memory_order_relaxed);
        }
        return queued;
    }

    void setParameter(int pid, float value) noexcept {
        enqueueParameterIntent(pid, value, -1, 0);
    }

    void setStickyPresetDisplaySlot(int slot) noexcept {
        telemetryPresetSlot_.store(std::clamp(slot, 0, ArpSID::kCanonicalFactoryPatchSlotMax), std::memory_order_relaxed);
    }

    int stickyPresetDisplaySlot() const noexcept {
        return std::clamp(telemetryPresetSlot_.load(std::memory_order_relaxed), 0, ArpSID::kCanonicalFactoryPatchSlotMax);
    }

    float getParameter(int pid) const noexcept {
        if ((unsigned)pid >= (unsigned)kNumParams) return 0.f;
        if (pid == kParamBankSlot) return ArpSID::canonicalNormalizedBankSlotValue(stickyPresetDisplaySlot());
        if (pid == kParamProgram) return ArpSID::canonicalNormalizedFactoryProgramValue(ArpSID::canonicalFactorySlotForRoot(stickyPresetDisplaySlot()));
        return params_[(size_t)pid].load(std::memory_order_relaxed);
    }

    void restoreHostParameterSnapshotImmediate(const float* values, int count) noexcept {
        if (!values || count <= 0) return;
        const int n = std::min<int>(count, kNumParams);
        for (int i = 0; i < n; ++i) {
            if (i == kParamProgram || i == kParamBankSlot) {
                // Program/BankSlot are readback-only preset mirrors. Logic may
                // include stale values in retained host snapshots at Play/Reset;
                // restoring them here would bypass AUv2/AUv3 preset guards.
                continue;
            }
            const float fallback = kParamInfos[(size_t)i].defaultNorm;
            const float clean = ArpSID::sanitizeNormalizedParamValue(i, values[i], fallback);
            params_[(size_t)i].store(clean, std::memory_order_relaxed);
            hostParamRetentionOverlay_[(size_t)i].store(clean, std::memory_order_relaxed);
            renderParams_[(size_t)i] = clean;
            dirty_[(size_t)i].store(false, std::memory_order_relaxed);
            // v898 split-brain fix: mirror into the runtime model's canonical
            // state root. Note routing (runtimeKernelOnMidiNoteOn) resolves the
            // render mode from STATE-ROOT params (isSynthModeEnabled/
            // isDrSidModeEnabled), while the engine guards read renderParams_.
            // A snapshot restore that updated only renderParams_ left the state
            // root stale, so a SynthMode note routed to the BitPerfect fallback
            // and was then dropped by the live-param guard — instruments went
            // silent and ignored MIDI after Logic replayed a retained snapshot.
            (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(i), clean);
        }
        hostParamRetentionOverlayPending_.store(true, std::memory_order_release);
        runtimeProjectionState_.firstApply = true;
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(true);
        syncSidSystemModelFromParams_(true);
        syncSidQueuedShadowFromLive_();
        sanitizeSynthModeQueuedShadow_();
    }

    void restoreTransportResetAudioSnapshotImmediate(const float* values, int count) noexcept {
        if (!values || count <= 0) return;
        const int n = std::min<int>(count, kNumParams);
        for (int i = 0; i < n; ++i) {
            if (ArpSID::isTransportResetStructuralAuthorityParam(i)) {
                continue;
            }
            const float fallback = kParamInfos[(size_t)i].defaultNorm;
            const float clean = ArpSID::sanitizeNormalizedParamValue(i, values[i], fallback);
            params_[(size_t)i].store(clean, std::memory_order_relaxed);
            renderParams_[(size_t)i] = clean;
            dirty_[(size_t)i].store(false, std::memory_order_relaxed);
            // v898 split-brain fix: keep the runtime model's state root in sync
            // (see restoreHostParameterSnapshotImmediate above).
            (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(i), clean);
        }
        hostParamRetentionOverlayPending_.store(false, std::memory_order_release);
        runtimeProjectionState_.firstApply = true;
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(true);
        syncSidSystemModelFromParams_(true);
        syncSidQueuedShadowFromLive_();
        sanitizeSynthModeQueuedShadow_();
    }

    void runtimeStageNormalizedParameterOnly(uint32_t target, float value) noexcept {
        if (!ArpSID::canonicalIsValidParamTarget(target, static_cast<uint32_t>(kNumParams))) return;
        const size_t idx = static_cast<size_t>(target);
        const float clean = ArpSID::sanitizeNormalizedParamValue(
            static_cast<int>(target),
            value,
            ArpSID::defaultNormalizedParamValue(static_cast<int>(target)));
        params_[idx].store(clean, std::memory_order_relaxed);  // render-thread owned — dirty_ not needed
        renderParams_[idx] = clean;
        dirty_[idx].store(false, std::memory_order_relaxed);
        // v945: AU3 and Phase2 share the same param-specific staging law.
        // Generic 0..1 clamping keeps params/render/model consistent but can still
        // preserve semantically invalid program/factory/structural values.
        // sanitizeNormalizedParamValue() is the canonical authority for every param.
        (void)runtimeModel_.applyAutomationPoint(target, clean);
    }

    void maybePromoteDrSidForGMNote_(uint8_t channel, uint8_t note) noexcept {
        if (!runtimeExecutionOwner_) return;
        if (!componentFlavorAllowsDrSidAutoPromotion_()) return;
        (void)ArpSID::sidCanonicalApplyGMDrSidPromotion(
            *this,
            channel,
            note,
            drs_() != nullptr,
            ArpSID::sidResolveRenderModeFromLiveParams(renderParams_) == ArpSID::SidRuntimeRenderMode::DrSid,
            true /* flavor/opt-in law already applied by componentFlavorAllowsDrSidAutoPromotion_ */);
    }

    // v910 single ingress authority: BOTH host events[] traffic and the
    // midiQueue_ ring drain dispatch note/controller events through this one
    // helper so transport suppression policy, GM DrSID promotion, the DIGI
    // sample-pad route, held-note mirroring and the canonical push can never
    // diverge between ingress paths again (the v909 audit found the two
    // duplicated blocks producing a ~42x audio mismatch for the same event).
    // Held-mirror authority is carried by te.ingressProvenance: the midiQueue_
    // ring mirrors held-note ingress at enqueue time (enqueueMidiIntent is
    // that path's mirror authority), host timed events mirror here with
    // synthetic raw bytes.
    void dispatchCanonicalIngressEvent_(const TimedEvent& te,
                                        int numFrames,
                                        bool suppressLiveNoteOns) noexcept {
        // v925: mirror physical held-note NoteOn/NoteOff at canonical dispatch
        // for both host events[] and accepted midiQueue_ ring events. Ring
        // NoteOff used to clear the held ledger at enqueue time, one block-edge
        // earlier than events[], which made NoteOn+NoteOff release audio diverge.
        const bool mirrorHeldAtDispatch =
            te.ingressProvenance == ArpSID::kIngressProvenanceHostEvents ||
            te.ingressProvenance == ArpSID::kIngressProvenanceAsyncRing;
        if (te.kind == EventKind::NoteOn &&
            suppressLiveNoteOns &&
            te.channel != 9 &&
            !sid808LivePadNote_(te)) {
            // Suppressed-note carve-outs: GM channel-10 drums, SID808 live pad
            // notes, and the DIGI sample pads must
            // audition from a MIDI keyboard even while the host transport is
            // stopped — exactly like the on-screen DIGI pads
            // (mirrors the ungated on-screen DIGI pads).
            // Only the synth/arp note is suppressed; the $D418 pad trigger
            // still fires. (v910: live input is never suppressed, so this
            // branch is reachable only for provenance-marked playback events.)
            auto canonical = te.toCanonical();
            finalizeCanonicalTiming_(canonical, numFrames);
            maybeTriggerDigiFromMidi_(canonical, numFrames);
            return;
        }
        if (te.kind == EventKind::NoteOn)
            maybePromoteDrSidForGMNote_(static_cast<uint8_t>(te.channel & 0x0F),
                                        static_cast<uint8_t>(te.pitch & 0x7F));
        // v898 SYNTH-MODE SILENCE FIX (reconciler feed): the two-pass orphan
        // reconciler judges "held" from the raw-MIDI held-ingress ledger.
        // Host-provided timed events must mirror into that ledger here with
        // synthetic raw bytes, otherwise every synth-mode voice they start is
        // declared an orphan and hard-gated off on the next reconcile pass.
        if (mirrorHeldAtDispatch &&
            (te.kind == EventKind::NoteOn || te.kind == EventKind::NoteOff)) {
            const bool on = (te.kind == EventKind::NoteOn) && te.value > 0.0f;
            const uint8_t bytes[3] = {
                static_cast<uint8_t>((on ? 0x90u : 0x80u) | (te.channel & 0x0Fu)),
                static_cast<uint8_t>(te.pitch & 0x7Fu),
                static_cast<uint8_t>(std::clamp<int>(
                    static_cast<int>(std::lround(te.value * 127.0f)), on ? 1 : 0, 127))
            };
            mirrorMidiHeldIngress_(bytes, 3u);
        }
        auto canonical = te.toCanonical();
        finalizeCanonicalTiming_(canonical, numFrames);
        maybeTriggerDigiFromMidi_(canonical, numFrames);
        ArpSID::sidWrapperPushEvent(runtimeModel_, canonical, numFrames);
    }

    // v910 shared ring translation: converts one raw midiQueue_ event into a
    // block-relative TimedEvent. Used by the normal-block drain AND the
    // parent-scope chunk normalization so hostTime→offset resolution can never
    // diverge between the two (the v909 audit's chunking drift risk: a queued
    // event meant for a later chunk must not clamp into the first chunk tail).
    // Returns false for messages that do not become timed events (e.g. raw
    // MIDI Program Change, which is song/GM metadata, not preset authority).
    // v912 ingress-boundary MIDI validation: malformed short channel-voice
    // packets must be rejected before they enter midiQueue_ or held-note mirrors.
    // v911 rejected them in translateRawMidiRingEvent_(), but enqueue-time
    // mirroring could still treat a short NoteOn/NoteOff as a real held-ledger
    // mutation before the translator discarded it. Keep this helper in lockstep
    // with translateRawMidiRingEvent_()'s accepted message set.
    static bool rawMidiChannelVoiceLengthOk_(const uint8_t* data, uint8_t len) noexcept {
        if (!data || len < 1u) return false;
        switch (data[0] & 0xF0u) {
            case 0x80u: // NoteOff
            case 0x90u: // NoteOn / velocity-zero NoteOff
            case 0xA0u: // Poly pressure
            case 0xB0u: // CC
            case 0xE0u: // Pitch bend
                return len >= 3u;
            case 0xC0u: // Program Change metadata is intentionally not a render event.
                return false;
            case 0xD0u: // Channel pressure
                return len >= 2u;
            default:
                return false;
        }
    }

    bool translateRawMidiRingEvent_(const ArpSIDMidiRingBuffer::Event& ev,
                                    uint64_t blockStartHostTime,
                                    int numFrames,
                                    TimedEvent& te) const noexcept {
        if (ev.len < 1) return false;
        const uint8_t st = ev.data[0] & 0xF0u;
        te.channel = ev.data[0] & 0x0Fu;

        // v911 malformed-MIDI closure / v912 ingress-boundary closure: never synthesize note 0 / CC 0
        // from truncated short messages. v912 also rejects them at enqueue, but
        // keep the render-side guard as the final authority for any legacy or
        // test-injected ring event.
        switch (st) {
            case 0x80: // NoteOff
            case 0x90: // NoteOn / vel-zero NoteOff
            case 0xA0: // Poly pressure
            case 0xB0: // CC
            case 0xE0: // Pitch bend
                if (ev.len < 3) return false;
                break;
            case 0xC0: // Program Change: GM metadata, not patch authority
                return false;
            case 0xD0: // Channel pressure
                if (ev.len < 2) return false;
                break;
            default:
                return false;
        }

        if (ev.explicitSampleOffset >= 0) {
            te.sampleOffset = std::clamp<int32_t>(ev.explicitSampleOffset, 0, std::max(0, numFrames - 1));
        } else if (ev.hostTime != 0 && blockStartHostTime != 0 && ev.hostTime >= blockStartHostTime) {
            const double deltaSec = hostTicksToSeconds_(ev.hostTime - blockStartHostTime);
            te.sampleOffset = (deltaSec >= 0.0)
                ? ArpSID::canonicalHostSampleOffsetFromSeconds(deltaSec, sampleRate_, numFrames)
                : -1;
        } else {
            te.sampleOffset = -1;
        }
        const bool staleOrInvalidHostTime = (ev.explicitSampleOffset < 0) &&
            !(ev.hostTime != 0 && blockStartHostTime != 0 && ev.hostTime >= blockStartHostTime);

        switch (st) {
            case 0x90:
                te.kind = (ev.data[2] > 0) ? EventKind::NoteOn : EventKind::NoteOff;
                te.pitch = ev.data[1];
                te.value = ev.data[2] / 127.f;
                break;
            case 0x80:
                te.kind = EventKind::NoteOff;
                te.pitch = ev.data[1];
                te.value = 0.f;
                break;
            case 0xB0:
                te.kind = EventKind::ControlChange;
                te.ccNum = ev.data[1];
                te.value = ev.data[2] / 127.f;
                te.value_f32 = te.value;
                break;
            case 0xE0:
                te.kind = EventKind::PitchBend;
                te.data14 = static_cast<uint16_t>((static_cast<uint16_t>(ev.data[2]) << 7) | ev.data[1]);
                break;
            case 0xD0:
                te.kind = EventKind::ChannelPressure;
                te.value = ev.data[1] / 127.f;
                te.value_f32 = te.value;
                break;
            case 0xA0:
                te.kind = EventKind::PolyPressure;
                te.pitch = ev.data[1];
                te.value = ev.data[2] / 127.f;
                te.value_f32 = te.value;
                break;
            default:
                return false;
        }
        // Stale/old MIDI note-off events are already due: apply at sample 0.
        if (staleOrInvalidHostTime && te.kind == EventKind::NoteOff) {
            te.sampleOffset = 0;
        }
        // v911: async ring held ingress is mirrored only after queue acceptance
        // (or after NoteOff fallback acceptance), never before a possible drop.
        te.ingressProvenance = ArpSID::kIngressProvenanceAsyncRing;
        return true;
    }

    // v910 shared param-intent translation (same block-relative offset law).
    void translateParamIntentEvent_(const ArpSIDParamIntentRingBuffer::Event& pev,
                                    uint64_t blockStartHostTime,
                                    int numFrames,
                                    TimedEvent& te) const noexcept {
        te.kind = EventKind::ParameterSet;
        if (pev.explicitSampleOffset >= 0) {
            te.sampleOffset = std::clamp<int32_t>(pev.explicitSampleOffset, 0, std::max(0, numFrames - 1));
        } else if (pev.hostTime != 0 && blockStartHostTime != 0 && pev.hostTime >= blockStartHostTime) {
            const double deltaSec = hostTicksToSeconds_(pev.hostTime - blockStartHostTime);
            te.sampleOffset = (deltaSec >= 0.0) // v855: negative = no platform timebase -> unresolved
                ? ArpSID::canonicalHostSampleOffsetFromSeconds(deltaSec, sampleRate_, numFrames)
                : -1;
        } else {
            te.sampleOffset = -1;
        }
        te.target = pev.paramID;
        te.value = pev.value;
        te.value_f32 = pev.value;
        te.ingressProvenance = ArpSID::kIngressProvenanceAsyncRing;
    }

    // v910 parent-scope chunk normalization: when a host block exceeds
    // kMaxFramesPerBlock, drain BOTH async ring queues once at parent scope so
    // hostTime/explicit offsets resolve against the PARENT block, then let the
    // chunk slicer distribute them. Draining inside each chunk resolved
    // offsets against chunk-local numFrames and stale parent renderHostTime_,
    // clamping every future-chunk event into the first chunk tail.
    void drainAsyncIngressToParentScope_(int parentNumFrames, EventBuffer& out) noexcept {
        const uint64_t parentBlockStartHostTime = renderHostTime_.load(std::memory_order_relaxed);
        {   ArpSIDParamIntentRingBuffer::Event pev;
            while (paramIntentQueue_.pop(pev)) {
                if (paramIntentWasSupersededByFallback_(pev)) continue;
                TimedEvent te{};
                translateParamIntentEvent_(pev, parentBlockStartHostTime, parentNumFrames, te);
                out.push(te);
            }
        }
        {   ArpSIDMidiRingBuffer::Event ev;
            while (midiQueue_.pop(ev)) {
                TimedEvent te{};
                if (translateRawMidiRingEvent_(ev, parentBlockStartHostTime, parentNumFrames, te))
                    out.push(te);
            }
        }
    }

    bool sid808LivePadNote_(const TimedEvent& ev) const noexcept {
        if (componentFlavor_ != ArpSID::ComponentFlavor::Sid808 ||
            ev.kind != EventKind::NoteOn) {
            return false;
        }
        const auto spec = ArpSID::sidGMDrumSpecForNote(
            static_cast<std::uint8_t>(ev.pitch & 0x7F));
        return spec.drumClass != ArpSID::SidGMDrumClass::Unsupported;
    }

    void runtimeImportSidRegisterNormalized(uint32_t target, float value) noexcept {
        if (target < (uint32_t)kParamSidRegD400 || target > (uint32_t)kParamSidRegD41D) return;
        const uint32_t reg = target - (uint32_t)kParamSidRegD400;
        if (reg >= 0x1Au) return;
        const uint8_t byteVal = static_cast<uint8_t>(std::clamp((int)std::lround(ArpSID::canonicalClampedNormalizedValue(value) * 255.f), 0, 255));
        runtimeModel_.importRegisterWriteSnapshot(reg, static_cast<uint32_t>(byteVal));
        if (runtimeIsSynthModeEnabled() && reg < 0x19u) {
            sreg_().write(static_cast<uint8_t>(reg), byteVal);
            publishSidCoreRegWrite_(static_cast<uint8_t>(reg), byteVal,
                sidCoreWriteStamp_(0));
        }
        if ((size_t)reg < sidQueuedShadow_.value.size()) {
            sidQueuedShadow_.value[(size_t)reg] = byteVal;
            sidQueuedShadow_.valid[(size_t)reg] = 1u;
            sidQueuedShadow_.sample[(size_t)reg] = 0u;
            sidQueuedShadow_.cycle[(size_t)reg] = 0u;
        }
    }

    void runtimeSetPitchBendRangeSemis(int ch, float semis) noexcept {
        const int c = std::clamp(ch, 0, 15);
        const float s = std::clamp(semis, 0.0f, 48.0f);
        // P1 FIX: Keep the runtime model in sync with the engine. handlePitchBend()
        // computes bendSemis from runtimeModel_.bendRangeSemis(c), so a stale model
        // value causes synth-mode voices to bend with the wrong range even when the
        // BitPerfect engine was updated correctly. VST already does both.
        runtimeModel_.setBendRangeSemis(c, s);
        if (bpe_()) bpe_()->setPitchBendRangeSemis(c, s);
    }

    void runtimePolicySetFilterDrive(float value) noexcept {
        // Filter Drive is wired: output_gain_bias drives output_drive scaling in
        // sid_static_params.h via: output_drive *= pow(10, output_gain_bias_dB / 20).
        // At value=1.0, normal synth/register mode still reaches 12 dB, but
        // DrSID is capped lower so forensic/filter drive cannot recreate the
        // drum-bus distortion stack closed in v232.
        const float drive = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
        const float maxDriveDb = runtimeIsDrSidEnabled() ? 4.5f : 12.0f;
        auto post = runtimeModel_.measuredPosterior();
        post.output_gain_bias = drive * maxDriveDb;
        runtimeModel_.setMeasuredPosterior(post);
    }

    void runtimePolicyHandleArpRate(float value) noexcept {
        runtimeModel_.setArpActiveFlag(runtimeIsArpEnabled());
        const bool oldFollowHost = runtimeModel_.followHostTempoArp();
        const bool newFollowHost = value < 0.005f;
        runtimeModel_.setFollowHostTempoArp(newFollowHost);
        if (!arp_()) return;
        if (runtimeModel_.followHostTempoArp() && runtimeHostSurface_().hostTempo > 1.0) {
            arp_()->setRateTempo(static_cast<float>(runtimeHostSurface_().hostTempo), 1.0f);
        } else {
            arp_()->setRate(value);
        }
        if (!oldFollowHost && newFollowHost) rewindArpPhase();
    }

    void runtimePolicyHandleSeqEnable(float value) noexcept {
        const bool requestedEnable = value > 0.5f;
        if (requestedEnable && !ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_)) {
            // v943: raw SeqEnable cannot arm a dormant sequencer under SynthMode.
            // DrSID/SID808 remains a valid drum sequencer authority. Canonicalize off immediately only when helper rejects it.
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);
            runtimeModel_.setSeqLastNote(-1);
            runtimeModel_.setSeqSamplesUntilStep(-1.0);
            runtimeModel_.setSeqStep(0);
            seqEngine_.resetPhase();
            prevSeqEnabled_ = false;
            kitSequencerLastStep_ = -1;
            return;
        }
        if (!requestedEnable) {
            kitSequencerLastStep_ = -1;
            if (runtimeModel_.seqLastNote() >= 0 && defaultEventChannel_ != kSidUnresolvedChannel) {
                handleNoteOff(defaultEventChannel_, static_cast<uint8_t>(std::clamp(runtimeModel_.seqLastNote(), 0, 127)));
            }
            runtimeModel_.setSeqLastNote(-1);
            runtimeModel_.setSeqSamplesUntilStep(-1.0);
            runtimeModel_.setSeqStep(0);
            seqEngine_.resetPhase();
            prevSeqEnabled_ = false;
        }
    }

    void runtimePolicyHandleSeqTempo(float value) noexcept {
        const bool oldFollowHost = runtimeModel_.followHostTempoSeq();
        const bool newFollowHost = value < 0.005f;
        runtimeModel_.setFollowHostTempoSeq(newFollowHost);
        if (oldFollowHost == newFollowHost) return;
        if (newFollowHost) {
            const double hostBeat = (std::isfinite(runtimeHostSurface_().hostBeatPosition) &&
                                     runtimeHostSurface_().hostBeatPosition >= 0.0)
                                        ? runtimeHostSurface_().hostBeatPosition
                                        : 0.0;
            if (runtimeHostSurface_().transportPlaying) {
                armSequencerTransportRestart_(hostBeat);
            } else {
                seqInternalBeatPosition_ = 0.0;
                clearSequencerTransportRestart_();
                seqEngine_.resetPhase();
                runtimeModel_.setSeqStep(0);
            }
        } else {
            clearSequencerTransportRestart_();
            if (runtimeHostSurface_().transportPlaying &&
                std::isfinite(runtimeHostSurface_().hostBeatPosition) &&
                runtimeHostSurface_().hostBeatPosition >= 0.0) {
                seqInternalBeatPosition_ = runtimeHostSurface_().hostBeatPosition;
            }
            syncSequencerCursorToBeat_(seqInternalBeatPosition_);
        }
    }

    void runtimePolicyHandleVirtualGate(float value) noexcept {
        const int virtualNote = std::clamp((int)std::lround(params_[(size_t)kParamVirtualNote].load(std::memory_order_relaxed) * 127.f), 0, 127);
        const bool virtualGate = value > 0.5f;
        const uint8_t virtCh = defaultEventChannel_;
        if (!runtimeHostSurface_().transportPlaying) {
            if (prevVirtualGate_ && virtCh != kSidUnresolvedChannel) {
                handleNoteOff(virtCh, static_cast<uint8_t>(prevVirtualNote_), prevVirtualNoteId_);
            }
            prevVirtualGate_ = false;
            prevVirtualNote_ = virtualNote;
            prevVirtualNoteId_ = -1;
            return;
        }
        if (virtualGate != prevVirtualGate_ || (virtualGate && virtualNote != prevVirtualNote_)) {
            if (prevVirtualGate_ && virtCh != kSidUnresolvedChannel) {
                handleNoteOff(virtCh, static_cast<uint8_t>(prevVirtualNote_), prevVirtualNoteId_);
            }
            if (virtualGate && virtCh != kSidUnresolvedChannel) {
                prevVirtualNoteId_ = virtualNoteIdCounter_++;
                handleNoteOn(virtCh, static_cast<uint8_t>(virtualNote), 100, prevVirtualNoteId_);
            } else if (!virtualGate) {
                prevVirtualNoteId_ = -1;
            }
            prevVirtualGate_ = virtualGate;
            prevVirtualNote_ = virtualNote;
        }
    }

    void runtimePolicyHandleSynthModeEnable(float value) noexcept {
        const bool synthNow = value > 0.5f;
        if (synthNow != prevSynthMode_) {
            prevSynthMode_ = synthNow;
            if (synthNow) {
                // P1 FIX: Do the full synth-mode materialization immediately, not in
                // applyTransientControls_(). applyTransientControls_ reads renderParams_
                // which is only flushed from params_[] on the next process() call; during
                // state restore (applyStateRootCanonical → projectStateToBackends) the
                // dirty params are in the atomic shadow but renderParams_ is still stale,
                // so applyTransientControls_ would see no change and skip the reseed
                // entirely. The VST version materializes immediately in its own policy.
                runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);
                runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);
                runtimeModel_.setArpActiveFlag(false);
                dirty_[(size_t)kParamArpEnable].store(false, std::memory_order_relaxed);
                dirty_[(size_t)kParamSeqEnable].store(false, std::memory_order_relaxed);
                runtimeApplyAllNotesOffPerformanceReset_(-1, false);
                engineBank_.sidWriteQueue.clear();
                for (auto& v : engineBank_.synthVoices) v.reset();
                reseedSynthModeRealtimeState_(0, 0, false);
            } else {
                for (int v = 0; v < 3; ++v)
                    if (engineBank_.synthVoices[(size_t)v].active)
                        hardSynthModeVoiceOff_(v, 0u, 0u, true);
            }
        }
    }

    // Fix #8: Per-channel MixFX processor arrays (render-thread owned after prepare).
    // One MixFxProcessor[kMixFxSlotsPerChannel] per MIX channel.
    // Synced from the live GuiRealtimeModelSnapshot_::mix each time the mix model changes
    // (via syncMixFxProcessors_() on the render side) and applied sample-by-sample
    // in applyMixFxToOutputs_() after the DIGI layer.
    using MixFxChain = ArpSID::Audio::MixFxProcessor[ArpSID::GUI::kMixFxSlotsPerChannel];
    std::array<MixFxChain, ArpSID::GUI::kMixChannelCount> mixFxProcessors_{};

    void syncMixFxProcessorsFromModel_(const ArpSID::GUI::MixPanelModel& model) noexcept {
        for (std::uint8_t ch = 0; ch < ArpSID::GUI::kMixChannelCount; ++ch) {
            const auto& chan = model.channels[(std::size_t)ch];
            for (std::uint8_t sl = 0; sl < ArpSID::GUI::kMixFxSlotsPerChannel; ++sl) {
                mixFxProcessors_[(std::size_t)ch][(std::size_t)sl].setParams(chan.fxSlots[(std::size_t)sl]);
            }
        }
    }

    void syncMixFxProcessors_() noexcept {
        syncMixFxProcessorsFromModel_(guiRealtimeRender_.mix);
    }

    void prepareMixFxProcessors_() noexcept {
        for (auto& chain : mixFxProcessors_) {
            for (auto& proc : chain) proc.prepare(static_cast<float>(sampleRate_));
        }
    }

    // Apply the master channel (ch 0) FX chain to the stereo output.
    // The MIX panel uses channel 0 as the master bus.
    // Per-voice channel routing is intentionally disabled in this release; only the master
    // bus FX chain is applied to the final stereo pair.
    void applyMixFxToOutputs_(float** outputs, int numFrames) noexcept {
        if (!outputs || !outputs[0] || numFrames <= 0) return;
        const auto& masterModel = guiRealtimeRender_.mix.channels[0];
        auto& masterChain = mixFxProcessors_[0];
        bool anyActive = false;
        for (std::uint8_t sl = 0; sl < ArpSID::GUI::kMixFxSlotsPerChannel; ++sl) {
            if (masterModel.fxSlots[sl].type != ArpSID::GUI::MixFxType::None &&
                !masterModel.fxSlots[sl].bypass) {
                anyActive = true; break;
            }
        }
        if (!anyActive) return;
        for (int i = 0; i < numFrames; ++i) {
            float L = outputs[0][i];
            float R = outputs[1] ? outputs[1][i] : L;
            ArpSID::Audio::applyMixFxChain(masterChain, masterModel.fxSlots, L, R);
            outputs[0][i] = std::clamp(L, -1.0f, 1.0f);
            if (outputs[1]) outputs[1][i] = std::clamp(R, -1.0f, 1.0f);
        }
    }

    // P2 FIX: Limiter and reverb are now implemented in AU, matching VST behaviour.
    // SimpleLimiter: zero-latency stereo peak limiter (envelope follower, brick-wall attack).
    // Shared with VST — see include/arpsid/core/sid_audio_processors.h
    using AUSimpleLimiter = ArpSID::SimpleLimiter;
    AUSimpleLimiter auLimiter_;
    bool  auLimiterEnabled_   = false;
    float auLimiterThreshold_ = 0.97f;
    float smoothedLimiterWet_ = 0.0f;
    float smoothedLimiterThreshold_ = 0.97f;
    float auLimiterAttackMs_  = 0.5f;
    float auLimiterReleaseMs_ = 200.0f;

    // Shared with VST — see include/arpsid/core/sid_audio_processors.h
    using AUSchroederReverb = ArpSID::SchroederReverb;
    AUSchroederReverb auReverb_;
    float auReverbMix_ = 0.0f;
    float smoothedReverbMix_ = 0.0f;
    uint32_t auPostFxSilentFrames_ = 0u;
    // Configurable quiet-tail reset threshold. Default 0.25 s can cut quiet
    // tails prematurely; expose as a tunable so host can set a longer hold time.
    float auReverbQuietResetSeconds_ = 0.25f;
    // Limiter clamp hit counter: incremented whenever the post-FX output is
    // clamped to ±1.0. Non-zero values mean the signal was too hot for the limiter.
    std::atomic<uint64_t> auLimiterClampHitCount_{0u};

    // AUv2 pure 1:1 SID output mode.
    // When enabled, the AU emits the SID engine output itself: no MIX FX, no
    // AU reverb, no AU limiter, no hard ±1 clamp, and no HI-FI/Transcendence
    // post layer. Only NaN/denormal cleanup remains so the AU host never sees
    // invalid float samples. This is intentionally outside ParamID space so no
    // historic automation IDs move. It is persisted by AU state keys instead.
    std::atomic<uint8_t> pureSid1Q1OutputMode_{0u};
    bool pureSid1Q1WasActive_ = false;

    // internal REC source: capture the same pre-post-FX pure SID/AU
    // engine stream that PURE 1:1 output emits, but without requiring the AU
    // output mode to be enabled. The GUI starts this on the main thread with a
    // preallocated bounded buffer; render only writes into existing storage and
    // updates atomics. STOP flips active=false and waits for in-flight render
    // capture to drain before copying, so there is no RT allocation or lock.
    std::vector<float> pureSid1Q1RecCaptureMono_{};
    std::atomic<uint8_t> pureSid1Q1RecCaptureActive_{0u};
    std::atomic<std::uint32_t> pureSid1Q1RecCaptureWriteFrames_{0u};
    std::atomic<std::uint32_t> pureSid1Q1RecCaptureDroppedFrames_{0u};
    std::atomic<float> pureSid1Q1RecCapturePeak_{0.0f};
    std::atomic<float> pureSid1Q1RecCaptureRms_{0.0f};
    std::atomic<std::uint32_t> pureSid1Q1RecCaptureInFlight_{0u};
    // audit P2.15: control-side capture state machine. The render/control lease
    // (pureSid1Q1RecCaptureInFlight_) protects the render thread from the control
    // thread, but it does NOT serialize two CONTROL operations (e.g. a UI timer
    // firing copyAndStop while a button press fires start). Every public capture
    // operation CASes this state to Busy to claim exclusive control access and
    // fails closed if another operation already holds it.
    enum class PureSidCaptureState : std::uint8_t { Idle = 0, Recording = 1, Busy = 2 };
    std::atomic<std::uint8_t> pureSid1Q1CaptureState_{static_cast<std::uint8_t>(PureSidCaptureState::Idle)};
    // Published allocation mirror. Render/status paths never inspect vector
    // metadata, and zero means fail closed while storage is being replaced.
    std::atomic<std::uint32_t> pureSid1Q1RecCaptureCapacity_{0u};
    std::atomic<double> pureSid1Q1RecCaptureSampleRate_{44100.0};
    // C64/PSID Pure-SID REC captures the deterministic internal $D418 volume-DAC
    // stream when a tune writes $D418. The held value persists across render blocks
    // so repeated identical nibbles and sparse writes reconstruct as a real zero-
    // order-held digi waveform instead of as silence between writes.
    float pureSid1Q1C64D418Held_ = 0.0f;
    bool pureSid1Q1C64D418Observed_ = false;

    // HI-FI Transcendence: render-thread post-SID player mode.
    // Fixed-storage/no-allocation processor; PureEmulation bypass preserves original SID output.
    ArpSID::SidHiFiTranscendence hifiTranscendence_{};
    ArpSID::SidHiFiConfig lastHiFiConfig_{};

    ArpSID::SidHiFiConfig resolveHiFiConfigFromRenderParams_() const noexcept {
        return ArpSID::sidHiFiConfigFromNormalized(renderParams_[(size_t)kParamHiFiEnable],
                                                   renderParams_[(size_t)kParamHiFiQuality],
                                                   renderParams_[(size_t)kParamHiFiOversampling],
                                                   renderParams_[(size_t)kParamHiFiMasterWidth],
                                                   renderParams_[(size_t)kParamHiFiTapeSaturation],
                                                   renderParams_[(size_t)kParamHiFiAnalogWarmth],
                                                   renderParams_[(size_t)kParamHiFiPsychoExciter],
                                                   renderParams_[(size_t)kParamHiFiStereoDepth],
                                                   renderParams_[(size_t)kParamHiFiVoiceDiffuser]);
    }

    ArpSID::ArpSIDForensicConfig resolveEffectiveForensicConfigForBlock_() const noexcept {
        return ArpSID::buildEffectiveForensicConfigFromParams(
            [this](ArpSID::ParamID pid) noexcept -> float { return renderParams_[(size_t)pid]; },
            runtimeModel_.variantProfile(),
            runtimeModel_.staticParams());
    }

    ArpSID::ArpSIDForensicConfig resolveHiFiForensicConfigForBlock_() const noexcept {
        // HI-FI warmth/tape/body must follow the same effective forensic
        // authority as the SID engines. The previous v393-v398 code passed a
        // default forensic config, so temperature/supply/freeze controls could
        // affect the SID core but not the HI-FI chain.
        return resolveEffectiveForensicConfigForBlock_();
    }

    void syncHiFiTranscendenceForBlock_() noexcept {
        ArpSID::SidHiFiConfig cfg = resolveHiFiConfigFromRenderParams_();
        const ArpSID::ArpSIDForensicConfig forensic = resolveHiFiForensicConfigForBlock_();
        hifiTranscendence_.configure(sampleRate_, forensic, cfg);
        lastHiFiConfig_ = cfg;
    }

        // single post-audio HI-FI authority used by synth/drum/SIDCore paths.
    void publishHiFiTelemetryFromProcessor_(const ArpSID::SidHiFiConfig& cfg) noexcept {
        telemetryHiFiEnabled_.store(cfg.quality != ArpSID::HiFiQuality::PureEmulation ? 1u : 0u, std::memory_order_relaxed);
        telemetryHiFiQuality_.store(static_cast<uint8_t>(cfg.quality), std::memory_order_relaxed);
        telemetryHiFiOversampling_.store(static_cast<uint8_t>(std::clamp(cfg.oversampling, 1, ArpSID::SidHiFiTranscendence::kMaxOversampling)), std::memory_order_relaxed);
        telemetryHiFiDryPeak_.store(ArpSIDSanitizeTelemetryUnitFloat(hifiTranscendence_.lastDryPeak()), std::memory_order_relaxed);
        telemetryHiFiWetPeak_.store(ArpSIDSanitizeTelemetryUnitFloat(hifiTranscendence_.lastWetPeak()), std::memory_order_relaxed);
        telemetryHiFiDeltaPeak_.store(ArpSIDSanitizeTelemetryUnitFloat(hifiTranscendence_.lastDeltaPeak()), std::memory_order_relaxed);
        telemetryHiFiMonoCorrelation_.store(ArpSIDSanitizeTelemetryBipolarFloat(hifiTranscendence_.lastMonoCorrelation()), std::memory_order_relaxed);
        telemetryHiFiSafetyGain_.store(ArpSIDSanitizeTelemetryUnitFloat(hifiTranscendence_.lastSafetyGain()), std::memory_order_relaxed);
    }

    void publishHiFiBypassTelemetry_() noexcept {
        ArpSID::SidHiFiConfig cfg{};
        telemetryHiFiEnabled_.store(0u, std::memory_order_relaxed);
        telemetryHiFiQuality_.store(static_cast<uint8_t>(cfg.quality), std::memory_order_relaxed);
        telemetryHiFiOversampling_.store(static_cast<uint8_t>(cfg.oversampling), std::memory_order_relaxed);
        telemetryHiFiDryPeak_.store(0.0f, std::memory_order_relaxed);
        telemetryHiFiWetPeak_.store(0.0f, std::memory_order_relaxed);
        telemetryHiFiDeltaPeak_.store(0.0f, std::memory_order_relaxed);
        telemetryHiFiMonoCorrelation_.store(1.0f, std::memory_order_relaxed);
        telemetryHiFiSafetyGain_.store(1.0f, std::memory_order_relaxed);
    }

    void applyAuReverbLimiterTimeline_(float** outputs,
                                            int numFrames,
                                            const SidTimedEventQueue& queue,
                                            SidPostFxAutomationState state) noexcept {
        if (!outputs || !outputs[0] || numFrames <= 0) return;
        float* writeR = outputs[1] ? outputs[1] : outputs[0];
        const bool sharedOutputBus = writeR == outputs[0];
        auLimiter_.setAttackMs(state.limiterAttackMs, sampleRate_);
        auLimiter_.setReleaseMs(state.limiterReleaseMs, sampleRate_);
        const float quietSeconds = std::clamp(auReverbQuietResetSeconds_, 0.05f, 10.0f);
        const uint32_t quietResetFrames = static_cast<uint32_t>(std::max(1.0, sampleRate_ * quietSeconds));
        constexpr float drySilenceThreshold = 1.0e-5f;
        constexpr float wetSilenceThreshold = 2.0e-5f;
        int eventIndex = 0;
        for (int i = 0; i < numFrames; ++i) {
            while (eventIndex < queue.count &&
                   static_cast<int>(queue.events[(size_t)eventIndex].sample_offset) <= i) {
                const float oldAttack = state.limiterAttackMs;
                const float oldRelease = state.limiterReleaseMs;
                (void)ArpSID::sidApplyPostFxAutomationEvent(state, queue.events[(size_t)eventIndex]);
                if (state.limiterAttackMs != oldAttack)
                    auLimiter_.setAttackMs(state.limiterAttackMs, sampleRate_);
                if (state.limiterReleaseMs != oldRelease)
                    auLimiter_.setReleaseMs(state.limiterReleaseMs, sampleRate_);
                ++eventIndex;
            }
            float l = ArpSID_sanitizeFloat(outputs[0][i]);
            float r = ArpSID_sanitizeFloat(writeR[i]);
            const float dryPeak = std::max(std::fabs(l), std::fabs(r));
            if (state.reverbMix > 1.0e-4f) {
                float rl = 0.0f, rr = 0.0f;
                auReverb_.process(l, r, rl, rr);
                l += rl * state.reverbMix;
                r += rr * state.reverbMix;
            }
            if (state.limiterEnabled) auLimiter_.processStereoSample(l, r, state.limiterThreshold);
            const float lc = std::clamp(ArpSID_sanitizeFloat(l), -1.0f, 1.0f);
            const float rc = std::clamp(ArpSID_sanitizeFloat(r), -1.0f, 1.0f);
            if (lc != l || rc != r) auLimiterClampHitCount_.fetch_add(1u, std::memory_order_relaxed);
            l = std::fabs(lc) < 1.0e-10f ? 0.0f : lc;
            r = std::fabs(rc) < 1.0e-10f ? 0.0f : rc;
            if (state.reverbMix > 1.0e-4f) {
                const float wetPeak = std::max(std::fabs(l), std::fabs(r));
                if (dryPeak < drySilenceThreshold && wetPeak < wetSilenceThreshold) {
                    if (++auPostFxSilentFrames_ >= quietResetFrames) {
                        auReverb_.reset();
                        auPostFxSilentFrames_ = 0u;
                    }
                } else auPostFxSilentFrames_ = 0u;
            } else auPostFxSilentFrames_ = 0u;
            if (sharedOutputBus) outputs[0][i] = 0.5f * (l + r);
            else { outputs[0][i] = l; writeR[i] = r; }
        }
        smoothedReverbMix_ = state.reverbMix;
        smoothedLimiterWet_ = state.limiterEnabled ? 1.0f : 0.0f;
        smoothedLimiterThreshold_ = state.limiterThreshold;
    }

    void applyAuHiFiTimeline_(float** outputs,
                              int numFrames,
                              const SidTimedEventQueue& queue,
                              SidPostFxAutomationState state) noexcept {
        if (!outputs || !outputs[0] || numFrames <= 0) {
            publishHiFiTelemetryFromProcessor_(ArpSID::sidHiFiConfigFromPostFxState(state));
            return;
        }
        float* right = outputs[1] ? outputs[1] : outputs[0];
        int cursor = 0;
        auto processSegment = [&](int end) noexcept {
            end = std::clamp(end, cursor, numFrames);
            if (end <= cursor) return;
            const ArpSID::SidHiFiConfig cfg = ArpSID::sidHiFiConfigFromPostFxState(state);
            hifiTranscendence_.configure(sampleRate_, resolveHiFiForensicConfigForBlock_(), cfg);
            hifiTranscendence_.processStereo(outputs[0] + cursor, right + cursor, end - cursor);
            lastHiFiConfig_ = cfg;
            cursor = end;
        };
        for (int i = 0; i < queue.count; ++i) {
            const SidTimedEvent& ev = queue.events[(size_t)i];
            if (!ArpSID::sidPostFxEventTargetsHiFi(ev)) continue;
            const int offset = std::clamp(static_cast<int>(ev.sample_offset), 0, numFrames);
            processSegment(offset);
            (void)ArpSID::sidApplyPostFxAutomationEvent(state, ev);
        }
        processSegment(numFrames);
        if (cursor == 0) lastHiFiConfig_ = ArpSID::sidHiFiConfigFromPostFxState(state);
        publishHiFiTelemetryFromProcessor_(lastHiFiConfig_);
    }

    void applyHiFiTranscendenceToOutputs_(float** outputs, int numFrames) noexcept {
        if (!outputs || !outputs[0] || numFrames <= 0) {
            ArpSID::SidHiFiConfig cfg = resolveHiFiConfigFromRenderParams_();
            publishHiFiTelemetryFromProcessor_(cfg);
            return;
        }
        syncHiFiTranscendenceForBlock_();
        hifiTranscendence_.processStereo(outputs[0], outputs[1] ? outputs[1] : outputs[0], numFrames);
        publishHiFiTelemetryFromProcessor_(lastHiFiConfig_);
    }

    void runtimePolicySetLimiterEnabled(bool on) noexcept { auLimiterEnabled_ = on; }
    void runtimePolicySetLimiterThreshold(float value) noexcept { auLimiterThreshold_ = ArpSIDSanitizeTelemetryUnitFloat(value); }
    void runtimePolicySetLimiterAttack(float value) noexcept { auLimiterAttackMs_ = ArpSIDSanitizeTelemetryUnitFloat(value) * 20.0f; auLimiter_.setAttackMs(auLimiterAttackMs_, sampleRate_); }
    void runtimePolicySetLimiterRelease(float value) noexcept { auLimiterReleaseMs_ = 10.0f + ArpSIDSanitizeTelemetryUnitFloat(value) * 990.0f; auLimiter_.setReleaseMs(auLimiterReleaseMs_, sampleRate_); }
    void runtimePolicySetReverbMix(float value) noexcept { auReverbMix_ = ArpSIDSanitizeTelemetryUnitFloat(value); }
    // Configurable reverb tail preservation: seconds of silence before reverb reset.
    void setReverbQuietResetSeconds(float s) noexcept {
        auReverbQuietResetSeconds_ = std::clamp(std::isfinite(s) ? s : 0.25f, 0.05f, 10.0f);
    }
    float reverbQuietResetSeconds() const noexcept { return auReverbQuietResetSeconds_; }
    uint64_t limiterClampHitCount() const noexcept {
        return auLimiterClampHitCount_.load(std::memory_order_relaxed);
    }

    void setPureSid1Q1OutputMode(bool enabled) noexcept {
        pureSid1Q1OutputMode_.store(enabled ? 1u : 0u, std::memory_order_release);
    }
    bool pureSid1Q1OutputModeEnabled() const noexcept {
        return pureSid1Q1OutputMode_.load(std::memory_order_acquire) != 0u;
    }

    // P0-1: wait until no render-thread Pure-SID capture is in flight. Returns true
    // only if the render capture is provably quiescent, so the control thread can
    // safely resize/copy pureSid1Q1RecCaptureMono_ without racing live render access.
    bool waitPureSid1Q1CaptureQuiescent_(int spinLimit) noexcept {
        for (int spin = 0; spin < spinLimit; ++spin) {
            if (pureSid1Q1RecCaptureInFlight_.load(std::memory_order_acquire) == 0u)
                return true;
            // audit P2.16: this is a NON-render (control thread) wait. yield() is
            // not a deterministic back-off — it can spin-return immediately and
            // starve. A fixed short sleep gives a bounded, predictable drain wait.
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        return pureSid1Q1RecCaptureInFlight_.load(std::memory_order_acquire) == 0u;
    }

    bool startPureSid1Q1RecordCapture(std::uint32_t maxFrames) {
        // audit P2.15: claim exclusive control access. Allowed from Idle or
        // Recording (a restart), but never while another control op is Busy.
        std::uint8_t expected = pureSid1Q1CaptureState_.load(std::memory_order_acquire);
        if (expected == static_cast<std::uint8_t>(PureSidCaptureState::Busy) ||
            !pureSid1Q1CaptureState_.compare_exchange_strong(
                expected, static_cast<std::uint8_t>(PureSidCaptureState::Busy),
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return false; // another start/stop/copy is in progress
        }
        const bool started = startPureSid1Q1RecordCaptureLocked_(maxFrames);
        pureSid1Q1CaptureState_.store(
            started ? static_cast<std::uint8_t>(PureSidCaptureState::Recording)
                    : static_cast<std::uint8_t>(PureSidCaptureState::Idle),
            std::memory_order_release);
        return started;
    }

    bool startPureSid1Q1RecordCaptureLocked_(std::uint32_t maxFrames) {
        maxFrames = std::clamp<std::uint32_t>(maxFrames, 1u, ArpSID::GUI::kDigiRecordCaptureMaxFrames);
        pureSid1Q1RecCaptureActive_.store(0u, std::memory_order_release);
        pureSid1Q1RecCaptureCapacity_.store(0u, std::memory_order_release);
        // P0-1: fail closed. Never reassign the capture vector while a render-thread
        // capture may still hold its storage pointer (a stalled/preempted render
        // thread would otherwise observe reallocated/freed storage).
        if (!waitPureSid1Q1CaptureQuiescent_(2000)) return false;
        try {
            pureSid1Q1RecCaptureMono_.assign(maxFrames, 0.0f);
        } catch (...) {
            pureSid1Q1RecCaptureMono_.clear();
            pureSid1Q1RecCaptureWriteFrames_.store(0u, std::memory_order_release);
            pureSid1Q1RecCaptureDroppedFrames_.store(0u, std::memory_order_release);
            pureSid1Q1RecCaptureCapacity_.store(0u, std::memory_order_release);
            return false;
        }
        pureSid1Q1RecCaptureWriteFrames_.store(0u, std::memory_order_release);
        pureSid1Q1RecCaptureDroppedFrames_.store(0u, std::memory_order_release);
        pureSid1Q1RecCapturePeak_.store(0.0f, std::memory_order_release);
        pureSid1Q1RecCaptureRms_.store(0.0f, std::memory_order_release);
        pureSid1Q1RecCaptureSampleRate_.store(
            (std::isfinite(sampleRate_) && sampleRate_ > 100.0) ? sampleRate_ : 44100.0,
            std::memory_order_release);
        pureSid1Q1C64D418Held_ = 0.0f;
        pureSid1Q1C64D418Observed_ = false;
        // Capacity is the final storage publication. Active is published only
        // after every vector/state initialization write is complete.
        pureSid1Q1RecCaptureCapacity_.store(maxFrames, std::memory_order_release);
        pureSid1Q1RecCaptureActive_.store(1u, std::memory_order_release);
        return true;
    }

    void pureSid1Q1RecordCaptureStatus(std::uint32_t* frameCount,
                                        double* sampleRate,
                                        std::uint32_t* droppedFrames,
                                        float* peak,
                                        float* rms) const noexcept {
        const std::uint32_t capacity =
            pureSid1Q1RecCaptureCapacity_.load(std::memory_order_acquire);
        if (frameCount) *frameCount = std::min<std::uint32_t>(
            pureSid1Q1RecCaptureWriteFrames_.load(std::memory_order_acquire),
            capacity);
        if (sampleRate) *sampleRate =
            pureSid1Q1RecCaptureSampleRate_.load(std::memory_order_acquire);
        if (droppedFrames) *droppedFrames = pureSid1Q1RecCaptureDroppedFrames_.load(std::memory_order_acquire);
        if (peak) *peak = std::clamp(pureSid1Q1RecCapturePeak_.load(std::memory_order_acquire), 0.0f, 1.0f);
        if (rms) *rms = std::clamp(pureSid1Q1RecCaptureRms_.load(std::memory_order_acquire), 0.0f, 1.0f);
    }

    bool copyAndStopPureSid1Q1RecordCapture(float* dst,
                                            std::uint32_t maxFrames,
                                            std::uint32_t* frameCount,
                                            double* sampleRate,
                                            std::uint32_t* droppedFrames) {
        // audit P2.15: only valid from the Recording state; CAS to Busy to claim
        // exclusive control access and fail closed if a start/stop/copy is racing.
        std::uint8_t expected = static_cast<std::uint8_t>(PureSidCaptureState::Recording);
        if (!pureSid1Q1CaptureState_.compare_exchange_strong(
                expected, static_cast<std::uint8_t>(PureSidCaptureState::Busy),
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            if (frameCount) *frameCount = 0u;
            if (sampleRate) *sampleRate = pureSid1Q1RecCaptureSampleRate_.load(std::memory_order_acquire);
            if (droppedFrames) *droppedFrames = pureSid1Q1RecCaptureDroppedFrames_.load(std::memory_order_acquire);
            return false;
        }
        const bool ok = copyAndStopPureSid1Q1RecordCaptureLocked_(dst, maxFrames, frameCount, sampleRate, droppedFrames);
        // Capture is stopped after a copy regardless of result.
        pureSid1Q1CaptureState_.store(static_cast<std::uint8_t>(PureSidCaptureState::Idle), std::memory_order_release);
        return ok;
    }

    bool copyAndStopPureSid1Q1RecordCaptureLocked_(float* dst,
                                            std::uint32_t maxFrames,
                                            std::uint32_t* frameCount,
                                            double* sampleRate,
                                            std::uint32_t* droppedFrames) {
        pureSid1Q1RecCaptureActive_.store(0u, std::memory_order_release);
        // Revoke the storage publication before waiting. A callback that already
        // acquired a lease either sees active/capacity revoked and exits, or is
        // counted by inFlight and must drain before the vector is read.
        const std::uint32_t capacity =
            pureSid1Q1RecCaptureCapacity_.exchange(0u, std::memory_order_acq_rel);
        // P0-1: fail closed. If the render capture did not drain, do NOT read the
        // vector (it may still be owned by a live render-thread capture).
        if (!waitPureSid1Q1CaptureQuiescent_(4000)) {
            if (frameCount) *frameCount = 0u;
            if (sampleRate) *sampleRate =
                pureSid1Q1RecCaptureSampleRate_.load(std::memory_order_acquire);
            if (droppedFrames) *droppedFrames = pureSid1Q1RecCaptureDroppedFrames_.load(std::memory_order_acquire);
            return false;
        }
        const std::uint32_t written = std::min<std::uint32_t>(
            pureSid1Q1RecCaptureWriteFrames_.load(std::memory_order_acquire),
            capacity);
        const std::uint32_t n = std::min<std::uint32_t>(written, maxFrames);
        if (dst && n > 0u) std::memcpy(dst, pureSid1Q1RecCaptureMono_.data(), static_cast<std::size_t>(n) * sizeof(float));
        if (frameCount) *frameCount = n;
        if (sampleRate) *sampleRate =
            pureSid1Q1RecCaptureSampleRate_.load(std::memory_order_acquire);
        if (droppedFrames) *droppedFrames = pureSid1Q1RecCaptureDroppedFrames_.load(std::memory_order_acquire) + (written > maxFrames ? (written - maxFrames) : 0u);
        pureSid1Q1RecCapturePeak_.store(0.0f, std::memory_order_release);
        pureSid1Q1RecCaptureRms_.store(0.0f, std::memory_order_release);
        return n > 0u;
    }

    void capturePureSid1Q1RecordSource_(float** outputs, int numFrames) noexcept {
        if (!outputs || !outputs[0] || numFrames <= 0) return;
        // The lease is the first access to capture-owned shared state. Control
        // may revoke capacity and replace the vector only after this count drains.
        pureSid1Q1RecCaptureInFlight_.fetch_add(1u, std::memory_order_acq_rel);
        struct CaptureExitGuard {
            std::atomic<std::uint32_t>* counter;
            ~CaptureExitGuard() { if (counter) counter->fetch_sub(1u, std::memory_order_acq_rel); }
        } guard{ &pureSid1Q1RecCaptureInFlight_ };
        if (pureSid1Q1RecCaptureActive_.load(std::memory_order_acquire) == 0u) return;
        const std::uint32_t cap =
            pureSid1Q1RecCaptureCapacity_.load(std::memory_order_acquire);
        if (cap == 0u) return;
        std::uint32_t w = pureSid1Q1RecCaptureWriteFrames_.load(std::memory_order_acquire);
        if (w >= cap) {
            pureSid1Q1RecCaptureDroppedFrames_.fetch_add(static_cast<std::uint32_t>(std::max(0, numFrames)), std::memory_order_acq_rel);
            return;
        }
        const std::uint32_t room = cap - w;
        const std::uint32_t n = std::min<std::uint32_t>(room, static_cast<std::uint32_t>(numFrames));
        float* dst = pureSid1Q1RecCaptureMono_.data();
        float blockPeak = 0.0f;
        double blockSumSq = 0.0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const float l = ArpSID_sanitizeFloat(outputs[0][i]);
            const float r = ArpSID_sanitizeFloat(outputs[1] ? outputs[1][i] : outputs[0][i]);
            float mono = 0.5f * (l + r);
            if (std::fabs(mono) < 1.0e-10f) mono = 0.0f;
            dst[w + i] = mono;
            blockPeak = std::max(blockPeak, std::fabs(mono));
            blockSumSq += static_cast<double>(mono) * static_cast<double>(mono);
        }
        pureSid1Q1RecCapturePeak_.store(ArpSIDSanitizeTelemetryUnitFloat(blockPeak), std::memory_order_release);
        pureSid1Q1RecCaptureRms_.store((n && std::isfinite(blockSumSq) && blockSumSq > 0.0) ? ArpSIDSanitizeTelemetryUnitFloat(static_cast<float>(std::sqrt(blockSumSq / static_cast<double>(n)))) : 0.0f, std::memory_order_release);
        pureSid1Q1RecCaptureWriteFrames_.store(w + n, std::memory_order_release);
        if (n < static_cast<std::uint32_t>(numFrames)) {
            pureSid1Q1RecCaptureDroppedFrames_.fetch_add(static_cast<std::uint32_t>(numFrames) - n, std::memory_order_acq_rel);
        }
    }


    template <typename RenderWriteT>
    void captureC64D418PureSid1Q1RecordSource_(const RenderWriteT* writes,
                                                std::uint32_t writeCount,
                                                int numFrames) noexcept {
        if (!writes || writeCount == 0u || numFrames <= 0) return;
        // Acquire the storage/state lease before reading active/capacity or
        // touching the held/observed D418 stream state.
        pureSid1Q1RecCaptureInFlight_.fetch_add(1u, std::memory_order_acq_rel);
        struct CaptureExitGuard {
            std::atomic<std::uint32_t>* counter;
            ~CaptureExitGuard() { if (counter) counter->fetch_sub(1u, std::memory_order_acq_rel); }
        } guard{ &pureSid1Q1RecCaptureInFlight_ };
        if (pureSid1Q1RecCaptureActive_.load(std::memory_order_acquire) == 0u) return;
        const std::uint32_t cap =
            pureSid1Q1RecCaptureCapacity_.load(std::memory_order_acquire);
        if (cap == 0u) return;
        bool hasD418 = false;
        for (std::uint32_t i = 0; i < writeCount; ++i) {
            if (writes[i].chip == 0u && ArpSID::C64::isD418Register(writes[i].reg)) { hasD418 = true; break; }
        }
        // TS-reference $D418 semantics are a continuous zero-order-held DAC
        // stream once the first volume-register write has been observed. The
        // previous implementation captured only render blocks that themselves
        // contained a $D418 write; blocks between sparse digi writes were skipped,
        // compressing REC/MON time and breaking real volume-DAC PCM. Before the
        // first $D418 write we still return so non-digi C64 playback can keep the
        // ordinary Pure SID audio-capture behavior. After observation, every block
        // appends the held sample even when no new write occurs.
        if (hasD418) pureSid1Q1C64D418Observed_ = true;
        if (!hasD418 && !pureSid1Q1C64D418Observed_) return;
        std::uint32_t w = pureSid1Q1RecCaptureWriteFrames_.load(std::memory_order_acquire);
        if (w >= cap) {
            pureSid1Q1RecCaptureDroppedFrames_.fetch_add(static_cast<std::uint32_t>(std::max(0, numFrames)), std::memory_order_acq_rel);
            return;
        }
        const std::uint32_t n = std::min<std::uint32_t>(cap - w, static_cast<std::uint32_t>(numFrames));
        float* dst = pureSid1Q1RecCaptureMono_.data();
        float held = ArpSID_sanitizeFloat(pureSid1Q1C64D418Held_);
        std::uint32_t wi = 0u;
        float blockPeak = 0.0f;
        double blockSumSq = 0.0;
        for (std::uint32_t frame = 0; frame < n; ++frame) {
            while (wi < writeCount && writes[wi].sample <= static_cast<int>(frame)) {
                if (writes[wi].chip == 0u && ArpSID::C64::isD418Register(writes[wi].reg)) {
                    held = ArpSID::C64::d418NibbleToBipolar(writes[wi].value);
                }
                ++wi;
            }
            dst[w + frame] = held;
            blockPeak = std::max(blockPeak, std::fabs(held));
            blockSumSq += static_cast<double>(held) * static_cast<double>(held);
        }
        pureSid1Q1C64D418Held_ = held;
        pureSid1Q1RecCapturePeak_.store(ArpSIDSanitizeTelemetryUnitFloat(blockPeak), std::memory_order_release);
        pureSid1Q1RecCaptureRms_.store((n && std::isfinite(blockSumSq) && blockSumSq > 0.0) ? ArpSIDSanitizeTelemetryUnitFloat(static_cast<float>(std::sqrt(blockSumSq / static_cast<double>(n)))) : 0.0f, std::memory_order_release);
        pureSid1Q1RecCaptureWriteFrames_.store(w + n, std::memory_order_release);
        if (n < static_cast<std::uint32_t>(numFrames)) {
            pureSid1Q1RecCaptureDroppedFrames_.fetch_add(static_cast<std::uint32_t>(numFrames) - n, std::memory_order_acq_rel);
        }
    }

    void sanitizePureSid1Q1Outputs_(float** outputs, int numFrames) noexcept {
        if (!outputs || !outputs[0] || numFrames <= 0) return;
        for (int i = 0; i < numFrames; ++i) {
            float l = ArpSID_sanitizeFloat(outputs[0][i]);
            float r = ArpSID_sanitizeFloat(outputs[1] ? outputs[1][i] : outputs[0][i]);
            if (std::fabs(l) < 1.0e-10f) l = 0.0f;
            if (std::fabs(r) < 1.0e-10f) r = 0.0f;
            outputs[0][i] = l;
            if (outputs[1] && outputs[1] != outputs[0]) outputs[1][i] = r;
        }
    }

    void clearDrumBridgePerformanceState_() noexcept {
        if (componentFlavor_ != ArpSID::ComponentFlavor::Sid808 &&
            componentFlavor_ != ArpSID::ComponentFlavor::DrumMachine) {
            return;
        }
        drumEngineBridge_.allNotesOff();
        drumBridgeDcBlockerReset_();
    }

    void runtimeApplyAllNotesOffPerformanceReset_(int channel = -1, bool clearPhysicalPedals = false) noexcept {
        // all render-visible silence/reset entrypoints must update the same
        // performance mirrors that factory-root replay trusts. Calling
        // runtimeRenderHostAllNotesOff*() directly silences engines, but leaves the
        // kernel-owned held-note / pedal mirrors alive. A later factory root could
        // then replay stale held notes or re-emit sustain/sostenuto over the new patch.
        if (channel < 0) {
            clearLivePerformanceMirrors_(clearPhysicalPedals);
            runtimeModel_.clearLiveMidiState();
            runtimeModel_.clearIdentityMirrors();
            ArpSID::runtimeRenderHostAllNotesOff(*this);
        } else {
            clearHeldIngressChannel_(static_cast<uint8_t>(channel & 0x0F));
            if (clearPhysicalPedals) clearLivePedalMirrorsChannel_(static_cast<uint8_t>(channel & 0x0F));
            runtimeModel_.clearLiveMidiChannel(channel);
            runtimeModel_.clearIdentityMirrors();
            ArpSID::runtimeRenderHostAllNotesOffChannel(*this, channel);
        }
        clearDrumBridgePerformanceState_();
    }

    void runtimeApplyHardPanicPerformanceReset_() noexcept {
        // One canonical panic/reset endpoint for UI parameter panic,
        // render-time transient panic, host reset, kernelPanic(), and critical
        // fallback cleanup. This clears queue fallbacks, held-note mirrors, physical
        // pedal mirrors, runtime live MIDI state, identity mirrors, and then resets
        // every audible backend.
        clearAllPendingMidiFallbacks_();
        runtimeModel_.clearTransientEvents();
        runtimeModel_.clearLiveMidiState();
        runtimeModel_.clearIdentityMirrors();
        ArpSID::runtimeRenderHostPanic(*this);
        clearDrumBridgePerformanceState_();
    }

    void runtimePolicyHandlePanic(float value) noexcept {
        if (value > 0.5f) {
            runtimeApplyHardPanicPerformanceReset_();
        }
    }
    void runtimePolicyApplyForensicConfig() noexcept {
        // Rebuild forensic config from params and push to all backends, matching
        // the VST3 path (ArpSIDProcessorPhase2::applyForensicConfig_).
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(false);
    }

    void runtimeApplyProjectedParameterBody(uint32_t target, float value) noexcept {
        const int id = static_cast<int>(target);
        runtimeStageNormalizedParameterOnly(target, value);
        const float clean = ArpSID::canonicalIsValidParamTarget(target, static_cast<uint32_t>(kNumParams))
            ? renderParams_[static_cast<size_t>(target)]
            : ArpSID::sanitizeNormalizedParamValue(id, value, ArpSID::defaultNormalizedParamValue(id));
        // v947: staging owns raw-value sanitization; backend/policy side effects
        // must consume the already-staged clean value so engine internals cannot
        // diverge from params_/renderParams_/runtimeModel_ on NaN/Inf/out-of-range
        // automation.
        if (!ArpSID::runtimeApplyProjectedBackendParameter(*this, target, clean)) {
            (void)ArpSID::runtimeApplyProjectedAdapterPolicyParameter(*this, target, clean);
        }
        if (runtimeIsSynthModeEnabled()) {
            if (id == static_cast<int>(kParamVoiceMode) || id == static_cast<int>(kParamVoiceSpread)) {
                if (auto* vp = runtimeVoicePolicy_()) {
                    vp->setPlayMode(ArpSID::sidPlayModeFromCanonicalVoiceMode(synthModeVoiceMode_()));
                    const int uniCount = ArpSID::requestedUnisonCountFromNormalizedSpread(renderParams_[(size_t)kParamVoiceSpread]);
                    vp->setUnisonCount(ArpSID::sidRegProjectedUnisonCount(uniCount));
                }
            }
            reseedSynthModeRealtimeState_(0, 0u, true);
            bool activeChannels[16]{};
            for (const auto& sv : engineBank_.synthVoices) {
                if (!sv.active) continue;
                activeChannels[std::clamp<int>(sv.channel, 0, 15)] = true;
            }
            bool anyActive = false;
            for (int ch = 0; ch < 16; ++ch) {
                if (!activeChannels[ch]) continue;
                anyActive = true;
                const float bendSemis = runtimeModel_.pitchBendNorm(ch) * runtimeModel_.bendRangeSemis(ch);
                ArpSID::applySynthModePitchBendToVoices(engineBank_.synthVoices,
                                                        engineBank_.sidWriteQueue,
                                                        currentSidClockHz_(),
                                                        bendSemis,
                                                        ch,
                                                        0u,
                                                        0u);
            }
            if (!anyActive) {
                const int focusedChannel = std::clamp<int>(defaultEventChannel_, 0, 15);
                const float bendSemis = runtimeModel_.pitchBendNorm(focusedChannel) * runtimeModel_.bendRangeSemis(focusedChannel);
                ArpSID::applySynthModePitchBendToVoices(engineBank_.synthVoices,
                                                        engineBank_.sidWriteQueue,
                                                        currentSidClockHz_(),
                                                        bendSemis,
                                                        focusedChannel,
                                                        0u,
                                                        0u);
            }
        }
    }

    void applyNormalizedParameterRenderOwned_(uint32_t target, float value) noexcept {
        // v912 runtime-owner guard closure: render/test/teardown paths must not
        // dereference runtimeExecutionOwner_ while the owner is temporarily absent.
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->applyProjectedNormalizedParameter(target, value);
    }



    void updateSerializableStateTemplate_(const SidStateRootV1& root) noexcept {
        // RT-safe producer: encode into the producer-owned mailbox buffer (pure
        // memcpy into caller's buffer — no heap allocation) and publish ownership to
        // the UI consumer. A second publish before the consumer drains simply
        // overwrites this producer's own buffer ("latest wins"); it can never race
        // the buffer the consumer owns. Never lock or copy SidStateRootV1 here — this
        // helper can be reached from render-owned projection/restore paths.
        auto& slot = templateBlobMailbox_.producerSlot();
        slot.len = ArpSID::encodeSidStateRootBinary(root, slot.bytes.get(), kSerialBlobSlotSize_);
        templateBlobMailbox_.publish();
    }

    void buildSerializableStateRootFromShadow(SidStateRootV1& out) const noexcept {
        SidStateRootV1 templateRoot{};
        {
            ArpSID::sidRealtimeGuardForbidLock("serializableStateTemplateMutex_");
            std::lock_guard<ArpSID::AuditedSerializationMutex> lock(serializableStateTemplateMutex_);
            // Single-consumer drain (serialized by the mutex above): take ownership of
            // the latest published blob, if any, and decode it from the consumer-owned
            // buffer. The render producer never touches this buffer, so the decode can
            // neither race nor read torn bytes. If nothing new was published the
            // cached serializableStateTemplate_ is reused.
            if (auto* slot = templateBlobMailbox_.tryConsume()) {
                if (slot->len > 0 && slot->len <= kSerialBlobSlotSize_) {
                    SidStateRootV1 decoded{};
                    if (ArpSID::decodeSidStateRootBinary(slot->bytes.get(), slot->len, decoded)) {
                        ArpSID::sanitizePersistentStateRootForSerialization(decoded);
                        serializableStateTemplate_ = std::move(decoded);
                    }
                }
            }
            templateRoot = serializableStateTemplate_;
        }
        ArpSID::buildStateRootFromPresentationTemplate(templateRoot, out,
            [this](int i) noexcept -> float {
                const float v = params_[(size_t)i].load(std::memory_order_relaxed);
                return std::isfinite(v) ? v : kParamInfos[(size_t)i].defaultNorm;
            });
        const bool drSidStateActive = params_[(size_t)kParamDrSidEnable].load(std::memory_order_relaxed) > 0.5f;
        if (drSidStateActive && drs_()) {
            out.patch.sid_runtime = drs_()->serializePrimarySidEnvelopeRuntimeState();
        } else if (bpe_()) {
            out.patch.sid_runtime = bpe_()->serializePrimarySidEnvelopeRuntimeState();
        }
    }

    // Schedule a state restore to be applied at the top of the next
    // processBlock() call (render thread owned). Safe to call from any thread.
    // Used by the ObjC AU layer for setFullState: / setCurrentPreset: so that
    // engine reset (voice tokens, SID write queue, voice policy) never races with
    // an in-progress render.
    void schedulePendingStateRestore(const SidStateRootV1& root) {
        // Non-RT writer — serialized version-counter double-buffer protocol.
        // The version counter protects the RT reader from torn reads; this mutex
        // closes the missing writer/writer race when UI, preset load and host
        // project restore arrive concurrently. Render never takes this mutex.
        ArpSID::sidRealtimeGuardForbidLock("schedulePendingStateRestore pendingStateRestoreWriterMutex_");
        std::lock_guard<ArpSID::AuditedStateRestoreWriterMutex> writerLock(pendingStateRestoreWriterMutex_);
        // Single-producer publish into the ownership mailbox. The writer mutex
        // guarantees there is exactly one producer at a time (UI, preset load and
        // host project restore can arrive concurrently); the render thread never
        // takes this mutex and never touches the producer-owned buffer.
        SidStateRootV1& dst = stateRootMailbox_.producerSlot();
        dst = root;  // copy on non-RT thread — heap alloc OK here
        prepareStateRootForApplyNonRealtime_(dst);
        // Publish: atomically hands this buffer to the render thread and reclaims
        // the previously parked buffer for the next fill. "Latest value wins" — a
        // second publish before the render thread consumes simply overwrites the
        // producer's own buffer; it never races the consumer's buffer.
        stateRootMailbox_.publish();
    }

    void drainPendingStateRestore_() noexcept {
        // RT consumer — wait-free, no mutex, no spin, no heap, no shared-buffer
        // writes. tryConsume() transfers ownership of the freshly published buffer
        // to this thread; the returned object is EXCLUSIVELY render-owned for the
        // remainder of applyStateRootCanonical(). A producer that laps this reader
        // can never touch this buffer (see sid_ownership_mailbox.h).
        if (SidStateRootV1* root = stateRootMailbox_.tryConsume()) {
            applyPreparedStateRootRT_(*root);
        }
    }

    void prepareStateRootForApplyNonRealtime_(SidStateRootV1& dst) {
        // P1-11/P1-12: one producer-side authority for all may-allocate state-root
        // preparation. Render consumes only roots that passed through this helper.
        // The complete transform SidRuntimeModel used to run inside applyStateRootBySwap
        // is intentionally finished here on a non-RT thread.
        ArpSID::sidCanonicalizeStateRootForApply(dst);
        // v803/P0: If this restore targets a SID-808 factory slot, record only
        // the intended bridge plan on the producer side. The bridge itself must
        // not mutate until the matching root wins mailbox ownership.
        preloadSid808FactorySlotForScheduledRestoreNonRealtime_(dst);
    }

    void applyPreparedStateRootRT_(SidStateRootV1& preparedRoot) noexcept {
        // P1-11/P1-12: render-side apply authority. Caller must pass a root prepared
        // by prepareStateRootForApplyNonRealtime_(). This wrapper keeps the mailbox
        // drain path honest and gives source guards a single render-entry marker.
        applyStateRootCanonical(preparedRoot, /*onRenderThread=*/true);
    }

    // onRenderThread: true when invoked from the audio thread via
    // drainPendingStateRestore_. In that case the render-forbidden drum-bridge
    // factory-slot load must be QUEUED (RT-safe) rather than performed inline; a
    // non-realtime drain (teardownReset → applyQueuedSlotNonRealtime) applies it
    // off the audio thread. Non-RT callers (reset/setup/tests) pass false and load
    // directly. (audit P0-4)
    void applyStateRootCanonical(SidStateRootV1& root, bool onRenderThread = false) noexcept {
        // Pass O: Held replay delegated to canonical policy (sid_runtime_held_replay.h).
        // Each overlapping same-note voice gets a distinct token via sidReplayHeldNotes.
        // Wrappers must not own replay ordering or token assignment.
        //
        // Use pre-allocated replayBuf_ member instead of 256 KB stack VLA.
        const int kMaxReplay = kMaxReplayEntries_;
        ArpSID::SidHeldReplayEntry* replayEntries = replayBuf_.get();
        int replayCount = 0;
        for (int ch = 0; ch < 16 && replayCount < kMaxReplay; ++ch) {
            for (int note = 0; note < 128 && replayCount < kMaxReplay; ++note) {
                const uint32_t channelGen = runtimeHostSurface_().heldIngressChannelGeneration[(size_t)ch].load(std::memory_order_acquire);
                const uint32_t noteGen = runtimeHostSurface_().heldIngressNoteGeneration[(size_t)ch][(size_t)note].load(std::memory_order_acquire);
                if (noteGen != channelGen) continue;
                const uint8_t depth = runtimeHostSurface_().heldIngressDepth[(size_t)ch][(size_t)note].load(std::memory_order_acquire);
                const uint8_t vel = runtimeHostSurface_().heldIngressVelocity[(size_t)ch][(size_t)note].load(std::memory_order_relaxed);
                if (vel == 0 || depth == 0) continue;
                const int d = std::clamp<int>(depth, 1, static_cast<int>(ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes));
                for (int lane = 0; lane < d && replayCount < kMaxReplay; ++lane) {
                    auto& e = replayEntries[replayCount++];
                    e.channel = static_cast<int16_t>(ch);
                    e.note    = static_cast<int16_t>(note);
                    const uint8_t lv = runtimeHostSurface_().heldIngressLaneVelocity[(size_t)ch][(size_t)note][(size_t)lane].load(std::memory_order_relaxed);
                    e.velocity7 = lv > 0u ? lv : (vel > 0u ? vel : 100u);
                    e.noteId = runtimeHostSurface_().heldIngressLaneNoteId[(size_t)ch][(size_t)note][(size_t)lane].load(std::memory_order_relaxed);
                    e.arrivalOrder = runtimeHostSurface_().heldIngressLaneOrder[(size_t)ch][(size_t)note][(size_t)lane].load(std::memory_order_relaxed);
                    if (e.arrivalOrder == 0u) e.arrivalOrder = runtimeHostSurface_().heldIngressSerial.fetch_add(1u, std::memory_order_relaxed) + 1u;
                    e.active = true;
                }
            }
        }
        // Held notes captured above into replayEntries are re-armed after the swap
        // (sidReplayHeldNotes below); this hard reset clears engines + in-flight
        // ingress so the incoming patch starts clean. (audit P0-5)
        realtimeEngineHardResetForStateApply_();
        // RT-safe apply: root was pre-canonicalized by schedulePendingStateRestore on
        // the non-RT side. Never call sanitizePersistentStateRootForSerialization here        // it allocates and would terminate via std::terminate if allocation fails.
        runtimeModel_.applyStateRootBySwap(root);
        const SidStateRootV1& appliedRoot = runtimeModel_.stateRoot();
        if (bpe_()) bpe_()->restorePrimarySidEnvelopeRuntimeState(appliedRoot.patch.sid_runtime);
        if (drs_()) drs_()->restorePrimarySidEnvelopeRuntimeState(appliedRoot.patch.sid_runtime);
        const bool applyingFactoryRoot = ArpSID::isFactoryStateRootIdentity(appliedRoot);
        clearQueuedFactoryPatchIngress_();
        // Update serializable template non-blockingly (try-lock; P1-1 fix applied there).
        updateSerializableStateTemplate_(runtimeModel_.stateRoot());
        for (int i = 0; i < kNumParams; ++i) {
            float v = kParamInfos[(size_t)i].defaultNorm;
            if (!ArpSID::isRuntimeOnlyOrTransientParam(i))
                v = ArpSID::sidStateRootParamValueFromHydratedValuesRT(appliedRoot, i);
            // v951: state-root apply must use the same param-specific sanitize law
            // as AU3/Phase2 live staging. Generic clamp can preserve a
            // semantically-invalid root value for enum/program/structural params and
            // then dirty-flush it as if it were authoritative.
            const float clean = ArpSID::sanitizeNormalizedParamValue(
                i,
                v,
                ArpSID::defaultNormalizedParamValue(i));
            params_[(size_t)i].store(clean, std::memory_order_release);
            dirty_[(size_t)i].store(true, std::memory_order_release);
        }

        // AUv2/auval retention overlay: after the serialized patch/root has been
        // applied, restore the host's writable parameter snapshot for every real
        // host-writable parameter. This intentionally includes runtime-only and
        // transient controls that are not serialized in SidStateRootV1. It
        // intentionally excludes Program/BankSlot because preset identity is owned
        // by sticky readback metadata, not by stale host writes.
        if (hostParamRetentionOverlayPending_.exchange(false, std::memory_order_acq_rel)) {
            // factory roots are complete audible/runtime authorities. v161 only
            // blocked a hand-picked oscillator/register subset; that still allowed
            // stale host snapshots to reintroduce old LFO, arp, detune, portamento,
            // forensic, limiter/reverb, voice-mode, and other sound-shaping values
            // after the new factory SidStateRootV1 had been applied. That is still
            // patch-switch split-brain. For factory preset/project roots, consume the
            // pending overlay but do not apply any host-retained audio state.
            if (!applyingFactoryRoot) {
                for (int i = 0; i < kNumParams; ++i) {
                    if (i == kParamProgram || i == kParamBankSlot) continue;
                    const float fallback = kParamInfos[(size_t)i].defaultNorm;
                    const float overlay = hostParamRetentionOverlay_[(size_t)i].load(std::memory_order_relaxed);
                    const float clean = ArpSID::sanitizeNormalizedParamValue(i, overlay, fallback);
                    params_[(size_t)i].store(clean, std::memory_order_release);
                    dirty_[(size_t)i].store(true, std::memory_order_release);
                }
            }
        }

        // Prevent duplicate factory-patch handling on the next render block.
        // Factory slot authority is kParamBankSlot; kParamProgram is only a metadata mirror.
        const float appliedBankSlot = ArpSID::sidStateRootParamValueFromHydratedValuesRT(appliedRoot, kParamBankSlot);
        prevProgram_ = appliedBankSlot;
        // A3 fixed: apply factory slot on this non-render state/preset path.
        // renderDrumBridgeIfActive_() must never call applyQueuedSlotNonRealtime()
        // or mutate kit/engine configuration on the audio thread.
        {
            const int decodedSlotInt = ArpSID::canonicalFactorySlotFromNormalizedBankSlot(appliedBankSlot);
            const int slotInt = (componentFlavor_ == ArpSID::ComponentFlavor::Sid808 &&
                                 !ArpSID::isSid808FactorySlot(decodedSlotInt))
                ? 120
                : decodedSlotInt;
            if (componentFlavor_ == ArpSID::ComponentFlavor::Sid808 &&
                ArpSID::isSid808FactorySlot(slotInt)) {
                if (onRenderThread) {
                    // audit P0-4/v803: NEVER call the non-RT bridge loader on the
                    // audio thread. Normal scheduled restores resolve the SID-808
                    // kit in schedulePendingStateRestore() on the non-RT producer
                    // side, then publish the fixed table through an ownership
                    // mailbox so render can activate it at the block boundary.
                    const int preloadedSlot = sid808PreloadedFactorySlotForRestore_v803_.load(std::memory_order_acquire);
                    if (preloadedSlot == slotInt &&
                        applyPreparedSid808BridgeSlotRT_(slotInt)) {
                        sid808PreloadedFactorySlotForRestore_v803_.store(-1, std::memory_order_release);
                    } else {
                        drumEngineBridge_.queueSlotLoadNonRealtime(slotInt);
                        if (preloadedSlot == slotInt) {
                            sid808PreloadedFactorySlotForRestore_v803_.store(-1, std::memory_order_release);
                        }
                    }
                } else {
                    (void)drumEngineBridge_.loadFactorySlot(slotInt);
                    sid808PreloadedFactorySlotForRestore_v803_.store(-1, std::memory_order_release);
                }
            }
            // Deliberately do not load DrSID factory slots into the bridge:
            // DrSID/DrumMachine production audio has one authority only,
            // canonical engineBank_.drSid.
        }
        // Flush atomic shadow into renderParams_ before projectStateToBackends().
        for (int i = 0; i < kNumParams; ++i) {
            renderParams_[(size_t)i] = params_[(size_t)i].load(std::memory_order_relaxed);
            dirty_[(size_t)i].store(false, std::memory_order_relaxed);
        }
        (void)enforceComponentFlavorPolicy_();
        // State roots are sanitized completely on the non-RT producer before
        // applyStateRootBySwap(). Do not mutate/canonicalize the root again on
        // the audio thread; only reconcile persistent adapter-local policy from
        // the already-staged render snapshot.
        ArpSID::runtimeReconcileStagedPersistentParameterPolicies(*this);
        runtimeProjectionState_.firstApply = true;
        runtimeProjectionState_.lastFamily = runtimeModel_.variantProfile().family;
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(true);
        syncSidSystemModelFromParams_(true);
        // [canonical held replay] token assignment + ordering owned by policy
        ArpSID::sidReplayHeldNotes(runtimeModel_, replayEntries, replayCount, 1);
    }

    //──────────────────────────────────────────────────────
    // MIDI — render thread only
    //──────────────────────────────────────────────────────

    // RT-safe: reads pre-warmed hostTimebase_ (initialized in constructor, never on RT).
    double hostTicksToSeconds_(uint64_t ticks) const noexcept {
#if defined(__APPLE__)
        return (double)ticks * (double)hostTimebase_.numer / (double)hostTimebase_.denom * 1.0e-9;
#else
        // v855 P2.4 portability fix: no platform timebase — returning 0.0 here
        // silently collapsed every host-time-stamped event to sample offset 0.
        // Return a negative sentinel so callers treat the offset as UNRESOLVED
        // (sampleOffset = -1, block-start dispatch) instead of wrong-but-precise.
        (void)ticks;
        return -1.0;
#endif
    }

    void mirrorMidiHeldIngress_(const uint8_t* data, uint8_t len) noexcept {
        if (data && len >= 1) {
            const uint8_t status = data[0] & 0xF0u;
            const uint8_t ch = data[0] & 0x0Fu;
            if ((status == 0x90u || status == 0x80u) && len >= 2) {
                const uint8_t note = static_cast<uint8_t>(data[1] & 0x7Fu);
                const uint8_t vel = (len >= 3) ? static_cast<uint8_t>(data[2] & 0x7Fu) : 0u;
                const bool on = (status == 0x90u && vel > 0u);
                const uint32_t heldGen = runtimeHostSurface_().heldIngressChannelGeneration[ch].load(std::memory_order_acquire);
                const bool heldCurrent = (runtimeHostSurface_().heldIngressNoteGeneration[ch][note].load(std::memory_order_acquire) == heldGen);
                if (on) {
                    const uint8_t prevDepth = heldCurrent ? runtimeHostSurface_().heldIngressDepth[ch][note].load(std::memory_order_acquire) : 0u;
                    const uint8_t nextDepth = static_cast<uint8_t>(std::min<int>(static_cast<int>(ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes), static_cast<int>(prevDepth) + 1));
                    runtimeHostSurface_().heldIngressVelocity[ch][note].store(vel, std::memory_order_relaxed);
                    uint32_t serial = runtimeHostSurface_().heldIngressSerial.fetch_add(1u, std::memory_order_relaxed) + 1u;
                    if (serial == 0u) serial = runtimeHostSurface_().heldIngressSerial.fetch_add(1u, std::memory_order_relaxed) + 1u;
                    const int32_t syntheticAnonymousId =
                        ArpSID::SidRuntimeHostSurface::makeSyntheticAnonymousNoteId(serial);
                    const size_t lane = std::min<size_t>(static_cast<size_t>(prevDepth), ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes - 1u);
                    runtimeHostSurface_().heldIngressLaneVelocity[ch][note][lane].store(vel, std::memory_order_relaxed);
                    runtimeHostSurface_().heldIngressLaneNoteId[ch][note][lane].store(syntheticAnonymousId, std::memory_order_relaxed);
                    runtimeHostSurface_().heldIngressLaneOrder[ch][note][lane].store(serial, std::memory_order_relaxed);
                    runtimeHostSurface_().heldIngressNoteId[ch][note].store(syntheticAnonymousId, std::memory_order_relaxed);
                    runtimeHostSurface_().heldIngressOrder[ch][note].store(serial, std::memory_order_relaxed);
                    runtimeHostSurface_().heldIngressNoteGeneration[ch][note].store(heldGen, std::memory_order_release);
                    runtimeHostSurface_().heldIngressDepth[ch][note].store(nextDepth, std::memory_order_release);
                } else {
                    const uint8_t prevDepth = heldCurrent ? runtimeHostSurface_().heldIngressDepth[ch][note].load(std::memory_order_acquire) : 0u;
                    const uint8_t nextDepth = (prevDepth > 0u) ? static_cast<uint8_t>(prevDepth - 1u) : 0u;
                    if (prevDepth > 0u) {
                        const size_t clearLane = std::min<size_t>(static_cast<size_t>(prevDepth - 1u), ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes - 1u);
                        runtimeHostSurface_().heldIngressLaneVelocity[ch][note][clearLane].store(0u, std::memory_order_relaxed);
                        runtimeHostSurface_().heldIngressLaneNoteId[ch][note][clearLane].store(-1, std::memory_order_relaxed);
                        runtimeHostSurface_().heldIngressLaneOrder[ch][note][clearLane].store(0u, std::memory_order_relaxed);
                    }
                    if (nextDepth == 0u) {
                        runtimeHostSurface_().heldIngressVelocity[ch][note].store(0u, std::memory_order_relaxed);
                        runtimeHostSurface_().heldIngressNoteId[ch][note].store(-1, std::memory_order_relaxed);
                        runtimeHostSurface_().heldIngressOrder[ch][note].store(0u, std::memory_order_relaxed);
                    } else {
                        const size_t topLane = std::min<size_t>(static_cast<size_t>(nextDepth - 1u), ArpSID::SidRuntimeHostSurface::kHeldIngressIdentityLanes - 1u);
                        runtimeHostSurface_().heldIngressVelocity[ch][note].store(runtimeHostSurface_().heldIngressLaneVelocity[ch][note][topLane].load(std::memory_order_relaxed), std::memory_order_relaxed);
                        runtimeHostSurface_().heldIngressNoteId[ch][note].store(runtimeHostSurface_().heldIngressLaneNoteId[ch][note][topLane].load(std::memory_order_relaxed), std::memory_order_relaxed);
                        runtimeHostSurface_().heldIngressOrder[ch][note].store(runtimeHostSurface_().heldIngressLaneOrder[ch][note][topLane].load(std::memory_order_relaxed), std::memory_order_relaxed);
                    }
                    runtimeHostSurface_().heldIngressNoteGeneration[ch][note].store(heldGen, std::memory_order_release);
                    runtimeHostSurface_().heldIngressDepth[ch][note].store(nextDepth, std::memory_order_release);
                }
            } else if (status == 0xB0u && len >= 3) {
                const uint8_t cc = static_cast<uint8_t>(data[1] & 0x7Fu);
                const uint8_t v = static_cast<uint8_t>(data[2] & 0x7Fu);
                if (cc == 64u) {
                    liveSustainPedalDown_[(size_t)ch].store(v >= 64u ? 1u : 0u, std::memory_order_release);
                } else if (cc == 66u) {
                    liveSostenutoPedalDown_[(size_t)ch].store(v >= 64u ? 1u : 0u, std::memory_order_release);
                } else if (cc == 121u) {
                    clearLivePedalMirrorsChannel_(ch);
                } else if (cc == 120u || cc == 123u) {
                    // CC120/123 kill notes/sound but do not synthesize a physical
                    // pedal-up. Pedal mirrors remain unchanged; held-note
                    // identity is invalidated so factory scrub cannot replay
                    // killed gates.
                    clearHeldIngressChannel_(ch);
                }
            }
        }
    }

    // Push a raw MIDI message from ANY thread. Drained at block start into the
    // canonical timed-event queue; actual mutation stays render-thread owned.
    bool enqueueMidiIntent(const uint8_t* data, uint8_t len, int32_t sampleOffset = -1, uint64_t hostTime = 0) noexcept {
        // AUv2/sample-offset paths call enqueueMidiIntent() directly, not pushMidi().
        // v911 accepted-only held mirror: a NoteOn must not enter the held-note
        // factory-root/reconcile ledger until the audio event is actually
        // accepted into the primary queue. Queue-full NoteOn drops therefore do
        // not create a phantom held note. Accepted NoteOffs and accepted/latching
        // safety fallbacks still clear held state.
        if (!data || len == 0) return false;
        if (!rawMidiChannelVoiceLengthOk_(data, len)) {
            // v912: reject malformed/unsupported raw MIDI at ingress so neither
            // the queue nor held-note/pedal mirrors can observe fabricated data.
            ingressDropTelemetry_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const uint8_t status = data[0] & 0xF0u;
        const uint8_t ch = data[0] & 0x0Fu;

        if (midiQueue_.push(data, len, hostTime, sampleOffset)) {
            // v925: accepted NoteOn/NoteOff mirror at canonical dispatch, not
            // enqueue. CC pedal/kill mirrors are not canonical note gates and
            // remain safe to mirror at ingress for immediate physical state.
            if (status == 0xB0u) mirrorMidiHeldIngress_(data, len);
            return true;
        }

        ingressDropTelemetry_.fetch_add(1, std::memory_order_relaxed);
        if ((status == 0x80u && len >= 2u) ||
            (status == 0x90u && len >= 3u && (data[2] & 0x7Fu) == 0u)) {
            latchDroppedNoteOff_(ch, static_cast<uint8_t>(data[1] & 0x7Fu), hostTime);
            mirrorMidiHeldIngress_(data, len);
        } else if (status == 0xB0u) {
            if (len < 3) return false;
            const uint8_t cc = static_cast<uint8_t>(data[1] & 0x7Fu);
            const uint8_t v = static_cast<uint8_t>(data[2] & 0x7Fu);
            // Controller safety fallbacks are accepted into pending render-thread
            // ledgers, so mirror the corresponding physical pedal/kill state too.
            mirrorMidiHeldIngress_(data, len);
            if (cc == 120u) { pendingAllSoundOff_[(size_t)ch].store(1u, std::memory_order_release); clearHeldIngressChannel_(ch); }
            else if (cc == 123u) { pendingAllNotesOff_[(size_t)ch].store(1u, std::memory_order_release); clearHeldIngressChannel_(ch); }
            else if (cc == 64u) { pendingSustain_[(size_t)ch].store(v, std::memory_order_release); pendingSustainDirty_[(size_t)ch].store(1u, std::memory_order_release); if (v < 64u) pendingSustainOff_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 66u) { pendingSostenuto_[(size_t)ch].store(v, std::memory_order_release); pendingSostenutoDirty_[(size_t)ch].store(1u, std::memory_order_release); if (v < 64u) pendingSostenutoOff_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 1u) { pendingModWheel_[(size_t)ch].store(v, std::memory_order_release); pendingModWheelDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 11u) { pendingExpression_[(size_t)ch].store(v, std::memory_order_release); pendingExpressionDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 7u) { pendingChannelVolume_[(size_t)ch].store(v, std::memory_order_release); pendingChannelVolumeDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 0u) { pendingBankMsb_[(size_t)ch].store(v, std::memory_order_release); pendingBankMsbDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 32u) { pendingBankLsb_[(size_t)ch].store(v, std::memory_order_release); pendingBankLsbDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 101u) { pendingRpnMsb_[(size_t)ch].store(v, std::memory_order_release); pendingRpnMsbDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 100u) { pendingRpnLsb_[(size_t)ch].store(v, std::memory_order_release); pendingRpnLsbDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 99u) { pendingNrpnMsb_[(size_t)ch].store(v, std::memory_order_release); pendingNrpnMsbDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 98u) { pendingNrpnLsb_[(size_t)ch].store(v, std::memory_order_release); pendingNrpnLsbDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 6u) { pendingDataEntryMsb_[(size_t)ch].store(v, std::memory_order_release); pendingDataEntryMsbDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 38u) { pendingDataEntryLsb_[(size_t)ch].store(v, std::memory_order_release); pendingDataEntryLsbDirty_[(size_t)ch].store(1u, std::memory_order_release); }
            else if (cc == 121u) pendingResetControllers_[(size_t)ch].store(1u, std::memory_order_release);
        } else if (status == 0xD0u) {
            if (len < 2) return false;
            pendingChannelPressure_[(size_t)ch].store(static_cast<uint8_t>(data[1] & 0x7Fu), std::memory_order_release);
            pendingChannelPressureDirty_[(size_t)ch].store(1u, std::memory_order_release);
        } else if (status == 0xE0u) {
            if (len < 3) return false;
            pendingPitchBend14_[(size_t)ch].store(static_cast<uint16_t>(((uint16_t)(data[2] & 0x7Fu) << 7) | (uint16_t)(data[1] & 0x7Fu)), std::memory_order_release);
            pendingPitchBendDirty_[(size_t)ch].store(1u, std::memory_order_release);
        } else if (status == 0xA0u) {
            if (len < 3) return false;
            const uint8_t note = static_cast<uint8_t>(data[1] & 0x7Fu);
            const uint8_t v = static_cast<uint8_t>(data[2] & 0x7Fu);
            pendingPolyPressure_[(size_t)ch][(size_t)note].store(v, std::memory_order_release);
            pendingPolyPressureDirty_[(size_t)ch][(size_t)note].store(1u, std::memory_order_release);
        }
        return false;
    }

    void pushMidiWithSampleOffset(const uint8_t* data,
                                uint8_t len,
                                int32_t sampleOffset,
                                uint64_t hostTime = 0) noexcept {
        enqueueMidiIntent(data, len, sampleOffset, hostTime);
    }

    void pushMidi(const uint8_t* data, uint8_t len, uint64_t hostTime = 0) noexcept {
        // enqueueMidiIntent() is the single held-ingress mirror authority for
        // both normal and sample-offset MIDI paths; do not double-increment held depth.
        enqueueMidiIntent(data, len, -1, hostTime);
    }

    void setRenderHostTime(uint64_t hostTime) noexcept {
        renderHostTime_.store(hostTime, std::memory_order_relaxed);
    }

    inline uint8_t sanitizeMidiChannel_(int ch) noexcept {
        return static_cast<uint8_t>(std::clamp(ch, 0, 15));
    }

    inline bool midiChannelKnown_(int ch) const noexcept {
        return ch >= 0 && ch < 16;
    }

    inline void observeDefaultEventChannel_(int ch) noexcept {
        if (midiChannelKnown_(ch))
            defaultEventChannel_ = sanitizeMidiChannel_(ch);
    }

    void handleNoteOn(uint8_t ch, uint8_t note, uint8_t vel, int noteId = -1) noexcept {
        ArpSID::runtimeHandleRenderedNoteOn(*this, ch, note, vel, noteId);
    }

    void handleNoteOff(uint8_t ch, uint8_t note, int noteId = -1) noexcept {
        ArpSID::runtimeHandleRenderedNoteOff(*this, ch, note, noteId);
    }

    void runtimeObserveDefaultEventChannel(int ch) noexcept { observeDefaultEventChannel_(ch); }
    bool runtimeHasBitPerfectEngine() const noexcept { return bpe_() != nullptr; }
    bool runtimeHasDrSidEngine() const noexcept { return drs_() != nullptr; }
    bool runtimeHasArpEngine() const noexcept { return arp_() != nullptr; }
    bool runtimeIsSynthModeEnabled() const noexcept { return ArpSID::sidResolveRenderModeFromLiveParams(renderParams_) == ArpSID::SidRuntimeRenderMode::SidRegister; }
    bool runtimeIsDrSidEnabled() const noexcept { return ArpSID::sidResolveRenderModeFromLiveParams(renderParams_) == ArpSID::SidRuntimeRenderMode::DrSid; }
    bool runtimeIsArpEnabled() const noexcept {
        // v932: ARP is only an effective note authority in BitPerfect/classic mode.
        // SynthMode/SID-register and DrSID structural modes must not expose stale
        // ArpEnable as active authority through internal AU3 note/telemetry paths.
        return ArpSID::sidEffectiveArpAuthorityFromLiveParams(renderParams_);
    }
    void runtimeSetLastNoteVelocity(float v) noexcept { lastNoteVelocity_ = v; }
    void runtimeStoreTelemetryLastNote(int note) noexcept { telemetryLastNote_.store(note, std::memory_order_relaxed); }
    // v859: RT-safe identity repair slot. When the bridge needs an SID-808
    // identity on the render thread, use the CURRENTLY LOADED bank slot (decoded
    // from renderParams_) instead of hard-coding kit 120 — direct MIDI after
    // loading kit 137 must play kit 137's identity, not snap back to Classic.
    int sid808IdentityRepairSlot_() const noexcept {
        const int slot = ArpSID::canonicalFactorySlotFromNormalizedBankSlot(
            renderParams_[(size_t)kParamBankSlot]);
        return (slot >= 120 && slot <= 149) ? slot : 120;
    }

    void runtimeTriggerDrSidNote(int note, float velocity) noexcept {
        // v859 double-render fix. In Sid808 flavor the bridge REPLACES the whole
        // output bus (renderDrumBridgeIfActive_), so also triggering the
        // canonical DrSID engine rendered a complete DrSID kit that was thrown
        // away every block — literal double render work, plus a wrong-sounding
        // DrSID ghost on any path where the replacement did not run. Sid808
        // flavor now dispatches ONLY to the bridge once its SID-808 identity is
        // active (slot-aware repair below); the canonical engine remains the
        // fail-open authority if the bridge identity cannot be established, so
        // notes can never fall silent. All other flavors stay canonical-only.
        if (componentFlavor_ == ArpSID::ComponentFlavor::Sid808) {
            const auto spec = ArpSID::sidGMDrumSpecForNote(
                static_cast<std::uint8_t>(std::clamp(note, 0, 127)));
            if (spec.drumClass != ArpSID::SidGMDrumClass::Unsupported) {
                const int selectedSlot = sid808IdentityRepairSlot_();
                if (drumEngineBridge_.activeIdentity().context != ArpSID::DrumContext::SID808_AnalogProjection) {
                    drumEngineBridge_.router().setActiveIdentityFromFactorySlot(selectedSlot);
                }
                if (drumEngineBridge_.activeIdentity().context == ArpSID::DrumContext::SID808_AnalogProjection) {
                    // v872: the router applies the GM note projection for SID808
                    // (velocityScale plus tuneOffsetNorm/decayScale) so direct MIDI
                    // and KIT scheduled hits share one merge path.
                    const float scaledVel = velocity * 127.0f;
                    const std::uint8_t vel = static_cast<std::uint8_t>(
                        std::clamp(static_cast<int>(scaledVel), 1, 127));
                    // v910 selected-kit authority: identity repair alone set only
                    // the router context — the hit could still resolve against
                    // the engine's previous/default voice config. Pass the same
                    // selected-factory-slot override the KIT sequencer path uses
                    // so every ingress path (direct MIDI, events[], midiQueue_,
                    // KIT scheduler) resolves identical factory kit data.
                    ArpSID::Sid808HitOverride slotOverride{};
                    slotOverride.selectedFactorySlot = static_cast<std::uint16_t>(selectedSlot);
                    slotOverride.hasSelectedFactorySlot = true;
                    drumEngineBridge_.noteOnWithOverride(spec.drumClass, vel,
                                                         static_cast<std::uint8_t>(note),
                                                         slotOverride);
                    return; // bridge owns the audio — no canonical double render
                }
            }
        }
        // Canonical DrSID is the audio authority for DrSID/DrumMachine flavors
        // and the fail-open path for Sid808 when the bridge is unavailable.
        if (drs_()) drs_()->triggerMidiNote(note, velocity);
    }
    void runtimeReleaseDrSidNote(int note) noexcept {
        if (componentFlavor_ == ArpSID::ComponentFlavor::Sid808) {
            const auto spec = ArpSID::sidGMDrumSpecForNote(
                static_cast<std::uint8_t>(std::clamp(note, 0, 127)));
            if (spec.drumClass != ArpSID::SidGMDrumClass::Unsupported &&
                drumEngineBridge_.activeIdentity().context == ArpSID::DrumContext::SID808_AnalogProjection) {
                drumEngineBridge_.router().noteOff(spec.drumClass);
                return;
            }
        }
        if (drs_()) drs_()->noteOffMidi(note);
    }
    void runtimeArpNoteOn(int note, float velocity) noexcept { arp_()->noteOn(note, velocity); }
    void runtimeArpNoteOff(int note) noexcept { arp_()->noteOff(note); }
    void runtimeBitPerfectNoteOn(int note, float velocity, int channel, int noteId, uint64_t voiceToken = 0) noexcept { bpe_()->noteOn(note, velocity, channel, noteId, voiceToken); }
    void runtimeBitPerfectNoteOff(int note, int channel, int noteId) noexcept { bpe_()->noteOff(note, channel, noteId); }

    ParamID runtimeMappedRealtimeCcParam_(uint8_t cc) const noexcept {
        return ArpSID::sidMappedRealtimeCcParam(cc);
    }

    bool runtimeCcRequiresImmediateAudioProjection_(uint8_t cc) const noexcept {
        return ArpSID::sidCcRequiresImmediateAudioProjection(cc);
    }

    void runtimeApplyImmediateCcAudioProjection_(uint8_t cc) noexcept {
        if (!runtimeCcRequiresImmediateAudioProjection_(cc)) return;
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(false);
        processModMatrix_(0);
    }


    static constexpr std::uint8_t kDigiD418MidiCcMode = 110u;   // 0..63 AUTH C64-bus D418, 64..127 FAST private SID D418; legacy float is not MIDI-selectable
    static constexpr std::uint8_t kDigiD418MidiCcRate = 111u;   // 0..127 -> 1000..32000 Hz
    static constexpr std::uint8_t kDigiD418MidiCcPanic = 112u;  // >=64 -> DIGI panic/all-notes-off
    static constexpr std::uint8_t kDigiD418MidiCcRoot  = 113u;  // 0..127 -> root note, clamped so 8 pads fit
    static constexpr std::uint8_t kDigiD418MidiCcChan  = 114u;  // 0..15 -> channel, >=16 -> OMNI

    static std::uint32_t digiD418RateFromMidiCc_(std::uint8_t val) noexcept {
        const std::uint32_t v = static_cast<std::uint32_t>(val & 0x7Fu);
        // Linear, deterministic mapping. CC 0 is 1 kHz, CC 127 is 32 kHz.
        return 1000u + static_cast<std::uint32_t>((v * 31000u + 63u) / 127u);
    }

    static std::uint8_t digiD418ModeFromMidiCc_(std::uint8_t val) noexcept {
        const std::uint8_t v = static_cast<std::uint8_t>(val & 0x7Fu);
        return (v < 64u) ? 0u : 1u;  // release-safe: never select LegacyFloatLayer from MIDI CC
    }

    static std::uint8_t digiD418RootFromMidiCc_(std::uint8_t val) noexcept {
        // Keep all eight chromatic pads inside MIDI range: root 0..120 maps
        // root..root+7. CC value is otherwise direct for predictable hardware.
        return static_cast<std::uint8_t>(std::min<std::uint8_t>(val & 0x7Fu, 120u));
    }

    static std::uint8_t digiD418ChannelFromMidiCc_(std::uint8_t val) noexcept {
        const std::uint8_t v = static_cast<std::uint8_t>(val & 0x7Fu);
        return (v <= 15u) ? v : 16u; // 16 = OMNI
    }

    bool applyDigiD418MidiControlCc_(std::uint8_t cc, std::uint8_t val) noexcept {
        switch (cc) {
            case kDigiD418MidiCcMode: {
                setDigiD418RuntimePolicy(digiD418ModeFromMidiCc_(val), digiD418RuntimeRateHz());
                return true;
            }
            case kDigiD418MidiCcRate: {
                setDigiD418RuntimePolicy(digiD418RuntimeMode(), digiD418RateFromMidiCc_(val));
                return true;
            }
            case kDigiD418MidiCcPanic: {
                if ((val & 0x7Fu) >= 64u) {
                    digiD418_.allNotesOff();
                    digiSampler_.allNotesOff();
                    digiD418Sid_.resetD418VolumeDacEmulationState();
                    digiOpenBus_.powerOn();
                    clearDigiD418TelemetryAtomics_(digiD418RuntimeMode());
                    digiD418RuntimePolicyGenerationRender_ = digiD418RuntimePolicyGeneration_.load(std::memory_order_acquire);
                }
                return true;
            }
            case kDigiD418MidiCcRoot: {
                setDigiMidiPadMapping(digiD418RootFromMidiCc_(val), digiMidiChannelFilter());
                return true;
            }
            case kDigiD418MidiCcChan: {
                setDigiMidiPadMapping(digiMidiRootNote(), digiD418ChannelFromMidiCc_(val));
                return true;
            }
            default:
                return false;
        }
    }

    void handleCC(uint8_t ch, uint8_t cc, uint8_t val) noexcept {
        // Reserved DIGI $D418 CCs must be consumed before any generic
        // runtime/parameter CC mapping. Otherwise an external controller
        // changing DIGI auth mode/rate could also mutate an unrelated synth
        // parameter in the same render callback.
        if (applyDigiD418MidiControlCc_(cc, val)) {
            telemetryLastMappedCC_.store((int)cc, std::memory_order_relaxed);
            telemetryLastMappedCCValue_.store(std::clamp(static_cast<float>(val) / 127.0f, 0.0f, 1.0f), std::memory_order_relaxed);
            return;
        }
        ArpSID::runtimeHandleRenderedCC(*this, ch, cc, val);
        const float norm = std::clamp(static_cast<float>(val) / 127.0f, 0.0f, 1.0f);
        if (const int knobIndex = ArpSID::sidAkaiMpkMiniDefaultKnobIndex(cc); knobIndex >= 0) {
            telemetryMpkKnobValue_[(size_t)knobIndex].store(norm, std::memory_order_relaxed);
            telemetryLastMappedCC_.store((int)cc, std::memory_order_relaxed);
            telemetryLastMappedCCValue_.store(norm, std::memory_order_relaxed);
        }
        if (cc == 1 && drs_()) drs_()->setPerformanceModWheel(norm);
        else if (cc == 11) {
            if (runtimeExecutionOwner_)
                runtimeExecutionOwner_->applyProjectedNormalizedParameter(kParamMasterVolume, norm);
        }
        else {
            const ParamID mapped = runtimeMappedRealtimeCcParam_(cc);
            if (mapped != kNumParams && runtimeExecutionOwner_)
                runtimeExecutionOwner_->applyProjectedNormalizedParameter(mapped, norm);
        }
        runtimeApplyImmediateCcAudioProjection_(cc);
        if (runtimeIsSynthModeEnabled()) {
            switch (cc) {
                case 1:
                case 2:
                case 7:
                case 11:
                case 64:
                case 66:
                    reseedSynthModeRealtimeState_(0, 0u, true);
                    {
                        bool activeChannels[16]{};
                        for (const auto& sv : engineBank_.synthVoices) {
                            if (!sv.active) continue;
                            activeChannels[std::clamp<int>(sv.channel, 0, 15)] = true;
                        }
                        bool anyActive = false;
                        for (int bendCh = 0; bendCh < 16; ++bendCh) {
                            if (!activeChannels[bendCh]) continue;
                            anyActive = true;
                            ArpSID::applySynthModePitchBendToVoices(engineBank_.synthVoices,
                                                                    engineBank_.sidWriteQueue,
                                                                    currentSidClockHz_(),
                                                                    runtimeModel_.pitchBendNorm(bendCh) * runtimeModel_.bendRangeSemis(bendCh),
                                                                    bendCh,
                                                                    0u,
                                                                    0u);
                        }
                        if (!anyActive) {
                            const int fallbackCh = std::clamp<int>(ch, 0, 15);
                            ArpSID::applySynthModePitchBendToVoices(engineBank_.synthVoices,
                                                                    engineBank_.sidWriteQueue,
                                                                    currentSidClockHz_(),
                                                                    runtimeModel_.pitchBendNorm(fallbackCh) * runtimeModel_.bendRangeSemis(fallbackCh),
                                                                    fallbackCh,
                                                                    0u,
                                                                    0u);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void handlePitchBend(uint8_t ch, int16_t bend14) noexcept {
        ArpSID::runtimeHandleRenderedPitchBend(*this, ch, bend14);
        const int c = std::clamp<int>(ch, 0, 15);
        if (runtimeIsSynthModeEnabled())
            reseedSynthModeRealtimeState_(0, 0u, true);
        const float bendSemis = runtimeModel_.pitchBendNorm(c) * runtimeModel_.bendRangeSemis(c);
        bool anySynthVoiceForChannel = false;
        for (const auto& sv : engineBank_.synthVoices) {
            if (sv.active && std::clamp<int>(sv.channel, 0, 15) == c) {
                anySynthVoiceForChannel = true;
                break;
            }
        }
        if (anySynthVoiceForChannel) {
            ArpSID::applySynthModePitchBendToVoices(engineBank_.synthVoices,
                                                    engineBank_.sidWriteQueue,
                                                    currentSidClockHz_(),
                                                    bendSemis,
                                                    c,
                                                    0u,
                                                    0u);
        } else if (runtimeIsSynthModeEnabled()) {
            const double bendMul = std::exp2(static_cast<double>(bendSemis) / 12.0);
            for (int voice = 0; voice < 3; ++voice) {
                const size_t base = static_cast<size_t>(voice * 7);
                const uint8_t curLo = ArpSID::currentOrShadowedSidReg(sreg_(), sidQueuedShadow_, static_cast<uint8_t>(base + 0));
                const uint8_t curHi = ArpSID::currentOrShadowedSidReg(sreg_(), sidQueuedShadow_, static_cast<uint8_t>(base + 1));
                const uint16_t unbent = static_cast<uint16_t>(curLo | (static_cast<uint16_t>(curHi) << 8));
                const uint16_t bent = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(std::lround(static_cast<double>(unbent) * bendMul)), 0, 65535));
                const uint8_t lo = static_cast<uint8_t>(bent & 0xFFu);
                const uint8_t hi = static_cast<uint8_t>((bent >> 8) & 0xFFu);
                sreg_().write(static_cast<uint8_t>(base + 0), lo);
                publishSidCoreRegWrite_(static_cast<uint8_t>(base + 0), lo,
                    sidCoreWriteStamp_(0));
                sreg_().write(static_cast<uint8_t>(base + 1), hi);
                publishSidCoreRegWrite_(static_cast<uint8_t>(base + 1), hi,
                    sidCoreWriteStamp_(0));
                sidQueuedShadow_.value[base + 0] = lo;
                sidQueuedShadow_.value[base + 1] = hi;
                sidQueuedShadow_.valid[base + 0] = sidQueuedShadow_.valid[base + 1] = 1u;
                sidQueuedShadow_.sample[base + 0] = sidQueuedShadow_.sample[base + 1] = 0u;
                sidQueuedShadow_.cycle[base + 0] = sidQueuedShadow_.cycle[base + 1] = 0u;
            }
        }
        if (drs_()) drs_()->setPerformancePitchBendSemis(std::clamp(bendSemis, -12.0f, 12.0f));
        handlePitchWheelContinuation_(ch, bend14);
    }

    void runtimeApplyNormalizedParameter(uint32_t id, float v) noexcept { if (runtimeExecutionOwner_) runtimeExecutionOwner_->applyProjectedNormalizedParameter(id, v); }
    void runtimeBitPerfectSetSustainPedal(int ch, bool on) noexcept { if (bpe_()) bpe_()->setSustainPedal(std::clamp(ch, 0, 15), on); }
    void runtimeBitPerfectSetSostenutoPedal(int ch, bool on) noexcept { if (bpe_()) bpe_()->setSostenutoPedal(std::clamp(ch, 0, 15), on); }
    void runtimeSetSustainState(int ch, bool on) noexcept {
        const int c = std::clamp(ch, 0, 15);
        runtimeModel_.setSustainState(c, on);
        engineBank_.pedalState.setSustain(c, on);
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlSustainBase) + c), on ? 1.0f : 0.0f);
    }
    void runtimeSetSostenutoState(int ch, bool on) noexcept {
        const int c = std::clamp(ch, 0, 15);
        runtimeModel_.setSostenutoState(c, on);
        engineBank_.pedalState.setSostenuto(c, on);
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(static_cast<int>(kParamHostCtrlSostenutoBase) + c), on ? 1.0f : 0.0f);
    }
    void runtimeVoicePolicySetSustain(int ch, bool on) noexcept {
        if (auto* vp = runtimeVoicePolicy_()) {
            ArpSID::applySynthModeSustainPedal(
                *vp,
                engineBank_.synthVoices,
                std::clamp(ch, 0, 15),
                on,
                0u,
                0u,
                [this](int voiceIdx, uint16_t off16, uint16_t cyc16, bool clearTracking) noexcept {
                    hardSynthModeVoiceOff_(voiceIdx, off16, cyc16, clearTracking);
                });
        }
    }
    void runtimeVoicePolicySetSostenuto(int ch, bool on) noexcept {
        if (auto* vp = runtimeVoicePolicy_()) {
            ArpSID::applySynthModeSostenutoPedal(
                *vp,
                engineBank_.synthVoices,
                std::clamp(ch, 0, 15),
                on,
                0u,
                0u,
                [this](int voiceIdx, uint16_t off16, uint16_t cyc16, bool clearTracking) noexcept {
                    hardSynthModeVoiceOff_(voiceIdx, off16, cyc16, clearTracking);
                });
        }
    }
    void runtimeHardSynthAllNotesOffChannel(int ch) noexcept {
        if (auto* vp = runtimeVoicePolicy_()) {
            if (ch < 0) {
                vp->clearAllNotesOffStateOnly();
            } else {
                vp->allNotesOffChannel(std::clamp(ch, 0, 15));
            }
        }
        for (int i = 0; i < ArpSID::kSidSynthVoiceCount; ++i) {
            const auto& v = engineBank_.synthVoices[(size_t)i];
            if (!v.active) continue;
            if (ch >= 0 && v.channel >= 0 && v.channel != ch) continue;
            hardSynthModeVoiceOff_(i, 0u, 0u, true);
        }
    }
    void runtimeAllNotesOff() noexcept { allNotesOff(); }
    void runtimeAllNotesOffChannel(int channel) noexcept { allNotesOffChannel(channel); }
    void runtimeBitPerfectSetPitchBend14(int ch, int raw14) noexcept { if (bpe_()) bpe_()->setPitchBend14(ch, raw14); }
    void runtimeApplySoftPedal(int c, bool on) noexcept {
        ArpSID::runtimeRenderHostApplySoftPedal(*this, c, on, runtimeHostSurface_().softPedalSavedCutoff);
    }

    void handlePitchWheelContinuation_(uint8_t ch, int16_t bend14) noexcept {
        // Keep continuation as state/telemetry maintenance only. The actual engine
        // pitch-bend application already happens in runtimeHandleRenderedPitchBend(...),
        // so repeating setPitchBend14(...) here would double-apply the same bend.
        ArpSID::runtimeContinueRenderedPitchBend(*this, ch, bend14);
    }

    void handleAftertouch(uint8_t ch, uint8_t pressure) noexcept {
        ArpSID::runtimeHandleRenderedChannelPressure(*this, ch, pressure);
        const int c = std::clamp<int>(ch, 0, 15);
        const float norm = runtimeModel_.channelPressureNorm(c);
        ArpSID::applySynthModeChannelPressureToVoices(engineBank_.synthVoices,
                                                      engineBank_.sidWriteQueue,
                                                      c,
                                                      norm,
                                                      0u,
                                                      0u);
        if (drs_()) drs_()->setPerformancePressure(norm);
    }
    void handlePolyPressure(uint8_t ch, uint8_t note, uint8_t pressure) noexcept {
        ArpSID::runtimeHandleRenderedPolyPressure(*this, ch, note, pressure);
    }

    void allNotesOff() noexcept { runtimeApplyAllNotesOffPerformanceReset_(-1, false); }
    void allNotesOffChannel(int channel) noexcept { runtimeApplyAllNotesOffPerformanceReset_(channel, false); }

    uint32_t droppedMidiIntentCount() const noexcept { return midiQueue_.dropped.load(std::memory_order_relaxed); }
    uint32_t droppedParamIntentCount() const noexcept { return paramIntentQueue_.dropped.load(std::memory_order_relaxed); }
    // audit P2.13: cumulative canonical event-queue overflow telemetry accessors.
    uint32_t eventOverflowDroppedNoteOnCount() const noexcept { return eventOverflowDroppedNoteOn_.load(std::memory_order_relaxed); }
    uint32_t eventOverflowDroppedNoteOffCount() const noexcept { return eventOverflowDroppedNoteOff_.load(std::memory_order_relaxed); }
    uint32_t eventOverflowDroppedAutomationCount() const noexcept { return eventOverflowDroppedAutomation_.load(std::memory_order_relaxed); }
    uint32_t eventOverflowDroppedControllerCount() const noexcept { return eventOverflowDroppedController_.load(std::memory_order_relaxed); }
    uint32_t eventOverflowDroppedTransportCount() const noexcept { return eventOverflowDroppedTransport_.load(std::memory_order_relaxed); }
    uint32_t eventOverflowReplacedLowerPriorityCount() const noexcept { return eventOverflowReplacedLowerPriority_.load(std::memory_order_relaxed); }
    uint32_t eventOverflowDroppedTotalCount() const noexcept { return eventOverflowDroppedTotal_.load(std::memory_order_relaxed); }

    // audit P1.6: split full vs producer-contention drops so ingress telemetry is honest.
    uint32_t droppedMidiIntentFullCount() const noexcept { return midiQueue_.droppedFull.load(std::memory_order_relaxed); }
    uint32_t droppedMidiIntentContentionCount() const noexcept { return midiQueue_.droppedContention.load(std::memory_order_relaxed); }
    uint32_t droppedParamIntentFullCount() const noexcept { return paramIntentQueue_.droppedFull.load(std::memory_order_relaxed); }
    uint32_t droppedParamIntentContentionCount() const noexcept { return paramIntentQueue_.droppedContention.load(std::memory_order_relaxed); }
    uint32_t droppedIngressCount() const noexcept { return ingressDropTelemetry_.load(std::memory_order_relaxed); }
    uint32_t paramIntentDirtyFlushFallbackCount() const noexcept {
        return paramIntentDirtyFlushFallbackTelemetry_.load(std::memory_order_relaxed);
    }

    //──────────────────────────────────────────────────────
    // Host transport / tempo (call once per render buffer)
    //──────────────────────────────────────────────────────
    void setHostTempo(double bpm) noexcept {
        if (bpm > 0.0 && bpm < 1000.0) runtimeHostSurface_().hostTempo = bpm;
    }
    void setTransportPlaying(bool playing) noexcept {
        runtimeHostSurface_().transportPlaying = playing;
    }
    void setTransportBeatPosition(double beatPos) noexcept {
        runtimeHostSurface_().hostBeatPosition = beatPos;
        runtimeHostSurface_().hasPublishedBeatPosition = false;
    }
    void handleTransportDiscontinuity_(bool wasPlaying,
                                       bool isPlaying,
                                       double prevBeatPosition,
                                       double nextBeatPosition,
                                       int frameCount) noexcept {
        const bool startEdge = !wasPlaying && isPlaying;
        const bool stopEdge = wasPlaying && !isPlaying;
        bool seekDiscontinuity = false;
        if (std::isfinite(prevBeatPosition) && std::isfinite(nextBeatPosition) && frameCount > 0 && sampleRate_ > 0.0) {
            const double bpm = runtimeHostSurface_().hostTempo > 0.0 ? runtimeHostSurface_().hostTempo : 120.0;
            const double beatsPerSample = bpm / (sampleRate_ * 60.0);
            const double expectedAdvance = wasPlaying ? beatsPerSample * (double)frameCount : 0.0;
            const double actualAdvance = nextBeatPosition - prevBeatPosition;
            // Tolerance must absorb normal AUv2 polling jitter: the host beat
            // is sampled by a ~5 ms background poller, NOT at render-block
            // boundaries, so the beat snapshot can lead or lag the current
            // block by up to one poll-interval plus one render-block worth
            // of beats. Worst-case lag at 5 ms poll + 11 ms render block at
            // 200 BPM is ≈ 16 ms × (200/60) ≈ 0.053 beats. We allow either
            // • 0.10 beats absolute (a generous musical eighth-of-an-eighth)
            // • or 1.5 render-blocks worth of beat advance
            // whichever is larger. Smaller deltas are treated as polling
            // jitter, not a host seek, so we don't restart the sequencer or
            // re-arm the transport-restart logic on every poll boundary.
            const double jitterToleranceBeats = std::max(0.10, beatsPerSample * (double)frameCount * 1.5);
            const double tolerance = std::max(1e-4, jitterToleranceBeats);
            seekDiscontinuity = std::fabs(actualAdvance - expectedAdvance) > tolerance;
        }
        seqEngine_.onTransportChange(wasPlaying, isPlaying);
        if (startEdge) {
            clearRuntimeStateForTransportStart_();
            // Arp determinism: seed PRNG from host beat position so DAW bounce
            // always produces the same random arpeggio sequence from bar 1.
            if (arp_()) {
                const double beat = runtimeHostSurface_().hostBeatPosition;
                arp_()->seedFromHostPosition(std::isfinite(beat) ? std::max(0.0, beat) : 0.0);
            }
        }
        if (stopEdge) {
            runtimeApplyAllNotesOffPerformanceReset_(-1, false);
            // v910: keep live MIDI/param ingress across the Stop edge too — a
            // note played exactly as the transport stops is live input, not
            // stale arrangement playback (see the Play-edge comment above).
            resetTransientRenderState_(false);
            runtimeModel_.setSeqStep(0);
            seqEngine_.resetPhase();
            seqInternalBeatPosition_ = 0.0;
            clearSequencerTransportRestart_();
        }
        if (startEdge || stopEdge || seekDiscontinuity) {
            if (isPlaying && runtimeModel_.followHostTempoSeq()) {
                const double hostBeat = (std::isfinite(nextBeatPosition) && nextBeatPosition >= 0.0)
                                            ? nextBeatPosition
                                            : 0.0;
                armSequencerTransportRestart_(hostBeat);
            } else {
                seqEngine_.resetPhase();
                runtimeModel_.setSeqStep(0);
                if (!isPlaying || runtimeModel_.followHostTempoSeq()) seqInternalBeatPosition_ = 0.0;
                clearSequencerTransportRestart_();
            }
            rewindArpPhase();
        }
    }
    double hostTempo()        const noexcept { return runtimeHostSurface_().hostTempo; }
    bool   transportPlaying() const noexcept { return runtimeHostSurface_().transportPlaying; }
    double sampleRate()       const noexcept { return sampleRate_; }
    double getSampleRate()    const noexcept { return sampleRate_; }
    double hostBeatPosition() const noexcept { return runtimeHostSurface_().hostBeatPosition; }
    ArpSID::SidRuntimeHostSurface& runtimeHostSurface() noexcept { return runtimeHostSurface_(); }
    const ArpSID::SidRuntimeHostSurface& runtimeHostSurface() const noexcept { return runtimeHostSurface_(); }
    SidRuntimeModel& runtimeModel() noexcept { return runtimeModel_; }
    const SidRuntimeModel& runtimeModel() const noexcept { return runtimeModel_; }
    ArpSID::SidRuntimeEngineBank& runtimeEngineBank() noexcept { return engineBank_; }
    const ArpSID::SidRuntimeEngineBank& runtimeEngineBank() const noexcept { return engineBank_; }
    std::array<float, kNumParams>& runtimeParameterValues() noexcept { return renderParams_; }
    const std::array<float, kNumParams>& runtimeParameterValues() const noexcept { return renderParams_; }
    VoiceAllocator* runtimeVoicePolicy() noexcept { return runtimeVoicePolicy_(); }
    const VoiceAllocator* runtimeVoicePolicy() const noexcept { return runtimeVoicePolicy_(); }
    double runtimeProjectionHostTempo() const noexcept { return runtimeHostSurface_().hostTempo; }
    ArpSID::SidRuntimeProjectionState& runtimeProjectionState() noexcept { return runtimeProjectionState_; }
    const ArpSID::SidRuntimeProjectionState& runtimeProjectionState() const noexcept { return runtimeProjectionState_; }
    ArpSID::SidRuntimeExecutionOwner<ArpSIDDSPKernel>& runtimeExecutionOwner() noexcept { return *runtimeExecutionOwner_; }
    const ArpSID::SidRuntimeExecutionOwner<ArpSIDDSPKernel>& runtimeExecutionOwner() const noexcept { return *runtimeExecutionOwner_; }
    BitPerfectEngine* runtimeBitPerfectEngine() noexcept { return engineBank_.bitPerfect.get(); }
    const BitPerfectEngine* runtimeBitPerfectEngine() const noexcept { return engineBank_.bitPerfect.get(); }
    DrSidEngine* runtimeDrSidEngine() noexcept { return engineBank_.drSid.get(); }
    const DrSidEngine* runtimeDrSidEngine() const noexcept { return engineBank_.drSid.get(); }
    SidRegisterEngine* runtimeSidRegisterEngine() noexcept { return &engineBank_.sidRegister; }
    const SidRegisterEngine* runtimeSidRegisterEngine() const noexcept { return &engineBank_.sidRegister; }
    float* runtimeFractionalAccumLData() noexcept { return runtimeFractionalAccumL_; }
    int runtimeCurrentRenderOffset() const noexcept { return runtimeCurrentRenderOffset_; }
    // v875 audit closure: applied-write observer hook. SID-register render calls this
    // for every write it actually applies to the audio engine, including scheduler/
    // performance writes that never went through pushSidWriteTimed_(). It is gated
    // for the current render block from cockpit demand, so first-note gate/frequency
    // bursts are captured; if the cosmetic mirror is not advanced at block end, the
    // queued bus writes are discarded. Telemetry only; not audio authority.
    void runtimeMirrorAppliedProjectionWrite(uint8_t reg, uint8_t value,
                                             uint32_t sampleOffset, uint16_t cycleOffset) noexcept {
        if (!c64MirrorObserverActive_.load(std::memory_order_relaxed)) return;
        if (mirrorSidProjectionWriteToC64_(reg, value,
            static_cast<uint16_t>(sampleOffset > 65535u ? 65535u : sampleOffset), cycleOffset)) {
            if (c64ProjectionMirrorQueuedWritesThisBlock_ != UINT32_MAX)
                ++c64ProjectionMirrorQueuedWritesThisBlock_;
        }
    }
    float* runtimeFractionalAccumRData() noexcept { return runtimeFractionalAccumR_; }
    uint32_t* runtimeFractionalWeightData() noexcept { return runtimeFractionalWeight_; }
    uint8_t* runtimeFractionalActiveData() noexcept { return runtimeFractionalActive_; }
    size_t runtimeFractionalAccumCapacity() const noexcept { return static_cast<size_t>(kMaxFramesPerBlock); }
    void runtimeFinalizeFractionalHostSample_(float& outL, float& outR) noexcept {
        outL = outR = 0.0f;
        switch (ArpSID::sidResolveRenderModeFromLiveParams(renderParams_)) {
            case ArpSID::SidRuntimeRenderMode::DrSid:
                if (drs_()) drs_()->finalizeFractionalHostSample(outL, outR);
                break;
            case ArpSID::SidRuntimeRenderMode::BitPerfect:
                if (bpe_()) bpe_()->finalizeFractionalHostSample(outL, outR);
                break;
            case ArpSID::SidRuntimeRenderMode::SidRegister:
                sreg_().resetIntervalCursor();
                break;
            default:
                break;
        }
        outL = ArpSID_sanitizeFloat(outL);
        outR = ArpSID_sanitizeFloat(outR);
    }
    bool tryRenderFractionalSingleSample_(int sampleOffset, float* outL, float* outR) noexcept {
        if (!outL || !outR) return false;
        if (sampleOffset < 0 || sampleOffset >= kMaxFramesPerBlock) return false;
        if (!runtimeFractionalActive_[sampleOffset]) return false;
        float finalL = 0.0f, finalR = 0.0f;
        runtimeFinalizeFractionalHostSample_(finalL, finalR);
        if (runtimeFractionalActive_[sampleOffset] == 2u) {
            const float invW = runtimeFractionalWeight_[sampleOffset] > 0u
                ? (1.0f / static_cast<float>(runtimeFractionalWeight_[sampleOffset]))
                : 0.0f;
            *outL = ArpSID_sanitizeFloat(runtimeFractionalAccumL_[sampleOffset] * invW + finalL);
            *outR = ArpSID_sanitizeFloat(runtimeFractionalAccumR_[sampleOffset] * invW + finalR);
        } else {
            *outL = ArpSID_sanitizeFloat(runtimeFractionalAccumL_[sampleOffset] + finalL);
            *outR = ArpSID_sanitizeFloat(runtimeFractionalAccumR_[sampleOffset] + finalR);
        }
        runtimeFractionalAccumL_[sampleOffset] = 0.0f;
        runtimeFractionalAccumR_[sampleOffset] = 0.0f;
        runtimeFractionalWeight_[sampleOffset] = 0u;
        runtimeFractionalActive_[sampleOffset] = 0u;
        return true;
    }
    void runtimeRenderCanonicalSlice(int rendered, int sliceFrames) noexcept {
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->advanceTempoLinkedControllers(sliceFrames);
        processModMatrix_(sliceFrames);
        if (runtimeCanonicalBlockOutputs_) {
            renderSlice_(runtimeCanonicalBlockOutputs_, rendered, sliceFrames);
            return;
        }
        if (runtimeCanonicalScratchL_ && runtimeCanonicalScratchR_) {
            if (sliceFrames == 1 &&
                tryRenderFractionalSingleSample_(rendered,
                                                 runtimeCanonicalScratchL_ + rendered,
                                                 runtimeCanonicalScratchR_ + rendered)) {
                return;
            }
            std::memset(sliceScratchL_, 0, (size_t)sliceFrames * sizeof(float));
            std::memset(sliceScratchR_, 0, (size_t)sliceFrames * sizeof(float));
            runtimeCurrentRenderOffset_ = rendered;
            ArpSID::renderCanonicalAudioForTarget(*this, sliceScratchL_, sliceScratchR_, sliceFrames);
            runtimeCurrentRenderOffset_ = 0;
            std::memcpy(runtimeCanonicalScratchL_ + rendered, sliceScratchL_, (size_t)sliceFrames * sizeof(float));
            std::memcpy(runtimeCanonicalScratchR_ + rendered, sliceScratchR_, (size_t)sliceFrames * sizeof(float));
        }
    }


    void runtimeRenderCanonicalSubPhaseSpan(int sampleOffset, uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd) noexcept {
        ArpSID::runtimeDispatchSubPhaseIntervalToBackend(*this, sampleOffset, cycleIndex, subphaseStart, subphaseEnd);
    }

    uint16_t runtimeEstimatedCyclesPerHostSample() const noexcept {
        return ArpSID::runtimeEstimatedCyclesPerHostSampleForVariant(sampleRate_, runtimeModel_.variantProfile());
    }

    double runtimePhysicalSampleRateHz() const noexcept { return sampleRate_; }
    double runtimePhysicalSidClockHz() const noexcept { return currentSidClockHz_(); }

    void runtimeRenderCanonicalSubSampleSpan(int sampleOffset, uint16_t cycleStart, uint16_t cycleEnd) noexcept {
        ArpSID::runtimeDispatchIntervalToBackend(*this, sampleOffset, cycleStart, cycleEnd);
    }


    void finalizeCanonicalTiming_(ArpSID::SidTimedEvent& ev, int frameCount) const noexcept {
        if (ev.sample_offset == ArpSID::kSidUnresolvedSampleOffset) {
            ev.sid_cycle_stamp = 0ull;
            return;
        }
        const uint32_t maxFrame = static_cast<uint32_t>(std::max(0, frameCount - 1));
        const uint32_t clamped = std::min(ev.sample_offset, maxFrame);
        const uint16_t explicitCycle = ev.cycle_offset;
        ArpSID::assignBestEffortIntraSampleTiming(ev,
                                                  static_cast<int32_t>(clamped),
                                                  ev.arrival_order,
                                                  ev.subphase,
                                                  sampleRate_,
                                                  currentSidClockHz_(),
                                                  explicitCycle);
        ArpSID::finalizeSidCycleStampFromRuntimeClock(ev, sampleRate_, currentSidClockHz_());
    }

    //──────────────────────────────────────────────────────────────────────────
    // PRIMARY entry point: sample-sliced render with canonical timed events.
    // Replaces the monolithic process() for all new callers.
    // events[] must be sorted by sampleOffset ascending before calling.
    //──────────────────────────────────────────────────────────────────────────
    void processBlock(float** outputsRaw,
                      int outputChannelCount,
                      int numFrames,
                      const TimedEvent* events,
                      int eventCount,
                      const TransportState& transport) noexcept {
        if (!bpe_() || numFrames <= 0) return;
        // Missing/disabled output buses must not freeze the canonical runtime.
        // Render the identical pipeline into fixed preallocated scratch, matching
        // the Phase2/VST no-output contract, and discard only the final samples.
        const bool noOutputBus = !outputsRaw || outputChannelCount <= 0 || !outputsRaw[0];
        // v855 P1.7: a host bridge passing events==nullptr with eventCount>0 must
        // not dereference null in the chunk-split loops below. Normalize once.
        if (events == nullptr || eventCount < 0) { events = nullptr; eventCount = 0; }
        // audit P0.5: float** carries no channel count, so reading outputsRaw[1]
        // when the caller supplied a one-element array is out-of-bounds UB.
        // Normalize into a local, always-2-element array: a missing right channel
        // is nullptr and is handled by the per-sample null checks throughout this
        // function (true mono callers must route through processBlockMono()).
        float* outputs[2] = {
            noOutputBus ? sliceScratchL_ : outputsRaw[0],
            noOutputBus ? sliceScratchR_ : ((outputChannelCount > 1) ? outputsRaw[1] : nullptr),
        };
        if (noOutputBus && numFrames <= kMaxFramesPerBlock) {
            std::memset(sliceScratchL_, 0, static_cast<size_t>(numFrames) * sizeof(float));
            std::memset(sliceScratchR_, 0, static_cast<size_t>(numFrames) * sizeof(float));
        }
        const auto c64CallbackWallT0 = renderProfileNow_();
        ArpSID::SidRealtimeScope arpsidRtScope_("ArpSIDDSPKernel::processBlock");
        ArpSID::requireSidTablesPrewarmedForRealtime("ArpSIDDSPKernel::processBlock missing SID table prewarm");
        // Drain any pending state restore scheduled by a non-render thread
        // (setFullState:, setCurrentPreset:). Must run before any event dispatch or
        // engine queries so the reset is consistent with the rest of this block.
        drainPendingStateRestore_();
        drainPendingPsidHandoff_();
        applyPendingAudioEngineMode_();
        if (numFrames > kMaxFramesPerBlock) {
            // v910 parent-scope async normalization: drain the cross-thread
            // MIDI/param rings ONCE here so their hostTime/explicit offsets
            // resolve against the FULL parent block, then slice them per chunk
            // exactly like host events[]. Previously each chunk drained the
            // rings with chunk-local numFrames and the stale parent
            // renderHostTime_, clamping any event intended for a later chunk
            // into the first chunk tail (queued-MIDI/param timing drift).
            parentAsyncEventScratch_.reset();
            drainAsyncIngressToParentScope_(numFrames, parentAsyncEventScratch_);
            publishEventOverflowTelemetry_(parentAsyncEventScratch_.overflow());
            // Fallback ledgers are also parent-scope authorities; drain them
            // before recursive chunks, while child raw-ring drains are disabled.
            drainPendingCriticalMidiFallbacks_(numFrames);
            drainPendingDroppedNoteOffs_(numFrames, renderHostTime_.load(std::memory_order_relaxed));
            const bool previousParentChunkAsyncDrainActive = parentChunkAsyncDrainActive_;
            parentChunkAsyncDrainActive_ = true;
            int processed = 0;
            while (processed < numFrames) {
                const int chunkFrames = std::min(kMaxFramesPerBlock, numFrames - processed);
                chunkEventScratch_.reset();
                EventBuffer& chunkEvents = chunkEventScratch_;
                // Async ring events first (matches the non-chunked dispatch
                // order: midiQueue_/paramIntentQueue_ drain before events[]).
                for (int i = 0; i < parentAsyncEventScratch_.count; ++i) {
                    const TimedEvent& src = parentAsyncEventScratch_.events[i];
                    const bool unresolved = src.sampleOffset < 0;
                    const bool inChunk = ((unresolved && processed == 0) || (src.sampleOffset >= processed && src.sampleOffset < processed + chunkFrames));
                    if (!inChunk) continue;
                    TimedEvent dst = src;
                    if (!unresolved) dst.sampleOffset -= processed;
                    chunkEvents.push(dst);
                }
                for (int i = 0; i < eventCount; ++i) {
                    const TimedEvent& src = events[i];
                    const bool unresolved = src.sampleOffset < 0;
                    const bool inChunk = ((unresolved && processed == 0) || (src.sampleOffset >= processed && src.sampleOffset < processed + chunkFrames));
                    if (!inChunk) continue;
                    TimedEvent dst = src;
                    if (!unresolved) dst.sampleOffset -= processed;
                    chunkEvents.push(dst);
                }
                publishEventOverflowTelemetry_(chunkEvents.overflow());
                chunkEvents.sort();
                const int chunkEventCount = chunkEvents.count;
                TransportState chunkTransport = transport;
                chunkTransport.sampleRate = sampleRate_;
                chunkTransport.frameCount = chunkFrames;
                const double blockSampleRate = sampleRate_;
                if (blockSampleRate > 0.0 && transport.bpm > 0.0) {
                    // Beat position uses the negotiated render sample-rate authority, never a
                    // bogus transport snapshot rate.
                    const double beatsPerSample = transport.bpm / (blockSampleRate * 60.0);
                    chunkTransport.beatPosition = transport.beatPosition + static_cast<double>(processed) * beatsPerSample;
                }
                float* chunkOutputs[2] = {
                    (!noOutputBus && outputs[0]) ? (outputs[0] + processed) : nullptr,
                    (!noOutputBus && outputs[1]) ? (outputs[1] + processed) : nullptr
                };
                processBlock(noOutputBus ? nullptr : chunkOutputs,
                             noOutputBus ? 0 : 2,
                             chunkFrames,
                             chunkEvents.events,
                             chunkEventCount,
                             chunkTransport);
                processed += chunkFrames;
            }
            parentChunkAsyncDrainActive_ = previousParentChunkAsyncDrainActive;
            return;
        }
        beginSidCoreBlockTimeline_(numFrames);
        beginC64TelemetryDemandBlock_(numFrames);

        // ── 0. Flush denormals, apply transport ──────────────────────────────
        flushDenormalsToZero_();
        ArpSID::TransportState canonicalTransport = transport;
        canonicalTransport.sampleRate = sampleRate_;
        canonicalTransport.sanitize();

        // C64 SIDPLAY is its own player authority. Apply the minimum render-side
        // parameter work it still depends on (HiFi/post params, flavor policy),
        // then take the player branch before host-block/event-intent/MIDI/seq work.
        const bool earlyDirtyParamsChanged = flushDirtyParams_(false);
        const bool earlyTransientProjectionNeeded = applyTransientControls_();
        if (earlyDirtyParamsChanged || earlyTransientProjectionNeeded) {
            if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(false);
        }
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->syncTempoLinkedControllers(numFrames);
        if (renderC64SidplayPathIfActive_(outputs, numFrames,
                                          c64CallbackWallT0,
                                          renderProfileNow_())) {
            // C64 SIDPLAY is a separate player authority and returns before the
            // projection mirror publisher. Close any observer scope opened for
            // the normal synth/projection path so no active flag or scheduled
            // cosmetic write can leak into the next AU block.
            cancelC64TelemetryDemandBlock_();
            return;
        }
        c64BypassedPerformanceStateCleared_ = false;

        const ArpSID::SidCanonicalHostBlockState hostBlock =
            ArpSID::sidRuntimeBeginCanonicalHostBlock(runtimeModel_,
                                                      runtimeHostSurface_(),
                                                      canonicalTransport,
                                                      sampleRate_,
                                                      numFrames,
                                                      0);
        const bool wasPlaying = hostBlock.wasPlaying;
        const double prevBeatPosition = hostBlock.previousBeatPosition;
        // v910 live-play authority: host-delivered NoteOns while the transport
        // is stopped are live/manual input (keyboard-thru, audition) and must
        // play — matching VST3/Phase2, which never transport-gated them. Only
        // transport-driven internal playback consults
        // suppressTransportSequencerNoteOns; the note gates below consume the
        // live-instrument law, which is constitutionally false.
        const bool suppressHostNoteOns = hostBlock.suppressLiveInstrumentNoteOns;
        (void)canonicalTransport;
        const uint64_t blockStartHostTime = renderHostTime_.load(std::memory_order_relaxed);

        if (!parentChunkAsyncDrainActive_) {
            ArpSIDParamIntentRingBuffer::Event pev;
            while (paramIntentQueue_.pop(pev)) {
                if (paramIntentWasSupersededByFallback_(pev)) continue;
                TimedEvent te{};
                translateParamIntentEvent_(pev, blockStartHostTime, numFrames, te);
                auto canonical = te.toCanonical();
                finalizeCanonicalTiming_(canonical, numFrames);
                ArpSID::sidWrapperPushEvent(runtimeModel_, canonical, numFrames);
            }
        }

        // ── 0b. Sync SequencerEngine from params ────────────────────────────────
        // Re-anchor stepsPerBeat every block as an integration safety net so
        // SID-808 pads always quantize to 16ths against the host BPM/PPQ. If
        // a state restore (preset load, project open) ever clears the
        // stepsPerBeat value, the next block re-anchors it before any host        // beat-driven advance reaches the step grid.
        syncSequencerPatternFromParams_();
        seqEngine_.setEnabled(ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_));
        seqEngine_.setSampleRate(sampleRate_);
        seqEngine_.setStepsPerBeat(4.0f);    // 16ths per quarter — SID-808 grid resolution
        seqEngine_.setSwing(renderParams_[(size_t)kParamSeqSwing]);
        // Push the live host BPM into the seq tempo every block too. This
        // matters in two cases: (a) when the host follows-tempo path is off
        // but BPM is still used for swing/anticipation, and (b) when host
        // BPM changes mid-project — without this push, swing offsets are
        // computed against the stale tempo until the next setTempo at
        // sequencer-advance time.
        if (runtimeHostSurface_().hostTempo > 1.0) {
            seqEngine_.setTempo(runtimeHostSurface_().hostTempo);
        }
        const bool isPlaying = hostBlock.isPlaying;
        handleTransportDiscontinuity_(wasPlaying,
                                      isPlaying,
                                      prevBeatPosition,
                                      hostBlock.currentBeatPosition,
                                      numFrames);

        // ── 1. Drain cross-thread MIDI queue (UI piano / CoreMIDI) ───────────
        // MIDI-triggered DIGI pads need the same current GUI projection as the
        // sequencer/DIGI renderer. Refresh once before draining so C4..G4 pad
        // hits see the latest slot/sample assignments, even when the user edits
        // the DIGI tab while playback is running.
        {
            const std::uint8_t midiDigiStep = static_cast<std::uint8_t>(
                std::clamp(runtimeModel_.seqStep(), 0, static_cast<int>(ArpSID::GUI::kDigiStepCount - 1u)));
            refreshGuiRealtimeProjection_(midiDigiStep);
            // MIDI-triggered DIGI must consume pending GUI policy before events
            // are drained. Otherwise an AUTH/FAST/debug-legacy switch made from the
            // GUI could be applied later in renderDigiSamplerLayer_(), clearing
            // a freshly armed MIDI-triggered DIGI voice in the same block. The
            // render layer calls this again as a no-op safety net.
            consumeDigiD418RuntimePolicyChange_();
        }
        if (!parentChunkAsyncDrainActive_) {
            drainPendingCriticalMidiFallbacks_(numFrames);
            ArpSIDMidiRingBuffer::Event ev;
            while (midiQueue_.pop(ev)) {
                TimedEvent te{};
                if (!translateRawMidiRingEvent_(ev, blockStartHostTime, numFrames, te)) continue;
                // v911 single ingress authority: identical dispatch law as the
                // host events[] path below. Accepted async note events mirror
                // held ingress at canonical dispatch for release parity.
                dispatchCanonicalIngressEvent_(te, numFrames, suppressHostNoteOns);
            }
            // The primary queue is now represented in canonical ingress. Append
            // any overflowed releases with their original host timestamp so a
            // key-up cannot vanish or overtake an earlier key-down.
            drainPendingDroppedNoteOffs_(numFrames, blockStartHostTime);
        }

        // ── 2. Static state for the normal synth/DrSID/DIGI render path was
        // already flushed above so C64 SIDPLAY can branch before this point.
        syncSequencerPatternFromParams_();
        const bool seqEnabled = ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_);
        const bool followHostSeq = runtimeModel_.followHostTempoSeq();
        const int seqLength = seqLengthFromNorm_(renderParams_[(size_t)kParamSeqLength]);
        const int seqModeIndex = std::clamp((int)std::lround(renderParams_[(size_t)kParamSeqMode] * 3.0f), 0, 3);
        if ((seqEnabled && !prevSeqEnabled_) ||
            (followHostSeq != prevSeqFollowHost_) ||
            (seqLength != prevSeqLength_) ||
            (seqModeIndex != prevSeqModeIndex_)) {
            if (followHostSeq && runtimeHostSurface_().transportPlaying) {
                const double hostBeat = (std::isfinite(runtimeHostSurface_().hostBeatPosition) &&
                                         runtimeHostSurface_().hostBeatPosition >= 0.0)
                                            ? runtimeHostSurface_().hostBeatPosition
                                            : 0.0;
                armSequencerTransportRestart_(hostBeat);
            } else {
                clearSequencerTransportRestart_();
                syncSequencerCursorToBeat_(seqInternalBeatPosition_);
            }
        }
        prevSeqEnabled_ = seqEnabled;
        prevSeqFollowHost_ = followHostSeq;
        prevSeqLength_ = seqLength;
        prevSeqModeIndex_ = seqModeIndex;
        if (!seqEnabled) runtimeModel_.setSeqStep(0);
        if (bpe_()) bpe_()->setScopeCaptureEnabled(consumePresentationScopeDemand_(numFrames));

        // ── 3. Canonicalize host/UI + sequencer events, then sliced render on canonical order ─────
        int ei = 0;
        while (ei < eventCount) {
            const TimedEvent& ev = events[ei];
            if ((ev.kind == EventKind::ParameterSet || ev.kind == EventKind::ParameterRamp) &&
                (ev.target == (uint32_t)kParamProgram || ev.target == (uint32_t)kParamBankSlot)) {
                // Final render-thread firewall: even if a wrapper or future AU
                // translator leaks Program/BankSlot as a timed parameter event,
                // do not let transport-start metadata become audio preset
                // authority. Explicit preset APIs apply a SidStateRoot instead.
                ++ei;
                continue;
            }
            if (ev.kind == EventKind::ParameterRamp && ev.target < (uint32_t)kNumParams) {
                const bool rampResolved = (ev.sampleOffset >= 0);
                const int startSample = rampResolved ? std::clamp(ev.sampleOffset, 0, std::max(0, numFrames - 1)) : -1;
                const int targetParam = (int)ev.target;
                const float startValue = ArpSID::sanitizeNormalizedParamValue(targetParam,
                                                                              renderParams_[(size_t)ev.target],
                                                                              ArpSID::defaultNormalizedParamValue(targetParam));
                const float endValue = ArpSID::sanitizeNormalizedParamValue(targetParam,
                                                                            std::isfinite(ev.value_f32) ? ev.value_f32 : ev.value,
                                                                            startValue);
                TimedEvent rampPoint = ev;
                rampPoint.kind = EventKind::ParameterSet;
                if (!rampResolved) {
                    rampPoint.sampleOffset = -1;
                    rampPoint.value = endValue;
                    rampPoint.value_f32 = endValue;
                    auto canonical = rampPoint.toCanonical();
                    finalizeCanonicalTiming_(canonical, numFrames);
                    ArpSID::sidWrapperPushEvent(runtimeModel_, canonical, numFrames);
                } else {
                    const int blockEnd = std::max(0, numFrames - 1);
                    const int requestedFrames = std::max(1, (int)ev.data14);
                    const int availableFrames = std::max(1, blockEnd - startSample + 1);
                    const int rampFrames = std::min(requestedFrames, availableFrames);
                    constexpr int kMaxRampAnchors = 64;
                    const int anchorCount = std::min(kMaxRampAnchors, std::max(2, rampFrames));
                    for (int anchor = 0; anchor < anchorCount; ++anchor) {
                        TimedEvent anchorPoint = rampPoint;
                        const float t = (anchorCount <= 1) ? 1.0f : (float)anchor / (float)(anchorCount - 1);
                        anchorPoint.sampleOffset = std::clamp(startSample + (int)std::lround((double)(rampFrames - 1) * t), 0, blockEnd);
                        anchorPoint.value = ArpSID::sanitizeNormalizedParamValue(targetParam,
                                                                                 startValue + (endValue - startValue) * t,
                                                                                 startValue);
                        anchorPoint.value_f32 = anchorPoint.value;
                        auto canonical = anchorPoint.toCanonical();
                        finalizeCanonicalTiming_(canonical, numFrames);
                        ArpSID::sidWrapperPushEvent(runtimeModel_, canonical, numFrames);
                    }
                }
            } else {
                // v910 single ingress authority: identical dispatch law as the
                // midiQueue_ drain above. ev.ingressProvenance decides the
                // held-mirror site: host-provided timed events mirror at
                // dispatch; parent-scope injected ring events were already
                // mirrored at enqueue time.
                dispatchCanonicalIngressEvent_(ev, numFrames, suppressHostNoteOns);
            }
            ++ei;
        }
        const bool seqFollowHost = runtimeModel_.followHostTempoSeq();
        const float seqTempoBpm = seqTempoBpmFromNorm_(renderParams_[(size_t)kParamSeqTempo]);
        double blockBeatStart = 0.0;
        double blockBeatEnd = 0.0;
        bool advanceSequencer = false;
        if (sampleRate_ > 0.0) {
            if (seqFollowHost) {
                if (runtimeHostSurface_().transportPlaying && runtimeHostSurface_().hostTempo > 0.0) {
                    const double beatsPerSample = runtimeHostSurface_().hostTempo / (sampleRate_ * 60.0);
                    blockBeatStart = runtimeHostSurface_().hostBeatPosition;
                    blockBeatEnd   = blockBeatStart + numFrames * beatsPerSample;
                    if (seqTransportRestartPending_) {
                        const double restartBeat = (std::isfinite(seqTransportRestartBeat_) &&
                                                    seqTransportRestartBeat_ >= 0.0)
                                                       ? seqTransportRestartBeat_
                                                       : blockBeatStart;
                        if (restartBeat <= blockBeatEnd + 1.0e-9) {
                            blockBeatStart = std::min(blockBeatStart, restartBeat);
                            clearSequencerTransportRestart_();
                        }
                    }
                    seqInternalBeatPosition_ = blockBeatEnd;
                    seqEngine_.setTempo(runtimeHostSurface_().hostTempo);
                    advanceSequencer = true;
                }
            } else if (seqTempoBpm > 0.0f) {
                const double beatsPerSample = seqTempoBpm / (sampleRate_ * 60.0);
                blockBeatStart = seqInternalBeatPosition_;
                blockBeatEnd   = blockBeatStart + numFrames * beatsPerSample;
                seqInternalBeatPosition_ = blockBeatEnd;
                seqEngine_.setTempo(seqTempoBpm);
                advanceSequencer = seqEnabled;
            }
        }
        seqStepBoundaryCount_ = 0;
        if (advanceSequencer) {
            EventBuffer& seqEvts = seqEvtsScratch_;
            seqEvts.count = 0;  // reset: avoid expensive 655 KB zero-init every block
            seqEngine_.advanceWindow(blockBeatStart,
                                     blockBeatEnd,
                                     numFrames,
                                     seqEvts,
                                     seqStepBoundaries_.data(),
                                     static_cast<int>(seqStepBoundaries_.size()),
                                     &seqStepBoundaryCount_);
            runtimeModel_.setSeqStep(seqEngine_.currentStep());
            for (int se = 0; se < seqEvts.count; ++se)
                { auto canonical = seqEvts.events[se].toCanonical();
                  finalizeCanonicalTiming_(canonical, numFrames);
                  ArpSID::sidWrapperPushEvent(runtimeModel_, canonical, numFrames); }
        } else if (!seqEnabled) {
            runtimeModel_.setSeqStep(0);
        }
        // Refresh GUI/KIT projection before the KIT sequencer reads guiRealtimeRender_.
        // Previously DIGI refreshed later in the block, so KIT could compile/play stale state.
        {
            const std::uint8_t guiStep = static_cast<std::uint8_t>(
                std::clamp(runtimeModel_.seqStep(), 0, static_cast<int>(ArpSID::GUI::kDigiStepCount - 1u)));
            refreshGuiRealtimeProjection_(guiStep);
        }
        // Kit sequencer: fire drum notes from the compiled pattern on each step boundary.
        tickKitSequencer_(seqEnabled,
                            numFrames,
                            seqStepBoundaries_.data(),
                            seqStepBoundaryCount_);
        runtimeCanonicalBlockOutputs_ = outputs;
        std::memset(runtimeFractionalAccumL_, 0, (size_t)numFrames * sizeof(float));
        std::memset(runtimeFractionalAccumR_, 0, (size_t)numFrames * sizeof(float));
        std::memset(runtimeFractionalWeight_, 0, (size_t)numFrames * sizeof(uint32_t));
        std::memset(runtimeFractionalActive_, 0, (size_t)numFrames * sizeof(uint8_t));
        runtimeCanonicalScratchL_ = nullptr;
        runtimeCanonicalScratchR_ = nullptr;
        // Use pre-allocated scratch to avoid 1.31 MB stack allocation on the audio thread.
        canonicalQueueScratch_.reset();
        // v903 P1 closure: portamento/glide is register-write-authoritative.
        // The glide scheduler existed but was not wired into the AU3 live render
        // path, so active synth glide state could advance musically in metadata
        // without emitting the frequency writes consumed by the fractional SID
        // path. Schedule once per canonical block before the dispatcher consumes
        // sidWriteQueue. Block-tail survivors are rebased immediately after the
        // canonical render below by runtimeEndFractionalBlock().
        ArpSID::scheduleSynthModeGlideWrites(engineBank_.synthVoices,
                                             engineBank_.sidWriteQueue,
                                             numFrames,
                                             sampleRate_,
                                             currentSidClockHz_());
        const ArpSID::SidPostFxAutomationState postFxBlockStart =
            ArpSID::sidCapturePostFxAutomationState(renderParams_);
        ArpSID::processCanonicalAudioBlockForTargetInto(runtimeModel_, *this, numFrames, canonicalQueueScratch_);
        // v903 P0 closure: the fractional consumer owns block-local SID-write
        // lifetime. Rebase delayed survivors at block end so hard-restart/test
        // and glide writes scheduled past the tail become due in the next block
        // instead of remaining permanently at stale sampleOffset >= frameCount.
        ArpSID::runtimeEndFractionalBlock(*this, static_cast<uint32_t>(numFrames));
        const SidRuntimeRenderMode modeForDigiLayer = sidResolveRenderModeFromLiveParams(renderParams_);
        // internal DIGI REC source "AU Pure 1:1 SID Engine" must sample
        // the AU SID-engine stream itself, before bridge drums, DIGI overlay, or any
        // AU post-processing. captured here after renderDigiSamplerLayer_(),
        // which made the source useful but not actually pure when DIGI/drum layers
        // were active. Capture now sits directly after canonical SID rendering.
        capturePureSid1Q1RecordSource_(outputs, numFrames);
        renderDrumBridgeIfActive_(outputs, numFrames);  // bridge replaces engine-bank drum output
        renderDigiSamplerLayer_(outputs,
                                2,
                                numFrames,
                                modeForDigiLayer,
                                seqEnabled,
                                seqStepBoundaries_.data(),
                                seqStepBoundaryCount_); // DIGI layers after bridge so it is not erased
        const SidTimedEventQueue& canonicalQueue = canonicalQueueScratch_;
        runtimeCanonicalBlockOutputs_ = nullptr;
        canonicalEventsScratch_.count = 0;
        fromCanonicalQueueInto(canonicalQueue, numFrames, canonicalEventsScratch_);
        const EventBuffer& canonicalEvents = canonicalEventsScratch_;
        // audit P2.13: surface canonical event-queue overflow so dropped/replaced
        // NoteOns etc. are observable instead of silently audible. canonicalQueue's
        // overflow ledger is reset per block (canonicalQueueScratch_.reset() above),
        // so accumulating it into cumulative render-published counters is exact.
        publishEventOverflowTelemetry_(canonicalQueue.overflow());

        // ── 4. Post-process: reverb, limiter, NaN guard, telemetry ──────────
        // Phase 5: parity trace (compiled out when ARPSID_PARITY_TRACE=0)
        parityTracer_.traceBlock(blockIndex_++, transport, canonicalEvents,
                                 renderParams_.data(), kNumParams,
                                 outputs[0], outputs[1] ? outputs[1] : outputs[0]);
        // Fix #8: Apply MIX FX chain (master bus channel 0) before reverb/limiter.
        // This is where applyMixFxChain() is wired into the actual AU render path.
        // Unit tests (mix_fx_processors_v554_tests.cpp) verify correctness of the
        // processor DSP; this call site verifies it is actually reached in production.
        const bool pureSid1Q1 = pureSid1Q1OutputModeEnabled();
        if (pureSid1Q1) {
            // direct SID-engine output. Bypass every AU post-engine audio
            // modifier so the rendered stream is the engine's own 1:1 float output.
            // Reset post-FX state on entry so disabling pure mode later cannot leak
            // stale reverb/limiter history into the resumed processed path.
            if (!pureSid1Q1WasActive_) {
                auLimiter_.reset();
                auReverb_.reset();
                auPostFxSilentFrames_ = 0u;
                smoothedReverbMix_ = std::clamp(ArpSID_sanitizeFloat(auReverbMix_), 0.0f, 1.0f);
                smoothedLimiterWet_ = auLimiterEnabled_ ? 1.0f : 0.0f;
                smoothedLimiterThreshold_ = std::clamp(ArpSID_sanitizeFloat(auLimiterThreshold_), 0.05f, 1.5f);
            }
            pureSid1Q1WasActive_ = true;
            sanitizePureSid1Q1Outputs_(outputs, numFrames);
            publishHiFiTelemetryFromProcessor_(resolveHiFiConfigFromRenderParams_());
        } else {
            if (pureSid1Q1WasActive_) {
                auLimiter_.reset();
                auReverb_.reset();
                auPostFxSilentFrames_ = 0u;
                pureSid1Q1WasActive_ = false;
            }
            applyMixFxToOutputs_(outputs, numFrames);
            // Exact canonical timeline: automation at sample N affects sample N
            // and later only. No block-final value is interpolated backwards.
            applyAuReverbLimiterTimeline_(outputs, numFrames, canonicalQueue, postFxBlockStart);
            applyAuHiFiTimeline_(outputs, numFrames, canonicalQueue, postFxBlockStart);
        }
        ArpSID::sidRuntimeAdvanceCanonicalHostBlock(runtimeHostSurface_(), hostBlock, numFrames);
        // v550/v580: advance the SIDCORE timeline and publish a live snapshot.
        // The register-write events for this block have already been published
        // inline at each sreg_().write() call site above.
        finishSidCoreBlockTimeline_(numFrames);

        // Stuck-note safety net: reconcile gated poly voices against the host's
        // authoritative held-key mirror so a lost note-off (asymmetric noteId,
        // channel mismatch, dropped event, arp on/off handoff) can never leave a
        // voice gated forever. Only pure DIRECT polyphony is reconciled — the
        // arpeggiator, sequencer and DrSID generate voices that are not in the
        // MIDI held set, so when any of those is active every key reads as "held"
        // and nothing is cut. A voice must read orphaned on two consecutive blocks
        // (grace inside reconcileUnheldVoices) so a boundary note-off is not clipped.
        if (BitPerfectEngine* bpe = bpe_()) {
            const auto directPolyMode = ArpSID::sidResolveRenderModeFromLiveParams(renderParams_);
            const bool effectiveDirectPolyArp = ArpSID::sidEffectiveArpAuthorityFromLiveParams(renderParams_);
            const bool effectiveDirectPolySeq = ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_);
            const bool directPoly =
                directPolyMode == ArpSID::SidRuntimeRenderMode::BitPerfect &&
                bpe->voiceModeIndex() == 0 &&
                !effectiveDirectPolyArp &&
                !effectiveDirectPolySeq;
            auto& hs = runtimeHostSurface_();
            bpe->reconcileUnheldPolyVoices(
                [&hs, directPoly](int ch, int note) noexcept -> bool {
                    if (!directPoly) return true; // gated off → treat every key as held
                    if (ch < 0 || ch > 15 || note < 0 || note > 127) return true;
                    const uint32_t cg = hs.heldIngressChannelGeneration[(size_t)ch].load(std::memory_order_acquire);
                    const uint32_t ng = hs.heldIngressNoteGeneration[(size_t)ch][(size_t)note].load(std::memory_order_acquire);
                    const uint8_t  d = hs.heldIngressDepth[(size_t)ch][(size_t)note].load(std::memory_order_acquire);
                    return (ng == cg) && d > 0u;
                },
                [this, directPoly](int ch) noexcept -> bool {
                    if (!directPoly) return true;
                    if (ch < 0 || ch > 15) return false;
                    return liveSustainPedalDown_[(size_t)ch].load(std::memory_order_acquire) != 0u ||
                           liveSostenutoPedalDown_[(size_t)ch].load(std::memory_order_acquire) != 0u;
                });
        }
        reconcileSynthModeUnheldVoices_();

        updateTelemetry_(outputs, numFrames);
    }

    void processBlockMono(float* outputMono,
                          int numFrames,
                          const TimedEvent* events,
                          int eventCount,
                          const TransportState& transport) noexcept {
        if (!outputMono || numFrames <= 0) return;
        // v855 P1.7: same null-event normalization as processBlock().
        if (events == nullptr || eventCount < 0) { events = nullptr; eventCount = 0; }
        ArpSID::SidRealtimeScope arpsidRtScope_("ArpSIDDSPKernel::processBlockMono");
        ArpSID::requireSidTablesPrewarmedForRealtime("ArpSIDDSPKernel::processBlockMono missing SID table prewarm");
        // monoScratchR_[kMaxFramesPerBlock] is always safe: each inner call to
        // processBlock uses chunkFrames <= kMaxFramesPerBlock as the chunk size
        // and only writes monoScratchR_[0..chunkFrames-1]. The static_assert below
        // permanently enforces that this member is large enough.
        static_assert(sizeof(monoScratchR_) == kMaxFramesPerBlock * sizeof(float),
                      "monoScratchR_ size must equal kMaxFramesPerBlock floats");
        // v910 parent-scope async normalization for oversized mono blocks: the
        // inner per-chunk processBlock would otherwise drain the rings with
        // chunk-local numFrames and stale parent renderHostTime_ (see the
        // stereo chunking branch above for the full rationale). For blocks
        // within kMaxFramesPerBlock the single inner call drains correctly.
        parentAsyncEventScratch_.reset();
        const bool previousParentChunkAsyncDrainActive = parentChunkAsyncDrainActive_;
        if (numFrames > kMaxFramesPerBlock) {
            drainAsyncIngressToParentScope_(numFrames, parentAsyncEventScratch_);
            publishEventOverflowTelemetry_(parentAsyncEventScratch_.overflow());
            drainPendingCriticalMidiFallbacks_(numFrames);
            drainPendingDroppedNoteOffs_(numFrames, renderHostTime_.load(std::memory_order_relaxed));
            parentChunkAsyncDrainActive_ = true;
        }
        int processed = 0;
        while (processed < numFrames) {
            const int chunkFrames = std::min(kMaxFramesPerBlock, numFrames - processed);
            float* stereo[2] = { outputMono + processed, monoScratchR_ };
            std::memset(monoScratchR_, 0, (size_t)chunkFrames * sizeof(float));
            chunkEventScratch_.reset();
            EventBuffer& chunkEvents = chunkEventScratch_;
            for (int i = 0; i < parentAsyncEventScratch_.count; ++i) {
                const TimedEvent& src = parentAsyncEventScratch_.events[i];
                const bool unresolved = src.sampleOffset < 0;
                const bool inChunk = ((unresolved && processed == 0) || (src.sampleOffset >= processed && src.sampleOffset < processed + chunkFrames));
                if (!inChunk) continue;
                TimedEvent dst = src;
                if (!unresolved) dst.sampleOffset -= processed;
                chunkEvents.push(dst);
            }
            for (int i = 0; i < eventCount; ++i) {
                const TimedEvent& src = events[i];
                const bool unresolved = src.sampleOffset < 0;
                const bool inChunk = ((unresolved && processed == 0) || (src.sampleOffset >= processed && src.sampleOffset < processed + chunkFrames));
                if (!inChunk) continue;
                TimedEvent dst = src;
                if (!unresolved) dst.sampleOffset -= processed;
                chunkEvents.push(dst);
            }
            publishEventOverflowTelemetry_(chunkEvents.overflow());
            chunkEvents.sort();
            const int chunkEventCount = chunkEvents.count;

            TransportState chunkTransport = transport;
            chunkTransport.sampleRate = sampleRate_;
            chunkTransport.frameCount = chunkFrames;
            const double blockSampleRate = sampleRate_;
            if (blockSampleRate > 0.0 && transport.bpm > 0.0) {
                const double beatsPerSample = transport.bpm / (blockSampleRate * 60.0);
                chunkTransport.beatPosition = transport.beatPosition + (double)processed * beatsPerSample;
            }
            processBlock(stereo, 2, chunkFrames, chunkEvents.events, chunkEventCount, chunkTransport);
            for (int i = 0; i < chunkFrames; ++i)
                outputMono[processed + i] = 0.70710678f * (outputMono[processed + i] + monoScratchR_[i]);
            processed += chunkFrames;
        }
        parentChunkAsyncDrainActive_ = previousParentChunkAsyncDrainActive;
    }

    //──────────────────────────────────────────────────────
    // Audio render (render thread only)
    // outputBuffers[0]=L, outputBuffers[1]=R
    //──────────────────────────────────────────────────────
    // process() entry point removed — use processBlock() for all render paths.

    //──────────────────────────────────────────────────────
    // Arp transport sync (safe to call from render thread)
    //──────────────────────────────────────────────────────
    void rewindArpPhase() noexcept {
        if (arp_()) arp_()->rewindPhase(true);
    }

    //──────────────────────────────────────────────────────
    // Telemetry — safe to call from ANY thread
    //──────────────────────────────────────────────────────
    static constexpr int kOscBufLen      = 512; // must be power of 2
    static constexpr int kTelSidRegCount = 30;  // kSidRegCount = 0x1E = 30

    struct Telemetry {
        struct TokenCell {
            uint64_t token = 0;
            int   note = -1;
            int   channel = 0;
            float velocity = 0.0f;
            float polyPressure = 0.0f;
            bool  sustained = false;
            bool  sostenuto = false;
            bool  focused = false;
        };
        uint64_t telemetryFrameId = 0u;
        uint64_t digiFrameId = 0u;
        uint64_t mainOscFrameId = 0u;
        uint64_t hostSampleStart = 0u;
        uint64_t hostSampleEnd = 0u;
        float peakL        = 0.f;
        float peakR        = 0.f;
        float rmsL         = 0.f;
        float rmsR         = 0.f;
        int   activeVoices = 0;
        int   arpStep      = 0;
        int   lastMidiNote = -1;
        uint8_t sidRegs[30] = {};   // kSidRegCount = 0x1E = 30
        float   voiceEnvLevel[3] = {}; // VCO1/2/3 envelope levels [0..1], render-published
        double  hostTempo  = 120.0;
        double  hostBeat   = 0.0;
        bool    hostPlaying = false;
        int     renderMode = 0;
        int     sidModel   = 1;
        int     programNumber = 0;
        int     bankSlot   = 0;
        int     voiceMode  = 0;
        bool    synthMode  = false;
        bool    drSidMode  = false;
        bool    psidActive = false;
        int     drSidPlaybackMode = 0;  ///< B10: DrSidPlaybackMode cast to int (0=Legacy..4=Wavetable)
        bool    arpEnabled = false;
        bool    arpFollowHost = false;
        bool    seqEnabled = false;
        bool    seqFollowHost = false;
        int     seqStep    = 0;
        float   seqTempoBpm = 120.0f;
        float   drumVolume = 0.0f;
        float   drumMachineModel = 0.0f;
        float   drumAccentAmount = 0.0f;
        float   drumOutputDrive = 0.0f;
        float   drumHatMetal = 0.0f;
        float   drumClapSpread = 0.0f;
        float   drumKickTune = 0.0f;
        float   drumKickDecay = 0.0f;
        float   drumSnareTone = 0.0f;
        float   drumSnareSnap = 0.0f;
        float   drumHatTune = 0.0f;
        float   drumHatDecay = 0.0f;
        float   drumCowbellTune = 0.0f;
        float   drumCowbellDecay = 0.0f;
        float   drumTomTune = 0.0f;
        float   drumTomDecay = 0.0f;
        float   drumLevel[DrSidEngine::kDrumTypeCount] = {};
        float   drumVoiceLevel[3] = {};
        float   gmDrumNoteLevel[DrSidEngine::kGMDrumNoteCount] = {};
        int     lastDrumNote = -1;
        int     lastDrumClass = 255;
        float   lastDrumVelocity = 0.0f;
        int     sid808ConfiguredKit = -1;
        uint64_t sid808RoutedHitCount = 0u;
        int     sid808LastRoutedDrumClass = 255;
        int     sid808LastRoutedMidiNote = -1;
        float   sid808LastRoutedVelocity = 0.0f;
        float   sid808OutputPeak = 0.0f;
        int     sid808ActiveVoiceCount = 0;
        uint64_t sid808SilentActiveBlockCount = 0u;
        uint64_t sid808ZeroPeakWithActiveVoiceCount = 0u;
        bool    sid808SilentActiveSinceLastHit = false;
        float   sid808RawPeakBeforeDc = 0.0f;
        float   sid808RawMeanBeforeDc = 0.0f;
        float   sid808PostDcPeak = 0.0f;
        float   sid808PostDcMean = 0.0f;
        float   sid808DcBlockerR = 0.0f;
        uint64_t sid808DcBlockerResetCount = 0u;
        float   sid808LastSnareSnapPeak = 0.0f;
        float   sid808LastSnareSnapRms = 0.0f;
        float   sid808LastSnareBodyPeak = 0.0f;
        float   sid808LastSnareBodyRms = 0.0f;
        uint64_t sid808SnareMicroStageAppliedCount = 0u;
        uint64_t sid808SnareMicroStageLateCount = 0u;
        bool    sid808BridgeContextActive = false;
        bool    sid808BridgeReplacedOutput = false;
        int     digiActiveSlots = 0;
        int     digiConfiguredFactorySlots = 0;
        int     digiConfiguredUserImportSlots = 0;
        int     digiPlayingVoices = 0;
        int     digiPeakVoices = 0;   ///< high-water with slow decay
        int     digiStep = 0;
        int     digiLastSlot = 255;
        int     digiLastFactorySlot = 0;
        uint32_t digiTriggerCount = 0;
        uint32_t digiMidiTriggerCount = 0;
        uint32_t digiMidiIgnoredCount = 0;
        int      digiLastMidiNote = -1;
        int      digiLastMidiChannel = -1;
        int      digiMidiRootNote = ArpSID::DigiD418StreamEngine::kDefaultMidiRootNote;
        int      digiMidiChannelFilter = 16; // 0..15 fixed channel, 16=OMNI
        uint32_t digiGuiPadAcceptedCount = 0;
        uint32_t digiGuiPadIgnoredCount = 0;
        int      digiGuiPadLastSlot = -1;
        int      digiGuiPadLastVelocity = 0;
        bool     digiGuiPadLastAccepted = false;
        uint32_t digiUnavailableUserImports = 0;
        float   digiOutputPeak = 0.0f;
        float   digiScope[DigiSamplerEngine::kScopeLen] = {};
        uint32_t digiScopeWritePos = 0;
        // ── Authentic DIGI D418 telemetry ───────────────────────────────────
        // These values mirror the render-thread counters for the bus-write
        // DIGI engine. Writes are counted cumulatively and per block,
        // IO‑blocked writes are tracked separately, and collision and
        // queue overflow diagnostics are exposed. The last nibble, last
        // $D418 value and last open bus value provide live debugging info.
        uint32_t digiD418WriteCount = 0;
        uint32_t digiD418WritesThisBlock = 0;
        uint32_t digiD418SidAcceptedWriteCount = 0;
        uint32_t digiD418SidAcceptedWritesThisBlock = 0;
        uint32_t digiD418WritesBlockedByIo = 0;
        uint32_t digiD418WriteQueueOverflow = 0;
        uint32_t digiD418CollisionCount = 0;
        uint32_t digiD418OpenBusDriveCount = 0;
        uint32_t digiD418TimelineDiscontinuityResetCount = 0;
        uint32_t digiD418ForensicWritePos = 0;
        uint32_t digiD418LastHostFrame = 0;
        uint32_t digiD418LastPhi2Low = 0;
        uint8_t  digiAuthMode = 0; // release: 0=AUTH C64-bus D418, 1=FAST private D418; 2 only in ARPSID_ENABLE_LEGACY_FLOAT_DIGI debug builds
        uint8_t  digiD418LastNibble = 0;
        uint8_t  digiD418LastOldD418 = 0;
        uint8_t  digiD418LastD418 = 0;
        uint8_t  digiD418LastOpenBus = 0xFF;
        uint8_t  digiD418LastIoVisible = 1;
        uint8_t  digiD418LastSidAccepted = 0;
        float   lfoValue[4] = {};
        float   lfoPhase[4] = {};
        float   env1Level = 0.0f;
        float   lastNoteVelocity = 0.0f;
        float   modWheelNorm = 0.0f;
        float   focusedPitchBend = 0.0f;
        float   focusedChannelPressure = 0.0f;
        float   focusedPolyPressure = 0.0f;
        float   randomValue = 0.0f;
        float   forensicActivity = 0.0f;
        float   forensicIntensity = 0.0f;
        bool    forensicEnabled = false;
        float   forensicClockJitter = 0.0f;
        float   forensicSupplyRipple = 0.0f;
        float   forensicThermalDrift = 0.0f;
        float   forensicVoiceCrosstalk = 0.0f;
        float   forensicExternalBleed = 0.0f;
        float   forensicFilterOhmic = 0.0f;
        float   forensicSystemNoise = 0.0f;
        float   forensicD418Asymmetry = 0.0f;
        float   forensicEnvelopeTDM = 0.0f;
        float   forensicMotherboard = 0.0f;
        float   forensicADCBleed = 0.0f;
        float   forensicBusCollision = 0.0f;
        float   forensicPotInput = 0.0f;
        bool    forensicDigifix8580 = false;
        bool    hifiEnabled = false;
        int     hifiQuality = 0;
        int     hifiOversampling = 8;
        float   hifiDryPeak = 0.0f;
        float   hifiWetPeak = 0.0f;
        float   hifiDeltaPeak = 0.0f;
        float   hifiMonoCorrelation = 1.0f;
        float   hifiSafetyGain = 1.0f;
        // C64 platform telemetry: GUI-visible deterministic 6510/CIA/VIC/SID-bus
        // projection. This is a render-published scalar snapshot only; the GUI
        // never owns or advances the platform core.
        bool     c64PlatformEnabled = true;
        bool     c64MirrorEnabled = false;
        bool     c64PsidRuntimeActive = false;
        bool     c64ProjectionOnly = false;
        bool     c64Pal = true;
        bool     c64RealtimeRunning = false;
        bool     c64Booted = false;
        // Fix #8: provenance flags for GUI "from file" vs "UI fallback" display.
        bool     c64VideoFromFile   = false;   ///< PAL/NTSC was set by file header
        bool     c64SidModelFromFile = false;  ///< SID model was set by file header
        uint8_t  c64SidModel = 0;              ///< 0=6581, 1=8580, 2=both/unknown
        uint8_t  c64PsidLastParseResult = 0;  ///< PsidParseResult code from last AU load attempt
        uint8_t  c64PsidLastLoadFailure = 0;  ///< C64Runtime::PsidLoadFailure code from last AU load attempt
        // Fidelity ratio for GUI: near 1.0 = full coverage; <0.95 = mirror degraded.
        float    c64MirrorFidelity = 1.0f;
        uint64_t c64Phi2Cycle = 0;
        uint64_t c64BlockIndex = 0;
        uint32_t c64PlayCalls = 0;
        uint64_t c64PlayCallCapHits = 0;
        uint64_t c64PlayCallsDroppedByCapTotal = 0;
        uint32_t c64PlayCallsDroppedByCapLastBlock = 0;
        uint32_t c64PlayCallsDroppedByCapMaxBlock = 0;
        uint64_t c64RenderTransactionRollbackFailureCount = 0;
        uint8_t  c64RenderContaminated = 0;                 // v872 P1-4: latched rollback-failure recovery
        uint64_t c64RenderContaminationRecoveryCount = 0;
        uint64_t c64ContinuousBudgetHitCount = 0;           // v872 P2-1: continuous-run health
        uint64_t c64ContinuousCpuJamCount = 0;
        uint64_t c64ContinuousUnsupportedOpcodeCount = 0;
        uint64_t c64ContinuousIncompleteRunCount = 0;
        float    c64PlayRateHz = 0.0f;
        uint16_t c64CpuPc = 0;
        uint8_t  c64CpuA = 0;
        uint8_t  c64CpuX = 0;
        uint8_t  c64CpuY = 0;
        uint8_t  c64CpuSp = 0;
        uint8_t  c64CpuStatus = 0;
        bool     c64CpuJammed = false;
        bool     c64IrqLine = false;
        bool     c64NmiLine = false;
        bool     c64TrapBrkAsJam = false;
        uint8_t  c64ProcessorPort = 0xFF;
        uint16_t c64VicRaster = 0;
        uint8_t  c64VicCycle = 0;
        bool     c64VicBadline = false;
        bool     c64VicBa = true;
        bool     c64VicAec = true;
        bool     c64VicSpriteDma = false;
        uint8_t  c64VicHalfCycle = 0;
        uint64_t c64VicFrame = 0;
        uint32_t c64VicTotalStolen = 0;
        uint8_t  c64OpenBus = 0;
        uint8_t  c64OpenBusDecayMask = 0xFFu;
        bool     c64OpenBusDrivenWithinPersistence = true;
        uint64_t c64OpenBusAgePhi2 = 0;
        uint64_t c64OpenBusLastDrivenPhi2 = 0;
        uint32_t c64SidOpenBusReadCount = 0;
        uint32_t c64ColorRamOpenBusReadCount = 0;
        uint32_t c64PotxyOpenBusReadCount = 0;
        uint8_t  c64LastRead = 0;
        uint8_t  c64LastSidReg = 0;
        uint8_t  c64LastSidValue = 0;
        uint64_t c64LastSidWriteCycle = 0;
        float    c64OpenBusScope[kC64BusScopeLen] = {};
        float    c64SidBusScope[kC64BusScopeLen] = {};
        float    c64SidRegScope[kC64BusScopeLen] = {};
        float    c64SidValueScope[kC64BusScopeLen] = {};
        float    c64SidWritePulseScope[kC64BusScopeLen] = {};
        float    c64Phi2Scope[kC64BusScopeLen] = {};
        float    c64IrqDmaScope[kC64BusScopeLen] = {};
        float    c64ChipScope[kC64BusScopeLen] = {};
        uint32_t c64BusScopeWritePos = 0;
        uint8_t  c64Cia1Irq = 0;
        uint8_t  c64Cia2Irq = 0;
        bool     c64Cia1IrqLine = false;
        bool     c64Cia2IrqLine = false;
        bool     c64IecAtn = true;
        bool     c64IecClk = true;
        bool     c64IecData = true;
        bool     c64IecSrq = true;
        bool     c64TapeMotor = false;
        bool     c64TapeSense = true;
        bool     c64TapeWrite = false;
        bool     c64TapeRead = true;
        uint64_t c64TapePulseCount = 0;
        float   mpkKnobValue[8] = {};
        int     lastMappedCC = -1;
        float   lastMappedCCValue = 0.0f;
        int     activeTokenCount = 0;     // visible token slots copied below (0..8)
        int     totalActiveTokenCount = 0; // total active runtime voices (0..64)
        std::array<TokenCell, 8> tokens{};
    };

    Telemetry readTelemetry() const noexcept {
        return readTelemetry(true);
    }

    Telemetry readTelemetry(bool includeScopes) const noexcept {
        // GUI/read side of the telemetry seqlock. The audio thread publishes an
        // odd generation while updating the atomics and an even generation when
        // the frame is stable. Return only a snapshot whose acquire-before and
        // acquire-after generations match and are even.
        for (int telemetryReadAttempt = 0; telemetryReadAttempt < 8; ++telemetryReadAttempt) {
            const uint32_t gen0 = telemetryGeneration_.load(std::memory_order_acquire);
            if (gen0 & 1u) continue;
            Telemetry t;
            t.telemetryFrameId = telemetryFrameId_.load(std::memory_order_relaxed);
            t.peakL        = ArpSIDSanitizeTelemetryUnitFloat(telemetryPeakL_.load(std::memory_order_relaxed));
            t.peakR        = ArpSIDSanitizeTelemetryUnitFloat(telemetryPeakR_.load(std::memory_order_relaxed));
            t.rmsL         = ArpSIDSanitizeTelemetryUnitFloat(telemetryRmsL_.load(std::memory_order_relaxed));
            t.rmsR         = ArpSIDSanitizeTelemetryUnitFloat(telemetryRmsR_.load(std::memory_order_relaxed));
            t.activeVoices = telemetryVoices_.load(std::memory_order_relaxed);
            t.arpStep      = telemetryArpStep_.load(std::memory_order_relaxed);
            t.lastMidiNote = telemetryLastNote_.load(std::memory_order_relaxed);
            for (int r = 0; r < kTelSidRegCount; ++r)
                t.sidRegs[r] = telemetrySidRegs_[r].load(std::memory_order_relaxed);
            for (int v = 0; v < 3; ++v)
                t.voiceEnvLevel[v] = ArpSIDSanitizeTelemetryUnitFloat(telemetryVoiceEnvLevel_[(size_t)v].load(std::memory_order_relaxed));

            // Telemetry invariant: consume only the render-published snapshot.
            // This avoids UI-thread reads of live runtime model, host surface, LFOs,
            // sequencer state, and token tables while the audio thread mutates them.
            t.hostTempo   = ArpSIDSanitizeTelemetryTempo(telemetryHostTempo_.load(std::memory_order_relaxed));
            t.hostBeat    = ArpSIDSanitizeTelemetryBeat(telemetryHostBeat_.load(std::memory_order_relaxed));
            t.hostPlaying = telemetryHostPlaying_.load(std::memory_order_relaxed) != 0;
            t.renderMode = std::clamp((int)telemetryRenderMode_.load(std::memory_order_relaxed), 0, 3);
            t.synthMode  = telemetrySynthMode_.load(std::memory_order_relaxed) != 0;
            t.drSidMode  = telemetryDrSidMode_.load(std::memory_order_relaxed) != 0;
            t.psidActive = telemetryPsidActive_.load(std::memory_order_relaxed) != 0;
            // Render-published only: GUI never touches the live DrSID engine.
            t.drSidPlaybackMode = static_cast<int>(
                telemetryDrSidPlaybackMode_.load(std::memory_order_relaxed));
            const auto readTelemetryParam = [this](int pid) noexcept -> float {
                return ArpSIDSanitizeTelemetryUnitFloat(
                    telemetryParamSnapshot_[(size_t)pid].load(std::memory_order_relaxed));
            };
            t.arpEnabled = telemetryArpEnabled_.load(std::memory_order_relaxed) != 0;
            t.arpFollowHost = telemetryArpFollowHost_.load(std::memory_order_relaxed) != 0;
            t.seqEnabled = telemetrySeqEnabled_.load(std::memory_order_relaxed) != 0;
            t.seqFollowHost = telemetrySeqFollowHost_.load(std::memory_order_relaxed) != 0;
            t.seqStep    = telemetrySeqStep_.load(std::memory_order_relaxed);
            const float seqTempoSnapshot = ArpSID_sanitizeFloat(telemetrySeqTempoBpm_.load(std::memory_order_relaxed));
            t.seqTempoBpm = std::clamp(seqTempoSnapshot > 0.0f ? seqTempoSnapshot : 120.0f, 20.0f, 400.0f);
            t.sidModel   = telemetrySidModel_.load(std::memory_order_relaxed);
            t.bankSlot      = std::clamp(telemetryPresetSlot_.load(std::memory_order_relaxed), 0, ArpSID::kCanonicalFactoryPatchSlotMax);
            t.programNumber = t.bankSlot; // patch identity display follows AU sticky preset authority, never GM Program/BankSlot params.
            t.voiceMode     = std::clamp((int)std::lround(readTelemetryParam(kParamVoiceMode) * 3.f), 0, 3);
            t.drumVolume = readTelemetryParam(kParamDrSidVolume);
            t.drumMachineModel = readTelemetryParam(kParamDrSidMachineModel);
            t.drumAccentAmount = readTelemetryParam(kParamDrSidAccentAmount);
            t.drumOutputDrive = readTelemetryParam(kParamDrSidOutputDrive);
            t.drumHatMetal = readTelemetryParam(kParamDrSidHatMetal);
            t.drumClapSpread = readTelemetryParam(kParamDrSidClapSpread);
            t.drumKickTune = readTelemetryParam(kParamDrSidKickTune);
            t.drumKickDecay = readTelemetryParam(kParamDrSidKickDecay);
            t.drumSnareTone = readTelemetryParam(kParamDrSidSnareTone);
            t.drumSnareSnap = readTelemetryParam(kParamDrSidSnareSnap);
            t.drumHatTune = readTelemetryParam(kParamDrSidHatTune);
            t.drumHatDecay = readTelemetryParam(kParamDrSidHatDecay);
            t.drumCowbellTune = readTelemetryParam(kParamDrSidCowbellTune);
            t.drumCowbellDecay = readTelemetryParam(kParamDrSidCowbellDecay);
            t.drumTomTune = readTelemetryParam(kParamDrSidTomTune);
            t.drumTomDecay = readTelemetryParam(kParamDrSidTomDecay);
            for (int i = 0; i < DrSidEngine::kDrumTypeCount; ++i)
                t.drumLevel[i] = ArpSIDSanitizeTelemetryUnitFloat(telemetryDrumLevel_[(size_t)i].load(std::memory_order_relaxed));
            for (int i = 0; i < 3; ++i)
                t.drumVoiceLevel[i] = ArpSIDSanitizeTelemetryUnitFloat(telemetryDrumVoiceLevel_[(size_t)i].load(std::memory_order_relaxed));
            for (int i = 0; i < DrSidEngine::kGMDrumNoteCount; ++i)
                t.gmDrumNoteLevel[i] = ArpSIDSanitizeTelemetryUnitFloat(telemetryGMDrumNoteLevel_[(size_t)i].load(std::memory_order_relaxed));
            t.lastDrumNote = telemetryLastDrumNote_.load(std::memory_order_relaxed);
            t.lastDrumClass = telemetryLastDrumClass_.load(std::memory_order_relaxed);
            t.lastDrumVelocity = ArpSIDSanitizeTelemetryUnitFloat(telemetryLastDrumVelocity_.load(std::memory_order_relaxed));
            {
                const auto sid808Tel = drumEngineBridge_.sid808OutputTelemetry();
                t.sid808ConfiguredKit = std::clamp(sid808Tel.configuredKitSlot, -1, ArpSID::kCanonicalFactoryPatchSlotMax);
                t.sid808RoutedHitCount = sid808Tel.routedHitCount;
                t.sid808LastRoutedDrumClass = std::clamp(sid808Tel.lastRoutedDrumClass, 0, 255);
                t.sid808LastRoutedMidiNote = std::clamp(sid808Tel.lastRoutedMidiNote, -1, 127);
                t.sid808LastRoutedVelocity = ArpSIDSanitizeTelemetryUnitFloat(sid808Tel.lastRoutedVelocity);
                const float postDcPeak = ArpSIDSanitizeTelemetryUnitFloat(
                    telemetrySid808OutputPeak_.load(std::memory_order_relaxed));
                t.sid808OutputPeak = ArpSIDSanitizeTelemetryUnitFloat(std::max(sid808Tel.outputPeak, postDcPeak));
                t.sid808ActiveVoiceCount = std::clamp(static_cast<int>(sid808Tel.activeVoiceCount), 0, 3);
                t.sid808SilentActiveBlockCount = sid808Tel.silentActiveBlockCount;
                t.sid808ZeroPeakWithActiveVoiceCount = sid808Tel.zeroPeakWithActiveVoiceCount;
                t.sid808SilentActiveSinceLastHit = sid808Tel.silentActiveSinceLastHit;
                t.sid808RawPeakBeforeDc = ArpSIDSanitizeTelemetryUnitFloat(
                    telemetrySid808RawPeakBeforeDc_.load(std::memory_order_relaxed));
                t.sid808RawMeanBeforeDc = ArpSIDSanitizeTelemetryBipolarFloat(
                    telemetrySid808RawMeanBeforeDc_.load(std::memory_order_relaxed));
                t.sid808PostDcPeak = ArpSIDSanitizeTelemetryUnitFloat(
                    telemetrySid808PostDcPeak_.load(std::memory_order_relaxed));
                t.sid808PostDcMean = ArpSIDSanitizeTelemetryBipolarFloat(
                    telemetrySid808PostDcMean_.load(std::memory_order_relaxed));
                t.sid808DcBlockerR = ArpSIDSanitizeTelemetryUnitFloat(
                    telemetrySid808DcBlockerR_.load(std::memory_order_relaxed));
                t.sid808DcBlockerResetCount =
                    telemetrySid808DcBlockerResetCount_.load(std::memory_order_relaxed);
                t.sid808LastSnareSnapPeak = ArpSIDSanitizeTelemetryUnitFloat(sid808Tel.lastSnareSnapPeak);
                t.sid808LastSnareSnapRms = ArpSIDSanitizeTelemetryUnitFloat(sid808Tel.lastSnareSnapRms);
                t.sid808LastSnareBodyPeak = ArpSIDSanitizeTelemetryUnitFloat(sid808Tel.lastSnareBodyPeak);
                t.sid808LastSnareBodyRms = ArpSIDSanitizeTelemetryUnitFloat(sid808Tel.lastSnareBodyRms);
                t.sid808SnareMicroStageAppliedCount = sid808Tel.snareMicroStageAppliedCount;
                t.sid808SnareMicroStageLateCount = sid808Tel.snareMicroStageLateCount;
                t.sid808BridgeContextActive = sid808Tel.sid808ContextActive;
                t.sid808BridgeReplacedOutput =
                    telemetrySid808BridgeReplacedOutput_.load(std::memory_order_relaxed) != 0u;
            }
            t.digiActiveSlots = std::clamp((int)telemetryDigiActiveSlots_.load(std::memory_order_relaxed), 0, (int)ArpSID::GUI::kDigiActiveSlotCount);
            t.digiConfiguredFactorySlots = std::clamp((int)telemetryDigiConfiguredFactorySlots_.load(std::memory_order_relaxed), 0, (int)ArpSID::GUI::kDigiActiveSlotCount);
            t.digiConfiguredUserImportSlots = std::clamp((int)telemetryDigiConfiguredUserImportSlots_.load(std::memory_order_relaxed), 0, (int)ArpSID::GUI::kDigiActiveSlotCount);
            t.digiPlayingVoices = std::clamp((int)telemetryDigiPlayingVoices_.load(std::memory_order_relaxed), 0, (int)ArpSID::GUI::kDigiActiveSlotCount);
            t.digiPeakVoices    = std::clamp((int)telemetryDigiPeakVoices_.load(std::memory_order_relaxed),    0, (int)ArpSID::GUI::kDigiActiveSlotCount);
            t.digiStep = std::clamp((int)telemetryDigiStepIndex_.load(std::memory_order_relaxed), 0, (int)ArpSID::GUI::kDigiStepCount - 1);
            t.digiLastSlot = std::clamp((int)telemetryDigiLastSlot_.load(std::memory_order_relaxed), 0, 255);
            t.digiLastFactorySlot = std::clamp((int)telemetryDigiLastFactorySlot_.load(std::memory_order_relaxed), 0, 65535);
            t.digiTriggerCount = telemetryDigiTriggerCount_.load(std::memory_order_relaxed);
            t.digiMidiTriggerCount = telemetryDigiMidiTriggerCount_.load(std::memory_order_relaxed);
            t.digiMidiIgnoredCount = telemetryDigiMidiIgnoredCount_.load(std::memory_order_relaxed);
            {
                const int mn = static_cast<int>(telemetryDigiLastMidiNote_.load(std::memory_order_relaxed));
                const int mc = static_cast<int>(telemetryDigiLastMidiChannel_.load(std::memory_order_relaxed));
                t.digiLastMidiNote = (mn <= 127) ? mn : -1;
                t.digiLastMidiChannel = (mc <= 15) ? mc : -1;
            }
            t.digiMidiRootNote = static_cast<int>(digiMidiRootNote());
            t.digiMidiChannelFilter = static_cast<int>(digiMidiChannelFilter());
            t.digiGuiPadAcceptedCount = telemetryDigiGuiPadAcceptedCount_.load(std::memory_order_relaxed);
            t.digiGuiPadIgnoredCount = telemetryDigiGuiPadIgnoredCount_.load(std::memory_order_relaxed);
            {
                const int gs = static_cast<int>(telemetryDigiGuiPadLastSlot_.load(std::memory_order_relaxed));
                t.digiGuiPadLastSlot = (gs < static_cast<int>(ArpSID::GUI::kDigiActiveSlotCount)) ? gs : -1;
            }
            t.digiGuiPadLastVelocity = static_cast<int>(telemetryDigiGuiPadLastVelocity_.load(std::memory_order_relaxed));
            t.digiGuiPadLastAccepted = telemetryDigiGuiPadLastAccepted_.load(std::memory_order_relaxed) != 0u;
            t.digiUnavailableUserImports = telemetryDigiUnavailableUserImports_.load(std::memory_order_relaxed);
            t.digiOutputPeak = ArpSIDSanitizeTelemetryUnitFloat(telemetryDigiOutputPeak_.load(std::memory_order_relaxed));
            if (includeScopes) {
                DigiScopeSnapshot digiScope{};
                telemetryDigiScopeTriple_.peekLatest(digiScope);
                if (digiScope.frameId == t.telemetryFrameId) {
                    for (int i = 0; i < DigiSamplerEngine::kScopeLen; ++i)
                        t.digiScope[i] = ArpSIDSanitizeTelemetryBipolarFloat(digiScope.scope[i]);
                    t.digiScopeWritePos = digiScope.writePos & static_cast<uint32_t>(DigiSamplerEngine::kScopeLen - 1);
                    t.digiFrameId = digiScope.frameId;
                }
            }
            // Authentic DIGI D418 telemetry snapshot
            t.digiD418WriteCount = telemetryDigiD418WriteCount_.load(std::memory_order_relaxed);
            t.digiD418WritesThisBlock = telemetryDigiD418WritesThisBlock_.load(std::memory_order_relaxed);
            t.digiD418SidAcceptedWriteCount = telemetryDigiD418SidAcceptedWriteCount_.load(std::memory_order_relaxed);
            t.digiD418SidAcceptedWritesThisBlock = telemetryDigiD418SidAcceptedWritesThisBlock_.load(std::memory_order_relaxed);
            t.digiD418WritesBlockedByIo = telemetryDigiD418WritesBlockedByIo_.load(std::memory_order_relaxed);
            t.digiD418WriteQueueOverflow = telemetryDigiD418WriteQueueOverflow_.load(std::memory_order_relaxed);
            t.digiD418CollisionCount = telemetryDigiD418CollisionCount_.load(std::memory_order_relaxed);
            t.digiD418OpenBusDriveCount = telemetryDigiD418OpenBusDriveCount_.load(std::memory_order_relaxed);
            t.digiD418TimelineDiscontinuityResetCount = telemetryDigiD418TimelineDiscontinuityResetCount_.load(std::memory_order_relaxed);
            t.digiD418ForensicWritePos = telemetryDigiD418ForensicWritePos_.load(std::memory_order_relaxed);
            t.digiD418LastHostFrame = telemetryDigiD418LastHostFrame_.load(std::memory_order_relaxed);
            t.digiD418LastPhi2Low = telemetryDigiD418LastPhi2Low_.load(std::memory_order_relaxed);
            t.digiAuthMode = telemetryDigiAuthMode_.load(std::memory_order_relaxed);
            t.digiD418LastNibble = telemetryDigiD418LastNibble_.load(std::memory_order_relaxed);
            t.digiD418LastOldD418 = telemetryDigiD418LastOldD418_.load(std::memory_order_relaxed);
            t.digiD418LastD418 = telemetryDigiD418LastD418_.load(std::memory_order_relaxed);
            t.digiD418LastOpenBus = telemetryDigiD418LastOpenBus_.load(std::memory_order_relaxed);
            t.digiD418LastIoVisible = telemetryDigiD418LastIoVisible_.load(std::memory_order_relaxed);
            t.digiD418LastSidAccepted = telemetryDigiD418LastSidAccepted_.load(std::memory_order_relaxed);
            t.env1Level = ArpSIDSanitizeTelemetryUnitFloat(telemetryEnv1Level_.load(std::memory_order_relaxed));
            t.lastNoteVelocity = ArpSIDSanitizeTelemetryUnitFloat(telemetryLastNoteVelocity_.load(std::memory_order_relaxed));
            t.modWheelNorm = ArpSIDSanitizeTelemetryUnitFloat(telemetryModWheelNorm_.load(std::memory_order_relaxed));
            t.focusedPitchBend = ArpSIDSanitizeTelemetryBipolarFloat(telemetryFocusedPitchBend_.load(std::memory_order_relaxed));
            t.focusedChannelPressure = ArpSIDSanitizeTelemetryUnitFloat(telemetryFocusedChannelPressure_.load(std::memory_order_relaxed));
            t.focusedPolyPressure = ArpSIDSanitizeTelemetryUnitFloat(telemetryFocusedPolyPressure_.load(std::memory_order_relaxed));
            t.randomValue = ArpSIDSanitizeTelemetryBipolarFloat(telemetryRandomValue_.load(std::memory_order_relaxed));
            t.forensicActivity = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicActivity_.load(std::memory_order_relaxed));
            t.forensicIntensity = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicIntensity_.load(std::memory_order_relaxed));
            t.forensicEnabled = telemetryForensicEnabled_.load(std::memory_order_relaxed) != 0;
            t.forensicClockJitter = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicClockJitter_.load(std::memory_order_relaxed));
            t.forensicSupplyRipple = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicSupplyRipple_.load(std::memory_order_relaxed));
            t.forensicThermalDrift = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicThermalDrift_.load(std::memory_order_relaxed));
            t.forensicVoiceCrosstalk = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicVoiceCrosstalk_.load(std::memory_order_relaxed));
            t.forensicExternalBleed = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicExternalBleed_.load(std::memory_order_relaxed));
            t.forensicFilterOhmic = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicFilterOhmic_.load(std::memory_order_relaxed));
            t.forensicSystemNoise = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicSystemNoise_.load(std::memory_order_relaxed));
            t.forensicD418Asymmetry = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicD418Asymmetry_.load(std::memory_order_relaxed));
            t.forensicEnvelopeTDM = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicEnvelopeTDM_.load(std::memory_order_relaxed));
            t.forensicMotherboard = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicMotherboard_.load(std::memory_order_relaxed));
            t.forensicADCBleed = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicADCBleed_.load(std::memory_order_relaxed));
            t.forensicBusCollision = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicBusCollision_.load(std::memory_order_relaxed));
            t.forensicPotInput = ArpSIDSanitizeTelemetryUnitFloat(telemetryForensicPotInput_.load(std::memory_order_relaxed));
            t.forensicDigifix8580 = telemetryForensicDigifix8580_.load(std::memory_order_relaxed) != 0;
            t.hifiEnabled = telemetryHiFiEnabled_.load(std::memory_order_relaxed) != 0;
            t.hifiQuality = std::clamp((int)telemetryHiFiQuality_.load(std::memory_order_relaxed), 0, 2);
            t.hifiOversampling = std::clamp((int)telemetryHiFiOversampling_.load(std::memory_order_relaxed), 1, 16);
            t.hifiDryPeak = ArpSIDSanitizeTelemetryUnitFloat(telemetryHiFiDryPeak_.load(std::memory_order_relaxed));
            t.hifiWetPeak = ArpSIDSanitizeTelemetryUnitFloat(telemetryHiFiWetPeak_.load(std::memory_order_relaxed));
            t.hifiDeltaPeak = ArpSIDSanitizeTelemetryUnitFloat(telemetryHiFiDeltaPeak_.load(std::memory_order_relaxed));
            t.hifiMonoCorrelation = ArpSIDSanitizeTelemetryBipolarFloat(telemetryHiFiMonoCorrelation_.load(std::memory_order_relaxed));
            t.hifiSafetyGain = ArpSIDSanitizeTelemetryUnitFloat(telemetryHiFiSafetyGain_.load(std::memory_order_relaxed));

            // C64 platform GUI projection. Consume only the render-owned
            // deterministic C64 mirror snapshot; never synthesize fallback
            // CPU/VIC/CIA values from host beat on the GUI/read side.
            t.c64PlatformEnabled = telemetryC64PlatformEnabled_.load(std::memory_order_relaxed) != 0;
            t.c64MirrorEnabled = telemetryC64MirrorEnabled_.load(std::memory_order_relaxed) != 0;
            t.c64PsidRuntimeActive = telemetryC64PsidRuntimeActive_.load(std::memory_order_relaxed) != 0;
            t.c64ProjectionOnly = telemetryC64ProjectionOnly_.load(std::memory_order_relaxed) != 0;
            t.c64Pal = telemetryC64Pal_.load(std::memory_order_relaxed) != 0;
            t.c64RealtimeRunning = telemetryC64RealtimeRunning_.load(std::memory_order_relaxed) != 0;
            t.c64Booted = telemetryC64Booted_.load(std::memory_order_relaxed) != 0;
            // Fix #8: provenance flags.
            t.c64VideoFromFile    = telemetryC64VideoFromFile_.load(std::memory_order_relaxed) != 0;
            t.c64SidModelFromFile = telemetryC64SidModelFromFile_.load(std::memory_order_relaxed) != 0;
            t.c64SidModel         = telemetryC64SidModel_.load(std::memory_order_relaxed);
            t.c64PsidLastParseResult = telemetryC64PsidLastParseResult_.load(std::memory_order_relaxed);
            t.c64PsidLastLoadFailure = telemetryC64PsidLastLoadFailure_.load(std::memory_order_relaxed);
            t.c64MirrorFidelity = std::clamp(telemetryC64MirrorFidelity_.load(std::memory_order_relaxed), 0.0f, 1.0f);
            t.c64Phi2Cycle = telemetryC64Phi2Cycle_.load(std::memory_order_relaxed);
            t.c64BlockIndex = telemetryC64BlockIndex_.load(std::memory_order_relaxed);
            t.hostSampleStart = telemetryHostSampleStart_.load(std::memory_order_relaxed);
            t.hostSampleEnd = telemetryHostSampleEnd_.load(std::memory_order_relaxed);
            t.c64PlayCalls = telemetryC64PlayCalls_.load(std::memory_order_relaxed);
            t.c64PlayCallCapHits = telemetryC64PlayCallCapHits_.load(std::memory_order_relaxed);
            t.c64PlayCallsDroppedByCapTotal =
                telemetryC64PlayCallsDroppedByCapTotal_.load(std::memory_order_relaxed);
            t.c64PlayCallsDroppedByCapLastBlock =
                telemetryC64PlayCallsDroppedByCapLastBlock_.load(std::memory_order_relaxed);
            t.c64PlayCallsDroppedByCapMaxBlock =
                telemetryC64PlayCallsDroppedByCapMaxBlock_.load(std::memory_order_relaxed);
            t.c64RenderTransactionRollbackFailureCount =
                telemetryC64RenderTransactionRollbackFailureCount_.load(std::memory_order_relaxed);
            t.c64RenderContaminated =
                telemetryC64RenderContaminated_.load(std::memory_order_relaxed);
            t.c64RenderContaminationRecoveryCount =
                telemetryC64RenderContaminationRecoveryCount_.load(std::memory_order_relaxed);
            t.c64ContinuousBudgetHitCount =
                telemetryC64ContinuousBudgetHitCount_.load(std::memory_order_relaxed);
            t.c64ContinuousCpuJamCount =
                telemetryC64ContinuousCpuJamCount_.load(std::memory_order_relaxed);
            t.c64ContinuousUnsupportedOpcodeCount =
                telemetryC64ContinuousUnsupportedOpcodeCount_.load(std::memory_order_relaxed);
            t.c64ContinuousIncompleteRunCount =
                telemetryC64ContinuousIncompleteRunCount_.load(std::memory_order_relaxed);
            t.c64PlayRateHz = ArpSIDSanitizeTelemetryUnitFloat(telemetryC64PlayRateHz_.load(std::memory_order_relaxed) / 1000.0f) * 1000.0f;
            t.c64CpuPc = telemetryC64CpuPc_.load(std::memory_order_relaxed);
            t.c64CpuA = telemetryC64CpuA_.load(std::memory_order_relaxed);
            t.c64CpuX = telemetryC64CpuX_.load(std::memory_order_relaxed);
            t.c64CpuY = telemetryC64CpuY_.load(std::memory_order_relaxed);
            t.c64CpuSp = telemetryC64CpuSp_.load(std::memory_order_relaxed);
            t.c64CpuStatus = telemetryC64CpuStatus_.load(std::memory_order_relaxed);
            t.c64CpuJammed = telemetryC64CpuJammed_.load(std::memory_order_relaxed) != 0;
            t.c64IrqLine = telemetryC64IrqLine_.load(std::memory_order_relaxed) != 0;
            t.c64NmiLine = telemetryC64NmiLine_.load(std::memory_order_relaxed) != 0;
            t.c64TrapBrkAsJam = telemetryC64TrapBrkAsJam_.load(std::memory_order_relaxed) != 0;
            t.c64ProcessorPort = telemetryC64ProcessorPort_.load(std::memory_order_relaxed);
            t.c64VicRaster = telemetryC64VicRaster_.load(std::memory_order_relaxed);
            t.c64VicCycle = telemetryC64VicCycle_.load(std::memory_order_relaxed);
            t.c64VicBadline = telemetryC64VicBadline_.load(std::memory_order_relaxed) != 0;
            t.c64VicBa = telemetryC64VicBa_.load(std::memory_order_relaxed) != 0;
            t.c64VicAec = telemetryC64VicAec_.load(std::memory_order_relaxed) != 0;
            t.c64VicSpriteDma = telemetryC64VicSpriteDma_.load(std::memory_order_relaxed) != 0;
            t.c64VicHalfCycle = telemetryC64VicHalfCycle_.load(std::memory_order_relaxed);
            t.c64VicFrame = telemetryC64VicFrame_.load(std::memory_order_relaxed);
            t.c64VicTotalStolen = telemetryC64VicTotalStolen_.load(std::memory_order_relaxed);
            t.c64OpenBus = telemetryC64OpenBus_.load(std::memory_order_relaxed);
            t.c64OpenBusDecayMask = telemetryC64OpenBusDecayMask_.load(std::memory_order_relaxed);
            t.c64OpenBusDrivenWithinPersistence =
                telemetryC64OpenBusDrivenWithinPersistence_.load(std::memory_order_relaxed) != 0u;
            t.c64OpenBusAgePhi2 = telemetryC64OpenBusAgePhi2_.load(std::memory_order_relaxed);
            t.c64OpenBusLastDrivenPhi2 = telemetryC64OpenBusLastDrivenPhi2_.load(std::memory_order_relaxed);
            t.c64SidOpenBusReadCount = telemetryC64SidOpenBusReadCount_.load(std::memory_order_relaxed);
            t.c64ColorRamOpenBusReadCount = telemetryC64ColorRamOpenBusReadCount_.load(std::memory_order_relaxed);
            t.c64PotxyOpenBusReadCount = telemetryC64PotxyOpenBusReadCount_.load(std::memory_order_relaxed);
            t.c64LastRead = telemetryC64LastRead_.load(std::memory_order_relaxed);
            t.c64LastSidReg = telemetryC64LastSidReg_.load(std::memory_order_relaxed);
            t.c64LastSidValue = telemetryC64LastSidValue_.load(std::memory_order_relaxed);
            t.c64LastSidWriteCycle = telemetryC64LastSidWriteCycle_.load(std::memory_order_relaxed);
            if (includeScopes) {
                C64BusScopeSnapshot c64BusScope{};
                telemetryC64BusScopeTriple_.peekLatest(c64BusScope);
                if (c64BusScope.frameId == t.telemetryFrameId) for (int i = 0; i < kC64BusScopeLen; ++i) {
                    t.c64OpenBusScope[i] = ArpSIDSanitizeTelemetryBipolarFloat(c64BusScope.openBus[i]);
                    t.c64SidBusScope[i] = ArpSIDSanitizeTelemetryBipolarFloat(c64BusScope.sidBus[i]);
                    t.c64SidRegScope[i] = ArpSIDSanitizeTelemetryBipolarFloat(c64BusScope.sidReg[i]);
                    t.c64SidValueScope[i] = ArpSIDSanitizeTelemetryBipolarFloat(c64BusScope.sidValue[i]);
                    t.c64SidWritePulseScope[i] = ArpSIDSanitizeTelemetryBipolarFloat(c64BusScope.sidWritePulse[i]);
                    t.c64Phi2Scope[i] = ArpSIDSanitizeTelemetryBipolarFloat(c64BusScope.phi2[i]);
                    t.c64IrqDmaScope[i] = ArpSIDSanitizeTelemetryBipolarFloat(c64BusScope.irqDma[i]);
                    t.c64ChipScope[i] = ArpSIDSanitizeTelemetryBipolarFloat(c64BusScope.chip[i]);
                }
                if (c64BusScope.frameId == t.telemetryFrameId)
                    t.c64BusScopeWritePos = c64BusScope.writePos & static_cast<uint32_t>(kC64BusScopeLen - 1);
            }
            t.c64Cia1Irq = telemetryC64Cia1Irq_.load(std::memory_order_relaxed);
            t.c64Cia2Irq = telemetryC64Cia2Irq_.load(std::memory_order_relaxed);
            t.c64Cia1IrqLine = telemetryC64Cia1IrqLine_.load(std::memory_order_relaxed) != 0;
            t.c64Cia2IrqLine = telemetryC64Cia2IrqLine_.load(std::memory_order_relaxed) != 0;
            t.c64IecAtn = telemetryC64IecAtn_.load(std::memory_order_relaxed) != 0;
            t.c64IecClk = telemetryC64IecClk_.load(std::memory_order_relaxed) != 0;
            t.c64IecData = telemetryC64IecData_.load(std::memory_order_relaxed) != 0;
            t.c64IecSrq = telemetryC64IecSrq_.load(std::memory_order_relaxed) != 0;
            t.c64TapeMotor = telemetryC64TapeMotor_.load(std::memory_order_relaxed) != 0;
            t.c64TapeSense = telemetryC64TapeSense_.load(std::memory_order_relaxed) != 0;
            t.c64TapeWrite = telemetryC64TapeWrite_.load(std::memory_order_relaxed) != 0;
            t.c64TapeRead = telemetryC64TapeRead_.load(std::memory_order_relaxed) != 0;
            t.c64TapePulseCount = telemetryC64TapePulseCount_.load(std::memory_order_relaxed);
            if (includeScopes) {
                MainOscScopeSnapshot mainScope{};
                mainOscScopeTriple_.peekLatest(mainScope);
                if (mainScope.frameId == t.telemetryFrameId)
                    t.mainOscFrameId = mainScope.frameId;
            }
            for (int i = 0; i < 8; ++i) {
                t.mpkKnobValue[i] = ArpSIDSanitizeTelemetryUnitFloat(telemetryMpkKnobValue_[(size_t)i].load(std::memory_order_relaxed));
            }
            t.lastMappedCC = telemetryLastMappedCC_.load(std::memory_order_relaxed);
            t.lastMappedCCValue = ArpSIDSanitizeTelemetryUnitFloat(telemetryLastMappedCCValue_.load(std::memory_order_relaxed));
            t.totalActiveTokenCount = telemetryActiveTokenCount_.load(std::memory_order_relaxed);
            t.activeTokenCount = std::clamp(t.totalActiveTokenCount, 0, static_cast<int>(t.tokens.size()));
            for (size_t i = 0; i < t.tokens.size(); ++i) {
                const auto& src = telemetryTokens_[i];
                auto& dst = t.tokens[i];
                dst.token = src.token.load(std::memory_order_relaxed);
                dst.note = std::clamp(src.note.load(std::memory_order_relaxed), 0, 127);
                dst.channel = std::clamp(src.channel.load(std::memory_order_relaxed), 0, 15);
                dst.velocity = ArpSIDSanitizeTelemetryUnitFloat(src.velocity.load(std::memory_order_relaxed));
                dst.polyPressure = ArpSIDSanitizeTelemetryUnitFloat(src.polyPressure.load(std::memory_order_relaxed));
                dst.sustained = src.sustained.load(std::memory_order_relaxed) != 0;
                dst.sostenuto = src.sostenuto.load(std::memory_order_relaxed) != 0;
                dst.focused = src.focused.load(std::memory_order_relaxed) != 0;
            }
            for (int i = 0; i < 4; ++i) {
                t.lfoValue[i] = ArpSIDSanitizeTelemetryBipolarFloat(telemetryLfoValue_[(size_t)i].load(std::memory_order_relaxed));
                t.lfoPhase[i] = ArpSIDSanitizeTelemetryUnitFloat(telemetryLfoPhase_[(size_t)i].load(std::memory_order_relaxed));
            }
            const uint32_t gen1 = telemetryGeneration_.load(std::memory_order_acquire);
            if (gen0 == gen1 && !(gen1 & 1u)) return t;
        }
        return Telemetry{};
    }

    // Copies up to maxSamples from the oscilloscope ring buffer into dst.
    // Returns number of samples written (always maxSamples if buf is full).
    // Per-VCO oscilloscope snapshot — delegates to BitPerfectEngine's scope buffers.
    void getScopeSnapshot(float outVoice[8][256],
                          float outOsc[3][256],
                          float outFilter[2][256],
                          uint8_t& activeMask,
                          uint32_t& writePos) const noexcept {
        TelemetryScopeSnapshot snap{};
        telemetryScopeTriple_.peekLatest(snap);
        std::memcpy(outVoice, snap.voiceScope, sizeof(snap.voiceScope));
        std::memcpy(outOsc, snap.oscScope, sizeof(snap.oscScope));
        std::memcpy(outFilter, snap.filterScope, sizeof(snap.filterScope));
        activeMask = snap.activeMask;
        writePos = snap.writePos;
    }

    void getPresentationFullScopeSnapshot(float outVoice[8][256],
                                          float outOsc[3][256],
                                          float outFilter[2][256],
                                          uint8_t& activeMask,
                                          uint32_t& writePos,
                                          uint64_t& frameId) const noexcept {
        TelemetryScopeSnapshot snap{};
        telemetryScopeTriple_.peekLatest(snap);
        std::memcpy(outVoice, snap.voiceScope, sizeof(snap.voiceScope));
        std::memcpy(outOsc, snap.oscScope, sizeof(snap.oscScope));
        std::memcpy(outFilter, snap.filterScope, sizeof(snap.filterScope));
        activeMask = snap.activeMask;
        writePos = snap.writePos;
        frameId = snap.frameId;
    }

    void getPresentationScopeSnapshot(float outOsc[3][256],
                                      float outFilter[2][256],
                                      uint8_t& activeMask,
                                      uint32_t& writePos) const noexcept {
        TelemetryScopeSnapshot snap{};
        telemetryScopeTriple_.peekLatest(snap);
        std::memcpy(outOsc, snap.oscScope, sizeof(snap.oscScope));
        std::memcpy(outFilter, snap.filterScope, sizeof(snap.filterScope));
        activeMask = snap.activeMask;
        writePos = snap.writePos;
    }

    void getPresentationOscScopeSnapshot(float outOsc[3][256],
                                         uint8_t& activeMask,
                                         uint32_t& writePos) const noexcept {
        TelemetryScopeSnapshot snap{};
        telemetryScopeTriple_.peekLatest(snap);
        std::memcpy(outOsc, snap.oscScope, sizeof(snap.oscScope));
        activeMask = snap.activeMask;
        writePos = snap.writePos;
    }

    int readOscilloscope(float* dst, int maxSamples) const noexcept {
        const int n = std::min(maxSamples, kOscBufLen);
        MainOscScopeSnapshot snap{};
        mainOscScopeTriple_.peekLatest(snap);
        int wp = static_cast<int>(snap.writePos & (kOscBufLen - 1));
        // Read the n most recent samples ending at wp-1
        int rp = (wp - n + kOscBufLen) & (kOscBufLen - 1);
        for (int i = 0; i < n; ++i) {
            dst[i] = snap.mono[rp];
            rp = (rp + 1) & (kOscBufLen - 1);
        }
        return n;
    }

    void notePresentationScopeRequest() noexcept {
        telemetryPresentationScopeSerial_.fetch_add(1u, std::memory_order_relaxed);
    }

    void noteC64TelemetryRequest() noexcept {
        telemetryC64SnapshotDemandSerial_.fetch_add(1u, std::memory_order_relaxed);
    }

    // Update last MIDI note for telemetry display (call from MIDI handlers)
    void setTelemetryNote(int note) noexcept {
        telemetryLastNote_.store(note, std::memory_order_relaxed);
    }

private:
    bool consumePresentationScopeDemand_(int numFrames) noexcept {
        const uint32_t serial = telemetryPresentationScopeSerial_.load(std::memory_order_relaxed);
        if (serial != renderPresentationScopeSerial_) {
            renderPresentationScopeSerial_ = serial;
            const double holdFrames = ArpSIDSanitizeHostSampleRate(sampleRate_) * kTelemetryPresentationScopeHoldSeconds;
            renderPresentationScopeFramesRemaining_ = static_cast<uint32_t>(std::max(1.0, std::ceil(holdFrames)));
        } else if (renderPresentationScopeFramesRemaining_ > 0u) {
            const uint32_t consumedFrames = static_cast<uint32_t>(std::max(numFrames, 0));
            renderPresentationScopeFramesRemaining_ =
                (renderPresentationScopeFramesRemaining_ > consumedFrames)
                    ? (renderPresentationScopeFramesRemaining_ - consumedFrames)
                    : 0u;
        }
        return renderPresentationScopeFramesRemaining_ > 0u;
    }

    bool resolveC64ProjectionMirrorPal_() const noexcept {
        const auto fallbackPal = [this]() noexcept {
            return !(runtimePhysicalSidClockHz() > 1000000.0);
        };
        const ArpSID::C64::C64Runtime* psidLive = c64PsidLive_.load(std::memory_order_acquire);
        if (!psidLive) return fallbackPal();
        const uint32_t liveClock = psidLive->platform().clockHz();
        if (liveClock == ArpSID::C64::kPalPhi2Hz) return true;
        if (liveClock == ArpSID::C64::kNtscPhi2Hz) return false;
        // v882 closure: do not silently classify an uninitialized/unknown live
        // PSID platform clock as NTSC. The mirror clock resolver is used before
        // the applied-write observer opens; a bogus live clock must fall back to
        // the same runtime SID-clock policy as non-PSID projection, otherwise the
        // first observer burst can be scheduled/advanced with the wrong PAL/NTSC
        // authority on fresh handoff or partially initialized test runtimes.
        return fallbackPal();
    }

    void ensureC64ProjectionMirrorClockReady_(bool pal) noexcept {
        const uint32_t targetClock = pal ? ArpSID::C64::kPalPhi2Hz : ArpSID::C64::kNtscPhi2Hz;
        if (c64Platform_.clockHz() != targetClock) {
            // v878: make the cosmetic C64 projection mirror clock/reset-ready before
            // opening the applied-write observer. In v877 the publisher could reset
            // c64Platform_ after observer writes had already been queued, dropping the
            // first note-on/gate burst on PAL/NTSC transitions or fresh mirror init.
            c64Platform_.reset(pal);
            c64Platform_.bootFromResetVector();
            c64Platform_.startRealtimeSidCore();
            c64PlatformClock_.reset();
            c64RealtimeCycleDebt_ = 0;
            c64LastHeavyTelemetryPhi2_ = UINT64_MAX;
            c64ProjectionMirrorQueuedWritesThisBlock_ = 0u;
        }
        c64PlatformClock_.configure(std::max(1.0, sampleRate_),
                                    pal ? static_cast<double>(ArpSID::C64::kPalPhi2Hz)
                                        : static_cast<double>(ArpSID::C64::kNtscPhi2Hz));
    }

    void ensureC64ProjectionMirrorClockReady_() noexcept {
        ensureC64ProjectionMirrorClockReady_(resolveC64ProjectionMirrorPal_());
    }

    bool beginC64TelemetryDemandBlock_(int /*numFrames*/) noexcept {
        ensureC64ProjectionMirrorClockReady_();
        const uint32_t serial = telemetryC64SnapshotDemandSerial_.load(std::memory_order_relaxed);
        if (serial != renderC64SnapshotDemandSerial_) {
            renderC64SnapshotDemandSerial_ = serial;
            const double holdFrames = ArpSIDSanitizeHostSampleRate(sampleRate_) * kTelemetryC64SnapshotHoldSeconds;
            renderC64SnapshotFramesRemaining_ = static_cast<uint32_t>(std::max(1.0, std::ceil(holdFrames)));
        }
        renderC64SnapshotDemandThisBlock_ = renderC64SnapshotFramesRemaining_ > 0u;
        c64ProjectionMirrorQueuedWritesThisBlock_ = 0u;
        c64MirrorObserverActive_.store(renderC64SnapshotDemandThisBlock_ ? 1u : 0u, std::memory_order_relaxed);
        return renderC64SnapshotDemandThisBlock_;
    }

    bool finishC64TelemetryDemandBlock_(int numFrames) noexcept {
        const bool active = renderC64SnapshotDemandThisBlock_;
        if (renderC64SnapshotFramesRemaining_ > 0u) {
            const uint32_t consumedFrames = static_cast<uint32_t>(std::max(numFrames, 0));
            renderC64SnapshotFramesRemaining_ =
                (renderC64SnapshotFramesRemaining_ > consumedFrames)
                    ? (renderC64SnapshotFramesRemaining_ - consumedFrames)
                    : 0u;
        }
        renderC64SnapshotDemandThisBlock_ = false;
        c64MirrorObserverActive_.store(0u, std::memory_order_relaxed);
        return active;
    }

    void cancelC64TelemetryDemandBlock_() noexcept {
        // v876 audit closure: processBlock may take early-return paths after the
        // render-block observer has been armed, notably the C64 SIDPLAY branch.
        // Those paths do not run the projection mirror publisher, so they must
        // explicitly close the cosmetic observer scope and discard any speculative
        // projection events. Do not decrement the presentation-demand hold here;
        // this is a cancellation/cleanup hook, not normal telemetry consumption.
        renderC64SnapshotDemandThisBlock_ = false;
        c64ProjectionMirrorQueuedWritesThisBlock_ = 0u;
        c64RealtimeCycleDebt_ = 0;
        c64MirrorObserverActive_.store(0u, std::memory_order_relaxed);
        // v899: flush (apply values, then remove) instead of dropping — the
        // cancelled block's projection writes describe audio that already
        // played; the register image must land on them even though the
        // cosmetic mirror clock is not advanced. Block-local law unchanged.
        c64Platform_.flushScheduledProjectionWrites();
    }

    bool consumeC64TelemetryDemand_(int numFrames) noexcept {
        // Compatibility path for callers that did not explicitly begin the block.
        // Normal processBlock() calls beginC64TelemetryDemandBlock_() before render
        // so the applied-write observer can capture the first gate/frequency burst.
        if (!renderC64SnapshotDemandThisBlock_) beginC64TelemetryDemandBlock_(numFrames);
        return finishC64TelemetryDemandBlock_(numFrames);
    }

    void stageRenderParam_(ParamID pid, float v) noexcept {
        // v946: keep even legacy/local render staging on the same authority rail
        // as host automation, flavor enforcement and reset policies. This prevents
        // silent params_/renderParams_/runtimeModel split-brain if a future caller
        // reuses this helper for structural params.
        runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(pid), v);
    }

    // deleted legacy normalized-params factory apply helper.
    // Factory preset loads must hydrate/apply a full SidStateRootV1 through
    // schedulePendingStateRestore()/applyStateRootCanonical(). A normalized-only
    // render-param application bypasses state-root semantic canonicalization,
    // variant/profile authority, register-mirror sanitization, and factory host
    // overlay suppression, so keeping this helper around was a latent split-brain
    // reintroduction point.

    //──────────────────────────────────────────────────────
    bool applyTransientControls_() noexcept {
        bool projectionNeeded = false;
        const bool panic = renderParams_[(size_t)kParamPanic] > 0.5f;
        if (panic) {
            runtimeApplyHardPanicPerformanceReset_();
            params_[(size_t)kParamPanic].store(0.f, std::memory_order_relaxed);
            renderParams_[(size_t)kParamPanic] = 0.f;
            dirty_[(size_t)kParamPanic].store(false, std::memory_order_relaxed);
        }

        // Factory patch loading is intentionally NOT driven from render-time
        // parameter observation. Logic/MIDI files can reassert program/bank metadata
        // at transport start; if render interprets that as a factory-patch command,
        // the selected ArpSID patch resets when pressing Play.
        //
        // Only explicit preset APIs / GUI preset selection may call
        // setCurrentPreset() and schedule a factory SidStateRoot restore.
        // kParamBankSlot and kParamProgram are metadata/readback mirrors here.
        prevProgram_ = renderParams_[(size_t)kParamBankSlot];

        // SynthMode transition is handled exclusively in runtimePolicyHandleSynthModeEnable()
        // which is called from the canonical parameter-apply path (projectStateToBackends).
        // That function updates prevSynthMode_ first, so by the time applyTransientControls_
        // runs, synthNow == prevSynthMode_ always holds. The second enforcement site was
        // removed (P3-4 cleanup) to eliminate the dual-authority confusion.

        const int virtualNote = std::clamp((int)std::lround(renderParams_[(size_t)kParamVirtualNote] * 127.f), 0, 127);
        const bool virtualGate = renderParams_[(size_t)kParamVirtualGate] > 0.5f;
        if (virtualGate != prevVirtualGate_ || (virtualGate && virtualNote != prevVirtualNote_)) {
            const uint8_t virtCh = defaultEventChannel_;
            if (!runtimeHostSurface_().transportPlaying) {
                if (prevVirtualGate_ && virtCh != kSidUnresolvedChannel) {
                    handleNoteOff(virtCh, (uint8_t)prevVirtualNote_, prevVirtualNoteId_);
                }
                prevVirtualGate_ = false;
                prevVirtualNote_ = virtualNote;
                prevVirtualNoteId_ = -1;
            } else {
            if (prevVirtualGate_ && virtCh != kSidUnresolvedChannel) {
                handleNoteOff(virtCh, (uint8_t)prevVirtualNote_, prevVirtualNoteId_);
            }
            if (virtualGate && virtCh != kSidUnresolvedChannel) {
                prevVirtualNoteId_ = virtualNoteIdCounter_++;
                handleNoteOn(virtCh, (uint8_t)virtualNote, 100, prevVirtualNoteId_);
            } else if (!virtualGate) {
                prevVirtualNoteId_ = -1;
            }
            prevVirtualGate_ = virtualGate;
            prevVirtualNote_ = virtualNote;
            }
        }

        for (int ch = 0; ch < 16; ++ch) {
            const float rawMod = renderParams_[(size_t)kParamHostCtrlModWheelBase + ch];
            if (std::isfinite(rawMod)) {
                const float mod = ArpSID::canonicalClampedNormalizedValue(rawMod);
                if (mod != runtimeHostSurface_().prevModWheel[(size_t)ch]) {
                    runtimeHostSurface_().prevModWheel[(size_t)ch] = mod;
                    handleCC((uint8_t)ch, 1, (uint8_t)std::clamp((int)std::lround(mod * 127.f), 0, 127));
                }
            }

            const float rawExpr = renderParams_[(size_t)kParamHostCtrlExpressionBase + ch];
            if (std::isfinite(rawExpr)) {
                const float expr = ArpSID::canonicalClampedNormalizedValue(rawExpr);
                if (expr != runtimeHostSurface_().prevExpression[(size_t)ch]) {
                    runtimeHostSurface_().prevExpression[(size_t)ch] = expr;
                    handleCC((uint8_t)ch, 11, (uint8_t)std::clamp((int)std::lround(expr * 127.f), 0, 127));
                }
            }

            const float rawSus = renderParams_[(size_t)kParamHostCtrlSustainBase + ch];
            if (std::isfinite(rawSus)) {
                const float sus = ArpSID::canonicalClampedNormalizedValue(rawSus);
                if (sus != runtimeHostSurface_().prevSustain[(size_t)ch]) {
                    runtimeHostSurface_().prevSustain[(size_t)ch] = sus;
                    handleCC((uint8_t)ch, 64, sus >= 0.5f ? 127 : 0);
                }
            }

            const float rawPressure = renderParams_[(size_t)kParamHostCtrlChannelPressureBase + ch];
            if (std::isfinite(rawPressure)) {
                const float pressure = ArpSID::canonicalClampedNormalizedValue(rawPressure);
                if (pressure != runtimeHostSurface_().prevChannelPressure[(size_t)ch]) {
                    runtimeHostSurface_().prevChannelPressure[(size_t)ch] = pressure;
                    handleAftertouch((uint8_t)ch, (uint8_t)std::clamp((int)std::lround(pressure * 127.f), 0, 127));
                }
            }

            const float rawBend = renderParams_[(size_t)kParamHostCtrlPitchBendBase + ch];
            if (std::isfinite(rawBend)) {
                const float bend = ArpSID::canonicalClampedNormalizedValue(rawBend);
                if (bend != runtimeHostSurface_().prevPitchBend[(size_t)ch]) {
                    runtimeHostSurface_().prevPitchBend[(size_t)ch] = bend;
                    const int bend14 = std::clamp((int)std::lround(bend * 16383.f) - 8192, -8192, 8191);
                    handlePitchBend((uint8_t)ch, (int16_t)bend14);
                }
            }
        }
        return projectionNeeded;
    }



public:
    // Shared canonical target-adapter surface (redirects to engineBank_ accessors above)
    ArpSID::Arpeggiator* runtimeArpeggiator() noexcept { return arp_(); }
    void kernelSetTransportPlaying(bool playing) noexcept {
        runtimeHostSurface_().transportPlaying = playing;
    }
    void kernelSetHostTempo(float bpm) noexcept {
        const float clean = ArpSID::canonicalClampedTempo(bpm, runtimeHostSurface_().hostTempo > 0.0 ? (float)runtimeHostSurface_().hostTempo : 120.0f);
        runtimeHostSurface_().hostTempo = clean;
    }
    void kernelRewindTransport() noexcept { rewindArpPhase(); }
    void kernelApplyNormalizedParameter(uint32_t target, float value) noexcept { applyNormalizedParameterRenderOwned_(target, value); }
    void kernelPanic() noexcept { runtimeApplyHardPanicPerformanceReset_(); }
    void kernelAllNotesOff() noexcept { runtimeApplyAllNotesOffPerformanceReset_(-1, false); }
    void kernelObserveNoteActivity(uint8_t note, float velocity) noexcept { lastNoteVelocity_ = velocity; setTelemetryNote((int)note); }
    void kernelTriggerDrumMidi(uint8_t note, float velocity) noexcept { ArpSID::canonicalTriggerDrumMidi(drs_(), note, velocity); }
    void kernelArpNoteOn(uint8_t note, float velocity) noexcept { ArpSID::canonicalArpNoteOn(arp_(), note, velocity); }
    void kernelArpNoteOff(uint8_t note) noexcept { ArpSID::canonicalArpNoteOff(arp_(), note); }
    void kernelSynthNoteOn(const SidTimedEvent& ev) noexcept {
        const int sampleOffset = std::clamp((int)ev.sample_offset, 0, std::max(0, kMaxFramesPerBlock - 1));
        const uint16_t cycleOffset = ev.cycle_offset;
        pushSynthModeFilterRegsRealtime_(sampleOffset, cycleOffset);
        ArpSID::scheduleSynthModeNoteOn(*runtimeVoicePolicy_(),
                                        engineBank_.synthVoices,
                                        engineBank_.sidWriteQueue,
                                        runtimeModel_.patchStartPolicy(),
                                        renderParams_.data(),
                                        sidQueuedShadow_.value.data(),
                                        sampleRate_,
                                        currentSidClockHz_(),
                                        synthModeVoiceMode_(),
                                        (int)ev.pitch,
                                        ev.value,
                                        sampleOffset,
                                        cycleOffset,
                                        (int)ev.channel,
                                        ev.noteId,
                                        [this](int voiceIdx, uint16_t off16, uint16_t cyc16, bool clearTracking) noexcept {
                                            hardSynthModeVoiceOff_(voiceIdx, off16, cyc16, clearTracking);
                                        },
                                        ev.voiceToken);
    }
    void kernelSynthNoteOff(const SidTimedEvent& ev) noexcept {
        const int sampleOffset = std::clamp((int)ev.sample_offset, 0, std::max(0, kMaxFramesPerBlock - 1));
        const uint64_t voiceToken = ev.voiceToken != 0
            ? ev.voiceToken
            : runtimeModel_.resolveVoiceTokenForIdentity((int)ev.channel, (int)ev.pitch, ev.noteId);
        ArpSID::scheduleSynthModeNoteOff(*runtimeVoicePolicy_(),
                                         engineBank_.synthVoices,
                                         engineBank_.sidWriteQueue,
                                         runtimeModel_.patchStartPolicy(),
                                         renderParams_.data(),
                                         sidQueuedShadow_.value.data(),
                                         sampleRate_,
                                         currentSidClockHz_(),
                                         synthModeVoiceMode_(),
                                         (int)ev.pitch,
                                         sampleOffset,
                                         ev.cycle_offset,
                                         (int)ev.channel,
                                         ev.noteId,
                                         [this](int voiceIdx, uint16_t off16, uint16_t cyc16, bool clearTracking) noexcept {
                                             hardSynthModeVoiceOff_(voiceIdx, off16, cyc16, clearTracking);
                                         },
                                         voiceToken);
    }
    void kernelBitPerfectNoteOn(const SidTimedEvent& ev) noexcept {
        if (ArpSID::sidResolveRenderModeFromLiveParams(renderParams_) != ArpSID::SidRuntimeRenderMode::BitPerfect) return;
        ArpSID::canonicalBitPerfectNoteOn(bpe_(), ev);
    }
    void kernelBitPerfectNoteOff(const SidTimedEvent& ev) noexcept {
        if (ArpSID::sidResolveRenderModeFromLiveParams(renderParams_) != ArpSID::SidRuntimeRenderMode::BitPerfect) return;
        ArpSID::canonicalBitPerfectNoteOff(bpe_(), ev);
    }
    void kernelApplyPitchBend(const SidTimedEvent& ev) noexcept { ArpSID::runtimeKernelDispatchPitchBend(*this, ev); }
    void kernelApplyPolyPressure(const SidTimedEvent& ev) noexcept { ArpSID::runtimeKernelDispatchPolyPressure(*this, ev); }
    void kernelApplyChannelPressure(const SidTimedEvent& ev) noexcept { ArpSID::runtimeKernelDispatchChannelPressure(*this, ev); }
    void kernelApplyMidiCC(const SidTimedEvent& ev) noexcept { ArpSID::runtimeKernelDispatchMidiCC(*this, ev); }
    void kernelApplyVariantProfile(const SidVariantProfile& profile) noexcept {
        // Canonical runtime state was already mutated by SidRuntimeModel before
        // concrete-engine dispatch. This sink performs projection only.
        // Presentation mirrors only; runtime truth is the variant profile above.
        renderParams_[(size_t)kParamSidModel] = (profile.family == SidFamily::MOS8580) ? 1.0f : 0.0f;
        renderParams_[(size_t)kParamSidClockSystem] = ArpSID::sidVariantUsesNtscClock(profile) ? 1.0f : 0.0f;
        params_[(size_t)kParamSidModel].store(renderParams_[(size_t)kParamSidModel], std::memory_order_relaxed);
        params_[(size_t)kParamSidClockSystem].store(renderParams_[(size_t)kParamSidClockSystem], std::memory_order_relaxed);
        dirty_[(size_t)kParamSidModel].store(false, std::memory_order_relaxed);
        dirty_[(size_t)kParamSidClockSystem].store(false, std::memory_order_relaxed);
        digiD418_.updateTimingPreserveVoices(sampleRate_, ArpSID::sidVariantClockHz(profile));
        if (runtimeExecutionOwner_) runtimeExecutionOwner_->projectStateToBackends(false);
    }
    void kernelApplyProgramChange(uint8_t program) noexcept {
        // Incoming Program Change is GM/song metadata. It must not select or overwrite
        // the ArpSID factory patch. Explicit patch selection uses PresentPreset or
        // kParamBankSlot.
        (void)program;
    }
    void runtimeHandleRenderModeTransition(ArpSID::SidRuntimeRenderMode oldMode, ArpSID::SidRuntimeRenderMode newMode) noexcept {
        ArpSID::runtimeRenderHostHandleModeTransition(*this, oldMode, newMode);
        resetRenderModeTransitionRuntime_();
        // Re-apply Filter Drive after the mode bit changes. The posterior stores
        // dB, not the normalized parameter; refresh the DrSID 4.5 dB cap now.
        runtimePolicySetFilterDrive(renderParams_[(size_t)kParamFilterDrive]);
    }
    void runtimeDispatchPitchBend14(int channel, int raw14) noexcept { SidTimedEvent ev{}; ev.type = SidTimedEventType::PitchBend; ev.channel = static_cast<uint8_t>(std::clamp(channel, 0, 15)); ev.data14 = static_cast<uint16_t>(std::clamp(raw14, 0, 16383)); ev.value = std::clamp((static_cast<float>(ev.data14) - 8192.0f) / 8192.0f, -1.0f, 1.0f); ev.value_f32 = ev.value; runtimeDispatchPitchBendEvent(ev); }
    void runtimeDispatchPitchBendEvent(const SidTimedEvent& ev) noexcept { handlePitchBend(static_cast<uint8_t>(std::clamp<int>(ev.channel, 0, 15)), static_cast<int16_t>(std::clamp(ArpSID::canonicalPitchBend14FromEvent(ev) - 8192, -8192, 8191))); }
    void runtimeDispatchPolyPressure(int channel, int pitch, int value7) noexcept { SidTimedEvent ev{}; ev.type = SidTimedEventType::PolyPressure; ev.channel = static_cast<uint8_t>(std::clamp(channel, 0, 15)); ev.pitch = static_cast<int16_t>(std::clamp(pitch, 0, 127)); ev.value = std::clamp((float)value7 / 127.0f, 0.0f, 1.0f); runtimeDispatchPolyPressureEvent(ev); }
    void runtimeDispatchPolyPressureEvent(const SidTimedEvent& ev) noexcept {
        const uint8_t ch = static_cast<uint8_t>(std::clamp<int>(ev.channel, 0, 15));
        const uint8_t note = static_cast<uint8_t>(std::clamp<int>(ev.pitch, 0, 127));
        const uint8_t p7 = static_cast<uint8_t>(ArpSID::canonicalMidi7FromEventValue(ev));
        handlePolyPressure(ch, note, p7);
        ArpSID::runtimeHandleRenderedPolyPressureIdentity(*this, ch, note, ev.noteId, p7);
    }
    void runtimeDispatchChannelPressure(int channel, int value7) noexcept { SidTimedEvent ev{}; ev.type = SidTimedEventType::ChannelPressure; ev.channel = static_cast<uint8_t>(std::clamp(channel, 0, 15)); ev.value = std::clamp((float)value7 / 127.0f, 0.0f, 1.0f); runtimeDispatchChannelPressureEvent(ev); }
    void runtimeDispatchChannelPressureEvent(const SidTimedEvent& ev) noexcept { handleAftertouch(static_cast<uint8_t>(std::clamp<int>(ev.channel, 0, 15)), static_cast<uint8_t>(ArpSID::canonicalMidi7FromEventValue(ev))); }
    void runtimeDispatchMidiCC(int channel, int cc, int value7) noexcept { handleCC(static_cast<uint8_t>(channel), static_cast<uint8_t>(cc), static_cast<uint8_t>(value7)); }
private:
    //──────────────────────────────────────────────────────
    // Event dispatch (called per event in processBlock)
    //──────────────────────────────────────────────────────
    void dispatchCanonicalTimedEvent_(const SidTimedEvent& ev) noexcept {
        ArpSID::dispatchCanonicalTimedEventToTarget(runtimeModel_, ev, *this);
    }

    void dispatchEvent_(const TimedEvent& ev) noexcept {
        SidTimedEvent canonical{};
        const double sidClockHz = currentSidClockHz_();
        ArpSID::assignBestEffortIntraSampleTiming(canonical,
                                                  ev.sampleOffset,
                                                  0u,
                                                  ev.subphase,
                                                  sampleRate_,
                                                  sidClockHz,
                                                  ev.cycleOffset == kSidUnresolvedCycleOffset ? kSidUnresolvedCycleOffset : ev.cycleOffset);
        canonical.channel = ev.channel;
        canonical.pitch = ev.pitch;
        canonical.value = ev.value;
        canonical.data14 = ev.data14;
        canonical.ccNum = ev.ccNum;
        canonical.noteId = ev.noteId;
        canonical.arrival_order = ev.rawOrder;
        canonical.target = ev.target;
        canonical.value_u32 = ev.value_u32;
        canonical.value_f32 = ev.value_f32;
        canonical.type = static_cast<SidTimedEventType>(ev.kind);
        canonical.sanitize(kMaxFramesPerBlock);
        dispatchCanonicalTimedEvent_(canonical);
    }

    // ── DIGI auth mode resolver ─────────────────────────────────────────────
    // Determines which DIGI engine to use. Release builds expose only
    // AUTH C64-bus D418 and FAST private D418. LegacyFloatLayer is accepted
    // only in explicit ARPSID_ENABLE_LEGACY_FLOAT_DIGI debug builds. AUTH/FAST
    // schedule PHI2-synchronous $D418 writes through digiD418_ into a private
    // bridge, never into the main PSID/RSID C64 SID bridge.
    inline ArpSID::DigiAuthMode resolveDigiAuthMode_() const noexcept {
        switch (digiD418RuntimeMode()) {
            case 1u: return ArpSID::DigiAuthMode::FastStandaloneD418Layer;
#if defined(ARPSID_ENABLE_LEGACY_FLOAT_DIGI) && ARPSID_ENABLE_LEGACY_FLOAT_DIGI
            case 2u: return ArpSID::DigiAuthMode::LegacyFloatLayer;
#endif
            case 0u:
            default: return ArpSID::DigiAuthMode::StandaloneD418Layer;
        }
    }

    inline ArpSID::DigiD418Config resolveDigiD418Config_() const noexcept {
        ArpSID::DigiD418Config cfg{};
        cfg.authMode = resolveDigiAuthMode_();
        cfg.clockSource = ArpSID::DigiClockSource::Phi2FixedRate;
        cfg.digiRateHz = digiD418RuntimeRateHz();
        cfg.respectIoBank = (cfg.authMode == ArpSID::DigiAuthMode::StandaloneD418Layer);
        cfg.driveOpenBus = (cfg.authMode == ArpSID::DigiAuthMode::StandaloneD418Layer);
        cfg.preserveD418HighNibble = true;
        cfg.useExternalD418HighNibble = true;
        cfg.externalD418HighNibble = static_cast<std::uint8_t>(c64SidBridge_.regs[0x18u] & 0xF0u);
        cfg.countCollisions = true;
        return cfg;
    }



    bool digiMidiNoteToSlot_(std::uint8_t note, std::uint8_t& slotOut) const noexcept {
        return ArpSID::DigiD418StreamEngine::midiNoteMapsToSlot(
            static_cast<std::uint8_t>(note & 0x7Fu),
            digiMidiRootNote(),
            slotOut);
    }

    bool digiMidiChannelAccepted_(std::uint8_t channel) const noexcept {
        const std::uint8_t filter = digiMidiChannelFilter();
        return filter > 15u || static_cast<std::uint8_t>(channel & 0x0Fu) == filter;
    }

    void maybeTriggerDigiFromMidi_(const SidTimedEvent& ev, int numFrames) noexcept {
        if (ev.type != SidTimedEventType::MidiNoteOn) return;
        const std::uint8_t note = static_cast<std::uint8_t>(ev.pitch & 0x7Fu);
        const std::uint8_t ch = static_cast<std::uint8_t>(ev.channel & 0x0Fu);
        const std::uint8_t vel = ArpSID::canonicalMidi7FromEventValue(ev);
        if (vel == 0u) return;
        if (!digiMidiChannelAccepted_(ch)) return;
        std::uint8_t slot = 0u;
        if (!digiMidiNoteToSlot_(note, slot)) return;
        const int sampleOffset = std::clamp<int>(static_cast<int>(ev.sample_offset), 0, std::max(0, numFrames - 1));
        const auto authMode = resolveDigiAuthMode_();
        if (authMode == ArpSID::DigiAuthMode::LegacyFloatLayer) {
            const std::uint32_t before = digiSampler_.telemetry().triggerCount;
            digiSampler_.triggerSlotAt(slot, vel, guiRealtimeDigiSampleBankRender_, guiRealtimeProjectionRender_.digi, sampleOffset);
            const std::uint32_t after = digiSampler_.telemetry().triggerCount;
            if (after != before) {
                telemetryDigiMidiTriggerCount_.fetch_add(1u, std::memory_order_relaxed);
                telemetryDigiLastMidiNote_.store(note, std::memory_order_relaxed);
                telemetryDigiLastMidiChannel_.store(ch, std::memory_order_relaxed);
            } else {
                telemetryDigiMidiIgnoredCount_.fetch_add(1u, std::memory_order_relaxed);
            }
            return;
        }
        digiD418_.setConfig(resolveDigiD418Config_());
        const bool accepted = digiD418_.triggerMidiNoteAt(ch, note, vel,
                                                         guiRealtimeDigiSampleBankRender_,
                                                         guiRealtimeProjectionRender_.digi,
                                                         sampleOffset,
                                                         digiMidiRootNote(),
                                                         (digiMidiChannelFilter() <= 15u) ? static_cast<int>(digiMidiChannelFilter()) : -1);
        if (!accepted) {
            // triggerMidiNoteAt() owns the stream-engine ignored counter; this
            // fallback keeps the kernel-side legacy/adapter counters coherent
            // until the next render-layer telemetry publish.
            telemetryDigiMidiIgnoredCount_.fetch_add(1u, std::memory_order_relaxed);
        }
    }

    void consumeGuiDigiPadTriggers_(ArpSID::DigiAuthMode authMode, int sampleOffset) noexcept {
        std::uint32_t mask = digiGuiPadTriggerMask_.exchange(0u, std::memory_order_acq_rel);
        mask &= ((1u << ArpSID::GUI::kDigiActiveSlotCount) - 1u);
        if (mask == 0u) return;
        const int safeOffset = std::max(0, sampleOffset);
        for (std::uint8_t slot = 0; slot < ArpSID::GUI::kDigiActiveSlotCount; ++slot) {
            if ((mask & (1u << slot)) == 0u) continue;
            const std::uint8_t vel = std::clamp<int>(
                digiGuiPadTriggerVelocity_[static_cast<std::size_t>(slot)].load(std::memory_order_acquire),
                1, 127);
            if (authMode == ArpSID::DigiAuthMode::LegacyFloatLayer) {
                const std::uint32_t before = digiSampler_.telemetry().triggerCount;
                digiSampler_.triggerSlotAt(slot, vel, guiRealtimeDigiSampleBankRender_, guiRealtimeProjectionRender_.digi, safeOffset);
                const std::uint32_t after = digiSampler_.telemetry().triggerCount;
                if (after != before) {
                    telemetryDigiGuiPadAcceptedCount_.fetch_add(1u, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastSlot_.store(slot, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastVelocity_.store(vel, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastAccepted_.store(1u, std::memory_order_relaxed);
                    telemetryDigiLastMidiNote_.store(static_cast<std::uint8_t>(digiMidiRootNote() + slot), std::memory_order_relaxed);
                    telemetryDigiLastMidiChannel_.store(255u, std::memory_order_relaxed);
                } else {
                    telemetryDigiGuiPadIgnoredCount_.fetch_add(1u, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastSlot_.store(slot, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastVelocity_.store(vel, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastAccepted_.store(0u, std::memory_order_relaxed);
                }
            } else {
                digiD418_.setConfig(resolveDigiD418Config_());
                const std::uint32_t before = digiD418_.telemetry().triggerCount;
                digiD418_.triggerSlotAt(slot, vel,
                                        guiRealtimeDigiSampleBankRender_,
                                        guiRealtimeProjectionRender_.digi,
                                        safeOffset);
                const std::uint32_t after = digiD418_.telemetry().triggerCount;
                if (after != before) {
                    telemetryDigiGuiPadAcceptedCount_.fetch_add(1u, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastSlot_.store(slot, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastVelocity_.store(vel, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastAccepted_.store(1u, std::memory_order_relaxed);
                } else {
                    telemetryDigiGuiPadIgnoredCount_.fetch_add(1u, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastSlot_.store(slot, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastVelocity_.store(vel, std::memory_order_relaxed);
                    telemetryDigiGuiPadLastAccepted_.store(0u, std::memory_order_relaxed);
                }
            }
        }
    }

    inline void clearDigiLayerTelemetryAtomics_(std::uint8_t mode) noexcept {
        telemetryDigiActiveSlots_.store(0u, std::memory_order_relaxed);
        telemetryDigiConfiguredFactorySlots_.store(0u, std::memory_order_relaxed);
        telemetryDigiConfiguredUserImportSlots_.store(0u, std::memory_order_relaxed);
        telemetryDigiPlayingVoices_.store(0u, std::memory_order_relaxed);
        telemetryDigiPeakVoices_.store(0u, std::memory_order_relaxed);
        telemetryDigiStepIndex_.store(0u, std::memory_order_relaxed);
        telemetryDigiLastSlot_.store(255u, std::memory_order_relaxed);
        telemetryDigiLastFactorySlot_.store(0u, std::memory_order_relaxed);
        telemetryDigiTriggerCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiUnavailableUserImports_.store(0u, std::memory_order_relaxed);
        telemetryDigiOutputPeak_.store(0.0f, std::memory_order_relaxed);
        {
            auto& scope = telemetryDigiScopeTriple_.writeSlot();
            scope = DigiScopeSnapshot{};
            scope.frameId = blockIndex_;
            telemetryDigiScopeTriple_.publish();
        }
        clearDigiD418TelemetryAtomics_(mode);
    }

    inline void clearDigiD418TelemetryAtomics_(std::uint8_t mode) noexcept {
        telemetryDigiAuthMode_.store((mode <= 2u) ? mode : 0u, std::memory_order_relaxed);
        telemetryDigiGuiPadAcceptedCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiGuiPadIgnoredCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiGuiPadLastSlot_.store(255u, std::memory_order_relaxed);
        telemetryDigiGuiPadLastVelocity_.store(0u, std::memory_order_relaxed);
        telemetryDigiGuiPadLastAccepted_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418WriteCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418WritesThisBlock_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418SidAcceptedWriteCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418SidAcceptedWritesThisBlock_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418WritesBlockedByIo_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418WriteQueueOverflow_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418CollisionCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418OpenBusDriveCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418TimelineDiscontinuityResetCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418ForensicWritePos_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418LastHostFrame_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418LastPhi2Low_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418LastNibble_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418LastOldD418_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418LastD418_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418LastOpenBus_.store(0xFFu, std::memory_order_relaxed);
        telemetryDigiD418LastIoVisible_.store(0u, std::memory_order_relaxed);
        telemetryDigiD418LastSidAccepted_.store(0u, std::memory_order_relaxed);
        telemetryDigiMidiTriggerCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiMidiIgnoredCount_.store(0u, std::memory_order_relaxed);
        telemetryDigiLastMidiNote_.store(255u, std::memory_order_relaxed);
        telemetryDigiLastMidiChannel_.store(255u, std::memory_order_relaxed);
    }

    inline void consumeDigiD418RuntimePolicyChange_() noexcept {
        const std::uint32_t gen = digiD418RuntimePolicyGeneration_.load(std::memory_order_acquire);
        if (gen == digiD418RuntimePolicyGenerationRender_) return;
        digiD418RuntimePolicyGenerationRender_ = gen;
        // Policy changes alter the meaning of already armed voices and the
        // PHI2 write interval. Consume the change on the render thread and
        // clear both engines so AUTH/FAST/debug-legacy cannot resume stale samples,
        // stale phase, open-bus state, or old private $D418 DAC memory.
        digiD418_.reset();
        digiSampler_.allNotesOff();
        digiD418Sid_.resetD418VolumeDacEmulationState();
        digiOpenBus_.powerOn();
        clearDigiD418TelemetryAtomics_(digiD418RuntimeMode());
    }

    //──────────────────────────────────────────────────────
    // Render a single audio slice into output buffers at given offset
    //──────────────────────────────────────────────────────
    void renderSlice_(float** outputs, int offset, int frames) noexcept {
        if (frames <= 0) return;
        const int safeFrames = std::min(frames, kMaxFramesPerBlock);
        if (!outputs || !outputs[0]) return;
        float* writeR = outputs[1] ? outputs[1] : outputs[0];
        const bool sharedOutputBus = (writeR == outputs[0]);
        if (safeFrames == 1) {
            float fracL = 0.0f;
            float fracR = 0.0f;
            // v902: the fractional finalizer is the single consumer of already-dispatched
            // sub-cycle SID spans. It must run for mono/shared output buses too; otherwise
            // mono AU layouts can fall through to canonical rendering after the dispatcher
            // has already advanced the backend, causing a one-sample double-advance and
            // stereo/mono timing divergence. Finalize into local stereo and fold to mono.
            if (tryRenderFractionalSingleSample_(offset, &fracL, &fracR)) {
                if (sharedOutputBus) {
                    outputs[0][offset] = ArpSID_sanitizeFloat(0.5f * (fracL + fracR));
                } else {
                    outputs[0][offset] = fracL;
                    writeR[offset] = fracR;
                }
                return;
            }
        }

        std::memset(sliceScratchL_, 0, (size_t)safeFrames * sizeof(float));
        std::memset(sliceScratchR_, 0, (size_t)safeFrames * sizeof(float));
        runtimeCurrentRenderOffset_ = offset;
        ArpSID::renderCanonicalAudioForTarget(*this, sliceScratchL_, sliceScratchR_, safeFrames);
        runtimeCurrentRenderOffset_ = 0;

        for (int i = 0; i < safeFrames; ++i) {
            if (sharedOutputBus) {
                outputs[0][offset + i] = 0.5f * (sliceScratchL_[i] + sliceScratchR_[i]);
            } else {
                outputs[0][offset + i] = sliceScratchL_[i];
                writeR[offset + i] = sliceScratchR_[i];
            }
        }
    }

    bool refreshGuiRealtimeProjection_(std::uint8_t step) noexcept {
        if (ArpSID::GUI::DigiSampleBankBlob* bank = guiRealtimeDigiSampleBankMailbox_.tryConsume()) {
            guiRealtimeDigiSampleBankRender_ = *bank;
            ArpSID::GUI::sanitizeDigiSampleBankBlob(guiRealtimeDigiSampleBankRender_);
        }
        if (GuiRealtimeModelSnapshot_* next = guiRealtimeMailbox_.tryConsume()) {
            guiRealtimeRender_ = *next;
            ++guiRealtimeRenderVersion_;
            syncMixFxProcessors_();
            guiRealtimeProjectionRender_ =
                ArpSID::GUI::projectGuiRealtime(guiRealtimeRender_.mix, guiRealtimeRender_.kit, guiRealtimeRender_.digi, step);
            return true;
        }
        guiRealtimeProjectionRender_ =
            ArpSID::GUI::projectGuiRealtime(guiRealtimeRender_.mix, guiRealtimeRender_.kit, guiRealtimeRender_.digi, step);
        return true;
    }



    void renderDigiSamplerLayer_(float** outputs,
                                 int outputChannelCount,
                                 int numFrames,
                                 SidRuntimeRenderMode mode,
                                 bool seqEnabled,
                                 const SequencerEngine::StepBoundary* stepBoundaries,
                                 int stepBoundaryCount) noexcept {
        if (numFrames <= 0 || numFrames > kMaxFramesPerBlock) {
            digiSampler_.allNotesOff();
            digiD418_.reset();
            digiD418Sid_.resetD418VolumeDacEmulationState();
            clearDigiLayerTelemetryAtomics_(digiD418RuntimeMode());
            return;
        }
        const std::uint8_t step = static_cast<std::uint8_t>(
            std::clamp(runtimeModel_.seqStep(), 0, static_cast<int>(ArpSID::GUI::kDigiStepCount - 1u)));
        refreshGuiRealtimeProjection_(step);
        consumeDigiD418RuntimePolicyChange_();
        // C64SidPlayer is a flavor (enforceComponentFlavorPolicy_ forces
        // kParamDrSidEnable=0 / kParamSynthModeEnable=0), so mode is always
        // BitPerfect during C64 playback. Block DIGI layering by checking the
        // component flavor, not a render mode value — the C64Psid enum slot is
        // intentionally unassigned and never returned by the resolver.
        const bool isC64SidPlayerFlavor =
            (componentFlavor_ == ArpSID::ComponentFlavor::C64SidPlayer);
        const bool canLayer =
            outputs && outputs[0] && numFrames > 0 && !isC64SidPlayerFlavor;
        const bool canAudibleAuthDigi =
            outputs && outputs[0] && numFrames > 0 && !isC64SidPlayerFlavor;
        const bool transportAllowsStepTrigger =
            seqEnabled && (runtimeHostSurface_().transportPlaying || !runtimeModel_.followHostTempoSeq());
        if (isC64SidPlayerFlavor) {
            // C64SidPlayer must remain a pure PSID/RSID authority. Do not even
            // let already-armed DIGI voices drain in the background: the bridge
            // is restored later, but processing them here would still advance
            // private DIGI state, HUD counters and forensic write positions.
            // Kill both DIGI engines and publish an explicit inactive telemetry
            // snapshot for this block.
            digiSampler_.allNotesOff();
            digiD418_.reset();
            digiD418Sid_.resetD418VolumeDacEmulationState();
            digiOpenBus_.powerOn();
            clearDigiLayerTelemetryAtomics_(digiD418RuntimeMode());
            return;
        }
        const bool allowTriggers = canLayer && transportAllowsStepTrigger;
        const bool allowAuthBusTriggers = canAudibleAuthDigi && transportAllowsStepTrigger;
        const int safeBoundaryCount = (stepBoundaries && stepBoundaryCount > 0)
            ? std::min(stepBoundaryCount, kMaxSeqStepBoundariesPerBlock_)
            : 0;
        // Choose between legacy float DIGI sampler and authentic D418 bus‑write engine.
        ArpSID::DigiAuthMode authMode = resolveDigiAuthMode_();
        telemetryDigiAuthMode_.store(static_cast<std::uint8_t>(authMode), std::memory_order_relaxed);
            consumeGuiDigiPadTriggers_(authMode, 0);
        if (authMode == ArpSID::DigiAuthMode::LegacyFloatLayer) {
            if (allowTriggers) {
                for (int bi = 0; bi < safeBoundaryCount; ++bi) {
                    const int boundaryStep = std::clamp(stepBoundaries[bi].stepIndex,
                                                        0,
                                                        static_cast<int>(ArpSID::GUI::kDigiStepCount - 1u));
                    const int boundaryOffset = std::clamp(stepBoundaries[bi].sampleOffset, 0, numFrames - 1);
                    refreshGuiRealtimeProjection_(static_cast<std::uint8_t>(boundaryStep));
                    for (std::uint8_t slot = 0; slot < ArpSID::GUI::kDigiActiveSlotCount; ++slot) {
                        const auto& projected = guiRealtimeProjectionRender_.digi.slots[slot];
                        if (!projected.activeAtStep || projected.stepVelocity == 0u) continue;
                        digiSampler_.triggerSlotAt(slot,
                                                  projected.stepVelocity,
                                                  guiRealtimeDigiSampleBankRender_,
                                                  guiRealtimeProjectionRender_.digi,
                                                  boundaryOffset);
                    }
                }
                refreshGuiRealtimeProjection_(step);
            }
            digiSampler_.process(guiRealtimeProjectionRender_.digi,
                                 guiRealtimeDigiSampleBankRender_,
                                 canLayer ? outputs[0] : nullptr,
                                 (canLayer && outputChannelCount > 1 && outputs[1]) ? outputs[1] : nullptr,
                                 numFrames,
                                 seqEnabled || digiSampler_.isActive(),
                                 false,
                                 0);
            // Legacy mode is explicitly direct-float and must not expose stale
            // $D418 bus state from a previous AUTH/FAST block. Keep the legacy
            // sampler telemetry live, but make all current D418 observability
            // fields read as inactive/no-event. Policy-change already clears
            // cumulative counters; this protects steady-state legacy blocks too.
            telemetryDigiD418WritesThisBlock_.store(0u, std::memory_order_relaxed);
            telemetryDigiD418SidAcceptedWritesThisBlock_.store(0u, std::memory_order_relaxed);
            telemetryDigiD418LastSidAccepted_.store(0u, std::memory_order_relaxed);
            telemetryDigiD418LastIoVisible_.store(0u, std::memory_order_relaxed);
            publishDigiSamplerTelemetry_();
        } else {
            // The authentic engine writes to the SID bridge; it does not mix
            // audio directly. Determine whether triggers are permitted and
            // schedule nibble writes accordingly. The low nibble of each
            // write carries the 4‑bit sample value; the high nibble comes
            // from the SID shadow in the bridge. When IO is not visible
            // ($D000 banked out) the writes are counted but not emitted.
            const std::uint64_t blockStartDigiPhi2 = digiPhi2Counter_;
            // single owner for the DIGI PHI2 block boundary.
            // The kernel carries fractional PAL-PHI2 remainder across audio
            // blocks; the stream engine must receive the exact same block end
            // or it can mis-diagnose normal one-cycle remainder carry as a
            // timeline discontinuity and reset pending $D418 scheduling.
            const double safeDigiAdvanceSampleRate = std::max(1.0, ArpSIDSanitizeHostSampleRate(sampleRate_));
            const double phi2PerFrame = currentSidClockHz_() / safeDigiAdvanceSampleRate;
            const double exactPhi2Advance = static_cast<double>(numFrames) * phi2PerFrame + digiPhi2Remainder_;
            const double wholePhi2AdvanceD = std::floor(std::max(0.0, exactPhi2Advance));
            const auto wholePhi2Advance = static_cast<std::uint64_t>(wholePhi2AdvanceD);
            const std::uint64_t blockEndDigiPhi2 = blockStartDigiPhi2 + wholePhi2Advance;
            digiD418_.setConfig(resolveDigiD418Config_());

            // hard-isolate standalone/auth DIGI from the main C64 SID
            // authority. The old implementation temporarily wrote DIGI events
            // into c64SidBridge_ and then restored only count/overflow/$D418;
            // that left regsByChip, last*, writeCount, mirrorSink/live-engine
            // side effects and stale queue tails vulnerable to contamination.
            // This private bridge has no sink/engine and is reset every block.
            digiD418Bridge_.reset();
            digiD418Bridge_.engine = nullptr;
            digiD418Bridge_.mirrorSink = nullptr;
            digiD418Bridge_.deferEngineWrites = true;
            const std::uint8_t inheritedD418 = c64SidBridge_.regs[0x18u];
            digiD418Bridge_.regs[0x18u] = inheritedD418;
            digiD418Bridge_.regsByChip[0][0x18u] = inheritedD418;

            if (allowAuthBusTriggers) {
                for (int bi = 0; bi < safeBoundaryCount; ++bi) {
                    const int boundaryStep = std::clamp(stepBoundaries[bi].stepIndex,
                                                        0,
                                                        static_cast<int>(ArpSID::GUI::kDigiStepCount - 1u));
                    const int boundaryOffset = std::clamp(stepBoundaries[bi].sampleOffset, 0, numFrames - 1);
                    refreshGuiRealtimeProjection_(static_cast<std::uint8_t>(boundaryStep));
                    for (std::uint8_t slot = 0; slot < ArpSID::GUI::kDigiActiveSlotCount; ++slot) {
                        const auto& projected = guiRealtimeProjectionRender_.digi.slots[slot];
                        if (!projected.activeAtStep || projected.stepVelocity == 0u) continue;
                        digiD418_.triggerSlotAt(slot,
                                               projected.stepVelocity,
                                               guiRealtimeDigiSampleBankRender_,
                                               guiRealtimeProjectionRender_.digi,
                                               boundaryOffset);
                    }
                }
                refreshGuiRealtimeProjection_(step);
            }
            digiD418_.processToSidBridge(guiRealtimeProjectionRender_.digi,
                                        guiRealtimeDigiSampleBankRender_,
                                        digiD418Bridge_,
                                        digiOpenBus_,
                                        blockStartDigiPhi2,
                                        numFrames,
                                        seqEnabled || digiD418_.isActive(),
                                        false,
                                        0,
                                        telemetryC64ProcessorPort_.load(std::memory_order_relaxed),
                                        blockEndDigiPhi2);
            // Render the just-created private $D418 writes through a dedicated
            // SID register engine so the DIGI layer is audible in the same host
            // block without advancing or corrupting the main PSID/RSID SID
            // authority. This is intentionally private SID-DAC render semantics:
            // nibble -> $D418-style write -> isolated SID output. AUTH mode
            // still gets its C64 bus/open-bus decision before events enter
            // this private audio-only render queue.
            const std::uint32_t bridgeCountAfterDigi = std::min<std::uint32_t>(
                digiD418Bridge_.timedWriteCount,
                static_cast<std::uint32_t>(ArpSID::C64::C64SidBridgeState::kMaxTimedWrites));
            const bool emittedD418ThisBlock = (bridgeCountAfterDigi > 0u);
            if (canAudibleAuthDigi && outputs && outputs[0] && emittedD418ThisBlock) {
                std::memset(digiD418ScratchL_, 0, static_cast<std::size_t>(numFrames) * sizeof(float));
                std::memset(digiD418ScratchR_, 0, static_cast<std::size_t>(numFrames) * sizeof(float));
                digiD418Sid_.writeSystemByte(sreg_().currentSystemByte());
                std::uint32_t wi = 0u;
                const double safeDigiSampleRate = std::max(1.0, ArpSIDSanitizeHostSampleRate(sampleRate_));
                const double phi2PerFrameForDigi = currentSidClockHz_() / safeDigiSampleRate;
                for (int frame = 0; frame < numFrames; ++frame) {
                    while (wi < bridgeCountAfterDigi) {
                        const auto& w = digiD418Bridge_.timedWrites[wi];
                        // Map the scheduled PHI2 write back into the host sample quantum
                        // that owns it. The old frame-start comparison dropped writes
                        // that landed after the final frame start but still inside this
                        // render block; those events had already been consumed from the
                        // private bridge and were therefore inaudible. This mirrors the
                        // stream-engine forensic host-frame mapping and guarantees that
                        // every accepted in-quantum $D418 write is applied to exactly one
                        // private SID sample before the private queue is discarded.
                        const double relPhi2 = (w.phi2Cycle >= blockStartDigiPhi2)
                            ? static_cast<double>(w.phi2Cycle - blockStartDigiPhi2)
                            : 0.0;
                        int writeFrame = 0;
                        if (std::isfinite(relPhi2) && phi2PerFrameForDigi > 0.0) {
                            writeFrame = static_cast<int>(std::floor(relPhi2 / phi2PerFrameForDigi));
                        }
                        writeFrame = std::clamp(writeFrame, 0, numFrames - 1);
                        if (writeFrame > frame) break;
                        if (w.chip == 0u && w.reg == 0x18u) {
                            digiD418Sid_.write(0x18u, w.value);
                        }
                        ++wi;
                    }
                    digiD418Sid_.renderBlock(&digiD418ScratchL_[frame], &digiD418ScratchR_[frame], 1);
                }
                float* outL = outputs[0];
                float* outR = (outputChannelCount > 1 && outputs[1]) ? outputs[1] : outputs[0];
                for (int frame = 0; frame < numFrames; ++frame) {
                    const float lBase = ArpSIDDigiSanitizeMixSample_(outL[frame]);
                    const float lDigi = ArpSIDDigiSanitizeMixSample_(digiD418ScratchL_[frame]);
                    outL[frame] = ArpSIDDigiSanitizeMixSample_(lBase + lDigi);
                    if (outR != outL) {
                        const float rBase = ArpSIDDigiSanitizeMixSample_(outR[frame]);
                        const float rDigi = ArpSIDDigiSanitizeMixSample_(digiD418ScratchR_[frame]);
                        outR[frame] = ArpSIDDigiSanitizeMixSample_(rBase + rDigi);
                    }
                }
            } else {
                // Hardware-authentic hold policy: if a DIGI voice is still
                // active but this host block contains no new $D418 write, do
                // not clear the private SID volume-DAC memory. A real SID
                // holds the last volume code until another write or reset.
                // We only clear on true idle, or when writes happened while no
                // audible buffer was available and the private renderer could
                // not consume the event stream coherently in this block.
                if (emittedD418ThisBlock || !digiD418_.isActive()) {
                    digiD418Sid_.resetD418VolumeDacEmulationState();
                }
            }
            // Advance the PHI2 counter for the next block using the same
            // exact advance that was supplied to DigiD418StreamEngine above.
            digiPhi2Remainder_ = exactPhi2Advance - wholePhi2AdvanceD;
            if (!std::isfinite(digiPhi2Remainder_) || digiPhi2Remainder_ < 0.0 || digiPhi2Remainder_ >= 1.0) {
                digiPhi2Remainder_ = 0.0;
            }
            digiPhi2Counter_ += wholePhi2Advance;
            // Pull the telemetry from the engine and publish into atomics for UI/diagnostic access.
            const auto& dt = digiD418_.telemetry();
            telemetryDigiActiveSlots_.store(dt.activeSlotCount, std::memory_order_relaxed);
            telemetryDigiConfiguredFactorySlots_.store(dt.configuredFactorySlotCount, std::memory_order_relaxed);
            telemetryDigiConfiguredUserImportSlots_.store(dt.configuredUserImportSlotCount, std::memory_order_relaxed);
            telemetryDigiPlayingVoices_.store(dt.playingVoiceCount, std::memory_order_relaxed);
            telemetryDigiPeakVoices_.store(dt.peakVoiceCount, std::memory_order_relaxed);
            telemetryDigiStepIndex_.store(dt.stepIndex, std::memory_order_relaxed);
            telemetryDigiLastSlot_.store(dt.lastTriggeredSlot, std::memory_order_relaxed);
            telemetryDigiLastFactorySlot_.store(dt.lastTriggeredFactorySlot, std::memory_order_relaxed);
            telemetryDigiTriggerCount_.store(dt.triggerCount, std::memory_order_relaxed);
            telemetryDigiMidiTriggerCount_.store(dt.midiTriggerCount, std::memory_order_relaxed);
            telemetryDigiMidiIgnoredCount_.store(dt.midiIgnoredCount, std::memory_order_relaxed);
            telemetryDigiLastMidiNote_.store(dt.lastMidiNote, std::memory_order_relaxed);
            telemetryDigiLastMidiChannel_.store(dt.lastMidiChannel, std::memory_order_relaxed);
            telemetryDigiUnavailableUserImports_.store(dt.unavailableUserImportCount, std::memory_order_relaxed);
            telemetryDigiD418WriteCount_.store(dt.d418WriteCount, std::memory_order_relaxed);
            telemetryDigiD418WritesThisBlock_.store(dt.d418WritesThisBlock, std::memory_order_relaxed);
            telemetryDigiD418SidAcceptedWriteCount_.store(dt.d418SidAcceptedWriteCount, std::memory_order_relaxed);
            telemetryDigiD418SidAcceptedWritesThisBlock_.store(dt.d418SidAcceptedWritesThisBlock, std::memory_order_relaxed);
            telemetryDigiD418WritesBlockedByIo_.store(dt.d418WritesBlockedByIoBank, std::memory_order_relaxed);
            telemetryDigiD418WriteQueueOverflow_.store(dt.d418WriteQueueOverflow, std::memory_order_relaxed);
            telemetryDigiD418CollisionCount_.store(dt.d418CollisionCount, std::memory_order_relaxed);
            telemetryDigiD418OpenBusDriveCount_.store(dt.openBusDriveCount, std::memory_order_relaxed);
            telemetryDigiD418TimelineDiscontinuityResetCount_.store(dt.timelineDiscontinuityResetCount, std::memory_order_relaxed);
            telemetryDigiD418ForensicWritePos_.store(digiD418_.forensicWritePos(), std::memory_order_relaxed);
            if (dt.d418WriteCount > 0u) {
                const auto& lastEvent = digiD418_.forensicEvent(digiD418_.forensicWritePos() - 1u);
                telemetryDigiD418LastHostFrame_.store(lastEvent.hostFrame, std::memory_order_relaxed);
                telemetryDigiD418LastPhi2Low_.store(static_cast<std::uint32_t>(lastEvent.phi2Cycle & 0xFFFFFFFFu), std::memory_order_relaxed);
                telemetryDigiD418LastIoVisible_.store(lastEvent.ioVisible ? 1u : 0u, std::memory_order_relaxed);
                telemetryDigiD418LastSidAccepted_.store(lastEvent.sidAccepted ? 1u : 0u, std::memory_order_relaxed);
            }
            telemetryDigiAuthMode_.store(static_cast<std::uint8_t>(authMode), std::memory_order_relaxed);
            telemetryDigiD418LastNibble_.store(dt.lastNibble, std::memory_order_relaxed);
            telemetryDigiD418LastOldD418_.store(dt.lastOldD418, std::memory_order_relaxed);
            telemetryDigiD418LastD418_.store(dt.lastD418, std::memory_order_relaxed);
            telemetryDigiD418LastOpenBus_.store(dt.lastOpenBus, std::memory_order_relaxed);
            telemetryDigiOutputPeak_.store(ArpSIDSanitizeTelemetryUnitFloat(dt.scopePeak), std::memory_order_relaxed);
            // Copy the scope to the telemetry ring.
            float tmpScope[ArpSID::DigiD418StreamEngine::kScopeLen];
            digiD418_.copyScope(tmpScope, ArpSID::DigiD418StreamEngine::kScopeLen);
            auto& scope = telemetryDigiScopeTriple_.writeSlot();
            scope = DigiScopeSnapshot{};
            for (int i = 0; i < ArpSID::DigiD418StreamEngine::kScopeLen; ++i)
                scope.scope[i] = ArpSIDSanitizeTelemetryBipolarFloat(tmpScope[i]);
            scope.writePos = digiD418_.scopeWritePos();
            scope.frameId = blockIndex_;
            telemetryDigiScopeTriple_.publish();
        }
    }

    // Kit sequencer: dispatches drum notes from the compiled KIT pattern each step.
    // Only fires when the sequencer is enabled and a new step boundary is crossed.
    void tickKitSequencer_(bool seqEnabled,
                             int numFrames,
                             const SequencerEngine::StepBoundary* stepBoundaries,
                             int stepBoundaryCount) noexcept {
        if (!seqEnabled) {
            kitSequencerLastStep_ = -1;
            return;
        }
        if (componentFlavor_ != ArpSID::ComponentFlavor::DrumMachine &&
            componentFlavor_ != ArpSID::ComponentFlavor::Sid808) return;
        // A stopped follow-host transport must neither audition step zero nor
        // consume its edge. Keep it armed so reset and Stop->Play both emit the
        // first SID808 pattern step exactly once.
        if (runtimeModel_.followHostTempoSeq() && !runtimeHostSurface_().transportPlaying) {
            kitSequencerLastStep_ = -1;
            return;
        }
        const int safeBoundaryCount = (stepBoundaries && stepBoundaryCount > 0)
            ? std::min(stepBoundaryCount, kMaxSeqStepBoundariesPerBlock_)
            : 0;
        if (safeBoundaryCount <= 0) return;

        // Compile the kit grid if the render-visible source changed. The old
        // generation==0 guard compiled only once, so GUI edits/restores could leave
        // the audio thread playing a stale pattern indefinitely.
        const auto& kitBlob = guiRealtimeRender_.kit;
        const auto kitHash = [](const ArpSID::GUI::KitStateBlob& kb) noexcept -> std::uint32_t {
            std::uint32_t h = 2166136261u;
            const auto mixByte = [&h](std::uint8_t b) noexcept {
                h ^= static_cast<std::uint32_t>(b);
                h *= 16777619u;
            };
            for (std::uint8_t dc = 0u; dc < ArpSID::GUI::kKitDrumClassCount; ++dc) {
                for (std::uint8_t st = 0u; st < ArpSID::GUI::kKitStepCount; ++st) {
                    mixByte(kb.stepGrid.steps[dc][st]);
                    mixByte((kb.stepGrid.schemaVersion >= 2u) ? kb.stepGrid.stepFlags[dc][st] : 0u);
                }
            }
            mixByte(kb.panelModel.activeDrumClass);
            mixByte(kb.panelModel.activeEngineTarget);
            for (std::uint8_t dc = 0u; dc < ArpSID::GUI::kKitDrumClassCount; ++dc) {
                for (std::uint8_t et = 0u; et < ArpSID::GUI::kKitEngineTargetCount; ++et) {
                    const auto& a = kb.panelModel.drumAssignments[dc][et];
                    mixByte(a.factorySlotIndex);
                    mixByte(a.reserved[0]);
                    mixByte(a.reserved[1]);
                    mixByte(a.reserved[2]);
                }
                const auto& ac = kb.assignConfigGrid.assignConfigs[dc];
                mixByte(ac.digiSlotIndex);
                mixByte(ac.tuneShiftBias);
                mixByte(ac.startOffset);
                mixByte(ac.lengthScale);
                mixByte(ac.flags);
                mixByte(ac.engineTargetOverride);
                const auto& vc = kb.voiceConfigGrid.voiceConfigs[dc];
                mixByte(vc.waveform);
                mixByte(vc.attackDecay);
                mixByte(vc.sustainRelease);
                mixByte(vc.pulseWidthLo);
                mixByte(vc.pulseWidthHi);
                mixByte(vc.flags);
            }
            return h ? h : 1u;
        }(kitBlob);
        if (kitBlob.stepGrid.schemaVersion >= 1u &&
            (compiledKitSequencer_.generation == 0u || compiledKitSourceHash_ != kitHash)) {
            if (ArpSID::GUI::compileKitSequencer(compiledKitSequencer_,
                                                  kitBlob.stepGrid,
                                                  kitBlob.panelModel,
                                                  kitBlob.voiceConfigGrid,
                                                  kitBlob.assignConfigGrid)) {
                compiledKitSourceHash_ = kitHash;
            }
        }
        if (compiledKitSequencer_.stepCount == 0u) return;
        for (int bi = 0; bi < safeBoundaryCount; ++bi) {
            const int step = std::clamp(stepBoundaries[bi].stepIndex,
                                        0,
                                        static_cast<int>(ArpSID::GUI::kKitStepCount) - 1);
            const int offset = std::clamp(stepBoundaries[bi].sampleOffset, 0, std::max(0, numFrames - 1));
            kitSequencerLastStep_ = step;
            const auto& csStep = compiledKitSequencer_.steps[static_cast<std::size_t>(step)];
            for (std::uint8_t ei = 0u; ei < csStep.eventCount; ++ei) {
            const auto& ev = csStep.events[(std::size_t)ei];
            if (ev.velocity == 0u) continue;
            // v872 P0: KitDrumClass and SidGMDrumClass do NOT share an ordering
            // (Rim<->Cowbell differ, KitDrumClass has a 9th Crash class). Map
            // explicitly instead of casting; a raw cast swapped Rim/Cowbell and
            // silenced Crash. See kit_sid808_class_map.h.
            const auto gmClass = ArpSID::GUI::kitDrumClassToSidGM(ev.drumClass);
            if (gmClass == ArpSID::SidGMDrumClass::Unsupported) continue;
            const bool accent = (ev.flags & ArpSID::GUI::kKitStepFlagAccent) != 0u;
            std::uint8_t vel = ev.velocity;
            if (accent) vel = static_cast<std::uint8_t>(std::min(127, static_cast<int>(vel) + 27));
            const auto target = static_cast<ArpSID::GUI::KitEngineTarget>(ev.engineTarget);
            if (target == ArpSID::GUI::KitEngineTarget::SID808 ||
                componentFlavor_ == ArpSID::ComponentFlavor::Sid808) {
                if (drumEngineBridge_.activeIdentity().context != ArpSID::DrumContext::SID808_AnalogProjection)
                    // v859: slot-aware identity repair (see sid808IdentityRepairSlot_).
                    drumEngineBridge_.router().setActiveIdentityFromFactorySlot(sid808IdentityRepairSlot_());
                Sid808HitOverride ov{};
                ov.waveform = ev.voiceWaveform;
                ov.attackDecay = ev.voiceAttackDecay;
                ov.sustainRelease = ev.voiceSustainRelease;
                ov.pulseWidth = static_cast<std::uint16_t>(
                    ev.voicePulseWidthLo | (static_cast<std::uint16_t>(ev.voicePulseWidthHi) << 8));
                ov.flags = ev.voiceFlags;
                ov.selectedFactorySlot = static_cast<std::uint16_t>(
                    ev.selectedFactorySlotLo | (static_cast<std::uint16_t>(ev.selectedFactorySlotHi) << 8));
                ov.hasSelectedFactorySlot = true;
                ov.hasWaveform = (ev.voiceOverrideMask & ArpSID::GUI::kKitVoiceOverrideWaveform) != 0u;
                ov.hasAttackDecay = (ev.voiceOverrideMask & ArpSID::GUI::kKitVoiceOverrideAttackDecay) != 0u;
                ov.hasSustainRelease = (ev.voiceOverrideMask & ArpSID::GUI::kKitVoiceOverrideSustainRelease) != 0u;
                ov.hasPulseWidth = (ev.voiceOverrideMask & ArpSID::GUI::kKitVoiceOverridePulseWidth) != 0u;
                ov.hasFlags = (ev.voiceOverrideMask & ArpSID::GUI::kKitVoiceOverrideFlags) != 0u;
                drumEngineBridge_.noteOnAtWithOverride(offset, gmClass, vel, ev.midiNote, ov, true);
            } else if (target == ArpSID::GUI::KitEngineTarget::DrSID) {
                if (drs_()) {
                    const float vf = static_cast<float>(vel) * (1.0f / 127.0f);
                    const std::uint16_t selectedDrsidSlot = static_cast<std::uint16_t>(
                        ev.selectedFactorySlotLo | (static_cast<std::uint16_t>(ev.selectedFactorySlotHi) << 8));
                    drs_()->triggerKitMidiNote(static_cast<int>(ev.midiNote),
                                               vf,
                                               selectedDrsidSlot,
                                               ArpSID::isDrSidFactorySlot(static_cast<int>(selectedDrsidSlot)),
                                               ev.voiceWaveform,
                                               ev.voiceAttackDecay,
                                               ev.voiceSustainRelease,
                                               static_cast<std::uint16_t>(
                                                   ev.voicePulseWidthLo | (static_cast<std::uint16_t>(ev.voicePulseWidthHi) << 8)),
                                               ev.voiceFlags,
                                               ev.voiceOverrideMask);
                }
            } else if (target == ArpSID::GUI::KitEngineTarget::Digi) {
                const std::uint8_t sourceSlotIndex = static_cast<std::uint8_t>(
                    std::min<int>(ev.digiSlotIndex, ArpSID::GUI::kDigiActiveSlotCount - 1));
                constexpr std::uint8_t slotIndex = 0u;
                ArpSID::GUI::GuiRealtimeDigiProjection oneShot{};
                oneShot.schemaVersion = ArpSID::GUI::kGuiRealtimeProjectionSchemaVersion;
                oneShot.stepIndex = static_cast<std::uint8_t>(step);
                oneShot.activeSlot = slotIndex;
                oneShot.activeSlotCount = 1u;
                auto& dst = oneShot.slots[slotIndex];
                const auto& kd = guiRealtimeProjectionRender_.kit.drums[static_cast<std::size_t>(ev.drumClass)];
                dst.activeAtStep = 1u;
                dst.sourceType = static_cast<std::uint8_t>(ArpSID::GUI::DigiSourceType::FactorySlot);
                const std::uint16_t selectedDigiSlot = static_cast<std::uint16_t>(
                    ev.selectedFactorySlotLo | (static_cast<std::uint16_t>(ev.selectedFactorySlotHi) << 8));
                const int selectedDigiIndex = ArpSID::isDigiFactorySlot(static_cast<int>(selectedDigiSlot))
                    ? static_cast<int>(selectedDigiSlot) - static_cast<int>(ArpSID::kDigiNewFactoryRange.first)
                    : static_cast<int>(sourceSlotIndex);
                dst.factorySlotIndex = static_cast<std::uint8_t>(
                    std::clamp(selectedDigiIndex, 0, ArpSID::GUI::kKitDigiSlotCount - 1));
                dst.absoluteFactorySlot = ArpSID::isDigiFactorySlot(static_cast<int>(selectedDigiSlot))
                    ? selectedDigiSlot
                    : kd.digiFactorySlot;
                dst.stepVelocity = vel;
                dst.tuneShift = kd.digiTuneShift;
                dst.startOffset = kd.digiStartOffset;
                dst.lengthScale = kd.digiLengthScale;
                dst.volume = 200u;
                dst.flags = kd.digiFlags;
                digiSampler_.triggerSlotAt(slotIndex, vel, guiRealtimeDigiSampleBankRender_, oneShot, offset);
            }
            }
        }
    }
    int kitSequencerLastStep_ = -1;  // tracks previous step to detect boundaries
    std::uint32_t compiledKitSourceHash_ = 0u;  // render-side invalidation key for KIT pattern

    // A2: Render through the DrumEngineHostBridge for DrumMachine/Sid808 flavors.
    // Flavor + active identity are the complete routing law.
    static ArpSID::DrumStemMixPolicy resolveDrumStemMixPolicyForFlavor_(ArpSID::ComponentFlavor flavor) noexcept {
        return (flavor == ArpSID::ComponentFlavor::Sid808)
            ? ArpSID::DrumStemMixPolicy::ReplaceWithSid808
            : ArpSID::DrumStemMixPolicy::AdditiveDrumMachine;
    }

    void renderDrumBridgeIfActive_(float** outputs, int numFrames) noexcept {
        if (componentFlavor_ != ArpSID::ComponentFlavor::DrumMachine &&
            componentFlavor_ != ArpSID::ComponentFlavor::Sid808) {
            telemetrySid808OutputPeak_.store(0.0f, std::memory_order_relaxed);
            telemetrySid808BridgeReplacedOutput_.store(0u, std::memory_order_relaxed);
            return;
        }
        if (!outputs || !outputs[0] || numFrames <= 0) {
            telemetrySid808OutputPeak_.store(0.0f, std::memory_order_relaxed);
            telemetrySid808BridgeReplacedOutput_.store(0u, std::memory_order_relaxed);
            return;
        }

        const auto ctx = drumEngineBridge_.activeIdentity().context;
        if (ctx != ArpSID::DrumContext::SID808_AnalogProjection) {
            // No split-brain rule:
            // * DrSID/DrumMachine audio is owned by canonical engineBank_.drSid.
            // * The bridge is allowed to replace output only for SID808.
            // * context None and DrSID_C64Wavetable both preserve canonical audio.
            drumBridgeDcBlockerReset_();
            telemetrySid808OutputPeak_.store(0.0f, std::memory_order_relaxed);
            telemetrySid808BridgeReplacedOutput_.store(0u, std::memory_order_relaxed);
            return;
        }

        // SID808 replacement render only. DrSID bridge-owned engine is never used
        // as production audio authority, so there is no canonical/bridge DrSID
        // split brain.
        syncDrumBridgeProjectionFromRenderParams_();

        if (numFrames > kMaxFramesPerBlock) {
            telemetrySid808OutputPeak_.store(0.0f, std::memory_order_relaxed);
            telemetrySid808RawPeakBeforeDc_.store(0.0f, std::memory_order_relaxed);
            telemetrySid808RawMeanBeforeDc_.store(0.0f, std::memory_order_relaxed);
            telemetrySid808PostDcPeak_.store(0.0f, std::memory_order_relaxed);
            telemetrySid808PostDcMean_.store(0.0f, std::memory_order_relaxed);
            telemetrySid808BridgeReplacedOutput_.store(0u, std::memory_order_relaxed);
            return;
        }
        std::memset(sliceScratchL_, 0, static_cast<size_t>(numFrames) * sizeof(float));
        std::memset(sliceScratchR_, 0, static_cast<size_t>(numFrames) * sizeof(float));
        drumEngineBridge_.processBlock(sliceScratchL_, sliceScratchR_, numFrames);

        float rawPeak = 0.0f;
        double rawMeanSum = 0.0;
        for (int i = 0; i < numFrames; ++i) {
            const float l = ArpSID_sanitizeFloat(sliceScratchL_[i]);
            const float r = ArpSID_sanitizeFloat(sliceScratchR_[i]);
            rawPeak = std::max(rawPeak, std::max(std::fabs(l), std::fabs(r)));
            rawMeanSum += 0.5 * (static_cast<double>(l) + static_cast<double>(r));
        }
        telemetrySid808RawPeakBeforeDc_.store(ArpSIDSanitizeTelemetryUnitFloat(rawPeak), std::memory_order_relaxed);
        telemetrySid808RawMeanBeforeDc_.store(
            ArpSIDSanitizeTelemetryBipolarFloat(static_cast<float>(rawMeanSum / std::max(1, numFrames))),
            std::memory_order_relaxed);

        // v860 noise/click fix: the SID808 engine's raw output carries the SID
        // model's D418 master-volume DC level (measured ≈ +0.0196 at idle for
        // kit 120). Because this path REPLACES the whole bus, that DC bypassed
        // every canonical DC-handling stage: the flavor emitted a constant DC
        // floor from the moment it loaded, and every pad gate on/off stepped it
        // — audible clicks/crackle ("just gives noise when pressing pad").
        // One-pole DC blocker (~4 Hz high-pass, RT-safe, 2 state floats per
        // channel) centres the bridge output; drums pass unchanged.
        {
            const float sr = (std::isfinite(sampleRate_) && sampleRate_ > 1.0) ? (float)sampleRate_ : 48000.0f;
            const float R = std::clamp(1.0f - 25.0f / sr, 0.990f, 0.99995f);
            telemetrySid808DcBlockerR_.store(ArpSIDSanitizeTelemetryUnitFloat(R), std::memory_order_relaxed);
            float xL = drumBridgeDcPrevInL_, yL = drumBridgeDcPrevOutL_;
            float xR = drumBridgeDcPrevInR_, yR = drumBridgeDcPrevOutR_;
            for (int i = 0; i < numFrames; ++i) {
                const float inL = sliceScratchL_[i];
                yL = inL - xL + R * yL;
                xL = inL;
                sliceScratchL_[i] = yL;
                const float inR = sliceScratchR_[i];
                yR = inR - xR + R * yR;
                xR = inR;
                sliceScratchR_[i] = yR;
            }
            drumBridgeDcPrevInL_ = xL; drumBridgeDcPrevOutL_ = ArpSID_sanitizeFloat(yL, 0.0f);
            drumBridgeDcPrevInR_ = xR; drumBridgeDcPrevOutR_ = ArpSID_sanitizeFloat(yR, 0.0f);
        }

        float bridgePeak = 0.0f;
        double postMeanSum = 0.0;
        for (int i = 0; i < numFrames; ++i) {
            const float l = ArpSID_sanitizeFloat(sliceScratchL_[i]);
            const float r = ArpSID_sanitizeFloat(sliceScratchR_[i]);
            bridgePeak = std::max(bridgePeak, std::max(std::fabs(l), std::fabs(r)));
            postMeanSum += 0.5 * (static_cast<double>(l) + static_cast<double>(r));
        }
        telemetrySid808OutputPeak_.store(ArpSIDSanitizeTelemetryUnitFloat(bridgePeak), std::memory_order_relaxed);
        telemetrySid808PostDcPeak_.store(ArpSIDSanitizeTelemetryUnitFloat(bridgePeak), std::memory_order_relaxed);
        telemetrySid808PostDcMean_.store(
            ArpSIDSanitizeTelemetryBipolarFloat(static_cast<float>(postMeanSum / std::max(1, numFrames))),
            std::memory_order_relaxed);

        if (componentFlavor_ == ArpSID::ComponentFlavor::Sid808) {
            const std::uint64_t noteOnCount = drumEngineBridge_.sid808Engine().noteOnCount();
            const bool bridgeHasRenderableActivity =
                bridgePeak > 1.0e-7f ||
                drumEngineBridge_.sid808Engine().activeVoiceCount() > 0u ||
                noteOnCount != sid808BridgeLastRenderedNoteOnCount_;
            sid808BridgeLastRenderedNoteOnCount_ = noteOnCount;

            // Sid808 flavor is a drum-authority flavor: renderable bridge
            // activity owns the whole output bus, but the policy must stay
            // fail-open so stale/idle scratch does not erase canonical fallback
            // audio while MIDI/telemetry still shows activity.
            if (bridgeHasRenderableActivity) {
                telemetrySid808BridgeReplacedOutput_.store(1u, std::memory_order_relaxed);
                std::memcpy(outputs[0], sliceScratchL_, static_cast<size_t>(numFrames) * sizeof(float));
                if (outputs[1] && outputs[1] != outputs[0])
                    std::memcpy(outputs[1], sliceScratchR_, static_cast<size_t>(numFrames) * sizeof(float));
            } else {
                telemetrySid808BridgeReplacedOutput_.store(0u, std::memory_order_relaxed);
                drumBridgeDcBlockerReset_();
            }
        } else {
            telemetrySid808BridgeReplacedOutput_.store(0u, std::memory_order_relaxed);
            // DrumMachine flavor can host other/canonical layers. Do not overwrite
            // the full bus; add the SID808 bridge layer and preserve existing audio.
            for (int i = 0; i < numFrames; ++i) {
                outputs[0][i] = std::clamp(outputs[0][i] + sliceScratchL_[i], -1.25f, 1.25f);
                if (outputs[1] && outputs[1] != outputs[0])
                    outputs[1][i] = std::clamp(outputs[1][i] + sliceScratchR_[i], -1.25f, 1.25f);
            }
        }
    }

    // v860/v861: SID808 bridge DC-blocker and fail-open state (see renderDrumBridgeIfActive_).
    float drumBridgeDcPrevInL_ = 0.0f, drumBridgeDcPrevOutL_ = 0.0f;
    float drumBridgeDcPrevInR_ = 0.0f, drumBridgeDcPrevOutR_ = 0.0f;
    std::uint64_t sid808BridgeLastRenderedNoteOnCount_ = 0u;
    void drumBridgeDcBlockerReset_() noexcept {
        drumBridgeDcPrevInL_ = drumBridgeDcPrevOutL_ = 0.0f;
        drumBridgeDcPrevInR_ = drumBridgeDcPrevOutR_ = 0.0f;
        telemetrySid808DcBlockerResetCount_.fetch_add(1u, std::memory_order_relaxed);
        sid808BridgeLastRenderedNoteOnCount_ = drumEngineBridge_.sid808Engine().noteOnCount();
    }

    void publishDigiSamplerTelemetry_() noexcept {
        // Contract fix: when AUTH C64-BUS D418 or FAST PRIVATE D418 owns DIGI, the D418 stream
        // engine has already published authoritative slot/scope/counter
        // telemetry. The legacy sampler may still contain old state, so never
        // let it overwrite AUTH HUD/forensic telemetry outside LegacyFloatLayer.
        if (resolveDigiAuthMode_() != ArpSID::DigiAuthMode::LegacyFloatLayer) return;
        const DigiSamplerTelemetry& d = digiSampler_.telemetry();
        telemetryDigiActiveSlots_.store(d.activeSlotCount, std::memory_order_relaxed);
        telemetryDigiConfiguredFactorySlots_.store(d.configuredFactorySlots, std::memory_order_relaxed);
        telemetryDigiConfiguredUserImportSlots_.store(d.configuredUserImportSlots, std::memory_order_relaxed);
        telemetryDigiPlayingVoices_.store(d.playingVoiceCount, std::memory_order_relaxed);
        telemetryDigiPeakVoices_.store(d.peakVoiceCount, std::memory_order_relaxed);
        telemetryDigiStepIndex_.store(d.stepIndex, std::memory_order_relaxed);
        telemetryDigiLastSlot_.store(d.lastTriggeredSlot, std::memory_order_relaxed);
        telemetryDigiLastFactorySlot_.store(d.lastTriggeredFactorySlot, std::memory_order_relaxed);
        telemetryDigiTriggerCount_.store(d.triggerCount, std::memory_order_relaxed);
        // Legacy float sampler has no D418-specific MIDI mapper; keep the AUTH MIDI
        // counters as the authority for D418 pad-trigger observability.
        telemetryDigiUnavailableUserImports_.store(d.unavailableUserImportCount, std::memory_order_relaxed);
        telemetryDigiOutputPeak_.store(ArpSIDSanitizeTelemetryUnitFloat(d.outputPeak), std::memory_order_relaxed);
        float scope[DigiSamplerEngine::kScopeLen]{};
        digiSampler_.copyScope(scope, DigiSamplerEngine::kScopeLen);
        auto& snap = telemetryDigiScopeTriple_.writeSlot();
        snap = DigiScopeSnapshot{};
        for (int i = 0; i < DigiSamplerEngine::kScopeLen; ++i)
            snap.scope[i] = ArpSIDSanitizeTelemetryBipolarFloat(scope[i]);
        snap.writePos = digiSampler_.scopeWritePos();
        snap.frameId = blockIndex_;
        telemetryDigiScopeTriple_.publish();
    }

    void publishTelemetryScopeSnapshot_(SidRuntimeRenderMode mode) noexcept {
        auto& snap = telemetryScopeTriple_.writeSlot();
        snap = TelemetryScopeSnapshot{};
        snap.frameId = blockIndex_;
        // C64SidPlayer flavor: mode resolver returns BitPerfect (not C64Psid which is
        // unassigned). Check the live PSID player pointer as the canonical indicator.
        if (c64PsidLive_.load(std::memory_order_acquire) != nullptr) {
            // C64 SIDPlay audio is rendered by SidRegisterEngine after the 6510/PSID
            // runtime has converted SID bus writes into timed register changes. Since
            // That path calls SidRegisterEngine::renderBlock(), which advances the
            // normal tick() path and therefore publishes the authoritative per-SID-voice
            // scope ring. Prefer that real V1/V2/V3 scope image whenever it contains
            // energy, but keep the render-owned main scope ring as an
            // unconditional C64 presentation authority:
            // if a SID voice scope is not published yet, is gated by a sparse tune, or
            // the first PSID/C64 snapshot races the GUI, every C64 VCO panel still gets
            // the live post-FX waveform that updateTelemetry_() writes every render.
            // Do not publish from renderTimedSampleAccurate(): that function is shared
            // with SID-register synth mode and setting scopeActiveMask_ there regresses
            // synth scopes.
            sreg_().getScopeSnapshot(snap.oscScope, snap.filterScope, snap.activeMask, snap.writePos);

            const int wp = static_cast<int>(mainOscLiveWritePos_ & (kOscBufLen - 1));
            float mix[256]{};
            bool mixAny = false;
            for (int i = 0; i < 256; ++i) {
                const int rp = (wp - 256 + i + kOscBufLen) & (kOscBufLen - 1);
                const float v = ArpSIDSanitizeTelemetryBipolarFloat(mainOscLive_[rp]);
                mix[i] = v;
                mixAny = mixAny || (std::fabs(v) > 1.0e-7f);
            }
            if (mixAny) {
                for (int vco = 0; vco < 3; ++vco) {
                    bool dedicatedAny = ((snap.activeMask & (uint8_t)(1u << vco)) != 0u);
                    if (dedicatedAny) {
                        dedicatedAny = false;
                        for (int i = 0; i < 256; ++i) {
                            if (std::fabs(snap.oscScope[vco][i]) > 1.0e-7f) { dedicatedAny = true; break; }
                        }
                    }
                    if (!dedicatedAny) {
                        for (int i = 0; i < 256; ++i) snap.oscScope[vco][i] = mix[i];
                        snap.activeMask |= (uint8_t)(1u << vco);
                    }
                }
                for (int i = 0; i < 256; ++i) {
                    snap.filterScope[0][i] = mix[i];
                    snap.filterScope[1][i] = mix[i];
                }
                snap.writePos = static_cast<uint32_t>(wp & 0xFF);
            }
        } else if (mode == SidRuntimeRenderMode::DrSid && drs_()) {
            drs_()->getScopeSnapshot(snap.oscScope, snap.filterScope, snap.activeMask, snap.writePos);
        } else if (mode == SidRuntimeRenderMode::SidRegister) {
            sreg_().getScopeSnapshot(snap.oscScope, snap.filterScope, snap.activeMask, snap.writePos);
        } else if (bpe_()) {
            bpe_()->getScopeSnapshot(snap.voiceScope, snap.oscScope, snap.filterScope, snap.activeMask, snap.writePos);
        } else {
            sreg_().getScopeSnapshot(snap.oscScope, snap.filterScope, snap.activeMask, snap.writePos);
        }

        // Telemetry guard: every GUI scope/presentation panel must have a
        // coherent render-owned signal even when the selected backend does not
        // publish a dedicated per-voice scope for the current mode. This keeps
        // forensic mode, SID-register mode, DrSID projection kits, and fallback
        // BitPerfect rendering from showing a stale/blank scope while the meter
        // and forensic telemetry are live. The source is the post-FX output ring
        // already written by updateTelemetry_() on the audio thread.
        if (snap.activeMask == 0u) {
            const int wp = static_cast<int>(mainOscLiveWritePos_ & (kOscBufLen - 1));
            bool any = false;
            for (int i = 0; i < 256; ++i) {
                const int rp = (wp - 256 + i + kOscBufLen) & (kOscBufLen - 1);
                const float v = ArpSIDSanitizeTelemetryBipolarFloat(mainOscLive_[rp]);
                snap.oscScope[0][i] = v;
                snap.oscScope[1][i] = v;
                snap.oscScope[2][i] = v;
                snap.filterScope[0][i] = v;
                snap.filterScope[1][i] = v;
                any = any || (std::fabs(v) > 1.0e-7f);
            }
            if (any) {
                snap.activeMask = 0x07u;
                snap.writePos = static_cast<uint32_t>(wp & 0xFF);
            }
        }
        telemetryScopeTriple_.publish();
    }

    static float forensicTelemetryActivityFromConfig_(const ArpSIDForensicConfig& fc) noexcept {
        if (!fc.enable) return 0.0f;
        const float intensity = ArpSIDSanitizeTelemetryUnitFloat(fc.clampedIntensity());
        const float clock  = fc.clockJitterEnabled ? ArpSIDSanitizeTelemetryUnitFloat(fc.clockJitter) : 0.0f;
        const float ripple = fc.supplyRippleEnabled ? ArpSIDSanitizeTelemetryUnitFloat(fc.supplyRipple) : 0.0f;
        const float drift  = fc.thermalDriftEnabled ? ArpSIDSanitizeTelemetryUnitFloat(fc.thermalDrift) : 0.0f;
        const float xtalk  = fc.voiceCrosstalkEnabled ? ArpSIDSanitizeTelemetryUnitFloat(fc.voiceCrosstalk) : 0.0f;
        const float bleed  = fc.externalBleedEnabled ? ArpSIDSanitizeTelemetryUnitFloat(fc.externalBleed) : 0.0f;
        const float analog = std::max({ArpSIDSanitizeTelemetryUnitFloat(fc.filterOhmic),
                                       ArpSIDSanitizeTelemetryUnitFloat(fc.systemNoise),
                                       ArpSIDSanitizeTelemetryUnitFloat(fc.d418Asymmetry)});
        const float bus = std::max({ArpSIDSanitizeTelemetryUnitFloat(fc.envelopeTDM),
                                    ArpSIDSanitizeTelemetryUnitFloat(fc.motherboard),
                                    ArpSIDSanitizeTelemetryUnitFloat(fc.adcBleed),
                                    ArpSIDSanitizeTelemetryUnitFloat(fc.busCollision),
                                    ArpSIDSanitizeTelemetryUnitFloat(fc.potInput)});
        const float temperatureBias = ArpSIDSanitizeTelemetryUnitFloat(
            ArpSID::ArpSID_sanitizeFloat(std::fabs(fc.temperatureCelsius - 35.0f) * (1.0f / 30.0f)));
        const float supplyBias = ArpSIDSanitizeTelemetryUnitFloat(
            ArpSID::ArpSID_sanitizeFloat(std::fabs(fc.supplyVoltage - 5.0f) * 2.0f));
        const float dense = 0.26f * std::max(clock, ripple)
                          + 0.20f * std::max(drift, xtalk)
                          + 0.14f * bleed
                          + 0.18f * analog
                          + 0.14f * bus
                          + 0.08f * temperatureBias
                          + 0.06f * supplyBias;
        return std::clamp(intensity * dense, 0.0f, 1.0f);
    }

    void publishMpkMiniTelemetryFromEffectiveParams_() noexcept {
        // Publish effective mapped params every telemetry frame so the MPK hub
        // reflects patch load, automation, JSON import, and MIDI CC alike.
        // lastMappedCC remains the separate physical-controller event indicator.
        for (int i = 0; i < 8; ++i) {
            const ParamID pid = ArpSID::sidAkaiMpkMiniDefaultKnobParamByIndex(i);
            float value = 0.0f;
            if (pid >= 0 && pid < kNumParams) value = renderParams_[(size_t)pid];
            telemetryMpkKnobValue_[(size_t)i].store(ArpSIDSanitizeTelemetryUnitFloat(value), std::memory_order_relaxed);
        }
    }

    void publishForensicTelemetry_(const ArpSIDForensicConfig& fc) noexcept {
        telemetryForensicEnabled_.store(fc.enable ? 1u : 0u, std::memory_order_relaxed);
        telemetryForensicIntensity_.store(ArpSIDSanitizeTelemetryUnitFloat(fc.clampedIntensity()), std::memory_order_relaxed);
        telemetryForensicClockJitter_.store((fc.enable && fc.clockJitterEnabled) ? ArpSIDSanitizeTelemetryUnitFloat(fc.clockJitter) : 0.0f, std::memory_order_relaxed);
        telemetryForensicSupplyRipple_.store((fc.enable && fc.supplyRippleEnabled) ? ArpSIDSanitizeTelemetryUnitFloat(fc.supplyRipple) : 0.0f, std::memory_order_relaxed);
        telemetryForensicThermalDrift_.store((fc.enable && fc.thermalDriftEnabled) ? ArpSIDSanitizeTelemetryUnitFloat(fc.thermalDrift) : 0.0f, std::memory_order_relaxed);
        telemetryForensicVoiceCrosstalk_.store((fc.enable && fc.voiceCrosstalkEnabled) ? ArpSIDSanitizeTelemetryUnitFloat(fc.voiceCrosstalk) : 0.0f, std::memory_order_relaxed);
        telemetryForensicExternalBleed_.store((fc.enable && fc.externalBleedEnabled) ? ArpSIDSanitizeTelemetryUnitFloat(fc.externalBleed) : 0.0f, std::memory_order_relaxed);
        telemetryForensicFilterOhmic_.store(fc.enable ? ArpSIDSanitizeTelemetryUnitFloat(fc.filterOhmic) : 0.0f, std::memory_order_relaxed);
        telemetryForensicSystemNoise_.store(fc.enable ? ArpSIDSanitizeTelemetryUnitFloat(fc.systemNoise) : 0.0f, std::memory_order_relaxed);
        telemetryForensicD418Asymmetry_.store(fc.enable ? ArpSIDSanitizeTelemetryUnitFloat(fc.d418Asymmetry) : 0.0f, std::memory_order_relaxed);
        telemetryForensicEnvelopeTDM_.store(fc.enable ? ArpSIDSanitizeTelemetryUnitFloat(fc.envelopeTDM) : 0.0f, std::memory_order_relaxed);
        telemetryForensicMotherboard_.store(fc.enable ? ArpSIDSanitizeTelemetryUnitFloat(fc.motherboard) : 0.0f, std::memory_order_relaxed);
        telemetryForensicADCBleed_.store(fc.enable ? ArpSIDSanitizeTelemetryUnitFloat(fc.adcBleed) : 0.0f, std::memory_order_relaxed);
        telemetryForensicBusCollision_.store(fc.enable ? ArpSIDSanitizeTelemetryUnitFloat(fc.busCollision) : 0.0f, std::memory_order_relaxed);
        telemetryForensicPotInput_.store(fc.enable ? ArpSIDSanitizeTelemetryUnitFloat(fc.potInput) : 0.0f, std::memory_order_relaxed);
        telemetryForensicDigifix8580_.store((fc.enable && fc.digifix8580) ? 1u : 0u, std::memory_order_relaxed);
        telemetryForensicActivity_.store(forensicTelemetryActivityFromConfig_(fc), std::memory_order_relaxed);
    }

    static uint64_t c64HeavyTelemetryPeriodSamples_(double sampleRate) noexcept {
        const double sanitized = ArpSIDSanitizeHostSampleRate(sampleRate);
        const uint64_t hostRate = static_cast<uint64_t>(std::clamp(std::llround(sanitized), 1ll, 384000ll));
        const uint64_t period = (hostRate + (kC64HeavyTelemetryHz / 2u)) / kC64HeavyTelemetryHz;
        return std::clamp<uint64_t>(std::max<uint64_t>(period, 1u), 128u, 65536u);
    }

    static uint32_t c64SidTelemetryHash_(const uint8_t* regs, int count) noexcept {
        uint32_t h = 2166136261u;
        const int n = std::clamp(count, 0, 32);
        for (int i = 0; i < n; ++i) {
            h ^= static_cast<uint32_t>(regs ? regs[i] : 0u);
            h *= 16777619u;
        }
        return h ? h : 1u;
    }

    void publishC64RealtimeBusScope_(uint8_t openBus,
                                     uint8_t lastRead,
                                     uint8_t lastSidReg,
                                     uint8_t lastSidValue,
                                     bool sidWriteObserved,
                                     bool irqOrDmaActive,
                                     uint8_t chipIndex = 0u,
                                     uint8_t openBusDecayMask = 0xFFu,
                                     bool openBusDrivenWithinPersistence = true) noexcept {
        static_assert((kC64BusScopeLen & (kC64BusScopeLen - 1)) == 0, "C64 bus scope length must be a power of two");
        (void)lastRead;
        const uint8_t sidMix = static_cast<uint8_t>(lastSidValue ^ static_cast<uint8_t>(lastSidReg * 17u) ^
                                                    static_cast<uint8_t>(sidWriteObserved ? 0x5Au : 0x00u));
        const float openDecayGain = openBusDecayMask == 0xFFu
            ? 1.0f
            : (openBusDecayMask == 0x00u ? 0.22f : 0.68f);
        const float openHoldGain = openBusDrivenWithinPersistence ? 1.0f : 0.55f;
        const float openSample = std::clamp(
            ((static_cast<float>(openBus) * (1.0f / 127.5f)) - 1.0f) * openDecayGain * openHoldGain,
            -1.0f,
            1.0f);
        const float writeGain = sidWriteObserved ? 1.0f : 0.42f;
        const float sidSample = std::clamp(((static_cast<float>(sidMix) * (1.0f / 127.5f)) - 1.0f) * writeGain,
                                           -1.0f,
                                           1.0f);
        const float regSample = std::clamp((static_cast<float>(lastSidReg) / 14.5f) - 1.0f, -1.0f, 1.0f);
        const float valueSample = (static_cast<float>(lastSidValue) * (1.0f / 127.5f)) - 1.0f;
        const float writePulseSample = sidWriteObserved ? 1.0f : -1.0f;
        const float phi2Sample = (telemetryC64Phi2Cycle_.load(std::memory_order_relaxed) & 1u) ? 1.0f : -1.0f;
        const float irqDmaSample = irqOrDmaActive ? 1.0f : -1.0f;
        const float chipSample = std::clamp((static_cast<float>(std::min<uint8_t>(chipIndex, 4u)) * 0.5f) - 1.0f,
                                            -1.0f, 1.0f);
        const uint32_t pos = c64BusScopeLive_.writePos & static_cast<uint32_t>(kC64BusScopeLen - 1);
        c64BusScopeLive_.openBus[pos] = openSample;
        c64BusScopeLive_.sidBus[pos] = sidSample;
        c64BusScopeLive_.sidReg[pos] = regSample;
        c64BusScopeLive_.sidValue[pos] = valueSample;
        c64BusScopeLive_.sidWritePulse[pos] = writePulseSample;
        c64BusScopeLive_.phi2[pos] = phi2Sample;
        c64BusScopeLive_.irqDma[pos] = irqDmaSample;
        c64BusScopeLive_.chip[pos] = chipSample;
        c64BusScopeLive_.writePos = (pos + 1u) & static_cast<uint32_t>(kC64BusScopeLen - 1);
    }

    // Single C64/SID mirror publisher: projection writes enter the C64 telemetry
    // mirror through the applied-write observer only. Backend register images are
    // GUI snapshots / reset repair data, not a parallel per-block bus authority.
    // GUI/HUD/tabs read only these atomically-published snapshots.
    void publishC64PlatformMirrorTelemetry_(const uint8_t* sidRegs, int sidRegCount, int frames) noexcept {
        const ArpSID::C64::C64Runtime* psidLive = c64PsidLive_.load(std::memory_order_acquire);
        const bool pal = resolveC64ProjectionMirrorPal_();
        const int nFrames = std::max(0, frames);
        const bool demandWantsC64Snapshot = finishC64TelemetryDemandBlock_(frames);
        const uint64_t blockStartSample = c64TelemetryHostSampleCursor_;
        const uint64_t blockEndSample = (UINT64_MAX - blockStartSample >= static_cast<uint64_t>(nFrames))
            ? (blockStartSample + static_cast<uint64_t>(nFrames))
            : static_cast<uint64_t>(nFrames);
        const uint64_t heavyPeriodSamples = c64HeavyTelemetryPeriodSamples_(sampleRate_);
        c64HeavyTelemetryPeriodSamplesLast_ = heavyPeriodSamples;
        // No C64/Projection panel is currently reading the chip snapshot: keep
        // scalar atomics live every block, but skip the heavy cockpit snapshot
        // unless an integer host-sample deadline or a C64/SID state edge asks for
        // it. The cadence is phase-locked to c64TelemetryHostSampleCursor_; no
        // fmod/floating debt is used, missed periods advance by whole periods,
        // and c64LastHeavyTelemetryBlock_ enforces at most one heavy publish per
        // render block.
        ensureC64ProjectionMirrorClockReady_(pal);
        // v884 closure: do not advance the cosmetic mirror clock until this
        // block is known to be consumed. cyclesForNextHostBlock() mutates the
        // fractional PHI2 accumulator; calling it for an idle/discarded cockpit
        // block creates a hidden clock-phase authority even though bus writes and
        // CPU debt are discarded below. Keep unconsumed mirror blocks fully
        // frozen: no fractional-clock advance, no debt, no queued projection
        // replay. Compute cycles lazily after the consumption decision.
        uint64_t cyclesDueThisBlock = 0u;
        uint64_t cyclesThisBlock = 0u;
        float mirrorFidelity = 1.0f;

        uint8_t lastSidReg = telemetryC64LastSidReg_.load(std::memory_order_relaxed);
        uint8_t lastSidValue = telemetryC64LastSidValue_.load(std::memory_order_relaxed);
        (void)lastSidReg;
        (void)lastSidValue;
        // v875 audit closure: do not reconcile the final SID register image through
        // the C64 bus on normal render blocks. That old path injected immediate
        // sample-zero writes that could contradict the applied-write observer's
        // sample/cycle timing. Reconciliation is reserved for explicit reset/load
        // repair sites; normal projection mirror authority is observer-only.
        (void)sidRegCount;
        // audit (state-churn): advance the realtime C64 telemetry-MIRROR CPU only
        // when a C64/SIDCORE cockpit is actually being viewed. This is a cosmetic
        // GUI mirror ("not audible authority"); once real stock ROMs are loaded its
        // CPU executes the live KERNAL, which — if run every block in every mode as
        // before — boots and free-runs continuously and makes the C64/SID register
        // telemetry look like it is "updating all the time" even on the synth/DrSID
        // tabs. The SID register projection above still runs every block, so the
        // register display stays live in all modes; only this cosmetic CPU mirror
        // is gated. Audible PSID/RSID playback uses the player's own platform and is
        // unaffected.
        // Idle visualizer fix: advance the cosmetic C64 telemetry-MIRROR CPU only
        // when a cockpit is viewed AND there is actual audio activity. Previously,
        // viewing the cockpit advanced the live KERNAL every block, so its idle loop
        // free-ran and the "REALTIME OPEN-BUS SCOPE" (the green Metal SID-bus surface)
        // rolled continuously even with no notes — i.e. movement with no data. Gate
        // the advance on sounding voices / measurable output (read from the previous
        // block's telemetry; one-block latency is imperceptible) so the mirror — and
        // thus the green open-bus scope and the C64 register/raster readouts — holds
        // still when nothing is playing and comes alive only while audio is present.
        const bool c64MirrorAudioActive =
            telemetryVoices_.load(std::memory_order_relaxed) > 0 ||
            std::max(telemetryPeakL_.load(std::memory_order_relaxed),
                     telemetryPeakR_.load(std::memory_order_relaxed)) > 0.0025f;
        const bool c64MirrorHasQueuedProjectionWrites = c64ProjectionMirrorQueuedWritesThisBlock_ > 0u;
        const bool c64MirrorConsumedThisBlock =
            demandWantsC64Snapshot && (c64MirrorAudioActive || c64MirrorHasQueuedProjectionWrites);
        if (c64MirrorConsumedThisBlock) {
            cyclesDueThisBlock = c64PlatformClock_.cyclesForNextHostBlock(static_cast<uint32_t>(nFrames));
            const uint64_t boundedNewDebt = std::min<uint64_t>(cyclesDueThisBlock, kC64RealtimeMaxCycleDebt);
            const uint64_t totalPendingCycles = std::min<uint64_t>(boundedNewDebt + c64RealtimeCycleDebt_, kC64RealtimeMaxCycleDebt);
            cyclesThisBlock = std::min<uint64_t>(totalPendingCycles, kC64RealtimeMaxCyclesPerAudioBlock);
            c64RealtimeCycleDebt_ = std::min<uint64_t>(totalPendingCycles - cyclesThisBlock, kC64RealtimeMaxCycleDebt);
            // Mirror fidelity ratio: how much of the real C64 clock this block covers.
            // kC64RealtimeMaxCyclesPerAudioBlock is intentionally lossy (GUI telemetry
            // mirror only — not audible authority). Expose ratio so UI can indicate
            // degradation.
            mirrorFidelity = (cyclesDueThisBlock > 0u)
                ? std::clamp(static_cast<float>(cyclesThisBlock) / static_cast<float>(cyclesDueThisBlock), 0.0f, 1.0f)
                : 1.0f;
            (void)c64Platform_.runRealtimeSidCoreCycles(cyclesThisBlock, 256u);
            // v885 closure: projection observer writes are block-local even when
            // the cosmetic C64 mirror is consumed. If the realtime mirror is
            // cycle-capped (mirrorFidelity < 1), some same-host-block projection
            // writes may still be scheduled beyond the cycles actually advanced.
            // Those writes describe audio events that already happened in this
            // host block and must not replay late in a future block. v899: they
            // are now FLUSHED (values applied through the normal bus path, then
            // removed) instead of dropped — the deliberate per-block cycle cap
            // previously deleted most same-block projection writes (a 512-frame
            // 48 kHz block spans ~10500 PHI2 cycles vs the 768-cycle cap, so
            // anything later than ~37 samples vanished and the SID projection
            // display froze). Ordinary CPU/CIA/PSID scheduled events are still
            // preserved; timing beyond the advanced window is reported via
            // mirrorFidelity.
            c64Platform_.flushScheduledProjectionWrites();
        } else {
            // Do not advance or accumulate a cycle backlog while no C64 cockpit is
            // reading, or while idle (no audio), so the cockpit telemetry stays
            // frozen. Also retire any observer-queued writes from this unconsumed
            // cosmetic block; they were block-local and must never replay late.
            // v899: flushed (values land in the register image) rather than
            // dropped, so a cockpit opened later shows current register state.
            c64RealtimeCycleDebt_ = 0;
            c64Platform_.flushScheduledProjectionWrites();
        }
        telemetryC64MirrorFidelity_.store(mirrorFidelity, std::memory_order_relaxed);

        const ArpSID::C64::C64Platform& telemetryPlatform = psidLive ? psidLive->platform() : c64Platform_;
        const bool psidActive = psidLive != nullptr;
        const uint32_t sidImageHash = c64SidTelemetryHash_(sidRegs ? sidRegs : telemetryPlatform.sidRegisterImage().data(),
                                                           sidRegs ? sidRegCount : 32);
        const auto& cpu = telemetryPlatform.cpu().state();
        const auto& vic = telemetryPlatform.vic();
        telemetryC64PlatformEnabled_.store(1u, std::memory_order_relaxed);
        telemetryC64MirrorEnabled_.store(1u, std::memory_order_relaxed);
        telemetryC64PsidRuntimeActive_.store(psidActive ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64ProjectionOnly_.store(psidActive ? 0u : 1u, std::memory_order_relaxed);
        telemetryC64Pal_.store(pal ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64RealtimeRunning_.store(telemetryPlatform.realtimeSidCoreRunning() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64Booted_.store(telemetryPlatform.booted() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64Phi2Cycle_.store(telemetryPlatform.phi2Cycle(), std::memory_order_relaxed);
        telemetryC64BlockIndex_.store(blockIndex_, std::memory_order_relaxed);
        telemetryC64PlayCalls_.store(c64BlockPlayCalls_, std::memory_order_relaxed);
        telemetryC64PlayCallCapHits_.store(c64PlayCallCapHitCount_.load(std::memory_order_relaxed),
                                           std::memory_order_relaxed);
        telemetryC64PlayCallsDroppedByCapTotal_.store(
            c64PlayCallsDroppedByCapTotal_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        telemetryC64PlayCallsDroppedByCapLastBlock_.store(
            c64PlayCallsDroppedByCapLastBlock_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        telemetryC64PlayCallsDroppedByCapMaxBlock_.store(
            c64PlayCallsDroppedByCapMaxBlock_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        telemetryC64RenderTransactionRollbackFailureCount_.store(
            c64RenderTransactionRollbackFailureCount_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        telemetryC64RenderContaminated_.store(
            c64RenderContaminated_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        telemetryC64RenderContaminationRecoveryCount_.store(
            c64RenderContaminationRecoveryCount_.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        telemetryC64ContinuousBudgetHitCount_.store(
            c64ContinuousBudgetHitCount_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        telemetryC64ContinuousCpuJamCount_.store(
            c64ContinuousCpuJamCount_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        telemetryC64ContinuousUnsupportedOpcodeCount_.store(
            c64ContinuousUnsupportedOpcodeCount_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        telemetryC64ContinuousIncompleteRunCount_.store(
            c64ContinuousIncompleteRunCount_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        telemetryC64CpuPc_.store(cpu.pc, std::memory_order_relaxed);
        telemetryC64CpuA_.store(cpu.a, std::memory_order_relaxed);
        telemetryC64CpuX_.store(cpu.x, std::memory_order_relaxed);
        telemetryC64CpuY_.store(cpu.y, std::memory_order_relaxed);
        telemetryC64CpuSp_.store(cpu.sp, std::memory_order_relaxed);
        telemetryC64CpuStatus_.store(cpu.p, std::memory_order_relaxed);
        telemetryC64CpuJammed_.store(cpu.jammed ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64IrqLine_.store(telemetryPlatform.irqLine() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64NmiLine_.store(telemetryPlatform.nmiLine() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64TrapBrkAsJam_.store(telemetryPlatform.trapBrkAsJam() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64ProcessorPort_.store(telemetryPlatform.effectiveProcessorPort(), std::memory_order_relaxed);
        telemetryC64VicRaster_.store(vic.rasterLine(), std::memory_order_relaxed);
        telemetryC64VicCycle_.store(static_cast<uint8_t>(std::min<uint16_t>(vic.cycleInLine(), 255u)), std::memory_order_relaxed);
        telemetryC64VicBadline_.store(vic.badline() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64VicBa_.store(vic.ba() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64VicAec_.store(vic.aec() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64VicSpriteDma_.store(vic.spriteDma() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64VicHalfCycle_.store(vic.halfCycle(), std::memory_order_relaxed);
        telemetryC64VicFrame_.store(vic.frame(), std::memory_order_relaxed);
        telemetryC64VicTotalStolen_.store(vic.totalStolen(), std::memory_order_relaxed);
        telemetryC64OpenBus_.store(telemetryPlatform.readOpenBus(), std::memory_order_relaxed);
        telemetryC64OpenBusDecayMask_.store(telemetryPlatform.openBusDecayMask(), std::memory_order_relaxed);
        telemetryC64OpenBusDrivenWithinPersistence_.store(
            telemetryPlatform.openBusDrivenWithinPersistence() ? 1u : 0u,
            std::memory_order_relaxed);
        telemetryC64OpenBusAgePhi2_.store(telemetryPlatform.openBusAgePhi2(), std::memory_order_relaxed);
        telemetryC64OpenBusLastDrivenPhi2_.store(telemetryPlatform.openBusLastDrivenPhi2(), std::memory_order_relaxed);
        telemetryC64SidOpenBusReadCount_.store(telemetryPlatform.sidNoSinkOpenBusReadCount(), std::memory_order_relaxed);
        telemetryC64ColorRamOpenBusReadCount_.store(
            telemetryPlatform.colorRamHighNibbleOpenBusReadCount(),
            std::memory_order_relaxed);
        telemetryC64PotxyOpenBusReadCount_.store(telemetryPlatform.sidNoSinkPotxyReadCount(), std::memory_order_relaxed);
        telemetryC64LastRead_.store(telemetryPlatform.lastReadValue(), std::memory_order_relaxed);
        const bool sidWriteObserved = telemetryPlatform.sidWriteObserved();
        if (telemetryPlatform.sidWriteObserved()) {
            // C64 mirror telemetry reads from platform bus authority; in non-PSID
            // mode that authority is c64Platform_.lastSidRegister().
            lastSidReg = telemetryPlatform.lastSidRegister();
            lastSidValue = telemetryPlatform.lastSidValue();
        }
        telemetryC64LastSidReg_.store(lastSidReg, std::memory_order_relaxed);
        telemetryC64LastSidValue_.store(lastSidValue, std::memory_order_relaxed);
        telemetryC64LastSidWriteCycle_.store(telemetryPlatform.lastSidWriteCycle(), std::memory_order_relaxed);
        publishC64RealtimeBusScope_(telemetryPlatform.readOpenBus(),
                                    telemetryPlatform.lastReadValue(),
                                    lastSidReg,
                                    lastSidValue,
                                    sidWriteObserved,
                                    telemetryPlatform.irqLine() || telemetryPlatform.nmiLine() ||
                                        vic.badline() || vic.spriteDma(),
                                    0u,
                                    telemetryPlatform.openBusDecayMask(),
                                    telemetryPlatform.openBusDrivenWithinPersistence());
        telemetryC64Cia1Irq_.store(telemetryPlatform.cia1().irqFlags(), std::memory_order_relaxed);
        telemetryC64Cia2Irq_.store(telemetryPlatform.cia2().irqFlags(), std::memory_order_relaxed);
        telemetryC64Cia1IrqLine_.store(telemetryPlatform.cia1().irq() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64Cia2IrqLine_.store(telemetryPlatform.cia2().irq() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64IecAtn_.store(telemetryPlatform.iec().atn() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64IecClk_.store(telemetryPlatform.iec().clk() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64IecData_.store(telemetryPlatform.iec().data() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64IecSrq_.store(telemetryPlatform.iec().srq() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64TapeMotor_.store(telemetryPlatform.tape().motor() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64TapeSense_.store(telemetryPlatform.tape().sense() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64TapeWrite_.store(telemetryPlatform.tape().write() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64TapeRead_.store(telemetryPlatform.tape().read() ? 1u : 0u, std::memory_order_relaxed);
        telemetryC64TapePulseCount_.store(telemetryPlatform.tape().pulseCount(), std::memory_order_relaxed);
        // Play rate = play-calls-this-block / block-duration-in-seconds.
        // Clamped to [0, 500] Hz (well above any realistic C64 rate) to prevent
        // telemetry spikes from single-sample blocks or rate-change transients.
        const double safeSr = (std::isfinite(sampleRate_) && sampleRate_ > 1.0) ? sampleRate_ : 44100.0;
        const float playRate = (frames > 0)
            ? static_cast<float>(std::clamp(
                  static_cast<double>(c64BlockPlayCalls_) * safeSr / static_cast<double>(frames),
                  0.0, 500.0))
            : 0.0f;
        telemetryC64PlayRateHz_.store(std::isfinite(playRate) ? playRate : 0.0f, std::memory_order_relaxed);
        const uint64_t phi2Now = telemetryPlatform.phi2Cycle();
        const bool firstHeavySnapshot = c64LastHeavyTelemetryBlock_ == UINT64_MAX;
        const bool phi2ResetOrWrap = !firstHeavySnapshot &&
            c64LastHeavyTelemetryPhi2_ != UINT64_MAX &&
            phi2Now < c64LastHeavyTelemetryPhi2_;
        const bool sidImageChanged = !firstHeavySnapshot && sidImageHash != c64LastHeavyTelemetrySidHash_;
        const bool psidLiveEdge = !firstHeavySnapshot &&
            ((psidActive ? 1u : 0u) != c64LastHeavyTelemetryPsidActive_);
        // v845 P0 stall fix: SID register-image changes are normal audio-rate
        // activity during PSID/RSID playback — the register image churns almost
        // every block. Treating that churn as an unconditional "heavy snapshot
        // state edge" forced the expensive C64 cockpit snapshot (8 window copies,
        // hashing, 6510 disassembly, string copies, seqlock publish) onto the
        // audio render thread every block, bypassing the demand/deadline gate.
        // That is the strongest concrete match for the play/stop choppiness.
        // The SID register changes stay live via the cheap scalar atomics and the
        // bus scope above; the heavy snapshot now only follows a register edge
        // when a C64/SIDCORE cockpit panel is actually demanding it.
        const bool stateEdgeSnapshot = firstHeavySnapshot || phi2ResetOrWrap || psidLiveEdge ||
            (demandWantsC64Snapshot && sidImageChanged);
        const bool deadlineDue = c64NextHeavyTelemetrySample_ <= blockEndSample;
        c64HeavyTelemetryDemandGated_ = !demandWantsC64Snapshot;
        const bool wantsC64Snapshot =
            c64LastHeavyTelemetryBlock_ != blockIndex_ &&
            (stateEdgeSnapshot || (demandWantsC64Snapshot && deadlineDue));
        if (c64LastHeavyTelemetryBlock_ != blockIndex_ && deadlineDue && !wantsC64Snapshot) {
            ++c64TelemetrySkippedSnapshotCount_;
        }
        if (wantsC64Snapshot && stateEdgeSnapshot && !deadlineDue) {
            ++c64TelemetryForcedSnapshotCount_;
        }
        if (wantsC64Snapshot) {
            const auto* psidHeader = psidLive ? &psidLive->image().header : nullptr;
            {
                ArpSID::C64::C64ChipSnapshot snap = ArpSID::C64::c64BuildTelemetrySnapshot(
                    telemetryPlatform, pal, blockIndex_, c64BlockPlayCalls_, playRate,
                    psidHeader ? psidHeader->name : nullptr,
                    psidHeader ? psidHeader->author : nullptr,
                    psidHeader ? psidHeader->released : nullptr,
                    psidHeader ? psidHeader->songs : 0u,
                    c64PsidCurrentSubtune_.load(std::memory_order_acquire),
                    sidRegs,
                    sidRegCount,
                    blockStartSample,
                    c64NextHeavyTelemetrySample_,
                    blockIndex_,
                    blockEndSample,
                    heavyPeriodSamples,
                    c64TelemetrySkippedSnapshotCount_,
                    c64TelemetryForcedSnapshotCount_,
                    c64TelemetryCatchupClampCount_,
                    c64HeavyTelemetryDemandGated_,
                    true);
                // Fill the full-resolution render-published scope rings into
                // the heavy snapshot before seqlock publish.
                constexpr size_t kSnapLen = ArpSID::C64::C64ChipSnapshot::kBusScopeLen;
                static_assert((kC64BusScopeLen & (kC64BusScopeLen - 1)) == 0, "");
                static_assert((kSnapLen & (kSnapLen - 1)) == 0, "");
                static_assert(kC64BusScopeLen == kSnapLen, "");
                float heat = 0.0f;
                const uint32_t liveWp = c64BusScopeLive_.writePos & static_cast<uint32_t>(kC64BusScopeLen - 1);
                for (size_t i = 0; i < kSnapLen; ++i) {
                    const size_t src = (liveWp + i) & static_cast<size_t>(kC64BusScopeLen - 1);
                    const float os = ArpSIDSanitizeTelemetryBipolarFloat(c64BusScopeLive_.openBus[src]);
                    const float ss = ArpSIDSanitizeTelemetryBipolarFloat(c64BusScopeLive_.sidBus[src]);
                    snap.openBusScope[i] = os;
                    snap.sidBusScope[i] = ss;
                    heat += std::fabs(ss);
                }
                snap.openBusScopeWritePos = 0u;
                snap.sidBusScopeWritePos  = 0u;
                snap.busScopeDecimation = 1u;
                snap.busScopeSourceLen = static_cast<uint8_t>(kC64BusScopeLen);
                snap.busScopeSnapshotLen = static_cast<uint8_t>(kSnapLen);
                snap.sidBusHeat = std::clamp(heat * (1.0f / static_cast<float>(kSnapLen)), 0.0f, 1.0f);
                c64TelemetryGate_.publish(snap);
            }
            c64LastHeavyTelemetryBlock_ = blockIndex_;
            c64LastHeavyTelemetryPhi2_ = phi2Now;
            c64LastHeavyTelemetryAudioPhaseSample_ = blockEndSample;
            c64LastHeavyTelemetrySidHash_ = sidImageHash;
            c64LastHeavyTelemetryPsidActive_ = psidActive ? 1u : 0u;
            uint32_t catchup = 0u;
            while (c64NextHeavyTelemetrySample_ <= blockEndSample && catchup < 4u) {
                c64NextHeavyTelemetrySample_ += heavyPeriodSamples;
                ++catchup;
            }
            if (c64NextHeavyTelemetrySample_ <= blockEndSample) {
                const uint64_t missed = ((blockEndSample - c64NextHeavyTelemetrySample_) / heavyPeriodSamples) + 1u;
                c64NextHeavyTelemetrySample_ += missed * heavyPeriodSamples;
                ++c64TelemetryCatchupClampCount_;
            }
        }
        c64TelemetryHostSampleCursor_ = blockEndSample;
    }

    //──────────────────────────────────────────────────────
    // v550: SIDCORE panel model publish helpers (render thread only)
    //──────────────────────────────────────────────────────

    // Publish one SID register write to the SIDCORE panel model.
    // Called inline at every sreg_().write() site in processBlock.
    // The panel model pointer is loaded with acquire; if nullptr this is a
    // no-op. voiceHint is derived from the SID register layout:
    // 0x00..0x06 → voice 0, 0x07..0x0D → voice 1,
    // 0x0E..0x14 → voice 2, 0x15+ → 0xFF (filter/master).
    // Gate/waveform flags are derived by comparing the incoming byte to the
    // queued shadow (so the caller does NOT need to pass the previous value).
    inline void publishSidCoreRegWrite_(uint8_t reg, uint8_t value,
                                        uint64_t cycleStamp) noexcept {
        ArpSID::GUI::SidCorePanelModel* m =
            sidCorePanelModel_.load(std::memory_order_acquire);
        if (!m) return;
        ArpSID::GUI::SidCoreRegisterWriteEvent ev{};
        ev.sidCycleStamp = cycleStamp;
        ev.registerIndex = reg;
        ev.value         = value;
        ev.voiceHint     = (reg <= 0x06u) ? 0u :
                           (reg <= 0x0Du) ? 1u :
                           (reg <= 0x14u) ? 2u : 0xFFu;
        // Derive flags from control registers ($D404/$D40B/$D412 = regs 4/11/18)
        if (reg == 0x04u || reg == 0x0Bu || reg == 0x12u) {
            const bool prevValid = (reg < (uint8_t)sidQueuedShadow_.valid.size()) &&
                                   (sidQueuedShadow_.valid[reg] != 0u);
            const uint8_t prevCtrl = prevValid ? sidQueuedShadow_.value[reg] : 0u;
            const bool prevGate  = (prevCtrl & 0x01u) != 0u;
            const bool newGate   = (value    & 0x01u) != 0u;
            const bool prevTest  = (prevCtrl & 0x08u) != 0u;
            const bool newTest   = (value    & 0x08u) != 0u;
            const bool wavChg    = ((prevCtrl ^ value) & 0xF0u) != 0u;
            if ( newGate && !prevGate) ev.flags |= ArpSID::GUI::SidCoreEventFlag::kGateOn;
            if (!newGate &&  prevGate) ev.flags |= ArpSID::GUI::SidCoreEventFlag::kGateOff;
            if ( newTest && !prevTest) ev.flags |= ArpSID::GUI::SidCoreEventFlag::kHardRestart;
            if (wavChg)                ev.flags |= ArpSID::GUI::SidCoreEventFlag::kWaveformChange;
        } else if (reg == 0x18u) {
            const bool prevValid = (0x18u < (uint8_t)sidQueuedShadow_.valid.size()) &&
                                   (sidQueuedShadow_.valid[0x18u] != 0u);
            const uint8_t prevMode = prevValid ? (sidQueuedShadow_.value[0x18u] & 0xF0u) : 0u;
            if ((value & 0xF0u) != prevMode)
                ev.flags |= ArpSID::GUI::SidCoreEventFlag::kFilterMode;
        }
        m->publishRegisterWrite(ev);
    }

    // Publish a live per-voice + filter snapshot to the SIDCORE panel model.
    // Called once per render block from processBlock after all writes.
    // Reads the queued register shadow directly — no cross-thread data,
    // since sidQueuedShadow_ is render-thread-private.
    void publishSidCoreLiveSnapshot_() noexcept {
        ArpSID::GUI::SidCorePanelModel* m =
            sidCorePanelModel_.load(std::memory_order_acquire);
        if (!m) return;
        const auto& v = sidQueuedShadow_.value;
        ArpSID::GUI::SidCoreLiveSnapshot snap{};
        // Voice 0 — regs 0x00..0x06
        snap.voiceFrequency[0]  = static_cast<uint16_t>(v[0] | (static_cast<uint16_t>(v[1]) << 8));
        snap.voicePulseWidth[0] = static_cast<uint16_t>(v[2] | (static_cast<uint16_t>(v[3] & 0x0Fu) << 8));
        snap.voiceWaveform[0]   = v[4];
        snap.voiceAD[0]         = v[5];
        snap.voiceSR[0]         = v[6];
        // Voice 1 — regs 0x07..0x0D
        snap.voiceFrequency[1]  = static_cast<uint16_t>(v[7] | (static_cast<uint16_t>(v[8]) << 8));
        snap.voicePulseWidth[1] = static_cast<uint16_t>(v[9] | (static_cast<uint16_t>(v[10] & 0x0Fu) << 8));
        snap.voiceWaveform[1]   = v[11];
        snap.voiceAD[1]         = v[12];
        snap.voiceSR[1]         = v[13];
        // Voice 2 — regs 0x0E..0x14
        snap.voiceFrequency[2]  = static_cast<uint16_t>(v[14] | (static_cast<uint16_t>(v[15]) << 8));
        snap.voicePulseWidth[2] = static_cast<uint16_t>(v[16] | (static_cast<uint16_t>(v[17] & 0x0Fu) << 8));
        snap.voiceWaveform[2]   = v[18];
        snap.voiceAD[2]         = v[19];
        snap.voiceSR[2]         = v[20];
        // Filter — regs 0x15..0x18
        // $D415 bits 2..0 = cutoff[2..0]; $D416 bits 7..0 = cutoff[10..3]
        snap.filterCutoff     = static_cast<uint16_t>((v[21] & 0x07u) |
                                                       (static_cast<uint16_t>(v[22]) << 3));
        snap.filterResRoute   = v[23];   // $D417
        snap.filterModeVolume = v[24];   // $D418
        m->publishLiveSnapshot(snap);
    }

    //──────────────────────────────────────────────────────
    // Telemetry update (called once after processBlock)
    //──────────────────────────────────────────────────────
    void updateC64SidplayTelemetryLight_(float** outputs, int frames) noexcept {
        if (!outputs || !outputs[0] || frames <= 0) return;
        uint32_t telemetryGen = telemetryGeneration_.load(std::memory_order_relaxed);
        if (telemetryGen & 1u) ++telemetryGen;
        telemetryGeneration_.store(telemetryGen + 1u, std::memory_order_release);
        for (const int pid : kTelemetryParamIds_) {
            telemetryParamSnapshot_[(size_t)pid].store(
                ArpSIDSanitizeTelemetryUnitFloat(renderParams_[(size_t)pid]),
                std::memory_order_relaxed);
        }

        SidRuntimeRenderMode mode = sidResolveRenderModeFromLiveParams(renderParams_);
        const ArpSID::C64::C64Runtime* c64Live = c64PsidLive_.load(std::memory_order_acquire);
        if (c64Live) mode = SidRuntimeRenderMode::C64Psid;

        float sumL = 0.0f, sumR = 0.0f, pkL = 0.0f, pkR = 0.0f;
        for (int i = 0; i < frames; ++i) {
            const float cleanL = ArpSID_sanitizeFloat(outputs[0][i]);
            const float cleanR = ArpSID_sanitizeFloat(outputs[1] ? outputs[1][i] : outputs[0][i]);
            const float al = std::fabs(cleanL);
            const float ar = std::fabs(cleanR);
            sumL += al * al;
            sumR += ar * ar;
            if (al > pkL) pkL = al;
            if (ar > pkR) pkR = ar;
        }
        const float rmsL = ArpSIDSanitizeTelemetryUnitFloat(std::sqrt(sumL / std::max(1, frames)));
        const float rmsR = ArpSIDSanitizeTelemetryUnitFloat(std::sqrt(sumR / std::max(1, frames)));
        const float decay = std::max(realtimePowUnit_(telemetryPeakDecayPerSample_, frames), 1.0e-8f);
        float pL = ArpSIDSanitizeTelemetryUnitFloat(telemetryPeakL_.load(std::memory_order_relaxed));
        float pR = ArpSIDSanitizeTelemetryUnitFloat(telemetryPeakR_.load(std::memory_order_relaxed));
        pL = ArpSIDSanitizeTelemetryUnitFloat(std::max(pkL, pL * decay));
        pR = ArpSIDSanitizeTelemetryUnitFloat(std::max(pkR, pR * decay));
        telemetryPeakL_.store(pL, std::memory_order_relaxed);
        telemetryPeakR_.store(pR, std::memory_order_relaxed);
        telemetryRmsL_.store(rmsL, std::memory_order_relaxed);
        telemetryRmsR_.store(rmsR, std::memory_order_relaxed);

        int activeVoiceCount = 0;
        {
            const uint8_t m = sreg_().getScopeActiveMask();
            for (int i = 0; i < 3; ++i) {
                if (m & (1u << i)) ++activeVoiceCount;
            }
        }
        telemetryVoices_.store(std::min(8, activeVoiceCount), std::memory_order_relaxed);
        telemetryArpStep_.store(0, std::memory_order_relaxed);
        for (auto& level : telemetryDrumLevel_) level.store(0.0f, std::memory_order_relaxed);
        for (auto& level : telemetryDrumVoiceLevel_) level.store(0.0f, std::memory_order_relaxed);
        for (auto& level : telemetryGMDrumNoteLevel_) level.store(0.0f, std::memory_order_relaxed);
        telemetryLastDrumNote_.store(-1, std::memory_order_relaxed);
        telemetryLastDrumClass_.store(255, std::memory_order_relaxed);
        telemetryLastDrumVelocity_.store(0.0f, std::memory_order_relaxed);
        telemetrySid808OutputPeak_.store(0.0f, std::memory_order_relaxed);
        telemetrySid808BridgeReplacedOutput_.store(0u, std::memory_order_relaxed);
        clearDigiLayerTelemetryAtomics_(digiD418RuntimeMode());

        const uint8_t* sr = nullptr;
        if (c64Live) {
            sr = c64Live->platform().sidRegisterImage().data();
        } else {
            sr = c64SidBridge_.regs.data();
        }
        if (!sr) sr = sreg_().getRegs().r.data();
        for (int r = 0; r < kSidRegCount && r < kTelSidRegCount; ++r) {
            telemetrySidRegs_[r].store(sr[r], std::memory_order_relaxed);
        }
        for (int v = 0; v < 3; ++v) {
            telemetryVoiceEnvLevel_[(size_t)v].store(
                ArpSIDSanitizeTelemetryUnitFloat(sreg_().getVoiceEnvelopeLevel(v)),
                std::memory_order_relaxed);
        }

        publishC64PlatformMirrorTelemetry_(sr, std::min(kSidRegCount, kTelSidRegCount), frames);
        c64BusScopeLive_.frameId = blockIndex_;
        telemetryC64BusScopeTriple_.writeSlot() = c64BusScopeLive_;
        telemetryC64BusScopeTriple_.publish();

        telemetryHostTempo_.store(ArpSIDSanitizeTelemetryTempo(runtimeHostSurface_().hostTempo), std::memory_order_relaxed);
        telemetryHostBeat_.store(ArpSIDSanitizeTelemetryBeat(runtimeHostSurface_().hostBeatPosition), std::memory_order_relaxed);
        telemetryHostPlaying_.store(runtimeHostSurface_().transportPlaying ? 1u : 0u, std::memory_order_relaxed);
        const uint64_t telemetrySampleEnd = c64TelemetryHostSampleCursor_;
        const uint64_t telemetryFrameCount = static_cast<uint64_t>(std::max(0, frames));
        const uint64_t telemetrySampleStart =
            telemetrySampleEnd >= telemetryFrameCount ? telemetrySampleEnd - telemetryFrameCount : 0u;
        telemetryHostSampleStart_.store(telemetrySampleStart, std::memory_order_relaxed);
        telemetryHostSampleEnd_.store(telemetrySampleEnd, std::memory_order_relaxed);
        telemetryRenderMode_.store(static_cast<uint8_t>(mode), std::memory_order_relaxed);
        telemetrySynthMode_.store(0u, std::memory_order_relaxed);
        telemetryDrSidMode_.store(0u, std::memory_order_relaxed);
        telemetryPsidActive_.store(mode == SidRuntimeRenderMode::C64Psid ? 1u : 0u, std::memory_order_relaxed);
        telemetryDrSidPlaybackMode_.store(drs_() ? static_cast<uint8_t>(drs_()->drSidPlaybackMode()) : 0u,
                                          std::memory_order_relaxed);
        telemetryArpEnabled_.store(ArpSID::sidEffectiveArpAuthorityFromLiveParams(renderParams_) ? 1u : 0u,
                                   std::memory_order_relaxed);
        telemetrySeqEnabled_.store(ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_) ? 1u : 0u,
                                   std::memory_order_relaxed);
        telemetryArpFollowHost_.store(runtimeModel_.followHostTempoArp() ? 1u : 0u, std::memory_order_relaxed);
        telemetrySeqFollowHost_.store(runtimeModel_.followHostTempoSeq() ? 1u : 0u, std::memory_order_relaxed);
        telemetrySeqStep_.store(runtimeModel_.seqStep(), std::memory_order_relaxed);
        const float rawInternalSeqTempo = ArpSID_sanitizeFloat(seqTempoBpmFromNorm_(renderParams_[(size_t)kParamSeqTempo]));
        const float internalSeqTempo = std::clamp(rawInternalSeqTempo > 0.0f ? rawInternalSeqTempo : 120.0f, 20.0f, 400.0f);
        const float hostSeqTempo = (runtimeHostSurface_().hostTempo > 1.0)
            ? (float)ArpSIDSanitizeTelemetryTempo(runtimeHostSurface_().hostTempo)
            : internalSeqTempo;
        telemetrySeqTempoBpm_.store(runtimeModel_.followHostTempoSeq() ? hostSeqTempo : internalSeqTempo,
                                    std::memory_order_relaxed);
        telemetrySidModel_.store((runtimeModel_.variantProfile().family == SidFamily::MOS8580) ? 1 : 0,
                                 std::memory_order_relaxed);
        c64VideoStandardFallbackAtomic_.store(
            (runtimeModel_.variantProfile().video_standard == ArpSID::SidVideoStandard::NTSC) ? 1u : 0u,
            std::memory_order_release);
        telemetryFrameId_.store(blockIndex_, std::memory_order_relaxed);
        telemetryGeneration_.store(telemetryGen + 2u, std::memory_order_release);
    }

    void updateTelemetry_(float** outputs, int frames) noexcept {
        uint32_t telemetryGen = telemetryGeneration_.load(std::memory_order_relaxed);
        if (telemetryGen & 1u) ++telemetryGen;
        telemetryGeneration_.store(telemetryGen + 1u, std::memory_order_release);
        for (const int pid : kTelemetryParamIds_) {
            telemetryParamSnapshot_[(size_t)pid].store(
                ArpSIDSanitizeTelemetryUnitFloat(renderParams_[(size_t)pid]),
                std::memory_order_relaxed);
        }
        auto mode = sidResolveRenderModeFromLiveParams(renderParams_);
        if (c64PsidLive_.load(std::memory_order_acquire) != nullptr) {
            // normal synth params resolve to BitPerfect while C64Psid is live;
            // the loaded PSID/RSID runtime is the audio authority, so scopes and
            // telemetry deliberately override the normal render-mode resolver.
            mode = SidRuntimeRenderMode::C64Psid;
        }
        float sumL=0.f, sumR=0.f, pkL=0.f, pkR=0.f;
        for (int i=0;i<frames;++i){
            const float cleanL = ArpSID_sanitizeFloat(outputs[0][i]);
            const float cleanR = ArpSID_sanitizeFloat(outputs[1] ? outputs[1][i] : outputs[0][i]);
            const float al=std::fabs(cleanL), ar=std::fabs(cleanR);
            sumL+=al*al; sumR+=ar*ar;
            if (al > pkL) pkL = al;
            if (ar > pkR) pkR = ar;
        }
        const float rmsL=ArpSIDSanitizeTelemetryUnitFloat(std::sqrt(sumL/std::max(1,frames)));
        const float rmsR=ArpSIDSanitizeTelemetryUnitFloat(std::sqrt(sumR/std::max(1,frames)));
        const float decay = std::max(realtimePowUnit_(telemetryPeakDecayPerSample_, frames), 1.0e-8f);
        float pL=ArpSIDSanitizeTelemetryUnitFloat(telemetryPeakL_.load(std::memory_order_relaxed));
        float pR=ArpSIDSanitizeTelemetryUnitFloat(telemetryPeakR_.load(std::memory_order_relaxed));
        pL=ArpSIDSanitizeTelemetryUnitFloat(std::max(pkL,pL*decay));
        pR=ArpSIDSanitizeTelemetryUnitFloat(std::max(pkR,pR*decay));
        telemetryPeakL_.store(pL, std::memory_order_relaxed);
        telemetryPeakR_.store(pR, std::memory_order_relaxed);
        telemetryRmsL_.store(rmsL, std::memory_order_relaxed);
        telemetryRmsR_.store(rmsR, std::memory_order_relaxed);
        int activeVoiceCount = 0;
        const bool sid808BridgeAuthority =
            componentFlavor_ == ArpSID::ComponentFlavor::Sid808 &&
            drumEngineBridge_.activeIdentity().context == ArpSID::DrumContext::SID808_AnalogProjection;
        if (sid808BridgeAuthority) {
            activeVoiceCount = drumEngineBridge_.sid808Engine().activeVoiceCount();
        } else if (mode == SidRuntimeRenderMode::DrSid && drs_()) {
            if (drs_()->isActive()) {
                const uint8_t m = drs_()->getScopeActiveMask();
                int n = 0; for (int i = 0; i < 3; ++i) if (m & (1u << i)) ++n;
                activeVoiceCount = std::max(drs_()->getActiveVoiceCount(), std::max(1, n));
            }
        } else if (mode == SidRuntimeRenderMode::SidRegister || mode == SidRuntimeRenderMode::C64Psid) {
            const uint8_t m = sreg_().getScopeActiveMask();
            int n = 0; for (int i = 0; i < 3; ++i) if (m & (1u << i)) ++n;
            activeVoiceCount = std::max(activeVoiceCount, n);
        } else if (bpe_()) {
            activeVoiceCount = bpe_()->getActiveVoiceCount();
        }
        const int digiTelemetryVoices = (resolveDigiAuthMode_() == ArpSID::DigiAuthMode::LegacyFloatLayer)
            ? static_cast<int>(digiSampler_.telemetry().playingVoiceCount)
            : static_cast<int>(digiD418_.telemetry().playingVoiceCount);
        activeVoiceCount = std::min(8, activeVoiceCount + digiTelemetryVoices);
        telemetryVoices_.store(activeVoiceCount, std::memory_order_relaxed);
        if(arp_() && runtimeIsArpEnabled())
            telemetryArpStep_.store(arp_()->getCurrentStep(), std::memory_order_relaxed);
        else
            telemetryArpStep_.store(0, std::memory_order_relaxed);
        if (sid808BridgeAuthority) {
            float drumLevels[DrSidEngine::kDrumTypeCount]{};
            drumEngineBridge_.sid808Engine().copyDrumLevels(drumLevels, DrSidEngine::kDrumTypeCount);
            for (int i = 0; i < DrSidEngine::kDrumTypeCount; ++i)
                telemetryDrumLevel_[(size_t)i].store(ArpSIDSanitizeTelemetryUnitFloat(drumLevels[i]), std::memory_order_relaxed);
            float drumVoiceLevels[3]{};
            drumEngineBridge_.sid808Engine().copyVoiceLevels(drumVoiceLevels, 3);
            for (int i = 0; i < 3; ++i)
                telemetryDrumVoiceLevel_[(size_t)i].store(ArpSIDSanitizeTelemetryUnitFloat(drumVoiceLevels[i]), std::memory_order_relaxed);
            for (auto& level : telemetryGMDrumNoteLevel_) level.store(0.0f, std::memory_order_relaxed);
            telemetryLastDrumNote_.store(drumEngineBridge_.sid808Engine().lastMidiNote(), std::memory_order_relaxed);
            const int lastClass = drumEngineBridge_.sid808Engine().lastDrumClass();
            telemetryLastDrumClass_.store(
                (lastClass >= 0 && lastClass < static_cast<int>(Sid808Drum::Count)) ? lastClass : 255,
                std::memory_order_relaxed);
            telemetryLastDrumVelocity_.store(
                ArpSIDSanitizeTelemetryUnitFloat(drumEngineBridge_.sid808Engine().lastVelocity()),
                std::memory_order_relaxed);
        } else if (drs_()) {
            float drumLevels[DrSidEngine::kDrumTypeCount]{};
            drs_()->copyDrumLevels(drumLevels, DrSidEngine::kDrumTypeCount);
            for (int i = 0; i < DrSidEngine::kDrumTypeCount; ++i)
                telemetryDrumLevel_[(size_t)i].store(ArpSIDSanitizeTelemetryUnitFloat(drumLevels[i]), std::memory_order_relaxed);
            float drumVoiceLevels[3]{};
            drs_()->copyVoiceLevels(drumVoiceLevels, 3);
            for (int i = 0; i < 3; ++i)
                telemetryDrumVoiceLevel_[(size_t)i].store(ArpSIDSanitizeTelemetryUnitFloat(drumVoiceLevels[i]), std::memory_order_relaxed);
            float gmNoteLevels[DrSidEngine::kGMDrumNoteCount]{};
            drs_()->copyGMDrumNoteLevels(gmNoteLevels, DrSidEngine::kGMDrumNoteCount);
            for (int i = 0; i < DrSidEngine::kGMDrumNoteCount; ++i)
                telemetryGMDrumNoteLevel_[(size_t)i].store(ArpSIDSanitizeTelemetryUnitFloat(gmNoteLevels[i]), std::memory_order_relaxed);
            telemetryLastDrumNote_.store(drs_()->lastGMDrumNote(), std::memory_order_relaxed);
            telemetryLastDrumClass_.store(drs_()->lastGMDrumClass(), std::memory_order_relaxed);
            telemetryLastDrumVelocity_.store(ArpSIDSanitizeTelemetryUnitFloat(drs_()->lastGMDrumVelocity()), std::memory_order_relaxed);
        } else {
            for (auto& level : telemetryDrumLevel_) level.store(0.0f, std::memory_order_relaxed);
            for (auto& level : telemetryDrumVoiceLevel_) level.store(0.0f, std::memory_order_relaxed);
            for (auto& level : telemetryGMDrumNoteLevel_) level.store(0.0f, std::memory_order_relaxed);
            telemetryLastDrumNote_.store(-1, std::memory_order_relaxed);
            telemetryLastDrumClass_.store(255, std::memory_order_relaxed);
            telemetryLastDrumVelocity_.store(0.0f, std::memory_order_relaxed);
        }
        publishDigiSamplerTelemetry_();
        {
            uint32_t wp = mainOscLiveWritePos_ & (kOscBufLen - 1);
            for(int i=0;i<frames;++i){
                mainOscLive_[wp]=ArpSIDSanitizeTelemetryBipolarFloat(
                    0.5f*(outputs[0][i]+(outputs[1] ? outputs[1][i] : outputs[0][i])));
                wp=(wp+1)&(kOscBufLen-1);}
            mainOscLiveWritePos_ = wp;
            auto& mainScope = mainOscScopeTriple_.writeSlot();
            std::memcpy(mainScope.mono, mainOscLive_, sizeof(mainOscLive_));
            mainScope.writePos = wp;
            mainScope.frameId = blockIndex_;
            mainOscScopeTriple_.publish();
        }
        {
            const uint8_t* sr = nullptr;
            if (mode == SidRuntimeRenderMode::DrSid && drs_()) {
                sr = drs_()->getRegisterImage().data();
            } else if (mode == SidRuntimeRenderMode::C64Psid) {
                if (const ArpSID::C64::C64Runtime* p = c64PsidLive_.load(std::memory_order_acquire)) {
                    sr = p->platform().sidRegisterImage().data();
                } else {
                    sr = c64SidBridge_.regs.data();
                }
            } else if (mode == SidRuntimeRenderMode::SidRegister) {
                sr = sreg_().getRegs().r.data();
            } else if (bpe_()) {
                sr = bpe_()->getPrimaryRegImage().data();
            }
            if (!sr) sr = sreg_().getRegs().r.data();
            for(int r=0;r<kSidRegCount&&r<kTelSidRegCount;++r)
                telemetrySidRegs_[r].store(sr[r],std::memory_order_relaxed);

            float envLevels[3]{};
            if (mode == SidRuntimeRenderMode::DrSid && drs_()) {
                for (int v = 0; v < 3; ++v) envLevels[v] = drs_()->getVoiceEnvelopeLevel(v);
            } else if (mode == SidRuntimeRenderMode::SidRegister || mode == SidRuntimeRenderMode::C64Psid) {
                for (int v = 0; v < 3; ++v) envLevels[v] = sreg_().getVoiceEnvelopeLevel(v);
            } else if (bpe_()) {
                for (int v = 0; v < 3; ++v) envLevels[v] = bpe_()->getVoiceEnvelopeLevel(v);
            }
            for (int v = 0; v < 3; ++v)
                telemetryVoiceEnvLevel_[(size_t)v].store(ArpSIDSanitizeTelemetryUnitFloat(envLevels[v]), std::memory_order_relaxed);

            publishC64PlatformMirrorTelemetry_(sr, std::min(kSidRegCount, kTelSidRegCount), frames);
            c64BusScopeLive_.frameId = blockIndex_;
            telemetryC64BusScopeTriple_.writeSlot() = c64BusScopeLive_;
            telemetryC64BusScopeTriple_.publish();
        }

        // Telemetry invariant: publish runtime/voice/host state from the audio
        // thread into atomics. UI reads never touch live mutable runtime tables.
        telemetryHostTempo_.store(ArpSIDSanitizeTelemetryTempo(runtimeHostSurface_().hostTempo), std::memory_order_relaxed);
        telemetryHostBeat_.store(ArpSIDSanitizeTelemetryBeat(runtimeHostSurface_().hostBeatPosition), std::memory_order_relaxed);
        telemetryHostPlaying_.store(runtimeHostSurface_().transportPlaying ? 1u : 0u, std::memory_order_relaxed);
        const uint64_t telemetrySampleEnd = c64TelemetryHostSampleCursor_;
        const uint64_t telemetryFrameCount = static_cast<uint64_t>(std::max(0, frames));
        const uint64_t telemetrySampleStart =
            telemetrySampleEnd >= telemetryFrameCount ? telemetrySampleEnd - telemetryFrameCount : 0u;
        telemetryHostSampleStart_.store(telemetrySampleStart, std::memory_order_relaxed);
        telemetryHostSampleEnd_.store(telemetrySampleEnd, std::memory_order_relaxed);
        telemetryRenderMode_.store(static_cast<uint8_t>(mode), std::memory_order_relaxed);
        telemetrySynthMode_.store(mode == SidRuntimeRenderMode::SidRegister ? 1u : 0u, std::memory_order_relaxed);
        telemetryDrSidMode_.store(mode == SidRuntimeRenderMode::DrSid ? 1u : 0u, std::memory_order_relaxed);
        telemetryPsidActive_.store(mode == SidRuntimeRenderMode::C64Psid ? 1u : 0u, std::memory_order_relaxed);
        telemetryDrSidPlaybackMode_.store(drs_() ? static_cast<uint8_t>(drs_()->drSidPlaybackMode()) : 0u,
                                          std::memory_order_relaxed);
        telemetryArpEnabled_.store(ArpSID::sidEffectiveArpAuthorityFromLiveParams(renderParams_) ? 1u : 0u,
                                   std::memory_order_relaxed);
        telemetrySeqEnabled_.store(ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_) ? 1u : 0u,
                                   std::memory_order_relaxed);
        telemetryArpFollowHost_.store(runtimeModel_.followHostTempoArp() ? 1u : 0u, std::memory_order_relaxed);
        telemetrySeqFollowHost_.store(runtimeModel_.followHostTempoSeq() ? 1u : 0u, std::memory_order_relaxed);
        telemetrySeqStep_.store(runtimeModel_.seqStep(), std::memory_order_relaxed);
        const float rawInternalSeqTempo = ArpSID_sanitizeFloat(seqTempoBpmFromNorm_(renderParams_[(size_t)kParamSeqTempo]));
        const float internalSeqTempo = std::clamp(rawInternalSeqTempo > 0.0f ? rawInternalSeqTempo : 120.0f, 20.0f, 400.0f);
        const float hostSeqTempo = (runtimeHostSurface_().hostTempo > 1.0) ? (float)ArpSIDSanitizeTelemetryTempo(runtimeHostSurface_().hostTempo) : internalSeqTempo;
        telemetrySeqTempoBpm_.store(runtimeModel_.followHostTempoSeq() ? hostSeqTempo : internalSeqTempo, std::memory_order_relaxed);
        telemetrySidModel_.store((runtimeModel_.variantProfile().family == SidFamily::MOS8580) ? 1 : 0, std::memory_order_relaxed);
        // audit P0.2: publish the render-owned video standard so the async PSID
        // loader can read it atomically instead of touching runtimeModel_.
        c64VideoStandardFallbackAtomic_.store(
            (runtimeModel_.variantProfile().video_standard == ArpSID::SidVideoStandard::NTSC) ? 1u : 0u,
            std::memory_order_release);
        telemetryEnv1Level_.store(ArpSIDSanitizeTelemetryUnitFloat(runtimeModel_.env1Level()), std::memory_order_relaxed);
        telemetryLastNoteVelocity_.store(ArpSIDSanitizeTelemetryUnitFloat(runtimeModel_.lastNoteVelocity()), std::memory_order_relaxed);
        telemetryModWheelNorm_.store(ArpSIDSanitizeTelemetryUnitFloat(runtimeModel_.modWheelNorm()), std::memory_order_relaxed);
        telemetryFocusedPitchBend_.store(ArpSIDSanitizeTelemetryBipolarFloat(runtimeModel_.focusedPitchBendNorm()), std::memory_order_relaxed);
        telemetryFocusedChannelPressure_.store(ArpSIDSanitizeTelemetryUnitFloat(runtimeModel_.focusedChannelPressureBipolar() * 0.5f + 0.5f), std::memory_order_relaxed);
        telemetryFocusedPolyPressure_.store(ArpSIDSanitizeTelemetryUnitFloat(runtimeModel_.focusedPolyPressureBipolar() * 0.5f + 0.5f), std::memory_order_relaxed);
        telemetryRandomValue_.store(ArpSIDSanitizeTelemetryBipolarFloat(runtimeModel_.randomBipolar()), std::memory_order_relaxed);
        publishMpkMiniTelemetryFromEffectiveParams_();
        publishForensicTelemetry_(resolveEffectiveForensicConfigForBlock_());
        const uint64_t focusedToken = runtimeModel_.focusedVoiceToken();
        int tokenIdx = 0;
        int activeTokenCount = 0;
        runtimeModel_.forEachActiveTokenVoice([&](const auto& e) noexcept {
            ++activeTokenCount;
            if (tokenIdx >= (int)telemetryTokens_.size()) return;
            auto& cell = telemetryTokens_[(size_t)tokenIdx++];
            cell.token.store(e.token.token, std::memory_order_relaxed);
            cell.note.store(std::clamp<int>(e.token.note, 0, 127), std::memory_order_relaxed);
            cell.channel.store(std::clamp<int>(e.token.channel, 0, 15), std::memory_order_relaxed);
            cell.velocity.store(ArpSIDSanitizeTelemetryUnitFloat(e.velocity), std::memory_order_relaxed);
            cell.polyPressure.store(ArpSIDSanitizeTelemetryUnitFloat(e.polyPressure), std::memory_order_relaxed);
            cell.sustained.store(e.sustained ? 1u : 0u, std::memory_order_relaxed);
            cell.sostenuto.store(e.sostenutoLatched ? 1u : 0u, std::memory_order_relaxed);
            cell.focused.store((focusedToken != 0 && focusedToken == e.token.token) ? 1u : 0u, std::memory_order_relaxed);
        });
        telemetryActiveTokenCount_.store(activeTokenCount, std::memory_order_relaxed);
        for (size_t i = (size_t)tokenIdx; i < telemetryTokens_.size(); ++i) {
            auto& cell = telemetryTokens_[i];
            cell.token.store(0, std::memory_order_relaxed);
            cell.note.store(0, std::memory_order_relaxed);
            cell.channel.store(0, std::memory_order_relaxed);
            cell.velocity.store(0.0f, std::memory_order_relaxed);
            cell.polyPressure.store(0.0f, std::memory_order_relaxed);
            cell.sustained.store(0, std::memory_order_relaxed);
            cell.sostenuto.store(0, std::memory_order_relaxed);
            cell.focused.store(0, std::memory_order_relaxed);
        }
        if (lfos_()) {
            for (int i = 0; i < 4; ++i) {
                const auto& lfo = lfos_()->getLFO(i);
                telemetryLfoValue_[(size_t)i].store(std::clamp(lfo.getValue(), -1.0f, 1.0f), std::memory_order_relaxed);
                telemetryLfoPhase_[(size_t)i].store(std::clamp(static_cast<float>(lfo.getPhase()), 0.0f, 1.0f), std::memory_order_relaxed);
            }
        } else {
            for (int i = 0; i < 4; ++i) {
                telemetryLfoValue_[(size_t)i].store(0.0f, std::memory_order_relaxed);
                telemetryLfoPhase_[(size_t)i].store(0.0f, std::memory_order_relaxed);
            }
        }
        // Publish every render block while a visible consumer is holding scope
        // demand. This guarantees the first block after a tab/popout request is
        // not suppressed by a cadence divider.
        if (consumePresentationScopeDemand_(frames))
            publishTelemetryScopeSnapshot_(mode);
        telemetryFrameId_.store(blockIndex_, std::memory_order_relaxed);
        telemetryGeneration_.store(telemetryGen + 2u, std::memory_order_release);
    }

    //──────────────────────────────────────────────────────
    void createEngines_() {
        engineBank_.create(sampleRate_);
        if (drs_()) drumEngineBridge_.bindCanonicalDrSidEngine(*drs_());
        audioEngineModeRender_ = 0xFFu;
        syncSidQueuedShadowFromLive_();
    }

    void applyPendingAudioEngineMode_() noexcept {
        const std::uint8_t requested =
            requestedAudioEngineMode_.load(std::memory_order_acquire) == 1u ? 1u : 0u;
        if (!bpe_() || audioEngineModeRender_ == requested) return;
        const auto topology = requested == 1u
            ? ArpSID::BitPerfectEngine::SidChipTopologyMode::SingleChip3Voice
            : ArpSID::BitPerfectEngine::SidChipTopologyMode::MultiChipPolyIllusion;
        bpe_()->setSidChipTopologyMode(topology);
        audioEngineModeRender_ = requested;
    }

    //──────────────────────────────────────────────────────
    bool flushDirtyParams_(bool forceAll) noexcept {
        bool changed = false;
        const auto modeBeforeFlush = ArpSID::sidResolveRenderModeFromLiveParams(renderParams_);
        for (int i = 0; i < kNumParams; ++i) {
            // exchange avoids clearing a new producer's dirty publication after
            // this consumer has inspected the slot.
            const bool wasDirty = dirty_[(size_t)i].exchange(false, std::memory_order_acq_rel);
            if (forceAll || wasDirty) {
                const uint64_t fallbackPacked =
                    paramIntentFallbackPacked_[(size_t)i].exchange(0u, std::memory_order_acq_rel);
                const bool hasIntentFallback = fallbackPacked != 0u;
                const float raw = hasIntentFallback
                    ? unpackParamIntentFallbackValue_(fallbackPacked)
                    : params_[(size_t)i].load(std::memory_order_relaxed);
                const float clean = ArpSID::sanitizeNormalizedParamValue(
                    i,
                    raw,
                    ArpSID::defaultNormalizedParamValue(i));
                params_[(size_t)i].store(clean, std::memory_order_relaxed);
                if (hasIntentFallback) {
                    // Publish the generation before the async queue is drained;
                    // queued events at or before this dropped latest-value intent
                    // are obsolete. Crucially, use the ordinary execution owner
                    // so tempo-follow, limiter/FX, mode transitions and every
                    // other adapter side effect are identical to normal intent.
                    paramIntentAppliedFallbackGeneration_[(size_t)i].store(
                        unpackParamIntentFallbackGeneration_(fallbackPacked),
                        std::memory_order_release);
                    if (runtimeExecutionOwner_) {
                        runtimeExecutionOwner_->applyProjectedNormalizedParameter(
                            static_cast<uint32_t>(i), clean);
                    } else {
                        renderParams_[(size_t)i] = clean;
                        (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(i), clean);
                    }
                } else {
                    // Non-ingress dirtiness (initialization/state restore) is an
                    // already-reconciled snapshot and only needs coherent staging.
                    renderParams_[(size_t)i] = clean;
                    (void)runtimeModel_.applyAutomationPoint(static_cast<uint32_t>(i), clean);
                }
                changed = true;
            }
        }
        changed = enforceComponentFlavorPolicy_() || changed;
        // Strict mode-flag sanitization: after all param flushes and flavor
        // enforcement, exactly one mode must be active. v943 mirrors sanitized
        // mode bits back through canonical staging so params_ and runtimeModel_
        // cannot keep the illegal DrSID+SynthMode combination after renderParams_
        // has been corrected.
        const float beforeDrSid = renderParams_[(size_t)kParamDrSidEnable];
        const float beforeSynth = renderParams_[(size_t)kParamSynthModeEnable];
        sidSanitizeRenderModeParamsRT(renderParams_);
        if (std::fabs(beforeDrSid - renderParams_[(size_t)kParamDrSidEnable]) > 1.0e-6f ||
            std::fabs(params_[(size_t)kParamDrSidEnable].load(std::memory_order_relaxed) - renderParams_[(size_t)kParamDrSidEnable]) > 1.0e-6f ||
            std::fabs(ArpSID::sidStateRootParamValue(runtimeModel_.stateRoot(), kParamDrSidEnable) - renderParams_[(size_t)kParamDrSidEnable]) > 1.0e-6f) {
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamDrSidEnable), renderParams_[(size_t)kParamDrSidEnable]);
            changed = true;
        }
        if (std::fabs(beforeSynth - renderParams_[(size_t)kParamSynthModeEnable]) > 1.0e-6f ||
            std::fabs(params_[(size_t)kParamSynthModeEnable].load(std::memory_order_relaxed) - renderParams_[(size_t)kParamSynthModeEnable]) > 1.0e-6f ||
            std::fabs(ArpSID::sidStateRootParamValue(runtimeModel_.stateRoot(), kParamSynthModeEnable) - renderParams_[(size_t)kParamSynthModeEnable]) > 1.0e-6f) {
            runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSynthModeEnable), renderParams_[(size_t)kParamSynthModeEnable]);
            changed = true;
        }
        const auto mode = ArpSID::sidResolveRenderModeFromLiveParams(renderParams_);
        const bool structuralModeReturnToBitPerfect =
            modeBeforeFlush != mode &&
            mode == ArpSID::SidRuntimeRenderMode::BitPerfect &&
            (modeBeforeFlush == ArpSID::SidRuntimeRenderMode::SidRegister ||
             modeBeforeFlush == ArpSID::SidRuntimeRenderMode::DrSid);
        {
            // v949: ARP and SEQ are different authority laws. ARP is blocked
            // outside BitPerfect. SEQ is blocked in SynthMode/SID-register, but
            // remains valid for DrSID/SID808 drum pattern transport. Same-block
            // structural return to pure Classic still clears both so host order
            // cannot revive old secondaries on the transition block.
            const bool modeBlocksArp = mode != ArpSID::SidRuntimeRenderMode::BitPerfect;
            const bool modeBlocksSeq = mode == ArpSID::SidRuntimeRenderMode::SidRegister;
            if (modeBlocksArp || structuralModeReturnToBitPerfect) {
                if (renderParams_[(size_t)kParamArpEnable] > 0.5f ||
                    params_[(size_t)kParamArpEnable].load(std::memory_order_relaxed) > 0.5f) {
                    runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamArpEnable), 0.0f);
                    if (arp_()) { arp_()->allNotesOff(); arp_()->setEnabled(false); }
                    runtimeModel_.setArpActiveFlag(false);
                    changed = true;
                }
            }
            if (modeBlocksSeq || structuralModeReturnToBitPerfect) {
                if (renderParams_[(size_t)kParamSeqEnable] > 0.5f ||
                    params_[(size_t)kParamSeqEnable].load(std::memory_order_relaxed) > 0.5f) {
                    runtimeStageNormalizedParameterOnly(static_cast<uint32_t>(kParamSeqEnable), 0.0f);
                    runtimeModel_.setSeqLastNote(-1);
                    runtimeModel_.setSeqSamplesUntilStep(-1.0);
                    runtimeModel_.setSeqStep(0);
                    seqEngine_.resetPhase();
                    prevSeqEnabled_ = false;
                    changed = true;
                }
            }
        }
        return changed;
    }

    //──────────────────────────────────────────────────────
    // Apply render snapshot to engines

    //──────────────────────────────────────────────────────
    // LFO + Mod Matrix (mirror VST3 process order)
    //──────────────────────────────────────────────────────

    void processModMatrix_(int /*frames*/) noexcept {
        runtimeModel_.advanceRandomScope();
        runtimeModel_.resolveRandomForCurrentScope();
        if (bpe_()) {
            applyTypedModRoutesToBitPerfect(runtimeModel_, bpe_(), renderParams_, lfos_());
            applyParamMatrixToBitPerfect(runtimeModel_, bpe_(), renderParams_);
        }
        if (drs_()) {
            applyParamMatrixToDrSid(runtimeModel_, drs_(), renderParams_);
        }
    }
    // appendSequencerCanonicalEvents_ and processSequencer_ removed (P3-2 cleanup).
    // All sequencer work now goes through seqEngine_.advanceWindow() inside processBlock().

    //──────────────────────────────────────────────────────
    // Synth Mode: tick the SidRegisterEngine sample-by-sample
};

} // namespace ArpSID
