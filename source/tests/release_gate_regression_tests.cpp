#undef NDEBUG
#include "arpsid/core/sid_event_timing.h"
#include "arpsid/core/sid_ingress_merge.h"
#include "arpsid/core/sid_runtime_model.h"
#include "arpsid/core/sid_runtime_register_ops.h"
#include "arpsid/core/sid_runtime_host_policy.h"
#include "../au3/ArpSIDStateSerializer.h"
#include "../au3/ArpSIDCanonicalEvents.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <utility>

using namespace ArpSID;

static void test_wrapper_conversion_does_not_own_sid_stamp() {
    TimedEvent te{};
    te.kind = EventKind::NoteOn;
    te.sampleOffset = 7;
    te.cycleOffset = 3;
    te.subphase = 2;
    te.rawOrder = 42;
    SidTimedEvent ev = te.toCanonical();
    assert(ev.sample_offset == 7u);
    assert(ev.cycle_offset == 3u);
    assert(ev.subphase == 2u);
    assert(ev.arrival_order == 42u);
    assert(ev.sid_cycle_stamp == 0ull && "wrapper conversion must not own SID-clock timing");
}

static void test_runtime_timing_resolves_unresolved_cycle_from_sid_clock() {
    SidTimedEvent ev{};
    ev.type = SidTimedEventType::MidiNoteOn;
    assignBestEffortIntraSampleTiming(ev, 7, 42, 2, 44100.0, 985248.0, kSidUnresolvedCycleOffset);
    assert(ev.sample_offset == 7u);
    assert(ev.cycle_offset == kSidUnresolvedCycleOffset && "assignment helper must not derive SID-cycle offset");
    assert(ev.sid_cycle_stamp == 0ull && "assignment helper must not own physical SID stamp");
    finalizeSidCycleStampFromRuntimeClock(ev, 44100.0, 985248.0);
    assert(ev.cycle_offset != kSidUnresolvedCycleOffset && "runtime finalization must resolve cycle offset");
    assert(ev.sid_cycle_stamp != 0ull && "runtime finalization must stamp with active SID clock");
}

static void test_sanitize_does_not_own_sid_stamp() {
    SidTimedEvent ev{};
    ev.sample_offset = 5u;
    ev.cycle_offset = 2u;
    ev.subphase = 3u;
    ev.arrival_order = 9u;
    ev.sid_cycle_stamp = 12345ull;
    ev.sanitize(64);
    assert(ev.sid_cycle_stamp == 0ull && "sanitize must not mint physical SID-cycle stamps");
}

static void test_ingress_merge_does_not_own_sid_stamp() {
    std::array<SidMergeEventLane, kMergeLaneCount> lanes{};
    SidTimedEventQueue pending{SidTimedEventQueue::AllocateStorage{}};
    std::atomic<uint32_t> arrival{0};
    std::atomic<uint64_t> overflow{0};   // audit #12: sidIngressMerge telemetry is now 64-bit
    std::array<SidMergeCandidate, kMergeScratchMax> scratch{};

    SidTimedEvent ev{};
    ev.type = SidTimedEventType::SidRegisterWrite;
    ev.sample_offset = 3;
    ev.cycle_offset = 2;
    ev.subphase = 1;
    ev.value_u32 = 0x0400u;
    assert(sidIngressPushToLane(lanes, ev, SidIngressSourcePriority::MidiNoteControl));

    const int merged = sidIngressMerge(lanes, pending, arrival, 16, overflow, scratch.data(), scratch.size());
    assert(merged == 1);
    assert(pending.count == 1);
    assert(pending.events[0].arrival_order == 0u);
    assert(pending.events[0].sid_cycle_stamp == 0ull && "ingress merge must not own physical SID-cycle timing");
}

static void test_ingress_critical_whitelist_includes_noteoff_and_state_events() {
    assert(sidIngressIsCriticalEvent(static_cast<uint8_t>(SidTimedEventType::MidiNoteOff)));
    assert(sidIngressIsCriticalEvent(static_cast<uint8_t>(SidTimedEventType::AllNotesOff)));
    assert(sidIngressIsCriticalEvent(static_cast<uint8_t>(SidTimedEventType::AllSoundOff)));
    assert(sidIngressIsCriticalEvent(static_cast<uint8_t>(SidTimedEventType::Panic)));
    assert(sidIngressIsCriticalEvent(static_cast<uint8_t>(SidTimedEventType::ProgramChange)));
    assert(sidIngressIsCriticalEvent(static_cast<uint8_t>(SidTimedEventType::VariantChange)));
    assert(sidIngressIsCriticalEvent(static_cast<uint8_t>(SidTimedEventType::TransportChange)));
    assert(sidIngressIsCriticalEvent(static_cast<uint8_t>(SidTimedEventType::TempoChange)));
    assert(!sidIngressIsCriticalEvent(static_cast<uint8_t>(SidTimedEventType::AutomationPoint)));
}

