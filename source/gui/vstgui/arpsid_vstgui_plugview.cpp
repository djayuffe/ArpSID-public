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
#include "pluginterfaces/vst/ivstcontextmenu.h"
#include "base/source/fobject.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include "vst3/arpsid_vst3_kernel_host.h"

namespace ArpSID {

namespace {

using Steinberg::Vst::EditController;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

// Target for ArpSID's own entries in the host parameter menu.
class MenuTarget final : public Steinberg::FObject, public Steinberg::Vst::IContextMenuTarget {
public:
    explicit MenuTarget(std::function<void(Steinberg::int32)> fn) : fn_(std::move(fn)) {}
    Steinberg::tresult PLUGIN_API executeMenuItem(Steinberg::int32 tag) override {
        if (fn_) fn_(tag);
        return Steinberg::kResultOk;
    }
    OBJ_METHODS(MenuTarget, FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IContextMenuTarget)
    END_DEFINE_INTERFACES(FObject)
    REFCOUNT_METHODS(FObject)

private:
    std::function<void(Steinberg::int32)> fn_;
};

class ControllerBackend final : public EditorBackend {
public:
    explicit ControllerBackend(EditController* c) : c_(c) {}

    // Set by the plug view: shows the host menu for a parameter at editor
    // coordinates (the view knows its zoom and IPlugView identity).
    std::function<bool(int, double, double)> contextMenu;

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
    int savedTab() const override { return arpsidControllerEditorTab(c_); }
    void tabChanged(int tab) override { arpsidControllerSetEditorTab(c_, tab); }
    bool paramContextMenu(int id, double x, double y) override { return contextMenu && contextMenu(id, x, y); }
    std::string trackName() const override {
        char buf[256] = {};
        arpsidControllerTrackName(c_, buf, sizeof buf);
        return buf;
    }
    std::uint32_t trackColour() const override { return arpsidControllerTrackColour(c_); }
    bool bypassed() const override {
        return c_->getParamNormalized(static_cast<ParamID>(kVst3BypassParamId)) >= 0.5;
    }

private:
    EditController* c_;
};

class CrossPlatformEditor final : public Steinberg::Vst::VSTGUIEditor,
                                  public Steinberg::IPlugViewContentScaleSupport {
public:
    explicit CrossPlatformEditor(EditController* controller)
        : VSTGUIEditor(controller, nullptr), backend_(controller), controller_(controller) {
        zoom_ = clampZoom_(arpsidControllerEditorZoom(controller));
        backend_.contextMenu = [this](int id, double x, double y) { return popupParamMenu_(id, x, y); };
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
        frame->registerKeyboardHook(view_.get());
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
            if (view_) frame->unregisterKeyboardHook(view_.get());
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
    // Host parameter menu (IComponentHandler3) with "Reset to Default".
    bool popupParamMenu_(int id, double x, double y) {
        Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler3> handler(controller_->getComponentHandler());
        if (!handler) return false;
        ParamID pid = static_cast<ParamID>(id);
        Steinberg::IPtr<Steinberg::Vst::IContextMenu> menu =
            Steinberg::owned(handler->createContextMenu(this, &pid));
        if (!menu) return false;
        auto target = Steinberg::owned(new MenuTarget([this, id](Steinberg::int32 tag) {
            if (tag != 1) return;
            if (auto* p = controller_->getParameterObject(static_cast<ParamID>(id)))
                editorSetParam(backend_, id, static_cast<float>(p->getInfo().defaultNormalizedValue));
        }));
        Steinberg::Vst::IContextMenu::Item sep{};
        sep.flags = Steinberg::Vst::IContextMenuItem::kIsSeparator;
        menu->addItem(sep, nullptr);
        Steinberg::Vst::IContextMenu::Item item{};
        const char* text = "Reset to Default";
        for (int i = 0; text[i] && i < 127; ++i) item.name[i] = static_cast<Steinberg::Vst::TChar>(text[i]);
        item.tag = 1;
        menu->addItem(item, target);
        menu->popup(static_cast<Steinberg::UCoord>(std::lround(x * zoom_)),
                    static_cast<Steinberg::UCoord>(std::lround(y * zoom_)));
        return true;
    }

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
