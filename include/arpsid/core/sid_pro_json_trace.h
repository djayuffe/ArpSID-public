// Copyright (C) 2024-2026 Ulf Bertilsson
#pragma once
// SID-PRO / bit-correct JSON SID trace importer.
// Header-only, allocation-bounded once vectors are reserved, non-render import path.
// Ported from bit-correct-json-sid---pro specification into ArpSID core.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

namespace ArpSID {

enum class SidProJsonResult : uint8_t {
    OK = 0,
    EmptyInput,
    BadJson,
    MissingMetadata,
    MissingOptimizedWrites,
    MissingCycleBlob,
    MissingDataBlob,
    BadBase64,
    BadCycleBlobSize,
    BadWriteDataSize,
    CycleWriteCountMismatch,
    UnsupportedSidCount,
    BadRegisterIndex,
    OutOfOrderCycles,
    TooManyWrites,
};

static inline const char* sidProJsonResultName(SidProJsonResult r) noexcept {
    switch (r) {
        case SidProJsonResult::OK: return "OK";
        case SidProJsonResult::EmptyInput: return "EmptyInput";
        case SidProJsonResult::BadJson: return "BadJson";
        case SidProJsonResult::MissingMetadata: return "MissingMetadata";
        case SidProJsonResult::MissingOptimizedWrites: return "MissingOptimizedWrites";
        case SidProJsonResult::MissingCycleBlob: return "MissingCycleBlob";
        case SidProJsonResult::MissingDataBlob: return "MissingDataBlob";
        case SidProJsonResult::BadBase64: return "BadBase64";
        case SidProJsonResult::BadCycleBlobSize: return "BadCycleBlobSize";
        case SidProJsonResult::BadWriteDataSize: return "BadWriteDataSize";
        case SidProJsonResult::CycleWriteCountMismatch: return "CycleWriteCountMismatch";
        case SidProJsonResult::UnsupportedSidCount: return "UnsupportedSidCount";
        case SidProJsonResult::BadRegisterIndex: return "BadRegisterIndex";
        case SidProJsonResult::OutOfOrderCycles: return "OutOfOrderCycles";
        case SidProJsonResult::TooManyWrites: return "TooManyWrites";
    }
    return "Unknown";
}

struct SidProJsonMetadata {
    std::string title;
    std::string author;
    uint32_t clockFreq = 985248u;
    bool isNtsc = false;
    uint8_t sidCount = 1;
    uint32_t frameCount = 0;
    double totalDuration = 0.0;
    double detectedRefreshRate = 0.0;
    std::array<uint16_t, 3> sidAddresses{{0xD400u, 0u, 0u}};
};

struct SidProJsonWrite {
    uint64_t cycle = 0;
    uint8_t chip = 0;
    uint8_t reg = 0;
    uint8_t value = 0;
};

struct SidProJsonVoiceTelemetry {
    uint32_t accumulator24 = 0;
    uint32_t lfsr23 = 0x7FFFFFu;
    uint8_t envelope = 0;
    uint8_t envelopeState = 3;
    uint8_t control = 0;
    double frequencyHz = 0.0;
};

struct SidProJsonTrace {
    SidProJsonMetadata metadata{};
    std::vector<SidProJsonWrite> writes;
    uint64_t lastCycle = 0;
    uint32_t rawCycleCount = 0;
    uint32_t rawDataBytes = 0;
};

namespace detail {

static inline const char* skipWs(const char* p, const char* e) noexcept {
    while (p < e && std::isspace(static_cast<unsigned char>(*p))) ++p;
    return p;
}

static inline const char* findKey(const char* b, const char* e, const char* key) noexcept {
    const size_t n = std::strlen(key);
    if (n == 0) return nullptr;
    for (const char* p = b; p + n <= e; ++p) {
        if (*p == '"' && static_cast<size_t>(e - p) >= n + 2u && std::memcmp(p + 1, key, n) == 0 && p[1 + n] == '"') {
            const char* q = skipWs(p + n + 2, e);
            if (q < e && *q == ':') return q + 1;
        }
    }
    return nullptr;
}

static inline bool jsonExtractString(const char* b, const char* e, const char* key, std::string& out) {
    const char* p = findKey(b, e, key);
    if (!p) return false;
    p = skipWs(p, e);
    if (p >= e || *p != '"') return false;
    ++p;
    out.clear();
    while (p < e) {
        const char c = *p++;
        if (c == '"') return true;
        if (c == '\\') {
            if (p >= e) return false;
            const char esc = *p++;
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                    // Keep importer small and deterministic: consume \uXXXX and store '?'.
                    if (e - p < 4) return false;
                    p += 4;
                    out.push_back('?');
                    break;
                default: return false;
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

static inline bool jsonExtractUInt(const char* b, const char* e, const char* key, uint32_t& out) noexcept {
    const char* p = findKey(b, e, key);
    if (!p) return false;
    p = skipWs(p, e);
    char* endp = nullptr;
    unsigned long v = std::strtoul(p, &endp, 10);
    if (endp == p || endp > e) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

static inline bool jsonExtractBool(const char* b, const char* e, const char* key, bool& out) noexcept {
    const char* p = findKey(b, e, key);
    if (!p) return false;
    p = skipWs(p, e);
    if (e - p >= 4 && std::memcmp(p, "true", 4) == 0) { out = true; return true; }
    if (e - p >= 5 && std::memcmp(p, "false", 5) == 0) { out = false; return true; }
    return false;
}

static inline bool jsonExtractDouble(const char* b, const char* e, const char* key, double& out) noexcept {
    const char* p = findKey(b, e, key);
    if (!p) return false;
    p = skipWs(p, e);
    char* endp = nullptr;
    const double v = std::strtod(p, &endp);
    if (endp == p || endp > e) return false;
    out = v;
    return true;
}

static inline int b64Val(unsigned char c) noexcept {
    if (c >= 'A' && c <= 'Z') return int(c - 'A');
    if (c >= 'a' && c <= 'z') return int(c - 'a') + 26;
    if (c >= '0' && c <= '9') return int(c - '0') + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    if (std::isspace(c)) return -3;
    return -1;
}

static inline bool base64Decode(const std::string& s, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    bool seenPad = false;
    for (unsigned char c : s) {
        const int v = b64Val(c);
        if (v == -3) continue;
        if (v == -2) { seenPad = true; continue; }
        if (v < 0 || seenPad) return false;
        acc = (acc << 6u) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((acc >> static_cast<uint32_t>(bits)) & 0xFFu));
        }
    }
    return true;
}

static inline uint32_t le32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

} // namespace detail

class SidProJsonParser {
public:
    static constexpr uint32_t kDefaultMaxWrites = 4u * 1024u * 1024u;

    static SidProJsonResult parse(const char* json, size_t len, SidProJsonTrace& out,
                                  uint32_t maxWrites = kDefaultMaxWrites) {
        out = {};
        if (!json || len == 0) return SidProJsonResult::EmptyInput;
        const char* b = json;
        const char* e = json + len;
        if (!detail::findKey(b, e, "metadata")) return SidProJsonResult::MissingMetadata;
        if (!detail::findKey(b, e, "optimizedWrites")) return SidProJsonResult::MissingOptimizedWrites;

        (void)detail::jsonExtractString(b, e, "title", out.metadata.title);
        (void)detail::jsonExtractString(b, e, "author", out.metadata.author);
        uint32_t clock = 0;
        if (detail::jsonExtractUInt(b, e, "clockFreq", clock) && clock > 0) out.metadata.clockFreq = clock;
        bool ntsc = false;
        if (detail::jsonExtractBool(b, e, "isNtsc", ntsc)) out.metadata.isNtsc = ntsc;
        uint32_t frameCount = 0;
        if (detail::jsonExtractUInt(b, e, "frameCount", frameCount)) out.metadata.frameCount = frameCount;
        double duration = 0.0;
        if (detail::jsonExtractDouble(b, e, "totalDuration", duration) && std::isfinite(duration) && duration >= 0.0) out.metadata.totalDuration = duration;
        double refresh = 0.0;
        if (detail::jsonExtractDouble(b, e, "detectedRefreshRate", refresh) && std::isfinite(refresh) && refresh >= 0.0) out.metadata.detectedRefreshRate = refresh;
        uint32_t sidCount = 1;
        if (detail::jsonExtractUInt(b, e, "sidCount", sidCount)) {
            if (sidCount < 1u || sidCount > 3u) return SidProJsonResult::UnsupportedSidCount;
            out.metadata.sidCount = static_cast<uint8_t>(sidCount);
        }
        // Default C64 SID base addresses for 1/2/3-SID SID-PRO traces.
        // Explicit sidAddresses arrays are deliberately not parsed by this
        // small non-allocating scanner yet; packed chip index remains the
        // authoritative routing field.
        out.metadata.sidAddresses[0] = 0xD400u;
        out.metadata.sidAddresses[1] = out.metadata.sidCount >= 2u ? 0xD420u : 0u;
        out.metadata.sidAddresses[2] = out.metadata.sidCount >= 3u ? 0xD440u : 0u;

        std::string cycles64;
        std::string data64;
        if (!detail::jsonExtractString(b, e, "cycles_u32le", cycles64)) return SidProJsonResult::MissingCycleBlob;
        if (!detail::jsonExtractString(b, e, "data_u8", data64)) return SidProJsonResult::MissingDataBlob;

        std::vector<uint8_t> cycleBytes;
        std::vector<uint8_t> dataBytes;
        if (!detail::base64Decode(cycles64, cycleBytes)) return SidProJsonResult::BadBase64;
        if (!detail::base64Decode(data64, dataBytes)) return SidProJsonResult::BadBase64;
        if ((cycleBytes.size() % 4u) != 0u) return SidProJsonResult::BadCycleBlobSize;
        const size_t n = cycleBytes.size() / 4u;
        if (n > maxWrites) return SidProJsonResult::TooManyWrites;
        if (dataBytes.size() != n * 2u && dataBytes.size() != n * 3u) return SidProJsonResult::BadWriteDataSize;

        out.rawCycleCount = static_cast<uint32_t>(n);
        out.rawDataBytes = static_cast<uint32_t>(dataBytes.size());
        out.writes.reserve(n);
        uint64_t prev = 0;
        for (size_t i = 0; i < n; ++i) {
            const uint32_t cyc = detail::le32(cycleBytes.data() + i * 4u);
            if (i != 0 && cyc < prev) return SidProJsonResult::OutOfOrderCycles;
            prev = cyc;

            SidProJsonWrite w{};
            w.cycle = cyc;
            if (dataBytes.size() == n * 2u) {
                const uint8_t packedReg = dataBytes[i * 2u + 0u];
                w.value = dataBytes[i * 2u + 1u];
                w.chip = static_cast<uint8_t>(packedReg / 32u);
                w.reg = static_cast<uint8_t>(packedReg & 31u);
            } else {
                w.chip = dataBytes[i * 3u + 0u];
                w.reg = dataBytes[i * 3u + 1u];
                w.value = dataBytes[i * 3u + 2u];
            }
            if (w.chip >= out.metadata.sidCount || w.chip >= 3u) return SidProJsonResult::BadRegisterIndex;
            if (w.reg >= 32u) return SidProJsonResult::BadRegisterIndex;
            out.writes.push_back(w);
        }
        out.lastCycle = n ? out.writes.back().cycle : 0;
        return SidProJsonResult::OK;
    }

    static SidProJsonResult parse(const std::string& json, SidProJsonTrace& out,
                                  uint32_t maxWrites = kDefaultMaxWrites) {
        return parse(json.data(), json.size(), out, maxWrites);
    }
};

class SidProJsonTracePlayer {
public:
    void reset() noexcept { cursor_ = 0; applied_ = 0; }
    size_t cursor() const noexcept { return cursor_; }
    uint64_t appliedWrites() const noexcept { return applied_; }

    template <class Sink>
    void applyUntil(const SidProJsonTrace& trace, uint64_t cycleInclusive, Sink&& sink) {
        while (cursor_ < trace.writes.size() && trace.writes[cursor_].cycle <= cycleInclusive) {
            const SidProJsonWrite& w = trace.writes[cursor_++];
            sink(w.chip, w.reg, w.value, w.cycle);
            ++applied_;
        }
    }

private:
    size_t cursor_ = 0;
    uint64_t applied_ = 0;
};

} // namespace ArpSID
