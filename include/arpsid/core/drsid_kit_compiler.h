// SPDX-License-Identifier: BSD-3-Clause
// drsid_kit_compiler.h — Compiles DrSidInstrumentProgram[] kits into a
// runtime snapshot with register-trace fingerprints (Audit DrSID §3
// follow-up; the slice that finalized the contract left this as the next
// step).
//
// PURPOSE
// ------// `DrSidInstrumentProgram` is the author-side format — every program
// passes `programIsWellFormed` at construction. To run it on the audio
// thread we need:
//
// 1. A *compiled* snapshot the runtime can swap atomically (no parsing,
// no allocation, no validation in the render path).
// 2. A *register-trace fingerprint* per program so CI can pin the
// audible behavior without recording WAV files.
//
// CONTRACT
// -------// * `compileProgram(p)` runs on the non-RT thread, validates the input
// via `programIsWellFormed`, computes the CRC32 over each step's
// (cycleOffset, regIndex, value) triples, and writes the resulting
// fingerprint back into the compiled program's `fingerprint` field.
// * `compileKit(programs, count)` populates a `CompiledDrSidKit` with
// up to `kMaxInstruments` programs. The kit's `generation` field is
// incremented every time so the runtime can detect handoffs.
// * The compiler is render-thread-safe in the sense that it NEVER runs
// on the render thread — but the runtime that consumes its output
// IS render-thread-safe by construction (POD, trivially copyable).

#ifndef ARPSID_CORE_DRSID_KIT_COMPILER_H
#define ARPSID_CORE_DRSID_KIT_COMPILER_H

#include "arpsid/core/drsid_instrument_program.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ArpSID {
namespace Drsid {

// ─── CRC32 (IEEE 802.3 polynomial) over a typed step trace ──────────────────
// Bytewise table-free implementation so we don't carry a 1 KB CRC table
// for a few-hundred-byte input. Speed isn't critical here — this runs
// once per kit compile, not per render block.
constexpr std::uint32_t kCrc32Polynomial = 0xEDB88320u;

inline std::uint32_t crc32Update(std::uint32_t crc, const std::uint8_t* data, std::size_t len) noexcept {
    crc = ~crc;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (kCrc32Polynomial & mask);
        }
    }
    return ~crc;
}

// ─── Compute the register-trace fingerprint for one program ─────────────────
// The trace is the byte-serialized sequence of every step's
// (cycleOffset, regIndex-equivalent, value) tuples. We model the
// "regIndex" abstractly by emitting the entire register-touching payload
// of each step (freq, pw, waveform, ADSR, filter bytes, modeVolume, flags)
// in canonical order. Any future runner that emits actual $D4xx writes
// must agree with this serialization to keep the fingerprint stable.
inline DrSidExpectedFingerprint computeFingerprint(const DrSidInstrumentProgram& p) noexcept {
    DrSidExpectedFingerprint fp{};
    fp.expectedStepCount = static_cast<std::uint32_t>(p.stepCount);

    std::uint32_t crc = 0u;
    auto emit = [&](const std::uint8_t* bytes, std::size_t n) noexcept {
        crc = crc32Update(crc, bytes, n);
    };

    for (std::uint8_t i = 0; i < p.stepCount; ++i) {
        const DrSidRegisterStep& s = p.steps[i];
        // Canonical byte order matches the SID register layout the runner
        // is expected to emit: cycleOffset → freq → pulseWidth → waveform
        // → AD → SR → cutoff lo/hi → resRoute → modeVolume → flags.
        const std::uint8_t bytes[14] = {
            static_cast<std::uint8_t>(s.cycleOffset & 0xFFu),
            static_cast<std::uint8_t>((s.cycleOffset >> 8) & 0xFFu),
            static_cast<std::uint8_t>(s.freq & 0xFFu),
            static_cast<std::uint8_t>((s.freq >> 8) & 0xFFu),
            static_cast<std::uint8_t>(s.pulseWidth & 0xFFu),
            static_cast<std::uint8_t>((s.pulseWidth >> 8) & 0xFFu),
            s.waveform,
            s.attackDecay,
            s.sustainRelease,
            s.filterCutoffLo,
            s.filterCutoffHi,
            s.filterResRoute,
            s.modeVolume,
            s.flags,
        };
        emit(bytes, sizeof(bytes));
    }
    fp.registerTraceHash = crc;

    // First-step spot-check: pack the first step's freq + waveform + flags
    // into a 64-bit word so trivial drift is caught even when the full CRC
    // can't be inspected (e.g., debug breakpoint dumps).
    if (p.stepCount > 0) {
        const DrSidRegisterStep& s0 = p.steps[0];
        std::uint64_t w = static_cast<std::uint64_t>(s0.freq);
        w |= static_cast<std::uint64_t>(s0.pulseWidth) << 16;
        w |= static_cast<std::uint64_t>(s0.waveform)   << 32;
        w |= static_cast<std::uint64_t>(s0.attackDecay)<< 40;
        w |= static_cast<std::uint64_t>(s0.sustainRelease) << 48;
        w |= static_cast<std::uint64_t>(s0.flags)      << 56;
        fp.firstStepWord = w;
    }
    return fp;
}

