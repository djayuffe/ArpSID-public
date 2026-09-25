#include "arpsid/core/sid_state_codec.h"
#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "arpsid/core/sid_runtime_model.h"
#include "au3/ArpSIDStateSerializer.h"
#include "parameter_ids.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace ArpSID;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

static void requireNear(float actual, float expected, float eps, const char* message) {
    if (std::fabs(actual - expected) > eps) {
        std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected << "\n";
        std::exit(1);
    }
}

static SidStateRootV1 make_root_6581_ntsc_with_stale_mirrors() {
    SidStateRootV1 root{};
    root.patch.variant_profile = sidDefaultVariantProfile(SidFamily::MOS6581, SidVideoStandard::NTSC);
    root.patch.variant_profile.sanitize();
    sidSetStateRootParamValue(root, kParamSidModel, 1.0f);        // stale mirror: says 8580
    sidSetStateRootParamValue(root, kParamSidClockSystem, 0.0f); // stale mirror: says PAL
    sidSetStateRootParamValue(root, kParamFilterCutoff, 0.77f);
    return root;
}

static void test_variant_profile_beats_stale_semantic_mirrors() {
    SidStateRootV1 root = make_root_6581_ntsc_with_stale_mirrors();
    sanitizePersistentStateRootForSerialization(root);

    require(root.patch.variant_profile.family == SidFamily::MOS6581, "variant family remains canonical 6581");
    require(root.patch.variant_profile.video_standard == SidVideoStandard::NTSC, "variant video standard remains canonical NTSC");
    requireNear(sidStateRootParamValue(root, kParamSidModel), 0.0f, 0.00001f, "SID model mirror is derived from variant profile");
    requireNear(sidStateRootParamValue(root, kParamSidClockSystem), 1.0f, 0.00001f, "SID clock mirror is derived from variant profile");
}

static void test_flat_values_are_ignored_when_semantic_entries_exist() {
    SidStateRootV1 root{};
    root.patch.variant_profile = sidDefaultVariantProfile(SidFamily::MOS8580, SidVideoStandard::PAL);
    sidSetStateRootParamValue(root, kParamFilterCutoff, 0.25f);
    root.patch.parameters.values.assign((size_t)kNumParams, 0.99f); // stale legacy snapshot
    sanitizePersistentStateRootForSerialization(root);

    requireNear(sidStateRootParamValue(root, kParamFilterCutoff), 0.25f, 0.00001f, "semantic cutoff beats stale flat values");
}

static void test_legacy_values_import_only_when_semantic_entries_absent() {
    SidStateRootV1 root{};
    root.patch.parameters.semantic_entries.clear();
    root.patch.parameters.values.assign((size_t)kNumParams, 0.0f);
    root.patch.parameters.values[(size_t)kParamFilterCutoff] = 0.66f;
    sanitizePersistentStateRootForSerialization(root);

    requireNear(sidStateRootParamValue(root, kParamFilterCutoff), 0.66f, 0.00001f, "legacy flat cutoff imports when semantics absent");
}

static void test_encode_decode_roundtrip_keeps_variant_authority() {
    SidStateRootV1 root = make_root_6581_ntsc_with_stale_mirrors();
    sanitizePersistentStateRootForSerialization(root);
    std::vector<uint8_t> blob(encodedSidStateRootBinarySize(root));
    const size_t n = encodeStateRoot(root, kProjectStateMagic, blob.data(), blob.size());
    require(n > 0, "state root encoding produced a payload");

    SidStateRootV1 decoded{};
    require(decodeStateToRoot(blob.data(), n, decoded, kProjectStateMagic), "state root decode succeeds");
    require(decoded.patch.variant_profile.family == SidFamily::MOS6581, "decoded variant family remains 6581");
    require(decoded.patch.variant_profile.video_standard == SidVideoStandard::NTSC, "decoded variant video standard remains NTSC");
    requireNear(sidStateRootParamValue(decoded, kParamSidModel), 0.0f, 0.00001f, "decoded SID model mirror remains derived");
    requireNear(sidStateRootParamValue(decoded, kParamSidClockSystem), 1.0f, 0.00001f, "decoded SID clock mirror remains derived");
}

static void test_runtime_apply_restore_uses_profile_not_stale_mirrors() {
    SidRuntimeModel runtime{};
    SidStateRootV1 root = make_root_6581_ntsc_with_stale_mirrors();
    runtime.applyStateRoot(root);

    require(runtime.variantProfile().family == SidFamily::MOS6581, "runtime variant family follows profile");
    require(runtime.variantProfile().video_standard == SidVideoStandard::NTSC, "runtime variant video follows profile");
    requireNear(sidStateRootParamValue(runtime.stateRoot(), kParamSidModel), 0.0f, 0.00001f, "runtime SID model mirror follows profile");
    requireNear(sidStateRootParamValue(runtime.stateRoot(), kParamSidClockSystem), 1.0f, 0.00001f, "runtime SID clock mirror follows profile");
}

int main() {
    test_variant_profile_beats_stale_semantic_mirrors();
    test_flat_values_are_ignored_when_semantic_entries_exist();
    test_legacy_values_import_only_when_semantic_entries_absent();
    test_encode_decode_roundtrip_keeps_variant_authority();
    test_runtime_apply_restore_uses_profile_not_stale_mirrors();
    return 0;
}
