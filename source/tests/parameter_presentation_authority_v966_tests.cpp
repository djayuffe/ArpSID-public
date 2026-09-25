// SPDX-License-Identifier: BSD-3-Clause
// parameter_presentation_authority_v966_tests.cpp — v966 closure.
//
// One parameter-ID-aware presentation authority (format / parse / unit) is
// shared by AUv2, AUv3 and VST3 and delegates to the identical canonical DSP
// laws the runtime renders with. Before v966 each wrapper carried its own
// generic unit-string formulas, so host text misrepresented real behavior:
//
//   LFO rate norm 0.2      displayed "4000.0 Hz"  — DSP renders 0.2885 Hz
//   Limiter attack 0.08    displayed "160.0 ms"   — DSP uses 1.6 ms
//   Limiter release 0.35   displayed "700.0 ms"   — DSP uses 356.5 ms
//   Portamento 0.5         displayed "2.500 s"    — DSP uses 1.25 s
//   Seq tempo 0.4          displayed "136.0 bpm"  — DSP uses 132 bpm
//   VST parse "160 bpm"    clamped to norm 1.0    — must invert to 0.5
//
// Also closes the VST UTF-8→UTF-16 byte-cast defect (handoff §22.2): real
// bounded decoding with surrogate pairs, deterministic U+FFFD replacement and
// truncation that never splits a pair.
//
// Sections:
//   I.   DSP-law/handoff regression values
//   II.  parse(format(v)) roundtrip across every parameter
//   III. Labels, booleans and enum text (numeric and label forms)
//   IV.  Malformed input never mutates the output value
//   V.   Typed unit descriptors
//   VI.  UTF-8 ⇄ UTF-16 conversion
//   VII. Wrapper source contracts (all three wrappers delegate; no local
//        unit-string formula or byte-cast conversion remains)

#include "arpsid/core/sid_parameter_presentation.h"
#include "arpsid/core/math_utils.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), path.c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void requireContains(const std::string& haystack, const std::string& needle, const char* msg) {
    if (haystack.find(needle) == std::string::npos) {
        std::cerr << "FAIL: " << msg << "\nMissing: " << needle << "\n";
        std::exit(1);
    }
}

void requireAbsent(const std::string& haystack, const std::string& needle, const char* msg) {
    if (haystack.find(needle) != std::string::npos) {
        std::cerr << "FAIL: " << msg << "\nUnexpected: " << needle << "\n";
        std::exit(1);
    }
}

std::string fmt(int pid, float norm) {
    char buf[64] = {};
    require(ArpSID::SidParameterPresentation::formatNormalized(pid, norm, buf, sizeof(buf)),
            "formatNormalized must succeed for a valid parameter");
    return std::string(buf);
}

float parseOk(int pid, const char* text) {
    float out = -999.0f;
    require(ArpSID::SidParameterPresentation::parseToNormalized(pid, text, out),
            text);
    return out;
}

bool near(float a, float b, float tol) {
    return std::fabs(a - b) <= tol;
}

// ─── I. DSP-law/handoff regression values ────────────────────────────────────

