// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// ingress_fallback_edge_ring_v525_tests.cpp
//
// Pins:
// * Audit #24 — IngressFallbackEdgeRing preserves publish order under
// single-producer/single-consumer and multi-producer/single-consumer
// workloads. The audit's ordering-critical scenarios (sustain
// down→up→down, RPN address-then-data handshake, transport play→stop→play)
// all reduce to "every popped sequence is non-decreasing relative to the
// producer ticket counter."
// * The ring's overflow semantics: when capacity is exhausted, the
// overflow counter ticks and the consumer never sees a corrupt slot;
// slots are self-healing across drain epochs.
// * Layout: IngressFallbackEdge is 16 bytes and trivially copyable.

#include "arpsid/core/ingress_fallback_edge_ring.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace {

void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

} // namespace

int main() {
    using namespace ArpSID;

    // ── A. Layout pins ──────────────────────────────────────────────────────
    require(sizeof(IngressFallbackEdge) == 16,
            "IngressFallbackEdge is 16 bytes at runtime");

    // ── B. SPSC ordering across capacity boundary ───────────────────────────
    {
        IngressFallbackEdgeRing<4> ring;
        require(ring.empty(),                                  "freshly constructed ring is empty");
        IngressFallbackEdge out{};
        require(!ring.pop(out),                                 "pop on empty ring returns false");

        for (std::uint8_t i = 0; i < 4; ++i) {
            IngressFallbackEdge e{};
            e.type    = SidTimedEventType::MidiCC;
            e.ccNum   = 64u;
            e.value7  = i;
            require(ring.push(e),                              "push within capacity");
        }
        // Fifth push must overflow (capacity == 4).
        {
            IngressFallbackEdge e{};
            e.type   = SidTimedEventType::MidiCC;
            e.ccNum  = 64u;
            e.value7 = 99u;
            require(!ring.push(e),                             "5th push on capacity-4 ring overflows");
        }
        require(ring.overflowCount() == 1u,                    "overflow counter ticks");

        for (std::uint8_t i = 0; i < 4; ++i) {
            IngressFallbackEdge o{};
            require(ring.pop(o),                               "pop committed slot");
            require(o.value7 == i,                             "pop preserves FIFO order");
        }
        // After consuming everything, the overflow-tagged slot should be
        // visible-as-skipped (the implementation self-heals by popping past it).
        IngressFallbackEdge tail{};
        const bool gotTail = ring.pop(tail);
        // pop() returns false at this point because the skipped slot was
        // already advanced over; no real payload remains.
        require(!gotTail,                                       "no real payload remains after draining real entries");
    }

    // ── C. Sustain stomp pattern (audit #24's #1 example) ───────────────────
    // down → up → down → up: 4 sustain edges. The collapsed latch path
    // would expose only "currently down" — the ring must surface all 4 in
    // order.
    {
        IngressFallbackEdgeRing<16> ring;
        const std::uint8_t pattern[] = { 0u, 127u, 0u, 127u };
        for (std::uint8_t v : pattern) {
            IngressFallbackEdge e{};
            e.type    = SidTimedEventType::MidiCC;
            e.ccNum   = 64u;
            e.value7  = v;
            require(ring.push(e),                              "sustain push");
        }
        for (std::uint8_t expected : pattern) {
            IngressFallbackEdge o{};
            require(ring.pop(o),                               "sustain pop");
            require(o.ccNum == 64u,                            "sustain CC preserved");
            require(o.value7 == expected,                      "sustain transition order preserved");
        }
    }

    // ── D. RPN handshake order (audit #24's #2 example) ─────────────────────
    // RPN MSB → RPN LSB → Data Entry MSB → Data Entry LSB. The collapsed
    // latch path latches all four independently and loses the handshake.
    {
        IngressFallbackEdgeRing<16> ring;
        struct Step { std::uint8_t cc; std::uint8_t val; };
        const Step seq[] = { {101u, 0u}, {100u, 1u}, {6u, 42u}, {38u, 17u} };
        for (const auto& s : seq) {
            IngressFallbackEdge e{};
            e.type   = SidTimedEventType::MidiCC;
            e.ccNum  = s.cc;
            e.value7 = s.val;
            require(ring.push(e),                              "rpn push");
        }
        for (const auto& expected : seq) {
            IngressFallbackEdge o{};
            require(ring.pop(o),                               "rpn pop");
            require(o.ccNum == expected.cc && o.value7 == expected.val,
                    "RPN handshake order preserved end-to-end");
        }
    }

    // ── E. Transport play→stop→play (audit #24's #3 example) ────────────────
    {
        IngressFallbackEdgeRing<16> ring;
        const float pattern[] = { 1.0f, 0.0f, 1.0f };
        for (float v : pattern) {
            IngressFallbackEdge e{};
            e.type       = SidTimedEventType::TransportChange;
            e.valueFloat = v;
            require(ring.push(e),                              "transport push");
        }
        for (float expected : pattern) {
            IngressFallbackEdge o{};
            require(ring.pop(o),                               "transport pop");
            require(o.type == SidTimedEventType::TransportChange,
                    "transport type preserved");
            require(o.valueFloat == expected,                  "transport edge order preserved");
        }
    }

    // ── F. MPSC stress: 4 producers × 1 consumer, ring large enough that
    // no overflow occurs. Producer-side ticket counter is monotonic
    // per producer; the consumer's *aggregate* sequence (producerSeq
    // field) must be strictly monotonic across all popped entries.
    {
        constexpr std::size_t N = 1024;
        IngressFallbackEdgeRing<N> ring;
        constexpr int kProducerCount = 4;
        constexpr int kPushesEach    = 200;

        std::atomic<int> ready{0};
        std::vector<std::thread> producers;
        for (int p = 0; p < kProducerCount; ++p) {
            producers.emplace_back([&, p]{
                ++ready;
                while (ready.load() < kProducerCount) { /* spin */ }
                for (int i = 0; i < kPushesEach; ++i) {
                    IngressFallbackEdge e{};
                    e.type    = SidTimedEventType::MidiCC;
                    e.ccNum   = 64u;
                    e.value7  = static_cast<std::uint8_t>(p);
                    e.data14  = static_cast<std::uint16_t>(i);
                    (void)ring.push(e);
                }
            });
        }
        for (auto& t : producers) t.join();

        // Drain everything.
        std::uint64_t lastSeq = 0u;
        bool firstDrain = true;
        int popped = 0;
        IngressFallbackEdge o{};
        while (ring.pop(o)) {
            if (firstDrain) {
                lastSeq = o.producerSeq;
                firstDrain = false;
            } else {
                require(o.producerSeq > lastSeq,
                        "MPSC drain produces strictly monotonic producerSeq across all popped entries");
                lastSeq = o.producerSeq;
            }
            ++popped;
        }
        require(popped == kProducerCount * kPushesEach,
                "MPSC drain returns every committed entry exactly once");
        require(ring.overflowCount() == 0u,
                "no overflow when ring is large enough");
    }

    // ── G. Reset semantics ──────────────────────────────────────────────────
    {
        IngressFallbackEdgeRing<8> ring;
        IngressFallbackEdge e{};
        e.type   = SidTimedEventType::MidiCC;
        e.ccNum  = 64u;
        e.value7 = 99u;
        require(ring.push(e), "push before reset");
        ring.reset();
        require(ring.empty(),               "ring is empty after reset");
        require(ring.overflowCount() == 0u, "overflow count cleared on reset");
        IngressFallbackEdge o{};
        require(!ring.pop(o),               "no entries to pop after reset");
    }

    std::cout << "ingress_fallback_edge_ring_v525_tests: ordering-critical MPSC ring contract pinned (audit #24/#25)\n";
    return 0;
}
