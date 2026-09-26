// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "../core/math_utils.h"

// FIX Bug#1: Removed header-local ArpSID_xorshift32 definition.
// Use ArpSID::ArpSID_xorshift32 from math_utils.h throughout this file.

namespace ArpSID {

/**
 * Arpeggiator Modes
 */
enum class ArpMode {
    Up,
    Down,
    UpDown,
    DownUp,
    Random,
    Pattern,
    Chord
};

/**
 * Arpeggiator - Pattern-based note sequencing
 * Supports multiple modes, swing, octave spanning
 */
class Arpeggiator {
public:
    static constexpr int MAX_PATTERN_STEPS = 32;
    static constexpr int kMaxTimedEventsPerProcess = 512;
    
    Arpeggiator() {
        // Initialize default pattern (up)
        for (int i = 0; i < MAX_PATTERN_STEPS; ++i) {
            pattern[i] = i % 8;
        }
        reset();
    }
    
    // Set a per-instance discriminant so RNG differs across plugin instances.
    // Call once after construction with e.g. a pointer-derived value.
    void setInstanceSeed(uint32_t discriminant) {
        instanceSeed_ = ArpSID::ArpSID_mixSeed(0xC0FFEE12u, discriminant);
        rngState = instanceSeed_;
    }

    // Seed the PRNG from the host transport position so that starting playback
    // at the same beat always produces the same random arpeggio sequence.
    // This makes DAW bounce deterministic and avoids variation between takes.
    // Call from the render thread when transport starts or repositions.
    void seedFromHostPosition(double hostBeatPosition) noexcept {
        // Quantize to semiquavers (1/16 beat resolution) for stable seeding.
        const auto beatQ = static_cast<uint32_t>(std::max(0.0, hostBeatPosition) * 16.0);
        // Mix instance seed + beat position → deterministic per-position sequence.
        const uint32_t base = (instanceSeed_ != 0u) ? instanceSeed_ : 0xC0FFEE12u;
        rngState = ArpSID::ArpSID_mixSeed(base, beatQ);
    }

    void reset() {
        noteCount = 0;
        physicalHoldCount.fill(0u);
        physicalVelocity.fill(0.0f);
        currentStep = 0;
        sampleCounter = 0;
        firstStepPending = false;
        pendingEventValid_ = false;
        pendingGateOff_ = false;
        flushGateOffAtBlockStart_ = false;
        lastNoteEmitted_ = -1;
        lastStepEmitted_ = 0;
        lastChordNoteCount_ = 0;
        enabled = false;
        // Re-seed with the instance-specific seed so each reset gives a fresh
        // but reproducible-per-instance sequence (deterministic within one session).
        rngState = (instanceSeed_ != 0u) ? instanceSeed_ : 0xC0FFEE12u;
    }
    
    void setSampleRate(double sr) {
        sampleRate = (std::isfinite(sr) && sr >= 1.0) ? sr : 44100.0;
        updateStepSamples();
    }

    // Reset transport phase/step timing without destroying held or latched notes.
    // Useful when the host transport stops, restarts, or jumps position while the
    // arpeggiator is tempo-synced.
    void rewindPhase(bool resetStep = true) {
        sampleCounter = 0;
        firstStepPending = (noteCount != 0);
        pendingEventValid_ = false;
        // Preserve the last emitted note across transport rewind. Dropping it here
        // silently loses the gate-off and can leave the BitPerfect voice stuck.
        // v961: arm the EXPLICIT block-start flush too — only rewinds/jumps and
        // buffer-exhausted boundaries may release at the next block start; a
        // normally sounding note keeps its gate until its true gate position.
        if (lastNoteEmitted_ >= 0) { pendingGateOff_ = true; flushGateOffAtBlockStart_ = true; }
        if (resetStep) currentStep = 0;
        updateStepSamples();
    }
    