// ─── compileProgram — validate + fingerprint a single program ──────────────
// Returns true iff the program passes `programIsWellFormed` and the
// fingerprint was computed. On false return, `out` is unchanged.
inline bool compileProgram(const DrSidInstrumentProgram& src,
                           DrSidInstrumentProgram& out) noexcept {
    if (!programIsWellFormed(src)) return false;
    out = src;
    out.fingerprint = computeFingerprint(src);
    return true;
}

// ─── Compiled-kit snapshot for atomic runtime handoff ──────────────────────
// One DrSID kit = up to `kMaxInstruments` compiled programs (typically
// 8 — kick, snare, hat closed/open, clap, cowbell, tom, rim). The kit
// carries an opaque `generation` counter so the runtime's pointer-swap
// handoff can verify "I'm running gen N" against "what I just saw" without
// needing string comparisons.
inline constexpr std::size_t kMaxInstrumentsPerKit = 16;

struct CompiledDrSidKit {
    std::array<DrSidInstrumentProgram, kMaxInstrumentsPerKit> instruments{};
    std::uint8_t  instrumentCount = 0;
    std::uint32_t kitId           = 0;
    std::uint32_t generation      = 0;
    std::uint32_t schemaVersion   = kProgramSchemaVersion;
};
static_assert(std::is_trivially_copyable<CompiledDrSidKit>::value,
              "CompiledDrSidKit must be trivially copyable for atomic snapshot handoff");

// ─── compileKit — validate + fingerprint a whole kit ──────────────────────
// B5: generation is incremented from out.generation so callers can detect
// stale compiled kits without a full re-comparison.
// B6: Atomicity — out is NEVER partially mutated. All validation happens
// in a local staging copy; `out` is overwritten only on full success.
// On false return `out` is unchanged.
[[nodiscard]] inline bool compileKit(const DrSidInstrumentProgram* programs,
                                     std::size_t count,
                                     std::uint32_t kitId,
                                     CompiledDrSidKit& out) noexcept {
    if (programs == nullptr) return false;
    if (count == 0 || count > kMaxInstrumentsPerKit) return false;

    // B6: Build into staging; never touch `out` until fully validated.
    CompiledDrSidKit staging{};
    staging.kitId         = kitId;
    staging.schemaVersion = kProgramSchemaVersion;
    // B5: Increment generation from previous value so callers can observe
    // each successful compile as a distinct snapshot. 0 is reserved for
    // "uninitialized" — start new kits at 1.
    staging.generation    = (out.generation == 0u) ? 1u : out.generation + 1u;

    for (std::size_t i = 0; i < count; ++i) {
        if (!compileProgram(programs[i], staging.instruments[i])) {
            // Validation failed — staging not written to out (B6).
            return false;
        }
    }
    staging.instrumentCount = static_cast<std::uint8_t>(count);
    // Only write to out on full success (B6 atomic failure guarantee).
    out = staging;
    return true;
}

// Helper: locate the first compiled program that matches a given drum class.
// Returns nullptr if not present. Linear scan — fine for ≤16 entries.
inline const DrSidInstrumentProgram* findProgramForClass(const CompiledDrSidKit& kit,
                                                         SidGMDrumClass cls) noexcept {
    for (std::uint8_t i = 0; i < kit.instrumentCount; ++i) {
        if (kit.instruments[i].drumClass == cls) return &kit.instruments[i];
    }
    return nullptr;
}

} // namespace Drsid
} // namespace ArpSID

#endif // ARPSID_CORE_DRSID_KIT_COMPILER_H
