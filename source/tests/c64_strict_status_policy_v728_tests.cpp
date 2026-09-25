#include "arpsid/core/c64_psid_runtime.h"
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

static std::vector<uint8_t> buildRsid(uint16_t loadAddr, const std::vector<uint8_t>& initCode) {
    std::vector<uint8_t> v(0x7Cu, 0u);
    v[0]='R'; v[1]='S'; v[2]='I'; v[3]='D';
    v[4]=0; v[5]=2; v[6]=0; v[7]=0x7Cu;
    v[10]=static_cast<uint8_t>(loadAddr >> 8u);
    v[11]=static_cast<uint8_t>(loadAddr & 0xFFu);
    v[14]=0; v[15]=1; v[16]=0; v[17]=1;
    v[0x76]=0; v[0x77]=0x04; // PAL RSID flag fixture
    v.push_back(static_cast<uint8_t>(loadAddr & 0xFFu));
    v.push_back(static_cast<uint8_t>(loadAddr >> 8u));
    v.insert(v.end(), initCode.begin(), initCode.end());
    return v;
}

static void runRtsRsid(C64Runtime& rt) {
    const auto rsid = buildRsid(0x0800u, {0x60u});
    require(rt.loadPsid(rsid.data(), rsid.size()), "RSID loads");
    require(rt.runInit(1, 4096), "RSID init succeeds");
    rt.enablePhi2Machine(true);
    require(rt.rsidPhi2PathActiveAnyMode(), "PHI2 path is active in current policy mode");
}

static void testDefaultRsidPolicyIsStrict() {
    C64Runtime rt;
    rt.reset(true);
    require(rt.rsidPlaybackMode() == RsidPlaybackMode::Strict,
            "default RSID runtime policy is strict, not compatible");
    runRtsRsid(rt);
    require(rt.rsidStrictPhi2PathActive(),
            "default strict runtime reports strict PHI2 path active after PHI2 init");
    require(rt.rsidStrictStatusCode() == 2u,
            "strict default reports downgraded status when HLE/CIA/VIC downgrades exist");
}

static void testCompatiblePhi2IsNotStrictStatus() {
    C64Runtime rt;
    rt.reset(true);
    rt.setRsidPlaybackMode(RsidPlaybackMode::Compatible);
    runRtsRsid(rt);
    require(!rt.rsidStrictPhi2PathActive(),
            "compatible-mode PHI2 path is not labelled strict");
    require(rt.rsidPhi2PathActiveAnyMode(),
            "compatible-mode PHI2 path can still be reported by any-mode helper");
    require(rt.rsidStrictStatusCode() == 0u,
            "compatible-mode PHI2 path does not produce strict status 1/2");
    require(!rt.rsidPhysicallyExact(),
            "compatible-mode PHI2 path is not physically exact");
}

int main() {
    testDefaultRsidPolicyIsStrict();
    testCompatiblePhi2IsNotStrictStatus();
    std::cout << "C64StrictStatusPolicyV728Tests PASS\n";
    return 0;
}