void testHandoffRegressionValues() {
    using namespace ArpSID;

    // The presentation laws ARE the DSP laws.
    require(near(ArpSID_normToLfoRateHz(0.2f), 0.28854f, 1e-4f),
            "LFO law: norm 0.2 renders 0.2885 Hz");
    require(near(ArpSID_normToLimiterAttackMs(0.08f), 1.6f, 1e-5f),
            "limiter attack law: norm 0.08 renders 1.6 ms");
    require(near(ArpSID_normToLimiterReleaseMs(0.35f), 356.5f, 1e-3f),
            "limiter release law: norm 0.35 renders 356.5 ms");
    require(near(ArpSID_normToPortamentoSeconds(0.5f), 1.25f, 1e-5f),
            "portamento law: norm 0.5 renders 1.25 s");
    require(near(ArpSID_normToSeqTempoBpm(0.4f), 132.0f, 1e-4f),
            "seq tempo law: norm 0.4 renders 132 bpm");

    // Display text carries the semantic value, not a wrapper formula.
    requireContains(fmt(kParamLFORate, 0.2f), "0.2885", "LFO display shows exponential-law Hz");
    requireContains(fmt(kParamLFORate, 0.2f), "Hz", "LFO display carries Hz suffix");
    require(fmt(kParamLimiterAttack, 0.08f) == "1.60 ms", "limiter attack display");
    require(fmt(kParamLimiterRelease, 0.35f) == "356.50 ms", "limiter release display");
    require(fmt(kParamPortamentoTime, 0.5f) == "1.250 s", "portamento display");
    require(fmt(kParamSeqTempo, 0.4f) == "132.0 bpm", "seq tempo display");

    // Text entry inverts the same laws.
    require(near(parseOk(kParamLFORate, "0.2885 Hz"), 0.2f, 5e-4f), "LFO parse inverts exponential law");
    require(near(parseOk(kParamLimiterAttack, "1.6 ms"), 0.08f, 1e-4f), "limiter attack parse");
    require(near(parseOk(kParamLimiterRelease, "356.5 ms"), 0.35f, 1e-4f), "limiter release parse");
    require(near(parseOk(kParamPortamentoTime, "1.25 s"), 0.5f, 1e-4f), "portamento parse (quadratic inverse)");
    require(near(parseOk(kParamPortamentoTime, "1250 ms"), 0.5f, 1e-4f), "portamento parse accepts ms");
    require(near(parseOk(kParamSeqTempo, "132 bpm"), 0.4f, 1e-4f), "seq tempo parse");

    // §22.1 regression: "160 bpm" must invert the display law, not clamp
    // the first numeric token into [0,1].
    require(near(parseOk(kParamSeqTempo, "160 bpm"), 0.5f, 1e-4f),
            "parse must be parameter-ID aware: 160 bpm -> norm 0.5, not 1.0");

    // Master tune ±100 ct law (the AUv2 unit map missed "cent" vs "ct").
    require(fmt(kParamMasterTune, 0.75f) == "+50.0 ct", "master tune display in cents");
    require(near(parseOk(kParamMasterTune, "-100 ct"), 0.0f, 1e-4f), "master tune parse clamps at -100 ct");

    // Locale: comma decimal separator must parse identically.
    require(near(parseOk(kParamPortamentoTime, "1,25 s"), 0.5f, 1e-4f),
            "comma decimal separator parses identically");
}

// ─── II. parse(format(v)) roundtrip across every parameter ──────────────────

void testRoundtripAllParameters() {
    using namespace ArpSID;
    const float grid[] = {0.0f, 0.08f, 0.2f, 0.25f, 0.35f, 0.4f, 0.5f, 0.75f, 0.9f, 1.0f};
    for (int pid = 0; pid < kNumParams; ++pid) {
        for (float g : grid) {
            const float sanitized = sanitizeNormalizedParamValue(pid, g, defaultNormalizedParamValue(pid));
            char buf[64] = {};
            require(SidParameterPresentation::formatNormalized(pid, g, buf, sizeof(buf)),
                    "every valid parameter must format");
            float back = -999.0f;
            require(SidParameterPresentation::parseToNormalized(pid, buf, back),
                    "every formatted value must parse back");
            // Tolerance covers display rounding; quadratic portamento is the
            // least precise near zero ("0.001 s" resolution).
            require(near(back, sanitized, 6e-3f),
                    "parse(format(v)) must roundtrip to the sanitized value");
        }
        const float def = defaultNormalizedParamValue(pid);
        const float defSanitized = sanitizeNormalizedParamValue(pid, def, def);
        char buf[64] = {};
        require(SidParameterPresentation::formatNormalized(pid, def, buf, sizeof(buf)),
                "default value must format");
        float back = -999.0f;
        require(SidParameterPresentation::parseToNormalized(pid, buf, back),
                "default value text must parse");
        require(near(back, defSanitized, 6e-3f), "default value must roundtrip");
    }
}

// ─── III. Labels, booleans and enum text ─────────────────────────────────────