    // Note input
    void noteOn(int midiNote, float velocity) {
        midiNote = std::clamp(midiNote, 0, 127);
        velocity = std::clamp(std::isfinite(velocity) ? velocity : 0.0f, 0.0f, 1.0f);

        const bool wasEmpty = noteCount == 0;
        auto& holdCount = physicalHoldCount[(size_t)midiNote];
        if (holdCount < 255u) ++holdCount;
        physicalVelocity[(size_t)midiNote] = velocity;

        addOrUpdateBufferedNote(midiNote, velocity);
        if (enabled && (wasEmpty || sampleCounter == 0)) {
            // Make the first audible step happen immediately on a fresh note/chord
            // instead of waiting a full step duration. This is especially important
            // for short taps in realtime standalone use.
            firstStepPending = true;
            if (wasEmpty) currentStep = 0;
        }
    }
    
    void noteOff(int midiNote) {
        midiNote = std::clamp(midiNote, 0, 127);
        auto& holdCount = physicalHoldCount[(size_t)midiNote];
        if (holdCount > 0u) --holdCount;

        if (holdCount > 0u) {
            // Overlapping same-note note-ons are still physically held.
            return;
        }

        physicalVelocity[(size_t)midiNote] = 0.0f;

        if (!holdMode && !latchMode) {
            removeBufferedNote(midiNote);
            if (noteCount == 0) {
                currentStep = 0;
                sampleCounter = 0;
                firstStepPending = false;
                pendingEventValid_ = false;
                if (lastNoteEmitted_ >= 0) pendingGateOff_ = true;
            }
        }
    }
    
    void allNotesOff() {
        physicalHoldCount.fill(0u);
        physicalVelocity.fill(0.0f);
        noteCount = 0;
        currentStep = 0;
        sampleCounter = 0;
        firstStepPending = false;
        pendingEventValid_ = false;
        pendingEvent_ = {};
        pendingGateOff_ = false;
        flushGateOffAtBlockStart_ = false;
        lastNoteEmitted_ = -1;
        lastStepEmitted_ = 0;
        lastChordNoteCount_ = 0;
        lastStepSamples_ = std::max(1, samplesPerStep);
    }
    
    // Process and return next arp note (call from audio callback)
    // Returns {midiNote, velocity, gate} or {-1, 0, false} if no note
    struct ArpNote {
        int midiNote = -1;
        float velocity = 0.0f;
        bool gate = false;
    };

    struct TimedArpEvent {
        int sampleOffset = -1;
        int stepIndexBefore = 0;
        ArpNote note{};
    };

