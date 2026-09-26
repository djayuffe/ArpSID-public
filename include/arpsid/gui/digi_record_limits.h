// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// digi_record_limits.h - central DIGI REC/import/sample length limits.

#ifndef ARPSID_GUI_DIGI_RECORD_LIMITS_H
#define ARPSID_GUI_DIGI_RECORD_LIMITS_H

#include <cstdint>

namespace ArpSID {
namespace GUI {

// Host-rate capture/import window. This is deliberately larger than the
// stored C64 $D418 payload because capture may arrive at 44.1/48/96 kHz before
// being decimated to the canonical 8 kHz DIGI stream.
inline constexpr std::uint32_t kDigiRecordCaptureMaxFrames = 1048576u;

// Persisted user sample payload. It remains uint16_t-compatible for schema v1
// and stays under eight seconds at the canonical 8 kHz DIGI write rate.
inline constexpr std::uint16_t kDigiUserSampleMaxFramesLong = 60000u;

inline constexpr std::uint32_t kDigiUserSampleCanonicalRateHz = 8000u;
inline constexpr double kDigiUserSampleMaxSeconds =
    static_cast<double>(kDigiUserSampleMaxFramesLong) /
    static_cast<double>(kDigiUserSampleCanonicalRateHz);
inline constexpr std::uint32_t kDigiUserSampleClipPinnedBytes =
    4u + 4u + 4u + 2u + 2u + 32u + kDigiUserSampleMaxFramesLong;
inline constexpr std::uint32_t kDigiSampleBankBlobPinnedBytes =
    4u + 1u + 3u + (8u * kDigiUserSampleClipPinnedBytes);

static_assert(kDigiUserSampleMaxFramesLong < 65535u,
              "Schema v1 uses uint16_t frameCount; keep below 65535");
static_assert(kDigiUserSampleMaxSeconds < 8.0,
              "User requested less than eight seconds for schema v1");

} // namespace GUI
} // namespace ArpSID

#endif // ARPSID_GUI_DIGI_RECORD_LIMITS_H
