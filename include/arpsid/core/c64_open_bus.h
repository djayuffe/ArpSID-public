#pragma once

#include <cstdint>

namespace ArpSID::C64 {

struct OpenBusLatch {
    static constexpr uint64_t kPersistencePhi2 = 0x1D00u;
    static constexpr uint64_t kStrongHoldPhi2 = 512u;
    static constexpr uint64_t kMidDecayPhi2 = 2048u;
    static constexpr uint64_t kWeakDecayPhi2 = 4096u;

    uint8_t latched = 0xFFu;
    uint8_t decayMask = 0xFFu;
    uint64_t lastDrivenPhi2 = 0;

    void powerOn(uint8_t initial = 0xFFu, uint64_t phi2 = 0) noexcept {
        latched = initial;
        decayMask = 0xFFu;
        lastDrivenPhi2 = phi2;
    }

    void drive(uint8_t v, uint64_t phi2 = 0) noexcept {
        latched = v;
        decayMask = 0xFFu;
        lastDrivenPhi2 = phi2;
    }

    void decayToPhi2(uint64_t phi2, bool vicOwnsBus = false) noexcept {
        if (phi2 < lastDrivenPhi2) return;
        const uint64_t age = phi2 - lastDrivenPhi2;
        if (age < kStrongHoldPhi2) return;

        // Real C64 open-bus is capacitive, not a synchronous zero-after-4096
        // latch. Keep the value observable for the documented persistence
        // window (~0x1D00 PHI2) while still applying deterministic bit decay so
        // reads are reproducible in tests and render-side diagnostics.
        uint8_t mask = 0xFFu;
        if (age >= kPersistencePhi2) {
            mask = 0x00u;
        } else if (age >= kWeakDecayPhi2) {
            mask = 0x03u;
        } else if (age >= kMidDecayPhi2) {
            mask = 0x0Fu;
        } else if (age >= 1024u) {
            mask = 0x3Fu;
        } else if (vicOwnsBus) {
            mask = 0x7Fu;
        }
        decayMask = static_cast<uint8_t>(decayMask & mask);
    }

    bool drivenWithinPersistence(uint64_t phi2) const noexcept {
        return phi2 >= lastDrivenPhi2 && (phi2 - lastDrivenPhi2) < kPersistencePhi2;
    }

    uint8_t value() const noexcept { return static_cast<uint8_t>(latched & decayMask); }
};

} // namespace ArpSID::C64
