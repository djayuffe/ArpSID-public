// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — what the cross-platform editor needs from its host.
//
// The VST3 edit controller implements this for the plug-in; the offscreen
// snapshot tool implements it over a Vst3KernelHost so the editor can be
// rendered and tested without a DAW.
#pragma once

#include <cstdint>
#include <string>

namespace ArpSID {

class Vst3KernelHost;
struct SidStateRootV1;

struct EditorBackend {
    virtual ~EditorBackend() = default;

    // Parameters (normalized 0..1).
    virtual float param(int id) const = 0;
    virtual void beginEdit(int id) = 0;
    virtual void performEdit(int id, float normalized) = 0;
    virtual void endEdit(int id) = 0;
    virtual std::string paramText(int id, float normalized) const = 0;

    // Factory patches.
    virtual void selectFactoryPatch(int slot) = 0;
    virtual int currentFactorySlot() const = 0;

    // Patches that are not factory slots (user bank, patch files, presets).
    // loadPatch applies <root> through the host and names it; patchName is
    // the current patch's name (user or factory) and isUserPatch tells which.
    virtual void loadPatch(const SidStateRootV1& root, const std::string& name) { (void)root; (void)name; }
    virtual std::string patchName() const { return {}; }
    virtual bool isUserPatch() const { return false; }
    // .vstpreset files (UTF-8 paths). False with a reason on failure.
    virtual bool savePresetFile(const std::string& path, std::string& error) {
        (void)path;
        error = "presets are not available here";
        return false;
    }
    virtual bool loadPresetFile(const std::string& path, std::string& error) {
        (void)path;
        error = "presets are not available here";
        return false;
    }

    // Editor keyboard.
    virtual void sendMidi(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) = 0;

    // The processor's kernel host (models, telemetry, SID player), or nullptr
    // when the processor runs out of process.
    virtual Vst3KernelHost* kernelHost() = 0;

    // A GUI model (SETTINGS/MIX/KIT/DIGI) changed: the host should save.
    virtual void markStateDirty() = 0;

    // Ask the host to resize the editor window (optional).
    virtual bool requestResize(int width, int height) { (void)width; (void)height; return false; }

    // Tab shown when the editor opens; the host keeps it between openings
    // and in the project (VST3 controller state).
    virtual int savedTab() const { return 0; }
    virtual void tabChanged(int visibleIndex) { (void)visibleIndex; }

    // Right-click on a parameter control: show the host's menu for that
    // parameter (automation, MIDI learn, ...) plus "Reset to Default".
    // x/y are editor coordinates. False when the host offers no menu.
    virtual bool paramContextMenu(int id, double x, double y) { (void)id; (void)x; (void)y; return false; }

    // Host track the plug-in sits on (VST3 IInfoListener); empty / 0 when the
    // host does not say. Colour is 0xAARRGGBB.
    virtual std::string trackName() const { return {}; }
    virtual std::uint32_t trackColour() const { return 0; }

    // Host bypass state (shown in the header).
    virtual bool bypassed() const { return false; }
};

// Convenience edit: begin + perform + end.
inline void editorSetParam(EditorBackend& b, int id, float normalized) {
    b.beginEdit(id);
    b.performEdit(id, normalized);
    b.endEdit(id);
}

} // namespace ArpSID
