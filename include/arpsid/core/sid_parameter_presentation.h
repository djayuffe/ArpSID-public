#pragma once

// ─── v966 canonical host parameter presentation authority ───────────────────
//
// One parameter-ID-aware format/parse/unit service shared by AUv2, AUv3 and
// VST3. Before v966 each wrapper carried its own generic unit-string formulas
// (e.g. "Hz" → norm × 20000) that disagreed with the canonical DSP laws
// (LFO rate is exponential 0.1 × 200^norm), so host text misrepresented real
// behavior and text entry produced wrong normalized values.
//
// Laws used here are the same helpers the runtime/DSP calls
// (math_utils.h, sid_runtime_forensic_config.h, parameter_ids.h,
// sid_mod_matrix_types.h). Do not add a display formula here without routing
// the DSP through the identical helper.
//
// Locale contract: formatting uses snprintf and therefore the process
// LC_NUMERIC decimal separator; parsing is locale-independent and accepts
// both '.' and ',' as the decimal separator, so parse(format(v)) roundtrips
// under any host locale. Parsing never mutates the output on failure.
//
// Everything is bounded, allocation-free and noexcept so callers on
// non-realtime host threads (parameter display callbacks) stay cheap; render
// code has no reason to call this header.

#include "parameter_ids.h"
#include "math_utils.h"
#include "sid_runtime_forensic_config.h"
#include "sid_mod_matrix_types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ArpSID {

// ─── Semantic unit identity (typed, not stringly) ───────────────────────────

enum class SidParameterUnit : uint8_t {
    Normalized = 0,   // raw 0..1 value
    Seconds,          // portamento (quadratic law)
    Milliseconds,     // limiter attack/release (per-parameter linear laws)
    Hertz,            // LFO rate (exponential law)
    Cents,            // master tune ±100 ct
    Bpm,              // sequencer tempo 20 + 280 × norm
    Steps,            // sequencer length 1..32
    Percent,          // 0..100 %
    Celsius,          // forensic die temperature
    Volts,            // forensic supply voltage
    Byte,             // raw SID register byte 0..255
    Index,            // floor-binned selector shown as its integer index
    IndexedLabel,     // enum with canonical text labels
    Boolean,          // ON/OFF
    Seed,             // 32-bit chip seed integer
};

struct ParameterUnitDescriptor {
    SidParameterUnit unit = SidParameterUnit::Normalized;
    const char* suffix = "";   // display suffix, "" when none
};

// ─── Canonical mod-source labels (presentation-only, single authority) ──────

inline const char* sidModSourceLabelFromIndex(int idx) noexcept {
    switch (static_cast<SidModSource>(std::clamp(idx, 0, kSidModSourceCount - 1))) {
        case SidModSource::None: return "None";
        case SidModSource::LFO1: return "LFO1";
        case SidModSource::LFO2: return "LFO2";
        case SidModSource::LFO3: return "LFO3";
        case SidModSource::LFO4: return "LFO4";
        case SidModSource::Velocity: return "Velocity";
        case SidModSource::NoteNumber: return "Note";
        case SidModSource::KeyFollow: return "KeyFollow";
        case SidModSource::ModWheel: return "ModWheel";
        case SidModSource::PitchBend: return "PitchBend";
        case SidModSource::AfterTouch: return "Aftertouch";
        case SidModSource::PolyPressure: return "PolyPressure";
        case SidModSource::Random: return "Random";
        case SidModSource::Macro1: return "Macro1";
        case SidModSource::Macro2: return "Macro2";
        case SidModSource::Macro3: return "Macro3";
        case SidModSource::Macro4: return "Macro4";
        case SidModSource::Macro5: return "Macro5";
        case SidModSource::Macro6: return "Macro6";
        case SidModSource::Macro7: return "Macro7";
        case SidModSource::Macro8: return "Macro8";
        case SidModSource::Env1: return "Env1";
        default: return "None";
    }
}

constexpr inline bool sidParameterIsModSourceParam(int id) noexcept {
    return id >= static_cast<int>(kParamModVCFCutoffSource) &&
           id <= static_cast<int>(kParamModMasterVolumeSource) &&
           ((id - static_cast<int>(kParamModVCFCutoffSource)) % 2) == 0;
}

// ─── Bounded UTF-8 ⇄ UTF-16 conversion (v966, replaces byte casting) ─────────
//
// Templated on the destination 16-bit character type so the VST3 TChar
// (char16), unichar and plain char16_t all use one tested implementation.
// Malformed input decodes deterministically to U+FFFD; output is always
// null-terminated; truncation never splits a surrogate pair.

