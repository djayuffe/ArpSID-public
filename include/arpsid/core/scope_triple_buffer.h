// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// scope_triple_buffer.h — Wait-free SPSC triple buffer (Audit #14, #15).
//
// PROBLEM
// ------// The legacy scope snapshot in BitPerfectEngine used a two-buffer
// alternating scheme: producer writes to `1 - readIdx`, GUI reads from
// `readIdx`. With only two buffers, the audit identified two races:
//
// #14 If the GUI copies the read buffer slowly, the audio thread can
// publish twice — at which point producer and consumer collide on
// the same buffer.
//
// #15 `setScopeCaptureEnabled(false)` zeroed the live scope arrays
// from a non-audio thread, racing with the audio thread that owns
// those arrays.
//
// FIX
// --// A classic single-producer/single-consumer triple buffer with three
// snapshot slots and an 8-bit atomic state word that packs
// (writeIdx, cleanIdx, readIdx, freshBit). Both `publish()` and
// `tryConsume()` are wait-free for the SPSC case — each operation is a
// CAS-loop bounded by single-producer contention (so it terminates after
// at most one retry in practice).
//
// CONTRACT
// -------// * `writeSlot()` returns a mutable reference to a buffer that NO consumer
// is currently reading from. Producer is free to memcpy into it.
//
// * `publish()` makes the most-recently-written buffer the "clean" one
// and atomically picks up the previous clean buffer as the new write
// slot. Sets the fresh bit so the consumer knows new data is ready.
//
// * `tryConsume(out)` returns true iff fresh data was published since
// the last consume; in that case `out` receives a copy of the freshest
// snapshot. Returns false (and leaves `out` untouched) if no new data
// is available.
//
// * `peekLatest(out)` is the GUI-convenience variant: it consumes fresh
// data if available, otherwise re-copies the previously consumed slot.
// Always returns a snapshot (the slot itself remains stable across
// calls until the next `tryConsume`/`peekLatest`).
//
// MULTI-CONSUMER READ (audit P0.4 / #4)
// ------------------------------------// This plugin has many GUI telemetry readers (tabs, panels, AUv2/AUv3
// wrappers, host polling). The destructive SPSC `tryConsume()`/triple-buffer
// rotation is correct for exactly ONE consumer, so `read()`/`peekLatest()`
// instead serve any number of concurrent consumers from an
// `AtomicSnapshotSeqlock` mirror that the producer refreshes inside
// `publish()`. The mirror backs the snapshot with atomic words, so the
// multi-consumer path is data-race-free under ThreadSanitizer — not merely
// coherent-in-practice.
//
// SPSC INVARIANTS (proof sketch)
// -----------------------------// * The three index pairs (write, clean, read) are *always pairwise
// distinct* — the state word literally encodes a permutation of {0,1,2}.
// * Producer only ever exchanges (write ↔ clean). Consumer only ever
// exchanges (read ↔ clean). Neither operation can land on the other
// party's index because both party's indices stay outside the swap.
// * Memory ordering: producer uses release on the state CAS, consumer
// uses acquire on the state CAS. The buffer contents are therefore
// fully visible to the consumer after a successful `tryConsume`.

#ifndef ARPSID_CORE_SCOPE_TRIPLE_BUFFER_H
#define ARPSID_CORE_SCOPE_TRIPLE_BUFFER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ArpSID {

