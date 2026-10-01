// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID .vstpreset files (Steinberg VST3 preset format), written and read
// with the SDK's PresetFile:
//   - the component (processor) state chunk is a patch-only ArpSID state
//     (Vst3KernelHost::encodePresetState): loading it changes the patch like a
//     program selection and leaves the MIX/KIT/DIGI models, bypass and a
//     loaded tune alone;
//   - no controller state chunk, so a preset never resets the editor's size
//     or tab;
//   - a MetaInfo chunk with the name, plug-in and musical category, as host
//     preset browsers expect.
// Used by the factory preset export tool and by the plug-in (editor preset
// browser and "save preset").
#pragma once

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include "arpsid_preset_paths.h"
#include "arpsid/core/sid_serializer_schema.h"
#include "vst3/arpsid_vst3_kernel_host.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace ArpSID::Presets {

inline std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out += c;
        }
    }
    return out;
}

// MetaInfo XML (the SDK's preset attribute ids). Factory presets mark their
// attributes write-protected; user presets leave them editable.
inline std::string metaInfoXml(const std::string& name, const char* category, const std::string& comment,
                               bool writeProtected) {
    const std::string flags = writeProtected ? "\" type=\"string\" flags=\"writeProtected\"/>\n"
                                             : "\" type=\"string\" flags=\"\"/>\n";
    auto attr = [&](const char* id, const std::string& value) {
        return std::string("\t<Attr id=\"") + id + "\" value=\"" + xmlEscape(value) + flags;
    };
    std::string x = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<MetaInfo>\n";
    x += attr("MediaType", "VstPreset");
    x += attr("PlugInName", kPluginName);
    x += attr("PlugInCategory", "Instrument|Synth");
    x += attr("Name", name);
    if (category && *category) {
        x += attr("MusicalCategory", category);
        x += attr("MusicalInstrument", category);
    }
    if (!comment.empty()) x += attr("Comment", comment);
    x += "</MetaInfo>\n";
    return x;
}

// A complete .vstpreset image holding <componentState>; empty on failure.
inline std::vector<char> buildPresetFile(const Steinberg::FUID& classId, const std::vector<std::uint8_t>& componentState,
                                         const std::string& metaXml) {
    using namespace Steinberg;
    std::vector<char> out;
    if (componentState.empty()) return out;
    IPtr<MemoryStream> comp = owned(new MemoryStream);
    int32 written = 0;
    if (comp->write(const_cast<std::uint8_t*>(componentState.data()), (int32)componentState.size(), &written) !=
            kResultOk ||
        written != (int32)componentState.size())
        return out;
    comp->seek(0, IBStream::kIBSeekSet, nullptr);
    IPtr<MemoryStream> file = owned(new MemoryStream);
    if (!Vst::PresetFile::savePreset(file, classId, comp, nullptr, metaXml.empty() ? nullptr : metaXml.data(),
                                     (int32)metaXml.size()))
        return out;
    out.assign(file->getData(), file->getData() + file->getSize());
    return out;
}

// Write <image> to <path>, creating its folder. False with a reason on error.
inline bool writeFileBytes(const std::filesystem::path& path, const std::vector<char>& image, std::string* error) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (f) f.write(image.data(), (std::streamsize)image.size());
    if (!f) {
        if (error) *error = "cannot write " + pathToUtf8(path);
        return false;
    }
    return true;
}

// The component state chunk of a .vstpreset image, if it was saved for
// <classId>. Also returns the MetaInfo "Name" when present.
inline bool readPresetComponentState(const std::vector<char>& image, const Steinberg::FUID& classId,
                                     std::vector<std::uint8_t>& componentState, std::string* metaName = nullptr) {
    using namespace Steinberg;
    componentState.clear();
    if (image.size() < 48 || image.size() > (std::size_t)64 * 1024 * 1024) return false;
    IPtr<MemoryStream> in = owned(new MemoryStream(const_cast<char*>(image.data()), (TSize)image.size()));
    Vst::PresetFile pf(in);
    if (!pf.readChunkList() || pf.getClassID() != classId) return false;
    const Vst::PresetFile::Entry* e = pf.getEntry(Vst::kComponentState);
    if (!e || e->size <= 0 || e->offset < 0 || e->offset + e->size > (TSize)image.size()) return false;
    const auto* p = reinterpret_cast<const std::uint8_t*>(image.data() + e->offset);
    componentState.assign(p, p + e->size);
    if (metaName) {
        metaName->clear();
        if (const Vst::PresetFile::Entry* m = pf.getEntry(Vst::kMetaInfo)) {
            if (m->size > 0 && m->offset >= 0 && m->offset + m->size <= (TSize)image.size()) {
                const std::string xml(image.data() + m->offset, (std::size_t)m->size);
                const std::string key = "id=\"Name\" value=\"";
                const auto at = xml.find(key);
                if (at != std::string::npos) {
                    const auto from = at + key.size();
                    const auto to = xml.find('"', from);
                    if (to != std::string::npos) {
                        std::string v = xml.substr(from, to - from);
                        // Undo xmlEscape.
                        const std::pair<const char*, char> ents[] = {
                            {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
                        std::string u;
                        for (std::size_t i = 0; i < v.size();) {
                            bool hit = false;
                            for (const auto& en : ents) {
                                const std::size_t n = std::char_traits<char>::length(en.first);
                                if (v.compare(i, n, en.first) == 0) {
                                    u += en.second;
                                    i += n;
                                    hit = true;
                                    break;
                                }
                            }
                            if (!hit) u += v[i++];
                        }
                        *metaName = u;
                    }
                }
            }
        }
    }
    return true;
}

inline bool readFileBytes(const std::filesystem::path& path, std::vector<char>& bytes,
                          std::size_t maxBytes = (std::size_t)64 * 1024 * 1024) {
    bytes.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (bytes.size() > maxBytes) bytes.clear();
    return !bytes.empty();
}

// Save <root> as a user .vstpreset at <path> (UTF-8); the file name is the
// preset name. False with a reason on failure.
inline bool savePatchPresetFile(const std::string& path, const Steinberg::FUID& classId, const SidStateRootV1& root,
                                std::string& error) {
    const std::filesystem::path p = pathFromUtf8(path);
    const std::vector<std::uint8_t> state = Vst3KernelHost::encodePresetState(root);
    if (state.empty()) {
        error = "no patch to save";
        return false;
    }
    const std::vector<char> image = buildPresetFile(classId, state, metaInfoXml(presetNameFromPath(p), "Synth", {}, false));
    if (image.empty()) {
        error = "cannot encode the preset";
        return false;
    }
    return writeFileBytes(p, image, &error);
}

// Read the patch of an ArpSID .vstpreset (patch-only or full state). <name>
// is the file name (else the preset's MetaInfo name).
inline bool loadPatchPresetFile(const std::string& path, const Steinberg::FUID& classId, SidStateRootV1& root,
                                std::string& name, std::string& error) {
    const std::filesystem::path p = pathFromUtf8(path);
    std::vector<char> image;
    if (!readFileBytes(p, image)) {
        error = "cannot read " + path;
        return false;
    }
    std::vector<std::uint8_t> comp;
    std::string metaName;
    if (!readPresetComponentState(image, classId, comp, &metaName)) {
        error = "not an ArpSID preset";
        return false;
    }
    if (!Vst3KernelHost::decodeStateRoot(comp.data(), comp.size(), root)) {
        error = "preset holds no ArpSID patch";
        return false;
    }
    name = presetNameFromPath(p);
    if (name.empty()) name = metaName;
    return true;
}

} // namespace ArpSID::Presets