void testLabelsAndEnums() {
    using namespace ArpSID;

    require(fmt(kParamArpEnable, 1.0f) == "ON" && fmt(kParamArpEnable, 0.0f) == "OFF",
            "booleans display ON/OFF");
    require(parseOk(kParamArpEnable, "on") == 1.0f && parseOk(kParamArpEnable, "OFF") == 0.0f,
            "boolean labels parse case-insensitively");
    require(parseOk(kParamArpEnable, "1") == 1.0f && parseOk(kParamArpEnable, "0.2") == 0.0f,
            "booleans accept numeric forms");

    require(fmt(kParamSidChipRevision, 1.0f) == "MOS 8580 R5", "chip revision label");
    require(near(parseOk(kParamSidChipRevision, "8580"), 1.0f, 1e-6f), "chip revision parses 8580");
    require(near(parseOk(kParamSidChipRevision, "mos 6581 r3"), 1.0f / 3.0f, 1e-6f),
            "chip revision parses R3 case-insensitively");

    require(fmt(kParamSidOversamplingFactor, 1.0f) == "8x", "SID oversampling label");
    require(near(parseOk(kParamSidOversamplingFactor, "2x"), 1.0f / 3.0f, 1e-6f),
            "SID oversampling parses factor label");
    require(fmt(kParamHiFiOversampling, 1.0f) == "16x", "HiFi oversampling label (4/8/16 law)");
    require(near(parseOk(kParamHiFiOversampling, "8x"), 0.5f, 1e-6f), "HiFi oversampling parses 8x");

    require(fmt(kParamDrSidMachineModel, 1.0f) == "Analog X0X-8", "DrSID machine model label");
    require(near(parseOk(kParamDrSidMachineModel, "x0x"), 1.0f, 1e-6f), "DrSID model parses label");

    require(fmt(kParamModVCFCutoffSource, 0.0f) == "None", "mod source None label");
    const float velocityNorm = parseOk(kParamModVCFCutoffSource, "Velocity");
    const int maxIdx = kSidModSourceCount - 1;
    require(near(velocityNorm, (float)(int)SidModSource::Velocity / (float)maxIdx, 1e-6f),
            "mod source parses Velocity label to its canonical index");
    require(fmt(kParamModVCFCutoffSource, velocityNorm) == "Velocity",
            "mod source label roundtrips");

    // Forensic revision family.
    require(fmt(kParamForensicRevision, 1.0f) == "8580 R5", "forensic revision label");
    require(near(parseOk(kParamForensicRevision, "6581 R4AR"), 2.0f / 3.0f, 1e-6f),
            "forensic revision parses R4 label");

    // °C display and parse (Unicode suffix shared by all wrappers).
    requireContains(fmt(kParamForensicTemp, 0.5f), "\xC2\xB0""C", "temperature displays UTF-8 degree C");
    require(near(parseOk(kParamForensicTemp, "40 \xC2\xB0""C"), 0.5f, 1e-4f),
            "temperature parse inverts the 20..60 C law");
}

// ─── IV. Malformed input never mutates the output ────────────────────────────

void testMalformedInput() {
    using namespace ArpSID;
    const char* garbage[] = {"", "   ", "abc", "--", "+", "e5", "Hz", "..", "\xC2\xB0"};
    for (const char* g : garbage) {
        float out = 0.62f;
        const bool ok = SidParameterPresentation::parseToNormalized((int)kParamSeqTempo, g, out);
        require(!ok, "garbage must not parse");
        require(out == 0.62f, "failed parse must not mutate the output value");
    }
    float out = 0.62f;
    require(!SidParameterPresentation::parseToNormalized(-1, "1.0", out) && out == 0.62f,
            "invalid parameter id must fail without mutation");
    char buf[64] = {};
    require(!SidParameterPresentation::formatNormalized(-1, 0.5f, buf, sizeof(buf)),
            "invalid parameter id must not format");
}

// ─── V. Typed unit descriptors ───────────────────────────────────────────────