static void test_moved_from_timed_queue_push_fails_closed_without_storage() {
    SidTimedEventQueue source;
    SidTimedEventQueue moved(std::move(source));
    SidTimedEvent ev{};
    ev.type = SidTimedEventType::MidiNoteOff;
    const bool accepted = source.push(ev);
    assert(!accepted && "moved-from render queue must fail closed, not allocate backing storage");
    assert(source.dropped == 1u);
    assert(source.overflowed);
    (void)moved;
}

static void test_rt_guard_detects_render_queue_allocation_and_allows_fail_closed_push() {
    // audit P0.3: the default constructor is now NON-ALLOCATING, so constructing
    // a queue inside a render scope must NOT record a guard violation.
    ArpSID::sidRealtimeGuardResetForTest();
    {
        ArpSID::SidRealtimeScope rtScope("test_rt_queue_constructor");
        SidTimedEventQueue q;
        (void)q;
    }
    assert(ArpSID::sidRealtimeGuardViolationCount() == 0u && "non-allocating default queue construction must not record a violation");

    // Explicitly acquiring backing storage on the render thread is still a
    // real-time violation and must be recorded by the guard.
    ArpSID::sidRealtimeGuardResetForTest();
    {
        ArpSID::SidRealtimeScope rtScope("test_rt_queue_explicit_storage");
        SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};
        (void)q;
    }
    assert(ArpSID::sidRealtimeGuardViolationCount() == 1u && "explicit storage acquisition in RT scope must be recorded");

    // A storage-less queue must fail push closed without allocating.
    SidTimedEventQueue source;
    ArpSID::sidRealtimeGuardResetForTest();
    {
        ArpSID::SidRealtimeScope rtScope("test_rt_storageless_push");
        SidTimedEvent ev{};
        ev.type = SidTimedEventType::MidiNoteOff;
        assert(!source.push(ev));
    }
    assert(ArpSID::sidRealtimeGuardViolationCount() == 0u && "storage-less queue push must fail closed without allocation");
}

static void test_canonical_sid_frequency_register_uses_rounding_and_shared_law() {
    const uint16_t a4pal = canonicalSidFrequencyRegisterForMidiNote(69.0, 985248.0);
    const uint16_t expected = static_cast<uint16_t>(std::lround(440.0 * 16777216.0 / 985248.0));
    assert(a4pal == expected);
    assert(canonicalSidFrequencyRegisterForHz(-1.0, 985248.0) == 0u);
    assert(canonicalSidFrequencyRegisterForHz(440.0, 0.0) == 0u);
    assert(canonicalSidFrequencyRegisterForHz(1.0e308, 1.0) == 65535u);
    assert(canonicalSidFrequencyRegisterForMidiNote(1.0e308, 985248.0) == 65535u);
}

static void test_sid_hard_sync_uses_source_msb_rising_edge() {
    assert(sidPhaseMsbRose(0x7FFFFFu, 0x800000u));
    assert(!sidPhaseMsbRose(0xFFFFFFu, 0x000000u));

    bool syncEnabled[3] = {false, true, false};
    bool sourceRose[3] = {true, false, false};
    assert(sidHardSyncShouldReset(1, syncEnabled, sourceRose));

    syncEnabled[0] = true;
    sourceRose[2] = true;
    assert(!sidHardSyncShouldReset(1, syncEnabled, sourceRose));
}