template <typename Char16>
inline std::size_t ArpSID_utf8ToUtf16(const char* src, Char16* dst, std::size_t dstCapacity) noexcept {
    static_assert(sizeof(Char16) == 2, "destination must be a 16-bit character type");
    if (!dst || dstCapacity == 0) return 0;
    dst[0] = 0;
    if (!src) return 0;

    std::size_t out = 0;
    const std::size_t maxOut = dstCapacity - 1;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(src);

    while (*s) {
        uint32_t cp = 0xFFFDu;
        const unsigned char lead = *s;
        std::size_t len = 1;
        if (lead < 0x80u) {
            cp = lead;
        } else if ((lead & 0xE0u) == 0xC0u) {
            len = 2; cp = lead & 0x1Fu;
        } else if ((lead & 0xF0u) == 0xE0u) {
            len = 3; cp = lead & 0x0Fu;
        } else if ((lead & 0xF8u) == 0xF0u) {
            len = 4; cp = lead & 0x07u;
        } else {
            len = 1; cp = 0xFFFDu;   // stray continuation or invalid lead byte
        }

        if (len > 1) {
            std::size_t got = 1;
            for (; got < len; ++got) {
                const unsigned char c = s[got];
                if ((c & 0xC0u) != 0x80u) break;
                cp = (cp << 6) | (c & 0x3Fu);
            }
            if (got != len) {
                cp = 0xFFFDu;
                len = got;   // resynchronize at the offending byte
            } else {
                // Reject overlongs, surrogate code points and out-of-range.
                const uint32_t minByLen = (len == 2) ? 0x80u : (len == 3) ? 0x800u : 0x10000u;
                if (cp < minByLen || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
                    cp = 0xFFFDu;
            }
        }

        if (cp >= 0x10000u) {
            if (out + 2 > maxOut) break;   // never emit half a surrogate pair
            const uint32_t v = cp - 0x10000u;
            dst[out++] = static_cast<Char16>(0xD800u | (v >> 10));
            dst[out++] = static_cast<Char16>(0xDC00u | (v & 0x3FFu));
        } else {
            if (out + 1 > maxOut) break;
            dst[out++] = static_cast<Char16>(cp);
        }
        s += len;
    }
    dst[out] = 0;
    return out;
}

template <typename Char16>
inline std::size_t ArpSID_utf16ToUtf8(const Char16* src, char* dst, std::size_t dstCapacity) noexcept {
    static_assert(sizeof(Char16) == 2, "source must be a 16-bit character type");
    if (!dst || dstCapacity == 0) return 0;
    dst[0] = 0;
    if (!src) return 0;

    std::size_t out = 0;
    const std::size_t maxOut = dstCapacity - 1;

    while (*src) {
        uint32_t cp = static_cast<uint16_t>(*src);
        std::size_t advance = 1;
        if (cp >= 0xD800u && cp <= 0xDBFFu) {
            const uint32_t low = static_cast<uint16_t>(src[1]);
            if (low >= 0xDC00u && low <= 0xDFFFu) {
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                advance = 2;
            } else {
                cp = 0xFFFDu;   // unpaired high surrogate
            }
        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
            cp = 0xFFFDu;       // unpaired low surrogate
        }

        char tmp[4];
        std::size_t n = 0;
        if (cp < 0x80u) {
            tmp[n++] = static_cast<char>(cp);
        } else if (cp < 0x800u) {
            tmp[n++] = static_cast<char>(0xC0u | (cp >> 6));
            tmp[n++] = static_cast<char>(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            tmp[n++] = static_cast<char>(0xE0u | (cp >> 12));
            tmp[n++] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            tmp[n++] = static_cast<char>(0x80u | (cp & 0x3Fu));
        } else {
            tmp[n++] = static_cast<char>(0xF0u | (cp >> 18));
            tmp[n++] = static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
            tmp[n++] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
            tmp[n++] = static_cast<char>(0x80u | (cp & 0x3Fu));
        }
        if (out + n > maxOut) break;   // never emit a partial UTF-8 sequence
        for (std::size_t i = 0; i < n; ++i) dst[out++] = tmp[i];
        src += advance;
    }
    dst[out] = 0;
    return out;
}

// ─── Locale-independent numeric scanning ─────────────────────────────────────

namespace detail_presentation {

inline bool asciiEqualNoCase(char a, char b) noexcept {
    const auto lower = [](char c) noexcept -> char {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    return lower(a) == lower(b);
}

// Case-insensitive "needle appears in haystack" for short ASCII labels.
inline bool containsNoCase(const char* hay, const char* needle) noexcept {
    if (!hay || !needle || !*needle) return false;
    for (; *hay; ++hay) {
        const char* h = hay;
        const char* n = needle;
        while (*h && *n && asciiEqualNoCase(*h, *n)) { ++h; ++n; }
        if (!*n) return true;
    }
    return false;
}

inline const char* skipSpace(const char* p) noexcept {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    return p;
}

// Manual decimal parser: sign, digits, '.' or ',' fraction, optional e±exp.
// Locale-independent by construction (unlike strtod/sscanf).
inline bool parseDouble(const char* text, double& out) noexcept {
    if (!text) return false;
    const char* p = skipSpace(text);
    bool negative = false;
    if (*p == '+' || *p == '-') { negative = (*p == '-'); ++p; }

    double value = 0.0;
    bool anyDigits = false;
    while (*p >= '0' && *p <= '9') { value = value * 10.0 + (*p - '0'); anyDigits = true; ++p; }
    if (*p == '.' || *p == ',') {
        ++p;
        double scale = 0.1;
        while (*p >= '0' && *p <= '9') { value += (*p - '0') * scale; scale *= 0.1; anyDigits = true; ++p; }
    }
    if (!anyDigits) return false;

    if (*p == 'e' || *p == 'E') {
        const char* mark = p;
        ++p;
        bool expNegative = false;
        if (*p == '+' || *p == '-') { expNegative = (*p == '-'); ++p; }
        int expVal = 0;
        bool expDigits = false;
        while (*p >= '0' && *p <= '9' && expVal < 1000) { expVal = expVal * 10 + (*p - '0'); expDigits = true; ++p; }
        if (expDigits) {
            value *= std::pow(10.0, expNegative ? -expVal : expVal);
        } else {
            p = mark;   // trailing 'e' belongs to a suffix, not the number
        }
    }
    out = negative ? -value : value;
    return std::isfinite(out);
}

inline bool copyLabel(const char* label, char* dst, std::size_t dstSize) noexcept {
    if (!dst || dstSize == 0) return false;
    const std::size_t n = label ? std::strlen(label) : 0;
    if (n + 1 > dstSize) { dst[0] = 0; return false; }
    std::memcpy(dst, label, n + 1);
    return true;
}

} // namespace detail_presentation

// ─── Unit descriptor ─────────────────────────────────────────────────────────

inline ParameterUnitDescriptor sidParameterUnitDescriptor(int paramId) noexcept {
    if (!isValidParamIndex(paramId)) return {};
    if (isBooleanNormalizedParam(paramId)) return {SidParameterUnit::Boolean, ""};

    switch (static_cast<ParamID>(paramId)) {
        case kParamPortamentoTime: return {SidParameterUnit::Seconds, "s"};
        case kParamMasterTune: return {SidParameterUnit::Cents, "ct"};
        case kParamLFORate: case kParamLFO2Rate: case kParamLFO3Rate: case kParamLFO4Rate:
            return {SidParameterUnit::Hertz, "Hz"};
        case kParamSeqTempo: return {SidParameterUnit::Bpm, "bpm"};
        case kParamSeqLength: return {SidParameterUnit::Steps, "steps"};
        case kParamLimiterAttack: case kParamLimiterRelease:
            return {SidParameterUnit::Milliseconds, "ms"};
        case kParamForensicTemp: return {SidParameterUnit::Celsius, "\xC2\xB0""C"};
        case kParamForensicSupply: return {SidParameterUnit::Volts, "V"};
        case kParamForensicChipSeed: return {SidParameterUnit::Seed, ""};
        case kParamForensicRevision: case kParamSidChipRevision:
        case kParamSidOversamplingFactor: case kParamHiFiOversampling:
        case kParamDrSidMachineModel:
            return {SidParameterUnit::IndexedLabel, ""};
        // Floor-binned legacy selectors: a rounded decimal display cannot
        // roundtrip through their half-open floor bins (e.g. "0.3333" floors
        // to bin 0, not bin 1), so they present as an integer bin index.
        case kParamVCO1Waveform: case kParamVCO2Waveform: case kParamVCO3Waveform:
        case kParamFilterMode: case kParamArpOctaves:
            return {SidParameterUnit::Index, ""};
        default: break;
    }
    if (sidParameterIsModSourceParam(paramId)) return {SidParameterUnit::IndexedLabel, ""};

    const char* u = kParamInfos[(size_t)paramId].unit;
    if (u && (std::strcmp(u, "%") == 0 || std::strcmp(u, "pct") == 0))
        return {SidParameterUnit::Percent, "%"};
    if (u && std::strcmp(u, "byte") == 0)
        return {SidParameterUnit::Byte, ""};
    return {SidParameterUnit::Normalized, ""};
}

// ─── HiFi oversampling presentation (law: sidHiFiConfigFromNormalized) ──────

inline int sidHiFiOversamplingFactorFromNormalized(float norm) noexcept {
    const int oi = std::clamp((int)std::lround(ArpSID_sanitize01(norm) * 2.0f), 0, 2);
    return (oi == 0) ? 4 : (oi == 1 ? 8 : 16);
}

inline const char* sidHiFiOversamplingLabel(float norm) noexcept {
    switch (sidHiFiOversamplingFactorFromNormalized(norm)) {
        case 8: return "8x";
        case 16: return "16x";
        default: return "4x";
    }
}

// ─── SidParameterPresentation ────────────────────────────────────────────────

struct SidParameterPresentation {
    static ParameterUnitDescriptor unit(int paramId) noexcept {
        return sidParameterUnitDescriptor(paramId);
    }

    // Renders the semantic display text for a sanitized normalized value.
    // Returns false (and an empty string) only on invalid arguments or an
    // undersized destination.
    static bool formatNormalized(int paramId, float normalized,
                                 char* dst, std::size_t dstSize) noexcept {
        using namespace detail_presentation;
        if (!dst || dstSize == 0) return false;
        dst[0] = 0;
        if (!isValidParamIndex(paramId)) return false;

        const float v = sanitizeNormalizedParamValue(paramId, normalized,
                                                     defaultNormalizedParamValue(paramId));
        const ParameterUnitDescriptor d = sidParameterUnitDescriptor(paramId);
        char buf[64] = {};

        switch (d.unit) {
            case SidParameterUnit::Boolean:
                return copyLabel(v > 0.5f ? "ON" : "OFF", dst, dstSize);
            case SidParameterUnit::Seconds:
                std::snprintf(buf, sizeof(buf), "%.3f s", (double)ArpSID_normToPortamentoSeconds(v));
                break;
            case SidParameterUnit::Milliseconds:
                std::snprintf(buf, sizeof(buf), "%.2f ms",
                              (double)(paramId == (int)kParamLimiterAttack
                                           ? ArpSID_normToLimiterAttackMs(v)
                                           : ArpSID_normToLimiterReleaseMs(v)));
                break;
            case SidParameterUnit::Hertz:
                std::snprintf(buf, sizeof(buf), "%.4g Hz", (double)ArpSID_normToLfoRateHz(v));
                break;
            case SidParameterUnit::Cents:
                std::snprintf(buf, sizeof(buf), "%+.1f ct", (double)ArpSID_normToDetuneCents(v));
                break;
            case SidParameterUnit::Bpm:
                std::snprintf(buf, sizeof(buf), "%.1f bpm", (double)ArpSID_normToSeqTempoBpm(v));
                break;
            case SidParameterUnit::Steps:
                std::snprintf(buf, sizeof(buf), "%d steps", ArpSID_normToSeqSteps(v));
                break;
            case SidParameterUnit::Percent:
                std::snprintf(buf, sizeof(buf), "%.1f %%", (double)(v * 100.0f));
                break;
            case SidParameterUnit::Celsius:
                std::snprintf(buf, sizeof(buf), "%.1f \xC2\xB0""C",
                              (double)sidForensicTemperatureFromNormalized(v));
                break;
            case SidParameterUnit::Volts:
                std::snprintf(buf, sizeof(buf), "%.2f V",
                              (double)sidForensicSupplyFromNormalized(v));
                break;
            case SidParameterUnit::Byte:
                std::snprintf(buf, sizeof(buf), "%u", (unsigned)ArpSID_sanitizeNormToByte(v));
                break;
            case SidParameterUnit::Index:
                std::snprintf(buf, sizeof(buf), "%d", floorBinIndex_(paramId, v));
                break;
            case SidParameterUnit::Seed:
                std::snprintf(buf, sizeof(buf), "%u", sidForensicChipSeedFromNormalized(v));
                break;
            case SidParameterUnit::IndexedLabel:
                return copyLabel(indexedLabelForNormalized_(paramId, v), dst, dstSize);
            case SidParameterUnit::Normalized:
            default:
                std::snprintf(buf, sizeof(buf), "%.4f", (double)v);
                break;
        }
        return copyLabel(buf, dst, dstSize);
    }

    // Parses host-entered text back to the normalized domain by inverting the
    // display law of this specific parameter. Accepts optional unit suffixes,
    // case-insensitive labels, and both '.' and ',' decimal separators.
    // On failure returns false and leaves normalizedOut untouched.
    static bool parseToNormalized(int paramId, const char* text,
                                  float& normalizedOut) noexcept {
        using namespace detail_presentation;
        if (!text || !isValidParamIndex(paramId)) return false;

        const ParameterUnitDescriptor d = sidParameterUnitDescriptor(paramId);
        float parsed = 0.0f;

        switch (d.unit) {
            case SidParameterUnit::Boolean: {
                if (containsNoCase(text, "on") || containsNoCase(text, "true") ||
                    containsNoCase(text, "yes")) { parsed = 1.0f; break; }
                if (containsNoCase(text, "off") || containsNoCase(text, "false") ||
                    containsNoCase(text, "no")) { parsed = 0.0f; break; }
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = raw > 0.5 ? 1.0f : 0.0f;
                break;
            }
            case SidParameterUnit::Seconds: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                if (containsNoCase(text, "ms")) raw *= 0.001;   // allow "250 ms"
                parsed = ArpSID_portamentoSecondsToNorm((float)raw);
                break;
            }
            case SidParameterUnit::Milliseconds: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                if (!containsNoCase(text, "ms") && containsNoCase(text, "s"))
                    raw *= 1000.0;   // allow "0.36 s"
                parsed = paramId == (int)kParamLimiterAttack
                             ? ArpSID_limiterAttackMsToNorm((float)raw)
                             : ArpSID_limiterReleaseMsToNorm((float)raw);
                break;
            }
            case SidParameterUnit::Hertz: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = ArpSID_lfoRateHzToNorm((float)raw);
                break;
            }
            case SidParameterUnit::Cents: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = ArpSID_detuneCentsToNorm((float)raw);
                break;
            }
            case SidParameterUnit::Bpm: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = ArpSID_seqTempoBpmToNorm((float)raw);
                break;
            }
            case SidParameterUnit::Steps: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = ArpSID_seqStepsToNorm((int)std::llround(raw));
                break;
            }
            case SidParameterUnit::Percent: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = (float)(raw / 100.0);
                break;
            }
            case SidParameterUnit::Celsius: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = sidForensicTemperatureToNormalized((float)raw);
                break;
            }
            case SidParameterUnit::Volts: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = sidForensicSupplyToNormalized((float)raw);
                break;
            }
            case SidParameterUnit::Byte: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = (float)(std::clamp(raw, 0.0, 255.0) / 255.0);
                break;
            }
            case SidParameterUnit::Seed: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                const uint32_t seed = raw <= 0.0 ? 0u
                    : (raw >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)std::llround(raw));
                parsed = sidForensicChipSeedToNormalized(seed);
                break;
            }
            case SidParameterUnit::Index: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = floorBinIndexToNorm_(paramId, (int)std::llround(raw));
                break;
            }
            case SidParameterUnit::IndexedLabel: {
                if (!parseIndexedLabel_(paramId, text, parsed)) return false;
                break;
            }
            case SidParameterUnit::Normalized:
            default: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsed = (float)std::clamp(raw, 0.0, 1.0);
                break;
            }
        }

        normalizedOut = sanitizeNormalizedParamValue(paramId, parsed,
                                                     defaultNormalizedParamValue(paramId));
        return true;
    }

