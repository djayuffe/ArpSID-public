// Copyright (C) 2024-2026 Ulf Bertilsson
// factory_patch_apply_roundtrip_v850_tests.cpp
//
// Deeper factory-patch load/apply guard. When a factory slot is selected the
// runtime builds a SidStateRootV1 (makeFactoryPatchStateRootForSlot) and applies
// it to the backends. This test proves that EVERY audio-authority parameter the
// factory authors for a slot survives into that applied state root — i.e. nothing
// the patch sets is silently dropped between load and apply, for all 180 slots.
//
// Program/bank identity params are canonicalised by the root builder and
// runtime-only params are reset by design, so those are excluded.

#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "../parameter_ids.h"
#include "../factory_patch_params.h"

#include <array>
#include <cmath>
#include <cstdio>

using namespace ArpSID;

static int g_failures = 0;
static int g_reported = 0;

int main() {
    int checkedParams = 0;

    for (int slot = 0; slot < kFactoryPatchSlotCount; ++slot) {
        std::array<float, static_cast<size_t>(kNumParams)> loaded{};
        if (!loadFactoryPatchNormalizedParamsForSlot(slot, loaded)) {
            std::printf("FAIL: slot %d failed to load factory params\n", slot);
            ++g_failures;
            continue;
        }

        SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
        if (!root.valid()) {
            std::printf("FAIL: slot %d produced an invalid state root\n", slot);
            ++g_failures;
            continue;
        }

        for (int i = 0; i < kNumParams; ++i) {
            if (!isFactoryPatchAudioAuthorityParam(i)) continue;      // only audio-authority state
            if (isRuntimeOnlyOrTransientParam(i)) continue;           // runtime-only reset by design
            if (i == static_cast<int>(kParamProgram) ||
                i == static_cast<int>(kParamBankSlot) ||
                i == static_cast<int>(kParamBankCommand)) continue;   // canonicalised identity

            // Factory definitions may use any normalized point inside an enum
            // bin (legacy authoring commonly used 0.10/0.22/0.28). State-root
            // installation canonicalizes that point to the shared semantic
            // grid, so compare the canonical value—not the incidental source
            // float—while still proving the selected parameter meaning survives.
            const float want = sanitizeNormalizedParamValue(
                i, loaded[static_cast<size_t>(i)], defaultNormalizedParamValue(i));
            const float got  = sidStateRootParamValue(root, i);
            ++checkedParams;
            if (!std::isfinite(got) || std::fabs(want - got) > 0.002f) {
                if (g_reported < 20) {
                    std::printf("MISMATCH slot %d param %d: loaded=%.6f applied=%.6f\n", slot, i, (double)want, (double)got);
                    ++g_reported;
                }
                ++g_failures;
            }
        }
    }

    std::printf("[apply-roundtrip] checked %d audio-authority param values across %d slots\n",
                checkedParams, kFactoryPatchSlotCount);

    if (g_failures == 0) {
        std::printf("factory_patch_apply_roundtrip_v850_tests: PASS\n");
        return 0;
    }
    std::printf("factory_patch_apply_roundtrip_v850_tests: %d FAILURE(S)\n", g_failures);
    return 1;
}
