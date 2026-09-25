// SPDX-License-Identifier: BSD-3-Clause
// render_epoch.h — Render-epoch counter (Audit #1, partial fix).
//
// PROBLEM
// ------// `ArpSIDAUv2Component::componentRender` snapshots the bridge generation
// seqlock before calling the wrapped render block and compares it against the
// post-render snapshot. If authority changes after a successful render, the
// wrapper preserves the completed block and reports diagnostics. A failed
// render is still silenced because its output cannot be trusted.
//
// THIS HEADER'S SCOPE
// ------------------// A header-only `RenderEpochCounter` that lifts the existing ad-hoc atomic
// bump pattern into a documented, testable abstraction. Two parties bump
// the epoch:
//
// * State-change paths on the non-RT side (preset apply, scratch resize,
// parameter ramp commit, wrapped-state restore).
// * The render path itself, never. The render path only `snapshot()`s
// at entry and `verifyHeld()`s at exit.
//
// Concretely this is a `std::atomic<uint64_t>` with release/acquire
// semantics that wrap the existing `wrappedStateGeneration` and
// `renderConfigurationGeneration` counters into one observable signal.
//
// WHAT THIS HEADER DOES *NOT* FIX
// ------------------------------// This type detects an authority change; it does not snapshot or roll back
// oscillator, envelope, filter, ring, or sync state. What it does today:
//
// 1. Centralizes epoch detection so future kernel hooks have one documented
// signal and can apply an explicit output policy.
// 2. Provides an SPSC-friendly API (release on bump, acquire on read).
// 3. Adds compile-time guarantees (trivially copyable, atomic-only
// storage, no virtuals) so the type is render-thread safe.

#ifndef ARPSID_CORE_RENDER_EPOCH_H
#define ARPSID_CORE_RENDER_EPOCH_H

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace ArpSID {

// Opaque snapshot of a `RenderEpochCounter`. Compare with `verifyHeld()`.
struct RenderEpochSnapshot {
    std::uint64_t value = 0u;

    constexpr bool operator==(const RenderEpochSnapshot& o) const noexcept { return value == o.value; }
    constexpr bool operator!=(const RenderEpochSnapshot& o) const noexcept { return value != o.value; }
};

static_assert(std::is_trivially_copyable<RenderEpochSnapshot>::value,
              "RenderEpochSnapshot must be trivially copyable for atomic snapshot embedding");

class RenderEpochCounter {
public:
    constexpr RenderEpochCounter() noexcept = default;

    // Non-RT side: increment the epoch and publish to readers. Returns the
    // new epoch value (callers can record it in audit logs).
    std::uint64_t bump() noexcept {
        // fetch_add(1, acq_rel) returns the prior value; we want the new one.
        return value_.fetch_add(1u, std::memory_order_acq_rel) + 1u;
    }

    // RT side, at render entry. Acquire-loads the current epoch so the
    // subsequent kernel work synchronizes-with the most recent bump.
    RenderEpochSnapshot snapshot() const noexcept {
        return RenderEpochSnapshot{ value_.load(std::memory_order_acquire) };
    }

    // RT side, at render exit. Returns true iff the epoch has NOT advanced
    // since `snapshot`. If false, the caller applies its documented policy:
    // reject before rendering, or preserve a successful completed block while
    // reporting the authority change.
    bool verifyHeld(RenderEpochSnapshot snapshot) const noexcept {
        return value_.load(std::memory_order_acquire) == snapshot.value;
    }

    // Diagnostic: current epoch value, for telemetry only. Do NOT use this
    // for synchronization — use `snapshot`/`verifyHeld` instead.
    std::uint64_t currentForTesting() const noexcept {
        return value_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::uint64_t> value_{0u};
};

// Render-side scope guard. Captures the epoch at construction; the caller
// queries `held()` at destruction (or wherever the post-render checkpoint
// lives) to learn whether the epoch survived the render.
//
// Intended usage (AUv2 componentRender):
//
// ArpSID::RenderEpochScope epoch(impl->renderEpoch);
// ... call wrapped render block ...
// if (!epoch.held()) report authority-change telemetry;
//
// The type is intentionally minimal: it has no destructor side-effects so
// it can be used in any control-flow shape without surprises.
class RenderEpochScope {
public:
    explicit RenderEpochScope(const RenderEpochCounter& counter) noexcept
      : counter_(counter), snap_(counter.snapshot()) {}

    bool held() const noexcept { return counter_.verifyHeld(snap_); }
    RenderEpochSnapshot entrySnapshot() const noexcept { return snap_; }

    RenderEpochScope(const RenderEpochScope&) = delete;
    RenderEpochScope& operator=(const RenderEpochScope&) = delete;

private:
    const RenderEpochCounter& counter_;
    RenderEpochSnapshot       snap_;
};

} // namespace ArpSID

#endif // ARPSID_CORE_RENDER_EPOCH_H
