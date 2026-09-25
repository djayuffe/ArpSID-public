#pragma once
#include <cstdint>

// CANONICAL TRUTH: Voice identity is owned by token.
// All voice lifecycle (note-on, note-off, poly-pressure, steal, replay)
// must resolve by token first. (channel, note, noteId) matching is compat-only.

namespace ArpSID {

struct SidVoiceToken {
    uint64_t token = 0;       // 0 = invalid / unbound
    int16_t channel = -1;
    int16_t note = -1;
    int32_t noteId = -1;
    uint32_t arrivalOrder = 0;
    bool active = false;

    bool valid() const noexcept { return token != 0 && active; }
    bool operator==(const SidVoiceToken& o) const noexcept { return token == o.token; }
    bool operator!=(const SidVoiceToken& o) const noexcept { return token != o.token; }
};

struct SidVoiceBinding {
    uint64_t token = 0;
    uint32_t engineVoiceIndex = 0;
    bool bound = false;
};

} // namespace ArpSID
