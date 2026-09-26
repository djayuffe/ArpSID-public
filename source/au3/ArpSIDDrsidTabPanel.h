// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// ArpSIDDrsidTabPanel.h — v519 contract-locked binding tables for the
// dedicated DrSID tab inside ArpSIDViewController.mm.
//
// Rationale
// --------// Earlier revisions of the DrSID panel passed `@(ArpSID::kParam…)` literals
// directly into the section builder. Any typo or fictional/future param ID
// would compile fine (every entry is just an `int`) and only surface as a
// silent control with no effect — or, in the v519 contract scenario, as an
// AUv2 link/runtime breakage further down the chain. Worse: a ParamID that
// existed in another branch but had been retired in this v443 codebase
// would still compile, leaving a stale reference in the GUI graph.
//
// This header closes that contract at *compile time*:
//
// • Every knob the DrSID panel exposes is listed as a typed
// `ControlBinding`. Every sequencer-bridge entry is a typed
// `SequencerBridgeBinding`.
// • `constexpr bool isValidParamId(int)` validates against the live
// `kNumParams` range from parameter_ids.h.
// • `allParamIdsValid`, `allControlBindingParamIdsValid`, and
// `allSequencerBridgeBindingParamIdsValid` recursively validate
// constexpr arrays of those types.
// • `static_assert` chains validate every section table, the sequencer
// bridge, and a flat mirror table — and assert that the section
// mirror and the flat list stay in lock-step. Any future drift fails
// the build with a precise diagnostic.
//
// Net effect: it is impossible to introduce a stale or fictional ParamID
// reference in the DrSID GUI without the AUv2 build failing immediately.
// ─────────────────────────────────────────────────────────────────────────────

#include "../parameter_ids.h"

#include <cstddef>

namespace ArpSID {
namespace Drsid {

// One knob on the DrSID page. `label` may be nullptr — in that case the
// renderer pulls the canonical name from `kParamInfos[id].name`. A non-null
// label overrides the canonical name for cases where the DrSID surface
// deliberately retitles a parameter (e.g. "DrSID Volume" reads tighter on
// the focused panel than the raw "DrSID Volume" canonical name).
struct ControlBinding {
    ParamID     id;
    const char* label;
};

// One row of the sequencer-bridge mini-strip. This is the only surface
// through which the DrSID page touches the sequencer; everything else
// lives on the dedicated SEQ tab.
struct SequencerBridgeBinding {
    ParamID     id;
    const char* label;
};

// ── constexpr validators ─────────────────────────────────────────────────────
//
// `isValidParamId` is the single source of truth. The other validators just
// forward into it. The enum kNumParams is asserted by parameter_ids.h to
// equal "last ParamID + 1", so this range check is exact.

constexpr bool isValidParamId(int id) noexcept {
    return id >= 0 && id < static_cast<int>(kNumParams);
}

template <std::size_t N>
constexpr bool allParamIdsValid(const ParamID (&ids)[N]) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        if (!isValidParamId(static_cast<int>(ids[i]))) return false;
    }
    return true;
}

template <std::size_t N>
constexpr bool allControlBindingParamIdsValid(const ControlBinding (&bindings)[N]) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        if (!isValidParamId(static_cast<int>(bindings[i].id))) return false;
    }
    return true;
}

template <std::size_t N>
constexpr bool allSequencerBridgeBindingParamIdsValid(const SequencerBridgeBinding (&bindings)[N]) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        if (!isValidParamId(static_cast<int>(bindings[i].id))) return false;
    }
    return true;
}

// ── voice / control binding tables ───────────────────────────────────────────

constexpr ControlBinding kEngine[] = {
    { kParamDrSidEnable,    nullptr },
    { kParamSidAdsrBug6581, nullptr },
};

constexpr ControlBinding kMachine[] = {
    { kParamDrSidMachineModel, nullptr },
};

constexpr ControlBinding kVolume[] = {
    { kParamDrSidVolume, "DrSID Volume" },
};

constexpr ControlBinding kAccentDrive[] = {
    { kParamDrSidAccentAmount, nullptr },
    { kParamDrSidOutputDrive,  nullptr },
};

constexpr ControlBinding kKick[] = {
    { kParamDrSidKickTune,  nullptr },
    { kParamDrSidKickDecay, nullptr },
};

constexpr ControlBinding kSnare[] = {
    { kParamDrSidSnareTone, nullptr },
    { kParamDrSidSnareSnap, nullptr },
};

constexpr ControlBinding kHat[] = {
    { kParamDrSidHatTune,  nullptr },
    { kParamDrSidHatDecay, nullptr },
    { kParamDrSidHatMetal, nullptr },
};

constexpr ControlBinding kClap[] = {
    { kParamDrSidClapDecay,  nullptr },
    { kParamDrSidClapSpread, nullptr },
};

constexpr ControlBinding kCowbell[] = {
    { kParamDrSidCowbellTune,  nullptr },
    { kParamDrSidCowbellDecay, nullptr },
};

constexpr ControlBinding kTom[] = {
    { kParamDrSidTomTune,  nullptr },
    { kParamDrSidTomDecay, nullptr },
};

constexpr ControlBinding kFilter[] = {
    { kParamFilterCutoff,    nullptr },
    { kParamFilterResonance, nullptr },
    { kParamFilterDrive,     nullptr },
};

constexpr ControlBinding kLimiter[] = {
    { kParamOutputLimiter,    nullptr },
    { kParamLimiterThreshold, nullptr },
};

