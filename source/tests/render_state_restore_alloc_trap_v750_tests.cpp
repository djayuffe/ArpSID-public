// Copyright (C) 2024-2026 Ulf Bertilsson
// render_state_restore_alloc_trap_v750_tests.cpp
//
// Behavioral test for audit P0-3: the realtime state-restore path
// (SidRuntimeModel::applyStateRootBySwap, called from the audio thread) must not
// allocate. Previously it ran sanitizeStateRoot_ on the audio thread, which
// rebuilt the parameter/semantic vectors and copied the mod-route vector — heap
// allocation in violation of the RT-safe contract. The fix pre-canonicalizes the
// root on the non-RT producer (sidCanonicalizeStateRootForApply) so the swap path
// only reuses already-allocated capacity.
//
// This installs a global allocation trap: while "armed" (a state only the test
// sets, on a single thread, around the call under test) every operator new is
// counted. The test asserts zero allocations across applyStateRootBySwap.

#include "arpsid/core/sid_runtime_model.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

// ── Global allocation trap ───────────────────────────────────────────────────
namespace {
std::atomic<bool> g_trapArmed{false};
std::atomic<long> g_allocCount{0};
inline void countIfArmed() {
    if (g_trapArmed.load(std::memory_order_relaxed))
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace

void* operator new(std::size_t n) {
    countIfArmed();
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n) {
    countIfArmed();
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new(std::size_t n, std::align_val_t al) {
    countIfArmed();
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(al) < sizeof(void*) ? sizeof(void*)
                                                                        : static_cast<std::size_t>(al),
                       n ? n : 1) != 0)
        throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n, std::align_val_t al) {
    return ::operator new(n, al);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }

static void require(bool ok, const char* msg) {
    if (!ok) {
        // Ensure the trap is off before doing anything that may allocate (printing).
        g_trapArmed.store(false, std::memory_order_relaxed);
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID;

    // Build a canonical, fully-hydrated state root the way the non-RT producer would.
    SidStateRootV1 base{};
    sidSetStateRootParamValue(base, kParamAttack, 0.30f);
    sidSetStateRootParamValue(base, kParamDecay, 0.40f);
    sidSetStateRootParamValue(base, kParamSustain, 0.50f);
    sidSetStateRootParamValue(base, kParamRelease, 0.60f);
    // Populate mod routes so the buffer carries a non-empty vector. This makes the
    // trap a real regression guard: if anyone re-introduces a vector-copying
    // sanitize (e.g. `mod_routes = sanitizedModRoutes_(...)`) onto the swap path,
    // copying this non-empty vector under the armed trap will fail the test.
    for (int i = 0; i < 6; ++i) {
        SidModRoute r;
        r.source = SidModSource::LFO1;
        r.target = SidModTarget::FilterCutoff;
        r.depth = 0.25f + 0.1f * static_cast<float>(i);
        r.enabled = true;
        base.patch.mod_routes.push_back(r);
    }
    sidCanonicalizeStateRootForApply(base);
    require(base.valid(), "canonical base root is valid");

    SidRuntimeModel model;

    // Warm up: apply several times so every lazily-grown buffer in the model reaches
    // steady-state capacity. Allocations here are allowed (trap not armed).
    for (int i = 0; i < 4; ++i) {
        SidStateRootV1 warm = base;
        sidCanonicalizeStateRootForApply(warm);
        model.applyStateRootBySwap(warm);
    }

    // Prepare the next pre-canonicalized root BEFORE arming (its construction allocates).
    SidStateRootV1 apply2 = base;
    sidCanonicalizeStateRootForApply(apply2);
    require(apply2.valid(), "second canonical root is valid");

    // ── The audio-thread apply path must be allocation-free. ──
    g_allocCount.store(0, std::memory_order_relaxed);
    g_trapArmed.store(true, std::memory_order_relaxed);
    model.applyStateRootBySwap(apply2);
    g_trapArmed.store(false, std::memory_order_relaxed);

    const long allocs = g_allocCount.load(std::memory_order_relaxed);
    require(allocs == 0, "applyStateRootBySwap performs zero heap allocations (RT-safe)");

    // Functional correctness: the state must actually be installed.
    require(std::abs(sidStateRootParamValue(model.stateRoot(), kParamAttack) - 0.30f) < 1e-5f,
            "attack restored");
    require(std::abs(sidStateRootParamValue(model.stateRoot(), kParamRelease) - 0.60f) < 1e-5f,
            "release restored");
    require(apply2.valid(), "swapped-out root remains a valid SidStateRootV1");

    std::printf("RenderStateRestoreAllocTrapV750Tests PASS\n");
    return 0;
}