    int collectTimedEvents(int numSamples, TimedArpEvent* outEvents, int maxEvents) {
        if (numSamples <= 0) return 0;
        if (!outEvents || maxEvents <= 0 || !enabled || noteCount == 0) {
            if (!enabled || noteCount == 0) {
                firstStepPending = false;
                pendingEventValid_ = false;
                // Emit gate-off for any still-held arp note so voices don't stick.
                // If the caller supplies no event capacity, preserve the pending
                // release for the next render call instead of silently dropping it.
                if (pendingGateOff_ && lastNoteEmitted_ >= 0) {
                    if (outEvents && maxEvents > 0) {
                        outEvents[0] = {0, lastStepEmitted_, ArpNote{lastNoteEmitted_, 0.0f, false}};
                        lastNoteEmitted_ = -1;
                        pendingGateOff_ = false;
                        return 1;
                    }
                    return 0;
                }
                lastNoteEmitted_ = -1;
                pendingGateOff_ = false;
            }
            return 0;
        }

        int count = 0;
        int cursor = 0;
        int localCounter = std::max(0, sampleCounter);

        // Rewind/transport jumps preserve lastNoteEmitted_ and arm the explicit
        // flush flag; flush that gate-off before any new firstStep/pending
        // note-on can fire. v961 P2 fix: this must NOT trigger for a normally
        // sounding step (pendingGateOff_ alone) — that flag is armed by EVERY
        // note-on, and flushing it here chopped every arp note to ~one render
        // block (~10 ms) in mono/legato/unison. A normal note keeps its gate
        // until its true gate position, emitted inside the step loop below.
        if (flushGateOffAtBlockStart_ && pendingGateOff_ && lastNoteEmitted_ >= 0 && !portamentoArpGlide_) {
            flushGateOffAtBlockStart_ = false;
            outEvents[count++] = {0, lastStepEmitted_, ArpNote{lastNoteEmitted_, 0.0f, false}};
            lastNoteEmitted_ = -1;
            pendingGateOff_ = false;
            if (count >= maxEvents) {
                sampleCounter = localCounter + std::max(0, numSamples - cursor);
                return count;
            }
        }
        flushGateOffAtBlockStart_ = false;

        if (pendingEventValid_) {
            pendingEventValid_ = false;
            outEvents[count++] = pendingEvent_;
            if (pendingEvent_.note.gate && pendingEvent_.note.midiNote >= 0) {
                lastNoteEmitted_ = pendingEvent_.note.midiNote;
                lastStepEmitted_ = pendingEvent_.stepIndexBefore;
                pendingGateOff_ = true;
            } else if (!pendingEvent_.note.gate && pendingEvent_.note.midiNote >= 0) {
                if (lastNoteEmitted_ == pendingEvent_.note.midiNote) lastNoteEmitted_ = -1;
                pendingGateOff_ = false;
            }
            if (count >= maxEvents) {
                sampleCounter = localCounter + std::max(0, numSamples - cursor);
                return count;
            }
        }

        if (firstStepPending) {
            firstStepPending = false;
            lastStepSamples_ = std::max(1, samplesPerStep);
            const int firstStepIndex = currentStep;
            const ArpNote firstNote = generateArpNote();
            outEvents[count++] = {0, firstStepIndex, firstNote};
            if (firstNote.gate && firstNote.midiNote >= 0) {
                lastNoteEmitted_ = firstNote.midiNote;
                lastStepEmitted_ = firstStepIndex;
                pendingGateOff_ = true;
            }
            if (count >= maxEvents) {
                sampleCounter = localCounter + std::max(0, numSamples - cursor);
                return count;
            }
        }

        while (cursor < numSamples) {
            const int stepLen = stepSamplesForStep(currentStep);
            // v961: release the sounding note at its TRUE gate position inside the
            // current step window as render time reaches it. Before this, the
            // gate-off could only be emitted at the next step boundary (backdated,
            // clamped into that block), so at slow arp rates the audible gate was
            // either chopped by the old block-start flush or stretched to ~100%
            // of the step; now GATE genuinely shapes the duty cycle at any rate.
            if (pendingGateOff_ && lastNoteEmitted_ >= 0 && !portamentoArpGlide_ && count < maxEvents) {
                const int gateOffRel = static_cast<int>(stepLen * std::clamp(gateLength, 0.1f, 1.0f));
                if (gateOffRel < stepLen) {  // gate < 1.0 → an intra-step off position exists
                    const int offAt = cursor + std::max(0, gateOffRel - localCounter);
                    if (offAt < numSamples) {
                        outEvents[count++] = {offAt, lastStepEmitted_, ArpNote{lastNoteEmitted_, 0.0f, false}};
                        lastNoteEmitted_ = -1;
                        pendingGateOff_ = false;
                    }
                }
            }
            const int remain = std::max(1, stepLen - localCounter);
            if (cursor + remain > numSamples) {
                localCounter += (numSamples - cursor);
                cursor = numSamples;
                break;
            }

            cursor += remain;
            localCounter = 0;
            const int stepIndexBefore = currentStep;
            const int emittedStepSamples = stepSamplesForStep(currentStep);
            lastStepSamples_ = emittedStepSamples;
            const ArpNote note = generateArpNote();

            // FIX: Emit gate-off for the previous note before the new note-on.
            // Gate-off position: gateLength fraction into the step just completed.
            // e.g. gateLength=0.5 → gate-off at 50% of the step, giving 50% duty cycle.
            // Gate-off always fires at least 1 sample before the next note-on.
            if (lastNoteEmitted_ >= 0 && portamentoArpGlide_) {
                // Classic C64 arp-slide/legato behavior: keep the gate open
                // between steps and let the next note-on retarget frequency/glide.
                pendingGateOff_ = true;
            } else if (lastNoteEmitted_ >= 0 && count < maxEvents) {
                const int gateOnSample = cursor;  // position of upcoming note-on
                const int gateOffRelative = static_cast<int>(emittedStepSamples * std::clamp(gateLength, 0.1f, 1.0f));
                const int gateOffAbs = gateOnSample - std::max(1, emittedStepSamples - gateOffRelative);
                const int gateOffSample = std::clamp(gateOffAbs, 0, std::max(0, gateOnSample - 1));
                outEvents[count++] = {gateOffSample, lastStepEmitted_, ArpNote{lastNoteEmitted_, 0.0f, false}};
                lastNoteEmitted_ = -1;
                pendingGateOff_ = false;
            } else if (lastNoteEmitted_ >= 0) {
                // Event buffer exhausted before the mandatory gate-off could be
                // emitted. Keep the previous note/chord armed so next block
                // flushes it at sample 0 instead of losing the release forever.
                pendingGateOff_ = true;
                flushGateOffAtBlockStart_ = true;  // v961: explicit block-start flush
                sampleCounter = localCounter + std::max(0, numSamples - cursor);
                return count;
            }
            if (count >= maxEvents) {
                pendingEvent_ = {0, stepIndexBefore, note};
                pendingEventValid_ = true;
                currentStep = (currentStep + 1) % std::max(1, getEffectivePatternLength());
                samplesPerStep = stepSamplesForStep(currentStep);
                break;
            }

            currentStep = (currentStep + 1) % std::max(1, getEffectivePatternLength());
            samplesPerStep = stepSamplesForStep(currentStep);
            if (cursor >= numSamples) {
                pendingEvent_ = {0, stepIndexBefore, note};
                pendingEventValid_ = true;
                break;
            }
            if (count < maxEvents) {
                const int samplePos = std::clamp(cursor, 0, std::max(0, numSamples - 1));
                outEvents[count++] = {samplePos, stepIndexBefore, note};
                if (note.gate && note.midiNote >= 0) {
                    lastNoteEmitted_ = note.midiNote;
                    lastStepEmitted_ = stepIndexBefore;
                    pendingGateOff_ = true;
                }
            }
        }

        sampleCounter = localCounter;
        return count;
    }
    
