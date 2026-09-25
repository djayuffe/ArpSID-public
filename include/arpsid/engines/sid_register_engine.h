#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
// FIX Bug#13: Include sid_chip.h to access SIDVoice DAC curve tables.
#include "arpsid/core/sid_chip.h"
#include "arpsid/core/sid_combined_wave_model.h"
#include "arpsid/core/sid_envelope_core.h"
#include "arpsid/core/math_utils.h"
#include "arpsid/core/sid_event_queue.h"
#include "arpsid/core/sid_interval_renderable.h"
#include "arpsid/core/scope_triple_buffer.h"

namespace ArpSID {


static constexpr int kSidRegCount = 0x1E;
static constexpr int kSidRealRegCount = 0x1D;
static constexpr int kSidLiveWritableRegCount = 0x19;
static constexpr uint16_t kSidBase = 0xD400;

static constexpr bool isSidReadOnlyRegIndex(uint8_t regIndex) noexcept {
 return regIndex >= 0x19u && regIndex <= 0x1Cu;
}

static constexpr bool isSidWritableRegIndex(uint8_t regIndex) noexcept {
 return regIndex < static_cast<uint8_t>(kSidLiveWritableRegCount);
}

static constexpr bool isSidPseudoSystemRegIndex(uint8_t regIndex) noexcept {
 return regIndex == 0x1Du;
}

static constexpr uint8_t sanitizeSidRegisterValue(uint8_t regIndex, uint8_t value) noexcept {
 switch (regIndex) {
 case 0x03u: // PW1 HI (12-bit total)
 case 0x0Au: // PW2 HI
 case 0x11u: // PW3 HI
 return static_cast<uint8_t>(value & 0x0Fu);
 case 0x15u: // FC LO (11-bit cutoff low 3 bits)
 return static_cast<uint8_t>(value & 0x07u);
 default:
 return value;
 }
}

// v895 split-brain cleanup: the local sidBlendNeighborBits12 / sidSmooth12Tap /
// sidBitWeightedLadder12 copies and the legacy non-Ultra sidAnalogCombined12()
// were removed. They had NO callers — the live combined-wave law is
// sidAnalogCombined12_Ultra and the shared sidCombined* helpers in
// sid_combined_wave_model.h. Dead local copies of an audio law are drift
// incubators; any future variant belongs in the shared model header.
static inline uint16_t sidRegisterPulseComparator12(uint16_t saw12,
                                                    uint16_t pw,
                                                    SIDModel model) noexcept {
 // v895: delegate to the single canonical comparator authority
 // (sidPulseComparator12 in sid_combined_wave_model.h) shared with
 // SIDVoice::generatePulse12 and SidReadbackModel, so the $000/$FFF edge
 // cases and the 6581 comparator bias can never drift between engines.
 return sidPulseComparator12(saw12, pw, model == SIDModel::MOS6581);
}




struct SidRegFile {
 std::array<uint8_t, kSidRegCount> r{};
 void reset() {
 r.fill(0);
 r[0x18] = 0x0F;
 r[0x1D] = 0x02;
 }
};

struct SidWrite {
 uint8_t regIndex = 0;
 uint8_t value = 0;
 uint32_t sampleOffset = 0xFFFFFFFFu;
 uint16_t cycleOffset = ArpSID::kSidUnresolvedCycleOffset;
 uint32_t order = 0;
};

class SidWriteQueue {
public:
 // Audit #11: theoretical worst case at 192 kHz × 2048-sample blocks × max
 // forensic traffic is ~44 k writes/block. The previous 32 768 cap could drop
 // writes under valid hostile traffic. 65 536 covers the worst case with ~49 %
 // headroom and costs 1 MB per queue (each SidWrite is 16 bytes after natural
 // alignment). Real-world traffic stays under 1 000 writes/block, so the extra
 // memory is paid only as a worst-case safety margin.
 static constexpr size_t kMaxWrites = 65536;
 void clear() noexcept { count = 0; orderCounter = 0; droppedThisBlock = 0; droppedSinceLastClear = 0; sorted = true; }
 bool push(uint8_t regIndex, uint8_t value, uint32_t sampleOffset) noexcept {
 return push(regIndex, value, sampleOffset, 0);
 }
 bool push(uint8_t regIndex, uint8_t value, uint32_t sampleOffset, uint16_t cycleOffset) noexcept {
 // Repeated writes to the same register at the exact same intra-block time can happen
 // during dense automation, patch recall bursts, or note-on sequencing. Keep the last
 // value (last-write-wins) without consuming another slot so we reduce avoidable queue
 // pressure on the realtime thread.
 if (count > 0) {
 SidWrite& tail = writes[count - 1];
 if (tail.regIndex == regIndex &&
 tail.sampleOffset == sampleOffset &&
 tail.cycleOffset == cycleOffset) {
 tail.value = value;
 return true;
 }
 }

 // Coalesce any earlier write to the same register at the same timestamp even when
 // other register writes were interleaved in between. Host automation often arrives
 // like: regA@t, regB@t, regA@t again. Only the final value for regA@t matters.
 size_t scanBudget = 16;
 for (size_t i = count; i > 0 && scanBudget > 0; --i, --scanBudget) {
 SidWrite& prev = writes[i - 1];
 if (prev.sampleOffset != sampleOffset || prev.cycleOffset != cycleOffset) {
 break;
 }
 if (prev.regIndex == regIndex) {
 prev.value = value;
 return true;
 }
 }

 if (count < kMaxWrites) {
 const SidWrite next{regIndex, value, sampleOffset, cycleOffset, orderCounter++};
 if (count > 0) {
 const SidWrite& prev = writes[count - 1];
 const bool ordered =
 (prev.sampleOffset < next.sampleOffset) ||
 (prev.sampleOffset == next.sampleOffset && prev.cycleOffset < next.cycleOffset) ||
 (prev.sampleOffset == next.sampleOffset && prev.cycleOffset == next.cycleOffset && prev.order <= next.order);
 sorted = sorted && ordered;
 }
 writes[count++] = next;
 return true;
 }
 ++droppedThisBlock;
 ++droppedSinceLastClear;
 ++droppedTotal;
 return false;
 }
 void sortStable() noexcept {
 if (sorted || count <= 1) return;
 // Audit #10 fix: replace the O(n²) bounded insertion sort with
 // std::sort, which on libc++ (the macOS toolchain we target) is
 // implemented as introsort — in-place, allocation-free, O(n log n)
 // worst case with a guaranteed heapsort fallback. The `order` field
 // (monotonically incremented in push()) is unique per entry, so the
 // composite key (sampleOffset, cycleOffset, order) is a strict total
 // order; std::sort therefore produces the same result a stable sort
 // would.
 //
 // For a hostile dense automation burst (n=32 k) the speedup vs the
 // previous insertion sort is ~2 000× in the asymptote (n² ≈ 1 G ops
 // → n log n ≈ 480 k ops). Real-world n is in the hundreds, where the
 // `sorted` early-out continues to make this a no-op.
 struct Cmp {
     bool operator()(const SidWrite& a, const SidWrite& b) const noexcept {
         if (a.sampleOffset != b.sampleOffset) return a.sampleOffset < b.sampleOffset;
         if (a.cycleOffset  != b.cycleOffset ) return a.cycleOffset  < b.cycleOffset;
         return a.order < b.order;
     }
 };
 // ARPSID_RT_SORT_CLASSIFICATION: render-thread, fixed write array, total-order
 // comparator, libc++ introsort is allocation-free with bounded stack use.
 std::sort(writes.begin(), writes.begin() + static_cast<std::ptrdiff_t>(count), Cmp{});
 sorted = true;
 }
 struct View {
 const SidWrite* b = nullptr; const SidWrite* e = nullptr;
 const SidWrite* begin() const noexcept { return b; }
 const SidWrite* end() const noexcept { return e; }
 size_t size() const noexcept { return (size_t)(e - b); }
 const SidWrite& operator[](size_t i) const noexcept { return b[i]; }
 };
 View data() const noexcept { return {writes.data(), writes.data() + count}; }
 size_t size() const noexcept { return count; }
 // v910 telemetry semantics: the queue is long-lived across blocks (future
 // writes are rebased at block end), so the drop counters have distinct
 // lifetimes:
 //   droppedThisBlock      — reset at every canonical block boundary
 //                           (rebaseAfterBlock), after telemetry publication
 //   droppedSinceLastClear — reset only by clear()
 //   droppedTotal          — never reset
 size_t droppedCount() const noexcept { return droppedThisBlock; }
 size_t droppedSinceLastClearCount() const noexcept { return droppedSinceLastClear; }
 size_t droppedTotalCount() const noexcept { return droppedTotal; }

 template <class Pred>
 size_t eraseIf(Pred&& pred) noexcept {
 size_t out = 0;
 size_t removed = 0;
 for (size_t i = 0; i < count; ++i) {
 const SidWrite& w = writes[i];
 if (pred(w)) {
 ++removed;
 continue;
 }
 if (out != i) writes[out] = w;
 ++out;
 }
 count = out;
 return removed;
 }

 // v894 unsync fix: SidWrite.sampleOffset is BLOCK-LOCAL. Writes scheduled
 // past the current block's end (e.g. pushSynthModeWriteDelayedByCycles
 // hard-restart re-gates landing after a block-tail note-on) survive the
 // render's eraseIf but kept their old-block offsets — in the next block they
 // fired a full block late, or NEVER at typical block sizes (the local sample
 // index never reaches them), leaving a permanently dead gate/TEST-muted
 // voice and a stale entry in the queue. Call once at end of each canonical
 // block: shifts every surviving write into the next block's timeline
 // (offsets clamp to 0 so nothing can fire earlier than the block start).
 // Subtracting a constant preserves the (sampleOffset, cycleOffset, order)
 // total order except for multiple writes clamping to 0, so re-sort lazily.
 void rebaseAfterBlock(uint32_t blockFrames) noexcept {
 // Canonical block boundary: "this block" drop telemetry has been
 // published by now, so open the next block's window even when no
 // writes survive the rebase.
 droppedThisBlock = 0;
 if (blockFrames == 0u || count == 0) return;
 bool clamped = false;
 for (size_t i = 0; i < count; ++i) {
 SidWrite& w = writes[i];
 if (w.sampleOffset >= blockFrames) {
 w.sampleOffset -= blockFrames;
 } else {
 w.sampleOffset = 0u;
 clamped = true;
 }
 }
 if (clamped) sorted = false;
 }
private:
 std::array<SidWrite, kMaxWrites> writes{};
 size_t count = 0;
 uint32_t orderCounter = 0;
 size_t droppedThisBlock = 0;
 size_t droppedSinceLastClear = 0;
 size_t droppedTotal = 0;
 bool sorted = true;
};

// SidRegisterEngine: reference backend for register-level SID rendering.
// Implements ISidIntervalRenderable as the canonical interval render contract.
class SidRegisterEngine : public ISidIntervalRenderable {
public:
 void prepare(double sampleRate) { sr = (sampleRate > 1.0) ? sampleRate : 44100.0; reset(); }

