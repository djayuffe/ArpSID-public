#include "arpsid/core/c64_psid_runtime.h"
#include "arpsid/gui/diagnostic_snapshot.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <array>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}


static std::string sourceRootFromThisFile() {
    std::string f = __FILE__;
    const std::string posix = "/source/tests/";
    auto p = f.rfind(posix);
    if (p != std::string::npos) return f.substr(0, p);
    const std::string rel = "source/tests/";
    p = f.rfind(rel);
    if (p != std::string::npos) return f.substr(0, p == 0 ? 0 : p - 1);
    return ".";
}

static std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), "can open source text fixture");
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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

int main() {
    C64Runtime unloadedRt;
    unloadedRt.reset(true);
    require(unloadedRt.rsidPlaybackModeCode() == 0u,
            "unloaded/non-RSID runtime reports playback mode code 0");

    const std::string guiPath = sourceRootFromThisFile() + "/source/au3/ArpSIDViewController.mm";
    const std::string guiText = readTextFile(guiPath);
    require(guiText.find("const uint64_t vals[27]") == std::string::npos,
            "GUI diagnostic value array must not keep stale 27-element declaration");
    require(guiText.find("kC64DiagValueCount_v302 = 55u") != std::string::npos,
            "GUI diagnostic value count is centralized at 55 (v841: split render-stall rows)");
    require(guiText.find("i < kC64DiagValueCount_v302") != std::string::npos,
            "GUI diagnostic refresh loop uses the centralized count");
    require(guiText.find("rsidPlaybackModeCode (0=none/1=strict/2=compatible)") != std::string::npos,
            "GUI playback mode label documents the none/unloaded mode 0");
    require(guiText.find("rsidPlaybackModeCode (1=strict/2=compatible)") == std::string::npos,
            "GUI playback mode label must not omit mode 0");
    require(guiText.find("v605  rsidStrictStatusCode") == std::string::npos,
            "GUI strict-status label must not keep stale v605 wording");
    require(guiText.find("v311  rsidStrictStatusCode") != std::string::npos,
            "GUI strict-status label uses current physical-risk version wording");

    const std::string diagText = readTextFile(sourceRootFromThisFile() + "/include/arpsid/gui/diagnostic_snapshot.h");
    require(diagText.find("24-field POD snapshot") == std::string::npos,
            "diagnostic snapshot comment must not advertise stale 24-field layout");
    require(diagText.find("POD snapshot for C64 STATE diagnostic dashboard") != std::string::npos,
            "diagnostic snapshot comment documents the current schema");

    static_assert(ArpSID::GUI::kDiagSchemaVersion == 8u,
                  "diagnostic snapshot schema must expose explicit RSID status/mode/mask + physical-blocker + v841 split render-stall fields");
    static_assert(sizeof(ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot) ==
                  sizeof(std::uint32_t) * 2 + sizeof(std::uint64_t) * 56,
                  "diagnostic snapshot v8 layout must be 2xuint32 + 56xuint64");

    C64Runtime strictRt;
    strictRt.reset(true);
    const auto rsid = buildRsid(0x0800u, {0x60u});
    require(strictRt.loadPsid(rsid.data(), rsid.size()), "RSID loads");
    require(strictRt.runInit(1, 4096), "strict init succeeds");
    strictRt.enablePhi2Machine(true);

    require(strictRt.rsidPlaybackModeCode() == 1u, "strict policy code is 1");
    require(strictRt.rsidStrictStatusCode() == 2u, "strict downgraded PHI2 status is 2");
    require(strictRt.rsidExactnessDowngradeMask() == rsidDowngradeMask(strictRt.rsidExactnessDowngradeReasons()),
            "runtime publishes exactness downgrade mask losslessly");
    require((strictRt.rsidExactnessDowngradeMask() & static_cast<uint32_t>(RsidExactnessDowngrade::MissingRealRoms)) != 0u,
            "downgrade mask exposes MissingRealRoms");
    require((strictRt.rsidExactnessDowngradeMask() & static_cast<uint32_t>(RsidExactnessDowngrade::CiaModelApprox)) == 0u,
            "downgrade mask omits CiaModelApprox after CIA cycle closure");

    ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot snap{};
    snap.rsidStrictStatusCode = strictRt.rsidStrictStatusCode();
    snap.rsidPlaybackModeCode = strictRt.rsidPlaybackModeCode();
    snap.rsidExactnessDowngradeMask = strictRt.rsidExactnessDowngradeMask();
    snap.rsidExactPlaybackActive = snap.rsidStrictStatusCode;
    require(snap.rsidStrictStatusCode == 2u, "GUI uses explicit strict status field");
    require(snap.rsidExactPlaybackActive == snap.rsidStrictStatusCode,
            "legacy diagnostic alias remains synchronized with explicit status");
    require(snap.rsidPlaybackModeCode == 1u, "GUI mode field reports strict");
    require(snap.rsidExactnessDowngradeMask != 0u, "GUI downgrade mask is nonzero for fallback-HLE RSID");

    C64Runtime compatibleRt;
    compatibleRt.reset(true);
    compatibleRt.setRsidPlaybackMode(RsidPlaybackMode::Compatible);
    require(compatibleRt.loadPsid(rsid.data(), rsid.size()), "compatible RSID loads");
    require(compatibleRt.runInit(1, 4096), "compatible init succeeds");
    compatibleRt.enablePhi2Machine(true);
    require(compatibleRt.rsidPhi2PathActiveAnyMode(), "compatible PHI2 path can be active");
    require(!compatibleRt.rsidStrictPhi2PathActive(), "compatible PHI2 path is not strict");
    require(compatibleRt.rsidStrictStatusCode() == 0u, "compatible PHI2 path has no strict status");
    require(compatibleRt.rsidPlaybackModeCode() == 2u, "compatible policy code is 2");

    C64Runtime compatibleNoPhiRt;
    compatibleNoPhiRt.reset(true);
    compatibleNoPhiRt.setRsidPlaybackMode(RsidPlaybackMode::Compatible);
    require(compatibleNoPhiRt.loadPsid(rsid.data(), rsid.size()), "compatible no-PHI2 RSID loads");
    require(compatibleNoPhiRt.runInit(1, 4096), "compatible no-PHI2 init succeeds");
    require(!compatibleNoPhiRt.runPlay(4096), "runtime refuses compatible non-PHI2 playback");
    require(compatibleNoPhiRt.legacyRuntimePlaybackCount() == 0u,
            "non-PHI2 refusal never records legacy playback");
    require(!c64PhysicalBlockerHas(strictRt.physicalExactnessBlockers(), C64PhysicalExactnessBlocker::LegacyMos6510PathPresent),
            "retired legacy Mos6510 path is not present");
    require(!c64PhysicalBlockerHas(strictRt.physicalExactnessBlockers(), C64PhysicalExactnessBlocker::LegacyMos6510PlaybackObserved),
            "strict PHI2 path does not claim observed legacy playback");
    require(!c64PhysicalBlockerHas(compatibleNoPhiRt.physicalExactnessBlockers(), C64PhysicalExactnessBlocker::LegacyMos6510PlaybackObserved),
            "compatible refusal does not expose observed legacy Mos6510 playback use");
    require(c64PhysicalBlockerHas(compatibleNoPhiRt.totalPhysicalRiskBlockers(), C64PhysicalExactnessBlocker::ObservedDowngradeLedger),
            "total physical risk combines capability blockers with observed downgrade ledger");
    require(c64PhysicalBlockerHas(strictRt.physicalExactnessBlockers(), C64PhysicalExactnessBlocker::MissingRealRoms),
            "physical blocker exposes missing ROMs even when strict PHI2 is active");
    require(!c64PhysicalBlockerHas(strictRt.physicalExactnessBlockers(), C64PhysicalExactnessBlocker::Cia6526NotCycleExact),
            "cycle-exact CIA has no capability blocker");
    require(!c64PhysicalBlockerHas(strictRt.physicalExactnessBlockers(), C64PhysicalExactnessBlocker::VicIINotCycleExact),
            "cycle-table VIC-II has no capability blocker");
    require(!c64PhysicalBlockerHas(strictRt.physicalExactnessBlockers(), C64PhysicalExactnessBlocker::SidReadbackModelApprox),
            "cycle-clocked SID readback has no capability blocker");
    require(c64PhysicalBlockerHas(strictRt.physicalExactnessBlockers(), C64PhysicalExactnessBlocker::OpenBusModelApprox),
            "open-bus model limitation is a capability blocker before any open-bus read occurs");

    {
        C64Runtime romRt;
        romRt.reset(true);
        require(romRt.loadPsid(rsid.data(), rsid.size()), "ROM-trust RSID loads");
        std::array<uint8_t, C64RomSet::kBasicSize> basic{};
        std::array<uint8_t, C64RomSet::kKernalSize> kernal{};
        std::array<uint8_t, C64RomSet::kCharacterSize> chargen{};
        for (size_t i = 0; i < basic.size(); ++i) basic[i] = static_cast<uint8_t>(0x11u + i * 13u);
        for (size_t i = 0; i < kernal.size(); ++i) kernal[i] = static_cast<uint8_t>(0x27u + i * 7u);
        for (size_t i = 0; i < chargen.size(); ++i) chargen[i] = static_cast<uint8_t>(0x33u + i * 5u);
        require(romRt.platform().loadBasicRom(basic.data(), basic.size()), "size-only BASIC ROM loads");
        require(romRt.platform().loadKernalRom(kernal.data(), kernal.size()), "size-only KERNAL ROM loads");
        require(romRt.platform().loadCharacterRom(chargen.data(), chargen.size()), "size-only character ROM loads");
        require(romRt.platform().hasCompleteExternalRomSet(), "size-only ROMs form a complete external set");
        require(!romRt.platform().hasVerifiedStockRomSet(), "size-only ROMs are not known-stock verified");
        const auto romBlockers = romRt.physicalExactnessBlockers();
        require(!c64PhysicalBlockerHas(romBlockers, C64PhysicalExactnessBlocker::MissingRealRoms),
                "complete external ROM set clears only the missing-ROM blocker");
        require(c64PhysicalBlockerHas(romBlockers, C64PhysicalExactnessBlocker::RomIdentityUnverified),
                "size-only random ROMs retain the ROM identity-unverified blocker");
        require((romRt.rsidExactnessDowngradeMask() & static_cast<uint32_t>(RsidExactnessDowngrade::RomIdentityUnverified)) != 0u,
                "size-only random ROMs remain visible in the RSID downgrade mask");
    }

    {
        C64Runtime trustedRomRt;
        trustedRomRt.reset(true);
        require(trustedRomRt.loadPsid(rsid.data(), rsid.size()), "trusted ROM RSID loads");
        std::array<uint8_t, C64RomSet::kBasicSize> basic{};
        std::array<uint8_t, C64RomSet::kKernalSize> kernal{};
        std::array<uint8_t, C64RomSet::kCharacterSize> chargen{};
        require(trustedRomRt.platform().loadBasicRom(basic.data(), basic.size(), C64RomTrust::KnownStock), "BASIC ROM bytes load even when caller asks for stock trust");
        require(trustedRomRt.platform().loadKernalRom(kernal.data(), kernal.size(), C64RomTrust::KnownStock), "KERNAL ROM bytes load even when caller asks for stock trust");
        require(trustedRomRt.platform().loadCharacterRom(chargen.data(), chargen.size(), C64RomTrust::KnownStock), "character ROM bytes load even when caller asks for stock trust");
        require(!trustedRomRt.platform().hasVerifiedStockRomSet(), "caller KnownStock flag alone does not form verified ROM set");
        require(trustedRomRt.platform().basicRomIdentity() == C64RomIdentity::UnknownSizeOnly, "zero BASIC blob remains unknown identity");
        const auto trustedBlockers = trustedRomRt.physicalExactnessBlockers();
        require(!c64PhysicalBlockerHas(trustedBlockers, C64PhysicalExactnessBlocker::MissingRealRoms), "complete ROM set clears missing-ROM blocker");
        require(c64PhysicalBlockerHas(trustedBlockers, C64PhysicalExactnessBlocker::RomIdentityUnverified), "unverified complete ROM set keeps ROM identity blocker");
        require(!c64PhysicalBlockerHas(trustedBlockers, C64PhysicalExactnessBlocker::Cia6526NotCycleExact), "CIA physical blocker remains cleared with external ROMs");
        require(!c64PhysicalBlockerHas(trustedBlockers, C64PhysicalExactnessBlocker::VicIINotCycleExact), "VIC-II physical blocker remains cleared with external ROMs");
        require(c64PhysicalBlockerHas(trustedRomRt.totalPhysicalRiskBlockers(), C64PhysicalExactnessBlocker::ObservedDowngradeLedger), "total risk remains set because non-ROM downgrades remain");
    }

    std::cout << "C64StrictStatusGuiContractV729Tests PASS\n";
    return 0;
}