void testUnitDescriptors() {
    using namespace ArpSID;
    require(sidParameterUnitDescriptor((int)kParamPortamentoTime).unit == SidParameterUnit::Seconds,
            "portamento is Seconds");
    require(sidParameterUnitDescriptor((int)kParamMasterTune).unit == SidParameterUnit::Cents,
            "master tune is Cents (was Generic under 'cent' vs 'ct' string drift)");
    require(sidParameterUnitDescriptor((int)kParamLFO3Rate).unit == SidParameterUnit::Hertz,
            "all LFO rates are Hertz");
    require(sidParameterUnitDescriptor((int)kParamForensicIntensity).unit == SidParameterUnit::Percent,
            "'%' unit params are Percent (was Generic under '%' vs 'pct' string drift)");
    require(sidParameterUnitDescriptor((int)kParamSeqLength).unit == SidParameterUnit::Steps,
            "seq length is Steps");
    require(sidParameterUnitDescriptor((int)kParamArpEnable).unit == SidParameterUnit::Boolean,
            "toggles are Boolean");
    require(std::strcmp(sidParameterUnitDescriptor((int)kParamForensicTemp).suffix, "\xC2\xB0""C") == 0,
            "temperature suffix is the UTF-8 degree sign");
    require(sidParameterUnitDescriptor((int)kParamSidRegD400).unit == SidParameterUnit::Byte,
            "SID register params are Byte");
}

// ─── VI. UTF-8 ⇄ UTF-16 conversion ───────────────────────────────────────────

void testUtfConversion() {
    using namespace ArpSID;
    char16_t u16[64] = {};

    // ASCII identity.
    require(ArpSID_utf8ToUtf16("Attack", u16, 64) == 6, "ASCII length");
    require(u16[0] == u'A' && u16[5] == u'k' && u16[6] == 0, "ASCII passthrough");

    // Two-byte: degree sign (the "Chip Temperature °C" defect).
    require(ArpSID_utf8ToUtf16("\xC2\xB0""C", u16, 64) == 2, "degree-C decodes to 2 UTF-16 units");
    require(u16[0] == 0x00B0 && u16[1] == u'C', "degree sign decodes to U+00B0");

    // Greek and Cyrillic (two-byte), CJK (three-byte).
    ArpSID_utf8ToUtf16("\xCE\xA9 \xD0\x96 \xE3\x81\x82", u16, 64);
    require(u16[0] == 0x03A9 && u16[2] == 0x0416 && u16[4] == 0x3042,
            "Greek/Cyrillic/CJK decode correctly");

    // Supplementary plane: U+1D11E MUSICAL SYMBOL G CLEF -> surrogate pair.
    require(ArpSID_utf8ToUtf16("\xF0\x9D\x84\x9E", u16, 64) == 2, "supplementary plane is one pair");
    require(u16[0] == 0xD834 && u16[1] == 0xDD1E, "surrogate pair encodes U+1D11E");

    // Malformed sequences decode deterministically to U+FFFD.
    ArpSID_utf8ToUtf16("A\xC2", u16, 64);              // truncated 2-byte tail
    require(u16[0] == u'A' && u16[1] == 0xFFFD && u16[2] == 0, "truncated sequence -> U+FFFD");
    ArpSID_utf8ToUtf16("\xFF", u16, 64);               // invalid lead
    require(u16[0] == 0xFFFD, "invalid lead byte -> U+FFFD");
    ArpSID_utf8ToUtf16("\xC0\xAF", u16, 64);           // overlong '/'
    require(u16[0] == 0xFFFD, "overlong encoding rejected");
    ArpSID_utf8ToUtf16("\xED\xA0\x80", u16, 64);       // encoded surrogate half
    require(u16[0] == 0xFFFD, "encoded surrogate code point rejected");

    // Truncation never splits a surrogate pair.
    char16_t tiny[2] = {u'x', u'x'};
    const std::size_t wrote = ArpSID_utf8ToUtf16("\xF0\x9D\x84\x9E", tiny, 2);
    require(wrote == 0 && tiny[0] == 0, "no room for a full pair -> clean empty string");

    // UTF-16 -> UTF-8 roundtrip including the pair.
    char u8[64] = {};
    const char16_t src[] = {0x0041, 0x00B0, 0xD834, 0xDD1E, 0};
    ArpSID_utf16ToUtf8(src, u8, 64);
    require(std::strcmp(u8, "A\xC2\xB0\xF0\x9D\x84\x9E") == 0, "UTF-16 -> UTF-8 roundtrip");

    // Unpaired surrogate -> U+FFFD, never raw garbage.
    const char16_t bad[] = {0xD834, 0x0041, 0};
    ArpSID_utf16ToUtf8(bad, u8, 64);
    require(std::strcmp(u8, "\xEF\xBF\xBD""A") == 0, "unpaired surrogate -> U+FFFD");
}