 // ISidIntervalRenderable — interval renderer with subphase-timed register support.
 //
 // This engine can render a requested interval while delivering queued register
 // writes at the requested (cycle, subphase) boundaries inside that interval.
 // In the current tree, this is the concrete low-level implementation used by
 // the SID-register path, but end-to-end authority still depends on the caller
 // feeding it the timed write stream correctly.
 //
 // What this function does guarantee locally:
 // - Writes whose timestamps fall inside the interval are applied before the
 // chip advances past that boundary.
 // - Hard-sync, ring-mod, waveform, and filter state changes therefore take
 // effect at the delivered boundary inside this engine.
 // - Output accumulation is weighted by actual subphase width, not by segment
 // count.
 //
 // It does not by itself prove repo-wide timing closure; that must be shown by
 // integration and tests in the callers that populate the timed write queue.
 void renderIntervalAccurate(const SidRenderInterval& iv,
                             float& outL, float& outR) noexcept override {
     if (!iv.valid()) return;
     const uint32_t bCyc = iv.beginCycle, eCyc = iv.endCycle;
     const uint16_t bSub = iv.beginSubphase, eSub = iv.endSubphase;

     // v898 SYNTH-MODE SILENCE FIX (final piece): apply queued writes whose
     // (cycle, subphase) position falls BEFORE this interval's begin. The
     // caller's interval tiling can legally skip positions (e.g. a whole-cycle
     // window ending at cycle N followed by a subphase span starting at
     // (N, s>0) never covers (N, 0)); a write queued exactly in such a gap --
     // the transferred synth-mode GATE-ON was the audible case -- was never in
     // any dispatch window and was then silently wiped by resetIntervalCursor()
     // at the sample boundary: the voice stayed gate-off forever. A skipped
     // write's time has already passed, so applying it at interval entry is at
     // most one cycle early relative to the gap and strictly better than never.
     if (subphaseWriteCount_ > 0) {
         dispatchSubphaseWrites_(0u, 0u, bCyc, bSub);
     }

     float accumL = 0.0f, accumR = 0.0f;
     int   accumSteps = 0;

     // Phase 1: leading partial-cycle subphases.
     if (bSub != 0u) {
         const uint16_t subEnd = (bCyc == eCyc) ? eSub : kSidSubcycleBoundary;
         if (subEnd > bSub) {
             // Apply any writes landing in this subphase range first.
             dispatchSubphaseWrites_(bCyc, bSub, bCyc, subEnd);
             float l = 0.f, r = 0.f;
             renderSubCyclePhaseContribution(static_cast<uint16_t>(bCyc), bSub, subEnd, l, r);
             const int steps = static_cast<int>(subEnd - bSub);
             accumL += l * static_cast<float>(steps);
             accumR += r * static_cast<float>(steps);
             accumSteps += steps;
         }
     }

     // Phase 2: whole cycles completely covered by the requested interval.
     // Example: [0,0)→[1,0) contains exactly cycle 0; cycle 1 starts at the
     // exclusive end boundary and must not be advanced or have its writes
     // consumed yet.
     const uint32_t wholeCycleStart = (bSub > 0u) ? bCyc + 1u : bCyc;
     const uint32_t wholeCycleEnd   = eCyc;
     if (wholeCycleEnd > wholeCycleStart) {
         // The hot no-automation path must not copy/restore the fractional
         // SID scratch once per SID cycle. When there are no queued subphase
         // writes in this covered interval, render the whole window in one
         // scratch pass. If any write is pending, keep the old per-cycle path so
         // CIA/VIC/SID write timing stays exact and no under-tick crackle returns.
         if (!hasPendingSubphaseWritesInRange_(wholeCycleStart, 0u, wholeCycleEnd, 0u)) {
             float l = 0.f, r = 0.f;
             renderCycleWindowContribution(static_cast<uint16_t>(wholeCycleStart),
                                           static_cast<uint16_t>(wholeCycleEnd), l, r);
             const int steps = static_cast<int>((wholeCycleEnd - wholeCycleStart) *
                                               static_cast<uint32_t>(kSidSubcycleResolution));
             accumL += l * static_cast<float>(steps);
             accumR += r * static_cast<float>(steps);
             accumSteps += steps;
         } else {
             for (uint32_t c = wholeCycleStart; c < wholeCycleEnd; ++c) {
                 // Apply writes at cycle boundary before advancing.
                 dispatchSubphaseWrites_(c, 0u, c, kSidSubcycleBoundary);
                 float l = 0.f, r = 0.f;
                 renderCycleWindowContribution(static_cast<uint16_t>(c),
                                               static_cast<uint16_t>(c + 1u), l, r);
                 accumL += l * static_cast<float>(kSidSubcycleResolution);
                 accumR += r * static_cast<float>(kSidSubcycleResolution);
                 accumSteps += static_cast<int>(kSidSubcycleResolution);
             }
         }
     }

     // Phase 3: trailing partial-cycle subphases.
     if (eSub > 0u && eCyc >= bCyc && (eCyc != bCyc || bSub == 0u)) {
         const uint16_t subStart = (eCyc == bCyc) ? bSub : 0u;
         if (eSub > subStart) {
             dispatchSubphaseWrites_(eCyc, subStart, eCyc, eSub);
             float l = 0.f, r = 0.f;
             renderSubCyclePhaseContribution(static_cast<uint16_t>(eCyc), subStart, eSub, l, r);
             const int steps = static_cast<int>(eSub - subStart);
             accumL += l * static_cast<float>(steps);
             accumR += r * static_cast<float>(steps);
             accumSteps += steps;
         }
     }

     if (accumSteps > 0) {
         const float invSteps = 1.0f / static_cast<float>(accumSteps);
         const float post = postProcessRenderedSample_(ArpSID_sanitizeFloat(0.5f * ((accumL * invSteps) + (accumR * invSteps))));
         outL += post;
         outR += post;
     }
     intervalCursor_ = (eCyc << 8u) + eSub;
 }

 // Queue a register write with exact subphase timing for delivery during the
 // next renderIntervalAccurate call. Writes are retained until an interval
 // reaches their timestamp, then consumed. When the queue is full, overflow is
 // observable through telemetry counters: a matching register write is
 // coalesced (last-write-wins), otherwise the oldest entry is retired.
 static constexpr int kMaxSubphaseWrites = 256;
 struct SubphaseWrite {
     uint32_t cycle    = 0;
     uint8_t  subphase = 0;
     uint8_t  regIndex = 0;
     uint8_t  value    = 0;
 };

 void queueSubphaseWrite(uint32_t cycle, uint8_t subphase,
                          uint8_t regIdx, uint8_t value) noexcept {
     const auto keyLE = [](const SubphaseWrite& a, uint32_t cyc, uint8_t sub) noexcept {
         return (a.cycle < cyc) || (a.cycle == cyc && a.subphase <= sub);
     };
     const auto insertSorted = [&](int count) noexcept {
         int pos = count;
         while (pos > 0 && !keyLE(subphaseWriteQueue_[pos - 1], cycle, subphase)) {
             subphaseWriteQueue_[pos] = subphaseWriteQueue_[pos - 1];
             --pos;
         }
         subphaseWriteQueue_[pos] = { cycle, subphase, regIdx, value };
     };
     if (subphaseWriteCount_ >= kMaxSubphaseWrites) {
        ++subphaseWriteOverflowCount_;
        // Only coalesce an entry when both the target register and exact
        // timestamp match. Coalescing across different timestamps would
        // silently corrupt temporal intent under queue pressure.
        for (int j = subphaseWriteCount_ - 1; j >= 0; --j) {
            const SubphaseWrite& existing = subphaseWriteQueue_[j];
            if (existing.regIndex == regIdx && existing.cycle == cycle && existing.subphase == subphase) {
                ++subphaseWriteCoalescedCount_;
                subphaseWriteQueue_[j].value = value;
                return;
            }
        }
        const auto writePriority = [](uint8_t reg, uint8_t val) noexcept -> int {
            const uint8_t voiceReg = static_cast<uint8_t>(reg % 7u);
            if (voiceReg == 4u) {
                const bool gate = (val & 0x01u) != 0u;
                const bool test = (val & 0x08u) != 0u;
                if (!gate) return 5;   // gate-off first: protect hard-restart prep and release law
                if (test)  return 4;   // test pulses are timing critical for authentic restart behavior
                return 3;              // gate-on / control writes
            }
            if (voiceReg == 0u || voiceReg == 1u) return 2; // freq low/high pairs
            return 1; // less timing-critical cosmetic/state writes
        };
        const int incomingPriority = writePriority(regIdx, value);
        int dropIndex = -1;
        int weakestPriority = incomingPriority;
        for (int j = 0; j < subphaseWriteCount_; ++j) {
            const int p = writePriority(subphaseWriteQueue_[j].regIndex, subphaseWriteQueue_[j].value);
            if (p < weakestPriority) {
                weakestPriority = p;
                dropIndex = j;
                break;
            }
        }
        if (dropIndex < 0) {
            // v902 pressure law: for the same SID register, newer equal-priority
            // musical-control state is usually the corrective state (for example a
            // later gate/control or frequency byte). Prefer replacing the oldest
            // same-register equal/lower-priority write over dropping the incoming
            // event. Gate-off/test/gate-on already get distinct priorities above,
            // so higher-priority safety/control writes remain protected.
            for (int j = 0; j < subphaseWriteCount_; ++j) {
                const SubphaseWrite& existing = subphaseWriteQueue_[j];
                if (existing.regIndex != regIdx) continue;
                const int p = writePriority(existing.regIndex, existing.value);
                if (p <= incomingPriority) { dropIndex = j; break; }
            }
        }
        if (dropIndex < 0) {
            ++subphaseWriteDroppedOldestCount_;
            return; // retain older higher-priority/different-register musical intent; drop the new weaker write.
        }
        ++subphaseWriteDroppedOldestCount_;
        if (dropIndex < subphaseWriteCount_ - 1) {
            std::memmove(&subphaseWriteQueue_[dropIndex], &subphaseWriteQueue_[dropIndex + 1],
                         static_cast<size_t>(subphaseWriteCount_ - dropIndex - 1) * sizeof(SubphaseWrite));
        }
        insertSorted(subphaseWriteCount_ - 1);
        return;
    }
    insertSorted(subphaseWriteCount_);
    ++subphaseWriteCount_;
}

