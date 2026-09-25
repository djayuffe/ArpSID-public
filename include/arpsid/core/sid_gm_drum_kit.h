#pragma once

#include <array>
#include <cstdint>

namespace ArpSID {

enum class SidGMDrumClass : uint8_t {
    Kick      = 0,
    Snare     = 1,
    ClosedHat = 2,
    OpenHat   = 3,
    Clap      = 4,
    Cowbell   = 5,
    Tom       = 6,
    Rim       = 7,
    Unsupported = 255
};

struct SidGMDrumNoteSpec final {
    uint8_t note = 0;
    SidGMDrumClass drumClass = SidGMDrumClass::Unsupported;
    const char* name = "Non-GM Drum";
    const char* shortName = "----";
    float velocityScale = 1.0f;
    float tuneOffsetNorm = 0.0f;
    float decayScale = 1.0f;
};

inline constexpr bool sidIsGMDrumChannel(uint8_t ch) noexcept { return ch == 9u; }
inline constexpr bool sidIsGMDrumNote(uint8_t note) noexcept { return note >= 35u && note <= 81u; }
inline constexpr bool sidIsGMDrumMessage(uint8_t ch, uint8_t note) noexcept { return sidIsGMDrumChannel(ch) && sidIsGMDrumNote(note); }

inline constexpr SidGMDrumNoteSpec sidGMUnsupportedDrumSpec(uint8_t note) noexcept {
    return SidGMDrumNoteSpec{note, SidGMDrumClass::Unsupported, "Non-GM Drum", "----", 1.0f, 0.0f, 1.0f};
}

inline constexpr SidGMDrumNoteSpec sidGMDrumSpecForNote(uint8_t note) noexcept {
    switch (note) {
        case 35: return {35, SidGMDrumClass::Kick,      "Acoustic Bass Drum", "BD-A", 1.00f, -0.08f, 1.08f};
        case 36: return {36, SidGMDrumClass::Kick,      "Bass Drum 1",        "BD-1", 1.00f,  0.00f, 1.00f};
        case 37: return {37, SidGMDrumClass::Rim,       "Side Stick",         "STIK", 0.95f,  0.00f, 0.86f};
        case 38: return {38, SidGMDrumClass::Snare,     "Acoustic Snare",     "SN-A", 1.00f,  0.00f, 1.00f};
        case 39: return {39, SidGMDrumClass::Clap,      "Hand Clap",          "CLAP", 0.94f,  0.00f, 1.08f};
        case 40: return {40, SidGMDrumClass::Snare,     "Electric Snare",     "SN-E", 0.94f,  0.12f, 0.92f};
        case 41: return {41, SidGMDrumClass::Tom,       "Low Floor Tom",      "TOM1", 1.00f, -0.30f, 1.12f};
        case 42: return {42, SidGMDrumClass::ClosedHat, "Closed Hi-Hat",      "CHH",  1.00f,  0.00f, 0.95f};
        case 43: return {43, SidGMDrumClass::Tom,       "High Floor Tom",     "TOM2", 1.00f, -0.22f, 1.05f};
        case 44: return {44, SidGMDrumClass::ClosedHat, "Pedal Hi-Hat",       "PHH",  0.88f, -0.02f, 0.85f};
        case 45: return {45, SidGMDrumClass::Tom,       "Low Tom",            "TOM3", 1.00f, -0.12f, 0.98f};
        case 46: return {46, SidGMDrumClass::OpenHat,   "Open Hi-Hat",        "OHH",  1.00f,  0.00f, 1.00f};
        case 47: return {47, SidGMDrumClass::Tom,       "Low-Mid Tom",        "TOM4", 1.00f, -0.04f, 0.94f};
        case 48: return {48, SidGMDrumClass::Tom,       "Hi-Mid Tom",         "TOM5", 1.00f,  0.06f, 0.92f};
        case 49: return {49, SidGMDrumClass::OpenHat,   "Crash Cymbal 1",     "CRS1", 1.04f,  0.18f, 1.70f};
        case 50: return {50, SidGMDrumClass::Tom,       "High Tom",           "TOM6", 1.00f,  0.18f, 0.88f};
        case 51: return {51, SidGMDrumClass::OpenHat,   "Ride Cymbal 1",      "RID1", 0.96f,  0.10f, 1.35f};
        case 52: return {52, SidGMDrumClass::OpenHat,   "Chinese Cymbal",     "CHIN", 1.00f,  0.24f, 1.55f};
        case 53: return {53, SidGMDrumClass::Cowbell,   "Ride Bell",          "BELL", 1.00f,  0.18f, 1.10f};
        case 54: return {54, SidGMDrumClass::ClosedHat, "Tambourine",         "TAMB", 0.90f,  0.12f, 0.92f};
        case 55: return {55, SidGMDrumClass::OpenHat,   "Splash Cymbal",      "SPL",  1.00f,  0.22f, 1.28f};
        case 56: return {56, SidGMDrumClass::Cowbell,   "Cowbell",            "CBEL", 1.00f,  0.00f, 1.00f};
        case 57: return {57, SidGMDrumClass::OpenHat,   "Crash Cymbal 2",     "CRS2", 1.00f,  0.28f, 1.62f};
        case 58: return {58, SidGMDrumClass::Rim,       "Vibraslap",          "VIB",  0.96f,  0.00f, 1.45f};
        case 59: return {59, SidGMDrumClass::OpenHat,   "Ride Cymbal 2",      "RID2", 0.92f,  0.14f, 1.24f};
        case 60: return {60, SidGMDrumClass::Tom,       "High Bongo",         "BNGH", 1.00f,  0.12f, 0.74f};
        case 61: return {61, SidGMDrumClass::Tom,       "Low Bongo",          "BNGL", 1.00f,  0.02f, 0.82f};
        case 62: return {62, SidGMDrumClass::Tom,       "Mute High Conga",    "CGAM", 1.00f,  0.08f, 0.70f};
        case 63: return {63, SidGMDrumClass::Tom,       "Open High Conga",    "CGAO", 1.00f,  0.14f, 0.82f};
        case 64: return {64, SidGMDrumClass::Tom,       "Low Conga",          "CGAL", 1.00f, -0.08f, 0.90f};
        case 65: return {65, SidGMDrumClass::Tom,       "High Timbale",       "TIMH", 1.00f,  0.26f, 0.68f};
        case 66: return {66, SidGMDrumClass::Tom,       "Low Timbale",        "TIML", 1.00f,  0.18f, 0.72f};
        case 67: return {67, SidGMDrumClass::Cowbell,   "High Agogo",         "AGOH", 1.00f,  0.28f, 0.96f};
        case 68: return {68, SidGMDrumClass::Cowbell,   "Low Agogo",          "AGOL", 1.00f,  0.10f, 1.12f};
        case 69: return {69, SidGMDrumClass::ClosedHat, "Cabasa",             "CABA", 0.78f,  0.08f, 1.10f};
        case 70: return {70, SidGMDrumClass::ClosedHat, "Maracas",            "MARA", 0.82f,  0.14f, 1.18f};
        case 71: return {71, SidGMDrumClass::OpenHat,   "Short Whistle",      "WSTL", 0.72f,  0.34f, 0.86f};
        case 72: return {72, SidGMDrumClass::OpenHat,   "Long Whistle",       "WSTH", 0.84f,  0.38f, 1.06f};
        case 73: return {73, SidGMDrumClass::ClosedHat, "Short Guiro",        "GUIR", 0.70f, -0.06f, 0.72f};
        case 74: return {74, SidGMDrumClass::OpenHat,   "Long Guiro",         "GUIL", 0.74f, -0.02f, 0.98f};
        case 75: return {75, SidGMDrumClass::Rim,       "Claves",             "CLAV", 0.88f,  0.02f, 0.78f};
        case 76: return {76, SidGMDrumClass::Rim,       "High Wood Block",    "WBDH", 0.88f,  0.14f, 0.82f};
        case 77: return {77, SidGMDrumClass::Rim,       "Low Wood Block",     "WBDL", 0.88f, -0.10f, 0.92f};
        case 78: return {78, SidGMDrumClass::Tom,       "Mute Cuica",         "CUIM", 0.82f,  0.22f, 0.68f};
        case 79: return {79, SidGMDrumClass::Tom,       "Open Cuica",         "CUIO", 0.84f,  0.16f, 0.74f};
        case 80: return {80, SidGMDrumClass::Cowbell,   "Mute Triangle",      "TRIM", 0.78f,  0.32f, 0.92f};
        case 81: return {81, SidGMDrumClass::OpenHat,   "Open Triangle",      "TRIO", 0.82f,  0.30f, 1.10f};
        default: return sidGMUnsupportedDrumSpec(note);
    }
}

inline constexpr SidGMDrumClass sidGMDrumClassForNote(uint8_t note) noexcept { return sidGMDrumSpecForNote(note).drumClass; }
// FullName returns the human-readable label ("Acoustic Bass Drum"); AbbrevName
// returns the 4-char short code ("BD-A"). The earlier name "sidGMDrumShortName"
// was misleading because it actually returned the long label, not the short code.
inline constexpr const char* sidGMDrumFullName(uint8_t note) noexcept { return sidGMDrumSpecForNote(note).name; }
inline constexpr const char* sidGMDrumAbbrevName(uint8_t note) noexcept { return sidGMDrumSpecForNote(note).shortName; }
// Deprecated alias for source compatibility with older call sites.
inline constexpr const char* sidGMDrumShortName(uint8_t note) noexcept { return sidGMDrumFullName(note); }

inline constexpr const char* sidGMDrumClassName(SidGMDrumClass c) noexcept {
    switch(c){
        case SidGMDrumClass::Kick: return "Kick";
        case SidGMDrumClass::Snare: return "Snare";
        case SidGMDrumClass::ClosedHat: return "ClosedHat";
        case SidGMDrumClass::OpenHat: return "OpenHat";
        case SidGMDrumClass::Clap: return "Clap";
        case SidGMDrumClass::Cowbell: return "Cowbell";
        case SidGMDrumClass::Tom: return "Tom";
        case SidGMDrumClass::Rim: return "Rim";
        default: return "Unsupported";
    }
}

} // namespace ArpSID
