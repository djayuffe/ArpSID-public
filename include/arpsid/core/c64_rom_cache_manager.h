#pragma once

#include "c64_platform.h"
#include "c64_pla.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace ArpSID::C64 {

enum class C64RomType : uint8_t {
    Kernal = 0,
    Basic = 1,
    Character = 2,
};

inline const char* c64RomTypeName(C64RomType type) noexcept {
    switch (type) {
        case C64RomType::Kernal: return "kernal";
        case C64RomType::Basic: return "basic";
        case C64RomType::Character: return "chargen";
    }
    return "unknown";
}

inline size_t c64RomExpectedSize(C64RomType type) noexcept {
    switch (type) {
        case C64RomType::Kernal: return C64RomSet::kKernalSize;
        case C64RomType::Basic: return C64RomSet::kBasicSize;
        case C64RomType::Character: return C64RomSet::kCharacterSize;
    }
    return 0;
}

enum class C64RomCacheResult : uint8_t {
    Ok = 0,
    MissingCacheDirectory,
    MissingRequiredRom,
    DownloadDisabledByPolicy,
    DownloaderFailed,
    BadRomSize,
    HtmlOrTextInsteadOfRom,
    WriteFailed,
    LoadFailed,
    CacheDirectoryCreateFailed,
    RealtimeUseForbidden,
};

inline const char* c64RomCacheResultName(C64RomCacheResult r) noexcept {
    switch (r) {
        case C64RomCacheResult::Ok: return "Ok";
        case C64RomCacheResult::MissingCacheDirectory: return "MissingCacheDirectory";
        case C64RomCacheResult::MissingRequiredRom: return "MissingRequiredRom";
        case C64RomCacheResult::DownloadDisabledByPolicy: return "DownloadDisabledByPolicy";
        case C64RomCacheResult::DownloaderFailed: return "DownloaderFailed";
        case C64RomCacheResult::BadRomSize: return "BadRomSize";
        case C64RomCacheResult::HtmlOrTextInsteadOfRom: return "HtmlOrTextInsteadOfRom";
        case C64RomCacheResult::WriteFailed: return "WriteFailed";
        case C64RomCacheResult::LoadFailed: return "LoadFailed";
        case C64RomCacheResult::CacheDirectoryCreateFailed: return "CacheDirectoryCreateFailed";
        case C64RomCacheResult::RealtimeUseForbidden: return "RealtimeUseForbidden";
    }
    return "Unknown";
}

struct C64RomRequirement {
    bool kernal = false;
    bool basic = false;
    bool character = false;
};

struct C64RomPrepareConfig {
    bool isRsid = false;
    bool c64BasicFlag = false;
    bool enableHle = true;
    bool allowNetworkDownloads = false;
    bool createCacheDirectory = true;
    bool calledFromRealtimeThread = false;
    std::string cacheDirectory;
};

struct C64RomCacheStatus {
    bool kernalCached = false;
    bool basicCached = false;
    bool characterCached = false;
    bool kernalLoaded = false;
    bool basicLoaded = false;
    bool characterLoaded = false;
    uint32_t kernalChecksum = 0;
    uint32_t basicChecksum = 0;
    uint32_t characterChecksum = 0;
    std::string kernalPath;
    std::string basicPath;
    std::string characterPath;
};

struct C64RomPrepareReport {
    C64RomCacheResult result = C64RomCacheResult::MissingRequiredRom;
    C64RomRequirement required{};
    C64RomCacheStatus status{};
    bool attemptedDownload = false;
    bool usedDownloader = false;
    bool createdCacheDirectory = false;
    bool rsidReady = false;
    bool fullRomSetReady = false;
    C64RomType failedType = C64RomType::Kernal;
};

class C64RomCacheManager {
public:
    using Downloader = std::function<bool(C64RomType type, std::vector<uint8_t>& out)>;

