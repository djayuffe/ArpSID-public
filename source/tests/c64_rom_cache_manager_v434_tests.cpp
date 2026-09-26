// Copyright (C) 2024-2026 Ulf Bertilsson
#include "arpsid/core/c64_rom_cache_manager.h"
#include "arpsid/core/c64_telemetry.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace ArpSID::C64;

static void require(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::vector<uint8_t> rom(C64RomType t, uint8_t seed) {
    std::vector<uint8_t> v(c64RomExpectedSize(t));
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<uint8_t>(seed + (i * 37u) + (i >> 3));
    v[0] ^= 0x80u; // avoid printable-header rejection
    return v;
}

int main() {
    const std::filesystem::path base = std::filesystem::temp_directory_path() / "arpsid_rom_cache_v434_tests";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);

    {
        C64Platform p;
        C64RomPrepareConfig cfg;
        cfg.isRsid = true;
        cfg.c64BasicFlag = true;
        cfg.enableHle = true;
        cfg.cacheDirectory = base.string();
        cfg.allowNetworkDownloads = true;
        bool called = false;
        auto r = C64RomCacheManager::prepare(p, cfg, [&](C64RomType t, std::vector<uint8_t>& out) {
            called = true;
            out = rom(t, static_cast<uint8_t>(0x10u + static_cast<unsigned>(t) * 0x20u));
            return true;
        });
        require(called, "explicit downloader callback used for missing required ROMs only when policy allows it");
        require(r.result == C64RomCacheResult::Ok, "prepare succeeds with explicit legal-source callback");
        require(r.createdCacheDirectory, "cache directory is created outside realtime path");
        require(r.usedDownloader && r.attemptedDownload, "report records downloader use");
        require(r.rsidReady && r.fullRomSetReady, "report exposes RSID and full-set readiness");
        require(p.hasCompleteExternalRomSet(), "platform has complete external ROM set");
        require(std::filesystem::exists(base / "kernal.rom"), "canonical kernal cache file written");
        require(std::filesystem::exists(base / "basic.rom"), "canonical basic cache file written");
        require(std::filesystem::exists(base / "chargen.rom"), "canonical chargen cache file written");

        const auto snap = c64BuildTelemetrySnapshot(p, true, 7, 0, 0.0f);
        require(snap.externalKernalRom && snap.externalBasicRom && snap.externalCharacterRom, "telemetry exposes loaded external ROMs");
        require(snap.externalCompleteRomSet, "telemetry exposes complete ROM set");
        require(snap.kernalRomChecksum == p.kernalRomChecksum(), "telemetry kernal checksum matches platform");
        require(snap.basicRomChecksum == p.basicRomChecksum(), "telemetry basic checksum matches platform");
        require(snap.characterRomChecksum == p.characterRomChecksum(), "telemetry chargen checksum matches platform");
    }

    {
        C64Platform p;
        auto status = C64RomCacheManager::cacheStatus(p, base.string());
        require(C64RomCacheManager::statusHasFullSet(status), "cacheStatus reloads full set from persistent cache");
        require(C64RomCacheManager::statusReadyForRsid(status, true), "cacheStatus reports RSID+BASIC ready");
        require(p.hasCompleteExternalRomSet(), "cacheStatus loads persistent ROMs into platform");
    }

    {
        C64Platform p;
        auto bad = C64RomCacheManager::importRomBytes(p, base.string(), C64RomType::Kernal, rom(C64RomType::Kernal, 0x44), true, true);
        require(bad.result == C64RomCacheResult::RealtimeUseForbidden, "ROM import is forbidden from realtime path");
    }

    {
        C64Platform p;
        const auto html = std::vector<uint8_t>{'<','h','t','m','l','>','b','a','d'};
        auto bad = C64RomCacheManager::importRomBytes(p, base.string(), C64RomType::Basic, html);
        require(bad.result == C64RomCacheResult::BadRomSize, "wrong size import fails before cache write");
        auto page = std::vector<uint8_t>(c64RomExpectedSize(C64RomType::Character), 'A');
        page[0] = '<'; page[1] = 'h'; page[2] = 't'; page[3] = 'm';
        bad = C64RomCacheManager::importRomBytes(p, base.string(), C64RomType::Character, page);
        require(bad.result == C64RomCacheResult::HtmlOrTextInsteadOfRom, "HTML/error-page shaped ROM is rejected");
    }

    std::cout << "C64RomCacheManagerV434 tests passed\n";
    return 0;
}
