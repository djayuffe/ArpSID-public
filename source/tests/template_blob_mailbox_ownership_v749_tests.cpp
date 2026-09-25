// template_blob_mailbox_ownership_v749_tests.cpp
//
// Behavioral tests for the serializable-template blob handoff (audit P0-2:
// "Render can decode from shared blob storage while writer may reuse slots").
// The fix routes the blob through ArpSID::OwnershipMailbox<FixedBlobSlot<Cap>> so
// the consumer owns its buffer outright and the producer can never overwrite it
// mid-decode. These tests exercise the real runtime ownership transfer:
//   (1) producer-owned and consumer-owned blob buffers are always distinct
//       (three-buffer invariant), including the producer-laps-consumer case;
//   (2) a multi-threaded torn-blob stress where a fast producer laps a slow
//       consumer: every consumed blob must pass a checksum over its full length
//       (a buffer written by the producer mid-read would corrupt the checksum).

#include "arpsid/core/sid_ownership_mailbox.h"
#include "arpsid/core/sid_state_blob_slot.h"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <thread>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

namespace {
constexpr std::size_t kCap = 4096;
using Slot = ArpSID::FixedBlobSlot<kCap>;

// Framed message: [u64 seq][u64 payloadLen][payload: payloadLen bytes all = seq&0xFF]
// [u64 checksum]. The checksum is a simple sum over seq, payloadLen and payload so a
// torn buffer (some bytes from a different publish) is detected.
std::size_t encodeFrame(uint8_t* buf, std::size_t cap, uint64_t seq) {
    const uint64_t payloadLen = 16 + (seq % (kCap - 64));
    std::size_t off = 0;
    auto putU64 = [&](uint64_t v) {
        std::memcpy(buf + off, &v, sizeof(v));
        off += sizeof(v);
    };
    putU64(seq);
    putU64(payloadLen);
    const uint8_t fill = static_cast<uint8_t>(seq & 0xFFu);
    std::memset(buf + off, fill, payloadLen);
    off += payloadLen;
    uint64_t checksum = seq + payloadLen;
    for (uint64_t i = 0; i < payloadLen; ++i) checksum += fill;
    require(off + 8 <= cap, "frame fits in capacity");
    putU64(checksum);  // off now == 24 + payloadLen (total frame length)
    return off;
}

// Returns true iff the buffer is an internally consistent frame.
bool verifyFrame(const uint8_t* buf, std::size_t len) {
    if (len < 32) return false;
    std::size_t off = 0;
    auto getU64 = [&](uint64_t& v) {
        std::memcpy(&v, buf + off, sizeof(v));
        off += sizeof(v);
    };
    uint64_t seq = 0, payloadLen = 0, checksum = 0;
    getU64(seq);
    getU64(payloadLen);
    if (off + payloadLen + 8 != len) return false;
    const uint8_t fill = static_cast<uint8_t>(seq & 0xFFu);
    uint64_t recomputed = seq + payloadLen;
    for (uint64_t i = 0; i < payloadLen; ++i) {
        if (buf[off + i] != fill) return false;
        recomputed += fill;
    }
    off += payloadLen;
    std::memcpy(&checksum, buf + off, sizeof(checksum));
    return checksum == recomputed;
}
}  // namespace

// ── Test 1: three-buffer ownership invariant incl. producer-laps-consumer ──
static void testBlobOwnershipInvariant() {
    ArpSID::OwnershipMailbox<Slot> mb;
    std::set<const Slot*> seen;
    const Slot* consumerOwned = nullptr;

    auto recordProducer = [&]() {
        const Slot* p = &mb.producerSlot();
        seen.insert(p);
        require(p != consumerOwned,
                "producer blob buffer never aliases consumer-owned buffer");
    };

    recordProducer();
    {
        Slot& s = mb.producerSlot();
        s.len = encodeFrame(s.bytes.get(), kCap, 1);
        mb.publish();
    }
    {
        Slot* got = mb.tryConsume();
        require(got != nullptr, "first blob consumed");
        require(verifyFrame(got->bytes.get(), got->len), "first frame intact");
        consumerOwned = got;
    }

    // Producer laps the (stalled) consumer many times.
    for (uint64_t seq = 2; seq <= 500; ++seq) {
        recordProducer();
        Slot& s = mb.producerSlot();
        s.len = encodeFrame(s.bytes.get(), kCap, seq);
        mb.publish();
        recordProducer();
    }

    Slot* latest = mb.tryConsume();
    require(latest != nullptr, "blob available after lapping");
    require(verifyFrame(latest->bytes.get(), latest->len),
            "latest frame intact after producer lapped consumer");
    uint64_t seq = 0;
    std::memcpy(&seq, latest->bytes.get(), sizeof(seq));
    require(seq == 500, "latest-value-wins delivers most recent blob");

    require(seen.size() == 3, "blob mailbox uses exactly three distinct buffers");
}

// ── Test 2: concurrent fast-producer / slow-consumer torn-blob stress ──
static void testConcurrentNoTornBlob() {
    ArpSID::OwnershipMailbox<Slot> mb;
    constexpr uint64_t kPublishes = 1'000'000;
    std::atomic<bool> done{false};
    std::atomic<uint64_t> consumed{0};

    std::thread producer([&]() {
        for (uint64_t seq = 1; seq <= kPublishes; ++seq) {
            Slot& s = mb.producerSlot();
            s.len = encodeFrame(s.bytes.get(), kCap, seq);
            mb.publish();
        }
        done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        uint64_t lastSeq = 0;
        for (;;) {
            Slot* got = mb.tryConsume();
            if (!got) {
                if (!done.load(std::memory_order_acquire)) {
                    continue; // producer still running — spin for the next publish
                }
                // Producer has finished. publish() of the final blob
                // happens-before done.store(release), and our done.load(acquire)
                // synchronizes with it, so one more tryConsume is guaranteed to
                // surface the latest (final) blob if we have not consumed it yet.
                got = mb.tryConsume();
                if (!got) break; // nothing left — fully drained
            }
            // Single processing path so a blob drained after `done` still records
            // its sequence. Previously the post-done drain consumed the final blob
            // without updating lastSeq, so under load the terminal
            // `lastSeq == kPublishes` check flaked.
            require(verifyFrame(got->bytes.get(), got->len),
                    "consumed blob passes full-length checksum (no torn buffer)");
            uint64_t seq = 0;
            std::memcpy(&seq, got->bytes.get(), sizeof(seq));
            require(seq >= lastSeq, "consumed blob sequence never goes backwards");
            lastSeq = seq;
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
        require(lastSeq == kPublishes, "consumer eventually sees final blob");
    });

    producer.join();
    consumer.join();
    require(consumed.load() > 0, "consumer made progress");
}

int main() {
    testBlobOwnershipInvariant();
    testConcurrentNoTornBlob();
    std::cout << "TemplateBlobMailboxOwnershipV749Tests PASS\n";
    return 0;
}
