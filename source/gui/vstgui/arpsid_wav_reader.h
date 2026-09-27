// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — minimal WAV reader for DIGI sample import (cross-platform editor).
//
// RIFF/WAVE, PCM 8/16/24/32-bit integer and 32/64-bit float, including
// WAVE_FORMAT_EXTENSIBLE; all channels are mixed down to mono float.
#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace ArpSID::Editor {

struct WavData {
    std::vector<float> samples; // mono
    double sampleRate = 44100.0;
};

inline bool parseWavMono(const std::vector<std::uint8_t>& b, WavData& out, std::string& err) {
    auto u16 = [&](std::size_t p) { return static_cast<std::uint32_t>(b[p] | (b[p + 1] << 8)); };
    auto u32 = [&](std::size_t p) {
        return static_cast<std::uint32_t>(b[p]) | (static_cast<std::uint32_t>(b[p + 1]) << 8) |
               (static_cast<std::uint32_t>(b[p + 2]) << 16) | (static_cast<std::uint32_t>(b[p + 3]) << 24);
    };
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
        err = "not a RIFF/WAVE file";
        return false;
    }
    std::uint32_t format = 0, channels = 0, rate = 0, bits = 0;
    std::size_t dataPos = 0, dataLen = 0;
    for (std::size_t p = 12; p + 8 <= b.size();) {
        const std::uint32_t len = u32(p + 4);
        const std::size_t body = p + 8;
        if (body + len > b.size() + 1) break;
        if (std::memcmp(b.data() + p, "fmt ", 4) == 0 && len >= 16) {
            format = u16(body);
            channels = u16(body + 2);
            rate = u32(body + 4);
            bits = u16(body + 14);
            if (format == 0xFFFE && len >= 26) format = u16(body + 24); // extensible: sub-format tag
        } else if (std::memcmp(b.data() + p, "data", 4) == 0) {
            dataPos = body;
            dataLen = std::min<std::size_t>(len, b.size() - body);
        }
        p = body + len + (len & 1u);
    }
    if (!dataPos || !channels || !rate) {
        err = "missing fmt or data chunk";
        return false;
    }
    const bool isFloat = (format == 3);
    if (!(format == 1 || isFloat) || !(bits == 8 || bits == 16 || bits == 24 || bits == 32 || (isFloat && bits == 64))) {
        err = "unsupported WAV encoding";
        return false;
    }
    const std::size_t frameBytes = (bits / 8) * channels;
    const std::size_t frames = dataLen / frameBytes;
    out.samples.assign(frames, 0.f);
    out.sampleRate = rate;
    for (std::size_t f = 0; f < frames; ++f) {
        double sum = 0.0;
        for (std::uint32_t c = 0; c < channels; ++c) {
            const std::size_t p = dataPos + f * frameBytes + c * (bits / 8);
            double v = 0.0;
            if (isFloat && bits == 32) {
                float x;
                std::memcpy(&x, &b[p], 4);
                v = x;
            } else if (isFloat) {
                double x;
                std::memcpy(&x, &b[p], 8);
                v = x;
            } else if (bits == 8) {
                v = (static_cast<int>(b[p]) - 128) / 128.0;
            } else if (bits == 16) {
                v = static_cast<std::int16_t>(u16(p)) / 32768.0;
            } else if (bits == 24) {
                std::int32_t x = static_cast<std::int32_t>(b[p] | (b[p + 1] << 8) | (b[p + 2] << 16));
                if (x & 0x800000) x -= 0x1000000;
                v = x / 8388608.0;
            } else {
                v = static_cast<std::int32_t>(u32(p)) / 2147483648.0;
            }
            sum += v;
        }
        out.samples[f] = static_cast<float>(sum / channels);
    }
    if (out.samples.empty()) {
        err = "no audio frames";
        return false;
    }
    return true;
}

inline bool readWavMono(const std::string& path, WavData& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot open file";
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() > 256u * 1024u * 1024u) {
        err = "file too large";
        return false;
    }
    return parseWavMono(bytes, out, err);
}

} // namespace ArpSID::Editor
