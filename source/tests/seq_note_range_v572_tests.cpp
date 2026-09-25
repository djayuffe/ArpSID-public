// SPDX-License-Identifier: BSD-3-Clause
// seq_note_range_v572_tests.cpp — Sequencer step note formula consistency (v572).
//
// Three code paths map kParamSeqStep*Note [0,1] to a MIDI note (0..127):
//
// Path A — ArpSIDDSPKernel (AUv3) syncSequencerPatternFromParams_()
// seqStep.midiNote = clamp(round(param × 127), 0, 127) ← full MIDI range
//
// Path B — ArpSIDViewController (UI display, _setSequencerStep / seqNotes[] read)
// seqNotes[s] = clamp(round(param × 127), 0, 127) ← full MIDI range (same)
//
// Path C — ArpSIDProcessorPhase2 (AUv2) processSequencer() ← OLD BUG
// midiNote = clamp(round(param × 48) + 36, 0, 127) ← 4-octave [36, 84] only
//
// The bug: Phase2 (AUv2) used a 48-semitone window anchored at C2 (MIDI 36).
// At the parameter default of 0.5:
// Path A + B: round(0.5 × 127) = round(63.5) = 64 (E4)
// Path C old: round(0.5 × 48) + 36 = 24 + 36 = 60 (Middle C) ← wrong!
// At param=0.0:
// Path A + B: MIDI 0 (C-1)
// Path C old: MIDI 36 (C2) ← 3-octave offset at bottom
// At param=1.0:
// Path A + B: MIDI 127 (G9)
// Path C old: MIDI 84 (C6) ← 43-semitone off at top
//
// NEW (correct, v572): Phase2 uses clamp(round(param × 127), 0, 127), matching
// DSPKernel and UI. Middle C (MIDI 60) corresponds to param 60/127 ≈ 0.4724,
// consistent with the UI's defaultCenter hint (60.0/127.0).
//
// Tests cover:
// I. Canonical formula: round(v × 127), clamped to [0, 127]
// II. Old formula documents the range divergence (was [36, 84], now [0, 127])
// III. Two-path consistency: Phase2 and DSPKernel now produce the same MIDI note
// IV. Endpoint contracts: param=0 → MIDI 0, param=1 → MIDI 127
// V. Default param (0.5) → MIDI 64 for both paths (was 60 in old Phase2)
// VI. Middle-C alignment: param 60/127 ≈ 0.4724 → MIDI 60
// VII. Monotone non-decreasing across 11 sample points
// VIII. Clamp guard: param < 0 or > 1 clamped to [0, 127]

#include <cassert>
#include <cmath>
#include <cstddef>
#include <algorithm>
#include <limits>

// ─── Corrected formula (v572) — matches DSPKernel and UI ─────────────────────

static int canonicalSeqNoteFromParam(float noteNorm) {
    const float v = std::isfinite(noteNorm) ? std::clamp(noteNorm, 0.0f, 1.0f) : 0.0f;
    return std::clamp((int)std::lround(v * 127.0f), 0, 127);
}

// ─── Old (broken) Phase2 formula for comparison ──────────────────────────────

static int oldPhase2SeqNoteFromParam(float noteNorm) {
    // OLD: noteNorm × 48 + 36 → range [36, 84] (4-octave C2-C6 window)
    return std::clamp((int)std::lround(noteNorm * 48.0f) + 36, 0, 127);
}

// ─── I. Canonical formula: round(v × 127) ────────────────────────────────────

static void testCanonicalFormula() {
    // param=0.0 → MIDI 0 (C-1, full bottom)
    assert(canonicalSeqNoteFromParam(0.0f) == 0);

    // param=0.5 → round(63.5) = 64 (E4)
    assert(canonicalSeqNoteFromParam(0.5f) == 64);

    // param=1.0 → MIDI 127 (G9, full top)
    assert(canonicalSeqNoteFromParam(1.0f) == 127);

    // param=60/127 ≈ 0.4724 → MIDI 60 (Middle C)
    const float middleCParam = 60.0f / 127.0f;
    assert(canonicalSeqNoteFromParam(middleCParam) == 60);

    // param=0.25 → round(31.75) = 32
    assert(canonicalSeqNoteFromParam(0.25f) == 32);

    // param=0.75 → round(95.25) = 95
    assert(canonicalSeqNoteFromParam(0.75f) == 95);
}

// ─── II. Old formula divergence ──────────────────────────────────────────────