 bool hasPendingSubphaseWrite(uint32_t cycle, uint8_t subphase, uint8_t regIdx, uint8_t value) const noexcept {
     for (int i = 0; i < subphaseWriteCount_; ++i) {
         const auto& w = subphaseWriteQueue_[i];
         if (w.cycle == cycle && w.subphase == subphase && w.regIndex == regIdx && w.value == value) return true;
     }
     return false;
 }


 bool hasPendingSubphaseWritesInRange_(uint32_t beginCycle, uint8_t beginSubphase,
                                      uint32_t endCycle, uint8_t endSubphase) const noexcept {
     const uint64_t beginKey = (static_cast<uint64_t>(beginCycle) << 8u) | beginSubphase;
     const uint64_t endKey   = (static_cast<uint64_t>(endCycle) << 8u) | endSubphase;
     for (int i = 0; i < subphaseWriteCount_; ++i) {
         const auto& w = subphaseWriteQueue_[i];
         const uint64_t key = (static_cast<uint64_t>(w.cycle) << 8u) | w.subphase;
         if (key >= beginKey && key < endKey) return true;
         if (key >= endKey) break;
     }
     return false;
 }

 void clearSubphaseWrites() noexcept { subphaseWriteCount_ = 0; }
 int pendingSubphaseWriteCount() const noexcept { return subphaseWriteCount_; }
 uint32_t subphaseWriteOverflowCount() const noexcept { return subphaseWriteOverflowCount_; }
 uint32_t subphaseWriteCoalescedCount() const noexcept { return subphaseWriteCoalescedCount_; }
 uint32_t subphaseWriteDroppedOldestCount() const noexcept { return subphaseWriteDroppedOldestCount_; }
 void resetSubphaseWriteTelemetry() noexcept {
     subphaseWriteOverflowCount_ = 0u;
     subphaseWriteCoalescedCount_ = 0u;
     subphaseWriteDroppedOldestCount_ = 0u;
 }

 float renderTimedSampleAccurate(const SidWrite* writeBegin,
                                 const SidWrite* writeEnd) noexcept {
     cycleFrac += clockFrequency_ / sr;
     int cycles = static_cast<int>(std::floor(cycleFrac));
     cycleFrac -= static_cast<double>(cycles);
     cycles = std::max(0, cycles);

     clearSubphaseWrites();
     intervalCursor_ = 0u;
     fractionalCycleCursor_ = 0u;
     fractionalSubphaseCursor_ = 0u;

     for (auto it = writeBegin; it != writeEnd; ++it) {
         uint16_t boundedCycle = 0u;
         uint8_t boundedSubphase = 0u;
         resolveWritePosition_(it->cycleOffset, cycles, boundedCycle, boundedSubphase);
         queueSubphaseWrite(boundedCycle,
                            boundedSubphase,
                            it->regIndex,
                            it->value);
     }

     if (cycles <= 0) {
         dispatchSubphaseWrites_(0u, 0u, 0u, kSidSubcycleBoundary);
         clearSubphaseWrites();
         return std::clamp(ArpSID_sanitizeFloat(renderCurrentState_()), -1.0f, 1.0f);
     }

     SidRenderInterval iv{};
     iv.beginCycle = 0u;
     iv.beginSubphase = 0u;
     iv.endCycle = static_cast<uint32_t>(cycles);
     iv.endSubphase = 0u;

     float outL = 0.0f, outR = 0.0f;
     renderIntervalAccurate(iv, outL, outR);
     clearSubphaseWrites();
     return std::clamp(ArpSID_sanitizeFloat(0.5f * (outL + outR)), -1.0f, 1.0f);
 }

 uint16_t estimatedCyclesPerHostSample() const noexcept override {
     return boundedSidCyclesPerHostSampleEstimate(
         clockFrequency_, sr > 1.0 ? sr : 44100.0);
 }

 void resetIntervalCursorOnly() noexcept {
     intervalCursor_ = 0;
     subphaseWriteCount_ = 0;
 }
 void resetSubphaseWriteTelemetryForBlock() noexcept { resetSubphaseWriteTelemetry(); }
 void resetIntervalCursor() noexcept override {
     // v902: cursor reset is part of the per-sample fractional finalizer path.
     // Telemetry must not be erased here, otherwise overflow/coalesce/drop counters
     // can be cleared every host sample and hide the exact write-pressure fault that
     // caused a stuck gate, missing note-off, or filter/control glitch. Owners that
     // start a new block/audit window can call resetSubphaseWriteTelemetryForBlock().
     resetIntervalCursorOnly();
 }

