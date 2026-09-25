// ─── ArpSIDSequencerEngine.h ─────────────────────────────────────────────────
// Phase 3: Transport-aware step sequencer that generates canonical TimedEvents.
// Decoupled from voice/audio: emits events, never writes to engines directly.
//
// Features:
// • Transport-window advance (beatStart → beatEnd → events)
// • Ties, rests, accent, probability, ratchet
// • Deterministic seeded PRNG (tied to pattern + transport cycle + step)
// • Loop-wrap and host-stop restart handling
// • Decoupled from voice allocator — caller realizes events
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include "ArpSIDCanonicalEvents.h"
#include "arpsid/core/math_utils.h"
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace ArpSID {

// ── Step definition ───────────────────────────────────────────────────────────
struct SeqStep {
    int16_t  midiNote    = 60;     // 0..127
    float    velocity    = 0.75f;  // 0..1
    float    gate        = 1.0f;   // 0=rest, 1=full gate; fractional = gate length
    bool     tied        = false;  // tie: extend previous note, no retrigger
    bool     accent      = false;  // accent: velocity bump
    uint8_t  ratchet     = 1;      // 1=normal, 2-4=subdivisions
    float    probability = 1.0f;   // 0..1: chance this step fires
    bool     active      = true;   // false = rest regardless of gate

    void sanitize() noexcept {
        midiNote    = (int16_t)std::clamp((int)midiNote, 0, 127);
        velocity    = std::clamp(std::isfinite(velocity) ? velocity : 0.75f, 0.f, 1.f);
        gate        = std::clamp(std::isfinite(gate) ? gate : 1.f, 0.f, 1.f);
        probability = std::clamp(std::isfinite(probability) ? probability : 1.f, 0.f, 1.f);
        ratchet     = (uint8_t)std::clamp((int)ratchet, 1, 4);
    }
};

static constexpr int kMaxSeqSteps = 32;

// ── Pattern ───────────────────────────────────────────────────────────────────
struct SeqPattern {
    SeqStep steps[kMaxSeqSteps];
    int     length  = 8;    // 1..32
    uint32_t seed   = 0xDEADBEEFu;  // PRNG seed for probability evaluation

    void sanitize() noexcept {
        length = std::clamp(length, 1, kMaxSeqSteps);
        for (int i = 0; i < kMaxSeqSteps; ++i) steps[i].sanitize();
    }
};

// ── Restart policy ─────────────────────────────────────────────────────────────
enum class SeqRestartPolicy : uint8_t {
    OnTransportPlay  = 0,   // reset step on host play
    OnLoopWrap       = 1,   // reset step when host loops
    Free             = 2,   // never auto-reset
};

enum class SeqTraversalMode : uint8_t {
    Forward = 0,
    Reverse = 1,
    PingPong = 2,
    Random = 3,
};

// ── Runtime cursor (never serialised) ─────────────────────────────────────────
struct SeqCursor {
    int    step       = 0;
    int    direction  = 1;
    double beatAccum  = 0.0;   // accumulated beats within current step
    double stepBeats  = 0.0;   // how many beats current step lasts
    double gateOff    = -1.0;  // beat position to gate-off current note
    int    lastNote   = -1;
    bool   gateOpen   = false;
    uint32_t rngState = 0xDEADBEEFu;

    void reset(uint32_t seed) noexcept {
        step = 0; direction = 1; beatAccum = 0.0; stepBeats = 0.0;
        gateOff = -1.0; lastNote = -1; gateOpen = false;
        rngState = (seed == 0) ? 0x6D2B79F5u : seed;
    }
};

// ── Sequencer Engine ───────────────────────────────────────────────────────────
class SequencerEngine {
public:
    struct StepBoundary final {
        int sampleOffset = 0;
        int stepIndex = 0;
    };

    SequencerEngine() { cursor_.reset(pattern_.seed); }

    void setPattern(const SeqPattern& p) noexcept { pattern_ = p; pattern_.sanitize(); }
    void setTempo(double bpm) noexcept { bpm_ = (std::isfinite(bpm) && bpm > 1.0 && bpm < 1000.0) ? bpm : 120.0; }
    void setStepsPerBeat(float spb) noexcept { stepsPerBeat_ = (std::isfinite(spb) && spb >= 0.001f) ? spb : 4.0f; }
    void setEnabled(bool on) noexcept { enabled_ = on; }
    void setRestartPolicy(SeqRestartPolicy p) noexcept { restartPolicy_ = p; }
    void setTraversalMode(SeqTraversalMode mode) noexcept { traversalMode_ = mode; }
    void setSwing(float sw) noexcept { swing_ = std::clamp(std::isfinite(sw) ? sw : 0.0f, 0.f, 1.f); }
    void setSampleRate(double sr) noexcept { sampleRate_ = (std::isfinite(sr) && sr > 0.0) ? sr : 44100.0; }
    void resetPhase() noexcept { resetCursor_(); }
    void syncToBeatPosition(double beatPosition) noexcept {
        resetCursor_();
        cursor_.gateOpen = false;
        cursor_.gateOff = -1.0;
        cursor_.lastNote = -1;
        if (!std::isfinite(beatPosition) || beatPosition <= 0.0) {
            cursor_.stepBeats = stepBeatsForStepIndex_(cursor_.step);
            cursor_.beatAccum = 0.0;
            return;
        }
        if (traversalMode_ == SeqTraversalMode::Random) {
            cursor_.stepBeats = stepBeatsForStepIndex_(cursor_.step);
            cursor_.beatAccum = 0.0;
            return;
        }

        const int cycleLen = traversalCycleLength_();
        double cycleBeats = 0.0;
        for (int ord = 0; ord < cycleLen; ++ord)
            cycleBeats += stepBeatsForStepIndex_(stepIndexForTraversalOrdinal_(ord, nullptr));
        if (!(cycleBeats > 0.0)) {
            cursor_.stepBeats = stepBeatsForStepIndex_(cursor_.step);
            cursor_.beatAccum = 0.0;
            return;
        }

        double localBeat = std::fmod(beatPosition, cycleBeats);
        if (!std::isfinite(localBeat)) localBeat = 0.0;
        if (localBeat < 0.0) localBeat += cycleBeats;

        double accum = 0.0;
        for (int ord = 0; ord < cycleLen; ++ord) {
            int direction = 1;
            const int stepIndex = stepIndexForTraversalOrdinal_(ord, &direction);
            const double stepBeats = stepBeatsForStepIndex_(stepIndex);
            const bool terminal = (ord == cycleLen - 1);
            if (localBeat < accum + stepBeats || terminal) {
                cursor_.step = stepIndex;
                cursor_.direction = direction;
                cursor_.stepBeats = stepBeats;
                cursor_.beatAccum = std::clamp(localBeat - accum, 0.0, std::max(0.0, stepBeats - 1.0e-9));
                return;
            }
            accum += stepBeats;
        }

        cursor_.stepBeats = stepBeatsForStepIndex_(cursor_.step);
        cursor_.beatAccum = 0.0;
    }

    int currentStep() const noexcept { return cursor_.step; }

    // ── Transport event notification ─────────────────────────────────────────
    void onTransportChange(bool wasPlaying, bool isPlaying) noexcept {
        if (!wasPlaying && isPlaying && restartPolicy_ == SeqRestartPolicy::OnTransportPlay)
            resetCursor_();
    }

    void onTransportStartEdge(bool playEdge) noexcept {
        if (playEdge && restartPolicy_ == SeqRestartPolicy::OnTransportPlay)
            resetCursor_();
    }

    void onLoopWrap() noexcept {
        if (restartPolicy_ == SeqRestartPolicy::OnLoopWrap)
            resetCursor_();
    }

    // ── Advance by transport window → emit canonical TimedEvents ─────────────
    //
    // beatStart / beatEnd: host beat positions for this render block.
    // frameCount: samples in this block (for sample-offset calculation).
    // Events are stamped with sampleOffset relative to block start.
    //
    void advanceWindow(double beatStart, double beatEnd, int frameCount,
                       EventBuffer& out,
                       StepBoundary* stepBoundaries = nullptr,
                       int stepBoundaryCapacity = 0,
                       int* stepBoundaryCountOut = nullptr) noexcept {
        int stepBoundaryCount = 0;
        if (stepBoundaryCountOut) *stepBoundaryCountOut = 0;
        if (!enabled_ || frameCount <= 0) return;
        if (!std::isfinite(beatStart) || !std::isfinite(beatEnd) ||
            beatEnd <= beatStart || !std::isfinite(bpm_) || bpm_ <= 0.0) return;

        const double beatsPerFrame = (beatEnd - beatStart) / frameCount;

        double beatCursor = beatStart;

        // Emit any pending gate-off first
        if (cursor_.gateOpen && cursor_.gateOff >= beatStart && cursor_.gateOff < beatEnd) {
            const int offset = beatToFrame_(cursor_.gateOff, beatStart, beatsPerFrame, frameCount);
            emitNoteOff_(cursor_.lastNote, offset, out);
            cursor_.gateOpen = false;
        }

        while (beatCursor < beatEnd) {
            if (cursor_.stepBeats <= 0.0)
                advanceStep_();  // compute step duration

            const double stepEnd = beatCursor + (cursor_.stepBeats - cursor_.beatAccum);
            const double segEnd  = std::min(stepEnd, beatEnd);

            if (cursor_.beatAccum == 0.0) {
                // Start of new step. Publish the explicit boundary even when the
                // melodic step is a rest/tie; KIT and DIGI must consume the same
                // transport timeline rather than infer one edge from end state.
                const int offset = beatToFrame_(beatCursor, beatStart, beatsPerFrame, frameCount);
                if (stepBoundaries && stepBoundaryCount < std::max(0, stepBoundaryCapacity)) {
                    stepBoundaries[stepBoundaryCount] = StepBoundary{offset, cursor_.step};
                    ++stepBoundaryCount;
                }
                // Emit note(s)
                fireStep_(beatCursor, beatsPerFrame, beatStart, frameCount, out);
            }

            cursor_.beatAccum += segEnd - beatCursor;
            beatCursor = segEnd;

            if (cursor_.beatAccum >= cursor_.stepBeats - 1e-9) {
                // Step complete
                cursor_.beatAccum = 0.0;
                cursor_.stepBeats = 0.0;
                advanceCursor_();
            }
        }
        if (stepBoundaryCountOut) *stepBoundaryCountOut = stepBoundaryCount;
    }

private:
    SeqPattern       pattern_{};
    SeqCursor        cursor_{};
    double           bpm_          = 120.0;
    float            stepsPerBeat_ = 4.0f;   // 4 = 16th notes at quarter-note beat
    float            swing_        = 0.0f;
    double           sampleRate_   = 44100.0;
    bool             enabled_      = false;
    SeqRestartPolicy restartPolicy_= SeqRestartPolicy::OnTransportPlay;
    SeqTraversalMode traversalMode_= SeqTraversalMode::Forward;

    void resetCursor_() noexcept {
        cursor_.reset(pattern_.seed);
        if (pattern_.length <= 0) return;
        if (traversalMode_ == SeqTraversalMode::Reverse) {
            cursor_.step = std::max(0, pattern_.length - 1);
            cursor_.direction = -1;
        }
    }

    int traversalCycleLength_() const noexcept {
        const int len = std::max(1, pattern_.length);
        if (traversalMode_ == SeqTraversalMode::PingPong)
            return (len > 1) ? (len * 2 - 2) : 1;
        return len;
    }

    int stepIndexForTraversalOrdinal_(int ordinal, int* directionOut) const noexcept {
        const int len = std::max(1, pattern_.length);
        const int ord = std::max(0, ordinal);
        switch (traversalMode_) {
            case SeqTraversalMode::Reverse:
                if (directionOut) *directionOut = -1;
                return std::max(0, len - 1 - (ord % len));
            case SeqTraversalMode::PingPong: {
                const int cycleLen = (len > 1) ? (len * 2 - 2) : 1;
                const int cyclePos = (cycleLen > 0) ? (ord % cycleLen) : 0;
                if (len <= 1) {
                    if (directionOut) *directionOut = 1;
                    return 0;
                }
                if (cyclePos < len) {
                    if (directionOut) *directionOut = (cyclePos < len - 1) ? 1 : -1;
                    return cyclePos;
                }
                if (directionOut) *directionOut = -1;
                return (len - 2) - (cyclePos - len);
            }
            case SeqTraversalMode::Random:
                if (directionOut) *directionOut = 1;
                return 0;
            case SeqTraversalMode::Forward:
            default:
                if (directionOut) *directionOut = 1;
                return ord % len;
        }
    }

    void advanceCursor_() noexcept {
        const int len = std::max(1, pattern_.length);
        switch (traversalMode_) {
            case SeqTraversalMode::Reverse:
                cursor_.step = (cursor_.step - 1 + len) % len;
                break;
            case SeqTraversalMode::PingPong:
                if (len <= 1) {
                    cursor_.step = 0;
                    cursor_.direction = 1;
                    break;
                }
                cursor_.step += cursor_.direction;
                if (cursor_.step >= len) {
                    cursor_.step = len - 2;
                    cursor_.direction = -1;
                } else if (cursor_.step < 0) {
                    cursor_.step = 1;
                    cursor_.direction = 1;
                }
                cursor_.step = std::clamp(cursor_.step, 0, len - 1);
                break;
            case SeqTraversalMode::Random: {
                const uint32_t next = ArpSID_xorshift32(cursor_.rngState);
                cursor_.rngState = next == 0u ? 0x6D2B79F5u : next;
                cursor_.step = (int)(cursor_.rngState % (uint32_t)len);
                break;
            }
            case SeqTraversalMode::Forward:
            default:
                cursor_.step = (cursor_.step + 1) % len;
                break;
        }
    }

    void advanceStep_() noexcept {
        cursor_.stepBeats = stepBeatsForStepIndex_(cursor_.step);
    }

    double stepBeatsForStepIndex_(int stepIndex) const noexcept {
        const float beatsPerStep = 1.0f / stepsPerBeat_;
        const bool isOdd = (std::max(0, stepIndex) & 1) != 0;
        const float swingFactor = isOdd ? (1.0f + swing_ * 0.5f)
                                        : (1.0f - swing_ * 0.5f);
        return std::max((double)(beatsPerStep * swingFactor), 1e-6);
    }

    int beatToFrame_(double beatPos, double beatStart,
                     double beatsPerFrame, int frameCount) const noexcept {
        if (!std::isfinite(beatPos) || !std::isfinite(beatStart) ||
            !std::isfinite(beatsPerFrame) || beatsPerFrame <= 0.0 || frameCount <= 0) return 0;
        const int f = (int)std::floor((beatPos - beatStart) / beatsPerFrame);
        return std::clamp(f, 0, frameCount - 1);
    }

    void fireStep_(double beatNow, double beatsPerFrame, double beatStart,
                   int frameCount, EventBuffer& out) noexcept {
        const int stepIdx = cursor_.step % pattern_.length;
        SeqStep& s = pattern_.steps[stepIdx];

        // Probability check — deterministic per step/cycle
        if (s.probability < 1.0f) {
            const float rng = ArpSID_xorshift32(cursor_.rngState) / (float)0xFFFFFFFFu;
            if (rng > s.probability) return;  // step skipped
        }

        if (!s.active || s.gate < 0.01f) return;  // rest

        const int offset = beatToFrame_(beatNow, beatStart, beatsPerFrame, frameCount);
        const float vel  = s.accent ? std::min(1.f, s.velocity + 0.2f) : s.velocity;

        if (s.tied && cursor_.gateOpen && cursor_.lastNote == s.midiNote) {
            // Tie: do not retrigger, just extend gate
            cursor_.gateOff = beatNow + cursor_.stepBeats * (double)s.gate;
            return;
        }

        // Gate off previous note if open
        if (cursor_.gateOpen && cursor_.lastNote >= 0) {
            emitNoteOff_(cursor_.lastNote, offset, out);
            cursor_.gateOpen = false;
        }

        // Handle ratchet: subdivide step into ratchet count
        const int ratchets = std::max(1, (int)s.ratchet);
        const double ratchetBeats = cursor_.stepBeats / ratchets;
        for (int r = 0; r < ratchets; ++r) {
            const double rBeat  = beatNow + ratchetBeats * r;
            const double rGateOff = rBeat + ratchetBeats * (double)s.gate * 0.95;
            const int rOffset = beatToFrame_(rBeat, beatStart, beatsPerFrame, frameCount);
            const int rOffOff = beatToFrame_(rGateOff, beatStart, beatsPerFrame, frameCount);

            TimedEvent on{};
            on.kind        = EventKind::NoteOn;
            on.sampleOffset= std::min(rOffset, frameCount - 1);
            on.pitch       = (int16_t)s.midiNote;
            on.value       = vel;
            out.push(on);

            if (rOffOff > rOffset && rOffOff < frameCount) {
                TimedEvent off{};
                off.kind        = EventKind::NoteOff;
                off.sampleOffset= rOffOff;
                off.pitch       = (int16_t)s.midiNote;
                out.push(off);
            } else if (r == ratchets - 1) {
                // Final ratchet: schedule gate-off for next block
                cursor_.gateOff  = rGateOff;
                cursor_.gateOpen = true;
                cursor_.lastNote = s.midiNote;
            }
        }

        if (ratchets == 1) {
            cursor_.gateOff  = beatNow + cursor_.stepBeats * (double)s.gate;
            cursor_.gateOpen = true;
            cursor_.lastNote = s.midiNote;
        }
    }

    void emitNoteOff_(int note, int sampleOffset, EventBuffer& out) noexcept {
        if (note < 0) return;
        TimedEvent ev{};
        ev.kind        = EventKind::NoteOff;
        ev.sampleOffset= sampleOffset;
        ev.pitch       = (int16_t)note;
        out.push(ev);
    }
};

} // namespace ArpSID
