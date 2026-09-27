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

    // Editor keyboard.
    virtual void sendMidi(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) = 0;

    // The processor's kernel host (models, telemetry, SID player), or nullptr
    // when the processor runs out of process.
    virtual Vst3KernelHost* kernelHost() = 0;

    // A GUI model (SETTINGS/MIX/KIT/DIGI) changed: the host should save.
    virtual void markStateDirty() = 0;

    // Ask the host to resize the editor window (optional).
    virtual bool requestResize(int width, int height) { (void)width; (void)height; return false; }
};

// Convenience edit: begin + perform + end.
inline void editorSetParam(EditorBackend& b, int id, float normalized) {
    b.beginEdit(id);
    b.performEdit(id, normalized);
    b.endEdit(id);
}

} // namespace ArpSID
