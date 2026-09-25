// state_root_mailbox_ownership_v748_tests.cpp
//
// Behavioral tests for ArpSID::OwnershipMailbox (audit P0-1: state-root mailbox
// ABA / ownership race). These are NOT source-string checks — they exercise the
// actual runtime ownership protocol:
//   (1) the three-buffer invariant (producer / consumer / parked are always
//       distinct buffers), proven by address inspection across many publishes,
//       including the "writer laps a stalled reader" case;
//   (2) latest-value-wins semantics;
//   (3) a multi-threaded torn-buffer stress: a producer that laps a slow consumer
//       must never corrupt the buffer the consumer is reading.

#include "arpsid/core/sid_ownership_mailbox.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

// A heap-owning payload that can detect a torn buffer: a consistent value has
// every element of `data` equal to `seq`, and `data.size() == kPayloadLen`.
struct Payload {
    static constexpr std::size_t kPayloadLen = 64;
    uint64_t seq = 0;
    std::vector<uint64_t> data{};

    void set(uint64_t s) {
        seq = s;
        data.assign(kPayloadLen, s);
    }
    bool consistent() const {
        if (data.size() != kPayloadLen) return false;
        for (uint64_t v : data)
            if (v != seq) return false;
        return true;
    }
};

// ── Test 1: three-buffer ownership invariant, including writer-laps-reader ──
//
// At every observable point the producer-owned buffer must be distinct from the
// buffer the consumer currently owns. We also confirm the mailbox only ever
// cycles through exactly three distinct buffer addresses.
static void testThreeBufferInvariant() {
    ArpSID::OwnershipMailbox<Payload> mb;
    std::set<const Payload*> seenBuffers;

    const Payload* consumerOwned = nullptr;

    auto recordProducer = [&]() {
        const Payload* p = &mb.producerSlot();
        seenBuffers.insert(p);
        // The producer's private buffer must never be the one the consumer owns.
        require(p != consumerOwned,
                "producer buffer must differ from consumer-owned buffer");
    };

    // Initial publish + consume round-trip.
    recordProducer();
    mb.producerSlot().set(1);
    mb.publish();
    {
        Payload* got = mb.tryConsume();
        require(got != nullptr, "first consume returns published value");
        require(got->seq == 1 && got->consistent(), "first value intact");
        consumerOwned = got;
    }

    // WRITER LAPS A STALLED READER: the consumer holds its buffer (does not call
    // tryConsume) while the producer publishes many times. Each publish must use a
    // private buffer distinct from the consumer-owned one.
    for (uint64_t s = 2; s <= 1000; ++s) {
        recordProducer();
        mb.producerSlot().set(s);
        mb.publish();
        recordProducer();  // after publish the reclaimed buffer is the new private one
    }

    // Now the consumer finally drains: it must get the LATEST value (1000), intact.
    {
        Payload* got = mb.tryConsume();
        require(got != nullptr, "consume after lapping returns a value");
        require(got->seq == 1000 && got->consistent(),
                "latest-value-wins after producer lapped the reader");
        consumerOwned = got;
    }

    // No fresh value pending now.
    require(mb.tryConsume() == nullptr, "no value after draining latest");
    require(!mb.hasPending(), "hasPending false after drain");

    // The mailbox must only ever have cycled through exactly three buffers.
    require(seenBuffers.size() == 3,
            "ownership mailbox uses exactly three distinct buffers");
}

// ── Test 2: empty-mailbox and interleaved publish/consume semantics ──
static void testInterleavedSemantics() {
    ArpSID::OwnershipMailbox<Payload> mb;
    require(mb.tryConsume() == nullptr, "fresh mailbox has nothing to consume");
    require(!mb.hasPending(), "fresh mailbox not pending");

    for (uint64_t s = 1; s <= 50; ++s) {
        mb.producerSlot().set(s);
        mb.publish();
        require(mb.hasPending(), "pending after publish");
        Payload* got = mb.tryConsume();
        require(got && got->seq == s && got->consistent(),
                "1:1 publish/consume delivers each value intact");
        require(mb.tryConsume() == nullptr, "no double-delivery of the same value");
    }
}

// ── Test 3: multi-threaded torn-buffer stress ──
//
// A producer publishes a stream of monotonically increasing, internally
// consistent payloads as fast as it can. A consumer continuously drains. Every
// consumed payload must be internally consistent (no element written by the
// producer mid-read) and the sequence must be monotonically non-decreasing
// (latest-value-wins; values may be skipped but never go backwards or tear).
static void testConcurrentNoTear() {
    ArpSID::OwnershipMailbox<Payload> mb;
    constexpr uint64_t kPublishes = 2'000'000;
    std::atomic<bool> done{false};
    std::atomic<uint64_t> consumedCount{0};

    std::thread producer([&]() {
        for (uint64_t s = 1; s <= kPublishes; ++s) {
            mb.producerSlot().set(s);
            mb.publish();
        }
        done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        uint64_t last = 0;
        for (;;) {
            Payload* got = mb.tryConsume();
            if (got) {
                require(got->consistent(),
                        "consumed payload is internally consistent (no torn buffer)");
                require(got->seq >= last,
                        "consumed sequence never goes backwards");
                last = got->seq;
                consumedCount.fetch_add(1, std::memory_order_relaxed);
            } else if (done.load(std::memory_order_acquire)) {
                // Drain any final published value.
                Payload* tail = mb.tryConsume();
                if (!tail) break;
                require(tail->consistent(), "final drained payload consistent");
                require(tail->seq >= last, "final sequence not backwards");
                last = tail->seq;
            }
        }
        require(last == kPublishes,
                "consumer eventually observes the final published value");
    });

    producer.join();
    consumer.join();
    require(consumedCount.load() > 0, "consumer made progress");
}

int main() {
    testThreeBufferInvariant();
    testInterleavedSemantics();
    testConcurrentNoTear();
    std::cout << "StateRootMailboxOwnershipV748Tests PASS\n";
    return 0;
}
