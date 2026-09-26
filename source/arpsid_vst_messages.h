// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VST3 controller <-> processor message contract.
//
// Non-realtime commands travel through the VST3 IConnectionPoint (IMessage),
// which hosts deliver on their main/UI thread. The processor handles them off
// the audio thread, exactly like setState(), so heavy work (building a factory
// patch state root) never runs inside process().
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace ArpSID {

// Controller -> processor: load factory patch <slot> (0 .. slot max).
inline constexpr const char* kVstMsgLoadFactoryPatch = "ArpSID.LoadFactoryPatch";
inline constexpr const char* kVstMsgAttrSlot = "slot";

// Editor (via controller) -> processor: one short MIDI channel message from
// the on-screen keyboard (status, data1, data2).
inline constexpr const char* kVstMsgUiMidi = "ArpSID.UiMidi";
inline constexpr const char* kVstMsgAttrStatus = "status";
inline constexpr const char* kVstMsgAttrData1 = "data1";
inline constexpr const char* kVstMsgAttrData2 = "data2";

struct VstUiMidiEvent final {
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

// Single-producer (host UI thread, IConnectionPoint::notify) /
// single-consumer (audio thread, process()) lock-free queue. Full queue drops
// the newest event: on-screen keyboard input is best-effort, never blocking.
template <std::size_t Capacity>
class VstUiMidiQueue final {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    bool push(const VstUiMidiEvent& ev) noexcept {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        const uint32_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= Capacity) return false;
        slots_[head & (Capacity - 1)] = ev;
        head_.store(head + 1u, std::memory_order_release);
        return true;
    }
    bool pop(VstUiMidiEvent& out) noexcept {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        const uint32_t head = head_.load(std::memory_order_acquire);
        if (tail == head) return false;
        out = slots_[tail & (Capacity - 1)];
        tail_.store(tail + 1u, std::memory_order_release);
        return true;
    }
private:
    std::array<VstUiMidiEvent, Capacity> slots_{};
    std::atomic<uint32_t> head_{0u};
    std::atomic<uint32_t> tail_{0u};
};

} // namespace ArpSID