// State word layout (8 bits):
// bits 0–1 : writeIdx (0..2)
// bits 2–3 : cleanIdx (0..2)
// bits 4–5 : readIdx (0..2)
// bit 7 : freshBit (1 = consumer has new data to pick up)
namespace detail {
constexpr std::uint8_t kSTB_WriteMask = 0x03;
constexpr std::uint8_t kSTB_CleanShift = 2;
constexpr std::uint8_t kSTB_CleanMask  = 0x0C;
constexpr std::uint8_t kSTB_ReadShift  = 4;
constexpr std::uint8_t kSTB_ReadMask   = 0x30;
constexpr std::uint8_t kSTB_FreshBit   = 0x80;

constexpr std::uint8_t makeState(std::uint8_t w, std::uint8_t c, std::uint8_t r, bool fresh) noexcept {
    return static_cast<std::uint8_t>((w & 0x3)
                                   | ((c & 0x3) << kSTB_CleanShift)
                                   | ((r & 0x3) << kSTB_ReadShift)
                                   | (fresh ? kSTB_FreshBit : 0));
}

constexpr std::uint8_t writeIdx(std::uint8_t s) noexcept { return static_cast<std::uint8_t>(s & kSTB_WriteMask); }
constexpr std::uint8_t cleanIdx(std::uint8_t s) noexcept { return static_cast<std::uint8_t>((s & kSTB_CleanMask) >> kSTB_CleanShift); }
constexpr std::uint8_t readIdx (std::uint8_t s) noexcept { return static_cast<std::uint8_t>((s & kSTB_ReadMask)  >> kSTB_ReadShift); }
constexpr bool         fresh   (std::uint8_t s) noexcept { return (s & kSTB_FreshBit) != 0; }

// Initial state: write=0, clean=2, read=1, no fresh data.
constexpr std::uint8_t kSTB_InitialState = makeState(0, 2, 1, false);

// audit #4: generic, data-race-free seqlock mirror for an arbitrary trivially
// copyable Snapshot. The earlier mirror stored the Snapshot as a plain (non-
// atomic) member and relied on a generation counter to keep it coherent. That
// is correct in practice but is a formal C++ data race — a non-atomic object
// read by consumers while the producer writes it — which ThreadSanitizer flags
// and which the standard leaves undefined regardless of the guard.
//
// This version backs the snapshot bytes with an array of std::atomic words, so
// EVERY shared access (payload words AND the generation) goes through an atomic.
// There is therefore no non-atomic shared access and no data race in the C++
// model; the seqlock generation still discards any torn read. This mirrors the
// already-clean HostTransportSnapshotSeqlock (which decomposes its POD into
// per-field atomics) but works for any Snapshot via a word-array memcpy.
template <typename Snapshot>
class AtomicSnapshotSeqlock {
    static_assert(std::is_trivially_copyable<Snapshot>::value,
                  "AtomicSnapshotSeqlock requires a trivially copyable Snapshot (memcpy semantics).");
    using Word = std::uintptr_t;
    static constexpr std::size_t kWordCount =
        (sizeof(Snapshot) + sizeof(Word) - 1u) / sizeof(Word);
    // audit #4: the seqlock is only data-race-free AND wait-free if both the
    // payload words and the generation are genuinely lock-free atomics. If a
    // target ever made these emulated (mutex-backed) atomics, the producer side
    // would take a lock on the audio thread — assert that away at compile time.
    static_assert(std::atomic<Word>::is_always_lock_free,
                  "AtomicSnapshotSeqlock payload atomics must be lock-free (no audio-thread lock).");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "AtomicSnapshotSeqlock generation atomic must be lock-free.");

public:
    AtomicSnapshotSeqlock() noexcept {
        for (auto& w : words_) w.store(0u, std::memory_order_relaxed);
    }

    // Quiescent-state reset. Caller guarantees no concurrent store()/load().
    void reset() noexcept {
        seq_.store(0u, std::memory_order_release);
        for (auto& w : words_) w.store(0u, std::memory_order_relaxed);
    }

    // Single-producer publish of a fresh snapshot. Generation goes odd → even.
    // audit #4: runs on the AUDIO thread. It streams word-by-word straight from
    // the source snapshot into the atomic array using a SINGLE Word of stack
    // (O(1)), instead of building a full `Word[kWordCount]` temporary — for a
    // large scope Snapshot that temporary was a multi-KB audio-thread stack
    // allocation. Each store is a plain aligned mov on a lock-free atomic.
    void store(const Snapshot& s) noexcept {
        const auto* src = reinterpret_cast<const unsigned char*>(&s);
        const std::uint64_t g = seq_.load(std::memory_order_relaxed);
        seq_.store(g + 1u, std::memory_order_release);          // odd: write in progress
        for (std::size_t i = 0; i < kWordCount; ++i) {
            Word w = 0u;                                        // zero-fills tail of the last partial word
            const std::size_t off = i * sizeof(Word);
            const std::size_t n   = (sizeof(Snapshot) - off < sizeof(Word))
                                        ? (sizeof(Snapshot) - off) : sizeof(Word);
            std::memcpy(&w, src + off, n);
            words_[i].store(w, std::memory_order_relaxed);
        }
        seq_.store(g + 2u, std::memory_order_release);          // even: stable
    }

