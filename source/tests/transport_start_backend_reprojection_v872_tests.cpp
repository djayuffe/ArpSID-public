// transport_start_backend_reprojection_v872_tests.cpp
//
// v872 root-cause regression — "drums stop working after Stop→Play until you
// reapply the patch".
//
// clearRuntimeStateForTransportStart_() (and other reset paths) call
// runtimeRenderHostResetEngines(), which resets DrSID/SID808/sidRegister to a blank
// backend. The backend projection uses change-tracking (firstApply/lastMode/last*)
// to skip redundant work, so if a reset does NOT re-arm firstApply, the next
// projectStateToBackends() sees "nothing changed" and never rebuilds the patch into
// the freshly-reset backend — the drums go silent until a manual patch reapply
// forces a full projection.
//
// The fix re-arms firstApply inside runtimeRenderHostResetEngines(). This test
// drives that function against a minimal target and asserts firstApply is true
// afterwards, so any reset is guaranteed to trigger a full re-projection.

#include "arpsid/core/sid_runtime_render_host.h"
#include "arpsid/core/sid_runtime_engine_bank.h"
#include "arpsid/core/sid_runtime_target_adapter.h"
#include "arpsid/core/sid_runtime_voice_policy.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "transport_start_backend_reprojection_v872_tests FAIL: " << message << "\n";
        std::exit(1);
    }
}

// Minimal target satisfying the runtimeRenderHostResetEngines() contract. Engine
// pointers stay null (reset/all-notes-off are skipped for them); sidRegister is a
// value engine and is reset. We only care about the projection-state re-arming.
struct MockTarget {
    ArpSID::SidRuntimeEngineBank bank_{};
    ArpSID::SidRuntimeProjectionState proj_{};

    ArpSID::SidRuntimeEngineBank& runtimeEngineBank() noexcept { return bank_; }
    ArpSID::SidRuntimeProjectionState& runtimeProjectionState() noexcept { return proj_; }
    ArpSID::VoiceAllocator* runtimeVoicePolicy() noexcept { return nullptr; }
    void runtimeHardSynthAllNotesOffChannel(int) noexcept {}
};

void testResetReArmsProjectionSoBackendRebuilds() {
    MockTarget t;
    // Simulate a runtime that has already projected once (steady state): the
    // change-tracking cache believes the backend is up to date.
    t.proj_.firstApply = false;

    ArpSID::runtimeRenderHostResetEngines(t);

    // After resetting the concrete backends, the projection MUST be re-armed so the
    // next projectStateToBackends() does a full rebuild — otherwise the reset
    // backend keeps no patch/kit/model config (the Stop→Play silent-drums bug).
    require(t.proj_.firstApply,
            "runtimeRenderHostResetEngines must re-arm firstApply so the reset backend is fully re-projected");
}

void testPanicAlsoReArmsProjection() {
    // Panic routes through runtimeRenderHostResetEngines, so it must also re-arm.
    MockTarget t;
    t.proj_.firstApply = false;
    ArpSID::runtimeRenderHostResetEngines(t); // reset path shared by panic
    require(t.proj_.firstApply, "any engine reset must re-arm the projection");
}

} // namespace

int main() {
    testResetReArmsProjectionSoBackendRebuilds();
    testPanicAlsoReArmsProjection();
    std::cout << "transport_start_backend_reprojection_v872_tests PASS\n";
    return 0;
}
