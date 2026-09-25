// render_mode_transition_kit_preservation_v942_tests.cpp
// Source/behavioral guard for v942 render-mode transition cleanup without kit destruction.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

namespace {
void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "RenderModeTransitionKitPreservationV942Tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}
std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
void requireContains(const std::string& s, const char* needle, const char* msg) {
    require(s.find(needle) != std::string::npos, msg);
}
void requireAbsent(const std::string& s, const char* needle, const char* msg) {
    require(s.find(needle) == std::string::npos, msg);
}
}

int main() {
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string renderHost = readFile((root + "/include/arpsid/core/sid_runtime_render_host.h").c_str());
    const std::string phase2 = readFile((root + "/source/arpsid_processor_phase2.cpp").c_str());
    const std::string au3 = readFile((root + "/source/au3/ArpSIDDSPKernel.hpp").c_str());
    const std::string factory = readFile((root + "/source/factory_patch_params.h").c_str());

    requireContains(renderHost, "if (bank.drSid) bank.drSid->allNotesOff();",
                    "mode transition must silence DrSID voices");
    requireContains(renderHost, "Do not call drSid->reset() here; it can wipe",
                    "mode transition must document DrSID kit preservation");
    requireContains(renderHost, "Do not call drSid->reset() here; it can wipe\n        // the selected SID808/DrSID kit on BitPerfect<->DrSID transitions.",
                    "mode transition must document that DrSID reset is forbidden in this path");
    requireContains(renderHost, "target.runtimeModel().setSeqStep(0);",
                    "shared transition helper must reset SEQ step");

    requireContains(phase2, "v942: Phase2 mode transitions must not destroy DrSID/SID808 kit state.",
                    "Phase2 local transition must preserve DrSID/SID808 kit state");
    requireContains(phase2, "ArpSID::runtimeRenderHostAllNotesOff(*this);\n    sidWriteQueue_().clear();",
                    "Phase2 transition must silence performance authorities without backend reset");
    requireAbsent(phase2, "resetRenderModeOutputNormalizer_();\n    ArpSID::runtimeRenderHostResetEngines(*this);\n    sidWriteQueue_().clear();",
                  "Phase2 mode transition must not use full engine reset");

    requireContains(au3, "const bool effectiveSeqAfterTransition =\n            ArpSID::sidEffectiveSeqAuthorityFromLiveParams(renderParams_);",
                    "AU3 mode transition must compute effective SEQ before transport re-arm");
    requireContains(au3, "if (effectiveSeqAfterTransition &&\n            runtimeHostSurface_().transportPlaying && runtimeModel_.followHostTempoSeq())",
                    "AU3 mode transition must not re-arm SEQ while SEQ is not effective authority");

    requireContains(factory, "v950: DrSID/SID808 factory roots are first-class DrSID authority.",
                    "DrSID/SID808 factory roots must document first-class authority canonicalization");
    requireContains(factory, "sp(kParamArpEnable, 0.0f);\n    sp(kParamSeqEnable, 0.0f);\n    sp(kParamSeqMode, 0.0f);",
                    "DrSID factory defaults must clear raw ARP/SEQ while retaining pattern data");
    requireContains(factory, "sp(kParamDrSidEnable, 1.0f);\n        sp(kParamArpEnable, 0.0f);\n        sp(kParamSeqEnable, 0.0f);",
                    "schema DrSID/SID808 roots must clear raw ARP/SEQ");
    requireContains(factory, "else if (e.param_id == static_cast<uint32_t>(kParamArpEnable)) {\n            e.value = 0.0f;\n        } else if (e.param_id == static_cast<uint32_t>(kParamSeqEnable)) {\n            e.value = 0.0f;",
                    "schema state-root semantic entries must clear ARP/SEQ");

    std::cout << "RenderModeTransitionKitPreservationV942Tests PASS\n";
    return 0;
}
