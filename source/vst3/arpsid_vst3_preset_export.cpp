// Copyright (C) 2024-2026 Ulf Bertilsson
// Writes the factory patches as standard VST3 preset files (.vstpreset).
//
// Hosts whose preset browser reads files from disk do not read the plug-in's
// program list; they look for .vstpreset files in the VST3 preset folders:
//
//   Linux    ~/.vst3/presets/<Vendor>/<Plug-in>/          (user)
//            /usr/share/vst3/presets/<Vendor>/<Plug-in>/  (system)
//   Windows  %USERPROFILE%/Documents/VST3 Presets/<Vendor>/<Plug-in>/
//            %PROGRAMDATA%/VST3 Presets/<Vendor>/<Plug-in>/
//   macOS    ~/Library/Audio/Presets/<Vendor>/<Plug-in>/
//            /Library/Audio/Presets/<Vendor>/<Plug-in>/
//
// The tool loads the built plug-in exactly as a host does, selects every
// factory program through the controller, saves the processor state with the
// SDK's PresetFile writer (plus MetaInfo: name, category, description) and
// then loads each file back into a fresh instance to prove it restores that
// patch. One sub-folder per patch role (Bass, Lead, Pad, ...).
//
// usage: arpsid_vst3_preset_export <arpsid_vst3.vst3> <output-dir>
//   <output-dir> receives <Vendor>/<Plug-in>/<Role>/<Patch name>.vstpreset

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include "parameter_ids.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "factory_patch_params.h"
#include "vst3/arpsid_vst3_kernel_host.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace fs = std::filesystem;

