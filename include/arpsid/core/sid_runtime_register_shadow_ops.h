#pragma once

#include <algorithm>
#include <cstdint>

#include "sid_runtime_register_shadow.h"
#include "arpsid/engines/sid_register_engine.h"

namespace ArpSID {

inline void invalidateRegisterShadow(SidRuntimeRegisterShadow& shadow) noexcept {
    shadow.invalidate();
}

inline void syncRegisterShadowFromLive(const SidRegisterEngine& engine,
                                       SidRuntimeRegisterShadow& shadow) noexcept {
    const auto& regs = engine.getRegs().r;
    for (int r = 0; r < kSidRegCount; ++r) {
        shadow.value[(size_t)r] = regs[(size_t)r];
        shadow.valid[(size_t)r] = 1u;
        shadow.sample[(size_t)r] = 0u;
        shadow.cycle[(size_t)r] = 0u;
    }
}

inline void pushRegisterShadowWrite(SidWriteQueue& queue,
                                    SidRuntimeRegisterShadow& shadow,
                                    uint8_t reg,
                                    uint8_t value,
                                    uint16_t sampleOff,
                                    uint16_t cycleOff) noexcept {
    queue.push(reg, value, sampleOff, cycleOff);
    shadow.value[(size_t)reg] = value;
    shadow.valid[(size_t)reg] = 1u;
    shadow.sample[(size_t)reg] = sampleOff;
    shadow.cycle[(size_t)reg] = cycleOff;
}

inline bool registerShadowMatchesAtOrBefore(const SidRuntimeRegisterShadow& shadow,
                                            uint8_t regIndex,
                                            uint8_t expectedValue,
                                            uint32_t sampleOffset,
                                            uint16_t cycleOffset) noexcept {
    if (regIndex >= kSidRegCount) return false;
    if (!shadow.valid[(size_t)regIndex]) return false;
    if (shadow.value[(size_t)regIndex] != expectedValue) return false;
    const uint16_t shadowSample = shadow.sample[(size_t)regIndex];
    const uint16_t shadowCycle = shadow.cycle[(size_t)regIndex];
    if (sampleOffset == kSidUnresolvedSampleOffset) return true;
    if (shadowSample < sampleOffset) return true;
    if (shadowSample > sampleOffset) return false;
    if (cycleOffset == kSidUnresolvedCycleOffset || shadowCycle == kSidUnresolvedCycleOffset) return true;
    return shadowCycle <= cycleOffset;
}

inline uint8_t currentOrShadowedSidReg(const SidRegisterEngine& engine,
                                       const SidRuntimeRegisterShadow& shadow,
                                       uint8_t reg) noexcept {
    if (shadow.valid[(size_t)reg]) return shadow.value[(size_t)reg];
    return engine.getRegs().r[(size_t)reg];
}

inline uint8_t synthVoiceCtrlNoGate(const SidRuntimeRegisterShadow& shadow,
                                    int voiceIndex) noexcept {
    const int base = voiceIndex * 7;
    return shadow.value[(size_t)base + 4] & 0xFEu;
}

inline void clearQueuedSidWritesForVoice(SidWriteQueue& queue, int voiceIndex) noexcept {
    if (voiceIndex < 0 || voiceIndex > 2) return;
    const int base = voiceIndex * 7;
    queue.eraseIf([&](const SidWrite& w) noexcept -> bool {
        return w.regIndex >= base && w.regIndex < base + 7;
    });
}

inline bool pushRegisterShadowWriteIfChanged(SidWriteQueue& queue,
                                             SidRuntimeRegisterShadow& shadow,
                                             uint8_t reg,
                                             uint8_t value,
                                             uint16_t sampleOff,
                                             uint16_t cycleOff) noexcept {
    if (shadow.valid[(size_t)reg] && shadow.value[(size_t)reg] == value) return false;
    pushRegisterShadowWrite(queue, shadow, reg, value, sampleOff, cycleOff);
    return true;
}

} // namespace ArpSID
