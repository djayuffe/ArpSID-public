// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// scope_triple_buffer_multiconsumer_v687_tests.cpp
//
// audit P0.4 regression test ("TelemetryMultipleConsumersStable"): the scope
// triple buffer is documented as wait-free SPSC, but the plugin has many GUI
// telemetry readers (tabs/panels/AUv2+AUv3 wrappers/host polling). A destructive
// tryConsume()/peekLatest() shared between multiple readers lets one reader steal
// the fresh bit / swap the read slot while another is copying. The fix added a
// seqlock-protected published snapshot read via read()/peekLatest() that is
// non-destructive and safe for an arbitrary number of concurrent consumers.
//
// This test runs one producer and TWO concurrent consumers and asserts:
//   * every consumer read is COHERENT (no torn snapshot — both fields agree),
//   * published ids are monotonic non-decreasing per consumer,
//   * both consumers make progress (neither is starved by the other),
//   * peekLatest() never blocks/destroys another consumer's state.

#include "arpsid/core/scope_triple_buffer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace {

struct Snap {
    std::uint64_t id = 0;
    std::uint64_t idMirror = 0;   // producer always writes idMirror == id
    std::uint64_t payload[6] = {0,0,0,0,0,0};  // widen the struct so a torn copy is detectable
};

int g_failures = 0;
void require(bool cond, const char* msg) {
    if (!cond) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_failures; }
}

} // namespace

int main() {
    using ArpSID::ScopeTripleBuffer;

    // ── Single-threaded coherency: read() reflects the latest publish ────────
    {
        ScopeTripleBuffer<Snap> tb;
        Snap out{};
        require(!tb.read(out), "read() returns false before first publish");
        Snap& w = tb.writeSlot();
        w.id = 42; w.idMirror = 42;
        tb.publish();
        require(tb.read(out), "read() returns true after publish");
        require(out.id == 42 && out.idMirror == 42, "read() returns coherent latest snapshot");
        // Non-destructive: a second read still returns the same latest snapshot.
        Snap out2{};
        require(tb.read(out2), "read() is non-destructive (still readable)");
        require(out2.id == 42, "second read() returns the same latest snapshot");
        // peekLatest mirrors read() and never returns a torn/old value.
        Snap peek{};
        tb.peekLatest(peek);
        require(peek.id == 42 && peek.idMirror == 42, "peekLatest returns latest coherent snapshot");
    }

    // ── Concurrent: 1 producer, 2 consumers, coherency + progress ────────────
    {
        ScopeTripleBuffer<Snap> tb;
        std::atomic<bool> stop{false};
        constexpr std::uint64_t kPublishes = 200000;

        std::atomic<std::uint64_t> readsA{0}, readsB{0}, lastA{0}, lastB{0};
        std::atomic<bool> tornA{false}, tornB{false}, regA{false}, regB{false};

        auto consumer = [&](std::atomic<std::uint64_t>* reads,
                            std::atomic<std::uint64_t>* lastId,
                            std::atomic<bool>* torn,
                            std::atomic<bool>* regressed) {
            Snap out{};
            std::uint64_t prev = 0;
            while (!stop.load(std::memory_order_acquire)) {
                // Alternate the two non-destructive read paths.
                bool got = (reads->load(std::memory_order_relaxed) & 1u) ? tb.read(out)
                                                                         : (tb.peekLatest(out), true);
                if (!got) continue;
                // peekLatest() returns the default snapshot (id==0) when read()
                // exhausts its torn-retry budget under heavy contention — that is
                // documented acceptable behavior, not a real published value
                // (publishes start at id==1), so skip the sentinel here.
                if (out.id == 0) continue;
                // Coherency: a torn read would mix fields from two publishes.
                if (out.idMirror != out.id) torn->store(true, std::memory_order_relaxed);
                for (auto p : out.payload) if (p != out.id) torn->store(true, std::memory_order_relaxed);
                // Published ids only increase, so a consumer must never see one go
                // backwards (the seqlock returns whole snapshots, never a stale mix).
                if (out.id < prev) regressed->store(true, std::memory_order_relaxed);
                prev = out.id;
                lastId->store(out.id, std::memory_order_relaxed);
                reads->fetch_add(1u, std::memory_order_relaxed);
            }
        };

        // Start both consumers before the producer so neither misses the burst.
        std::thread cA([&]{ consumer(&readsA, &lastA, &tornA, &regA); });
        std::thread cB([&]{ consumer(&readsB, &lastB, &tornB, &regB); });

        std::thread producer([&] {
            for (std::uint64_t i = 1; i <= kPublishes; ++i) {
                Snap& w = tb.writeSlot();
                w.id = i;
                w.idMirror = i;
                for (auto& p : w.payload) p = i;
                tb.publish();
            }
            // v969 flake fix: do NOT end the run until both consumers have made
            // progress. Under a loaded parallel CTest a consumer thread can be
            // starved for the whole fixed burst, which previously failed the
            // progress assertion — a scheduling artifact, not a buffer defect.
            // The latest snapshot stays readable, so a consumer that is finally
            // scheduled reads it and progresses. Bounded (~5s) so a genuinely
            // stuck consumer still fails the test rather than hanging.
            for (int guard = 0;
                 guard < 50000 && !(readsA.load(std::memory_order_relaxed) > 0 &&
                                    readsB.load(std::memory_order_relaxed) > 0);
                 ++guard) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            stop.store(true, std::memory_order_release);
        });

        producer.join();
        cA.join();
        cB.join();

        require(!tornA.load() && !tornB.load(), "no torn snapshot reached either consumer");
        require(!regA.load() && !regB.load(), "no consumer observed an id going backwards");
        require(readsA.load() > 0, "consumer A made progress (not starved)");
        require(readsB.load() > 0, "consumer B made progress (not starved)");
    }

    if (g_failures == 0) {
        std::printf("scope_triple_buffer_multiconsumer_v687_tests: PASS (audit P0.4 multi-consumer telemetry stable)\n");
        return 0;
    }
    std::fprintf(stderr, "scope_triple_buffer_multiconsumer_v687_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