namespace {

constexpr const char* kVendor = "Uber Sound Solutions";
constexpr const char* kPluginName = "ArpSID";

const char* roleFolder(ArpSID::PatchRole role) {
    switch (role) {
        case ArpSID::PatchRole::Init: return "Init";
        case ArpSID::PatchRole::Bass: return "Bass";
        case ArpSID::PatchRole::Lead: return "Lead";
        case ArpSID::PatchRole::Arp: return "Arp";
        case ArpSID::PatchRole::Chord: return "Keys";
        case ArpSID::PatchRole::PadIllusion: return "Pad";
        case ArpSID::PatchRole::Bell: return "Bell";
        case ArpSID::PatchRole::Metallic: return "Metallic";
        case ArpSID::PatchRole::Drum: return "Drums";
        case ArpSID::PatchRole::FX: return "FX";
        case ArpSID::PatchRole::Utility: return "Utility";
    }
    return "Other";
}

// VST3 MusicalCategory ("Synth|Lead" etc., as in the SDK's preset metadata).
const char* musicalCategory(ArpSID::PatchRole role) {
    switch (role) {
        case ArpSID::PatchRole::Bass: return "Synth|Bass";
        case ArpSID::PatchRole::Lead: return "Synth|Lead";
        case ArpSID::PatchRole::Arp: return "Synth|Arp";
        case ArpSID::PatchRole::Chord: return "Keys|Synth";
        case ArpSID::PatchRole::PadIllusion: return "Synth|Pad";
        case ArpSID::PatchRole::Bell: return "Mallet|Bell";
        case ArpSID::PatchRole::Metallic: return "Synth|Metallic";
        case ArpSID::PatchRole::Drum: return "Drum&Perc";
        case ArpSID::PatchRole::FX: return "Sound FX";
        default: return "Synth";
    }
}

// A file name every file system accepts.
std::string fileSafe(const std::string& name) {
    std::string out;
    for (char c : name) {
        const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                         c == '>' || c == '|' || static_cast<unsigned char>(c) < 0x20;
        out += bad ? '-' : c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? std::string("Patch") : out;
}

std::string xmlEscape(const std::string& s) {
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

std::string metaInfoXml(const std::string& name, const char* category, const std::string& comment) {
    auto attr = [](const char* id, const std::string& value) {
        return std::string("\t<Attr id=\"") + id + "\" value=\"" + xmlEscape(value) +
               "\" type=\"string\" flags=\"writeProtected\"/>\n";
    };
    std::string x = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<MetaInfo>\n";
    x += attr("MediaType", "VstPreset");
    x += attr("PlugInName", kPluginName);
    x += attr("PlugInCategory", "Instrument|Synth");
    x += attr("Name", name);
    x += attr("MusicalCategory", category);
    x += attr("MusicalInstrument", category);
    if (!comment.empty()) x += attr("Comment", comment);
    x += "</MetaInfo>\n";
    return x;
}

struct Instance {
    IPtr<IComponent> component;
    IPtr<IEditController> controller;
    FUnknownPtr<IConnectionPoint> procCp, ctrlCp;
};

bool makeInstance(const VST3::Hosting::PluginFactory& factory, FUnknown* host, Instance& out, VST3::UID& cid) {
    for (const auto& ci : factory.classInfos())
        if (ci.category() == kVstAudioEffectClass) {
            cid = ci.ID();
            out.component = factory.createInstance<IComponent>(ci.ID());
            break;
        }
    if (!out.component || out.component->initialize(host) != kResultOk) return false;
    TUID ctrlCid{};
    if (out.component->getControllerClassId(ctrlCid) != kResultOk) return false;
    out.controller = factory.createInstance<IEditController>(VST3::UID::fromTUID(ctrlCid));
    if (!out.controller || out.controller->initialize(host) != kResultOk) return false;
    out.procCp = FUnknownPtr<IConnectionPoint>(out.component);
    out.ctrlCp = FUnknownPtr<IConnectionPoint>(out.controller);
    if (!out.procCp || !out.ctrlCp) return false;
    out.procCp->connect(out.ctrlCp);
    out.ctrlCp->connect(out.procCp);
    return true;
}

void destroyInstance(Instance& in) {
    if (in.procCp && in.ctrlCp) {
        in.procCp->disconnect(in.ctrlCp);
        in.ctrlCp->disconnect(in.procCp);
    }
    if (in.controller) in.controller->terminate();
    if (in.component) in.component->terminate();
    in = Instance{};
}

std::uint32_t getU32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
}

// The processor's full state reduced to a patch-only preset state: the
// preset marker and the ROOT chunk (the patch), without the GUI models,
// bypass or a loaded tune, so loading it acts like a program selection.
bool presetStateFrom(IComponent* component, std::vector<std::uint8_t>& out) {
    IPtr<MemoryStream> full = owned(new MemoryStream);
    if (component->getState(full) != kResultOk) return false;
    const auto* data = reinterpret_cast<const std::uint8_t*>(full->getData());
    const std::size_t size = (std::size_t)full->getSize();
    if (size < 4 || getU32(data) != ArpSID::kVst3StateVersion) return false;
    out.clear();
    putU32(out, ArpSID::kVst3StateVersion);
    putU32(out, ArpSID::kVst3StateTagPreset);
    putU32(out, 0u);
    for (std::size_t pos = 4; pos + 8 <= size;) {
        const std::uint32_t tag = getU32(data + pos), len = getU32(data + pos + 4);
        pos += 8;
        if (len > size - pos) return false;
        if (tag == ArpSID::kVst3StateTagRoot) {
            putU32(out, tag);
            putU32(out, len);
            out.insert(out.end(), data + pos, data + pos + len);
            return true;
        }
        pos += len;
    }
    return false;
}

bool readFile(const fs::path& p, std::vector<char>& bytes) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !bytes.empty();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <arpsid_vst3.vst3> <output-dir>\n", argv[0]);
        return 2;
    }
    std::string err;
    auto module = VST3::Hosting::Module::create(argv[1], err);
    if (!module) {
        std::fprintf(stderr, "cannot load module: %s\n", err.c_str());
        return 2;
    }
    IPtr<HostApplication> host = owned(new HostApplication);
    const auto& factory = module->getFactory();

    Instance src;
    VST3::UID cid;
    if (!makeInstance(factory, host, src, cid)) {
        std::fprintf(stderr, "cannot instantiate ArpSID\n");
        return 1;
    }
    const FUID classId = FUID::fromTUID(cid.data());

