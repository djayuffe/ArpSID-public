// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VST3 plug-in view for the cross-platform (VSTGUI) editor.
//
// Hosts Editor::EditorView in a VSTGUI frame on Linux (X11) and Windows
// (HWND), drives its 30 Hz refresh, and adapts the edit controller to the
// editor's backend interface.

#include "gui/vstgui/arpsid_editor_view.h"
#include "gui/arpsid_vst_cocoa_bridge.h"

#include "arpsid/core/sid_parameter_presentation.h"
#include "parameter_ids.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstguieditor.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"

#include <algorithm>
#include <cmath>

namespace ArpSID {

namespace {

using Steinberg::Vst::EditController;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

class ControllerBackend final : public EditorBackend {
public:
    explicit ControllerBackend(EditController* c) : c_(c) {}

    float param(int id) const override {
        return static_cast<float>(c_->getParamNormalized(static_cast<ParamID>(id)));
    }
    void beginEdit(int id) override { c_->beginEdit(static_cast<ParamID>(id)); }
    void performEdit(int id, float v) override {
        const ParamValue pv = std::clamp(static_cast<ParamValue>(v), 0.0, 1.0);
        c_->setParamNormalized(static_cast<ParamID>(id), pv);
        c_->performEdit(static_cast<ParamID>(id), pv);
    }
    void endEdit(int id) override { c_->endEdit(static_cast<ParamID>(id)); }
    std::string paramText(int id, float v) const override {
        char buf[64] = {};
        if (!SidParameterPresentation::formatNormalized(id, v, buf, sizeof buf)) return {};
        return buf;
    }
    void selectFactoryPatch(int slot) override { arpsidControllerSelectFactoryPatch(c_, slot); }
    int currentFactorySlot() const override { return arpsidControllerLoadedFactorySlot(c_); }
    void sendMidi(uint8_t s, uint8_t d1, uint8_t d2) override { arpsidControllerSendUiMidi(c_, s, d1, d2); }
    Vst3KernelHost* kernelHost() override { return arpsidControllerKernelHost(c_); }
    void markStateDirty() override { arpsidControllerMarkStateDirty(c_); }

private:
    EditController* c_;
};

class CrossPlatformEditor final : public Steinberg::Vst::VSTGUIEditor,
                                  public Steinberg::IPlugViewContentScaleSupport {
public:
    explicit CrossPlatformEditor(EditController* controller)
        : VSTGUIEditor(controller, nullptr), backend_(controller) {
        Steinberg::ViewRect r(0, 0, static_cast<Steinberg::int32>(Editor::EditorView::kWidth),
                              static_cast<Steinberg::int32>(Editor::EditorView::kHeight));
        setRect(r);
    }

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override {
        if (frame) return false;
        const VSTGUI::CRect size(0, 0, Editor::EditorView::kWidth, Editor::EditorView::kHeight);
        frame = new VSTGUI::CFrame(size, this);
        frame->setTransparency(false);
        view_ = VSTGUI::makeOwned<Editor::EditorView>(backend_);
        frame->addView(view_);
        view_->remember();
        if (!frame->open(parent, platformType)) {
            close();
            return false;
        }
        applyScale_();
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*) {
            if (view_) view_->refresh();
        }, 33);
        return true;
    }

    void PLUGIN_API close() override {
        if (timer_) {
            timer_->stop();
            timer_ = nullptr;
        }
        if (frame) {
            frame->forget();
            frame = nullptr;
        }
        view_ = nullptr;
    }

    // IPlugViewContentScaleSupport (Windows / Linux HiDPI).
    Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override {
        scale_ = std::clamp(static_cast<double>(factor), 0.5, 4.0);
        applyScale_();
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API canResize() override { return Steinberg::kResultFalse; }

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(VSTGUIEditor)
    REFCOUNT_METHODS(VSTGUIEditor)

private:
    void applyScale_() {
        if (!frame) return;
        frame->setZoom(scale_);
        Steinberg::ViewRect r(0, 0, static_cast<Steinberg::int32>(std::lround(Editor::EditorView::kWidth * scale_)),
                              static_cast<Steinberg::int32>(std::lround(Editor::EditorView::kHeight * scale_)));
        if (plugFrame && (r.getWidth() != getRect().getWidth() || r.getHeight() != getRect().getHeight()))
            plugFrame->resizeView(this, &r);
        setRect(r);
    }

    ControllerBackend backend_;
    VSTGUI::SharedPointer<Editor::EditorView> view_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    double scale_ = 1.0;
};

} // namespace

Steinberg::IPlugView* arpsidCreateCrossPlatformEditor(void* editController) {
    auto* c = static_cast<EditController*>(editController);
    return c ? new CrossPlatformEditor(c) : nullptr;
}

} // namespace ArpSID
