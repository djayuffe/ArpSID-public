// SPDX-License-Identifier: BSD-3-Clause
// digi_d418_sid_volume_dac_v699_tests.cpp
// Verifies that authentic DIGI $D418 writes are audible through the SID
// register engine's explicit volume-DAC model even when no oscillator voice
// is running. This guards the important authenticity rule:
// nibble -> $D418 register write -> SID volume DAC output, not direct float mix.

#include "arpsid/engines/sid_register_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

void requireTrue(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

float renderPeakAfterD418Writes(bool enableDac) {
    ArpSID::SidRegisterEngine sid;
    sid.prepare(44100.0);
    sid.setD418VolumeDacEmulation(enableDac);
    sid.writeSystemByte(0x02u); // 8580/PAL default

    float l[128]{};
    float r[128]{};
    float peak = 0.0f;

    for (int i = 0; i < 128; ++i) {
        // Alternate low-nibble volume data; high nibble zero keeps this a pure
        // DIGI volume-DAC exercise with no filter/voice bits involved.
        sid.write(0x18u, static_cast<std::uint8_t>((i & 1) ? 0x0Fu : 0x00u));
        sid.renderBlock(&l[i], &r[i], 1);
        peak = std::max(peak, std::fabs(l[i]));
        peak = std::max(peak, std::fabs(r[i]));
    }
    return peak;
}

void volumeDacDisabledDoesNotInventDigiAudio() {
    const float peak = renderPeakAfterD418Writes(false);
    requireTrue(peak < 1.0e-6f, "disabled volume-DAC emulation must remain silent with no oscillator voices");
}

void volumeDacEnabledMakesD418Audible() {
    const float peak = renderPeakAfterD418Writes(true);
    requireTrue(peak > 1.0e-5f, "enabled volume-DAC emulation must make $D418 nibble writes audible");
}

} // namespace

int main() {
    volumeDacDisabledDoesNotInventDigiAudio();
    volumeDacEnabledMakesD418Audible();
    return 0;
}
