#pragma once
#include "sid_variant_profile.h"
#include "sid_runtime_sizing.h"
#include "sid_static_params.h"
#include "sid_dynamic_state.h"
#include "sid_measured_posterior.h"
#include "sid_register_image.h"
#include "sid_event_queue.h"
#include "sid_serializer_schema.h"
#include "sid_runtime_backend.h"
#include "sid_runtime_midi_ops.h"
#include "sid_runtime_state_root_presentation.h"
#include "sid_ingress_lane.h"
#include "sid_ingress_merge.h"
#include "ingress_fallback_edge_ring.h"
#include "sid_host_cycle_dispatcher.h"
#include "parameter_ids.h"
#include <utility>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

namespace ArpSID {


// SidRuntimeRenderMode — canonical parameter-driven render authority.
//
// SINGLE-SOURCE RULE: sidResolveRenderModeFromLiveParams() is the ONLY
// place that maps live parameters to a mode value. All render-path
// branches switch on this result. No code may compare against a mode value
// derived from a different source.
//
// C64SidPlayer is a ComponentFlavor, NOT a render mode. The C64 SID player
// forces kParamDrSidEnable=0 / kParamSynthModeEnable=0 via
// enforceComponentFlavorPolicy_(), so the resolver returns BitPerfect.
// Callers that need to know whether the C64 SID player is active must query
// the flavor, not the render mode.
enum class SidRuntimeRenderMode : uint8_t {
    BitPerfect  = 0,  ///< Play engine (poly/mono/unison), C64 SID registers via BitPerfectEngine
    SidRegister = 1,  ///< Direct SID register authority (SidRegisterEngine)
    DrSid       = 2,  ///< Drum-machine / SID-808 engine authority (DrSidEngine / Sid808Engine)
    // C64Psid = 3 is a TELEMETRY SENTINEL ONLY.
    //
    // The resolver (sidResolveRenderModeFromLiveParams) NEVER returns this value;
    // C64SidPlayer is a ComponentFlavor that forces both enable flags to 0, so the
    // resolver always returns BitPerfect during C64 playback.
    //
    // The telemetry path (updateTelemetry_) detects c64PsidLive_ != nullptr and sets
    // mode = C64Psid so scope/register/envelope readback can choose the correct source.
    // This is valid ONLY inside updateTelemetry_; all render-path switches must use
    // the resolver result or the component flavor, never C64Psid directly.
    C64Psid     = 3,  ///< Telemetry sentinel — C64 SID player active (NOT a render-path mode)
};

// Single authority for resolving render mode from live params.
// Enforces mutual exclusivity: at most one enable flag is honoured.
// The priority order (DrSid > SidRegister > BitPerfect) matches the flavour
// enforcement in enforceComponentFlavorPolicy_() so disabled modes are never
// accidentally promoted.
template <class ParamContainer>
inline SidRuntimeRenderMode sidResolveRenderModeFromLiveParams(const ParamContainer& params) noexcept {
    if (params[(size_t)kParamDrSidEnable]      > 0.5f) return SidRuntimeRenderMode::DrSid;
    if (params[(size_t)kParamSynthModeEnable]  > 0.5f) return SidRuntimeRenderMode::SidRegister;
    return SidRuntimeRenderMode::BitPerfect;
}

// Returns true when exactly one render mode flag is active (no boolean priority accident).
template <class ParamContainer>
inline bool sidRenderModeParamsAreSane(const ParamContainer& params) noexcept {
    const bool drSid  = params[(size_t)kParamDrSidEnable]     > 0.5f;
    const bool synth  = params[(size_t)kParamSynthModeEnable] > 0.5f;
    return !(drSid && synth);  // both true simultaneously is a priority accident
}

// Returns true when ARP is an effective note authority for the live param image.
// v939: CLASSIC / BitPerfect is now a first-class authority mode, so ARP is
// considered active only as an explicit secondary authority inside BitPerfect.
// Stale raw ArpEnable must not leak into SynthMode/SID-register or DrSID.
template <class ParamContainer>
inline bool sidEffectiveArpAuthorityFromLiveParams(const ParamContainer& params) noexcept {
    return params[(size_t)kParamArpEnable] > 0.5f &&
           sidResolveRenderModeFromLiveParams(params) == SidRuntimeRenderMode::BitPerfect;
}

// Returns true when SEQ is an effective performance authority for the live param image.
// v949: SEQ is not the same law as ARP. ARP is a melodic secondary authority
// and remains BitPerfect-only; SEQ is also the DrSID/SID808 drum pattern
// transport authority. It is blocked in SynthMode/SID-register and C64/player
// policy layers, but remains valid in DrSID/SID808.
template <class ParamContainer>
inline bool sidEffectiveSeqAuthorityFromLiveParams(const ParamContainer& params) noexcept {
    if (params[(size_t)kParamSeqEnable] <= 0.5f) return false;
    const auto mode = sidResolveRenderModeFromLiveParams(params);
    return mode == SidRuntimeRenderMode::BitPerfect ||
           mode == SidRuntimeRenderMode::DrSid;
}


class SidRuntimeModel {
private:
    void syncVariantPresentationMirrors_() noexcept {
        ensureParameterCapacity(kNumParams);
        sidSetStateRootParamValue(state_root_, kParamSidModel,
            variant_profile_.family == SidFamily::MOS8580 ? 1.0f : 0.0f);
        sidSetStateRootParamValue(state_root_, kParamSidClockSystem,
            variant_profile_.video_standard == SidVideoStandard::NTSC ? 1.0f : 0.0f);
        sidEnsureSemanticParameterEntries(state_root_);
    }

    void syncVariantPresentationMirrorsRT_() noexcept {
        // Render-thread variant mirror update for already canonicalized roots.
        // This deliberately touches only the pre-sized values vector; semantic
        // entries were canonicalized on the non-RT producer side.
        (void)sidSetHydratedStateRootParamValueRT(state_root_, kParamSidModel,
            variant_profile_.family == SidFamily::MOS8580 ? 1.0f : 0.0f);
        (void)sidSetHydratedStateRootParamValueRT(state_root_, kParamSidClockSystem,
            variant_profile_.video_standard == SidVideoStandard::NTSC ? 1.0f : 0.0f);
    }

public:
    SidRuntimeModel() : ingress_nodes_(new IngressNode[kIngressCapacity_]) { ensureParameterCapacity(kNumParams); syncVariantPresentationMirrors_(); }
    SidRuntimeModel(const SidRuntimeModel&) = delete;
    SidRuntimeModel& operator=(const SidRuntimeModel&) = delete;
    SidRuntimeModel(SidRuntimeModel&&) = delete;
    SidRuntimeModel& operator=(SidRuntimeModel&&) = delete;
    static uint32_t floatToBits_(float v) noexcept { uint32_t out = 0u; std::memcpy(&out, &v, sizeof(out)); return out; }
    static float bitsToFloat_(uint32_t bits) noexcept { float out = 0.0f; std::memcpy(&out, &bits, sizeof(out)); return out; }
    void reset() noexcept {
        dynamic_state_.reset();
        register_image_.clear();
        parameter_register_image_.clear();
        previous_parameter_register_image_.clear();
        resetAllIngressSurfaces_();
        state_root_ = SidStateRootV1{};
        variant_profile_ = sidDefaultVariantProfile();
        posterior_ = {};
        static_params_ = resolveEffectiveSidStaticParams(variant_profile_, posterior_);
        ensureParameterCapacity(kNumParams);
        syncVariantPresentationMirrors_();
    }

    // ----------------------------------------------------------------------
    // CANONICAL INGRESS: Lane-based push (preferred path for all wrappers).
    // Wrappers must call pushToLane() rather than enqueueEvent() for normal flow.
    // enqueueEvent() is preserved as the emergency/legacy fallback path.
    // --------------------------------------------------------------------------
    // Push an event into the canonical merge lane for this source priority.
    // This is the primary truth ingress path. Returns false only on lane overflow
    // (explicit attributed degraded path).
    bool pushToLane(const SidTimedEvent& ev, SidIngressSourcePriority priority,
                    int frameCount) noexcept {
        SidTimedEvent clean = ev;
        clean.sanitize(frameCount);
        if (sidIngressPushToLane(mergeLanes_, clean, priority)) return true;
        mergeOverflowTelemetry_.fetch_add(1, std::memory_order_relaxed);
        return pushIngress_(clean);
    }

    // Drain all merge lanes into pending_events_ at render boundary.
    // Must be called before consumePendingEvents(). Called internally by
    // consumePendingEvents() so callers do not need to invoke it separately.
    // Uses persistent mergeScratch_ to avoid large stack allocations on host threads.
    int flushMergeLanesToPending(int frameCount) noexcept {
        return sidIngressMerge(mergeLanes_, pending_events_,
                               mergeArrivalCounter_, frameCount,
                               mergeOverflowTelemetry_,
                               mergeScratch_.data(), mergeScratch_.size());
    }

    uint64_t mergeOverflowCount() const noexcept {   // audit #12: 64-bit cumulative drop telemetry
        return mergeOverflowTelemetry_.load(std::memory_order_relaxed);
    }

    bool editorLayoutBlobWasTruncated() const noexcept {
        return editor_layout_blob_truncated_.load(std::memory_order_relaxed);
    }

    void clearSanitizeTelemetry() noexcept {
        editor_layout_blob_truncated_.store(false, std::memory_order_relaxed);
    }

    // Called by sid_runtime_reset_policy during panic/reset to amputate merge lanes.
    void resetMergeLanes() noexcept { resetMergeLanes_(); }