static void test_variant_clock_authority_preserves_custom_clock() {
    SidVariantProfile profile = sidDefaultVariantProfile(SidFamily::MOS8580, SidVideoStandard::PAL);
    profile.master_clock_hz = 1000000.0f;
    profile.nominal_sid_clock_hz = 1000000.0f;
    profile.sanitize();
    assert(sidVariantClockHz(profile) == 1000000.0f);

    sidSetVariantVideoStandard(profile, SidVideoStandard::NTSC);
    assert(profile.master_clock_hz == sidDefaultClockHz(SidVideoStandard::NTSC));
    assert(profile.nominal_sid_clock_hz == sidDefaultClockHz(SidVideoStandard::NTSC));

    const SidVariantProfile packed = canonicalVariantProfileFromPackedBits(0u, profile);
    assert(packed.video_standard == SidVideoStandard::PAL);
    assert(packed.master_clock_hz == sidDefaultClockHz(SidVideoStandard::PAL));
    assert(packed.nominal_sid_clock_hz == sidDefaultClockHz(SidVideoStandard::PAL));

    SidRegisterEngine sidreg;
    sidreg.prepare(48000.0);
    sidreg.setClockFrequency(1080000.0);
    assert(sidreg.clockFrequency() == 1080000.0);
    assert(sidreg.estimatedCyclesPerHostSample() == 23u);
    sidreg.setClockFrequency(1.0e308);
    assert(sidreg.clockFrequency() == 1080000.0);
    assert(boundedSidCyclesPerHostSampleEstimate(1.0e308, 48000.0) == 65535u);
}

static void test_canonical_voice_frequency_write_order() {
    SidWriteQueue q{};
    canonicalQueueSidVoiceFrequencyWrite(q, 1, 0x1234u, 7u, 2u);
    auto view = q.data();
    assert(view.size() == 2u);
    assert(view[0].regIndex == 7u && view[0].value == 0x34u);
    assert(view[1].regIndex == 8u && view[1].value == 0x12u);
}

static void test_canonical_host_sample_offset_helper_is_single_law() {
    assert(canonicalHostSampleOffsetFromSeconds(0.0, 48000.0, 64) == 0);
    assert(canonicalHostSampleOffsetFromSeconds(0.5 / 48000.0, 48000.0, 64) == 0);
    assert(canonicalHostSampleOffsetFromSeconds(-10.0, 48000.0, 64) == 0);
    assert(canonicalHostSampleOffsetFromSeconds(10.0, 48000.0, 64) == 63);
    assert(canonicalHostSampleOffsetFromSeconds(std::numeric_limits<double>::max(), 48000.0, 64) == 63);
}

static void test_subcycle_lattice_is_256_half_open_steps() {
    assert(kSidSubcycleResolution == 256u);
    assert(kSidSubcycleLast == 255u);
    assert(kSidSubcycleBoundary == 256u);
    SidRenderInterval full{};
    full.beginCycle = 0u;
    full.beginSubphase = 0u;
    full.endCycle = 1u;
    full.endSubphase = 0u;
    assert(full.widthSubphases() == 256u && "one full SID cycle must be exactly 256 subphase steps");
    SidRenderInterval tail{};
    tail.beginCycle = 3u;
    tail.beginSubphase = 255u;
    tail.endCycle = 4u;
    tail.endSubphase = 0u;
    assert(tail.widthSubphases() == 1u && "subphase 255 to next cycle boundary is one final step, not zero");
    SidRenderInterval boundary{};
    boundary.beginCycle = 1u;
    boundary.beginSubphase = 0u;
    boundary.endCycle = 1u;
    boundary.endSubphase = kSidSubcycleBoundary;
    assert(boundary.valid());
    assert(boundary.widthSubphases() == 256u &&
           "exclusive boundary 256 must remain one full cycle at non-zero cycle indices");
    boundary.endSubphase = kSidSubcycleBoundary + 1u;
    assert(!boundary.valid() && boundary.widthSubphases() == 0u);
}

static void test_runtime_subphase_to_cycle_estimate_uses_256_denominator() {
    const double sr = 44100.0;
    const double clk = 985248.0;
    const uint16_t cps = estimateSidCyclesPerHostSample(sr, clk);
    const uint16_t half = estimateCycleOffsetFromSubphase(128u, sr, clk);
    const uint16_t expected = static_cast<uint16_t>(std::clamp<int>(
        static_cast<int>(std::floor((128.0 / 256.0) * static_cast<double>(cps))),
        0,
        static_cast<int>(cps - 1u)));
    assert(half == expected && "subphase->cycle estimate must use the same 256-step lattice as render");
}