    // Policy note: ArpSID does not hard-code or automatically fetch copyrighted
    // Commodore ROM URLs. The host/UI may provide a downloader callback only
    // after the user has supplied a legal source/consent. This manager then
    // validates exact sizes, rejects HTML/error pages, writes a persistent cache,
    // and loads the ROMs into the C64 platform outside the audio render thread.
    static C64RomPrepareReport prepare(C64Platform& platform,
                                       const C64RomPrepareConfig& config,
                                       const Downloader& downloader = Downloader{}) {
        C64RomPrepareReport report;
        report.required = requirementsFor(config.isRsid, config.c64BasicFlag, config.enableHle);
        if (config.calledFromRealtimeThread) {
            report.result = C64RomCacheResult::RealtimeUseForbidden;
            return report;
        }
        if (config.cacheDirectory.empty()) {
            report.result = C64RomCacheResult::MissingCacheDirectory;
            return report;
        }
        if (config.createCacheDirectory) {
            bool created = false;
            if (!ensureDirectory_(config.cacheDirectory, created)) {
                report.result = C64RomCacheResult::CacheDirectoryCreateFailed;
                return report;
            }
            report.createdCacheDirectory = created;
        }

        loadCachedAvailable_(platform, config.cacheDirectory, report.status);
        report.rsidReady = statusReadyForRsid(report.status, config.c64BasicFlag);
        report.fullRomSetReady = statusHasFullSet(report.status);
        if (requirementsSatisfied_(report.required, report.status)) {
            report.result = C64RomCacheResult::Ok;
            return report;
        }

        if (!config.allowNetworkDownloads || !downloader) {
            report.result = config.allowNetworkDownloads ? C64RomCacheResult::MissingRequiredRom
                                                         : C64RomCacheResult::DownloadDisabledByPolicy;
            return report;
        }

        report.attemptedDownload = true;
        const C64RomType order[3] = {C64RomType::Kernal, C64RomType::Basic, C64RomType::Character};
        for (C64RomType type : order) {
            if (!isRequired_(report.required, type) || isLoaded_(report.status, type)) continue;
            std::vector<uint8_t> bytes;
            if (!downloader(type, bytes)) {
                report.failedType = type;
                report.result = C64RomCacheResult::DownloaderFailed;
                return report;
            }
            report.usedDownloader = true;
            const C64RomCacheResult validation = validateBytes_(type, bytes);
            if (validation != C64RomCacheResult::Ok) {
                report.failedType = type;
                report.result = validation;
                return report;
            }
            const std::string path = canonicalPath_(config.cacheDirectory, type);
            if (!writeFile_(path, bytes)) {
                report.failedType = type;
                report.result = C64RomCacheResult::WriteFailed;
                return report;
            }
            if (!loadIntoPlatform_(platform, type, bytes)) {
                report.failedType = type;
                report.result = C64RomCacheResult::LoadFailed;
                return report;
            }
            markLoaded_(platform, type, path, report.status);
        }

        report.rsidReady = statusReadyForRsid(report.status, config.c64BasicFlag);
        report.fullRomSetReady = statusHasFullSet(report.status);
        report.result = requirementsSatisfied_(report.required, report.status)
            ? C64RomCacheResult::Ok
            : C64RomCacheResult::MissingRequiredRom;
        return report;
    }

    static C64RomPrepareReport importRomBytes(C64Platform& platform,
                                             const std::string& cacheDirectory,
                                             C64RomType type,
                                             const std::vector<uint8_t>& bytes,
                                             bool createCacheDirectory = true,
                                             bool calledFromRealtimeThread = false) {
        C64RomPrepareReport report;
        report.failedType = type;
        if (calledFromRealtimeThread) {
            report.result = C64RomCacheResult::RealtimeUseForbidden;
            return report;
        }
        if (cacheDirectory.empty()) {
            report.result = C64RomCacheResult::MissingCacheDirectory;
            return report;
        }
        if (createCacheDirectory) {
            bool created = false;
            if (!ensureDirectory_(cacheDirectory, created)) {
                report.result = C64RomCacheResult::CacheDirectoryCreateFailed;
                return report;
            }
            report.createdCacheDirectory = created;
        }
        const C64RomCacheResult validation = validateBytes_(type, bytes);
        if (validation != C64RomCacheResult::Ok) {
            report.result = validation;
            return report;
        }
        const std::string path = canonicalPath_(cacheDirectory, type);
        if (!writeFile_(path, bytes)) {
            report.result = C64RomCacheResult::WriteFailed;
            return report;
        }
        if (!loadIntoPlatform_(platform, type, bytes)) {
            report.result = C64RomCacheResult::LoadFailed;
            return report;
        }
        markLoaded_(platform, type, path, report.status);
        // Fill the rest of status from already-cached files after importing this one.
        loadCachedAvailable_(platform, cacheDirectory, report.status);
        report.rsidReady = statusReadyForRsid(report.status, false);
        report.fullRomSetReady = statusHasFullSet(report.status);
        report.result = C64RomCacheResult::Ok;
        return report;
    }