    // Multi-consumer coherent read. Returns false before the first store() or in
    // the pathological case of >8 consecutive torn reads. Runs on GUI consumer
    // threads (never the audio thread); it streams word-by-word into `out` with
    // O(1) stack and only reports success when the generation was stable across
    // the copy. `out` is left untouched before the first publish (the only path
    // a non-test caller relies on); a torn final retry may leave it partially
    // written, which callers discard via the false return (peekLatest overwrites).
    bool load(Snapshot& out) const noexcept {
        auto* dst = reinterpret_cast<unsigned char*>(&out);
        for (int retry = 0; retry < 8; ++retry) {
            const std::uint64_t before = seq_.load(std::memory_order_acquire);
            if (before == 0u || (before & 1u) != 0u) continue;  // never stored / write in progress
            for (std::size_t i = 0; i < kWordCount; ++i) {
                const Word w = words_[i].load(std::memory_order_relaxed);
                const std::size_t off = i * sizeof(Word);
                const std::size_t n   = (sizeof(Snapshot) - off < sizeof(Word))
                                            ? (sizeof(Snapshot) - off) : sizeof(Word);
                std::memcpy(dst + off, &w, n);
            }
            // Fence so the relaxed payload loads above cannot be reordered past
            // the trailing generation acquire-load (mirrors HostTransportSnapshotSeqlock).
            std::atomic_thread_fence(std::memory_order_acquire);
            const std::uint64_t after = seq_.load(std::memory_order_acquire);
            if (before == after && (after & 1u) == 0u) return true;
        }
        return false;
    }

    std::uint64_t generation() const noexcept { return seq_.load(std::memory_order_acquire); }

private:
    alignas(64) std::atomic<Word> words_[kWordCount];
    std::atomic<std::uint64_t>    seq_{0};
};
} // namespace detail

template <typename Snapshot>
class ScopeTripleBuffer {
    static_assert(std::is_trivially_copyable<Snapshot>::value,
                  "ScopeTripleBuffer requires a trivially copyable Snapshot type so it can be "
                  "memcpy'd into the read-side `out` parameter without invoking copy/move ctors.");
public:
    static constexpr int kSlotCount = 3;

    ScopeTripleBuffer() noexcept : state_(detail::kSTB_InitialState) {}

    // Quiescent-state reset for setup/reset paths. NOT safe to call while a
    // producer or consumer is concurrently accessing the buffer — the name
    // makes that precondition explicit at the call site (audit #5). There are
    // intentionally no concurrent-safe reset semantics; tear down telemetry only
    // when render is stopped.
    void resetQuiescent() noexcept {
        for (auto& slot : buf_) slot = Snapshot{};
        state_.store(detail::kSTB_InitialState, std::memory_order_release);
        publishedOnce_.store(false, std::memory_order_release);
        // audit P0.4/#4: reset the multi-consumer seqlock mirror too.
        mirror_.reset();
    }

    // P2-1: true once publish() has run at least once since construction/reset.
    // Lets consumers distinguish "no telemetry yet" from "valid zero telemetry".
    bool hasPublished() const noexcept { return publishedOnce_.load(std::memory_order_acquire); }

    // ── Producer (audio thread) interface ───────────────────────────────────
    // Returns a reference to the buffer the audio thread is allowed to write.
    // The reference remains valid until the next `publish()` call.
    Snapshot& writeSlot() noexcept {
        return buf_[detail::writeIdx(state_.load(std::memory_order_relaxed))];
    }

    // Clear the producer-owned slot through one explicit, layout-aware API.
    // Keeping this operation inside the triple-buffer type prevents compilers
    // from losing the slot bounds while optimizing a large temporary assignment
    // through writeSlot(), which previously triggered -Wstringop-overflow.
    void clearWriteSlot() noexcept {
        Snapshot& slot = writeSlot();
        slot = Snapshot{};
    }

    // Promote the just-written slot to "clean", and atomically pick up the
    // previous clean slot as the new write target. Set the fresh bit so a
    // pending or future consumer knows there's new data.
    void publish() noexcept {
        // P2-1: distinguish "never published" from "published a zero snapshot" so
        // consumers can tell genuine-but-zero telemetry from a pre-first-publish
        // initial slot returned by peekLatest().
        publishedOnce_.store(true, std::memory_order_release);
        std::uint8_t old = state_.load(std::memory_order_relaxed);
        std::uint8_t committedWrite = detail::writeIdx(old);
        for (;;) {
            const std::uint8_t w = detail::writeIdx(old);
            const std::uint8_t c = detail::cleanIdx(old);
            const std::uint8_t r = detail::readIdx(old);
            const std::uint8_t neu = detail::makeState(c, w, r, /*fresh=*/true);
            if (state_.compare_exchange_weak(old, neu,
                                             std::memory_order_acq_rel,
                                             std::memory_order_relaxed)) {
                committedWrite = w;
                break;
            }
        }
        // audit P0.4: the SPSC triple buffer is correct for exactly one consumer,
        // but this plugin has many GUI telemetry readers (tabs, panels, AUv2/AUv3
        // wrappers, host polling). A destructive tryConsume()/peekLatest() shared
        // between multiple readers lets one reader steal the fresh bit / swap the
        // read slot while another is copying. So in addition to the triple buffer,
        // publish a seqlock-protected copy of the just-committed snapshot that any
        // number of consumers can read coherently and non-destructively via
        // read()/peekLatest(). The just-committed slot is now "clean" and the
        // single producer will not write it again until the consumer rotates it,
        // so copying it here is race-free on the producer side. audit #4: the
        // mirror is now an atomic-word-backed seqlock, so the consumer-side copy
        // is also a data-race-free (TSan-clean) atomic read, not a non-atomic
        // object read concurrently with this write.
        mirror_.store(buf_[committedWrite]);
    }