    void applyStateRoot(const SidStateRootV1& root) noexcept {
        if (!root.valid()) return;
        state_root_ = root;
        sanitizeStateRoot_(state_root_);
        variant_profile_ = state_root_.patch.variant_profile;
        variant_profile_.sanitize();
        posterior_ = state_root_.patch.posterior;
        posterior_.sanitize();
        static_params_ = resolveEffectiveSidStaticParams(variant_profile_, posterior_);
        dynamic_state_.sanitize();
        resetAllIngressSurfaces_();
        ensureParameterCapacity(kNumParams);
        syncVariantPresentationMirrors_();
        {
            const float raw = sidStateRootParamValue(state_root_, kParamSidRegD417);
            const uint8_t byte = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(raw * 255.0f)), 0, 255));
            dynamic_state_.synth_filter_route_low_nibble = static_cast<uint8_t>(byte & 0x07u);
        }
        rebuildParameterRegisterImage_(parameter_register_image_);
        register_image_.clear();
        previous_parameter_register_image_ = parameter_register_image_;
    }

    // applyStateRootBySwap: RT-safe variant. Swaps inOut with state_root_ (O(1), no heap
    // allocation on the caller's thread) instead of copy-assigning. After the call, inOut
    // holds the old state_root_ data; the caller is responsible for ensuring it is freed on
    // a non-RT thread. Used exclusively by drainPendingStateRestore_ on the audio thread.
    //
    // audit P0-3 / RT-safety CONTRACT: inOut MUST already be fully canonicalized by the
    // non-RT producer via sidCanonicalizeStateRootForApply() (schedulePendingStateRestore
    // does this). This method therefore does NOT call sanitizeStateRoot_ — that function
    // allocates (rebuilds parameter/semantic vectors, copies mod routes) and must never
    // run on the audio thread. The post-swap setup below only reuses already-allocated
    // capacity, so it stays allocation-free for a canonical root.
    void applyStateRootBySwap(SidStateRootV1& inOut) noexcept {
        if (!inOut.valid()) return;
        // Swap state_root_ with inOut: O(1) pointer exchange, no alloc, no free on this thread.
        // The caller's inOut now holds the old state_root_; it will be freed on the non-RT
        // side when the double-buffer slot is next overwritten by schedulePendingStateRestore.
        using std::swap;
        swap(state_root_, inOut);
        variant_profile_ = state_root_.patch.variant_profile;
        variant_profile_.sanitize();
        posterior_ = state_root_.patch.posterior;
        posterior_.sanitize();
        static_params_ = resolveEffectiveSidStaticParams(variant_profile_, posterior_);
        dynamic_state_.sanitize();
        resetAllIngressSurfaces_();
        syncVariantPresentationMirrorsRT_();
        {
            const float raw = sidStateRootParamValueFromHydratedValuesRT(state_root_, kParamSidRegD417);
            const uint8_t byte = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(raw * 255.0f)), 0, 255));
            dynamic_state_.synth_filter_route_low_nibble = static_cast<uint8_t>(byte & 0x07u);
        }
        rebuildParameterRegisterImage_(parameter_register_image_);
        register_image_.clear();
        previous_parameter_register_image_ = parameter_register_image_;
    }

    void exportStateRootTo(SidStateRootV1& out) const {
        buildStateRootFromPresentationTemplate(state_root_, out,
            [this](int i) noexcept -> float {
                return sidStateRootParamValue(state_root_, i);
            });
        out.patch.variant_profile = variant_profile_;
        out.patch.posterior = posterior_;
        out.patch.mod_routes = sanitizedModRoutes_(out.patch.mod_routes);
        sanitizePersistentStateRootForSerialization(out);
    }

    SidStateRootV1 exportStateRoot() const {
        SidStateRootV1 root{};
        exportStateRootTo(root);
        return root;
    }

    void ensureParameterCapacity(size_t count) noexcept {
        if ((int)count >= kNumParams) {
            sidEnsureSemanticParameterEntries(state_root_);
            sidHydrateParameterValuesFromSemanticEntries(state_root_);
            return;
        }
        if (state_root_.patch.parameters.values.size() < count)
            state_root_.patch.parameters.values.resize(count, 0.0f);
    }

    void prepareRealtimeParameterStorage(size_t count = static_cast<size_t>(kNumParams)) noexcept {
        ensureParameterCapacity(count);
    }

private:
    // Legacy preload/import mutation surface only. Live automation must not call this directly.
    bool importNormalizedParameterPreload_(size_t index, float value) noexcept {
        if (index >= static_cast<size_t>(kNumParams)) return false;
        float v = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
        if (sidStateRootParamValue(state_root_, static_cast<int>(index)) == v) return false;
        sidSetStateRootParamValue(state_root_, static_cast<int>(index), v);
        sidEnsureSemanticParameterEntries(state_root_);
        if (index >= kFirstSidParamIndex_() && index <= kLastSidParamIndex_()) {
            const int regIdx = static_cast<int>(index - kFirstSidParamIndex_());
            const uint8_t byte = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(v * 255.0f)), 0, 255));
            // Parameter import updates the parameter-derived image only.
            // The live readback image is derived from canonical SidRegisterWrite events.
            parameter_register_image_.set(regIdx, byte);
            if (index == static_cast<size_t>(kParamSidRegD417)) {
                dynamic_state_.synth_filter_route_low_nibble = static_cast<uint8_t>(byte & 0x07u);
            }
        }
        if (index == static_cast<size_t>(kParamSidModel)) {
            SidVariantProfile profile = variant_profile_;
            profile.family = (v >= 0.5f) ? SidFamily::MOS8580 : SidFamily::MOS6581;
            setVariantProfile(profile);
        } else if (index == static_cast<size_t>(kParamSidClockSystem)) {
            SidVariantProfile profile = variant_profile_;
            sidSetVariantVideoStandard(profile,
                (v >= 0.5f) ? SidVideoStandard::NTSC : SidVideoStandard::PAL);
            setVariantProfile(profile);
        }
        return true;
    }

