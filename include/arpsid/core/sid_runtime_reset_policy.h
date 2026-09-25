#pragma once
#include "sid_dynamic_state.h"
#include "sid_event_queue.h"
#include <cstdint>

// CANONICAL RESET TRUTH (Pass N).
// Panic/reset semantics live here, not in wrappers.
// Both AU and VST wrappers must call these functions — they must not implement
// their own reset/panic logic.
//
// Reset law:
// 1. Amputate all transient queues.
// 2. Amputate all ingress lanes.
// 3. Amputate spill and fallback state.
// 4. Clear all active tokens/identities.
// 5. No stale note/control/pressure event survives reset.

namespace ArpSID {

// Full destructive panic: amputate everything transient.
// Call from any wrapper; do not duplicate this logic in wrappers.
template <class RuntimeModel>
inline void sidRuntimePanic(RuntimeModel& model) noexcept {
    // Clear pending event queue.
    model.pendingEventsNonRealtimeOnly().reset();
    // Clear transient ingress (lock-free nodes + spill + fallbacks + merge lanes).
    model.clearTransientEvents();
    // Clear all active voice tokens and identities.
    model.clearAllCanonicalVoiceState();
    model.clearIdentityMirrors();
    // Reset token serial to guarantee fresh non-reused IDs post-panic.
    // Without this, post-panic tokens can collide with previously-held bindings.
    model.resetTokenSerial();
    // Reset per-channel live MIDI state.
    model.clearLiveMidiState();
}

// All-notes-off on a specific channel: clear note identity for that channel,
// preserve controller state. Both AU and VST must call this.
// [canonical] Single consistent implementation — no divergence between wrappers.
template <class RuntimeModel>
inline void sidRuntimeAllNotesOff(RuntimeModel& model, int channel) noexcept {
    model.clearVoicesForChannel(channel);
}

// Reset-all-controllers: clear controller state but leave note identity intact.
// [canonical] Wrappers must not own this law.
template <class RuntimeModel>
inline void sidRuntimeResetAllControllers(RuntimeModel& model, int channel) noexcept {
    model.resetControllersForChannel(channel);
}

} // namespace ArpSID