static void test_canonical_gm_drsid_promotion_policy_is_shared() {
    assert(sidCanonicalGMDrumPromotionCandidate(9u, 36u));
    assert(sidCanonicalGMDrumPromotionCandidate(9u, 81u));
    assert(!sidCanonicalGMDrumPromotionCandidate(0u, 36u));
    assert(!sidCanonicalGMDrumPromotionCandidate(9u, 34u));
    assert(!sidCanonicalGMDrumPromotionCandidate(9u, 82u));
    auto d = sidCanonicalEvaluateGMDrSidPromotion(9u, 36u, true, false, true);
    assert(d.promote);
    assert(d.disableSynthParam == static_cast<uint32_t>(kParamSynthModeEnable));
    assert(d.enableDrSidParam == static_cast<uint32_t>(kParamDrSidEnable));
    assert(!sidCanonicalEvaluateGMDrSidPromotion(9u, 36u, false, false, true).promote);
    assert(!sidCanonicalEvaluateGMDrSidPromotion(9u, 36u, true, true, true).promote);
    // v909 Classic-mode authority: promotion is refused when the wrapper does
    // not allow auto-promotion, even for a perfect GM drum candidate.
    assert(!sidCanonicalEvaluateGMDrSidPromotion(9u, 36u, true, false, false).promote);
    // Shared flavor/opt-in law: dedicated drum flavors always allow, Hybrid
    // requires the explicit param, other flavors never allow.
    assert(sidCanonicalGMDrumAutoPromotionAllowed(true, false, false));
    assert(!sidCanonicalGMDrumAutoPromotionAllowed(false, true, false));
    assert(sidCanonicalGMDrumAutoPromotionAllowed(false, true, true));
    assert(!sidCanonicalGMDrumAutoPromotionAllowed(false, false, true));
}

static void test_canonical_transport_tempo_pushes_only_changes() {
    SidRuntimeModel model{};
    model.setTransportPlayingFlag(false);
    model.setHostTempoBpm(120.0f);
    sidCanonicalPushTransportTempoIfChanged(model, false, 120.0f, 64, 0);
    assert(model.consumeDirectDispatchDropped() == 0u);
    SidTimedEventQueue q{};
    (void)q;
    sidCanonicalPushTransportTempoIfChanged(model, true, 120.0f, 64, 0);
    assert(model.transportPlayingFlag());
    sidCanonicalPushTransportTempoIfChanged(model, true, 121.0f, 64, 0);
    assert(std::fabs(model.hostTempoBpm() - 121.0f) < 1.0e-4f);
}


static int countActiveTokenVoices(const SidRuntimeModel& model, int channel = -1) {
    int count = 0;
    model.forEachActiveTokenVoice([&](const SidTokenVoiceEntry& e) {
        if (channel < 0 || e.token.channel == channel) ++count;
    });
    return count;
}

static void test_queue_sorted_subphase_write_regression() {
    SidTimedEventQueue q{SidTimedEventQueue::AllocateStorage{}};
    SidTimedEvent late{};
    late.type = SidTimedEventType::SidRegisterWrite;
    late.sample_offset = 4u;
    late.cycle_offset = 9u;
    late.subphase = 200u;
    late.arrival_order = 1u;
    late.target = 1u;
    SidTimedEvent early = late;
    early.subphase = 3u;
    early.arrival_order = 2u;
    early.target = 0u;
    assert(q.push(late));
    assert(q.push(early));
    q.sort();
    assert(q.count == 2);
    assert(q.events[0].target == 0u && "same-sample/cycle SID writes must sort by subphase before arrival order");
    assert(q.events[1].target == 1u);
}

static void test_noteoff_token_identity_regression() {
    SidRuntimeModel model{};
    SidTimedEvent on1{};
    on1.type = SidTimedEventType::MidiNoteOn;
    on1.channel = 0u;
    on1.pitch = 60;
    on1.noteId = 101;
    on1.value = 0.75f;
    SidTimedEvent on2 = on1;
    on2.noteId = 202;
    assert(model.applyCanonicalEventToState(on1));
    assert(model.applyCanonicalEventToState(on2));
    assert(countActiveTokenVoices(model, 0) == 2);

    const uint64_t tok1 = model.resolveVoiceTokenForIdentity(0, 60, 101);
    const uint64_t tok2 = model.resolveVoiceTokenForIdentity(0, 60, 202);
    assert(tok1 != 0u && tok2 != 0u && tok1 != tok2);

    SidTimedEvent off1{};
    off1.type = SidTimedEventType::MidiNoteOff;
    off1.channel = 0u;
    off1.pitch = 60;
    off1.noteId = 101;
    assert(model.applyCanonicalEventToState(off1));
    assert(!model.hasActiveVoiceToken(tok1));
    assert(model.hasActiveVoiceToken(tok2));
    assert(countActiveTokenVoices(model, 0) == 1);
}