public:
    bool applyAutomationPoint(uint32_t target, float value) noexcept {
        const size_t index = static_cast<size_t>(target);
        return importNormalizedParameterPreload_(index, value);
    }

    bool importRegisterWriteSnapshot(uint32_t regIndex, uint32_t valueU32) noexcept {
        if (regIndex >= static_cast<uint32_t>(kSidCanonicalRegCount)) return false;
        // Live SID register write import is only authoritative for writable hardware registers.
        // Readback registers ($D419-$D41C), the pseudo system byte ($D41D), and open-bus / mirror
        // slots ($D41E-$D41F) must not enter the live audio register-write path, otherwise UI writes
        // can fight telemetry/presentation state or silently target undefined hardware behavior.
        if (regIndex >= 0x19u) return false;
        constexpr size_t kFirstSidRegParam = static_cast<size_t>(kParamSidRegD400);
        (void)kFirstSidRegParam; // retained for documentation; index derived via kFirstSidParamIndex_()
        const uint8_t byte = static_cast<uint8_t>(valueU32 & 0xFFu);
        // Acceptance and mutation are intentionally separate here: SID REG live-write callers need
        // a stable "accepted writable register" result even when the incoming byte matches the
        // already-mirrored value (for example $D418 default MODE/VOL = 0x0F).
        (void)importNormalizedParameterPreload_(kFirstSidParamIndex_() + static_cast<size_t>(regIndex), static_cast<float>(byte) / 255.0f);
        return true;
    }

    bool importPseudoSystemSnapshot(uint32_t valueU32) noexcept {
        const uint8_t byte = static_cast<uint8_t>(valueU32 & 0xFFu);
        (void)importNormalizedParameterPreload_(kFirstSidParamIndex_() + static_cast<size_t>(0x1Du), static_cast<float>(byte) / 255.0f);
        return true;
    }

    bool applyVariantChange(const SidVariantProfile& profile) noexcept {
        SidVariantProfile clean = profile;
        clean.sanitize();
        if (clean.family == variant_profile_.family &&
            clean.chip_revision_code == variant_profile_.chip_revision_code &&
            clean.video_standard == variant_profile_.video_standard &&
            clean.board_revision == variant_profile_.board_revision &&
            clean.output_stage == variant_profile_.output_stage &&
            clean.master_clock_hz == variant_profile_.master_clock_hz &&
            clean.nominal_sid_clock_hz == variant_profile_.nominal_sid_clock_hz &&
            clean.allow_runtime_variant_switch == variant_profile_.allow_runtime_variant_switch)
            return false;
        setVariantProfile(clean);
        return true;
    }

    void replaceModRoutes(const std::vector<SidModRoute>& routes) {
        state_root_.patch.mod_routes = sanitizedModRoutes_(routes);
    }

    void setVariantProfile(const SidVariantProfile& profile) noexcept {
        variant_profile_ = profile;
        variant_profile_.sanitize();
        static_params_ = resolveEffectiveSidStaticParams(variant_profile_, posterior_);
        state_root_.patch.variant_profile = variant_profile_;
        syncVariantPresentationMirrors_();
    }

    SidRuntimeRenderMode resolveRenderMode() const noexcept {
        const auto readParam = [this](size_t idx) noexcept -> float {
            return sidStateRootParamValue(state_root_, static_cast<int>(idx));
        };
        if (readParam(static_cast<size_t>(kParamDrSidEnable)) > 0.5f) return SidRuntimeRenderMode::DrSid;
        if (readParam(static_cast<size_t>(kParamSynthModeEnable)) > 0.5f) return SidRuntimeRenderMode::SidRegister;
        return SidRuntimeRenderMode::BitPerfect;
    }

    bool isSynthModeEnabled() const noexcept { return resolveRenderMode() == SidRuntimeRenderMode::SidRegister; }
    bool isDrSidModeEnabled() const noexcept { return resolveRenderMode() == SidRuntimeRenderMode::DrSid; }
    bool isArpEnabled() const noexcept {
        // v939: mirror the live-param helper contract for the state-root model.
        // ARP is an explicit secondary authority only when the resolved render
        // mode is BitPerfect/CLASSIC; stale raw flags are ignored in SynthMode
        // and DrSID.
        const auto mode = resolveRenderMode();
        if (mode != SidRuntimeRenderMode::BitPerfect) return false;
        const size_t idx = static_cast<size_t>(kParamArpEnable);
        return sidStateRootParamValue(state_root_, static_cast<int>(idx)) > 0.5f;
    }

    const SidVariantProfile& variantProfile() const noexcept { return variant_profile_; }
    const SidStaticParams& staticParams() const noexcept { return static_params_; }
    const SidMeasuredPosterior& measuredPosterior() const noexcept { return posterior_; }
    void setMeasuredPosterior(const SidMeasuredPosterior& posterior) noexcept {
        posterior_ = posterior;
        posterior_.sanitize();
        state_root_.patch.posterior = posterior_;
        static_params_ = resolveEffectiveSidStaticParams(variant_profile_, posterior_);
    }
    // Mutable dynamic state is an internal canonical-runtime surface only.  Do not
    // expose it under the generic `dynamicState()` spelling: that invited wrappers
    // and compatibility layers to bypass SidRuntimeModel's single-authority
    // mutation law.  Render/internal legacy code that still needs the aggregate
    // must opt in with the long name below; normal callers use the narrow setters/
    // observers on SidRuntimeModel.
    SidDynamicState& dynamicStateInternalForCanonicalRuntimeOnly() noexcept { return dynamic_state_; }
    const SidDynamicState& dynamicState() const noexcept { return dynamic_state_; }

    uint8_t synthFilterRouteLowNibble() const noexcept { return static_cast<uint8_t>(dynamic_state_.synth_filter_route_low_nibble & 0x07u); }
    void setLfoValue(int index, float value) noexcept {
        if (index < 0 || index >= static_cast<int>(std::size(dynamic_state_.lfo_values))) return;
        dynamic_state_.lfo_values[index] = std::clamp(value, -1.0f, 1.0f);
    }
    float lfoValue(int index) const noexcept {
        if (index < 0 || index >= static_cast<int>(std::size(dynamic_state_.lfo_values))) return 0.0f;
        return dynamic_state_.lfo_values[index];
    }
    void clearLfoValues() noexcept { for (float& v : dynamic_state_.lfo_values) v = 0.0f; }
    void resolveRandomForCurrentScope() noexcept { dynamic_state_.resolveRandomForCurrentScope(); }
    void setEnv1Level(float value) noexcept { dynamic_state_.env1_level = std::clamp(value, 0.0f, 1.0f); }
    float env1Level() const noexcept { return dynamic_state_.env1_level; }
    const SidRegisterImage& registerImage() const noexcept { return register_image_; }
    // Mutable pending-event queue access is non-realtime/internal only.  Canonical
    // render ingress must flow through the typed event/ingress APIs, not through a
    // generic public queue reference.
    SidTimedEventQueue& pendingEventsNonRealtimeOnly() noexcept { return pending_events_; }
    const SidTimedEventQueue& pendingEvents() const noexcept { return pending_events_; }
    const SidStateRootV1& stateRoot() const noexcept { return state_root_; }
    uint64_t consumeDirectDispatchDropped() noexcept { return direct_dispatch_dropped_.exchange(0u, std::memory_order_relaxed); }

    void setHostTempoBpm(float bpm) noexcept {
        if (!std::isfinite(bpm) || bpm <= 0.0f) bpm = 120.0f;
        dynamic_state_.host_tempo_bpm = std::clamp(bpm, 1.0f, 400.0f);
    }
    void setHostProjectTimePPQ(double ppq) noexcept {
        dynamic_state_.seq_last_project_time_ppq = std::isfinite(ppq) ? ppq : -1.0;
    }
    void setArpActiveFlag(bool active) noexcept {
        dynamic_state_.arp_active = active;
    }
    void setTransportPlayingFlag(bool playing) noexcept {
        dynamic_state_.transport_playing = playing;
    }
    void setPitchBendNorm(int channel, float norm) noexcept {
        if (channel < 0 || channel >= 16) return;
        dynamic_state_.pitch_bend_norm[(size_t)channel] = std::clamp(std::isfinite(norm) ? norm : 0.0f, -1.0f, 1.0f);
    }
    void setChannelPressureNorm(int channel, float norm) noexcept {
        if (channel < 0 || channel >= 16) return;
        dynamic_state_.channel_pressure[(size_t)channel] = std::clamp(std::isfinite(norm) ? norm : 0.0f, 0.0f, 1.0f);
    }
    void setSustainState(int channel, bool on) noexcept {
        if (channel < 0 || channel >= 16) return;
        dynamic_state_.sustain[(size_t)channel] = on;
    }
    void setSostenutoState(int channel, bool on) noexcept {
        if (channel < 0 || channel >= 16) return;
        dynamic_state_.sostenuto[(size_t)channel] = on;
    }

    void setModWheelNorm(float norm, bool fromCc = true) noexcept {
        dynamic_state_.mod_wheel = std::clamp(std::isfinite(norm) ? norm : 0.0f, 0.0f, 1.0f);
        dynamic_state_.mod_wheel_from_cc = fromCc;
    }
    void setBendRangeSemis(int channel, float semis) noexcept {
        if (channel < 0 || channel >= 16) return;
        dynamic_state_.bend_range_semis[(size_t)channel] = std::clamp(std::isfinite(semis) ? semis : 2.0f, 0.0f, 96.0f);
    }
    float bendRangeSemis(int channel) const noexcept {
        if (channel < 0 || channel >= 16) return 2.0f;
        return dynamic_state_.bend_range_semis[(size_t)channel];
    }
    void setFollowHostTempoArp(bool on) noexcept { dynamic_state_.follow_host_tempo_arp = on; }
    bool followHostTempoArp() const noexcept { return dynamic_state_.follow_host_tempo_arp; }
    void setFollowHostTempoSeq(bool on) noexcept { dynamic_state_.follow_host_tempo_seq = on; }
    bool followHostTempoSeq() const noexcept { return dynamic_state_.follow_host_tempo_seq; }
    void setSeqLastNote(int note) noexcept { dynamic_state_.seq_last_note = std::clamp(note, -1, 127); }
    int seqLastNote() const noexcept { return dynamic_state_.seq_last_note; }
    void setSeqSamplesUntilStep(double v) noexcept { dynamic_state_.seq_samples_until_step = std::isfinite(v) ? v : -1.0; }
    double seqSamplesUntilStep() const noexcept { return dynamic_state_.seq_samples_until_step; }
    void setSeqStep(int step) noexcept { dynamic_state_.seq_step = std::max(0, step); }
    int seqStep() const noexcept { return std::max(0, dynamic_state_.seq_step); }
    int nextSeqNoteId() noexcept {
        const int id = std::max(1, dynamic_state_.seq_note_id_counter);
        dynamic_state_.seq_note_id_counter = (id >= 0x7ffffffe) ? 1 : (id + 1);
        return id;
    }
    void resetSeqNoteIdCounter() noexcept { dynamic_state_.seq_note_id_counter = 1; }
    void setSeqHostWasPlaying(bool on) noexcept { dynamic_state_.seq_host_was_playing = on; }
    bool seqHostWasPlaying() const noexcept { return dynamic_state_.seq_host_was_playing; }
    void advanceRandomScope() noexcept { dynamic_state_.advanceRandomScope(); }
        float hostTempoBpm() const noexcept { return dynamic_state_.host_tempo_bpm; }
    double hostProjectTimePPQ() const noexcept { return dynamic_state_.seq_last_project_time_ppq; }
    bool arpActiveFlag() const noexcept { return dynamic_state_.arp_active; }
    bool transportPlayingFlag() const noexcept { return dynamic_state_.transport_playing; }
    float modWheelNorm() const noexcept { return std::clamp(dynamic_state_.mod_wheel, 0.0f, 1.0f); }
    float randomBipolar() const noexcept { return std::clamp(dynamic_state_.random_bipolar, -1.0f, 1.0f); }
    float channelPressureNorm(int channel) const noexcept { return dynamic_state_.channel_pressure[std::clamp(channel, 0, 15)]; }
    float strongestChannelPressureNorm() const noexcept {
        float best = 0.0f;
        for (int i = 0; i < 16; ++i) best = std::max(best, std::clamp(dynamic_state_.channel_pressure[i], 0.0f, 1.0f));
        return best;
    }
    float pitchBendNorm(int channel) const noexcept { return dynamic_state_.pitch_bend_norm[std::clamp(channel, 0, 15)]; }
    uint64_t resolveVoiceTokenForIdentity(int channel, int note, int32_t noteId) const noexcept {
        return dynamic_state_.resolveVoiceTokenForEventIdentity(static_cast<int16_t>(channel), static_cast<int16_t>(note), noteId);
    }
    void bindPolyPressureToToken(uint64_t tok, float pressure) noexcept { dynamic_state_.bindPolyPressureToToken(tok, pressure); }
    void setLastPolyPressureNote(int channel, int note) noexcept { dynamic_state_.last_poly_pressure_note[std::clamp(channel, 0, 15)] = static_cast<uint8_t>(std::clamp(note, 0, 127)); }
    void setLastNoteState(int note, int channel, float velocity) noexcept {
        dynamic_state_.last_note = static_cast<uint8_t>(std::clamp(note, 0, 127));
        dynamic_state_.last_note_channel = static_cast<uint8_t>(std::clamp(channel, 0, 15));
        dynamic_state_.last_note_velocity = std::clamp(velocity, 0.0f, 1.0f);
    }
    int lastNote() const noexcept { return dynamic_state_.last_note; }
    int lastNoteChannel() const noexcept { return dynamic_state_.last_note_channel; }
    float lastNoteVelocity() const noexcept { return dynamic_state_.last_note_velocity; }
    uint64_t focusedVoiceToken() const noexcept { return dynamic_state_.focusedVoiceToken; }
    const SidTokenVoiceEntry* focusedTokenEntry() const noexcept { return dynamic_state_.focusedTokenEntry(); }
    const SidTokenVoiceEntry* newestActiveTokenEntry() const noexcept { return dynamic_state_.newestActiveTokenEntry(); }
    int focusedChannelIndex() const noexcept {
        if (const auto* tok = focusedTokenEntry()) return std::clamp<int>(tok->token.channel, 0, 15);
        if (const auto* tok = newestActiveTokenEntry()) return std::clamp<int>(tok->token.channel, 0, 15);
        return std::clamp<int>(lastNoteChannel(), 0, 15);
    }
    float focusedPitchBendNorm() const noexcept {
        return std::clamp(pitchBendNorm(focusedChannelIndex()), -1.0f, 1.0f);
    }
    float focusedChannelPressureBipolar() const noexcept {
        return std::clamp(channelPressureNorm(focusedChannelIndex()) * 2.0f - 1.0f, -1.0f, 1.0f);
    }
    float focusedPolyPressureBipolar() const noexcept {
        if (const auto* tok = focusedTokenEntry())
            return std::clamp(std::clamp(tok->polyPressure, 0.0f, 1.0f) * 2.0f - 1.0f, -1.0f, 1.0f);
        if (const auto* tok = newestActiveTokenEntry())
            return std::clamp(std::clamp(tok->polyPressure, 0.0f, 1.0f) * 2.0f - 1.0f, -1.0f, 1.0f);
        return 0.0f;
    }
    void clearIdentityMirrors() noexcept { dynamic_state_.rebuildCompatActiveIdentitiesFromTokens(); }
    uint64_t bindVoiceTokenBridge(int channel, int note, int32_t noteId, float velocity, uint32_t arrivalOrder) noexcept {
        const uint64_t tok = dynamic_state_.bindVoiceToken(channel, note, noteId, velocity, arrivalOrder);
        dynamic_state_.rebuildCompatActiveIdentitiesFromTokens();
        return tok;
    }
    uint64_t bindVoiceTokenBridgeWithToken(int channel,
                                           int note,
                                           int32_t noteId,
                                           float velocity,
                                           uint32_t arrivalOrder,
                                           uint64_t voiceToken) noexcept {
        const uint64_t tok = dynamic_state_.bindVoiceTokenExplicit(
            static_cast<int16_t>(channel),
            static_cast<int16_t>(note),
            noteId,
            velocity,
            arrivalOrder,
            voiceToken);
        dynamic_state_.rebuildCompatActiveIdentitiesFromTokens();
        return tok;
    }
    bool hasActiveVoiceToken(uint64_t tok) const noexcept { return dynamic_state_.findActiveVoiceByToken(tok) != nullptr; }
    void clearAllCanonicalVoiceState() noexcept { dynamic_state_.clearAllTokenVoices(); dynamic_state_.rebuildCompatActiveIdentitiesFromTokens(); }
    void resetTokenSerial() noexcept { dynamic_state_.nextTokenSerial_ = 1; }
    void clearVoicesForChannel(int channel) noexcept {
        if (channel < 0) {
            clearAllCanonicalVoiceState();
            clearIdentityMirrors();
            resetTokenSerial();
            return;
        }
        for (auto& e : dynamic_state_.tokenVoices) {
            if (!e.token.active) continue;
            if (e.token.channel >= 0 && e.token.channel != channel) continue;
            e = {};
        }
        uint64_t bestTok = 0; uint32_t bestOrder = 0;
        for (const auto& e : dynamic_state_.tokenVoices) {
            if (!e.token.active) continue;
            if (bestTok == 0 || e.token.arrivalOrder >= bestOrder) {
                bestTok = e.token.token; bestOrder = e.token.arrivalOrder;
            }
        }
        dynamic_state_.focusedVoiceToken = bestTok;
        dynamic_state_.rebuildCompatActiveIdentitiesFromTokens();
    }
    template <class Fn>
    void forEachActiveTokenVoice(Fn&& fn) const noexcept {
        for (const auto& e : dynamic_state_.tokenVoices) {
            if (!e.token.active) continue;
            fn(e);
        }
    }
        void setPnState(int channel, int active, int number, int msb, int lsb) noexcept {
        if (channel < 0 || channel >= 16) return;
        dynamic_state_.pn_active[(size_t)channel] = active;
        dynamic_state_.pn_number[(size_t)channel] = number;
        dynamic_state_.pn_data_msb[(size_t)channel] = msb;
        dynamic_state_.pn_data_lsb[(size_t)channel] = lsb;
    }
    int pnActive(int channel) const noexcept { return (channel < 0 || channel >= 16) ? 0 : dynamic_state_.pn_active[(size_t)channel]; }
    int pnNumber(int channel) const noexcept { return (channel < 0 || channel >= 16) ? -1 : dynamic_state_.pn_number[(size_t)channel]; }
    int pnDataMsb(int channel) const noexcept { return (channel < 0 || channel >= 16) ? 0 : dynamic_state_.pn_data_msb[(size_t)channel]; }
    int pnDataLsb(int channel) const noexcept { return (channel < 0 || channel >= 16) ? 0 : dynamic_state_.pn_data_lsb[(size_t)channel]; }

    void resetControllersForChannel(int channel) noexcept {
        if (channel < 0 || channel >= 16) {
            for (int ch = 0; ch < 16; ++ch) {
                setPitchBendNorm(ch, 0.0f);
                setChannelPressureNorm(ch, 0.0f);
            }
            setModWheelNorm(0.0f, false);
            return;
        }
        setPitchBendNorm(channel, 0.0f);
        setChannelPressureNorm(channel, 0.0f);
        if (channel == lastNoteChannel()) setModWheelNorm(0.0f, false);
    }

    void noteOnCanonical(int note, int channel, int32_t noteId, float velocity) noexcept {
        dynamic_state_.noteOnCanonical(note, channel, noteId, velocity);
    }
    void noteOffCanonical(int note, int channel, int32_t noteId) noexcept {
        dynamic_state_.noteOffCanonical(note, channel, noteId);
    }
    void refreshLastNoteFromTokenState(int preferredChannel = -1) noexcept {
        if (const auto* tok = focusedTokenEntry()) {
            setLastNoteState(tok->token.note, tok->token.channel, tok->velocity);
            return;
        }
        if (const auto* tok = newestActiveTokenEntry()) {
            if (preferredChannel < 0 || tok->token.channel == preferredChannel) {
                setLastNoteState(tok->token.note, tok->token.channel, tok->velocity);
                return;
            }
        }
        if (preferredChannel >= 0) {
            for (const auto& e : dynamic_state_.tokenVoices) {
                if (!e.token.active || e.token.channel != preferredChannel) continue;
                if (e.sustained) continue;
                setLastNoteState(e.token.note, e.token.channel, e.velocity);
                return;
            }
            for (const auto& e : dynamic_state_.tokenVoices) {
                if (!e.token.active || e.token.channel != preferredChannel) continue;
                if (!e.sustained) continue;
                setLastNoteState(e.token.note, e.token.channel, e.velocity);
                return;
            }
        }
        setLastNoteState(lastNote(), lastNoteChannel(), 0.0f);
    }

    const PatchStartPolicy& patchStartPolicy() const noexcept { return state_root_.patch.start_policy; }
    void bindBackend(SidRuntimeBackend* backend) noexcept { backend_ = backend; }
    SidRuntimeBackend* boundBackend() noexcept { return backend_; }
    const SidRuntimeBackend* boundBackend() const noexcept { return backend_; }
    SidRuntimePrimitiveSurface* primitiveSurface() noexcept { return backend_; }
    const SidRuntimePrimitiveSurface* primitiveSurface() const noexcept { return backend_; }


    // enqueueEvent() removed — use pushToLane() for all canonical ingress.
    // The legacy lock-free node pool (pushIngress_) remains as the spill fallback
    // when merge lanes overflow; it is not a public API.

    bool hasPendingEvents() const noexcept {
        if (pending_events_.count > 0) return true;
        if (ingress_pending_head_.load(std::memory_order_acquire) != kIngressNull_) return true;
        if (ingress_spill_count_.load(std::memory_order_acquire) > 0) return true;
        for (const auto& lane : mergeLanes_) {
            if (!lane.empty()) return true;
        }
        return hasPendingFallbacks_();
    }

    void clearTransientEvents() noexcept {
        resetAllIngressSurfaces_();
    }

    void sortPendingEvents() noexcept { flushIngressToPending_(); pending_events_.sort(); }

    void consumePendingEventsInto(int frameCount, SidTimedEventQueue& out) noexcept {
        // Canonical path: drain merge lanes first (primary truth ingress).
        flushMergeLanesToPending(frameCount);
        // Legacy path: drain lock-free node ingress, spill, and fallbacks.
        flushIngressToPending_();
        for (int i = 0; i < pending_events_.count; ++i) {
            pending_events_.events[i].sanitize(frameCount);
        }
        pending_events_.sort();
        // Swap queue ownership instead of move-assigning into a fresh queue each block.
        // This preserves preallocated storage on both sides and avoids a lazy
        // re-allocation the next time the runtime queue is used.
        pending_events_.swap(out);
        out.dropped += ingress_dropped_.load(std::memory_order_relaxed);
        out.dropped += mergeOverflowTelemetry_.exchange(0, std::memory_order_relaxed);
        pending_events_.reset();
        ingress_dropped_.store(0, std::memory_order_relaxed);
        ingress_sequence_.store(1u, std::memory_order_release);
        if (tryLockIngressSpill_()) { ingress_spill_count_.store(0, std::memory_order_release); unlockIngressSpill_(); }
    }

    // audit P0.3: the by-value consumePendingEvents() wrapper was a render-path
    // allocation trap (it default-constructed — and therefore allocated — a
    // temporary SidTimedEventQueue). Removed. Use consumePendingEventsInto() with
    // caller-owned storage, or consumePendingEventsDirect() with a sink.

    uint64_t ingressDroppedCount() const noexcept { return ingress_dropped_.load(std::memory_order_relaxed); }
    // audit #2: observable per-type NoteOn/NoteOff loss under genuine ring fullness.
    uint64_t ingressDroppedNoteOnCount() const noexcept { return ingress_dropped_note_on_.load(std::memory_order_relaxed); }
    uint64_t ingressDroppedNoteOffCount() const noexcept { return ingress_dropped_note_off_.load(std::memory_order_relaxed); }


    template <class Sink>
    int consumePendingEventsTo(int frameCount, Sink&& sink) noexcept(noexcept(sink(std::declval<const SidTimedEvent&>()))) {
        // Route through the direct sink path to avoid constructing a giant
        // SidTimedEventQueue local on the real-time stack.
        return consumePendingEventsDirect(frameCount, std::forward<Sink>(sink));
    }

    template <class Sink>
    int consumePendingEventsDirect(int frameCount, Sink&& sink) noexcept(noexcept(sink(std::declval<const SidTimedEvent&>()))) {
        flushMergeLanesToPending(frameCount);
        flushIngressToPending_();
        for (int i = 0; i < pending_events_.count; ++i) {
            pending_events_.events[i].sanitize(frameCount);
        }
        pending_events_.sort();
        const int count = pending_events_.count;
        for (int i = 0; i < count; ++i) {
            sink(pending_events_.events[i]);
        }
        const uint64_t dropped = ingress_dropped_.load(std::memory_order_relaxed) +
                                 mergeOverflowTelemetry_.exchange(0, std::memory_order_relaxed);
        direct_dispatch_dropped_.fetch_add(dropped + pending_events_.dropped, std::memory_order_relaxed);
        pending_events_.reset();
        ingress_dropped_.store(0, std::memory_order_relaxed);
        ingress_sequence_.store(1u, std::memory_order_release);
        if (tryLockIngressSpill_()) { ingress_spill_count_.store(0, std::memory_order_release); unlockIngressSpill_(); }
        return count;
    }


    template <class Sink>
    int dispatchPendingEvents(int frameCount, Sink&& sink) noexcept(noexcept(sink(std::declval<const SidTimedEvent&>()))) {
        return consumePendingEventsTo(frameCount, std::forward<Sink>(sink));
    }

    void processBoundCanonicalBlockInto(int frameCount, SidTimedEventQueue& out) noexcept {
        if (!backend_) {
            consumePendingEventsInto(frameCount, out);
            return;
        }
        processCanonicalBlockInto(frameCount,
            [this](const SidTimedEvent& ev) noexcept { backend_->dispatchCanonicalEvent(ev); },
            [this](int offset, int frames) noexcept { backend_->renderCanonicalSlice(offset, frames); },
            out);
    }

    // audit P0.3: by-value processBoundCanonicalBlock() removed (render-path
    // allocation trap). Use processBoundCanonicalBlockInto() with owned storage.


    void renderBoundAudio(float* left, float* right, int frames) noexcept;

    template <class Dispatcher, class RenderSlice>
    void processCanonicalBlockInto(int frameCount,
                                   Dispatcher&& dispatcher,
                                   RenderSlice&& renderSlice,
                                   SidTimedEventQueue& q) noexcept(noexcept(dispatcher(std::declval<const SidTimedEvent&>())) && noexcept(renderSlice(0, 0))) {
        // 1. Drain merge lanes + lock-free ingress, then let render-owned
        // generators append to the same canonical timeline. One final sort is
        // the sole ordering authority for host, sequencer and arp events.
        consumePendingEventsInto(frameCount, q);
        if (backend_) backend_->appendGeneratedTimedEvents(frameCount, q);
        for (int i = 0; i < q.count; ++i) q.events[(size_t)i].sanitize(frameCount);
        q.sort();
        q.recomputeResolvedCycleTimingFlag_();

        // 2. Resolve host-near cycle timing parameters from the active backend.
        // Render-mode and physical-clock changes are structural boundaries. They
        // are deliberately quantized to their canonical host-sample boundary for
        // the whole block so no sample can be partially accumulated by two engines
        // and no dispatcher can continue with an obsolete Q32 clock after the
        // backend has switched PAL/NTSC/model.
        bool hasStructuralBoundary = false;
        for (int i = 0; i < q.count; ++i) {
            const SidTimedEvent& ev = q.events[static_cast<size_t>(i)];
            if (ev.type == SidTimedEventType::VariantChange) {
                hasStructuralBoundary = true;
                break;
            }
            if (ev.type == SidTimedEventType::AutomationPoint &&
                (ev.target == static_cast<uint32_t>(kParamDrSidEnable) ||
                 ev.target == static_cast<uint32_t>(kParamSynthModeEnable) ||
                 ev.target == static_cast<uint32_t>(kParamSidModel) ||
                 ev.target == static_cast<uint32_t>(kParamSidClockSystem))) {
                hasStructuralBoundary = true;
                break;
            }
        }
        const bool fractionalCapable = backend_ &&
                                       backend_->supportsFractionalSubSampleSpans() &&
                                       !hasStructuralBoundary;
        if (fractionalCapable && backend_) {
            const double physicalSampleRate = backend_->physicalSampleRateHz();
            const double physicalSidClock = backend_->physicalSidClockHz();
            if (std::isfinite(physicalSampleRate) && physicalSampleRate > 1.0 &&
                std::isfinite(physicalSidClock) && physicalSidClock > 1.0) {
                sid_cycle_clock_.configure(physicalSampleRate, physicalSidClock);
            } else {
                sid_cycle_clock_.configureFixedCyclesPerSample(backend_->estimatedCyclesPerHostSample());
            }
        } else {
            sid_cycle_clock_.reset();
        }

        // 3. Delegate the dispatch/render interleaving to the single explicit
        // host-near cycle dispatcher. SidRuntimeModel owns ingress and state;
        // SidHostCycleDispatcher owns the dispatch policy.
        SidHostCycleDispatcher::dispatchBlock(
            q,
            frameCount,
            sid_cycle_clock_,
            fractionalCapable,
            [&](const SidTimedEvent& ev) noexcept {
                // Authority ordering is event-class dependent. Creation/control
                // events (notably MidiNoteOn) must update canonical state before
                // dispatch so the concrete engine can inherit the just-created
                // canonical voice token. Release/cleanup events must dispatch to
                // concrete engines first: otherwise applyCanonicalEventToState()
                // can erase the exact identity or transport/pedal pre-state that
                // the release path needs to gate off physical voices.
                const bool dispatchBeforeCanonical =
                    ev.type == SidTimedEventType::MidiNoteOff ||
                    ev.type == SidTimedEventType::AllNotesOff ||
                    ev.type == SidTimedEventType::AllSoundOff ||
                    ev.type == SidTimedEventType::Panic ||
                    (ev.type == SidTimedEventType::TransportChange && !(ev.value > 0.5f)) ||
                    (ev.type == SidTimedEventType::MidiCC &&
                        (ev.ccNum == 64 || ev.ccNum == 66 || ev.ccNum == 120 ||
                         ev.ccNum == 121 || ev.ccNum == 123));

                if (dispatchBeforeCanonical) {
                    dispatcher(ev);
                    applyCanonicalEventToState(ev);
                } else {
                    applyCanonicalEventToState(ev);
                    dispatcher(ev);
                }
            },
            std::forward<RenderSlice>(renderSlice),
            [&](int o, uint16_t cs, uint16_t ce) noexcept {
                if (backend_) backend_->renderCanonicalSubSampleSpan(o, cs, ce);
            },
            [&](int o, uint16_t ci, uint16_t ss, uint16_t se) noexcept {
                if (backend_) backend_->renderCanonicalSubPhaseSpan(o, ci, ss, se);
            });
    }

    // audit P0.3: by-value processCanonicalBlock() removed (render-path
    // allocation trap). Use processCanonicalBlockInto() with owned storage.

    void clearLiveMidiChannel(int channel, bool clearPedals = false) noexcept {
        if (channel < 0 || channel >= 16) {
            clearLiveMidiState();
            return;
        }
        setPitchBendNorm(channel, 0.0f);
        setChannelPressureNorm(channel, 0.0f);
        if (clearPedals) {
            setSustainState(channel, false);
            setSostenutoState(channel, false);
        }
        dynamic_state_.last_poly_pressure_note[(size_t)channel] = 0;
        for (auto& v : dynamic_state_.poly_pressure[(size_t)channel]) v = 0u;
        clearVoicesForChannel(channel);
    }

    void clearLiveMidiState() noexcept {
        setLastNoteState(0, 0, 0.0f);
        setModWheelNorm(0.0f, false);
        for (int i = 0; i < 16; ++i) {
            setPitchBendNorm(i, 0.0f);
            setChannelPressureNorm(i, 0.0f);
            setSustainState(i, false);
            setSostenutoState(i, false);
            dynamic_state_.last_poly_pressure_note[i] = 0;
            for (auto& v : dynamic_state_.poly_pressure[i]) v = 0u;
        }
        // Canonical all-notes-off / all-sound-off / panic must not leave a stale
        // focused token or active token-voice table behind after the MIDI mirrors
        // are cleared. Keep the live dynamic voice state consistent with the MIDI
        // reset semantics.
        clearAllCanonicalVoiceState();
        clearIdentityMirrors();
    }

    int emitRegisterDiffEvents(int frameCount, uint32_t& arrivalCounter,
                               uint32_t sampleOffset = kSidUnresolvedSampleOffset) noexcept {
        if (sampleOffset != kSidUnresolvedSampleOffset) {
            const int maxFrame = std::max(0, frameCount - 1);
            sampleOffset = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(sampleOffset), 0, maxFrame));
        }
        rebuildParameterRegisterImage_(parameter_register_image_);
        int emitted = 0;
        for (int i = 0; i < kSidCanonicalRegCount; ++i) {
            if (previous_parameter_register_image_.reg[i] == parameter_register_image_.reg[i]) continue;
            SidTimedEvent ev{};
            ev.type = SidTimedEventType::SidRegisterWrite;
            ev.sample_offset = sampleOffset;
            ev.cycle_offset = kSidUnresolvedCycleOffset;
            ev.sid_cycle_stamp = 0ull;
            ev.target = static_cast<uint32_t>(i);
            ev.value_u32 = static_cast<uint32_t>(parameter_register_image_.reg[i]);
            ev.value_f32 = static_cast<float>(parameter_register_image_.reg[i]) / 255.0f;
            ev.arrival_order = arrivalCounter++;
            if (pending_events_.push(ev)) {
                ++emitted;
                previous_parameter_register_image_.reg[i] = parameter_register_image_.reg[i];
            }
        }
        previous_parameter_register_image_.dirty_mask = parameter_register_image_.dirty_mask;
        parameter_register_image_.dirty_mask = 0;
        return emitted;
    }


    bool applyCanonicalEventToState(const SidTimedEvent& ev) noexcept {
        // Arp-generated gates are render-control events, not new host-held MIDI
        // identities. Their engine timing is canonical, but they must not create
        // or erase host voice tokens in the persistent runtime state.
        if (sidTimedEventIsInternalArpGenerated(ev)) return false;
        switch (ev.type) {
            case SidTimedEventType::AutomationPoint:
                return applyAutomationPoint(ev.target, ev.value);
            case SidTimedEventType::SidRegisterWrite:
                if (ev.target < kSidCanonicalRegCount) {
                    const uint8_t v = static_cast<uint8_t>(ev.value_u32 & 0xFFu);
                    register_image_.reg[ev.target] = v;
                    register_image_.dirty_mask |= (1ull << ev.target);
                    return true;
                }
                return false;
            case SidTimedEventType::VariantChange:
                return applyVariantChange(canonicalVariantProfileFromEvent(ev, variant_profile_));
            case SidTimedEventType::TempoChange:
                if (!std::isfinite(ev.value_f32)) return false;
                setHostTempoBpm(ev.value_f32);
                return true;
            case SidTimedEventType::TransportChange:
                if (!(ev.value > 0.5f)) {
                    setLastNoteState(0, 0, 0.0f);
                    clearAllCanonicalVoiceState();
                    clearIdentityMirrors();
                    setSeqLastNote(-1);
                    setSeqSamplesUntilStep(-1.0);
                    setSeqStep(0);
                }
                setTransportPlayingFlag(ev.value > 0.5f);
                return true;
            case SidTimedEventType::MidiNoteOn:
                setLastNoteState(static_cast<int>(ev.pitch & 0x7F), std::clamp<int>(ev.channel, 0, 15), std::clamp(ev.value, 0.0f, 1.0f));
                noteOnCanonical(static_cast<int>(ev.pitch & 0x7F), std::clamp<int>(ev.channel, 0, 15), ev.noteId, lastNoteVelocity());
                return true;
            case SidTimedEventType::MidiNoteOff: {
                const int note = static_cast<int>(ev.pitch & 0x7F);
                const int channel = std::clamp<int>(ev.channel, 0, 15);
                noteOffCanonical(note, channel, ev.noteId);
                refreshLastNoteFromTokenState(channel);
                return true;
            }
            case SidTimedEventType::PitchBend:
                if (ev.channel < 16) {
                    const int raw14 = canonicalPitchBend14FromEvent(ev);
                    const float norm = std::clamp((static_cast<float>(raw14) - 8192.0f) / 8192.0f, -1.0f, 1.0f);
                    setPitchBendNorm(ev.channel, norm);
                    return true;
                }
                return false;
            case SidTimedEventType::PolyPressure:
                if (ev.channel < 16 && ev.pitch < 128) {
                    const float pressure = std::clamp(ev.value, 0.0f, 1.0f);
                    // Token-first: update canonical token poly-pressure
                    {
                        const uint64_t tok = resolveVoiceTokenForIdentity(
                            static_cast<int16_t>(ev.channel),
                            static_cast<int16_t>(ev.pitch), ev.noteId);
                        if (tok != 0)
                            bindPolyPressureToToken(tok, pressure);
                    }
                    setLastPolyPressureNote(ev.channel, ev.pitch & 0x7F);
                    // Update focused token pressure when this note is focused
                    if (focusedVoiceToken() != 0) {
                        const auto* fe = dynamic_state_.findActiveVoiceByToken(focusedVoiceToken());
                        if (fe && fe->token.channel == ev.channel && fe->token.note == ev.pitch)
                            bindPolyPressureToToken(focusedVoiceToken(), pressure);
                    }
                    return true;
                }
                return false;
            case SidTimedEventType::ChannelPressure:
                if (ev.channel < 16) {
                    setChannelPressureNorm(ev.channel, ev.value);
                    return true;
                }
                return false;
            case SidTimedEventType::MidiCC:
                if (ev.channel < 16) {
                    if (ev.ccNum == 64) {
                        const bool wasOn = dynamic_state_.sustain[(size_t)ev.channel];
                        const bool nowOn = ev.value >= 0.5f;
                        setSustainState(ev.channel, nowOn);
                        if (wasOn && !nowOn) dynamic_state_.releaseSustainedVoicesForChannel(ev.channel);
                        return true;
                    }
                    if (ev.ccNum == 66) { setSostenutoState(ev.channel, ev.value >= 0.5f); return true; }
                    if (ev.ccNum == 1)  { setModWheelNorm(std::clamp(ev.value, 0.0f, 1.0f), true); return true; }
                }
                return false;
            case SidTimedEventType::AllNotesOff:
                // P0 FIX: MIDI spec mandates AllNotesOff is channel-scoped — release only
                // the voices on the sending channel, and do NOT reset pitch bend, mod wheel,
                // or any other controller on other channels.
                if (ev.channel < 16) {
                    clearVoicesForChannel(ev.channel);
                } else {
                    clearAllCanonicalVoiceState();
                    clearIdentityMirrors();
                }
                return true;
            case SidTimedEventType::AllSoundOff:
                // P0 FIX: AllSoundOff is also channel-scoped — silence voices and reset
                // controllers (sustain, sostenuto, pitch bend) for the sending channel only.
                if (ev.channel < 16) {
                    clearVoicesForChannel(ev.channel);
                    resetControllersForChannel(ev.channel);
                    setSustainState(ev.channel, false);
                    setSostenutoState(ev.channel, false);
                } else {
                    clearLiveMidiState();
                    clearIdentityMirrors();
                }
                return true;
            case SidTimedEventType::Panic:
                // Panic is deliberately global — clear all 16 channels.
                clearLiveMidiState();
                clearIdentityMirrors();
                return true;
            default:
                return false;
        }
    }

    int applyCanonicalQueueToState(const SidTimedEventQueue& q) noexcept {
        int changed = 0;
        for (int i = 0; i < q.count; ++i) changed += applyCanonicalEventToState(q.events[i]) ? 1 : 0;
        return changed;
    }

    // ── Audit #24/#25 diagnostic accessors (v549) ───────────────────────────
    uint64_t pendingEventsDrainDroppedCount() const noexcept {
        return pending_events_drain_dropped_.load(std::memory_order_acquire);
    }
    uint64_t ingressFallbackEdgeOverflowCount() const noexcept {
        return ingress_fallback_edge_ring_.overflowCount();
    }