    ArpNote process(int numSamples) {
        if (!enabled || noteCount == 0) {
            firstStepPending = false;
            return {-1, 0.0f, false};
        }

        TimedArpEvent events[kMaxTimedEventsPerProcess]{};
        const int n = collectTimedEvents(numSamples, events, kMaxTimedEventsPerProcess);
        return (n > 0) ? events[n - 1].note : ArpNote{-1, 0.0f, false};
    }
    
    // Parameters
    void setEnabled(bool enable) {
        if (enabled == enable) return;
        enabled = enable;
        if (!enabled) {
            firstStepPending = false;
            sampleCounter = 0;
            currentStep = 0;
            pendingEventValid_ = false;
            if (lastNoteEmitted_ >= 0) pendingGateOff_ = true;
        } else if (noteCount != 0) {
            rewindPhase(true);
            firstStepPending = true;
        }
    }
    bool isEnabled() const { return enabled; }
    
    void setMode(float value) {
        // FIX Bug#17: Replaced magic 6.99f with std::lround + clamp.
        // The 6.99 hack worked most of the time but could produce incorrect results
        // near floating-point boundaries (e.g. value=6/7 → ambiguous truncation).
        const float safeValue = std::isfinite(value) ? value : 0.0f;
        const int modeIndex = std::clamp(static_cast<int>(std::lround(safeValue * 6.0f)), 0, 6);
        mode = static_cast<ArpMode>(modeIndex);
        normalizeTransportState_();
    }
    
    void setRate(float value) {
        // FIX v567: Exponential rate curve. value ∈ [0,1] maps to [0.1, 50] Hz
        // via rateHz = 0.1 × 500^value. This places the perceptual midpoint
        // (value=0.5) at √(0.1×50) ≈ 2.236 Hz — one quarter-note per beat at
        // ≈134 BPM — rather than the old linear midpoint of 25 Hz (inaudibly
        // fast for any musical tempo).
        // • value=0 → 0.1 Hz (very slow, ~6 steps/minute)
        // • value=0.5 → 2.24 Hz (quarter-note at ≈134 BPM, geometric mean)
        // • value=1 → 50 Hz (maximum, sub-perceptual blur)
        const float clamped = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
        rateHz = std::clamp(0.1f * std::pow(500.0f, clamped), 0.01f, 50.0f);
        updateStepSamples();
    }
    