    // ── Consumer (GUI thread) interface ─────────────────────────────────────
    // audit #5: the destructive SPSC consume path is NO LONGER part of the public
    // surface. Multi-consumer GUI code must use read()/peekLatest() (below), which
    // are non-destructive; a stray tryConsume() from a second consumer would steal
    // the fresh bit / rotate the read slot under another reader. The destructive
    // single-consumer rotation is now private and exercised only by the SPSC
    // contract tests via ScopeTripleBufferSpscTestAccess.

    // audit P0.4: non-destructive, multi-consumer-safe coherent read of the
    // latest published snapshot via the seqlock mirror. Unlike tryConsume(),
    // this does NOT mutate the triple-buffer read/clean ownership, so any number
    // of GUI consumers may call read()/peekLatest() concurrently without
    // stealing each other's fresh state. Returns false only before the first
    // publish() (or in the pathological case of >8 consecutive torn reads).
    bool read(Snapshot& out) const noexcept {
        // audit #4: delegates to the atomic-word-backed seqlock mirror. All
        // shared accesses (payload + generation) are atomic, so concurrent
        // producer publish() / multi-consumer read() is data-race-free.
        return mirror_.load(out);
    }

    // Convenience: always returns a snapshot. Non-destructive (audit P0.4): it
    // returns the latest published snapshot, or a default snapshot before the
    // first publish(). hasPublished() distinguishes "no telemetry yet" from a
    // genuine zero snapshot. Safe to call from multiple consumer threads.
    void peekLatest(Snapshot& out) const noexcept {
        if (!read(out)) out = Snapshot{};
    }

    // Diagnostic: returns the current state word (writeIdx | cleanIdx | readIdx | freshBit).
    // Intended for tests only.
    std::uint8_t stateForTesting() const noexcept { return state_.load(std::memory_order_acquire); }

private:
    // audit #5: destructive single-consumer (SPSC) consume — private. If fresh
    // data is available, atomically swap the consumer's read slot with the clean
    // slot and copy the fresh snapshot into `out`; returns false (out untouched)
    // if nothing new was published. Only the SPSC contract tests reach this via
    // ScopeTripleBufferSpscTestAccess; production uses read()/peekLatest().
    bool tryConsumeSpsc_(Snapshot& out) noexcept {
        std::uint8_t old = state_.load(std::memory_order_acquire);
        for (;;) {
            if (!detail::fresh(old)) return false;
            const std::uint8_t w = detail::writeIdx(old);
            const std::uint8_t c = detail::cleanIdx(old);
            const std::uint8_t r = detail::readIdx(old);
            const std::uint8_t neu = detail::makeState(w, r, c, /*fresh=*/false);
            if (state_.compare_exchange_weak(old, neu,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
                out = buf_[c];  // trivial copy of the now-owned read slot
                return true;
            }
        }
    }

    template <typename S> friend struct ScopeTripleBufferSpscTestAccess;

    alignas(64) Snapshot       buf_[kSlotCount]{};
    std::atomic<std::uint8_t> state_;
    std::atomic<bool>         publishedOnce_{false};
    // audit P0.4/#4: atomic-word-backed seqlock mirror for non-destructive,
    // data-race-free (TSan-clean) multi-consumer reads.
    detail::AtomicSnapshotSeqlock<Snapshot> mirror_{};
};

// audit #5: test-only accessor for the now-private destructive SPSC consume.
// Keeps the single-consumer triple-buffer contract under test without exposing a
// destructive method on the public class surface.
template <typename Snapshot>
struct ScopeTripleBufferSpscTestAccess {
    static bool tryConsume(ScopeTripleBuffer<Snapshot>& tb, Snapshot& out) noexcept {
        return tb.tryConsumeSpsc_(out);
    }
};

} // namespace ArpSID

#endif // ARPSID_CORE_SCOPE_TRIPLE_BUFFER_H