// ─── VII. Wrapper source contracts ───────────────────────────────────────────

void testWrapperSourceContracts() {
#ifndef ARPSID_SOURCE_ROOT
#error ARPSID_SOURCE_ROOT must be defined
#endif
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string vst = readFile(root + "/source/arpsid_controller.cpp");
    const std::string au2 = readFile(root + "/source/au2/ArpSIDAUv2Component.mm");
    const std::string au3 = readFile(root + "/source/au3/ArpSIDAudioUnit.mm");

    // All three wrappers delegate to the shared authority.
    requireContains(vst, "SidParameterPresentation::formatNormalized",
                    "VST3 display must delegate to the shared presentation authority");
    requireContains(vst, "SidParameterPresentation::parseToNormalized",
                    "VST3 parse must delegate to the shared presentation authority");
    requireContains(au2, "SidParameterPresentation::formatNormalized",
                    "AUv2 display must delegate to the shared presentation authority");
    requireContains(au2, "SidParameterPresentation::parseToNormalized",
                    "AUv2 parse must delegate to the shared presentation authority");
    requireContains(au3, "SidParameterPresentation::formatNormalized",
                    "AUv3 display must delegate to the shared presentation authority");
    requireContains(au3, "implementorValueFromStringCallback",
                    "AUv3 must install a text-entry inversion callback");

    // The wrapper-local generic unit formulas must not return.
    requireAbsent(vst, "v * 20000.0f", "VST3 must not re-grow a linear Hz display formula");
    requireAbsent(vst, "v*240.0f+40.0f", "VST3 must not re-grow the 40+240 bpm formula");
    requireAbsent(au2, "v * 20000.0f", "AUv2 must not re-grow a linear Hz display formula");
    requireAbsent(au2, "v * 240.0f + 40.0f", "AUv2 must not re-grow the 40+240 bpm formula");

    // The UTF-8 byte cast must not return; VST diagnostics must be gated.
    requireContains(vst, "ArpSID_utf8ToUtf16", "VST3 must use real UTF-8 decoding");
    requireAbsent(vst, "dst[i] = (TChar)(unsigned char)src[i]",
                  "the byte-cast UTF-8 shortcut must stay dead");
    requireContains(vst, "ARPSID_VST_EDITOR_DIAGNOSTICS",
                    "createView stderr diagnostics must be compile-time gated");

    // The render-side laws delegate to the same shared helpers the
    // presentation service inverts (single-authority guarantee).
    const std::string services = readFile(root + "/include/arpsid/core/sid_runtime_parameter_services.h");
    const std::string postfx = readFile(root + "/include/arpsid/core/sid_postfx_timeline.h");
    const std::string kernel = readFile(root + "/source/au3/ArpSIDDSPKernel.hpp");
    requireContains(services, "ArpSID_normToLfoRateHz", "runtime LFO rate uses the shared law");
    requireContains(postfx, "ArpSID_normToLimiterAttackMs", "post-FX limiter attack uses the shared law");
    requireContains(postfx, "ArpSID_normToLimiterReleaseMs", "post-FX limiter release uses the shared law");
    requireContains(kernel, "ArpSID_normToSeqTempoBpm", "kernel seq tempo uses the shared law");
    requireContains(kernel, "ArpSID_normToSeqSteps", "kernel seq length uses the shared law");
}

} // namespace

int main() {
    testHandoffRegressionValues();
    testRoundtripAllParameters();
    testLabelsAndEnums();
    testMalformedInput();
    testUnitDescriptors();
    testUtfConversion();
    testWrapperSourceContracts();
    std::cout << "parameter_presentation_authority_v966_tests: OK\n";
    return 0;
}
