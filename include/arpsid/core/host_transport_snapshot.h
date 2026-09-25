#pragma once
#include <atomic>
#include <cstdint>
#include <cmath>

namespace ArpSID {

struct HostTransportPodSnapshot {
    double bpm = 120.0;
    double beatPosition = 0.0;
    double sampleRate = 44100.0;
    double loopStart = 0.0;
    double loopEnd = 0.0;
    int frameCount = 0;
    uint32_t flags = 0;
};

class HostTransportSnapshotSeqlock {
public:
    void reset(double sampleRate = 44100.0, int frameCount = 0) noexcept {
        generation_.store(0u, std::memory_order_relaxed);
        bpm_.store(120.0, std::memory_order_relaxed);
        beat_.store(0.0, std::memory_order_relaxed);
        sampleRate_.store(sanitizeSampleRate_(sampleRate), std::memory_order_relaxed);
        loopStart_.store(0.0, std::memory_order_relaxed);
        loopEnd_.store(0.0, std::memory_order_relaxed);
        frameCount_.store(frameCount, std::memory_order_relaxed);
        flags_.store(0u, std::memory_order_relaxed);
    }

    void publish(const HostTransportPodSnapshot& s) noexcept {
        uint64_t seq = generation_.load(std::memory_order_relaxed);
        if ((seq & 1u) != 0u) ++seq;
        generation_.store(seq + 1u, std::memory_order_release);
        bpm_.store(sanitizeBpm_(s.bpm), std::memory_order_relaxed);
        beat_.store(std::isfinite(s.beatPosition) && s.beatPosition >= 0.0 ? s.beatPosition : 0.0, std::memory_order_relaxed);
        sampleRate_.store(sanitizeSampleRate_(s.sampleRate), std::memory_order_relaxed);
        const double cleanLoopStart = (std::isfinite(s.loopStart) && s.loopStart >= 0.0) ? s.loopStart : 0.0;
        double cleanLoopEnd = (std::isfinite(s.loopEnd) && s.loopEnd >= 0.0) ? s.loopEnd : cleanLoopStart;
        if (cleanLoopEnd < cleanLoopStart) cleanLoopEnd = cleanLoopStart;
        loopStart_.store(cleanLoopStart, std::memory_order_relaxed);
        loopEnd_.store(cleanLoopEnd, std::memory_order_relaxed);
        frameCount_.store(s.frameCount >= 0 ? s.frameCount : 0, std::memory_order_relaxed);
        flags_.store(s.flags, std::memory_order_relaxed);
        generation_.store(seq + 2u, std::memory_order_release);
    }

    bool read(HostTransportPodSnapshot& out) const noexcept {
        for (int retry = 0; retry < 8; ++retry) {
            const uint64_t before = generation_.load(std::memory_order_acquire);
            if (before == 0u || (before & 1u) != 0u) continue;
            HostTransportPodSnapshot s{};
            s.bpm = bpm_.load(std::memory_order_relaxed);
            s.beatPosition = beat_.load(std::memory_order_relaxed);
            s.sampleRate = sampleRate_.load(std::memory_order_relaxed);
            s.loopStart = loopStart_.load(std::memory_order_relaxed);
            s.loopEnd = loopEnd_.load(std::memory_order_relaxed);
            s.frameCount = frameCount_.load(std::memory_order_relaxed);
            s.flags = flags_.load(std::memory_order_relaxed);
            // Fence prevents the relaxed data reads above from being reordered
            // past the trailing generation acquire-load on architectures that
            // permit it. Without this, the textbook seqlock has a theoretical
            // hole where torn reads with before==after can leak through.
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint64_t after = generation_.load(std::memory_order_acquire);
            if (before == after && (after & 1u) == 0u) { out = s; return true; }
        }
        return false;
    }

    uint64_t generation() const noexcept { return generation_.load(std::memory_order_acquire); }

private:
    static double sanitizeBpm_(double v) noexcept { return (std::isfinite(v) && v >= 1.0 && v <= 1000.0) ? v : 120.0; }
    static double sanitizeSampleRate_(double v) noexcept { return (std::isfinite(v) && v >= 8000.0 && v <= 384000.0) ? v : 44100.0; }

    std::atomic<uint64_t> generation_{0};
    std::atomic<double> bpm_{120.0};
    std::atomic<double> beat_{0.0};
    std::atomic<double> sampleRate_{44100.0};
    std::atomic<double> loopStart_{0.0};
    std::atomic<double> loopEnd_{0.0};
    std::atomic<int> frameCount_{0};
    std::atomic<uint32_t> flags_{0};
};

} // namespace ArpSID