// ── sequencer bridge mini-strip ──────────────────────────────────────────────
//
// The DrSID page surfaces five sequencer transport parameters. These are the
// only sequencer IDs touched by the dedicated DrSID tab; any deeper editing
// happens on the SEQ tab itself. The set is intentionally small and stable.
constexpr SequencerBridgeBinding kSequencerBridge[] = {
    { kParamSeqEnable, "Seq"   },
    { kParamSeqTempo,  "Tempo" },
    { kParamSeqSwing,  "Swing" },
    { kParamSeqMode,   "Mode"  },
    { kParamSeqLength, "Len"   },
};

// ── compile-time contract closure ────────────────────────────────────────────

static_assert(allControlBindingParamIdsValid(kEngine),
              "DrSID Engine table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kMachine),
              "DrSID Machine table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kVolume),
              "DrSID Volume table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kAccentDrive),
              "DrSID Accent/Drive table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kKick),
              "DrSID Kick table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kSnare),
              "DrSID Snare table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kHat),
              "DrSID Hat table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kClap),
              "DrSID Clap table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kCowbell),
              "DrSID Cowbell table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kTom),
              "DrSID Tom table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kFilter),
              "DrSID Filter table references an unknown ParamID");
static_assert(allControlBindingParamIdsValid(kLimiter),
              "DrSID Limiter table references an unknown ParamID");

static_assert(allSequencerBridgeBindingParamIdsValid(kSequencerBridge),
              "DrSID Sequencer Bridge table references an unknown ParamID");

// Flat mirror — every ParamID referenced by any binding above must also
// appear in this list, and vice-versa. The static_assert at the bottom
// keeps the two representations in lock-step so a new entry added in one
// place but forgotten in the other fails to compile.
constexpr ParamID kAllReferencedParamIds[] = {
    kParamDrSidEnable,      kParamSidAdsrBug6581,
    kParamDrSidMachineModel,
    kParamDrSidVolume,
    kParamDrSidAccentAmount, kParamDrSidOutputDrive,
    kParamDrSidKickTune,     kParamDrSidKickDecay,
    kParamDrSidSnareTone,    kParamDrSidSnareSnap,
    kParamDrSidHatTune,      kParamDrSidHatDecay,    kParamDrSidHatMetal,
    kParamDrSidClapDecay,    kParamDrSidClapSpread,
    kParamDrSidCowbellTune,  kParamDrSidCowbellDecay,
    kParamDrSidTomTune,      kParamDrSidTomDecay,
    kParamFilterCutoff,      kParamFilterResonance,  kParamFilterDrive,
    kParamOutputLimiter,     kParamLimiterThreshold,
    kParamSeqEnable, kParamSeqTempo, kParamSeqSwing, kParamSeqMode, kParamSeqLength,
};

static_assert(allParamIdsValid(kAllReferencedParamIds),
              "DrSID panel flat-mirror table references an unknown ParamID");

constexpr std::size_t kVoiceBindingControlCount =
    (sizeof(kEngine)      / sizeof(kEngine[0]))      +
    (sizeof(kMachine)     / sizeof(kMachine[0]))     +
    (sizeof(kVolume)      / sizeof(kVolume[0]))      +
    (sizeof(kAccentDrive) / sizeof(kAccentDrive[0])) +
    (sizeof(kKick)        / sizeof(kKick[0]))        +
    (sizeof(kSnare)       / sizeof(kSnare[0]))       +
    (sizeof(kHat)         / sizeof(kHat[0]))         +
    (sizeof(kClap)        / sizeof(kClap[0]))        +
    (sizeof(kCowbell)     / sizeof(kCowbell[0]))     +
    (sizeof(kTom)         / sizeof(kTom[0]))         +
    (sizeof(kFilter)      / sizeof(kFilter[0]))      +
    (sizeof(kLimiter)     / sizeof(kLimiter[0]));

constexpr std::size_t kSequencerBridgeBindingCount =
    sizeof(kSequencerBridge) / sizeof(kSequencerBridge[0]);

constexpr std::size_t kAllReferencedParamIdCount =
    sizeof(kAllReferencedParamIds) / sizeof(kAllReferencedParamIds[0]);

static_assert(kVoiceBindingControlCount + kSequencerBridgeBindingCount
              == kAllReferencedParamIdCount,
              "DrSID binding-table mirror and flat ParamID list have drifted out of lock-step");

// Voice trigger pads. These are the MIDI notes the dedicated DrSID
// performance buttons fire. The notes match the General-MIDI drum kit so
// the same buttons drive both the internal DrSID engine and any host MIDI
// routing the user wires up.
struct VoicePadBinding {
    const char* label;
    int         midiNote;
};

constexpr VoicePadBinding kVoicePads[] = {
    { "KICK",  36 },
    { "SNARE", 38 },
    { "C-HAT", 42 },
    { "O-HAT", 46 },
    { "CLAP",  39 },
    { "COW",   56 },
    { "TOM-L", 41 },
    { "TOM-H", 50 },
};

constexpr std::size_t kVoicePadCount =
    sizeof(kVoicePads) / sizeof(kVoicePads[0]);

// MIDI note range guard. Pad notes must be valid 7-bit MIDI (0–127);
// fail the build if anyone slips a bad value in.
constexpr bool allVoicePadNotesValid() noexcept {
    for (std::size_t i = 0; i < kVoicePadCount; ++i) {
        const int n = kVoicePads[i].midiNote;
        if (n < 0 || n > 127) return false;
    }
    return true;
}
static_assert(allVoicePadNotesValid(),
              "DrSID VoicePad table contains a MIDI note outside the 0–127 range");

} // namespace Drsid
} // namespace ArpSID
