// fix-order #16: DIGI model + sample-bank state is one atomic persistence unit.
// The adapter keeps legacy split selectors as private fail-closed compatibility
// stubs, but the GUI/debug protocol must not advertise those split selectors as
// callable capability; otherwise future UI/AU code can reintroduce half-pair
// model/bank split-brain by asking respondsToSelector: for the old APIs.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifndef ARPSID_SOURCE_ROOT
#define ARPSID_SOURCE_ROOT "."
#endif

static std::string readFile(const char* rel) {
    std::ifstream f(std::string(ARPSID_SOURCE_ROOT) + "/" + rel, std::ios::binary);
    if (!f) {
        std::cerr << "cannot open " << rel << "\n";
        std::exit(2);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string sliceBetween(const std::string& s, const std::string& begin, const std::string& end) {
    const auto a = s.find(begin);
    require(a != std::string::npos, "begin marker found");
    const auto b = s.find(end, a);
    require(b != std::string::npos, "end marker found");
    return s.substr(a, b - a);
}

int main() {
    const std::string vc = readFile("source/au3/ArpSIDViewController.mm");
    const std::string protocol = sliceBetween(vc, "@protocol ArpSIDDebugAdapterLike", "@end");

    require(protocol.find("getDigiModel:(ArpSID::GUI::DigiPanelModel*)out") == std::string::npos,
            "public GUI/debug protocol must not expose legacy split getDigiModel:");
    require(protocol.find("setDigiModel:(const ArpSID::GUI::DigiPanelModel*)m;") == std::string::npos,
            "public GUI/debug protocol must not expose legacy split setDigiModel:");
    require(protocol.find("getDigiSampleBank:(ArpSID::GUI::DigiSampleBankBlob*)out") == std::string::npos,
            "public GUI/debug protocol must not expose legacy split getDigiSampleBank:");
    require(protocol.find("setDigiSampleBank:(const ArpSID::GUI::DigiSampleBankBlob*)bank;") == std::string::npos,
            "public GUI/debug protocol must not expose legacy split setDigiSampleBank:");

    require(protocol.find("getDigiModel:(ArpSID::GUI::DigiPanelModel*)model") != std::string::npos &&
            protocol.find("sampleBank:(ArpSID::GUI::DigiSampleBankBlob*)bank") != std::string::npos,
            "public protocol keeps atomic matched DIGI read selector");
    require(protocol.find("setDigiModel:(const ArpSID::GUI::DigiPanelModel*)model") != std::string::npos &&
            protocol.find("sampleBank:(const ArpSID::GUI::DigiSampleBankBlob*)bank") != std::string::npos,
            "public protocol keeps atomic matched DIGI write selector");
    require(protocol.find("public GUI/debug protocol deliberately does not expose") != std::string::npos,
            "protocol documents why split selectors are not advertised");

    const std::string h = readFile("source/au3/ArpSIDDSPKernelAdapter.h");
    require(h.find("Legacy split read: compatibility only; fail-closed default") != std::string::npos,
            "adapter category still documents fail-closed legacy split getters");
    require(h.find("Legacy split write: compatibility only; fail-closed no-op") != std::string::npos,
            "adapter category still documents fail-closed legacy split setters");

    const std::string mm = readFile("source/au3/ArpSIDDSPKernelAdapter.mm");
    require(mm.find("*out = ArpSID::GUI::makeDefaultDigiPanelModel();") != std::string::npos,
            "legacy split model getter returns default/empty model");
    require(mm.find("ArpSID::GUI::resetDigiSampleBankBlob(*out);") != std::string::npos,
            "legacy split sample-bank getter returns default/empty bank");
    require(mm.find("would indirectly publish a half-updated DIGI pair. New callers must use") != std::string::npos,
            "legacy split setters are documented as no-op compatibility stubs");

    std::cout << "digi_split_protocol_surface_v763_tests: PASS\n";
    return 0;
}
