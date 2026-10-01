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
#include "pluginterfaces/vst/ivstplugview.h"
#include "base/source/fobject.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"

#if defined(__linux__)
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/linux/x11platform.h"
#include <cairo/cairo-xcb.h>
#include <xcb/xcb.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>
#include "vst3/arpsid_vst3_kernel_host.h"

namespace ArpSID {

namespace {

#if defined(__linux__)
// VSTGUI on Linux has no event loop of its own: X events and timers run on
// the host's Linux::IRunLoop, which hosts serve from the IPlugFrame. VSTGUI
// opens its X connection only when a frame is opened with an X11::FrameConfig
// carrying a run loop; without one it drives xcb through a null connection
// and crashes in the first X call. It also keeps the first run loop it is
// given for the life of the module, so ArpSID installs this one forwarding
// object and points it at the current editor's host run loop on every open.
class HostRunLoop final : public VSTGUI::IRunLoop, public VSTGUI::AtomicReferenceCounted {
public:
    static HostRunLoop& instance() {
        static auto* loop = new HostRunLoop; // lives as long as the module (VSTGUI keeps a reference)
        return *loop;
    }

    // The host run loop new registrations go to (nullptr while no editor is open).
    void setTarget(Steinberg::Linux::IRunLoop* target) { target_ = target; }
    bool hasTarget() const { return target_ != nullptr; }

    bool registerEventHandler(int fd, VSTGUI::IEventHandler* handler) override {
        if (!target_ || !handler) return false;
        auto h = Steinberg::owned(new FdHandler(handler));
        if (target_->registerEventHandler(h, fd) != Steinberg::kResultTrue) return false;
        fds_.push_back({h, target_});
        return true;
    }
    bool unregisterEventHandler(VSTGUI::IEventHandler* handler) override {
        for (auto it = fds_.begin(); it != fds_.end(); ++it) {
            if (it->handler->target != handler) continue;
            it->loop->unregisterEventHandler(it->handler);
            fds_.erase(it);
            return true;
        }
        return false;
    }
    bool registerTimer(uint64_t intervalMs, VSTGUI::ITimerHandler* handler) override {
        if (!target_ || !handler) return false;
        auto h = Steinberg::owned(new TimerHandler(handler));
        if (target_->registerTimer(h, intervalMs) != Steinberg::kResultTrue) return false;
        timers_.push_back({h, target_});
        return true;
    }
    bool unregisterTimer(VSTGUI::ITimerHandler* handler) override {
        for (auto it = timers_.begin(); it != timers_.end(); ++it) {
            if (it->handler->target != handler) continue;
            it->loop->unregisterTimer(it->handler);
            timers_.erase(it);
            return true;
        }
        return false;
    }

private:
    struct FdHandler final : Steinberg::FObject, Steinberg::Linux::IEventHandler {
        explicit FdHandler(VSTGUI::IEventHandler* t) : target(t) {}
        void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor) override { target->onEvent(); }
        VSTGUI::IEventHandler* target;
        OBJ_METHODS(FdHandler, FObject)
        DEFINE_INTERFACES
            DEF_INTERFACE(Steinberg::Linux::IEventHandler)
        END_DEFINE_INTERFACES(FObject)
        REFCOUNT_METHODS(FObject)
    };
    struct TimerHandler final : Steinberg::FObject, Steinberg::Linux::ITimerHandler {
        explicit TimerHandler(VSTGUI::ITimerHandler* t) : target(t) {}
        void PLUGIN_API onTimer() override { target->onTimer(); }
        VSTGUI::ITimerHandler* target;
        OBJ_METHODS(TimerHandler, FObject)
        DEFINE_INTERFACES
            DEF_INTERFACE(Steinberg::Linux::ITimerHandler)
        END_DEFINE_INTERFACES(FObject)
        REFCOUNT_METHODS(FObject)
    };
    // Each registration remembers the host loop it went to, so it is removed
    // from that loop even after another editor retargeted this object.
    struct FdEntry { Steinberg::IPtr<FdHandler> handler; Steinberg::IPtr<Steinberg::Linux::IRunLoop> loop; };
    struct TimerEntry { Steinberg::IPtr<TimerHandler> handler; Steinberg::IPtr<Steinberg::Linux::IRunLoop> loop; };