    void setRateTempo(float bpm, float division) {
        // Sync to host tempo.
        // division: 1.0=quarter, 0.5=eighth, 0.25=sixteenth, 2.0=half note.
        const float safeBpm = (std::isfinite(bpm) && bpm > 0.0f) ? bpm : 120.0f;
        const float safeDivision = (std::isfinite(division) && std::fabs(division) >= 0.0001f)
            ? std::fabs(division) : 1.0f;
        rateHz = std::clamp((safeBpm / 60.0f) / safeDivision, 0.01f, 50.0f);
        updateStepSamples();
    }
    
    void setOctaves(float value) {
        // 1-4 octaves
        const float safeValue = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
        octaves = 1 + static_cast<int>(safeValue * 3.0f);
        normalizeTransportState_();
    }
    
    void setGateLength(float value) {
        // 0.1 - 1.0 (10% to 100%)
        gateLength = 0.1f + std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f) * 0.9f;
    }

    void setPortamentoArpGlide(bool enabled) noexcept { portamentoArpGlide_ = enabled; }
    bool portamentoArpGlide() const noexcept { return portamentoArpGlide_; }
void setSwing(float value) {
        // swing amount 0..0.75; musical swing percent becomes (1 + swing) / 2
        swing = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f) * 0.75f;
    }
    
    void setHold(bool hold) {
        if (holdMode == hold) return;
        holdMode = hold;
        if (!holdMode && !latchMode) {
            rebuildBufferFromPhysical();
            currentStep = 0;
        }
    }
    void setLatch(bool latch) { 
        if (latchMode == latch) return;
        latchMode = latch;
        if (!latchMode) {
            if (holdMode) {
                currentStep = 0;
            } else {
                rebuildBufferFromPhysical();
                currentStep = 0;
            }
        }
    }
    
    void setTranspose(float value) {
        // FIX: was static_cast<int>((value-0.5)*48) which truncates toward zero,
        // creating asymmetric quantization (positive semitones reach their target
        // value 1 unit earlier than negative ones). Use lround for symmetric
        // semitone mapping. Range: ±24 semitones.
        const float safeValue = std::clamp(std::isfinite(value) ? value : 0.5f, 0.0f, 1.0f);
        transpose = static_cast<int>(std::lround((safeValue - 0.5f) * 48.0f));
    }
    
    void setRandomAmount(float value) {
        randomAmount = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    }