private:
    // Floor-bin decode/encode mirrors sanitizeNormalizedParamValue's
    // half-open floor law for these legacy selectors: waveform/filter decode
    // floor(v × 8) into indices 0..7; arp octaves decode floor(v × 3) into
    // indices 0..3. Encoding places the index on the canonical grid idx/max.
    static int floorBinIndex_(int paramId, float v) noexcept {
        const bool octaves = paramId == (int)kParamArpOctaves;
        const float mul = octaves ? 3.0f : 8.0f;
        const int maxIdx = octaves ? 3 : 7;
        return std::clamp((int)std::floor(ArpSID_sanitize01(v) * mul), 0, maxIdx);
    }

    static float floorBinIndexToNorm_(int paramId, int idx) noexcept {
        const int maxIdx = paramId == (int)kParamArpOctaves ? 3 : 7;
        return (float)std::clamp(idx, 0, maxIdx) / (float)maxIdx;
    }

    static const char* indexedLabelForNormalized_(int paramId, float v) noexcept {
        switch (static_cast<ParamID>(paramId)) {
            case kParamForensicRevision:
                return sidForensicRevisionLabel(sidForensicRevisionFromNormalized(v));
            case kParamSidChipRevision:
                return sidChipRevisionSelectorLabel(v);
            case kParamSidOversamplingFactor:
                return sidOversamplingLabel(v);
            case kParamHiFiOversampling:
                return sidHiFiOversamplingLabel(v);
            case kParamDrSidMachineModel:
                return drSidMachineModelDisplayName(v);
            default: break;
        }
        if (sidParameterIsModSourceParam(paramId)) {
            const int maxIdx = kSidModSourceCount - 1;
            const int idx = std::clamp((int)std::lround(ArpSID_sanitize01(v) * (float)maxIdx), 0, maxIdx);
            return sidModSourceLabelFromIndex(idx);
        }
        return "";
    }

    static bool parseIndexedLabel_(int paramId, const char* text, float& parsedOut) noexcept {
        using namespace detail_presentation;
        switch (static_cast<ParamID>(paramId)) {
            case kParamForensicRevision: {
                uint8_t revision = 0u;
                if (containsNoCase(text, "8580") || containsNoCase(text, "r5")) revision = 5u;
                else if (containsNoCase(text, "r4")) revision = 4u;
                else if (containsNoCase(text, "r3")) revision = 3u;
                else if (containsNoCase(text, "r2")) revision = 2u;
                else {
                    double raw = 0.0;
                    if (!parseDouble(text, raw)) return false;
                    revision = (uint8_t)std::clamp<long long>(std::llround(raw), 2ll, 5ll);
                }
                parsedOut = sidForensicRevisionToNormalized(revision);
                return true;
            }
            case kParamSidChipRevision: {
                int idx = -1;
                if (containsNoCase(text, "8580") || containsNoCase(text, "r5")) idx = 3;
                else if (containsNoCase(text, "r4")) idx = 2;
                else if (containsNoCase(text, "r3")) idx = 1;
                else if (containsNoCase(text, "r2")) idx = 0;
                else {
                    double raw = 0.0;
                    if (!parseDouble(text, raw)) return false;
                    idx = std::clamp((int)std::llround(raw), 0, 3);
                }
                parsedOut = sidChipRevisionIndexToNormalized(idx);
                return true;
            }
            case kParamSidOversamplingFactor: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                const long long factor = std::llround(raw);
                const int idx = factor >= 8 ? 3 : (factor >= 4 ? 2 : (factor >= 2 ? 1 : 0));
                parsedOut = (float)idx / 3.0f;
                return true;
            }
            case kParamHiFiOversampling: {
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                const long long factor = std::llround(raw);
                const int idx = factor >= 16 ? 2 : (factor >= 8 ? 1 : 0);
                parsedOut = (float)idx / 2.0f;
                return true;
            }
            case kParamDrSidMachineModel: {
                if (containsNoCase(text, "x0x")) { parsedOut = drSidMachineModelIndexToNormalized(1); return true; }
                if (containsNoCase(text, "sid")) { parsedOut = drSidMachineModelIndexToNormalized(0); return true; }
                double raw = 0.0;
                if (!parseDouble(text, raw)) return false;
                parsedOut = drSidMachineModelIndexToNormalized((int)std::llround(raw));
                return true;
            }
            default: break;
        }
        if (sidParameterIsModSourceParam(paramId)) {
            const int maxIdx = kSidModSourceCount - 1;
            // Longest labels first so "LFO1" is not shadowed by a shorter hit.
            for (int idx = maxIdx; idx >= 0; --idx) {
                if (containsNoCase(text, sidModSourceLabelFromIndex(idx))) {
                    parsedOut = (float)idx / (float)maxIdx;
                    return true;
                }
            }
            double raw = 0.0;
            if (!parseDouble(text, raw)) return false;
            const int idx = std::clamp((int)std::llround(raw), 0, maxIdx);
            parsedOut = (float)idx / (float)maxIdx;
            return true;
        }
        return false;
    }
};

} // namespace ArpSID
