// SPDX-License-Identifier: BSD-3-Clause
// drsid_user_kit_save_mode_normalization_v968_tests.cpp — v968 closure.
//
// A user "DrSID kit" is a drum-role patch by construction: _drumSaveUserKit
// stamps meta.role = Drum. Before v968 the save serialized the live kernel
// shadow verbatim (buildSerializableStateRootFromShadow) with no render-mode
// normalization, unlike the import/export bank paths which always force
// DrSidEnable=1 / SynthModeEnable=0 into the saved root. A kit saved while the
// engine happened to be in another render mode was therefore stamped role=Drum
// (so it appeared in the drum user-kit library) yet loaded back without
// entering DrSID mode.
//
// v968 routes _drumSaveUserKit through a dedicated _saveDrumKitDocumentToURL
// that normalizes the saved document into DrSID mode for both .arpsid and .json
// formats. This is a source-contract test (the save path is Objective-C++ that
// a portable C++ test cannot exercise directly), matching the repo style.

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "drsid_user_kit_save_mode_normalization_v968_tests FAIL: " << msg << "\n";
        std::exit(1);
    }
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), (std::string("cannot open ") + path).c_str());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void requireContains(const std::string& hay, const std::string& needle, const char* msg) {
    if (hay.find(needle) == std::string::npos) {
        std::cerr << "drsid_user_kit_save_mode_normalization_v968_tests FAIL: " << msg
                  << "\n  missing: " << needle << "\n";
        std::exit(1);
    }
}

std::string between(const std::string& s, const std::string& a, const std::string& b) {
    const std::size_t i = s.find(a);
    if (i == std::string::npos) return {};
    const std::size_t j = s.find(b, i + a.size());
    if (j == std::string::npos) return {};
    return s.substr(i, j - (i));
}

} // namespace

int main() {
#ifndef ARPSID_SOURCE_ROOT
#error ARPSID_SOURCE_ROOT must be defined
#endif
    const std::string root = ARPSID_SOURCE_ROOT;
    const std::string vc = readFile(root + "/source/au3/ArpSIDViewController.mm");

    // (1) The DrSID user-kit save routes through the drum-normalized save, not
    //     the generic patch save.
    const std::string saveKit = between(vc, "-(void)_drumSaveUserKit:(id)sender{", "\n-(");
    require(!saveKit.empty(), "could not isolate _drumSaveUserKit body");
    requireContains(saveKit, "_saveDrumKitDocumentToURL:kitURL adapter:adapter meta:meta",
                    "_drumSaveUserKit must save via the drum-normalized _saveDrumKitDocumentToURL");

    // (2) The drum-normalized save forces DrSID render mode into the saved root
    //     (matching the import/export paths), for both .arpsid and .json.
    const std::string saveDoc = between(vc,
        "-(ArpSIDFileBankResult)_saveDrumKitDocumentToURL:(NSURL*)url",
        "-(ArpSIDFileBankResult)_loadPatchDocumentFromURL:(NSURL*)url");
    require(!saveDoc.empty(), "could not isolate _saveDrumKitDocumentToURL body");
    requireContains(saveDoc, "sidSetStateRootParamValue(root, (int)ArpSID::kParamDrSidEnable, 1.0f)",
                    "drum-kit save must force DrSidEnable=1 into the saved root");
    requireContains(saveDoc, "sidSetStateRootParamValue(root, (int)ArpSID::kParamSynthModeEnable, 0.0f)",
                    "drum-kit save must force SynthModeEnable=0 into the saved root");
    requireContains(saveDoc, "sidEnsureSemanticParameterEntries(root)",
                    "drum-kit save must ensure semantic parameter entries like import/export");
    // Both formats go through the state-root path so normalization always applies.
    requireContains(saveDoc, "ArpSIDJSONPatchDocumentFromRoot(root, fileMeta)",
                    "drum-kit save writes JSON from the normalized root");
    requireContains(saveDoc, "ArpSID::ArpSIDFileBank::savePatchToFile(std::string(url.path.UTF8String), root, fileMeta)",
                    "drum-kit save writes .arpsid from the normalized root");

    std::cout << "drsid_user_kit_save_mode_normalization_v968_tests: OK\n";
    return 0;
}