 inline void renderBlock(float* left, float* right, int numSamples) {
  if (!left || !right || numSamples <= 0) return;
  const SidWrite* begin = nullptr;
  const SidWrite* end = nullptr;
 for (int i = 0; i < numSamples; ++i) {
   const float v = tick(begin, end);
   left[i] = v;
   right[i] = v;
  }
  publishScopeSnapshot_();
 }
 private:
 static inline void copyFractionalScratchState_(const SidRegisterEngine& src, SidRegisterEngine& dst) noexcept {
 dst.sr = src.sr;
  dst.clockFrequency_ = src.clockFrequency_;
  dst.cycleFrac = src.cycleFrac;
  dst.dcIn = src.dcIn;
  dst.dcOut = src.dcOut;
  dst.regs = src.regs;
  dst.voice = src.voice;
  dst.filter = src.filter;
  dst.forensic_ = src.forensic_;
  dst.busNoiseState_ = src.busNoiseState_;
  dst.envTdmHold_ = src.envTdmHold_;
  dst.d418BiasMem_ = src.d418BiasMem_;
  dst.d418PrevVolume_ = src.d418PrevVolume_;
  dst.d418VolumeDacState_ = src.d418VolumeDacState_;
  dst.d418VolumeDacEmulation_ = src.d418VolumeDacEmulation_;
  dst.lastVoiceOut_ = src.lastVoiceOut_;
  dst.lastFilterInput_ = src.lastFilterInput_;
  dst.lastFilterOutput_ = src.lastFilterOutput_;
  // ISSUE-07 FIX: Copy pot/bus/motherboard forensic state so scratch renders do not
  // inherit a zero-initialised environment and produce a contaminated first sample.
  dst.potX = src.potX;
  dst.potY = src.potY;
  dst.potPhase_ = src.potPhase_;
  dst.potXState_ = src.potXState_;
  dst.potYState_ = src.potYState_;
  dst.busLatch_ = src.busLatch_;
  dst.motherboardLp_ = src.motherboardLp_;
  dst.motherboardLp2_ = src.motherboardLp2_;
  dst.motherboardHp_ = src.motherboardHp_;
 }
 static inline void restoreFractionalScratchState_(SidRegisterEngine& dst, const SidRegisterEngine& src) noexcept {
  dst.regs = src.regs;
  dst.voice = src.voice;
  dst.filter = src.filter;
  dst.busNoiseState_ = src.busNoiseState_;
  dst.envTdmHold_ = src.envTdmHold_;
  dst.d418BiasMem_ = src.d418BiasMem_;
  dst.d418PrevVolume_ = src.d418PrevVolume_;
  dst.d418VolumeDacState_ = src.d418VolumeDacState_;
  dst.d418VolumeDacEmulation_ = src.d418VolumeDacEmulation_;
  dst.lastVoiceOut_ = src.lastVoiceOut_;
  dst.lastFilterInput_ = src.lastFilterInput_;
  dst.lastFilterOutput_ = src.lastFilterOutput_;
 }
 static inline void advanceScratchCycles_(SidRegisterEngine& scratch, int span) noexcept {
  for (int c = 0; c < span; ++c) {
   bool sourceMsbRose[3] = {};
   bool syncEnabled[3] = {};
   for (size_t i = 0; i < 3u; ++i) {
    const uint32_t previousPhase = scratch.voice[i].phase & 0xFFFFFFu;
    scratch.voice[i].stepCycle();
    sourceMsbRose[i] = sidPhaseMsbRose(previousPhase, scratch.voice[i].phase);
    syncEnabled[i] = (scratch.voice[i].ctrl & 0x02u) != 0u;
   }
   for (size_t i = 0; i < 3u; ++i) {
    if (sidHardSyncShouldReset(static_cast<int>(i), syncEnabled, sourceMsbRose)) scratch.voice[i].phase = 0;
   }
  }
 }
 // Fractional subcycle advance: distribute the full SID-cycle phase increment over the
 // engine's current subcycle lattice so the accumulated per-substep motion sums exactly
 // to one full-cycle register step. This removes the old quarter-step freq>>2 remainder
 // packing that used to quantize the scratch path onto four synthetic bins.
 static inline void advanceScratchSubphase_(SidRegisterEngine& scratch, uint16_t subphaseIdx) noexcept {
  bool sourceMsbRose[3] = {};
  bool syncEnabled[3] = {};
  for (size_t i = 0; i < 3u; ++i) {
   syncEnabled[i] = (scratch.voice[i].ctrl & 0x02u) != 0u;
   if ((scratch.voice[i].ctrl & 0x08u) != 0u) {
    scratch.voice[i].phase = 0u;
    scratch.voice[i].lfsr = 0x7FFFFFu;
    sourceMsbRose[i] = false;
    continue;
   }
   const uint32_t previousPhase = scratch.voice[i].phase & 0xFFFFFFu;
   const uint32_t freq = static_cast<uint32_t>(scratch.voice[i].freq);
   const uint32_t start = static_cast<uint32_t>((static_cast<uint64_t>(freq) * static_cast<uint32_t>(subphaseIdx)) / static_cast<uint32_t>(kSidSubcycleBoundary));
   const uint32_t end   = static_cast<uint32_t>((static_cast<uint64_t>(freq) * static_cast<uint32_t>(subphaseIdx + 1u)) / static_cast<uint32_t>(kSidSubcycleBoundary));
   const uint32_t step = end - start;
   scratch.voice[i].phase = (scratch.voice[i].phase + step) & 0xFFFFFFu;
   sourceMsbRose[i] = sidPhaseMsbRose(previousPhase, scratch.voice[i].phase);
  }
  for (size_t i = 0; i < 3u; ++i) {
   if (sidHardSyncShouldReset(static_cast<int>(i), syncEnabled, sourceMsbRose)) scratch.voice[i].phase = 0;
  }
 }
 public:
 static SidRegisterEngine& fractionalScratchInstance_() noexcept {
  static thread_local SidRegisterEngine scratch;
  return scratch;
 }
 void renderCycleWindowContribution(uint16_t cycleStart, uint16_t cycleEnd, float& outL, float& outR) noexcept {
  outL = outR = 0.0f;
  const int start = std::max(0, (int)cycleStart);
  const int end = std::max(start, (int)cycleEnd);
  const int span = end - start;
  if (span <= 0) return;
  SidRegisterEngine& scratch = fractionalScratchInstance_();
  copyFractionalScratchState_(*this, scratch);
  if (start < (int)fractionalCycleCursor_) {
   fractionalCycleCursor_ = static_cast<uint16_t>(start);
   fractionalSubphaseCursor_ = 0u;
  }
  if (start > (int)fractionalCycleCursor_) {
   advanceScratchCycles_(scratch, start - (int)fractionalCycleCursor_);
  }
  float accum = 0.0f;
  for (int c = 0; c < span; ++c) {
   advanceScratchCycles_(scratch, 1);
   accum += scratch.renderCoreStateRaw_();
  }
  restoreFractionalScratchState_(*this, scratch);
  fractionalCycleCursor_ = static_cast<uint16_t>(end);
  fractionalSubphaseCursor_ = 0u;
  const float s = ArpSID_sanitizeFloat(accum / (float)std::max(1, span));
  outL = outR = s;
 }
 void renderSubCyclePhaseContribution(uint16_t cycleIndex, uint16_t subphaseStart, uint16_t subphaseEnd, float& outL, float& outR) noexcept {
  outL = outR = 0.0f;
  uint16_t s0 = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(subphaseStart), 0, static_cast<int>(kSidSubcycleBoundary)));
  if (cycleIndex == fractionalCycleCursor_) s0 = static_cast<uint16_t>(std::max<uint16_t>(s0, fractionalSubphaseCursor_));
  const uint16_t s1 = static_cast<uint16_t>(std::clamp<int>(static_cast<int>(subphaseEnd), static_cast<int>(s0), static_cast<int>(kSidSubcycleBoundary)));
  if (s1 <= s0) return;
  SidRegisterEngine& scratch = fractionalScratchInstance_();
  copyFractionalScratchState_(*this, scratch);
  if (cycleIndex < fractionalCycleCursor_) {
   fractionalCycleCursor_ = cycleIndex;
   fractionalSubphaseCursor_ = 0u;
  }
  if (cycleIndex > fractionalCycleCursor_) {
   advanceScratchCycles_(scratch, (int)cycleIndex - (int)fractionalCycleCursor_);
  }
  if (cycleIndex == fractionalCycleCursor_ && s0 > fractionalSubphaseCursor_) {
   for (uint16_t s = fractionalSubphaseCursor_; s < s0; ++s) advanceScratchSubphase_(scratch, s);
  }
  float accum = 0.0f;
  int steps = 0;
  for (uint16_t s = s0; s < s1; ++s) {
   advanceScratchSubphase_(scratch, s);
   accum += scratch.renderCoreStateRaw_();
   ++steps;
  }
  const float s = ArpSID_sanitizeFloat((steps > 0) ? (accum / (float)steps) : 0.0f);
  outL = outR = s;
  restoreFractionalScratchState_(*this, scratch);
  if (s1 >= kSidSubcycleBoundary) {
   fractionalCycleCursor_ = static_cast<uint16_t>(cycleIndex + 1u);
   fractionalSubphaseCursor_ = 0u;
  } else {
   fractionalCycleCursor_ = cycleIndex;
   fractionalSubphaseCursor_ = s1;
  }
 }
 void finalizePlannedSample(float& outL, float& outR) noexcept {
  const float s = tick();
  outL = outR = s;
  fractionalCycleCursor_ = 0u;
  fractionalSubphaseCursor_ = 0u;
 }
 void setForensicConfig(const ArpSIDForensicConfig& cfg) noexcept {
 forensic_ = cfg;
	 filter.revision = forensic_.revision;
 const uint32_t seedBase = forensic_.chipIdSeed != 0u ? forensic_.chipIdSeed : 0xDEADBEEFu;
 for (size_t i = 0; i < voice.size(); ++i) {
  voice[i].combinedWaveSeed = ArpSID_mixSeed(seedBase, static_cast<uint32_t>(i) + 1u);
  voice[i].combinedWaveTemperature = forensic_.temperatureCelsius;
  voice[i].combinedWaveSupply = forensic_.supplyVoltage;
  voice[i].combinedWaveRevision = forensic_.revision;
 }
 }
 const ArpSIDForensicConfig& getForensicConfig() const noexcept { return forensic_; }
 double getCycleFrac() const noexcept { return cycleFrac; }
 void reset() {
 regs.reset();
 for (auto& v : voice) v.reset();
 writeSystemByte(0x02u);
 ensureSidTablesReadyForNonRealtimeUse("SidRegisterEngine::reset requires prewarmed SID tables");
 filter.reset(sr);
 cycleFrac = 0.0;
 dcIn = dcOut = 0.0f;
 d418VolumeDacState_ = 0.0f;
 dcR = static_cast<float>(std::clamp(std::exp(-2.0 * ArpSID_pi() * 16.0 / sr), 0.0, 0.99999));
 limiter_.reset(sr);
 clearScopeHistory_();
 }

 inline void writeSystemByte(uint8_t value) noexcept {
 const uint8_t sanitized = value;
 regs.r[0x1D] = sanitized;
 clockFrequency_ = (sanitized & 0x01u) != 0u ? NTSC_CLOCK_FREQ : PAL_CLOCK_FREQ;
 for (auto& v : voice) v.setSystemByte(sanitized);
 }

 inline void setClockFrequency(double hz) noexcept {
 if (sidClockFrequencySupported(hz)) clockFrequency_ = hz;
 }
 inline double clockFrequency() const noexcept { return clockFrequency_; }

 // Public read-only accessor for the SID system byte (reg 0x1D pseudo-register).
 // PSID/RSID playback needs to keep this in sync with the loaded player's
 // clock without reaching into the private regs file.
 inline uint8_t currentSystemByte() const noexcept { return regs.r[0x1D]; }

 inline void setModel(SIDModel sidModel) noexcept {
 uint8_t systemByte = regs.r[0x1D];
 if (sidModel == SIDModel::MOS8580) systemByte = static_cast<uint8_t>(systemByte | 0x02u);
 else systemByte = static_cast<uint8_t>(systemByte & static_cast<uint8_t>(~0x02u));
 writeSystemByte(systemByte);
 }

 inline SIDModel model() const noexcept {
 return (regs.r[0x1D] & 0x02u) ? SIDModel::MOS8580 : SIDModel::MOS6581;
 }

 inline SIDModel getModel() const noexcept { return model(); }
 inline bool isMos8580() const noexcept { return model() == SIDModel::MOS8580; }
 inline bool isMos6581() const noexcept { return model() == SIDModel::MOS6581; }

 // Enable/disable explicit $D418 volume-DAC leakage emulation. The normal
 // SID-register engine keeps this disabled so established synth and PSID
 // render fingerprints stay unchanged. The authentic DIGI path enables it on
 // its private render engine because real C64 DIGI is heard through the SID
 // master-volume DAC even when no SID oscillator voice is producing waveform
 // audio. Static DC is still removed by the engine's post high-pass stage;
 // rapid low-nibble changes become the audible 4-bit PCM edge train.
 inline void setD418VolumeDacEmulation(bool enabled) noexcept {
     d418VolumeDacEmulation_ = enabled;
 }

 inline bool d418VolumeDacEmulationEnabled() const noexcept {
     return d418VolumeDacEmulation_;
 }

 // Clear only the private $D418 DIGI DAC memory. This is intentionally
 // narrower than reset(): it does not touch SID registers, oscillators,
 // envelopes, filter state, model selection, or forensic configuration.
 // The AU authentic-DIGI path calls this on explicit reset/panic/policy change
 // and when the private DIGI voice is truly idle. Sparse active DIGI blocks
 // intentionally hold the last $D418 code, matching real SID volume-latch
 // behaviour instead of clearing at every host-buffer boundary.
 inline void resetD418VolumeDacEmulationState() noexcept {
     d418VolumeDacState_ = 0.0f;
 }

 inline void write(uint8_t regIndex, uint8_t value) {
 if (!isSidWritableRegIndex(regIndex)) return;
 const uint8_t sanitized = sanitizeSidRegisterValue(regIndex, value);
 regs.r[(size_t)regIndex] = sanitized;
 if (regIndex < 0x15) {
 const int vi = regIndex / 7;
 if (vi >= 0 && vi < 3) {
 const size_t viu = static_cast<size_t>(vi);
 const size_t base = static_cast<size_t>(vi * 7);
 voice[viu].refreshFromRegs(&regs.r[base]);
 voice[viu].setSystemByte(regs.r[0x1D]);
 }
 }
 }


 float renderCoreStateRaw_() noexcept {
  const uint8_t sysRender = regs.r[0x1D];
  const uint16_t fc = (uint16_t)((regs.r[0x16] << 3) | (regs.r[0x15] & 0x07));
  const uint8_t resFilt = regs.r[0x17];
  const uint8_t modeVol = regs.r[0x18];
  const uint8_t vol = (uint8_t)(modeVol & 0x0F);
  const bool is6581 = (sysRender & 0x02) == 0;
  uint16_t fcAdj = fc;
  uint8_t resAdj = (uint8_t)((resFilt >> 4) & 0x0F);
  if (forensic_.enable && forensic_.filterOhmic > 0.0f) {
   const float amt = forensic_.active(forensic_.filterOhmic);
   const float thermal = 0.65f + 0.35f * std::clamp((float)std::fabs(dcOut), 0.0f, 1.0f);
   const float fcScale = is6581 ? (1.0f - 0.10f * amt * thermal) : (1.0f - 0.05f * amt * thermal);
   fcAdj = (uint16_t)std::clamp<int>((int)std::lround((double)fc * fcScale), 0, 2047);
   resAdj = (uint8_t)std::clamp<int>((int)std::lround((double)resAdj * (1.0 - (is6581 ? 0.08 : 0.05) * amt * thermal)), 0, 15);
  }
  filter.configure(fcAdj, resAdj, modeVol);
  const float filterDrive = std::clamp(std::fabs(ArpSID_sanitizeFloat(lastFilterInput_))
                                     + 0.35f * std::fabs(ArpSID_sanitizeFloat(dcOut)), 0.0f, 1.0f);
  const float chipTempNorm = std::clamp((forensic_.temperatureCelsius - 20.0f) * (1.0f / 40.0f), 0.0f, 1.0f);
  const float chipSupplyScale = std::clamp(forensic_.supplyVoltage / 5.0f, 0.90f, 1.10f);
  const float parityThermal = std::clamp((is6581 ? 0.10f : 0.06f) * chipTempNorm + (is6581 ? 0.18f : 0.10f) * filterDrive, 0.0f, 1.0f);
  const float paritySupplyScale = std::clamp(chipSupplyScale * (1.0f - (is6581 ? 0.018f : 0.010f) * filterDrive), 0.90f, 1.10f);
  const float parityRipple = std::clamp((is6581 ? 0.45f : 0.28f) * (0.25f + 0.75f * ((float)resAdj * (1.0f / 15.0f))), 0.0f, 1.0f);
  filter.setParityEnvironment(parityThermal, paritySupplyScale, parityRipple);

    float vOut[3];
  for (size_t i = 0; i < 3u; ++i) {
   const bool ringMod = (voice[i].ctrl & 0x04) != 0u;
   const bool srcMsb = (voice[static_cast<size_t>(kSidHardSyncSourceOf[i])].phase & 0x800000u) != 0u;
   vOut[i] = voice[i].render(ringMod, srcMsb, is6581);
   if (forensic_.enable && forensic_.envelopeTDM > 0.0f) {
    const float amt = forensic_.active(forensic_.envelopeTDM);
    const float env = voice[i].env_.level;
    const float target = env * (0.75f + 0.25f * std::fabs(vOut[i]));
    const float charge = (is6581 ? 0.010f : 0.016f) + (is6581 ? 0.070f : 0.050f) * env;
    const float discharge = (is6581 ? 0.0015f : 0.0025f) + (is6581 ? 0.015f : 0.010f) * (1.0f - env);
    const float coeff = (target > envTdmHold_[(size_t)i]) ? charge : discharge;
    envTdmHold_[(size_t)i] += (target - envTdmHold_[(size_t)i]) * std::clamp(coeff, 0.0f, 1.0f);
    const float held = std::clamp(0.65f * env + 0.35f * envTdmHold_[(size_t)i], 0.0f, 1.0f);
    const float slot = (float)i - 1.0f;
    const float skew = 1.0f + ((is6581 ? 0.020f : 0.010f) * amt) * slot * (0.20f + 0.80f * held);
    vOut[i] *= skew;
   }
   lastVoiceOut_[(size_t)i] = vOut[i];
  }

  const bool route1 = (resFilt & 0x01) != 0;
  const bool route2 = (resFilt & 0x02) != 0;
  const bool route3 = (resFilt & 0x04) != 0;
  const bool voice3Off = (modeVol & 0x80u) != 0u;
  float inFilt = 0.0f, bypass = 0.0f;
  if (route1) inFilt += vOut[0]; else bypass += vOut[0];
  if (route2) inFilt += vOut[1]; else bypass += vOut[1];
  if (route3) inFilt += vOut[2]; else if (!voice3Off) bypass += vOut[2];
  lastFilterInput_ = ArpSID_sanitizeFloat(inFilt);
  const float filteredOnly = ArpSID_sanitizeFloat(filter.process(inFilt, is6581));
  lastFilterOutput_ = filteredOnly;
  float y = ArpSID_sanitizeFloat(filteredOnly + bypass);
  y *= (float)vol / 15.0f;
  if (d418VolumeDacEmulation_) {
   const float normVol = ((float)vol * (1.0f / 15.0f)) * 2.0f - 1.0f;
   const float chipScale = is6581 ? 0.034f : (forensic_.digifix8580 ? 0.018f : 0.006f);
   const float target = normVol * chipScale;
   const float slew = is6581 ? 0.42f : 0.30f;
   d418VolumeDacState_ += (target - d418VolumeDacState_) * slew;
   y += d418VolumeDacState_;
  }
  if (forensic_.enable && forensic_.d418Asymmetry > 0.0f) {
   const float a = forensic_.active(forensic_.d418Asymmetry);
   const float targetBias = ((is6581 ? 0.020f : 0.012f) * (15.0f - (float)vol) * (1.0f / 15.0f));
   d418BiasMem_ += (targetBias - d418BiasMem_) * (0.010f + 0.060f * a);
   const float sign = y >= 0.0f ? 1.0f : -1.0f;
   const float transition = std::fabs((float)vol - d418PrevVolume_) * (1.0f / 15.0f);
   y = (y + sign * d418BiasMem_ * a) * (1.0f + (0.10f + 0.08f * transition) * a * (1.0f - (float)vol / 15.0f));
   d418PrevVolume_ = (float)vol;
  } else {
   d418PrevVolume_ = (float)vol;
  }
  if (forensic_.enable && forensic_.systemNoise > 0.0f) {
   busNoiseState_ ^= (busNoiseState_ << 13); busNoiseState_ ^= (busNoiseState_ >> 17); busNoiseState_ ^= (busNoiseState_ << 5);
   const float n = ((busNoiseState_ & 0x00FFFFFFu) * (1.0f/8388608.0f)) - 1.0f;
   y += ((is6581 ? 0.0045f : 0.0020f) * forensic_.systemNoise * forensic_.intensity) * n;
  }
  if (forensic_.enable && forensic_.adcBleed > 0.0f) {
   y += (inFilt * (is6581 ? 0.010f : 0.006f)) * (forensic_.adcBleed * forensic_.intensity);
  }
  return ArpSID_sanitizeFloat(y);
 }

 float postProcessRenderedSample_(float rawSample) noexcept {
  const bool is6581 = (regs.r[0x1D] & 0x02) == 0;
  const float dcHp = rawSample - dcIn + dcR * dcOut;
  dcIn = ArpSID_sanitizeFloat(rawSample);
  dcOut = ArpSID_sanitizeFloat(dcHp);
  float post = dcOut;
  post = post / (1.0f + std::fabs(post));
  if (forensic_.enable && forensic_.motherboard > 0.0f) {
   const float amt = forensic_.active(forensic_.motherboard);
   const float shelf = (is6581 ? 0.08f : 0.04f) * amt;
   motherboardLp_ += (post - motherboardLp_) * (0.010f + 0.030f * amt);
   motherboardLp2_ += (motherboardLp_ - motherboardLp2_) * (0.008f + 0.020f * amt);
   motherboardHp_ += (post - motherboardHp_) * (0.0015f + 0.0035f * amt);
   const float motherboardHp = post - motherboardHp_;
   post = post * (1.0f - shelf) + motherboardLp2_ * shelf + motherboardHp * ((is6581 ? 0.030f : 0.018f) * amt);
  }
  post = ArpSID_sanitizeFloat(limiter_.process(post));

  // OSC3 is the top eight bits of the selected waveform-generator output,
  // not the raw accumulator. Pulse, noise, triangle/ring and combined
  // waveforms must therefore read back the same digital waveform state heard
  // by the voice path.
  uint8_t osc3 = voice[2].oscReadByte;
  uint8_t env3 = voice[2].env_.envCounter;
  if (forensic_.enable && forensic_.busCollision > 0.0f) {
   const float a = forensic_.active(forensic_.busCollision);
   const uint8_t busMix = (uint8_t)((regs.r[0x18] & 0xF0u) ^ regs.r[0x17] ^ regs.r[0x04] ^ regs.r[0x0B] ^ regs.r[0x12]);
   busLatch_ += ((float)busMix - busLatch_) * (0.08f + 0.20f * a);
   const float latched = std::clamp(busLatch_, 0.0f, 255.0f);
   osc3 = (uint8_t)std::clamp<int>((int)std::lround((1.0f - 0.35f * a) * osc3 + (0.35f * a) * latched), 0, 255);
   env3 = (uint8_t)std::clamp<int>((int)std::lround((1.0f - 0.20f * a) * env3 + (0.20f * a) * std::fmod(latched + 85.0f, 256.0f)), 0, 255);
  }
  regs.r[0x1B] = osc3;
  regs.r[0x1C] = env3;
  uint8_t px = potX, py = potY;
  if (forensic_.enable && forensic_.potInput > 0.0f) {
   const float a = forensic_.active(forensic_.potInput);
   const float ripple = (is6581 ? 9.0f : 5.0f) * a * std::sin(2.0 * ArpSID_pi() * (double)potPhase_);
   potPhase_ += (float)(0.37 / std::max(1.0, sr)); if (potPhase_ >= 1.0f) potPhase_ -= 1.0f;
   potXState_ += (((float)potX + ripple) - potXState_) * (0.05f + 0.08f * a);
   potYState_ += (((float)potY - 0.8f * ripple) - potYState_) * (0.05f + 0.08f * a);
   px = (uint8_t)std::clamp<int>((int)std::lround((double)potXState_), 0, 255);
   py = (uint8_t)std::clamp<int>((int)std::lround((double)potYState_), 0, 255);
  }
  regs.r[0x19] = px;
  regs.r[0x1A] = py;
  return post;
 }

 float renderCurrentState_() noexcept {
  return postProcessRenderedSample_(renderCoreStateRaw_());
 }

 template <typename It>
 inline float tick(It writeBegin, It writeEnd) {
 // Use the register state at the *start* of the sample to decide how many
 // SID master-clock cycles elapse during this output sample.
 cycleFrac += clockFrequency_ / sr;
 int cycles = (int)std::floor(cycleFrac);
 cycleFrac -= (double)cycles;
 cycles = std::max(0, cycles);

  It it = writeBegin;
 auto renderCurrentCoreState = [&]() noexcept -> float { return renderCoreStateRaw_(); };

 while (it != writeEnd && (int)it->cycleOffset <= 0) {
  write(it->regIndex, it->value);
  ++it;
 }
 float accum = 0.0f;
 int renderCount = 0;
 for (int c = 0; c < cycles; ++c) {
  bool sourceMsbRose[3] = {};
  bool syncEnabled[3] = {};
  while (it != writeEnd && (int)it->cycleOffset <= c) {
   write(it->regIndex, it->value);
   ++it;
  }
  for (size_t i = 0; i < 3u; ++i) {
   const uint32_t previousPhase = voice[i].phase & 0xFFFFFFu;
   voice[i].stepCycle();
   sourceMsbRose[i] = sidPhaseMsbRose(previousPhase, voice[i].phase);
   syncEnabled[i] = (voice[i].ctrl & 0x02u) != 0u;
  }
  for (size_t i = 0; i < 3u; ++i) {
   if (sidHardSyncShouldReset(static_cast<int>(i), syncEnabled, sourceMsbRose)) voice[i].phase = 0;
  }
  accum += renderCurrentCoreState();
  ++renderCount;
 }
 while (it != writeEnd) {
  write(it->regIndex, it->value);
  ++it;
 }
 if (renderCount == 0) {
  accum = renderCurrentCoreState();
  renderCount = 1;
 }
 // BUG-01 FIX: postProcessRenderedSample_ already handles busCollision, potInput,
 // osc3/env3 readback, dc-block, motherboard EQ, and lookahead limiter. Duplicating
 // that logic here caused forensic state (busLatch_, potPhase_, potXState_/Y) to
 // advance twice per tick, producing double-speed forensic evolution.
 const float y = postProcessRenderedSample_(std::clamp(accum / (float)renderCount, -1.0f, 1.0f));
 publishScopeSample_();
 return y;
 }


 inline float tick() {
 const SidWrite* nullIt = nullptr;
 return tick(nullIt, nullIt);
 }

 const SidRegFile& getRegs() const { return regs; }
 SidRegFile& getRegs() { return regs; }
 float getVoiceLastSample(int i) const noexcept { return (i >= 0 && i < 3) ? lastVoiceOut_[(size_t)i] : 0.0f; }
 float getVoiceEnvelopeLevel(int i) const noexcept {
  return (i >= 0 && i < 3) ? std::clamp(ArpSID_sanitizeFloat(voice[(size_t)i].env_.level), 0.0f, 1.0f) : 0.0f;
 }
 float getOscLastSample(int i) const noexcept { return getVoiceLastSample(i); }
 float getLastFilterInputSample() const noexcept { return lastFilterInput_; }
 float getLastFilterOutputSample() const noexcept { return lastFilterOutput_; }
 struct ScopeSnapshot {
  float osc[3][256]{};
  float filter[2][256]{};
  uint32_t writePos = 0u;
  uint8_t activeMask = 0u;
 };
 void getScopeSnapshot(float outOsc[3][256], float outFilter[2][256], uint8_t& activeMask, uint32_t& writePos) const noexcept {
  ScopeSnapshot snap{};
  scopeTriple_.peekLatest(snap);
  std::memcpy(outOsc, snap.osc, sizeof(snap.osc));
  std::memcpy(outFilter, snap.filter, sizeof(snap.filter));
  activeMask = snap.activeMask;
  writePos = snap.writePos;
 }
 uint8_t getScopeActiveMask() const noexcept {
  uint8_t m = 0;
  for (size_t i = 0; i < 3u; ++i) {
   const bool gate = (voice[i].ctrl & 0x01u) != 0u;
   if (gate || voice[i].env_.envCounter > 0u || voice[i].hardRestart >= 0) m |= static_cast<uint8_t>(1u << i);
  }
  return m;
 }
 void setPots(uint8_t x, uint8_t y) { potX = x; potY = y; }
 bool isActive() const noexcept {
 for (const auto& v : voice) {
 const bool gate = (v.ctrl & 0x01u) != 0u;
 if (gate || v.hardRestart >= 0 || v.env_.envCounter > 0u) return true;
 }
 return false;
 }

 // Audit #38 diagnostic accessor (v549) — delegates to inner Filter struct.
 uint64_t filterAbsClampHitCount() const noexcept { return filter.filterAbsClampHitCount(); }