    static bool statusReadyForRsid(const C64RomCacheStatus& s, bool c64BasicFlag) noexcept {
        return s.kernalLoaded && s.characterLoaded && (!c64BasicFlag || s.basicLoaded);
    }

    static bool statusHasFullSet(const C64RomCacheStatus& s) noexcept {
        return s.kernalLoaded && s.basicLoaded && s.characterLoaded;
    }

    static C64RomRequirement requirementsFor(bool isRsid, bool c64BasicFlag, bool enableHle) noexcept {
        C64RomRequirement r;
        r.kernal = isRsid || !enableHle;
        r.basic = (isRsid && c64BasicFlag) || !enableHle;
        r.character = isRsid || !enableHle;
        return r;
    }

    static C64RomCacheStatus cacheStatus(C64Platform& platform, const std::string& cacheDirectory) {
        C64RomCacheStatus status;
        if (!cacheDirectory.empty()) loadCachedAvailable_(platform, cacheDirectory, status);
        return status;
    }

    static C64RomCacheResult validateBytes(C64RomType type, const std::vector<uint8_t>& bytes) noexcept {
        return validateBytes_(type, bytes);
    }

private:
    static bool isRequired_(const C64RomRequirement& r, C64RomType type) noexcept {
        switch (type) {
            case C64RomType::Kernal: return r.kernal;
            case C64RomType::Basic: return r.basic;
            case C64RomType::Character: return r.character;
        }
        return false;
    }

    static bool isLoaded_(const C64RomCacheStatus& s, C64RomType type) noexcept {
        switch (type) {
            case C64RomType::Kernal: return s.kernalLoaded;
            case C64RomType::Basic: return s.basicLoaded;
            case C64RomType::Character: return s.characterLoaded;
        }
        return false;
    }

    static bool requirementsSatisfied_(const C64RomRequirement& r, const C64RomCacheStatus& s) noexcept {
        return (!r.kernal || s.kernalLoaded) && (!r.basic || s.basicLoaded) && (!r.character || s.characterLoaded);
    }

    static void loadCachedAvailable_(C64Platform& platform, const std::string& directory, C64RomCacheStatus& status) {
        loadCachedOne_(platform, directory, C64RomType::Kernal, status);
        loadCachedOne_(platform, directory, C64RomType::Basic, status);
        loadCachedOne_(platform, directory, C64RomType::Character, status);
    }

    static void loadCachedOne_(C64Platform& platform, const std::string& directory, C64RomType type, C64RomCacheStatus& status) {
        std::vector<uint8_t> bytes;
        std::string path;
        bool found = false;
        switch (type) {
            case C64RomType::Kernal:
                found = readAny_(directory, {"kernal.rom", "kernel.rom", "kernal.bin", "kernel.bin", "kernal", "kernel"}, c64RomExpectedSize(type), bytes, path);
                break;
            case C64RomType::Basic:
                found = readAny_(directory, {"basic.rom", "basic.bin", "basic"}, c64RomExpectedSize(type), bytes, path);
                break;
            case C64RomType::Character:
                found = readAny_(directory, {"chargen.rom", "char.rom", "characters.rom", "characters.901225-01.bin", "chargen.bin", "char.bin", "chargen"}, c64RomExpectedSize(type), bytes, path);
                break;
        }
        if (!found) return;
        markCached_(type, path, status);
        if (validateBytes_(type, bytes) != C64RomCacheResult::Ok) return;
        if (!loadIntoPlatform_(platform, type, bytes)) return;
        markLoaded_(platform, type, path, status);
    }

    static std::string canonicalFileName_(C64RomType type) {
        switch (type) {
            case C64RomType::Kernal: return "kernal.rom";
            case C64RomType::Basic: return "basic.rom";
            case C64RomType::Character: return "chargen.rom";
        }
        return "unknown.rom";
    }

    static std::string canonicalPath_(const std::string& directory, C64RomType type) {
        return join_(directory, canonicalFileName_(type).c_str());
    }