// Pattern editing
    void setPatternStep(int step, int value) {
        if (step >= 0 && step < MAX_PATTERN_STEPS) {
            pattern[step] = std::clamp(value, 0, std::max(0, MAX_PATTERN_STEPS * std::max(1, octaves) - 1));
        }
    }
    
    void setPatternLength(int length) {
        patternLength = std::clamp(length, 1, MAX_PATTERN_STEPS);
        normalizeTransportState_();
    }

    // Convenience overload used by normalized 0..1 parameters.
    // Maps [0..1] -> [1..MAX_PATTERN_STEPS].
    void setPatternLength(float normalized) {
        const float n = (std::isfinite(normalized) ? std::clamp(normalized, 0.0f, 1.0f) : 0.0f);
        const int len = 1 + static_cast<int>(std::lround(n * static_cast<float>(MAX_PATTERN_STEPS - 1)));
        setPatternLength(len);
    }
    
    // Get current state
    int getCurrentStep() const { return currentStep; }
    int getPatternLength() const { return getEffectivePatternLength(); }
    int getSamplesPerStep() const { return std::max(1, samplesPerStep); }
    int getLastStepSamples() const { return std::max(1, lastStepSamples_); }
    float getGateLength() const { return std::clamp(gateLength, 0.01f, 1.0f); }
    bool isChordMode() const { return mode == ArpMode::Chord; }

    int getChordNotesForStep(int step, int* outNotes, float* outVelocities, int maxNotes, bool applyRandom = true) const {
        if (!outNotes || !outVelocities || maxNotes <= 0 || noteCount == 0) return 0;
        const int chordNotes = static_cast<int>(noteCount);
        const int oct = std::max(1, octaves);
        const int safeStep = std::max(0, step);
        const int rotate = (chordNotes > 0) ? (safeStep % chordNotes) : 0;
        int count = 0;
        for (int octave = 0; octave < oct && count < maxNotes; ++octave) {
            for (int i = 0; i < chordNotes && count < maxNotes; ++i) {
                const int srcIndex = (i + rotate) % chordNotes;
                int midiNote = noteBuffer[(size_t)srcIndex].note + transpose + octave * 12;
                if (applyRandom && randomAmount > 0.0f) {
                    midiNote += randomSemitoneJitter(randomAmount, rngState);
                }
                outNotes[count] = std::clamp(midiNote, 0, 127);
                outVelocities[count] = noteBuffer[(size_t)srcIndex].velocity;
                ++count;
            }
        }
        return count;
    }

    void rememberEmittedChordNotes(const int* notes, const float* velocities, int count) noexcept {
        lastChordNoteCount_ = 0;
        if (!notes || !velocities || count <= 0) return;
        const int n = std::clamp(count, 0, kMaxCachedChordNotes);
        for (int i = 0; i < n; ++i) {
            lastChordNotes_[(size_t)i] = std::clamp(notes[i], 0, 127);
            lastChordVelocities_[(size_t)i] = std::isfinite(velocities[i]) ? std::clamp(velocities[i], 0.0f, 1.0f) : 0.0f;
        }
        lastChordNoteCount_ = n;
    }

    int getLastEmittedChordNotes(int* outNotes, float* outVelocities, int maxNotes) const noexcept {
        if (!outNotes || !outVelocities || maxNotes <= 0 || lastChordNoteCount_ <= 0) return 0;
        const int n = std::min(lastChordNoteCount_, maxNotes);
        for (int i = 0; i < n; ++i) {
            outNotes[i] = lastChordNotes_[(size_t)i];
            outVelocities[i] = lastChordVelocities_[(size_t)i];
        }
        return n;
    }

    void clearLastEmittedChordNotes() noexcept { lastChordNoteCount_ = 0; }
    

    static int randomSemitoneJitter(float randomAmount, uint32_t& rngState) noexcept {
        const float amt = std::clamp(randomAmount, 0.0f, 1.0f);
        if (amt <= 0.0f) return 0;
        const uint32_t r1 = ArpSID::ArpSID_xorshift32(rngState);
        const uint32_t r2 = ArpSID::ArpSID_xorshift32(rngState);
        const float u1 = (float)((r1 >> 8) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
        const float u2 = (float)((r2 >> 8) & 0x00FFFFFFu) * (1.0f / 16777215.0f);
        const float moveProb = 0.03f + 0.34f * amt + 0.08f * amt * amt;
        if (u2 >= moveProb) return 0;

        const float centered = (u1 * 2.0f) - 1.0f;
        const float absCentered = std::fabs(centered);
        const int sign = (centered < 0.0f) ? -1 : 1;

        int magnitude = 1;
        const float twoSemiProb = std::max(0.0f, (amt - 0.72f) / 0.28f) * 0.14f;
        if (absCentered > (1.0f - twoSemiProb) && amt > 0.72f) {
            magnitude = 2;
        }

        if (amt < 0.22f && absCentered < 0.70f) return 0;
        return sign * magnitude;
    }
private:
    struct NoteInfo {
        int note;
        float velocity;
    };
    
    static constexpr size_t MAX_HELD_NOTES = 128;
    std::array<NoteInfo, MAX_HELD_NOTES> noteBuffer{};
    size_t noteCount = 0;
    std::array<int, MAX_PATTERN_STEPS> pattern{};
    std::array<uint8_t, 128> physicalHoldCount{};
    std::array<float, 128> physicalVelocity{};
    
    double sampleRate = 44100.0;
    float rateHz = 4.0f;  // 4 notes per second
    int samplesPerStep = 11025;
    int sampleCounter = 0;
    
    int currentStep = 0;
    int octaves = 1;
    float gateLength = 0.9f;
    float swing = 0.0f;
    int transpose = 0;
    float randomAmount = 0.0f;
    
    ArpMode mode = ArpMode::Up;
    bool enabled = false;
    bool holdMode = false;
    bool latchMode = false;
    bool portamentoArpGlide_ = false;
    bool firstStepPending = false;
    bool pendingEventValid_ = false;
    TimedArpEvent pendingEvent_{};
    int patternLength = 16;
    int lastStepSamples_ = 11025;
    bool pendingGateOff_ = false;
    // v961: pendingGateOff_ alone means "a note is sounding and will need its
    // gate-off at the gate position". This flag adds the SECOND meaning that the
    // old code conflated with it: "release at the start of the next block" — armed
    // only by transport rewinds/jumps and buffer-exhausted step boundaries.
    bool flushGateOffAtBlockStart_ = false;
    int lastNoteEmitted_ = -1;  // MIDI note of last gate=true step; -1 = none pending gate-off
    int lastStepEmitted_ = 0;   // Actual pattern/chord step that emitted lastNoteEmitted_.
    static constexpr int kMaxCachedChordNotes = 32;
    int lastChordNoteCount_ = 0;
    std::array<int, kMaxCachedChordNotes> lastChordNotes_{};
    std::array<float, kMaxCachedChordNotes> lastChordVelocities_{};

    mutable uint32_t rngState = 0xC0FFEE12u;
    uint32_t instanceSeed_ = 0u;

    void updateStepSamples() {
        samplesPerStep = stepSamplesForStep(currentStep);
    }

    void normalizeTransportState_() {
        const int effectiveLen = std::max(1, getEffectivePatternLength());
        currentStep = ((currentStep % effectiveLen) + effectiveLen) % effectiveLen;
        sampleCounter = std::clamp(sampleCounter, 0, std::max(0, stepSamplesForStep(currentStep) - 1));
        if (pendingEventValid_) {
            pendingEvent_.stepIndexBefore = ((pendingEvent_.stepIndexBefore % effectiveLen) + effectiveLen) % effectiveLen;
        }
        updateStepSamples();
    }

    int stepSamplesForStep(int stepIndex) const {
        const double sr = std::max(1.0, sampleRate);
        const float hz = std::clamp(std::isfinite(rateHz) ? rateHz : 1.0f, 0.01f, 50.0f);
        const double base = std::max(1.0, sr / (double)hz);
        const float swingAmt = std::clamp(swing, 0.0f, 0.95f);
        const bool odd = (stepIndex & 1) != 0;
        const double mul = odd ? (1.0 + (double)swingAmt) : std::max(0.10, 1.0 - (double)swingAmt);
        return std::max(1, (int)std::lround(base * mul));
    }
    
    void addOrUpdateBufferedNote(int midiNote, float velocity) {
        for (size_t i = 0; i < noteCount; ++i) {
            if (noteBuffer[i].note == midiNote) {
                noteBuffer[i].velocity = velocity;
                return;
            }
        }
        if (noteCount >= MAX_HELD_NOTES) return;
        noteBuffer[noteCount++] = {midiNote, velocity};
        sortNoteBuffer();
    }

    void removeBufferedNote(int midiNote) {
        for (size_t i = 0; i < noteCount; ++i) {
            if (noteBuffer[i].note != midiNote) continue;
            for (size_t j = i + 1; j < noteCount; ++j) noteBuffer[j - 1] = noteBuffer[j];
            --noteCount;
            return;
        }
    }

    void rebuildBufferFromPhysical() {
        noteCount = 0;
        for (int n = 0; n < 128; ++n) {
            if (physicalHoldCount[(size_t)n] == 0u) continue;
            if (noteCount < MAX_HELD_NOTES) noteBuffer[noteCount++] = {n, physicalVelocity[(size_t)n]};
        }
        sortNoteBuffer();
    }

    void sortNoteBuffer() {
        // ARPSID_RT_SORT_CLASSIFICATION: MIDI/render-adjacent, bounded
        // MAX_HELD_NOTES array, noexcept scalar comparator, no allocation.
        std::sort(noteBuffer.begin(), noteBuffer.begin() + static_cast<std::ptrdiff_t>(noteCount),
            [](const NoteInfo& a, const NoteInfo& b) {
                return a.note < b.note;
            });
    }
    
    int getEffectivePatternLength() const {
        if (mode == ArpMode::Pattern) {
            return patternLength;
        }
        const int base = static_cast<int>(noteCount) * std::max(1, octaves);
        // FIX #02: UpDown and DownUp are ping-pong modes: the full cycle is
        // twice as long as the linear span so that currentStep actually reaches
        // the descending half. Without this, currentStep was always wrapped at
        // 'base' which is < totalNotes*2, making the descending branch unreachable.
        if (mode == ArpMode::UpDown || mode == ArpMode::DownUp) {
            return (base > 1) ? (base * 2 - 2) : base;
        }
        return base;
    }
    
    ArpNote generateArpNote() {
        if (noteCount == 0) {
            return {-1, 0.0f, false};
        }

        // Expanded length of the currently buffered chord across octaves.
        // Several modes (Down / UpDown / DownUp) rely on this for reverse/ping-pong indexing.
        const int chordNotes = static_cast<int>(noteCount);
        if (chordNotes <= 0) return ArpNote { -1, 0, false };
        const int oct = std::max(1, octaves);
        const int totalNotes = chordNotes * oct;

        int noteIndex = 0;
        int octaveOffset = 0;

        switch (mode) {
            case ArpMode::Up: {
                noteIndex = currentStep % noteCount;
                octaveOffset = (currentStep / noteCount) % octaves;
                break;
            }
            
            case ArpMode::Down: {
                // Wrap currentStep by totalNotes so reverseStep is always non-negative.
                const int wrappedStep = currentStep % std::max(1, totalNotes);
                int reverseStep = totalNotes - 1 - wrappedStep;
                noteIndex = reverseStep % chordNotes;
                octaveOffset = (reverseStep / chordNotes) % oct;
                break;
            }
            
            case ArpMode::UpDown: {
                const int cycleLen = (totalNotes > 1) ? (totalNotes * 2 - 2) : 1;
                const int pingPong = currentStep % cycleLen;
                const int linear = (pingPong < totalNotes) ? pingPong : (cycleLen - pingPong);
                noteIndex = linear % chordNotes;
                octaveOffset = (linear / chordNotes) % oct;
                break;
            }
            
            case ArpMode::DownUp: {
                const int cycleLen = (totalNotes > 1) ? (totalNotes * 2 - 2) : 1;
                const int pingPong = currentStep % cycleLen;
                const int linearUp = (pingPong < totalNotes) ? pingPong : (cycleLen - pingPong);
                const int linear = (totalNotes - 1) - linearUp;
                noteIndex = linear % chordNotes;
                octaveOffset = (linear / chordNotes) % oct;
                break;
            }
            
            case ArpMode::Random: {
                // FIX Bug#22: Replaced `rng() % nMax` with rejection sampling to
                // eliminate modulo bias. For values of nMax that don't evenly divide 2³²,
                // low indices were slightly over-represented — ~1 part in 10⁹ per step.
                // Rejection sampling is still O(1) amortised (expected < 2 iterations).
                const int nMax = std::max(1, chordNotes);
                const int oMax = std::max(1, oct);
                const uint64_t nRange = (uint64_t)nMax;
                const uint64_t oRange = (uint64_t)oMax;
                const uint64_t nThreshold = (0x100000000ULL / nRange) * nRange;
                const uint64_t oThreshold = (0x100000000ULL / oRange) * oRange;
                uint32_t rn;
                do { rn = ArpSID::ArpSID_xorshift32(rngState); } while ((uint64_t)rn >= nThreshold);
                noteIndex = (int)((uint64_t)rn % nRange);
                uint32_t ro;
                do { ro = ArpSID::ArpSID_xorshift32(rngState); } while ((uint64_t)ro >= oThreshold);
                octaveOffset = (int)((uint64_t)ro % oRange);
                break;
            }
            
            case ArpMode::Pattern: {
                int patternValue = std::max(0, pattern[currentStep % patternLength]);
                noteIndex = patternValue % chordNotes;
                octaveOffset = (patternValue / chordNotes) % oct;
                break;
            }
            
            case ArpMode::Chord: {
                // Play all notes simultaneously (handled externally)
                noteIndex = currentStep % chordNotes;
                octaveOffset = 0;
                break;
            }
        }
        
        // Get base note
        const auto& noteInfo = noteBuffer[(size_t)noteIndex];
        int midiNote = noteInfo.note + (octaveOffset * 12) + transpose;
        
        // Add randomization
        if (randomAmount > 0.0f) {
            midiNote += randomSemitoneJitter(randomAmount, rngState);
        }
        
        // Clamp to MIDI range
        midiNote = std::clamp(midiNote, 0, 127);
        
        return {midiNote, noteInfo.velocity, true};
    }
};

} // namespace ArpSID
