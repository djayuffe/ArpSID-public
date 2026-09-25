#pragma once
#include "sid_event_queue.h"
#include "arpsid/engines/bitperfect_engine.h"
#include "arpsid/engines/sid_register_engine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ArpSID {

inline uint16_t canonicalSidFrequencyRegisterForHz(double hz, double clockHz) noexcept {
    return ArpSID_hzToSidFrequencyRegister(hz, clockHz);
}

inline uint16_t canonicalSidFrequencyRegisterForMidiNote(double midiNote, double clockHz) noexcept {
    if (!std::isfinite(midiNote)) return 0u;
    const double hz = 440.0 * std::exp2((midiNote - 69.0) / 12.0);
    return canonicalSidFrequencyRegisterForHz(hz, clockHz);
}

template <class SidWriteQueueT>
inline void canonicalQueueSidRegisterWrite(SidWriteQueueT& queue, uint8_t reg, uint8_t value, uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
    queue.push(reg, value, sampleOffset, cycleOffset);
}

template <class SidWriteQueueT>
inline void canonicalQueueSidVoiceFrequencyWrite(SidWriteQueueT& queue, int voiceIndex, uint16_t freqReg, uint16_t sampleOffset, uint16_t cycleOffset) noexcept {
    if (voiceIndex < 0 || voiceIndex > 2) return;
    const int base = voiceIndex * 7;
    canonicalQueueSidRegisterWrite(queue, static_cast<uint8_t>(base + 0), static_cast<uint8_t>(freqReg & 0xFFu), sampleOffset, cycleOffset);
    canonicalQueueSidRegisterWrite(queue, static_cast<uint8_t>(base + 1), static_cast<uint8_t>((freqReg >> 8) & 0xFFu), sampleOffset, cycleOffset);
}

inline uint32_t canonicalSidRegisterIndex(const SidTimedEvent& ev) noexcept {
    return std::min<uint32_t>(ev.target, 0x1Du);
}

inline float canonicalSidRegisterNormalizedValue(const SidTimedEvent& ev) noexcept {
    return std::clamp(static_cast<float>(ev.value_u32 & 0xFFu) / 255.0f, 0.0f, 1.0f);
}

inline uint32_t canonicalSidRegisterParamId(uint32_t baseParamId, const SidTimedEvent& ev) noexcept {
    return baseParamId + canonicalSidRegisterIndex(ev);
}

inline void canonicalSidRegisterWrite(BitPerfectEngine* eng, const SidTimedEvent& ev) noexcept {
    if (!eng) return;
    eng->writeSidRegister(static_cast<uint8_t>(canonicalSidRegisterIndex(ev) & 0x1Fu),
                  static_cast<uint8_t>(ev.value_u32 & 0xFFu));
}

inline void canonicalSidRegisterWrite(SidRegisterEngine* eng, const SidTimedEvent& ev) noexcept {
    if (!eng) return;
    const uint8_t reg = static_cast<uint8_t>(canonicalSidRegisterIndex(ev) & 0x1Fu);
    const uint8_t value = static_cast<uint8_t>(ev.value_u32 & 0xFFu);
    if (reg == 0x1Du) eng->writeSystemByte(value);
    else eng->write(reg, value);
}



// Live register authority is event/engine-owned. Do not provide a generic
// apply-whole-image helper here: state/preset restore must enter through the
// controlled runtime restore barrier or timestamped SidRegisterWrite events,
// never by pushing a presentation/readback SidRegisterImage into live engines.

inline void canonicalRenderSidRegister(BitPerfectEngine* bitperfect,
                                       SidRegisterEngine* sidreg,
                                       float* left,
                                       float* right,
                                       int frames) noexcept {
    if (!left || !right || frames <= 0) return;
    // Prefer the dedicated live SID-register engine when available. In synth mode
    // it carries the continuation/sustain state after queued note-start writes have
    // been consumed. Falling back to BitPerfect's raw SID-register engine here can
    // lose sustain state once the queue becomes empty.
    if (sidreg) {
        sidreg->renderBlock(left, right, frames);
        return;
    }
    if (bitperfect) {
        bitperfect->renderSidRegisterBlock(left, right, frames);
        return;
    }
}

} // namespace ArpSID