    static bool readAny_(const std::string& directory,
                         std::initializer_list<const char*> names,
                         size_t expectedSize,
                         std::vector<uint8_t>& out,
                         std::string& usedPath) {
        for (const char* name : names) {
            const std::string path = join_(directory, name);
            std::ifstream f(path, std::ios::binary);
            if (!f.good()) continue;
            f.seekg(0, std::ios::end);
            const std::streamoff size = f.tellg();
            f.seekg(0, std::ios::beg);
            if (size < 0) continue;
            out.assign(static_cast<size_t>(size), 0);
            if (!out.empty()) f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
            if (!f && !out.empty()) continue;
            if (out.size() != expectedSize) continue;
            usedPath = path;
            return true;
        }
        out.clear();
        return false;
    }

    static C64RomCacheResult validateBytes_(C64RomType type, const std::vector<uint8_t>& bytes) noexcept {
        if (bytes.size() != c64RomExpectedSize(type)) return C64RomCacheResult::BadRomSize;
        const size_t n = bytes.size() < 16 ? bytes.size() : 16;
        size_t printable = 0;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t c = bytes[i];
            if ((c >= 0x20u && c <= 0x7Eu) || c == '\n' || c == '\r' || c == '\t') ++printable;
        }
        if (n >= 4) {
            const char a = static_cast<char>(bytes[0] | 0x20u);
            const char b = static_cast<char>(bytes[1] | 0x20u);
            const char c = static_cast<char>(bytes[2] | 0x20u);
            const char d = static_cast<char>(bytes[3] | 0x20u);
            if ((a == '<' && b == 'h' && c == 't' && d == 'm') ||
                (a == '<' && b == '!' && c == 'd' && d == 'o')) {
                return C64RomCacheResult::HtmlOrTextInsteadOfRom;
            }
        }
        if (n >= 12 && printable == n) return C64RomCacheResult::HtmlOrTextInsteadOfRom;
        return C64RomCacheResult::Ok;
    }

    static bool ensureDirectory_(const std::string& directory, bool& created) noexcept {
        created = false;
        try {
            std::error_code ec;
            if (std::filesystem::exists(directory, ec)) return !ec && std::filesystem::is_directory(directory, ec);
            created = std::filesystem::create_directories(directory, ec);
            return !ec && std::filesystem::is_directory(directory, ec);
        } catch (...) {
            created = false;
            return false;
        }
    }

    static bool writeFile_(const std::string& path, const std::vector<uint8_t>& bytes) {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f.good()) return false;
        if (!bytes.empty()) f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(f);
    }

    static bool loadIntoPlatform_(C64Platform& platform, C64RomType type, const std::vector<uint8_t>& bytes) noexcept {
        switch (type) {
            case C64RomType::Kernal: return platform.loadKernalRom(bytes.data(), bytes.size());
            case C64RomType::Basic: return platform.loadBasicRom(bytes.data(), bytes.size());
            case C64RomType::Character: return platform.loadCharacterRom(bytes.data(), bytes.size());
        }
        return false;
    }

    static void markCached_(C64RomType type, const std::string& path, C64RomCacheStatus& status) {
        switch (type) {
            case C64RomType::Kernal: status.kernalCached = true; status.kernalPath = path; break;
            case C64RomType::Basic: status.basicCached = true; status.basicPath = path; break;
            case C64RomType::Character: status.characterCached = true; status.characterPath = path; break;
        }
    }

    static void markLoaded_(C64Platform& platform, C64RomType type, const std::string& path, C64RomCacheStatus& status) {
        markCached_(type, path, status);
        switch (type) {
            case C64RomType::Kernal:
                status.kernalLoaded = platform.hasExternalKernalRom();
                status.kernalChecksum = platform.kernalRomChecksum();
                break;
            case C64RomType::Basic:
                status.basicLoaded = platform.hasExternalBasicRom();
                status.basicChecksum = platform.basicRomChecksum();
                break;
            case C64RomType::Character:
                status.characterLoaded = platform.hasExternalCharacterRom();
                status.characterChecksum = platform.characterRomChecksum();
                break;
        }
    }

    static std::string join_(const std::string& directory, const char* name) {
        if (directory.empty()) return std::string(name);
        const char last = directory[directory.size() - 1];
        if (last == '/' || last == '\\') return directory + name;
        return directory + "/" + name;
    }
};

} // namespace ArpSID::C64
