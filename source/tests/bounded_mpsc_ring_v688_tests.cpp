// Correctness test for the bounded MPSC ingress ring (audit #8/#9/#10/#11):
// multiple producers + single consumer, lock-free, lossless except genuine
// fullness, no contention loss, FIFO per producer.
#include "arpsid_bounded_mpsc_ring.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

using ArpSID::BoundedMpscRing;

static int g_fail = 0;
static void require(bool ok, const char* msg) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); ++g_fail; } }

int main() {
    // ── A. Single-thread FIFO + full/empty ──────────────────────────────────
    {
        BoundedMpscRing<uint32_t, 8> r; // 8 usable slots
        uint32_t v;
        require(!r.pop(v), "empty pop returns false");
        int pushed = 0;
        for (uint32_t i = 0; i < 8; ++i) if (r.push(i)) ++pushed;
        require(pushed == 8, "MPSC ring holds full Capacity (no -1 slot waste)");
        require(!r.push(999), "push fails when full");
        for (uint32_t i = 0; i < 8; ++i) { require(r.pop(v) && v == i, "FIFO order preserved"); }
        require(!r.pop(v), "empty after draining");
        // reuse after drain
        require(r.push(42), "push ok after drain"); require(r.pop(v) && v == 42, "pop reused slot");
    }

    // ── B. Multi-producer + single consumer: no loss, no dup ─────────────────
    {
        constexpr int kProducers = 4;
        constexpr uint32_t kPerProducer = 250000;
        BoundedMpscRing<uint64_t, 1024> ring;
        std::atomic<bool> done{false};
        std::vector<uint32_t> counts((size_t)kProducers, 0u);   // consumer tallies per producer
        std::vector<uint64_t> lastIdx((size_t)kProducers, 0u);  // FIFO-per-producer check
        std::atomic<bool> orderViolation{false};
        std::atomic<long long> totalConsumed{0};

        std::thread consumer([&]{
            uint64_t item;
            auto drain = [&]{
                while (ring.pop(item)) {
                    const uint32_t prod = (uint32_t)(item >> 40);
                    const uint64_t idx  = item & 0xFFFFFFFFFFull;
                    if (prod < (uint32_t)kProducers) {
                        if (counts[prod] != 0 && idx <= lastIdx[prod]) orderViolation.store(true);
                        lastIdx[prod] = idx;
                        counts[prod]++;
                    }
                    totalConsumed.fetch_add(1, std::memory_order_relaxed);
                }
            };
            while (!done.load(std::memory_order_acquire)) { drain(); std::this_thread::yield(); }
            drain();
        });

        std::vector<std::thread> producers;
        std::atomic<long long> totalProduced{0};
        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&, p]{
                for (uint32_t i = 0; i < kPerProducer; ++i) {
                    const uint64_t item = ((uint64_t)p << 40) | (uint64_t)(i + 1);
                    while (!ring.push(item)) std::this_thread::yield(); // retry until delivered
                    totalProduced.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        for (auto& t : producers) t.join();
        done.store(true, std::memory_order_release);
        consumer.join();

        require(totalProduced.load() == (long long)kProducers * kPerProducer, "all items produced");
        require(totalConsumed.load() == totalProduced.load(), "every produced item consumed exactly once (no loss/dup)");
        for (int p = 0; p < kProducers; ++p)
            require(counts[(size_t)p] == kPerProducer, "each producer's items all delivered");
        require(!orderViolation.load(), "per-producer FIFO order preserved (no reorder/dup)");
    }

    // ── C. audit #1: semantically-atomic clear (clearEnqueuedBeforeNow) ──────
    {
        BoundedMpscRing<uint32_t, 16> r;
        for (uint32_t i = 1; i <= 6; ++i) require(r.push(i), "push pre-clear item");
        const std::size_t cleared = r.clearEnqueuedBeforeNow();
        require(cleared == 6u, "clearEnqueuedBeforeNow discards exactly the items enqueued before the call");
        uint32_t v;
        require(!r.pop(v), "ring is empty after the atomic clear");
        // Items pushed AFTER the clear survive — the clean before/after boundary.
        require(r.push(100u) && r.push(101u), "push post-clear items");
        require(r.pop(v) && v == 100u, "post-clear item survives (FIFO 1)");
        require(r.pop(v) && v == 101u, "post-clear item survives (FIFO 2)");
        require(!r.pop(v), "no extra items after the boundary");
        require(r.clearEnqueuedBeforeNow() == 0u, "clear on an empty ring discards nothing");
    }

    if (g_fail == 0) { std::printf("bounded_mpsc_ring_v688_tests: PASS (lossless multi-producer MPSC)\n"); return 0; }
    std::fprintf(stderr, "bounded_mpsc_ring_v688_tests: %d FAILURE(S)\n", g_fail);
    return 1;
}
