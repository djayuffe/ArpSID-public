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
    v[0x76]=0; v[0x77]=0x04;
    v.push_back(static_cast<uint8_t>(loadAddr & 0xFFu));
    v.push_back(static_cast<uint8_t>(loadAddr >> 8u));
    v.insert(v.end(), initCode.begin(), initCode.end());
    return v;
}

static void testMicroJamReasons() {
    Cpu6510Micro cpu; cpu.powerOn();
    cpu.setTrapBrkAsJam(true);
    cpu.tickPhi2End(0x00u); // no pending request: should not mutate reason
    auto req = cpu.tickPhi2Begin();
    require(req.active(), "opcode fetch active");
    cpu.tickPhi2End(0x00u);
    require(cpu.state().jammed, "BRK trap jams when requested");
    require(cpu.lastJamReason() == CpuJamReason::TrapBrkAsJam, "BRK trap jam reason");
    require(cpu.lastJamOpcode() == 0x00u, "BRK trap jam opcode");

    cpu.powerOn();
    req = cpu.tickPhi2Begin();
    require(req.active(), "KIL opcode fetch active");
    cpu.tickPhi2End(0x02u);
    require(cpu.state().jammed, "KIL opcode jams");
    require(cpu.lastJamReason() == CpuJamReason::KilOpcode, "KIL jam reason");
    require(cpu.lastJamOpcode() == 0x02u, "KIL jam opcode");
}

static void testStrictApproximateOpcodeJamReason() {
    // ARR #imm is implemented as approximate. Strict mode must stop before
    // producing a silently wrong value and expose an explicit jam reason.
    auto rsid = buildRsid(0x0800u, {0x6Bu, 0x00u, 0x60u});
    C64Runtime rt; rt.reset(true);
    rt.setRsidPlaybackMode(RsidPlaybackMode::Strict);
    require(rt.loadPsid(rsid.data(), rsid.size()), "strict approximate RSID loads");
    require(!rt.runInit(1, 2048), "strict approximate opcode init is refused");
    require(rt.phi2Machine().cpu().state().jammed, "strict approximate opcode jams PHI2 CPU");
    require(rt.phi2Machine().cpu().lastJamReason() == CpuJamReason::ApproximateOpcodeStrictPolicy,
            "strict approximate opcode jam reason is explicit");
    require(rt.phi2Machine().cpu().lastJamOpcode() == 0x6Bu,
            "strict approximate opcode jam opcode is ARR");
    const auto d = rt.rsidExactnessDowngradeReasons();
    require(rsidDowngradeHas(d, RsidExactnessDowngrade::ApproximateOpcode),
            "strict approximate opcode is propagated into exactness ledger");
    require(!rsidDowngradeHas(d, RsidExactnessDowngrade::LegacyFallback),
            "strict approximate opcode does not fallback to legacy init");
}

int main() {
    testMicroJamReasons();
    testStrictApproximateOpcodeJamReason();
    std::cout << "C64CpuJamReasonV725Tests PASS\n";
    return 0;
}