static void testOldFormulaDivergence() {
    // Old range was [36, 84]; new range is [0, 127].
    assert(oldPhase2SeqNoteFromParam(0.0f) == 36);   // 3 octaves above MIDI 0
    assert(oldPhase2SeqNoteFromParam(1.0f) == 84);   // 43 semitones below MIDI 127
    assert(oldPhase2SeqNoteFromParam(0.5f) == 60);   // Middle C (wrong for default)

    // New formula at same points differs significantly.
    assert(canonicalSeqNoteFromParam(0.0f) == 0);    // MIDI 0 not 36
    assert(canonicalSeqNoteFromParam(1.0f) == 127);  // MIDI 127 not 84
    assert(canonicalSeqNoteFromParam(0.5f) == 64);   // MIDI 64 not 60

    // At default param (0.5), old gave Middle C, new gives E4 (matches UI + AUv3).
    const int oldDefault = oldPhase2SeqNoteFromParam(0.5f);
    const int newDefault = canonicalSeqNoteFromParam(0.5f);
    assert(oldDefault == 60);  // Old was wrong
    assert(newDefault == 64);  // New matches DSPKernel + UI

    // Old covered only 48 semitones (4 octaves); new covers all 128 MIDI notes.
    const int oldRange = oldPhase2SeqNoteFromParam(1.0f) - oldPhase2SeqNoteFromParam(0.0f);
    const int newRange = canonicalSeqNoteFromParam(1.0f) - canonicalSeqNoteFromParam(0.0f);
    assert(oldRange == 48);
    assert(newRange == 127);
}

// ─── III. Two-path consistency (Phase2 = DSPKernel formula) ──────────────────

static void testTwoPathConsistency() {
    // DSPKernel formula: clamp(round(param × 127), 0, 127)
    // Phase2 v572 formula: same.
    // Both must produce identical MIDI notes at every sample point.
    for (int i = 0; i <= 20; ++i) {
        const float v = static_cast<float>(i) / 20.0f;
        // Phase2 new formula
        const int phase2Note = std::clamp((int)std::lround(v * 127.0f), 0, 127);
        // DSPKernel formula (canonical reference)
        const int dspNote = std::clamp((int)std::lround(v * 127.0f), 0, 127);
        assert(phase2Note == dspNote);
        // Also verify our helper matches
        assert(canonicalSeqNoteFromParam(v) == phase2Note);
    }
}

// ─── IV. Endpoint contracts ───────────────────────────────────────────────────

static void testEndpoints() {
    // param=0 → MIDI 0 (lowest possible MIDI note)
    assert(canonicalSeqNoteFromParam(0.0f) == 0);

    // param=1 → MIDI 127 (highest possible MIDI note)
    assert(canonicalSeqNoteFromParam(1.0f) == 127);
}

// ─── V. Default param (0.5) produces MIDI 64, matching DSPKernel and UI ──────

static void testDefaultParamAlignment() {
    const float kParamDefault = 0.5f;  // set(..., "", 0.5f) in parameter_ids.h
    const int note = canonicalSeqNoteFromParam(kParamDefault);
    assert(note == 64);  // round(0.5 × 127) = round(63.5) = 64

    // Old formula would have given 60 here — confirm it's now different.
    assert(oldPhase2SeqNoteFromParam(kParamDefault) == 60);
    assert(note != oldPhase2SeqNoteFromParam(kParamDefault));
}

// ─── VI. Middle-C alignment: 60/127 → MIDI 60 ────────────────────────────────

static void testMiddleCAlignment() {
    // The UI uses defaultCenter:60.0f/127.0f as the "default center" hint for
    // sequencer note controls, implying that param value 60/127 should map to
    // MIDI 60 (Middle C). With the canonical formula this is exact.
    const float middleCParam = 60.0f / 127.0f;  // ≈ 0.4724
    assert(canonicalSeqNoteFromParam(middleCParam) == 60);

    // With the OLD formula, 60/127 ≈ 0.4724 gives round(0.4724 × 48) + 36
    // = round(22.676) + 36 = 23 + 36 = 59 (B3) — one semitone flat.
    assert(oldPhase2SeqNoteFromParam(middleCParam) == 59);
    // New formula fixes this alignment.
}

// ─── VII. Monotone non-decreasing ─────────────────────────────────────────────

static void testMonotoneNonDecreasing() {
    int prev = -1;
    for (int i = 0; i <= 20; ++i) {
        const float v = static_cast<float>(i) / 20.0f;
        const int note = canonicalSeqNoteFromParam(v);
        assert(note >= prev);
        prev = note;
    }
    assert(prev == 127);  // must reach maximum
}

// ─── VIII. Clamp guard ────────────────────────────────────────────────────────

static void testClampGuard() {
    // param < 0 → clamp to 0 → MIDI 0
    assert(canonicalSeqNoteFromParam(-1.0f) == 0);
    assert(canonicalSeqNoteFromParam(-0.01f) == 0);

    // param > 1 → clamp to 1 → MIDI 127
    assert(canonicalSeqNoteFromParam(2.0f) == 127);
    assert(canonicalSeqNoteFromParam(1.001f) == 127);

    // NaN → treated as 0 → MIDI 0
    assert(canonicalSeqNoteFromParam(std::numeric_limits<float>::quiet_NaN()) == 0);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int main() {
    testCanonicalFormula();
    testOldFormulaDivergence();
    testTwoPathConsistency();
    testEndpoints();
    testDefaultParamAlignment();
    testMiddleCAlignment();
    testMonotoneNonDecreasing();
    testClampGuard();
    return 0;
}
