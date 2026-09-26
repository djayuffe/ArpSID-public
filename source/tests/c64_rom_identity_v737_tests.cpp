// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_platform.h"
#include "arpsid/core/c64_psid_runtime.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

static void require(bool ok, const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}

int main() {
    using namespace ArpSID::C64;

    require(c64IdentifyRomByCrc(C64RomSlot::Basic, C64RomSet::kBasicSize, 0xF833D117u) ==
                C64RomIdentity::Basic90122601,
            "BASIC 901226-01 CRC32 identifies as known stock");
    require(c64IdentifyRomByCrc(C64RomSlot::Kernal, C64RomSet::kKernalSize, 0xDBE3E7C7u) ==
                C64RomIdentity::Kernal90122703,
            "KERNAL 901227-03 CRC32 identifies as known stock");
    require(c64IdentifyRomByCrc(C64RomSlot::Character, C64RomSet::kCharacterSize, 0xEC4272EEu) ==
                C64RomIdentity::Character90122501,
            "character 901225-01 CRC32 identifies as known stock");
    require(c64RomIdentityIsKnownStock(C64RomIdentity::Kernal90122703),
            "known Commodore KERNAL identity is stock");
    require(!c64RomIdentityIsKnownStock(C64RomIdentity::UnknownSizeOnly),
            "unknown size-only identity is not stock");

    std::array<uint8_t, C64RomSet::kBasicSize> zeroBasic{};
    std::array<uint8_t, C64RomSet::kKernalSize> zeroKernal{};
    std::array<uint8_t, C64RomSet::kCharacterSize> zeroChar{};

    C64Platform platform;
    platform.reset();
    require(platform.loadBasicRom(zeroBasic.data(), zeroBasic.size(), C64RomTrust::KnownStock),
            "size-valid BASIC load accepts input bytes even when caller requests stock trust");
    require(platform.basicRomTrust() == C64RomTrust::SizeOnlyUnverified,
            "arbitrary BASIC bytes cannot be promoted to KnownStock by caller flag");
    require(platform.basicRomIdentity() == C64RomIdentity::UnknownSizeOnly,
            "arbitrary BASIC bytes remain unknown size-only identity");
    require(!platform.setBasicRomTrust(C64RomTrust::KnownStock),
            "setBasicRomTrust(KnownStock) rejects unverified bytes");
    require(platform.setBasicRomTrust(C64RomTrust::KnownPatchedNonStock),
            "caller may explicitly mark arbitrary loaded BASIC as patched non-stock");
    require(platform.basicRomTrust() == C64RomTrust::KnownPatchedNonStock,
            "patched non-stock trust is retained but is not physical stock");
    require(!platform.hasVerifiedStockRomSet(),
            "patched/non-stock or incomplete ROM set is not verified stock");

    C64Runtime rt;
    rt.reset(true);
    require(rt.platform().loadBasicRom(zeroBasic.data(), zeroBasic.size(), C64RomTrust::KnownStock),
            "runtime BASIC load accepts bytes");
    require(rt.platform().loadKernalRom(zeroKernal.data(), zeroKernal.size(), C64RomTrust::KnownStock),
            "runtime KERNAL load accepts bytes");
    require(rt.platform().loadCharacterRom(zeroChar.data(), zeroChar.size(), C64RomTrust::KnownStock),
            "runtime character load accepts bytes");
    require(rt.platform().hasCompleteExternalRomSet(),
            "three size-valid ROM blobs form a complete external ROM set");
    require(!rt.platform().hasVerifiedStockRomSet(),
            "three arbitrary blobs are not verified stock despite caller KnownStock request");
    require((rt.physicalExactnessBlockerMask() & static_cast<uint32_t>(C64PhysicalExactnessBlocker::RomIdentityUnverified)) != 0u,
            "complete but unverified ROM set keeps RomIdentityUnverified physical blocker");

    C64Runtime unloaded;
    unloaded.reset(true);
    require((unloaded.totalPhysicalRiskMask() & static_cast<uint32_t>(C64PhysicalExactnessBlocker::ObservedDowngradeLedger)) == 0u,
            "unloaded/non-RSID runtime does not report an observed RSID downgrade ledger event");
    require(unloaded.rsidExactnessDowngradeReasons() != RsidExactnessDowngrade::None,
            "unloaded runtime still reports NotRsid through the RSID-specific downgrade API");

    std::cout << "C64RomIdentityV737Tests PASS\n";
    return 0;
}