    HostRunLoop() { remember(); }

    Steinberg::IPtr<Steinberg::Linux::IRunLoop> target_;
    std::vector<FdEntry> fds_;
    std::vector<TimerEntry> timers_;
};
#endif

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
    void loadPatch(const SidStateRootV1& root, const std::string& name) override {
        arpsidControllerLoadPatch(c_, root, name.c_str());
    }
    std::string patchName() const override {
        char buf[512] = {};
        arpsidControllerPatchName(c_, buf, sizeof buf);
        return buf;
    }
    bool isUserPatch() const override { return arpsidControllerIsUserPatch(c_); }
    bool savePresetFile(const std::string& path, std::string& error) override {
        char buf[512] = {};
        const bool ok = arpsidControllerSavePresetFile(c_, path.c_str(), buf, sizeof buf);
        if (!ok) error = buf;
        return ok;
    }
    bool loadPresetFile(const std::string& path, std::string& error) override {
        char buf[512] = {};
        const bool ok = arpsidControllerLoadPresetFile(c_, path.c_str(), buf, sizeof buf);
        if (!ok) error = buf;
        return ok;
    }
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
                                  public Steinberg::IPlugViewContentScaleSupport,
                                  public Steinberg::Vst::IParameterFinder {
public:
    explicit CrossPlatformEditor(EditController* controller)
        : VSTGUIEditor(controller, nullptr), backend_(controller), controller_(controller) {
        zoom_ = clampZoom_(arpsidControllerEditorZoom(controller));
        backend_.contextMenu = [this](int id, double x, double y) { return popupParamMenu_(id, x, y); };
        Steinberg::ViewRect r(0, 0, static_cast<Steinberg::int32>(std::lround(Editor::EditorView::kWidth * zoom_)),
                              static_cast<Steinberg::int32>(std::lround(Editor::EditorView::kHeight * zoom_)));
        setRect(r);
    }

#if defined(__linux__)
    // VSTGUI is built without Wayland support: offer X11 embedding only, so
    // hosts that prefer Wayland fall back to X11 (XWayland) instead of opening
    // an editor that cannot work.
    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override {
        return type && std::strcmp(type, Steinberg::kPlatformTypeX11EmbedWindowID) == 0 ? Steinberg::kResultTrue
                                                                                         : Steinberg::kResultFalse;
    }
#endif

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override {
        if (frame) return false;
#if defined(__linux__)
        // Run loop: the host's, from the plug frame (see HostRunLoop). A host
        // that passed one to the factory instead (SDK 3.8 host context) has
        // already installed it in VSTGUI. With neither, refuse to open.
        auto* lf = VSTGUI::getPlatformFactory().asLinuxFactory();
        if (!lf) return false;
        auto& forward = HostRunLoop::instance();
        Steinberg::FUnknownPtr<Steinberg::Linux::IRunLoop> hostLoop(plugFrame);
        if (hostLoop) {
            forward.setTarget(hostLoop);
            if (!lf->getRunLoop()) lf->setRunLoop(VSTGUI::SharedPointer<VSTGUI::IRunLoop>(&forward));
        }
        if (!lf->getRunLoop() || (lf->getRunLoop().get() == &forward && !forward.hasTarget()))
            return false;
        VSTGUI::X11::FrameConfig config;
        config.runLoop = lf->getRunLoop();
        VSTGUI::IPlatformFrameConfig* frameConfig = &config;
#else
        VSTGUI::IPlatformFrameConfig* frameConfig = nullptr;
#endif
        const VSTGUI::CRect size(0, 0, Editor::EditorView::kWidth, Editor::EditorView::kHeight);
        frame = new VSTGUI::CFrame(size, this);
        frame->setTransparency(false);
        view_ = VSTGUI::makeOwned<Editor::EditorView>(backend_);
        frame->addView(view_);
        view_->remember();
        if (!frame->open(parent, platformType, frameConfig)) {
            close();
            return false;
        }
        ++openEditors_;
        counted_ = true;
        applyZoom_(false);
        frame->registerKeyboardHook(view_.get());
        Editor::ParamKnob::setMode(arpsidControllerKnobMode(controller_));
        timer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>([this](VSTGUI::CVSTGUITimer*) {
            // The host may change its knob mode preference while the editor is open.
            Editor::ParamKnob::setMode(arpsidControllerKnobMode(controller_));
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
#if defined(__linux__)
            const bool last = counted_ && openEditors_ == 1;
            // The last X11 frame's destructor closes VSTGUI's X connection, but
            // cairo keeps its per-connection cache keyed by the connection
            // pointer. The next editor's xcb_connect often gets the same
            // address back and cairo then asserts on the stale entry
            // (cairo-xcb-screen.c _get_screen_index). Keep the connection open
            // across the frame teardown, finish cairo's device for it, then
            // let VSTGUI close it.
            if (last) VSTGUI::X11::RunLoop::init();
#endif
            view_ = nullptr;
            frame->forget();
            frame = nullptr;
            if (counted_) {
                counted_ = false;
                --openEditors_;
            }
#if defined(__linux__)
            if (last) {
                finishCairoDevice_(VSTGUI::X11::RunLoop::instance().getXcbConnection());
                VSTGUI::X11::RunLoop::exit();
                // No editor left: drop the reference to the host's run loop.
                HostRunLoop::instance().setTarget(nullptr);
            }
#endif
        }
        view_ = nullptr;
    }

    // VSTGUIEditor::attached reports success even when open() fails; tell the
    // host the truth so it does not show an empty window.
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override {
        const Steinberg::tresult r = VSTGUIEditor::attached(parent, type);
        if (r == Steinberg::kResultOk && !frame) {
            Steinberg::Vst::EditorView::removed();
            return Steinberg::kResultFalse;
        }
        return r;
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

    // IParameterFinder: the parameter under the mouse, for host "learn"
    // functions (e.g. Cubase Quick Controls, Reaper "last touched").
    // Coordinates are in view pixels; the editor lays out at 1x.
    Steinberg::tresult PLUGIN_API findParameter(Steinberg::int32 xPos, Steinberg::int32 yPos,
                                                ParamID& resultTag) override {
        if (!view_ || zoom_ <= 0.0) return Steinberg::kResultFalse;
        const int id = view_->paramIdAt(VSTGUI::CPoint(xPos / zoom_, yPos / zoom_));
        if (id < 0) return Steinberg::kResultFalse;
        resultTag = static_cast<ParamID>(id);
        return Steinberg::kResultTrue;
    }

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::IPlugViewContentScaleSupport)
        DEF_INTERFACE(Steinberg::Vst::IParameterFinder)
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

#if defined(__linux__)
    // Finishes cairo's xcb device for `c` (drops its cache entry for the
    // connection); cairo hands back the existing device for a new surface.
    static void finishCairoDevice_(xcb_connection_t* c) {
        if (!c || xcb_connection_has_error(c)) return;
        xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
        if (!screen) return;
        xcb_visualtype_t* visual = nullptr;
        for (auto d = xcb_screen_allowed_depths_iterator(screen); d.rem && !visual; xcb_depth_next(&d))
            for (auto v = xcb_depth_visuals_iterator(d.data); v.rem; xcb_visualtype_next(&v))
                if (v.data->visual_id == screen->root_visual) {
                    visual = v.data;
                    break;
                }
        if (!visual) return;
        cairo_surface_t* s = cairo_xcb_surface_create(c, screen->root, visual, 1, 1);
        if (cairo_device_t* dev = cairo_surface_get_device(s)) cairo_device_finish(dev);
        cairo_surface_destroy(s);
    }
#endif

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
    static inline int openEditors_ = 0; // editors with an open frame (UI thread only)
    bool counted_ = false;              // this editor is in openEditors_
    double hostScale_ = 1.0; // host content scale factor
    double zoom_ = 1.0;      // total zoom (user size x host scale)
};

} // namespace

Steinberg::IPlugView* arpsidCreateCrossPlatformEditor(void* editController) {
    auto* c = static_cast<EditController*>(editController);
    return c ? new CrossPlatformEditor(c) : nullptr;
}

} // namespace ArpSID
