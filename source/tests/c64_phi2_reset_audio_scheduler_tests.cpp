// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_phi2_audio_scheduler.h"
#include "arpsid/core/c64_phi2_machine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    using namespace ArpSID::C64;

    {
        C64Phi2Machine m;
        m.powerOn();
        m.memory().pokeKernalRom(kVectorResetLo, 0x00u);
        m.memory().pokeKernalRom(kVectorResetHi, 0xC0u);
        m.resetToVector();
        m.runPhi2(8);
        require(m.cpu().pc() == 0xC000u, "reset vector fetched through visible memory map");
        require(m.diagnostics().resetEntries == 1u, "reset entry diagnostic increments");
    }

    {
        Phi2AudioScheduler sched;
        sched.configure(kC64PalCpuHzExact, 48000.0);
        bool saw20 = false;
        bool saw21 = false;
        uint64_t total = 0;
        for (int i = 0; i < 1000; ++i) {
            const uint32_t c = sched.cyclesForNextSample();
            saw20 = saw20 || c == 20u;
            saw21 = saw21 || c == 21u;
            total += c;
        }
        require(saw20 && saw21, "PHI2 scheduler produces 20/21 cycle alternation at PAL/48k");
        const double expected = kC64PalCpuHzExact * 1000.0 / 48000.0;
        require(std::fabs(static_cast<double>(total) - std::floor(expected)) <= 1.0,
                "PHI2 scheduler preserves fractional debt over many samples");
    }

    {
        Phi2AudioScheduler sched;
        sched.configure(std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::quiet_NaN());
        const uint32_t cycles = sched.cyclesForNextSample();
        require(cycles > 0u && cycles < 1000u,
                "PHI2 scheduler replaces non-finite configuration with bounded defaults");
        sched.phi2Debt = std::numeric_limits<double>::infinity();
        require(sched.cyclesForNextSample() < 1000u,
                "PHI2 scheduler recovers from non-finite accumulated debt");
    }

    {
        SidScalarIntervalRenderer sid;
        sid.beginHostSample(100, 10);
        sid.writeSidRegisterPhi2(103, 0x00u, 0xFFu, false);
        sid.writeSidRegisterPhi2(107, 0x00u, 0x00u, false);
        const float sample = sid.finishHostSample(110);
        require(std::fabs(sample - 0.4f) < 0.0001f, "SID split sample is interval weighted");
        require(sid.maxWritesPerSample() == 2u, "interval renderer tracks max writes per sample");
    }

    std::cout << "c64_phi2_reset_audio_scheduler_tests PASS\n";
    return 0;
}