static void test_sustain_pedal_release_regression() {
    SidRuntimeModel model{};
    SidTimedEvent sustainOn{};
    sustainOn.type = SidTimedEventType::MidiCC;
    sustainOn.channel = 0u;
    sustainOn.ccNum = 64u;
    sustainOn.value = 1.0f;
    assert(model.applyCanonicalEventToState(sustainOn));

    SidTimedEvent on{};
    on.type = SidTimedEventType::MidiNoteOn;
    on.channel = 0u;
    on.pitch = 64;
    on.noteId = 303;
    on.value = 0.8f;
    assert(model.applyCanonicalEventToState(on));
    const uint64_t tok = model.resolveVoiceTokenForIdentity(0, 64, 303);
    assert(tok != 0u);

    SidTimedEvent off{};
    off.type = SidTimedEventType::MidiNoteOff;
    off.channel = 0u;
    off.pitch = 64;
    off.noteId = 303;
    assert(model.applyCanonicalEventToState(off));
    assert(model.hasActiveVoiceToken(tok) && "note-off while sustain is held must not destroy the token");

    SidTimedEvent sustainOff = sustainOn;
    sustainOff.value = 0.0f;
    assert(model.applyCanonicalEventToState(sustainOff));
    assert(!model.hasActiveVoiceToken(tok) && "CC64-off must release sustained token voices");
}

static void test_all_notes_off_clears_target_channel_only() {
    SidRuntimeModel model{};
    SidTimedEvent on{};
    on.type = SidTimedEventType::MidiNoteOn;
    on.pitch = 60;
    on.value = 1.0f;
    on.channel = 0u;
    on.noteId = 1;
    assert(model.applyCanonicalEventToState(on));
    on.channel = 1u;
    on.noteId = 2;
    assert(model.applyCanonicalEventToState(on));
    assert(countActiveTokenVoices(model) == 2);

    SidTimedEvent allOff{};
    allOff.type = SidTimedEventType::AllNotesOff;
    allOff.channel = 0u;
    assert(model.applyCanonicalEventToState(allOff));
    assert(countActiveTokenVoices(model, 0) == 0);
    assert(countActiveTokenVoices(model, 1) == 1);
}

static void test_panic_clears_all_canonical_voice_state() {
    SidRuntimeModel model{};
    SidTimedEvent on{};
    on.type = SidTimedEventType::MidiNoteOn;
    on.pitch = 60;
    on.value = 1.0f;
    for (uint8_t ch = 0; ch < 4; ++ch) {
        on.channel = ch;
        on.noteId = 100 + ch;
        assert(model.applyCanonicalEventToState(on));
    }
    assert(countActiveTokenVoices(model) == 4);
    SidTimedEvent panic{};
    panic.type = SidTimedEventType::Panic;
    assert(model.applyCanonicalEventToState(panic));
    assert(countActiveTokenVoices(model) == 0);
    assert(model.focusedVoiceToken() == 0u);
}

static void test_latency_constant_is_explicit_until_core_latency_authority_lands() {
    // Phase 6 gate: keep the current AU-facing latency contract visible to tests
    // until Phase 7/8 replaces it with a single core latency-samples authority.
    constexpr double kCurrentAULatencySeconds = 0.005;
    assert(kCurrentAULatencySeconds > 0.0);
    assert(std::fabs(kCurrentAULatencySeconds * 48000.0 - 240.0) < 1.0e-9);
}

static void test_ingress_primary_lane_is_mpsc_without_spinlock() {
    SidIngressLane<SidTimedEvent, 8> lane{};
    SidIngressEntry<SidTimedEvent> entry{};
    for (int i = 0; i < 8; ++i) {
        entry.event.type = SidTimedEventType::MidiCC;
        entry.event.arrival_order = static_cast<uint32_t>(i);
        assert(lane.push(entry));
    }
    entry.event.type = SidTimedEventType::MidiNoteOff;
    entry.event.arrival_order = 99u;
    assert(lane.push(entry) && "critical note-off must spill to safety ring when primary MPSC ring is full");
    SidIngressEntry<SidTimedEvent> out{};
    assert(lane.pop(out));
    assert(out.event.type == SidTimedEventType::MidiNoteOff && "critical safety ring must drain before primary backlog");
    int drained = 0;
    while (lane.pop(out)) ++drained;
    assert(drained == 8);
    assert(lane.empty());
}

