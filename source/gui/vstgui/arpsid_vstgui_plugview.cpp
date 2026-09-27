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
        : VSTGUIEditor(controller, nullptr), backend_(controller), controller_(controller) {
        zoom_ = clampZoom_(arpsidControllerEditorZoom(controller));
        Steinberg::ViewRect r(0, 0, static_cast<Steinberg::int32>(std::lround(Editor::EditorView::kWidth * zoom_)),
                              static_cast<Steinberg::int32>(std::lround(Editor::EditorView::kHeight * zoom_)));
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
        applyZoom_(false);
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

    // IPlugViewContentScaleSupport (Windows / Linux HiDPI). The host's scale
    // multiplies the user's size choice, so a resized editor keeps its size
    // relative to the screen when the scale changes.
    Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override {
        const double s = std::clamp(static_cast<double>(factor), 0.5, 4.0);
        if (std::fabs(s - hostScale_) < 1e-6) return Steinberg::kResultTrue;
        zoom_ = clampZoom_(zoom_ / hostScale_ * s);
        hostScale_ = s;
        applyZoom_(true);
        return Steinberg::kResultTrue;
    }

    // Resizing scales the whole editor (fixed 3:2 layout) between 0.5x and
    // 3x of its 1200 x 800 design size.
    Steinberg::tresult PLUGIN_API canResize() override { return Steinberg::kResultTrue; }

    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* r) override {
        if (!r) return Steinberg::kInvalidArgument;
        const double z = zoomForRect_(*r);
        r->right = r->left + static_cast<Steinberg::int32>(std::lround(Editor::EditorView::kWidth * z));
        r->bottom = r->top + static_cast<Steinberg::int32>(std::lround(Editor::EditorView::kHeight * z));
        return Steinberg::kResultTrue;
    }

    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* r) override {
        if (!r) return Steinberg::kInvalidArgument;
        zoom_ = zoomForRect_(*r);
        arpsidControllerSetEditorZoom(controller_, zoom_ / hostScale_);
        if (frame) frame->setZoom(zoom_);
        // EditorView (not VSTGUIEditor::onSize): the frame is sized by its zoom.
        return Steinberg::Vst::EditorView::onSize(r);
    }

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::IPlugViewContentScaleSupport)
    END_DEFINE_INTERFACES(VSTGUIEditor)
    REFCOUNT_METHODS(VSTGUIEditor)

private:
    static constexpr double kMinZoom = 0.5, kMaxZoom = 3.0;
    static double clampZoom_(double z) { return std::clamp(z, kMinZoom, kMaxZoom); }
    // The largest zoom whose 3:2 editor fits the offered rect.
    static double zoomForRect_(const Steinberg::ViewRect& r) {
        const double zw = r.getWidth() / Editor::EditorView::kWidth;
        const double zh = r.getHeight() / Editor::EditorView::kHeight;
        return clampZoom_(std::min(zw, zh));
    }

    void applyZoom_(bool requestResize) {
        if (frame) frame->setZoom(zoom_);
        Steinberg::ViewRect r(0, 0, static_cast<Steinberg::int32>(std::lround(Editor::EditorView::kWidth * zoom_)),
                              static_cast<Steinberg::int32>(std::lround(Editor::EditorView::kHeight * zoom_)));
        if (requestResize && plugFrame && (r.getWidth() != getRect().getWidth() || r.getHeight() != getRect().getHeight()))
            plugFrame->resizeView(this, &r);
        setRect(r);
    }

    ControllerBackend backend_;
    EditController* controller_;
    VSTGUI::SharedPointer<Editor::EditorView> view_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    double hostScale_ = 1.0; // host content scale factor
    double zoom_ = 1.0;      // total zoom (user size x host scale)
};

} // namespace

Steinberg::IPlugView* arpsidCreateCrossPlatformEditor(void* editController) {
    auto* c = static_cast<EditController*>(editController);
    return c ? new CrossPlatformEditor(c) : nullptr;
}

} // namespace ArpSID