private:
    static constexpr int kIngressCapacity_ = kSidRuntimeIngressCapacity;
    static constexpr int kIngressSpillCapacity_ = kSidRuntimeIngressSpillCapacity;
    static constexpr int32_t kIngressNull_ = -1;

    struct IngressNode {
        SidTimedEvent event{};
        std::atomic<int32_t> next{kIngressNull_};
    };

    bool tryLockIngressSpill_() noexcept {
        return !ingress_spill_lock_.test_and_set(std::memory_order_acquire);
    }
    void unlockIngressSpill_() noexcept { ingress_spill_lock_.clear(std::memory_order_release); }

    bool pushIngressSpill_(const SidTimedEvent& ev) noexcept {
        if (!tryLockIngressSpill_()) return false;
        const int count = ingress_spill_count_.load(std::memory_order_acquire);
        const bool ok = count < kIngressSpillCapacity_;
        if (ok) {
            ingress_spill_events_[static_cast<size_t>(count)] = ev;
            ingress_spill_count_.store(count + 1, std::memory_order_release);
        }
        unlockIngressSpill_();
        return ok;
    }

    void flushIngressSpillToPending_() noexcept {
        if (!tryLockIngressSpill_()) return;
        const int spillCount = ingress_spill_count_.load(std::memory_order_acquire);
        for (int i = 0; i < spillCount; ++i) {
            if (!pending_events_.push(ingress_spill_events_[static_cast<size_t>(i)]))
                ingress_dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        ingress_spill_count_.store(0, std::memory_order_release);
        unlockIngressSpill_();
    }

    void resetIngress_() noexcept {
        if (!ingress_freelist_initialized_) {
            for (int32_t i = 0; i < kIngressCapacity_ - 1; ++i)
                ingress_nodes_[(size_t)i].next.store(i + 1, std::memory_order_relaxed);
            ingress_nodes_[(size_t)(kIngressCapacity_ - 1)].next.store(kIngressNull_, std::memory_order_relaxed);
            ingress_freelist_initialized_ = true;
        }
        ingress_free_head_.store(0, std::memory_order_relaxed);
        ingress_pending_head_.store(kIngressNull_, std::memory_order_relaxed);
        ingress_dropped_.store(0, std::memory_order_relaxed);
        ingress_dropped_note_on_.store(0, std::memory_order_relaxed);   // audit #2
        ingress_dropped_note_off_.store(0, std::memory_order_relaxed);  // audit #2
        ingress_sequence_.store(1u, std::memory_order_release);
        if (tryLockIngressSpill_()) { ingress_spill_count_.store(0, std::memory_order_release); unlockIngressSpill_(); }
    }

    // Audit #24 helper: push an ordering-critical event onto the MPSC edge
    // ring alongside any latch update. Idempotent — if the ring is full the
    // existing latch path is still updated, so the worst case is loss of
    // *order* (not loss of *state*) for transitions that overflow.
    bool captureIngressFallbackEdge_(const SidTimedEvent& ev,
                                     std::uint8_t  ccNum,
                                     std::uint8_t  value7,
                                     std::uint16_t data14,
                                     float         valueFloat) noexcept {
        IngressFallbackEdge edge{};
        edge.type       = ev.type;
        edge.channel    = (ev.channel != kSidUnresolvedChannel) ? static_cast<std::uint8_t>(ev.channel & 0x0Fu) : 0u;
        edge.ccNum      = ccNum;
        edge.value7     = value7;
        edge.data14     = data14;
        edge.valueFloat = valueFloat;
        return ingress_fallback_edge_ring_.push(edge);
    }

    bool captureIngressFallback_(const SidTimedEvent& ev) noexcept {
        switch (ev.type) {
            case SidTimedEventType::Panic:
                ingress_fallback_panic_.store(1u, std::memory_order_release);
                return true;
            case SidTimedEventType::AllSoundOff:
                if (ev.channel != kSidUnresolvedChannel) ingress_fallback_all_sound_off_[(size_t)(ev.channel & 0x0Fu)].store(1u, std::memory_order_release);
                return true;
            case SidTimedEventType::AllNotesOff:
                if (ev.channel != kSidUnresolvedChannel) ingress_fallback_all_notes_off_[(size_t)(ev.channel & 0x0Fu)].store(1u, std::memory_order_release);
                return true;
            case SidTimedEventType::PitchBend:
                if (ev.channel != kSidUnresolvedChannel) {
                    ingress_fallback_pitch_bend14_[(size_t)(ev.channel & 0x0Fu)].store(canonicalPitchBend14FromEvent(ev), std::memory_order_release);
                    ingress_fallback_pitch_bend_dirty_[(size_t)(ev.channel & 0x0Fu)].store(1u, std::memory_order_release);
                    return true;
                }
                break;
            case SidTimedEventType::ChannelPressure:
                if (ev.channel != kSidUnresolvedChannel) {
                    ingress_fallback_channel_pressure_[(size_t)(ev.channel & 0x0Fu)].store(canonicalMidi7FromEventValue(ev), std::memory_order_release);
                    ingress_fallback_channel_pressure_dirty_[(size_t)(ev.channel & 0x0Fu)].store(1u, std::memory_order_release);
                    return true;
                }
                break;
            case SidTimedEventType::PolyPressure:
                if (ev.channel != kSidUnresolvedChannel) {
                    const size_t ch = static_cast<size_t>(ev.channel & 0x0Fu);
                    const size_t note = static_cast<size_t>(std::clamp<int>(ev.pitch, 0, 127));
                    ingress_fallback_poly_pressure_[ch][note].store(canonicalMidi7FromEventValue(ev), std::memory_order_release);
                    ingress_fallback_poly_pressure_dirty_[ch][note].store(1u, std::memory_order_release);
                    return true;
                }
                break;
            case SidTimedEventType::MidiCC:
                if (ev.channel != kSidUnresolvedChannel) {
                    const size_t ch = static_cast<size_t>(ev.channel & 0x0Fu);
                    const uint8_t cc = ev.ccNum & 0x7Fu;
                    const uint8_t v = canonicalMidi7FromEventValue(ev);
                    if (cc == 1u) {
                        ingress_fallback_mod_wheel_[ch].store(v, std::memory_order_release);
                        ingress_fallback_mod_wheel_dirty_[ch].store(1u, std::memory_order_release);
                        return true;
                    }
                    if (cc == 11u) {
                        ingress_fallback_expression_[ch].store(v, std::memory_order_release);
                        ingress_fallback_expression_dirty_[ch].store(1u, std::memory_order_release);
                        return true;
                    }
                    if (cc == 7u) {
                        ingress_fallback_channel_volume_[ch].store(v, std::memory_order_release);
                        ingress_fallback_channel_volume_dirty_[ch].store(1u, std::memory_order_release);
                        return true;
                    }
                    if (cc == 0u) {
                        ingress_fallback_bank_msb_[ch].store(v, std::memory_order_release);
                        ingress_fallback_bank_msb_dirty_[ch].store(1u, std::memory_order_release);
                        return true;
                    }
                    if (cc == 32u) {
                        ingress_fallback_bank_lsb_[ch].store(v, std::memory_order_release);
                        ingress_fallback_bank_lsb_dirty_[ch].store(1u, std::memory_order_release);
                        return true;
                    }
                    // Ordering-critical fallbacks use the edge ring as authority.
                    // The latches below are overflow-only floors so the same
                    // CC/tempo/transport event cannot drain twice.
                    if (cc == 101u) { if (captureIngressFallbackEdge_(ev, 101u, v, 0u, 0.0f)) return true; ingress_fallback_rpn_msb_[ch].store(v, std::memory_order_release); ingress_fallback_rpn_msb_dirty_[ch].store(1u, std::memory_order_release); return true; }
                    if (cc == 100u) { if (captureIngressFallbackEdge_(ev, 100u, v, 0u, 0.0f)) return true; ingress_fallback_rpn_lsb_[ch].store(v, std::memory_order_release); ingress_fallback_rpn_lsb_dirty_[ch].store(1u, std::memory_order_release); return true; }
                    if (cc == 99u)  { if (captureIngressFallbackEdge_(ev,  99u, v, 0u, 0.0f)) return true; ingress_fallback_nrpn_msb_[ch].store(v, std::memory_order_release); ingress_fallback_nrpn_msb_dirty_[ch].store(1u, std::memory_order_release); return true; }
                    if (cc == 98u)  { if (captureIngressFallbackEdge_(ev,  98u, v, 0u, 0.0f)) return true; ingress_fallback_nrpn_lsb_[ch].store(v, std::memory_order_release); ingress_fallback_nrpn_lsb_dirty_[ch].store(1u, std::memory_order_release); return true; }
                    if (cc == 6u)   { if (captureIngressFallbackEdge_(ev,   6u, v, 0u, 0.0f)) return true; ingress_fallback_data_entry_msb_[ch].store(v, std::memory_order_release); ingress_fallback_data_entry_msb_dirty_[ch].store(1u, std::memory_order_release); return true; }
                    if (cc == 38u)  { if (captureIngressFallbackEdge_(ev,  38u, v, 0u, 0.0f)) return true; ingress_fallback_data_entry_lsb_[ch].store(v, std::memory_order_release); ingress_fallback_data_entry_lsb_dirty_[ch].store(1u, std::memory_order_release); return true; }
                    if (cc == 121u) {
                        if (captureIngressFallbackEdge_(ev, 121u, 0u, 0u, 0.0f)) return true;
                        ingress_fallback_reset_controllers_[ch].store(1u, std::memory_order_release);
                        return true;
                    }
                    if (cc == 64u) {
                        if (captureIngressFallbackEdge_(ev, 64u, v, 0u, 0.0f)) return true;
                        ingress_fallback_sustain_[ch].store(v, std::memory_order_release);
                        ingress_fallback_sustain_dirty_[ch].store(1u, std::memory_order_release);
                        if (v < 64u) ingress_fallback_sustain_off_[ch].store(1u, std::memory_order_release);
                        return true;
                    }
                    if (cc == 66u) {
                        if (captureIngressFallbackEdge_(ev, 66u, v, 0u, 0.0f)) return true;
                        ingress_fallback_sostenuto_[ch].store(v, std::memory_order_release);
                        ingress_fallback_sostenuto_dirty_[ch].store(1u, std::memory_order_release);
                        if (v < 64u) ingress_fallback_sostenuto_off_[ch].store(1u, std::memory_order_release);
                        return true;
                    }
                    if (cc == 120u) {
                        ingress_fallback_all_sound_off_[ch].store(1u, std::memory_order_release);
                        return true;
                    }
                    if (cc == 123u) {
                        ingress_fallback_all_notes_off_[ch].store(1u, std::memory_order_release);
                        return true;
                    }
                }
                break;
            case SidTimedEventType::ProgramChange:
                // GM/channel Program Change is not an ArpSID factory-preset selector.
                // Drop it at the canonical fallback boundary so host playback cannot
                // reset selected patch identity. Explicit patch changes use BankSlot
                // or AU/VST preset APIs only.
                return true;
            case SidTimedEventType::TempoChange: {
                const float tempo = std::clamp(std::isfinite(ev.value_f32) ? ev.value_f32 : ev.value, 0.0f, 400.0f);
                if (captureIngressFallbackEdge_(ev, 0u, 0u, 0u, tempo)) return true;
                ingress_fallback_tempo_bits_.store(floatToBits_(tempo), std::memory_order_release);
                ingress_fallback_tempo_dirty_.store(1u, std::memory_order_release);
                return true;
            }
            case SidTimedEventType::TransportChange: {
                const float playingF = std::isfinite(ev.value_f32) ? ev.value_f32 : ev.value;
                if (captureIngressFallbackEdge_(ev, 0u, 0u, 0u, playingF)) return true;
                ingress_fallback_transport_playing_.store(playingF > 0.5f ? 1u : 0u, std::memory_order_release);
                ingress_fallback_transport_dirty_.store(1u, std::memory_order_release);
                return true;
            }
            case SidTimedEventType::MidiNoteOff:
                // audit #2: a NoteOff dropped under genuine ring fullness would leave
                // a STUCK note (the note can never release). Apply a stuck-note safety:
                // force an all-notes-off on the event's channel (or a global panic if
                // the channel is unresolved). Heavy, but it only fires under extreme
                // overflow, and a brief all-off is far better than a permanently hung
                // note. Counted for telemetry.
                if (ev.channel != kSidUnresolvedChannel)
                    ingress_fallback_all_notes_off_[(size_t)(ev.channel & 0x0Fu)].store(1u, std::memory_order_release);
                else
                    ingress_fallback_panic_.store(1u, std::memory_order_release);
                ingress_dropped_note_off_.fetch_add(1u, std::memory_order_relaxed);
                return true;
            case SidTimedEventType::MidiNoteOn:
                // audit #2: a dropped NoteOn is a lost note — no stuck state, and it
                // cannot be reconstructed from a latch, so it is genuinely dropped.
                // Count it so NoteOn loss under fullness is OBSERVABLE, not silent.
                ingress_dropped_note_on_.fetch_add(1u, std::memory_order_relaxed);
                break;   // -> return false -> also counted as a generic ingress drop
            default: break;
        }
        return false;
    }

    void clearIngressControllerFallbacksForChannel_(size_t ch) noexcept {
        ingress_fallback_sustain_off_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_sustain_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_sostenuto_off_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_sostenuto_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_mod_wheel_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_expression_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_channel_volume_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_bank_msb_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_bank_lsb_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_rpn_msb_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_rpn_lsb_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_nrpn_msb_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_nrpn_lsb_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_data_entry_msb_dirty_[ch].store(0u, std::memory_order_relaxed);
        ingress_fallback_data_entry_lsb_dirty_[ch].store(0u, std::memory_order_relaxed);
    }

    void resetIngressFallbacks_() noexcept {
        ingress_fallback_edge_ring_.reset();
        ingress_fallback_panic_.store(0u, std::memory_order_relaxed);
        ingress_fallback_transport_dirty_.store(0u, std::memory_order_relaxed);
        ingress_fallback_transport_playing_.store(0u, std::memory_order_relaxed);
        ingress_fallback_tempo_dirty_.store(0u, std::memory_order_relaxed);
        ingress_fallback_tempo_bits_.store(floatToBits_(120.0f), std::memory_order_relaxed);
        for (size_t ch = 0; ch < 16u; ++ch) {
            clearIngressControllerFallbacksForChannel_(ch);
            ingress_fallback_sustain_[ch].store(0u, std::memory_order_relaxed);
            ingress_fallback_sostenuto_[ch].store(0u, std::memory_order_relaxed);
            ingress_fallback_pitch_bend14_[ch].store(0x2000u, std::memory_order_relaxed);
            ingress_fallback_pitch_bend_dirty_[ch].store(0u, std::memory_order_relaxed);
            ingress_fallback_channel_pressure_[ch].store(0u, std::memory_order_relaxed);
            ingress_fallback_channel_pressure_dirty_[ch].store(0u, std::memory_order_relaxed);
            ingress_fallback_all_sound_off_[ch].store(0u, std::memory_order_relaxed);
            ingress_fallback_all_notes_off_[ch].store(0u, std::memory_order_relaxed);
            ingress_fallback_reset_controllers_[ch].store(0u, std::memory_order_relaxed);
            ingress_fallback_program_dirty_[ch].store(0u, std::memory_order_relaxed);
            for (size_t note = 0; note < 128u; ++note) {
                ingress_fallback_poly_pressure_[ch][note].store(0u, std::memory_order_relaxed);
                ingress_fallback_poly_pressure_dirty_[ch][note].store(0u, std::memory_order_relaxed);
            }
        }
    }

    // Audit #25 — every drain-side push goes through this helper so a
    // second-level pending_events_ overflow is counted and observable,
    // not silently dropped. The helper preserves the historical "push
    // best-effort and continue" semantics while finally surfacing the
    // drop count via `pending_events_drain_dropped_`.
    void drainPushOrDrop_(const SidTimedEvent& ev) noexcept {
        if (!pending_events_.push(ev)) {
            pending_events_drain_dropped_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Audit #24 — drain ordering-critical edges FIRST, preserving the
    // exact publish order across sustain stomps, RPN/NRPN handshakes,
    // transport edges and tempo pulses. Level latches are drained
    // afterwards as a "floor" so any state that didn't fit the ring is
    // still applied (at the cost of order, which is benign for levels).
    void flushIngressFallbackEdgeRing_() noexcept {
        IngressFallbackEdge edge{};
        while (ingress_fallback_edge_ring_.pop(edge)) {
            SidTimedEvent out{};
            out.type    = edge.type;
            out.channel = edge.channel;
            switch (edge.type) {
                case SidTimedEventType::MidiCC: {
                    out.ccNum    = edge.ccNum;
                    out.value    = std::clamp(static_cast<float>(edge.value7) / 127.0f, 0.0f, 1.0f);
                    out.value_f32= out.value;
                    break;
                }
                case SidTimedEventType::TempoChange: {
                    out.value    = edge.valueFloat;
                    out.value_f32= edge.valueFloat;
                    break;
                }
                case SidTimedEventType::TransportChange: {
                    out.value    = edge.valueFloat;
                    out.value_f32= edge.valueFloat;
                    break;
                }
                default:
                    // Unsupported type in the edge ring — skip (the latch
                    // path will still surface it as latest-state).
                    continue;
            }
            drainPushOrDrop_(out);
        }
    }

    void flushIngressFallbacksToPending_() noexcept {
        // Audit #24 — drain the ordered edge ring first so sustain/sostenuto/
        // RPN/NRPN/transport/tempo edges keep their semantic order before
        // the per-channel level latches collapse to "latest value".
        flushIngressFallbackEdgeRing_();
        if (ingress_fallback_panic_.exchange(0u, std::memory_order_acquire) != 0u) {
            SidTimedEvent ev{};
            ev.type = SidTimedEventType::Panic;
            drainPushOrDrop_(ev);
        }
        if (ingress_fallback_transport_dirty_.exchange(0u, std::memory_order_acquire) != 0u) {
            SidTimedEvent ev{}; ev.type = SidTimedEventType::TransportChange;
            const uint8_t playing = ingress_fallback_transport_playing_.load(std::memory_order_acquire);
            ev.value = playing ? 1.0f : 0.0f; ev.value_f32 = ev.value; drainPushOrDrop_(ev);
        }
        if (ingress_fallback_tempo_dirty_.exchange(0u, std::memory_order_acquire) != 0u) {
            SidTimedEvent ev{}; ev.type = SidTimedEventType::TempoChange;
            ev.value_f32 = bitsToFloat_(ingress_fallback_tempo_bits_.load(std::memory_order_acquire)); ev.value = ev.value_f32; drainPushOrDrop_(ev);
        }
        for (int ch = 0; ch < 16; ++ch) {
            const bool resetControllers = ingress_fallback_reset_controllers_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u;
            if (resetControllers) {
                clearIngressControllerFallbacksForChannel_(static_cast<size_t>(ch));
            }
            if (ingress_fallback_all_sound_off_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::AllSoundOff; ev.channel = static_cast<uint8_t>(ch); drainPushOrDrop_(ev);
                for (int note = 0; note < 128; ++note) {
                    ingress_fallback_poly_pressure_dirty_[(size_t)ch][(size_t)note].store(0u, std::memory_order_relaxed);
                    ingress_fallback_poly_pressure_[(size_t)ch][(size_t)note].store(0u, std::memory_order_relaxed);
                }
            }
            if (ingress_fallback_all_notes_off_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::AllNotesOff; ev.channel = static_cast<uint8_t>(ch); drainPushOrDrop_(ev);
                for (int note = 0; note < 128; ++note) {
                    ingress_fallback_poly_pressure_dirty_[(size_t)ch][(size_t)note].store(0u, std::memory_order_relaxed);
                    ingress_fallback_poly_pressure_[(size_t)ch][(size_t)note].store(0u, std::memory_order_relaxed);
                }
            }
            if (ingress_fallback_sustain_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 64u;
                const uint8_t v = ingress_fallback_sustain_[(size_t)ch].load(std::memory_order_acquire);
                ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev);
            }
            else if (ingress_fallback_sustain_off_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 64u; ev.value = 0.0f; ev.value_f32 = 0.0f; drainPushOrDrop_(ev);
            }
            if (ingress_fallback_sostenuto_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 66u;
                const uint8_t v = ingress_fallback_sostenuto_[(size_t)ch].load(std::memory_order_acquire);
                ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev);
            }
            else if (ingress_fallback_sostenuto_off_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 66u; ev.value = 0.0f; ev.value_f32 = 0.0f; drainPushOrDrop_(ev);
            }
            if (ingress_fallback_channel_pressure_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::ChannelPressure; ev.channel = static_cast<uint8_t>(ch);
                const uint8_t v = ingress_fallback_channel_pressure_[(size_t)ch].load(std::memory_order_acquire);
                ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev);
            }
            if (ingress_fallback_pitch_bend_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::PitchBend; ev.channel = static_cast<uint8_t>(ch);
                ev.data14 = ingress_fallback_pitch_bend14_[(size_t)ch].load(std::memory_order_acquire) & 0x3FFFu; drainPushOrDrop_(ev);
            }
            if (ingress_fallback_mod_wheel_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 1u;
                const uint8_t v = ingress_fallback_mod_wheel_[(size_t)ch].load(std::memory_order_acquire);
                ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev);
            }
            if (ingress_fallback_expression_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 11u;
                const uint8_t v = ingress_fallback_expression_[(size_t)ch].load(std::memory_order_acquire);
                ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev);
            }
            if (ingress_fallback_channel_volume_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 7u;
                const uint8_t v = ingress_fallback_channel_volume_[(size_t)ch].load(std::memory_order_acquire);
                ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev);
            }
            if (ingress_fallback_bank_msb_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 0u;
                const uint8_t v = ingress_fallback_bank_msb_[(size_t)ch].load(std::memory_order_acquire);
                ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev);
            }
            if (ingress_fallback_bank_lsb_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 32u;
                const uint8_t v = ingress_fallback_bank_lsb_[(size_t)ch].load(std::memory_order_acquire);
                ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev);
            }
            if (ingress_fallback_rpn_msb_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) { SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 101u; const uint8_t v = ingress_fallback_rpn_msb_[(size_t)ch].load(std::memory_order_acquire); ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev); }
            if (ingress_fallback_rpn_lsb_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) { SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 100u; const uint8_t v = ingress_fallback_rpn_lsb_[(size_t)ch].load(std::memory_order_acquire); ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev); }
            if (ingress_fallback_nrpn_msb_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) { SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 99u; const uint8_t v = ingress_fallback_nrpn_msb_[(size_t)ch].load(std::memory_order_acquire); ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev); }
            if (ingress_fallback_nrpn_lsb_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) { SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 98u; const uint8_t v = ingress_fallback_nrpn_lsb_[(size_t)ch].load(std::memory_order_acquire); ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev); }
            if (ingress_fallback_data_entry_msb_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) { SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 6u; const uint8_t v = ingress_fallback_data_entry_msb_[(size_t)ch].load(std::memory_order_acquire); ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev); }
            if (ingress_fallback_data_entry_lsb_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire) != 0u) { SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 38u; const uint8_t v = ingress_fallback_data_entry_lsb_[(size_t)ch].load(std::memory_order_acquire); ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev); }
            if (resetControllers) {
                SidTimedEvent ev{}; ev.type = SidTimedEventType::MidiCC; ev.channel = static_cast<uint8_t>(ch); ev.ccNum = 121u; ev.value = 0.0f; ev.value_f32 = 0.0f; drainPushOrDrop_(ev);
            }
            // ProgramChange fallback dirty bit is consumed as metadata-only.
            (void)ingress_fallback_program_dirty_[(size_t)ch].exchange(0u, std::memory_order_acquire);
            for (int note = 0; note < 128; ++note) {
                if (ingress_fallback_poly_pressure_dirty_[(size_t)ch][(size_t)note].exchange(0u, std::memory_order_acquire) != 0u) {
                    SidTimedEvent ev{}; ev.type = SidTimedEventType::PolyPressure; ev.channel = static_cast<uint8_t>(ch); ev.pitch = static_cast<int16_t>(note);
                    const uint8_t v = ingress_fallback_poly_pressure_[(size_t)ch][(size_t)note].load(std::memory_order_acquire);
                    ev.value = std::clamp(static_cast<float>(v) / 127.0f, 0.0f, 1.0f); ev.value_f32 = ev.value; drainPushOrDrop_(ev);
                }
            }
        }
    }

    bool pushIngress_(const SidTimedEvent& ev) noexcept {
        int32_t head = ingress_free_head_.load(std::memory_order_acquire);
        while (true) {
            if (head == kIngressNull_) {
                if (pushIngressSpill_(ev)) return true;
                if (captureIngressFallback_(ev)) return true;
                ingress_dropped_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            const int32_t next = ingress_nodes_[(size_t)head].next.load(std::memory_order_acquire);
            if (ingress_free_head_.compare_exchange_weak(head, next, std::memory_order_acq_rel, std::memory_order_acquire))
                break;
        }
        ingress_nodes_[(size_t)head].event = ev;
        std::atomic_thread_fence(std::memory_order_release);
        if (ingress_nodes_[(size_t)head].event.arrival_order == 0u)
            ingress_nodes_[(size_t)head].event.arrival_order = ingress_sequence_.fetch_add(1u, std::memory_order_acq_rel);
        int32_t pending = ingress_pending_head_.load(std::memory_order_acquire);
        do {
            ingress_nodes_[(size_t)head].next.store(pending, std::memory_order_release);
        } while (!ingress_pending_head_.compare_exchange_weak(pending, head, std::memory_order_release, std::memory_order_acquire));
        return true;
    }

    void flushIngressToPending_() noexcept {
        int32_t head = ingress_pending_head_.exchange(kIngressNull_, std::memory_order_acq_rel);
        std::atomic_thread_fence(std::memory_order_acquire);
        while (head != kIngressNull_) {
            const int32_t next = ingress_nodes_[(size_t)head].next.load(std::memory_order_acquire);
            if (!pending_events_.push(ingress_nodes_[(size_t)head].event))
                ingress_dropped_.fetch_add(1, std::memory_order_relaxed);
            int32_t freeHead = ingress_free_head_.load(std::memory_order_acquire);
            do {
                ingress_nodes_[(size_t)head].next.store(freeHead, std::memory_order_release);
            } while (!ingress_free_head_.compare_exchange_weak(freeHead, head, std::memory_order_release, std::memory_order_acquire));
            head = next;
        }
        flushIngressSpillToPending_();
        flushIngressFallbacksToPending_();
    }

    static constexpr size_t kFirstSidParamIndex_() noexcept { return static_cast<size_t>(kParamSidRegD400); }
    static constexpr size_t kLastSidParamIndex_() noexcept { return static_cast<size_t>(kParamSidRegD41D); }

    static std::vector<SidModRoute> sanitizedModRoutes_(const std::vector<SidModRoute>& src) {
        std::vector<SidModRoute> out = src;
        for (SidModRoute& r : out) r.sanitize();
        return out;
    }

    // Non-RT only: allocates. Delegates to the shared free-function canonicalizer and
    // additionally records the editor-layout-blob truncation telemetry. The realtime
    // apply path (applyStateRootBySwap) must NOT call this — see its contract.
    void sanitizeStateRoot_(SidStateRootV1& root) noexcept {
        const bool willTruncateEditorBlob =
            root.document.editor_layout_blob.size() > 1u * 1024u * 1024u;
        sidCanonicalizeStateRootForApply(root);
        if (willTruncateEditorBlob)
            editor_layout_blob_truncated_.store(true, std::memory_order_relaxed);
    }

    void rebuildParameterRegisterImage_(SidRegisterImage& out) const noexcept {
        out.clear();
        constexpr int kFirstSidRegParam = static_cast<int>(kParamSidRegD400);
        constexpr int kLastSidRegParam  = static_cast<int>(kParamSidRegD41D);
        for (int pid = kFirstSidRegParam; pid <= kLastSidRegParam; ++pid) {
            float v = sidStateRootParamValueFromHydratedValuesRT(state_root_, pid);
            if (!std::isfinite(v)) v = 0.0f;
            v = std::clamp(v, 0.0f, 1.0f);
            const int regIdx = pid - kFirstSidRegParam;
            const uint8_t byte = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::lround(v * 255.0f)), 0, 255));
            out.set(regIdx, byte);
        }
    }

    SidStateRootV1 state_root_{};
    std::array<SidTimedEvent, kIngressSpillCapacity_> ingress_spill_events_{};
    std::atomic<int> ingress_spill_count_{0};
    std::atomic_flag ingress_spill_lock_ = ATOMIC_FLAG_INIT;

    SidVariantProfile variant_profile_ = sidDefaultVariantProfile();
    SidStaticParams static_params_ = makeSidStaticParams(variant_profile_);
    SidDynamicState dynamic_state_{};
    SidMeasuredPosterior posterior_{};
    SidRegisterImage register_image_{};
    SidRuntimeBackend* backend_{};
    SidCycleClockState sid_cycle_clock_{};
    SidRegisterImage parameter_register_image_{};
    SidRegisterImage previous_parameter_register_image_{};
    // audit P0.3: explicit backing storage — the default ctor is now non-allocating.
    SidTimedEventQueue pending_events_{SidTimedEventQueue::AllocateStorage{}};
    std::unique_ptr<IngressNode[]> ingress_nodes_{};
    std::atomic<int32_t> ingress_free_head_{0};
    std::atomic<int32_t> ingress_pending_head_{kIngressNull_};
    // audit #12: 64-bit so cumulative ingress/dispatch drop totals never wrap in long sessions.
    std::atomic<uint64_t> ingress_dropped_{0};
    std::atomic<uint64_t> direct_dispatch_dropped_{0};
    // audit #2: per-type NoteOn/NoteOff drop telemetry under genuine ring fullness.
    // A dropped NoteOff additionally triggers the all-notes-off stuck-note safety
    // (see captureIngressFallback_); a dropped NoteOn is a benign lost note.
    std::atomic<uint64_t> ingress_dropped_note_on_{0};
    std::atomic<uint64_t> ingress_dropped_note_off_{0};
    // Audit #24 — bounded MPSC ring for ordering-critical fallback events.
    // Captured alongside the per-channel "latest value" latches so the level
    // latch remains the floor; the ring preserves transition order for
    // sustain (CC64), sostenuto (CC66), RPN/NRPN (CC101/100/99/98),
    // data-entry (CC6/38), transport edges and tempo edges.
    IngressFallbackEdgeRing<256> ingress_fallback_edge_ring_{};
    // Audit #25 — count pending_events_.push() failures during drain. The
    // legacy drain path ignored push return values, so a second-level
    // overflow could silently lose recovered critical state. Diagnostics
    // surface this counter.
    std::atomic<uint64_t> pending_events_drain_dropped_{0};
    bool ingress_freelist_initialized_ = false;
    std::atomic<uint32_t> ingress_sequence_{1u};
    std::atomic<uint8_t> ingress_fallback_panic_{0u};
    std::atomic<uint8_t> ingress_fallback_transport_playing_{0u};
    std::atomic<uint8_t> ingress_fallback_transport_dirty_{0u};
    std::atomic<uint32_t> ingress_fallback_tempo_bits_{floatToBits_(0.0f)};
    std::atomic<uint8_t> ingress_fallback_tempo_dirty_{0u};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_all_sound_off_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_all_notes_off_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_sustain_off_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_sostenuto_off_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_sustain_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_sustain_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_sostenuto_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_sostenuto_dirty_{};
    std::array<std::atomic<uint16_t>, 16> ingress_fallback_pitch_bend14_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_pitch_bend_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_channel_pressure_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_channel_pressure_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_mod_wheel_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_mod_wheel_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_expression_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_expression_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_channel_volume_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_channel_volume_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_bank_msb_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_bank_msb_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_bank_lsb_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_bank_lsb_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_rpn_msb_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_rpn_msb_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_rpn_lsb_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_rpn_lsb_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_nrpn_msb_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_nrpn_msb_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_nrpn_lsb_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_nrpn_lsb_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_data_entry_msb_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_data_entry_msb_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_data_entry_lsb_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_data_entry_lsb_dirty_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_reset_controllers_{};
    std::array<std::atomic<uint8_t>, 16> ingress_fallback_program_dirty_{};
    std::array<std::array<std::atomic<uint8_t>, 128>, 16> ingress_fallback_poly_pressure_{};
    std::array<std::array<std::atomic<uint8_t>, 128>, 16> ingress_fallback_poly_pressure_dirty_{};

    // ----------------------------------------------------------------------
    // Canonical merge lane infrastructure (Passes E/F).
    // Primary ingress truth surface. All wrappers write here in normal flow.
    // ----------------------------------------------------------------------
    std::array<SidMergeEventLane, kMergeLaneCount> mergeLanes_{};
    std::atomic<uint32_t> mergeArrivalCounter_{1u};
    std::atomic<uint64_t> mergeOverflowTelemetry_{0u};   // audit #12: 64-bit cumulative drop telemetry
    // Persistent heap-backed scratch avoids per-block stack allocation while
    // keeping the runtime model footprint bounded for AUv3 / multi-instance use.
    std::array<SidMergeCandidate, kMergeScratchMax> mergeScratch_{};
    std::atomic<bool> editor_layout_blob_truncated_{false};

    bool hasPendingFallbacks_() const noexcept {
        if (!ingress_fallback_edge_ring_.empty()) return true;
        if (ingress_fallback_panic_.load(std::memory_order_acquire) != 0u) return true;
        if (ingress_fallback_transport_dirty_.load(std::memory_order_acquire) != 0u) return true;
        if (ingress_fallback_tempo_dirty_.load(std::memory_order_acquire) != 0u) return true;
        for (size_t ch = 0; ch < 16u; ++ch) {
            if (ingress_fallback_all_sound_off_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_all_notes_off_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_sustain_off_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_sostenuto_off_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_sustain_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_sostenuto_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_pitch_bend_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_channel_pressure_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_mod_wheel_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_expression_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_channel_volume_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_bank_msb_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_bank_lsb_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_rpn_msb_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_rpn_lsb_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_nrpn_msb_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_nrpn_lsb_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_data_entry_msb_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_data_entry_lsb_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_reset_controllers_[ch].load(std::memory_order_acquire) != 0u) return true;
            if (ingress_fallback_program_dirty_[ch].load(std::memory_order_acquire) != 0u) return true;
            for (size_t note = 0; note < 128u; ++note) {
                if (ingress_fallback_poly_pressure_dirty_[ch][note].load(std::memory_order_acquire) != 0u) return true;
            }
        }
        return false;
    }

    void resetMergeLanes_() noexcept {
        for (auto& lane : mergeLanes_) lane.reset();
        // Start at 2 to avoid collision with note_on_counter which starts at 1 on first note.
        mergeArrivalCounter_.store(2u, std::memory_order_relaxed);
        mergeOverflowTelemetry_.store(0u, std::memory_order_relaxed);
    }

    void resetAllIngressSurfaces_() noexcept {
        pending_events_.reset();
        resetIngress_();
        resetIngressFallbacks_();
        resetMergeLanes_();
    }
};

} // namespace ArpSID
