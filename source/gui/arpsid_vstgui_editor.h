// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — thin VST3 host view for the canonical native Cocoa GUI.
#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "arpsid_vst_cocoa_bridge.h"

class ArpSIDVSTGUIEditor final : public Steinberg::Vst::EditorView {
public:
    explicit ArpSIDVSTGUIEditor(void* controller);
    ~ArpSIDVSTGUIEditor() override;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API removed() override;
    Steinberg::tresult PLUGIN_API getSize(Steinberg::ViewRect* size) override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;
    Steinberg::tresult PLUGIN_API canResize() override;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;

private:
    void closeNativeView_() noexcept;

    void* nativeCocoaHandle_ = nullptr;
};