private:
 struct Voice {
 uint32_t phase = 0;
 uint32_t lfsr = 0x7FFFFFu;
 // FIX Bug#23: Removed prevNoiseClock (was initialised in reset() but never read
 // or written after that — the noise clock is tracked inline in stepCycle() via
 // local prevBit/nextBit values).
 uint16_t freq = 0;
 uint16_t pw = 0;
 uint8_t ctrl = 0;
 uint8_t ad = 0;
 uint8_t srReg = 0;
 Sid6581Envelope env_{};   // isolated ADSR core — authoritative envelope state
 int hardRestart = -1;
 // FIX BUG-0014: Removed dangling unnamed `bool` (remnant of deleted prevNoiseClock).
 uint8_t sysByte = 0x02u; // $D41D bit1=1 => MOS8580, bit1=0 => MOS6581
 uint16_t lastCombinedWave = 0;
 uint8_t oscReadByte = 0;
 uint32_t combinedWaveSeed = 0xA341316Cu;
 float combinedWaveTemperature = 35.0f;
 float combinedWaveSupply = 5.0f;
	 uint8_t combinedWaveRevision = 5u;

 static inline void clockNoiseLfsr(uint32_t& lfsr) noexcept {
 const uint32_t fb = ((lfsr >> 22u) ^ (lfsr >> 17u)) & 1u;
 lfsr = ((lfsr << 1u) | fb) & 0x7FFFFFu;
 if (lfsr == 0u) lfsr = 0x7FFFFFu;
 }

 void reset() {
 phase = 0; lfsr = 0x7FFFFFu;
 freq = pw = 0; ctrl = ad = srReg = 0;
 env_.reset(); hardRestart = -1; sysByte = 0x02u; lastCombinedWave = 0; oscReadByte = 0;
 }

 void refreshFromRegs(const uint8_t* r) {
 const uint16_t oldFreq = freq;
 const uint16_t oldPw = pw;
 const uint8_t oldCtrl = ctrl;
 const uint16_t newFreq = (uint16_t)r[0] | ((uint16_t)r[1] << 8);
 const uint16_t newPw = (uint16_t)r[2] | (((uint16_t)r[3] & 0x0F) << 8);
 const uint8_t newCtrl = r[4];
 const uint8_t newAd = r[5];
 const uint8_t newSr = r[6];
 const bool prevGate = (oldCtrl & 0x01) != 0;
 const bool newGate = (newCtrl & 0x01) != 0;
 const bool prevTest = (oldCtrl & 0x08) != 0;
 const bool newTest = (newCtrl & 0x08) != 0;
 freq = newFreq; pw = static_cast<uint16_t>(newPw & 0x0FFFu); ad = newAd; srReg = newSr; ctrl = newCtrl;
 env_.setADFromByte(newAd); env_.setSRFromByte(newSr);
 if (newTest && !prevTest) {
 phase = 0;
 
 lfsr = 0x7FFFFFu;
 } else if (!newTest && prevTest) {
 phase = 0;
 
 if (lfsr == 0u) lfsr = 0x7FFFFFu;
 }
 if (newTest && ((newFreq != oldFreq) || ((newPw & 0x0FFFu) != oldPw) || ((newCtrl ^ oldCtrl) & 0xF0u))) {
 phase = 0;
 }
 if (((oldCtrl ^ newCtrl) & 0x80u) != 0u && !newTest) {
 // Keep noise/register transitions free of synthetic LFSR perturbation.
 // The authentic path should only evolve the noise state through the
 // oscillator clocking model, not through ad hoc XORs on register writes.
 }
 if (newGate && !prevGate) {
 // $D404 gate edges authoritatively drive the envelope FSM.
 env_.is6581 = !(sysByte & 0x02u);
 env_.gateOn();
 } else if (!newGate && prevGate) {
 env_.is6581 = !(sysByte & 0x02u);
 env_.gateOff();
 }
 }

 // ADSR rate periods, exponential divider, and tick logic live in Sid6581Envelope (sid_envelope_core.h).

 void setSystemByte(uint8_t sys) {
     sysByte = sys;
     env_.is6581 = !(sys & 0x02u);
     env_.onSustainChanged();
 }

	 bool stepCycle() {
 // ISSUE-12 FIX: Removed dead `gate` variable (was suppressed with (void)gate at
 // the end of the function and never used in any control-flow branch).
 const bool test = (ctrl & 0x08) != 0;
 if (sidHardRestartTickCountdown(hardRestart)) {
     ctrl |= 0x01;
     // BUG-04 FIX: MOS8580 resets rateCounter and expoCounter on gate-rising;
     // MOS6581 preserves them (authentic ADSR delay bug). env_.gateOn() handles both.
     env_.is6581 = !(sysByte & 0x02u);
     env_.gateOn();
 }
 bool wrapped = false;
	 if (!test) {
 const uint32_t prev = phase & 0xFFFFFFu;
 phase = (phase + (uint32_t)freq) & 0xFFFFFFu;
 wrapped = phase < prev;
 uint32_t rises = 0u;
 if (!wrapped) {
     rises = (phase >> 20u) - (prev >> 20u);
     const bool prevBit = ((prev >> 19u) & 1u) != 0u;
     const bool nextBit = ((phase >> 19u) & 1u) != 0u;
     if (!prevBit && nextBit) ++rises;
 } else {
     const uint32_t totalSpan = (0x1000000u - prev) + phase;
     rises = totalSpan >> 20u;
     const bool prevBit = ((prev >> 19u) & 1u) != 0u;
     const bool nextBit = ((phase >> 19u) & 1u) != 0u;
     if (!prevBit && nextBit && rises == 0u) ++rises;
 }
	 rises = std::min<uint32_t>(rises, 32u);
	 for (uint32_t i = 0u; i < rises; ++i) clockNoiseLfsr(lfsr);
	 } else {
	     phase = 0;
	     lfsr = 0x7FFFFFu;
	 }

 env_.tick();
 return wrapped;
 }

 uint16_t noise12() const {
 // FIX BUG-0001: Correct hardware LFSR output taps (bits 22,20,16,13,11,7,4,2).
 // Previous taps (20,18,14,11,9,5,2,0) did not match real SID die analysis / reSID.
 // Now identical to SIDVoice::generateNoise12() — both engines produce the same
 // noise output from the same LFSR state. Upper 8 bits mapped to bits 4-11;
 // bits 0-3 are hardware-authentic dead low nibble.
 uint16_t out = 0;
 out |= (uint16_t)(((lfsr >> 22u) & 1u) << 11u);
 out |= (uint16_t)(((lfsr >> 20u) & 1u) << 10u);
 out |= (uint16_t)(((lfsr >> 16u) & 1u) << 9u);
 out |= (uint16_t)(((lfsr >> 13u) & 1u) << 8u);
 out |= (uint16_t)(((lfsr >> 11u) & 1u) << 7u);
 out |= (uint16_t)(((lfsr >>  7u) & 1u) << 6u);
 out |= (uint16_t)(((lfsr >>  4u) & 1u) << 5u);
 out |= (uint16_t)(((lfsr >>  2u) & 1u) << 4u);
 return out & 0x0FFFu;
 }

	 float render(bool ringMod = false, bool sourceMsb = false, bool is6581 = false) {
	 const uint8_t wfIndex = static_cast<uint8_t>((ctrl >> 4u) & 0x0Fu);
	 const uint8_t wf = sidResolveWaveformControlMask(wfIndex);
	 const uint16_t saw12 = (uint16_t)((phase >> 12u) & 0x0FFFu);
	 const uint16_t triBase = (uint16_t)((phase >> 11u) & 0x0FFFu);
	 const bool triMsb = ((phase & 0x800000u) != 0u) ^ (ringMod && sourceMsb);
	 const uint16_t tri12 = triMsb ? (uint16_t)(triBase ^ 0x0FFFu) : triBase;
	 uint16_t s = 0;
	 // TEST: accumulator/LFSR held in reset — tri/saw/noise output 0, but the
	 // pulse comparator is forced HIGH while TEST is set (reSID law, mirrored
	 // from SIDVoice::renderFromPhase and SidReadbackModel). Pulse-only renders
	 // full-scale — the basis of the test-bit digi technique.
	 if (ctrl & 0x08) s = ((wf & 0xF0u) == 0x40u) ? 0x0FFFu : 0u;
	 else if (wf == 0x80u) s = noise12();
	 else {
	 const bool tri = (wf & 0x10u) != 0u;
	 const bool saw = (wf & 0x20u) != 0u;
	 const bool pul = (wf & 0x40u) != 0u;
	 const bool noi = (wf & 0x80u) != 0u;
 const uint16_t noise = noi ? noise12() : 0u;
 const uint16_t pulseV = [&]() -> uint16_t {
     return sidRegisterPulseComparator12(saw12, pw, is6581 ? SIDModel::MOS6581 : SIDModel::MOS8580);
 }();

 const uint16_t analogCombined = sidAnalogCombined12_Ultra(
     tri12, saw12, pulseV, noise,
     tri, saw, pul, noi,
     is6581,
     lastCombinedWave,
     combinedWaveTemperature,
     combinedWaveSupply,
     combinedWaveRevision,
     combinedWaveSeed);
 // Keep the register-engine combined-wave authority on the same analog-combined
 // path as the core SIDVoice implementation. The older tri+saw shortcut and the
 // extra DAC post-blend made the two engines disagree on identical register
 // images even when the same chip model was selected.
 s = static_cast<uint16_t>(analogCombined & 0x0FFFu);
 lastCombinedWave = s;
 }
 oscReadByte = static_cast<uint8_t>((s >> 4u) & 0xFFu);
 // ISSUE-001 FIX: Apply DAC LUT unconditionally for all waveform outputs.
 // Real hardware routes every oscillator output (including s==0) through the
 // R-2R DAC ladder. The previous `if (s != 0u)` guard was a latent divergence
 // from the SIDVoice path: DAC[0]==0 so functional output is unchanged, but
 // removing the guard ensures future DAC table changes (e.g. measured
 // calibration with a non-zero floor) are applied consistently.
 s = is6581 ? SIDVoice::s_dac_6581[s & 0x0FFFu]
            : SIDVoice::s_dac_8580[s & 0x0FFFu];
 const float osc = (float)s * (2.0f / 4095.0f) - 1.0f;
 return osc * env_.level;
 }
 };

 struct Filter {
 float z1 = 0.0f, z2 = 0.0f;
 float lpState = 0.0f, bpState = 0.0f, hpState = 0.0f;
 uint16_t fc = 0;
 uint8_t res = 0;
 uint8_t modeVol = 0x10;
 double sr = 44100.0;
 double cutoffHz = 20.0;
	 double smoothedCutoffHz = 20.0;
	 double q = 0.74;
	 double smoothedQ = 0.74;
	 double integratorLeak = 0.0;
	 uint8_t revision = 5u;
	 double smoothCoeff = 0.0;
 float thermalDrift = 0.0f;
 float supplyScale = 1.0f;
 float supplyRippleAmount = 0.0f;
 double ripplePhase = 0.0;
	 // Audit (v822): the parity law is a PURE function of (model, fc, res,
	 // thermalDrift, supplyScale, revision). process() runs once per render sample
	 // and previously rebuilt + introsort-sanitized a fresh SidAnalogueCalibration
	 // EVERY sample (sidComputeFilterParityLaw -> sidDefaultAnalogueCalibration ->
	 // SidAnalogueCalibration::sanitize), which dominated audio-thread CPU. Cache the
	 // last inputs and recompute only when one actually changes; cutoffHz/q/
	 // integratorLeak retain the law for the cached inputs, so output stays
	 // bit-identical to the uncached path.
	 bool lawCacheValid_ = false;
	 bool lawCacheIs6581_ = false;
	 uint16_t lawCacheFc_ = 0xFFFFu;
	 uint8_t lawCacheRes_ = 0xFFu;
	 uint8_t lawCacheRevision_ = 0xFFu;
	 float lawCacheThermal_ = -1.0f;
	 float lawCacheSupply_ = -1.0f;
	 void updateLaw(bool is6581) {
	 if (lawCacheValid_ && lawCacheIs6581_ == is6581 && lawCacheFc_ == fc &&
	     lawCacheRes_ == res && lawCacheRevision_ == revision &&
	     lawCacheThermal_ == thermalDrift && lawCacheSupply_ == supplyScale) {
	  return; // cutoffHz/q/integratorLeak already hold the law for these inputs
	 }
	 const SidFilterParityLaw law = sidComputeFilterParityLaw(is6581 ? SIDModel::MOS6581 : SIDModel::MOS8580,
	                                                          fc, res, thermalDrift, supplyScale, revision);
	 cutoffHz = law.cutoffHz;
	 q = law.q;
	 integratorLeak = law.integratorLeak;
	 lawCacheValid_ = true;
	 lawCacheIs6581_ = is6581;
	 lawCacheFc_ = fc;
	 lawCacheRes_ = res;
	 lawCacheRevision_ = revision;
	 lawCacheThermal_ = thermalDrift;
	 lawCacheSupply_ = supplyScale;
	 }
	 void reset(double sampleRate) {
	  z1 = z2 = 0.0f; lpState = bpState = hpState = 0.0f; sr = std::max(1.0, sampleRate); fc = 0; res = 0; modeVol = 0x10;
	  cutoffHz = 20.0; smoothedCutoffHz = 20.0; q = 0.74; smoothedQ = 0.74; integratorLeak = 0.0; revision = 5u;
	  thermalDrift = 0.0f; supplyScale = 1.0f; supplyRippleAmount = 0.0f; ripplePhase = 0.0;
	  smoothCoeff = 1.0 - std::exp(-1.0 / std::max(1.0, sr * 0.0015));
	  lawCacheValid_ = false; // force law recompute on first post-reset sample
	 }
 void configure(uint16_t cutoff, uint8_t resonance, uint8_t mv) { fc = cutoff; res = resonance; modeVol = mv; }
 void setParityEnvironment(float drift, float scale, float ripple) noexcept {
  thermalDrift = std::clamp(std::isfinite(drift) ? drift : 0.0f, 0.0f, 1.0f);
  supplyScale = std::clamp(std::isfinite(scale) ? scale : 1.0f, 0.85f, 1.15f);
  supplyRippleAmount = std::clamp(std::isfinite(ripple) ? ripple : 0.0f, 0.0f, 1.0f);
 }
 float process(float input, bool is6581) {
 updateLaw(is6581);
 const double ripple = (double)sidComputeFilterRipple(is6581 ? SIDModel::MOS6581 : SIDModel::MOS8580,
                                                     ripplePhase, sr, supplyRippleAmount);
 const double targetCutoff = std::clamp(cutoffHz * (1.0 + ripple), 8.0, std::min(48000.0, 0.45 * sr));
 const double targetQ = std::clamp(q * (1.0 + ripple * (is6581 ? 0.20 : 0.12)), 0.20, 8.0);
 smoothedCutoffHz += (targetCutoff - smoothedCutoffHz) * smoothCoeff;
 smoothedQ += (targetQ - smoothedQ) * smoothCoeff;
 smoothedCutoffHz = std::clamp(smoothedCutoffHz, 8.0, std::min(48000.0, 0.45 * sr));
 smoothedQ = std::clamp(smoothedQ, 0.20, 8.0);
 const float resNorm = (float)res * (1.0f / 15.0f);
 const float driveNorm = std::clamp(std::fabs(input), 0.0f, 2.0f) * 0.5f;
 // Audit #32: apply the 6581 op-amp loading cutoff-squash that SidRegisterEngine
 // was previously MISSING — same shared helper SIDChip's filter uses, so identical
 // register state now renders identically across both engines.
 const double effectiveCutoffHz =
     FilterCore::effectiveCutoffHz_6581_loading(smoothedCutoffHz, driveNorm, resNorm, is6581);
 const double g = std::tan(ArpSID_pi() * effectiveCutoffHz / sr);
 const double k = 1.0 / std::max(0.20, smoothedQ);
 float x = ArpSID_sanitizeFloat(input);
 // Audit #31: shared resonance-feedback law.
 const float feedback = FilterCore::resonanceFeedbackCoefficient(resNorm, is6581);
 x -= bpState * feedback;
 // Audit #32: shared 6581 op-amp loading + tanh saturation (the
 // missing-from-this-engine nonlinearity).
 x = FilterCore::applyModelNonlinearity(x, driveNorm, resNorm, is6581);
 const double hp = ((double)x - (k + g) * (double)z1 - (double)z2) / (1.0 + g * (g + k));
 const double bp = g * hp + (double)z1;
 const double lp = g * bp + (double)z2;
 // Audit #31: shared integrator-leak law.
 const double leak1 = FilterCore::integratorLeak1(integratorLeak);
 const double leak2 = FilterCore::integratorLeak2(integratorLeak, is6581);
 // BUG-03 FIX: Trapezoidal SVF integrator update must be z1=bp*leak1, z2=lp*leak2.
 // bp = z1_prev + g*hp and lp = z2_prev + g*bp already incorporate the prior
 // state. The old form (bp + g*hp) and (lp + g*bp) effectively doubled the
 // g contribution, shifting the cutoff frequency upward and mismatching SIDFilter.
 z1 = ArpSID_sanitizeFloat((float)(bp * leak1));
 z2 = ArpSID_sanitizeFloat((float)(lp * leak2));
 lpState = ArpSID_sanitizeFloat((float)lp);
 bpState = ArpSID_sanitizeFloat((float)bp);
 hpState = ArpSID_sanitizeFloat((float)hp);
 float out = 0.0f;
 if (modeVol & 0x10u) out += lpState;
 if (modeVol & 0x20u) out += bpState;
 if (modeVol & 0x40u) out += hpState;
 // Audit #38: ±8 absolute safety net via shared `FilterCore::absoluteSafetyClamp`
 // — bit-identical to SIDChip's clamp.
 const float sanitized = ArpSID_sanitizeFloat(out);
 const float absClamp = FilterCore::absoluteSafetyClamp(sanitized);
 if (sanitized != absClamp) ++filterAbsClampHitCount_;
 return absClamp;
 }
 mutable uint64_t filterAbsClampHitCount_ = 0u; // audit #38/#31 diagnostic
 uint64_t filterAbsClampHitCount() const noexcept { return filterAbsClampHitCount_; }
 };

 struct LookaheadLimiter {
 std::array<float, 1024> buf{};
 std::array<float, 1024> peakVals{};
 // FIX Bug#9 (register engine): keep limiter indices 64-bit to avoid wraparound in long sessions.
 // Use 1024-sample buffers so 5 ms lookahead remains intact up to 192 kHz.
 std::array<uint64_t, 1024> peakIdx{};
 int pos = 0, delay = 240;
 uint64_t dequeHead = 0uLL, dequeTail = 0uLL;
 uint64_t sampleCounter = 0uLL;
 float env = 1.0f, releaseCoeff = 0.9995f;
 double sampleRate = 44100.0;
 void reset(double newSampleRate) {
 sampleRate = std::max(1.0, newSampleRate);
 buf.fill(0.0f);
 peakVals.fill(0.0f);
 peakIdx.fill(0uLL);
 pos = 0;
 delay = std::clamp((int)std::lround(sampleRate * 0.005), 1, (int)buf.size() - 1);
 dequeHead = dequeTail = 0uLL;
 sampleCounter = 0uLL;
 env = 1.0f;
 releaseCoeff = std::exp(-1.0f / (0.050f * (float)sampleRate));
 }
 float process(float x) {
 buf[(size_t)pos] = x;
 const float ax = std::fabs(x);
 while (dequeTail > dequeHead && peakVals[(size_t)((dequeTail - 1uLL) % (uint64_t)peakVals.size())] <= ax) --dequeTail;
 peakVals[(size_t)(dequeTail % (uint64_t)peakVals.size())] = ax;
 peakIdx[(size_t)(dequeTail % (uint64_t)peakIdx.size())] = sampleCounter;
 ++dequeTail;
 const uint64_t oldest = sampleCounter - (uint64_t)delay + 1uLL;
 while (dequeTail > dequeHead && peakIdx[(size_t)(dequeHead % (uint64_t)peakIdx.size())] < oldest) ++dequeHead;
 const float peak = (dequeTail > dequeHead) ? peakVals[(size_t)(dequeHead % (uint64_t)peakVals.size())] : 0.0f;
 const float th = 0.9885531f;
 const float target = (peak > th) ? (th / peak) : 1.0f;
 if (target < env) env = target; else env = 1.0f - (1.0f - env) * releaseCoeff;
 const int readPos = (pos - delay + (int)buf.size()) % (int)buf.size();
 // Do not emit an artificial block of silence while the lookahead delay line is filling.
 // The top-level render path already has its own output protection, and synth SID-register
 // note-ons must be audible from the first sample instead of disappearing behind a 5 ms warmup.
 const float delayed = (sampleCounter < (uint64_t)delay) ? x : buf[(size_t)readPos];
 const float y = delayed * env;
 pos = (pos + 1) % (int)buf.size();
 ++sampleCounter;
 return y;
 }
 };

 double sr = 44100.0;
 double clockFrequency_ = PAL_CLOCK_FREQ;
 double cycleFrac = 0.0;
 uint16_t fractionalCycleCursor_ = 0u;
 uint16_t fractionalSubphaseCursor_ = 0u;
 float dcR = static_cast<float>(std::clamp(std::exp(-2.0 * ArpSID_pi() * 16.0 / 44100.0), 0.0, 0.99999));
 float dcIn = 0.0f, dcOut = 0.0f;
 uint8_t potX = 0, potY = 0;
 ArpSIDForensicConfig forensic_{};
 uint32_t busNoiseState_ = 0x13579BDFu;
 float potPhase_ = 0.0f;
 std::array<float, 3> envTdmHold_{};
 float d418BiasMem_ = 0.0f;
 float d418PrevVolume_ = 15.0f;
 bool  d418VolumeDacEmulation_ = false;
 float d418VolumeDacState_ = 0.0f;
 float motherboardLp_ = 0.0f;
 float motherboardLp2_ = 0.0f;
 float motherboardHp_ = 0.0f;
 float busLatch_ = 0.0f;
 float potXState_ = 0.0f;
 float potYState_ = 0.0f;
 SidRegFile regs;
 std::array<Voice, 3> voice{};
 std::array<float, 3> lastVoiceOut_{};
 float lastFilterInput_ = 0.0f;
 float lastFilterOutput_ = 0.0f;
 std::array<std::array<float, 256>, 3> scopeOsc_{};
 std::array<std::array<float, 256>, 2> scopeFilter_{};
 uint32_t scopeWritePos_ = 0;
 uint8_t scopeActiveMask_ = 0;
 mutable ScopeTripleBuffer<ScopeSnapshot> scopeTriple_{};
 Filter filter;
 LookaheadLimiter limiter_{};
 uint32_t intervalCursor_ = 0;  // sub-phase cursor for renderIntervalAccurate

 void clearScopeHistory_() noexcept {
  for (auto& s : scopeOsc_) s.fill(0.0f);
  for (auto& s : scopeFilter_) s.fill(0.0f);
  lastVoiceOut_.fill(0.0f);
  lastFilterInput_ = 0.0f;
  lastFilterOutput_ = 0.0f;
  scopeWritePos_ = 0u;
  scopeActiveMask_ = 0u;
  scopeTriple_.clearWriteSlot();
  scopeTriple_.publish();
 }

 void publishScopeSample_() noexcept {
  const uint32_t wp = scopeWritePos_ & 255u;
  for (int v = 0; v < 3; ++v) scopeOsc_[(size_t)v][wp] = std::clamp(ArpSID_sanitizeFloat(lastVoiceOut_[(size_t)v]), -1.0f, 1.0f);
  scopeFilter_[0][wp] = std::clamp(ArpSID_sanitizeFloat(lastFilterInput_), -1.0f, 1.0f);
  scopeFilter_[1][wp] = std::clamp(ArpSID_sanitizeFloat(lastFilterOutput_), -1.0f, 1.0f);
  scopeActiveMask_ = getScopeActiveMask();
  scopeWritePos_ = (wp + 1u) & 255u;
  if ((scopeWritePos_ & 31u) == 0u) publishScopeSnapshot_();
 }

 void publishScopeSnapshot_() noexcept {
  auto& snap = scopeTriple_.writeSlot();
  std::memcpy(snap.osc, scopeOsc_.data(), sizeof(snap.osc));
  std::memcpy(snap.filter, scopeFilter_.data(), sizeof(snap.filter));
  snap.activeMask = scopeActiveMask_;
  snap.writePos = scopeWritePos_;
  scopeTriple_.publish();
 }

 static uint8_t cycleOffsetToSubphase_(uint16_t cycleOffset,
                                       uint16_t cyclesPerHostSample) noexcept {
     if (cyclesPerHostSample == 0u) return 0u;
     if (cycleOffset == ArpSID::kSidUnresolvedCycleOffset) return 0u;
     const uint32_t boundedCycle = static_cast<uint32_t>(std::min<uint16_t>(cycleOffset, static_cast<uint16_t>(std::max<int>(0, static_cast<int>(cyclesPerHostSample) - 1))));
     const uint32_t numerator = boundedCycle << 8u;
     const uint32_t denom = static_cast<uint32_t>(std::max<uint16_t>(cyclesPerHostSample, 1u));
     return static_cast<uint8_t>(std::min<uint32_t>(kSidSubcycleLast, numerator / denom));
 }

 static inline void resolveWritePosition_(uint16_t cycleOffset,
                                          int cyclesThisSample,
                                          uint16_t& outCycle,
                                          uint8_t& outSubphase) noexcept {
     if (cyclesThisSample <= 0) {
         outCycle = 0u;
         outSubphase = 0u;
         return;
     }
     if (cycleOffset == ArpSID::kSidUnresolvedCycleOffset) {
         outCycle = 0u;
         outSubphase = 0u;
         return;
     }
     const uint16_t maxCycle = static_cast<uint16_t>(std::max(0, cyclesThisSample - 1));
     outCycle = std::min<uint16_t>(cycleOffset, maxCycle);
     outSubphase = cycleOffsetToSubphase_(outCycle, static_cast<uint16_t>(cyclesThisSample));
 }

 // Subphase-native write queue for renderIntervalAccurate.
 SubphaseWrite subphaseWriteQueue_[kMaxSubphaseWrites]{};
 int           subphaseWriteCount_ = 0;
 uint32_t      subphaseWriteOverflowCount_ = 0;
 uint32_t      subphaseWriteCoalescedCount_ = 0;
 uint32_t      subphaseWriteDroppedOldestCount_ = 0;

 // Apply all queued writes whose (cycle, subphase) position falls within
 // [beginCycle+beginSub, endCycle+endSub). Consumed writes are removed.
 void dispatchSubphaseWrites_(uint32_t beginCycle, uint16_t beginSub,
                               uint32_t endCycle,   uint16_t endSub) noexcept {
     if (subphaseWriteCount_ == 0) return;
     const uint64_t bPos = (static_cast<uint64_t>(beginCycle) << 8u) + beginSub;
     const uint64_t ePos = (static_cast<uint64_t>(endCycle) << 8u) + endSub;
     int remaining = 0;
     for (int i = 0; i < subphaseWriteCount_; ++i) {
         const auto& w = subphaseWriteQueue_[i];
         const uint64_t wPos = (static_cast<uint64_t>(w.cycle) << 8u) + w.subphase;
         if (wPos >= bPos && wPos < ePos) {
             // Apply write now — exact timing within interval.
             write(w.regIndex, w.value);
         } else {
             // Keep for a future interval.
             subphaseWriteQueue_[remaining++] = w;
         }
     }
     subphaseWriteCount_ = remaining;
 }
};

} // namespace ArpSID
