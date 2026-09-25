// ─── ArpSIDParityTrace.h ─────────────────────────────────────────────────────
// Phase 5: Host parity trace tool.
// Records canonical events, param values, and audio output hashes per block.
// Enables AU vs VST3 vs Standalone determinism comparison.
// Compile with ARPSID_PARITY_TRACE=1 to enable.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once
#include "ArpSIDCanonicalEvents.h"
#include "../parameter_ids.h"
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <array>
#include <cstdarg>
#include <cstring>
#include <cmath>

namespace ArpSID {

#ifndef ARPSID_PARITY_TRACE
#define ARPSID_PARITY_TRACE 0
#endif

// ── Lightweight audio block hash (FNV-1a over float bits) ────────────────────
inline uint32_t hashAudioBlock(const float* L, const float* R, int frames) noexcept {
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < frames; ++i) {
        uint32_t bL, bR;
        std::memcpy(&bL, L + i, 4);
        std::memcpy(&bR, R + i, 4);
        h ^= bL; h *= 0x01000193u;
        h ^= bR; h *= 0x01000193u;
    }
    return h;
}

// ── Param snapshot hash ──────────────────────────────────────────────────────
inline uint32_t hashParams(const float* params, int count) noexcept {
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < count; ++i) {
        uint32_t b; std::memcpy(&b, params + i, 4);
        h ^= b; h *= 0x01000193u;
    }
    return h;
}

// ── Trace record ─────────────────────────────────────────────────────────────
struct ParityRecord {
    uint64_t blockIndex   = 0;
    double   beatPosition = 0.0;
    double   tempo        = 120.0;
    int      frameCount   = 0;
    int      eventCount   = 0;
    uint32_t audioHash    = 0;
    uint32_t paramHash    = 0;

    // Events (first 16)
    struct EventSummary {
        int32_t  offset;
        EventKind kind;
        int16_t  pitch;
        float    value;
    } events[16];
};

// ── Parity Tracer ─────────────────────────────────────────────────────────────

struct ParityTraceLogSlot {
    std::atomic<uint8_t> ready{0};
    char text[256]{};
};

class ParityTraceLogBuffer {
public:
    static void enqueuef(const char* fmt, ...) noexcept {
#if ARPSID_PARITY_TRACE
        const uint32_t idx = writeIndex_.fetch_add(1, std::memory_order_relaxed) % kCapacity;
        auto& slot = slots_[idx];
        slot.ready.store(0, std::memory_order_relaxed);
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(slot.text, sizeof(slot.text), fmt, args);
        va_end(args);
        slot.ready.store(1, std::memory_order_release);
#else
        (void)fmt;
#endif
    }

    static void flushToFILE(FILE* f = stderr) noexcept {
#if ARPSID_PARITY_TRACE
        if (!f) f = stderr;
        const uint32_t end = writeIndex_.load(std::memory_order_acquire);
        while (readIndex_.load(std::memory_order_relaxed) < end) {
            const uint32_t ridx = readIndex_.load(std::memory_order_relaxed) % kCapacity;
            auto& slot = slots_[ridx];
            if (!slot.ready.load(std::memory_order_acquire)) break;
            std::fputs(slot.text, f);
            std::fputc('\n', f);
            slot.ready.store(0, std::memory_order_relaxed);
            readIndex_.fetch_add(1, std::memory_order_relaxed);
        }
#else
        (void)f;
#endif
    }

private:
    static constexpr uint32_t kCapacity = 256;
    static inline std::array<ParityTraceLogSlot, kCapacity> slots_{};
    static inline std::atomic<uint32_t> writeIndex_{0};
    static inline std::atomic<uint32_t> readIndex_{0};
};

class ParityTracer {
public:
    void setLabel(const char* label) noexcept {
#if ARPSID_PARITY_TRACE
        std::snprintf(label_, sizeof(label_), "%s", label ? label : "?");
#else
        (void)label;
#endif
    }

    void recordBlock(const ParityRecord& rec) noexcept {
#if ARPSID_PARITY_TRACE
        ParityTraceLogBuffer::enqueuef("[PARITY %s] blk=%llu beat=%.4f bpm=%.1f frames=%d evts=%d audioHash=%08X paramHash=%08X",
                    label_, (unsigned long long)rec.blockIndex,
                    rec.beatPosition, rec.tempo, rec.frameCount,
                    rec.eventCount, rec.audioHash, rec.paramHash);
        for (int i = 0; i < std::min(rec.eventCount, 16); ++i) {
            const auto& e = rec.events[i];
            ParityTraceLogBuffer::enqueuef("  [PARITY %s]   ev[%d] offset=%d kind=%d pitch=%d val=%.3f",
                        label_, i, e.offset, (int)e.kind, (int)e.pitch, (double)e.value);
        }
        ++blockCount_;
#else
        (void)rec;
#endif
    }

