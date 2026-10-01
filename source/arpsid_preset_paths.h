// Copyright (C) 2024-2026 Ulf Bertilsson
// VST3 preset locations and preset metadata shared by the plug-in (editor
// preset browser, user preset save), the factory .vstpreset export tool and
// the installers' layout:
//
//   Linux    ~/.vst3/presets/<Vendor>/<Plug-in>/          (user)
//            /usr/share/vst3/presets/<Vendor>/<Plug-in>/  (system)
//   Windows  <Documents>/VST3 Presets/<Vendor>/<Plug-in>/  (user)
//            %PROGRAMDATA%/VST3 Presets/<Vendor>/<Plug-in>/
//   macOS    ~/Library/Audio/Presets/<Vendor>/<Plug-in>/
//            /Library/Audio/Presets/<Vendor>/<Plug-in>/
//
// Factory presets live in one sub-folder per patch role (Bass, Lead, ...);
// presets saved from the editor go to the user folder's "User" sub-folder.
#pragma once

#include "arpsid/patchbank/forensic_patch_bank.h"
#include "parameter_ids.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#if defined(_MSC_VER)
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif
#endif

namespace ArpSID::Presets {

inline constexpr const char* kVendor = "Uber Sound Solutions";
inline constexpr const char* kPluginName = "ArpSID";
inline constexpr const char* kUserSubfolder = "User";
inline constexpr const char* kFileExtension = ".vstpreset";

// UTF-8 text <-> filesystem path (Windows paths are UTF-16).
inline std::filesystem::path pathFromUtf8(const std::string& utf8) {
#if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
#else
    return std::filesystem::u8path(utf8);
#endif
}

inline std::string pathToUtf8(const std::filesystem::path& p) {
#if defined(__cpp_char8_t)
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
#else
    return p.u8string();
#endif
}

namespace detail {
inline std::filesystem::path envPath(const char* name) {
#if defined(_WIN32)
    wchar_t buf[32768];
    std::wstring wname;
    for (const char* c = name; *c; ++c) wname.push_back(static_cast<wchar_t>(*c));
    const DWORD n = GetEnvironmentVariableW(wname.c_str(), buf, 32768);
    return (n > 0 && n < 32768) ? std::filesystem::path(std::wstring(buf, n)) : std::filesystem::path();
#else
    const char* v = std::getenv(name);
    return (v && *v) ? std::filesystem::path(v) : std::filesystem::path();
#endif
}
} // namespace detail

// <base>/<Vendor>/<Plug-in>
inline std::filesystem::path pluginFolderIn(const std::filesystem::path& base) {
    return base.empty() ? base : base / kVendor / kPluginName;
}

// The current user's ArpSID preset folder (empty when unknown).
inline std::filesystem::path userPresetFolder() {
#if defined(_WIN32)
    std::filesystem::path docs;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p)) && p) docs = p;
    if (p) CoTaskMemFree(p);
    if (docs.empty()) docs = detail::envPath("USERPROFILE") / "Documents";
    return pluginFolderIn(docs / "VST3 Presets");
#elif defined(__APPLE__)
    const auto home = detail::envPath("HOME");
    return home.empty() ? home : pluginFolderIn(home / "Library" / "Audio" / "Presets");
#else
    const auto home = detail::envPath("HOME");
    return home.empty() ? home : pluginFolderIn(home / ".vst3" / "presets");
#endif
}

// The all-users ArpSID preset folder (factory presets of a system install).
inline std::filesystem::path systemPresetFolder() {
#if defined(_WIN32)
    const auto pd = detail::envPath("PROGRAMDATA");
    return pd.empty() ? pd : pluginFolderIn(pd / "VST3 Presets");
#elif defined(__APPLE__)
    return pluginFolderIn("/Library/Audio/Presets");
#else
    return pluginFolderIn("/usr/share/vst3/presets");
#endif
}

// Where the editor saves a new preset.
inline std::filesystem::path userSavePresetFolder() {
    const auto u = userPresetFolder();
    return u.empty() ? u : u / kUserSubfolder;
}