    const fs::path root = fs::path(argv[2]) / kVendor / kPluginName;
    std::error_code ec;
    fs::remove_all(root, ec); // regenerate: no stale files from renamed patches
    int written = 0, failures = 0;

    for (int slot = 0; slot <= ArpSID::kCanonicalFactoryPatchSlotMax; ++slot) {
        const ArpSID::PatchDefinition* def = ArpSID::getFactoryPatchDefinition(slot);
        const std::string name = ArpSID::factoryPatchNameForSlot(slot);
        const ArpSID::PatchRole role = def ? def->usage.role : ArpSID::PatchRole::Utility;
        const ParamValue norm = ArpSID::canonicalNormalizedFactoryProgramValue(slot);

        // Select the program the way a host program list does.
        src.controller->setParamNormalized((ParamID)ArpSID::kParamProgram, norm);

        const fs::path dir = root / roleFolder(role);
        fs::create_directories(dir, ec);
        const fs::path file = dir / (fileSafe(name) + ".vstpreset");
        const std::string xml = metaInfoXml(name, musicalCategory(role), ArpSID::factoryPatchDescriptionForSlot(slot));

        // Patch-only processor state and no controller state: loading a
        // preset must not reset the editor's size or tab, the MIX/KIT/DIGI
        // models, bypass or a loaded tune; the controller follows the
        // processor state through setComponentState.
        std::vector<std::uint8_t> state;
        IPtr<MemoryStream> stream = owned(new MemoryStream);
        bool saved = presetStateFrom(src.component, state);
        if (saved) {
            IPtr<MemoryStream> comp = owned(new MemoryStream);
            int32 w = 0;
            comp->write(state.data(), (int32)state.size(), &w);
            comp->seek(0, IBStream::kIBSeekSet, nullptr);
            saved = PresetFile::savePreset(stream, classId, comp, nullptr, xml.data(), (int32)xml.size());
        }
        if (!saved) {
            std::fprintf(stderr, "slot %d (%s): savePreset failed\n", slot, name.c_str());
            ++failures;
            continue;
        }
        {
            std::ofstream f(file, std::ios::binary | std::ios::trunc);
            f.write(stream->getData(), (std::streamsize)stream->getSize());
            if (!f) {
                std::fprintf(stderr, "cannot write %s\n", file.string().c_str());
                ++failures;
                continue;
            }
        }

        // Round trip: a fresh instance loads the file and reports this patch.
        std::vector<char> bytes;
        Instance dst;
        VST3::UID dstCid;
        bool ok = readFile(file, bytes) && makeInstance(factory, host, dst, dstCid);
        if (ok) {
            IPtr<MemoryStream> in = owned(new MemoryStream(bytes.data(), (TSize)bytes.size()));
            ok = PresetFile::loadPreset(in, classId, dst.component, dst.controller);
        }
        if (ok) {
            const double slotNorm = dst.controller->getParamNormalized((ParamID)ArpSID::kParamBankSlot);
            ok = std::fabs(slotNorm - ArpSID::canonicalNormalizedBankSlotValue(slot)) < 1e-6;
            // Every host-visible value matches the controller that selected it.
            for (int pid = 0; ok && pid < ArpSID::kNumParams; ++pid) {
                if (ArpSID::isRuntimeOnlyOrTransientParam(pid)) continue;
                ok = std::fabs(dst.controller->getParamNormalized((ParamID)pid) -
                               src.controller->getParamNormalized((ParamID)pid)) < 1e-5;
                if (!ok) std::fprintf(stderr, "slot %d: parameter %d differs after reload\n", slot, pid);
            }
        }
        destroyInstance(dst);
        if (!ok) {
            std::fprintf(stderr, "slot %d (%s): preset does not restore the patch\n", slot, name.c_str());
            ++failures;
            continue;
        }
        ++written;
    }
    destroyInstance(src);

    std::printf("wrote %d VST3 presets to %s\n", written, root.string().c_str());
    if (failures) {
        std::fprintf(stderr, "%d preset(s) failed\n", failures);
        return 1;
    }
    return 0;
}