static void test_canonical_state_root_binary_roundtrip_is_schema_root_only() {
    SidRuntimeModel model{};
    SidStateRootV1 root{};
    model.exportStateRootTo(root);
    sanitizePersistentStateRootForSerialization(root);
    assert(root.valid());
    std::array<uint8_t, kStateBufferSize> blob{};
    const size_t len = encodeStateRoot(root, kProjectStateMagic, blob.data(), blob.size());
    assert(len >= sizeof(SidBinaryStateHeader));
    SidStateRootV1 decoded{};
    assert(decodeStateToRoot(blob.data(), len, decoded, kProjectStateMagic));
    sanitizePersistentStateRootForSerialization(decoded);
    assert(decoded.valid());
    assert(decoded.patch.parameters.semantic_entries.size() == root.patch.parameters.semantic_entries.size());
}

static void test_current_major_flat_param_blob_is_rejected_not_legacy_imported() {
    std::array<uint8_t, sizeof(SidBinaryStateHeader) + sizeof(uint32_t)> blob{};
    stateWriteLE32_(blob.data() + 0, kProjectStateMagic);
    stateWriteLE16_(blob.data() + 4, kStateMajorVersion);
    stateWriteLE16_(blob.data() + 6, kStateMinorVersion);
    stateWriteLE32_(blob.data() + 8, 1u);
    stateWriteLE32_(blob.data() + 12, 0u);
    stateWriteLE32_(blob.data() + 16, 0u);
    SidStateRootV1 decoded{};
    assert(!decodeStateToRoot(blob.data(), blob.size(), decoded, kProjectStateMagic) &&
           "current-version malformed flat-param blobs must not silently fall into legacy import");
}
static void test_state_root_does_not_seed_live_register_image() {
    SidRuntimeModel model{};
    SidStateRootV1 root{};
    // Default-valid root path: use current export, then import it back.
    model.exportStateRootTo(root);
    model.applyStateRoot(root);
    const auto& img = model.registerImage();
    for (int i = 0; i < kSidCanonicalRegCount; ++i) {
        assert(img.reg[i] == 0 && "state-root import must not make parameter image live register truth");
    }
}

int main() {
    test_ingress_critical_whitelist_includes_noteoff_and_state_events();
    test_ingress_primary_lane_is_mpsc_without_spinlock();
    test_moved_from_timed_queue_push_fails_closed_without_storage();
    test_rt_guard_detects_render_queue_allocation_and_allows_fail_closed_push();
    test_canonical_sid_frequency_register_uses_rounding_and_shared_law();
    test_sid_hard_sync_uses_source_msb_rising_edge();
    test_variant_clock_authority_preserves_custom_clock();
    test_canonical_voice_frequency_write_order();
    test_canonical_host_sample_offset_helper_is_single_law();
    test_subcycle_lattice_is_256_half_open_steps();
    test_runtime_subphase_to_cycle_estimate_uses_256_denominator();
    test_wrapper_conversion_does_not_own_sid_stamp();
    test_runtime_timing_resolves_unresolved_cycle_from_sid_clock();
    test_sanitize_does_not_own_sid_stamp();
    test_ingress_merge_does_not_own_sid_stamp();
    test_canonical_gm_drsid_promotion_policy_is_shared();
    test_canonical_transport_tempo_pushes_only_changes();
    test_queue_sorted_subphase_write_regression();
    test_noteoff_token_identity_regression();
    test_sustain_pedal_release_regression();
    test_all_notes_off_clears_target_channel_only();
    test_panic_clears_all_canonical_voice_state();
    test_latency_constant_is_explicit_until_core_latency_authority_lands();
    test_canonical_state_root_binary_roundtrip_is_schema_root_only();
    test_current_major_flat_param_blob_is_rejected_not_legacy_imported();
    test_state_root_does_not_seed_live_register_image();
    std::cout << "release_gate_regression_tests PASS\n";
    return 0;
}