    // Convenience: build record and log it
    void traceBlock(uint64_t blockIdx,
                    const TransportState& transport,
                    const EventBuffer& events,
                    const float* paramsL, int paramCount,
                    const float* audioL, const float* audioR) noexcept {
#if ARPSID_PARITY_TRACE
        ParityRecord rec{};
        rec.blockIndex   = blockIdx;
        rec.beatPosition = transport.beatPosition;
        rec.tempo        = transport.bpm;
        rec.frameCount   = transport.frameCount;
        rec.eventCount   = events.count;
        rec.audioHash    = (audioL && audioR) ? hashAudioBlock(audioL, audioR, transport.frameCount) : 0;
        rec.paramHash    = paramsL ? hashParams(paramsL, paramCount) : 0;
        const int n = std::min(events.count, 16);
        for (int i = 0; i < n; ++i) {
            rec.events[i] = { events.events[i].sampleOffset, events.events[i].kind,
                              events.events[i].pitch, events.events[i].value };
        }
        recordBlock(rec);
#else
        (void)blockIdx; (void)transport; (void)events;
        (void)paramsL; (void)paramCount; (void)audioL; (void)audioR;
#endif
    }

    // Check two hashes match — returns true if they do
    bool verifyParity(uint32_t hashA, uint32_t hashB, const char* context = "") const noexcept {
#if ARPSID_PARITY_TRACE
        if (hashA != hashB) {
            ParityTraceLogBuffer::enqueuef("[PARITY MISMATCH %s] %s: 0x%08X vs 0x%08X",
                        label_, context, hashA, hashB);
            return false;
        }
#else
        (void)hashA; (void)hashB; (void)context;
#endif
        return hashA == hashB;
    }

    uint64_t blockCount() const noexcept { return blockCount_; }

private:
    [[maybe_unused]] char label_[32] = "Unnamed";
    uint64_t blockCount_  = 0;
};

// ── Edge-case fuzzer (Phase 5) ────────────────────────────────────────────────
// Generates randomized but valid EventBuffers for stress testing.
inline void fuzzEventBuffer(EventBuffer& buf, int frameCount, uint32_t& rng) noexcept {
    buf.reset();
    const int numEvents = std::min((int)(ArpSID_xorshift32(rng) % 40), kMaxTimedEvents - 1);
    for (int i = 0; i < numEvents; ++i) {
        TimedEvent ev{};
        const uint32_t r = ArpSID_xorshift32(rng);
        const int kindRaw = (int)(r % 8);
        ev.kind = static_cast<EventKind>(kindRaw);
        ev.sampleOffset = (int32_t)(ArpSID_xorshift32(rng) % (uint32_t)std::max(1, frameCount));
        ev.channel  = (uint8_t)(ArpSID_xorshift32(rng) % 16);
        ev.pitch    = (int16_t)(ArpSID_xorshift32(rng) % 128);
        ev.value    = (float)(ArpSID_xorshift32(rng) % 1001) / 1000.f;
        ev.ccNum    = (uint8_t)(ArpSID_xorshift32(rng) % 128);
        ev.data14   = (uint16_t)(ArpSID_xorshift32(rng) % 16384);
        ev.noteId   = (int32_t)(ArpSID_xorshift32(rng) % 128) - 1;
        ev.rawOrder = (uint32_t)i;
        ev.sanitize(frameCount);
        buf.push(ev);
    }
    buf.sort();
}

// ── Realtime safety checker ───────────────────────────────────────────────────
// Checks for NaN/Inf in output buffer, logs if found.
inline bool checkAudioSafety(const float* L, const float* R, int frames, const char* ctx) noexcept {
    for (int i = 0; i < frames; ++i) {
        if (!std::isfinite(L[i]) || !std::isfinite(R[i])) {
#if ARPSID_PARITY_TRACE
            ParityTraceLogBuffer::enqueuef("[AUDIO SAFETY] NaN/Inf at frame %d in %s", i, ctx ? ctx : "?");
#else
            (void)ctx;
#endif
            return false;
        }
    }
    return true;
}

} // namespace ArpSID