// Factory sub-folder of a patch role.
inline const char* roleFolder(PatchRole role) noexcept {
    switch (role) {
        case PatchRole::Init: return "Init";
        case PatchRole::Bass: return "Bass";
        case PatchRole::Lead: return "Lead";
        case PatchRole::Arp: return "Arp";
        case PatchRole::Chord: return "Keys";
        case PatchRole::PadIllusion: return "Pad";
        case PatchRole::Bell: return "Bell";
        case PatchRole::Metallic: return "Metallic";
        case PatchRole::Drum: return "Drums";
        case PatchRole::FX: return "FX";
        case PatchRole::Utility: return "Utility";
    }
    return "Other";
}

// VST3 MusicalCategory of a patch role ("Synth|Lead" etc.).
inline const char* musicalCategory(PatchRole role) noexcept {
    switch (role) {
        case PatchRole::Bass: return "Synth|Bass";
        case PatchRole::Lead: return "Synth|Lead";
        case PatchRole::Arp: return "Synth|Arp";
        case PatchRole::Chord: return "Keys|Synth";
        case PatchRole::PadIllusion: return "Synth|Pad";
        case PatchRole::Bell: return "Mallet|Bell";
        case PatchRole::Metallic: return "Synth|Metallic";
        case PatchRole::Drum: return "Drum&Perc";
        case PatchRole::FX: return "Sound FX";
        default: return "Synth";
    }
}

// A file name every file system accepts (UTF-8 kept).
inline std::string fileSafeName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                         c == '>' || c == '|' || static_cast<unsigned char>(c) < 0x20;
        out += bad ? '-' : c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out.empty() ? std::string("Patch") : out;
}

// Preset name of a file: its name without ".vstpreset".
inline std::string presetNameFromPath(const std::filesystem::path& p) { return pathToUtf8(p.stem()); }

struct PresetFileEntry {
    std::filesystem::path path;
    std::string name;     // file stem
    std::string category; // sub-folder below the plug-in folder ("" at top level)
};

// All .vstpreset files below <folder> (recursively), sorted by category and
// name. Never throws; an unreadable folder yields what was found so far.
inline std::vector<PresetFileEntry> listPresetFiles(const std::filesystem::path& folder, std::size_t maxFiles = 4096) {
    std::vector<PresetFileEntry> out;
    std::error_code ec;
    if (folder.empty() || !std::filesystem::is_directory(folder, ec)) return out;
    std::filesystem::recursive_directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end && out.size() < maxFiles; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        const auto& p = it->path();
        std::string ext = pathToUtf8(p.extension());
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        if (ext != kFileExtension) continue;
        PresetFileEntry e;
        e.path = p;
        e.name = presetNameFromPath(p);
        const auto rel = p.parent_path().lexically_relative(folder);
        e.category = (rel.empty() || rel == ".") ? std::string() : pathToUtf8(rel.generic_string());
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const PresetFileEntry& a, const PresetFileEntry& b) {
        return a.category != b.category ? a.category < b.category : a.name < b.name;
    });
    return out;
}

// True for an installed factory preset: <Role>/<factory patch name>.vstpreset.
inline bool isFactoryPresetEntry(const PresetFileEntry& e) {
    for (int slot = 0; slot <= kCanonicalFactoryPatchSlotMax; ++slot) {
        const PatchDefinition* def = getFactoryPatchDefinition(slot);
        if (!def) continue;
        if (e.category == roleFolder(def->usage.role) && e.name == fileSafeName(factoryPatchNameForSlot(slot)))
            return true;
    }
    return false;
}

// The presets a user made or added: every .vstpreset in the user folder that
// is not one of the installed factory presets.
inline std::vector<PresetFileEntry> listUserPresets() {
    std::vector<PresetFileEntry> all = listPresetFiles(userPresetFolder());
    all.erase(std::remove_if(all.begin(), all.end(), [](const PresetFileEntry& e) { return isFactoryPresetEntry(e); }),
              all.end());
    return all;
}

} // namespace ArpSID::Presets
