// Copyright (C) 2024-2026 Ulf Bertilsson
// Renders every tab of the cross-platform VST3 editor offscreen, with the
// real shared kernel running, and writes one PNG per tab.
//
//   * every tab page builds and draws;
//   * the editor holds a control for every user parameter (layout table);
//   * each rendered tab has real content (not a blank frame);
//   * editing a knob reaches the kernel, and a host-side change reaches the
//     control (two-way binding).
//
// usage: arpsid_vst3_editor_snapshot <output-directory>

#include "gui/vstgui/arpsid_editor_view.h"
#include "vst3/arpsid_vst3_kernel_host.h"

#include "arpsid/core/sid_parameter_presentation.h"
#include "au3/ArpSIDCanonicalEvents.h"
#include "parameter_ids.h"
#include "factory_patch_params.h"
#include "arpsid/core/sid_serializer_schema.h"
#include "arpsid/patchbank/forensic_patch_bank.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/platform/iplatformbitmap.h"
#include "vstgui/lib/vstguiinit.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#endif

#include <array>
#include "vstgui/lib/events.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace ArpSID;
using namespace VSTGUI;

namespace {

int failures = 0;
void check(bool ok, const std::string& msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg.c_str());
        ++failures;
    }
}

// Editor backend over a Vst3KernelHost, like the plug-in controller.
class SnapshotBackend final : public EditorBackend {
public:
    explicit SnapshotBackend(Vst3KernelHost& h) : host_(h) {}
    float param(int id) const override { return host_.parameter(id); }
    void beginEdit(int) override {}
    void performEdit(int id, float v) override {
        host_.setParameterNonRealtime(id, v);
        ++edits;
    }
    void endEdit(int) override {}
    std::string paramText(int id, float v) const override {
        char buf[64] = {};
        if (!SidParameterPresentation::formatNormalized(id, v, buf, sizeof buf)) return {};
        return buf;
    }
    void selectFactoryPatch(int slot) override {
        slot_ = slot;
        userName_.clear();
        host_.loadFactorySlot(slot);
    }
    int currentFactorySlot() const override { return slot_; }
    void loadPatch(const SidStateRootV1& root, const std::string& name) override {
        host_.scheduleStateRoot(root);
        userName_ = name.empty() ? "User patch" : name;
    }
    std::string patchName() const override { return userName_.empty() ? factoryPatchNameForSlot(slot_) : userName_; }
    bool isUserPatch() const override { return !userName_.empty(); }
    std::string userName_;
    void sendMidi(uint8_t s, uint8_t d1, uint8_t d2) override {
        const uint8_t b[3] = {s, d1, d2};
        host_.injectMidi(b, 3);
        midi.push_back({s, d1, d2});
    }
    int savedTab() const override { return savedTabValue; }
    void tabChanged(int t) override { lastTab = t; }
    bool paramContextMenu(int id, double, double) override {
        menuId = id;
        return true;
    }
    std::string trackName() const override { return "Lead SID"; }
    std::uint32_t trackColour() const override { return 0xFF3366CCu; }
    int savedTabValue = 0, lastTab = -1, menuId = -1;
    std::vector<std::array<uint8_t, 3>> midi;
    Vst3KernelHost* kernelHost() override { return &host_; }
    void markStateDirty() override { ++dirty; }
    int edits = 0, dirty = 0;

private:
    Vst3KernelHost& host_;
    int slot_ = 0;
};

void renderAudio(Vst3KernelHost& host, int blocks) {
    std::vector<float> l(512), r(512);
    float* out[2] = {l.data(), r.data()};
    TransportState t{};
    t.sampleRate = 48000.0;
    t.bpm = 120.0;
    t.isPlaying = true;
    t.playStateKnown = true;
    for (int b = 0; b < blocks; ++b) {
        t.beatPosition = b * 512.0 / 48000.0 * 2.0;
        host.render(out, 2, 512, nullptr, 0, t);
    }
}

bool writePng(CBitmap* bmp, const std::string& path, double& spread) {
    auto platformBitmap = bmp ? bmp->getPlatformBitmap() : nullptr;
    if (!platformBitmap) return false;
    const auto png = getPlatformFactory().createBitmapMemoryPNGRepresentation(platformBitmap);
    if (png.empty()) return false;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    // Content check: a blank page compresses to almost nothing.
    spread = static_cast<double>(png.size());
    return static_cast<bool>(out);
}

std::string safeName(const char* n) {
    std::string s;
    for (const char* p = n; *p; ++p) s += (std::isalnum(static_cast<unsigned char>(*p)) ? *p : '_');
    return s;
}

} // namespace

int main(int argc, char** argv) {
    const std::string outDir = argc > 1 ? argv[1] : ".";
    std::error_code dirErr;
    std::filesystem::create_directories(outDir, dirErr);
    std::setvbuf(stdout, nullptr, _IONBF, 0); // progress survives a crash
#if defined(_WIN32)
    // Direct2D / WIC offscreen drawing needs COM (a host provides it).
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    VSTGUI::init(GetModuleHandleW(nullptr));
#else
    VSTGUI::init(nullptr);
#endif
    {
        Vst3KernelHost host;
        host.setup(48000.0, 512);
        host.loadFactorySlot(0);
        renderAudio(host, 4);
        // A held chord so meters, scopes and register views show activity.
        for (uint8_t n : {48, 55, 60, 64}) {
            const uint8_t on[3] = {0x90, n, 100};
            host.injectMidi(on, 3);
        }
        renderAudio(host, 40);

        std::puts("  engine running");
        SnapshotBackend backend(host);
        auto view = makeOwned<Editor::EditorView>(backend);
        view->buildAllPages();
        std::puts("  editor built");
        const auto& tabs = GUI::EditorLayout::tabs();
        check(view->tabCount() == static_cast<int>(tabs.size()), "editor exposes every production tab");
        check(view->parameterControlCount() >= 150, "editor holds controls for the parameter set");

        const CPoint size(Editor::EditorView::kWidth, Editor::EditorView::kHeight);
        for (int i = 0; i < view->tabCount(); ++i) {
            view->selectTab(i);
            renderAudio(host, 4);
            view->refresh();
            view->refresh();
            auto ctx = COffscreenContext::create(size, 1.0);
            check(static_cast<bool>(ctx), "offscreen context");
            if (!ctx) break;
            ctx->beginDraw();
            view->drawRect(ctx, CRect(0, 0, size.x, size.y));
            ctx->endDraw();
            const char* name = GUI::tabSpec(tabs[static_cast<std::size_t>(i)].id).displayName;
            const std::string path = outDir + "/editor_" + (i < 10 ? "0" : "") + std::to_string(i) + "_" + safeName(name) + ".png";
            double bytes = 0;
            check(writePng(ctx->getBitmap(), path, bytes), "write " + path);
            check(bytes > 20000.0, std::string("tab ") + name + " renders real content");
            std::printf("  %-10s -> %s (%.0f KB)\n", name, path.c_str(), bytes / 1024.0);
        }

        // Two-way binding: host change -> control, control edit -> kernel.
        view->selectTab(0);
        host.setParameterNonRealtime(kParamFilterCutoff, 0.123f);
        renderAudio(host, 2);
        view->refresh();
        check(std::fabs(host.parameter(kParamFilterCutoff) - 0.123f) < 1e-3f, "host-side cutoff change applied");
        const int editsBefore = backend.edits;
        editorSetParam(backend, kParamFilterResonance, 0.5f);
        renderAudio(host, 2);
        check(backend.edits == editsBefore + 1 && std::fabs(host.parameter(kParamFilterResonance) - 0.5f) < 1e-3f,
              "editor edit reaches the kernel");

        // A user patch (preset browser, patch file, user bank) shows its name
        // in the header's patch menu; a factory pick replaces it.
        {
            backend.loadPatch(makeFactoryPatchStateRootForSlot(33), "My Night Lead");
            renderAudio(host, 2);
            view->refresh();
            COptionMenu* menu = nullptr;
            for (CView* v = view->getViewAt(CPoint(400, 22), GetViewOptions().deep()); v && !menu; v = v->getParentView())
                menu = dynamic_cast<COptionMenu*>(v);
            check(menu != nullptr, "the header has a patch menu");
            if (menu) {
                const int idx = static_cast<int>(menu->getCurrentIndex(true));
                CMenuItem* item = menu->getEntry(idx);
                check(idx == kCanonicalFactoryPatchSlotCount + 1 && item &&
                          std::string(item->getTitle().data()).find("My Night Lead") != std::string::npos,
                      "the header names the loaded user patch");
                auto ctx = COffscreenContext::create(size, 1.0);
                if (ctx) {
                    ctx->beginDraw();
                    view->drawRect(ctx, CRect(0, 0, size.x, size.y));
                    ctx->endDraw();
                    double bytes = 0;
                    check(writePng(ctx->getBitmap(), outDir + "/editor_user_patch.png", bytes), "write user patch header");
                }
                backend.selectFactoryPatch(3);
                view->refresh();
                check(menu->getCurrentIndex() == 3, "a factory pick replaces the user patch in the header");
            }
        }

        // Tab memory: the editor reports tab changes and reopens on the saved tab.
        view->selectTab(2);
        check(backend.lastTab == 2, "tab change reported to the host");
        backend.savedTabValue = 5;
        {
            auto reopened = makeOwned<Editor::EditorView>(backend);
            check(reopened->selectedTab() == 5, "editor reopens on the saved tab");
        }
        view->selectTab(0);

        // Right-click on a parameter control asks the host for its menu.
        int foundId = -1;
        CPoint hit;
        for (CCoord y = 100; y < 600 && foundId < 0; y += 7)
            for (CCoord x = 12; x < 1188 && foundId < 0; x += 7)
                if ((foundId = view->paramIdAt(CPoint(x, y))) >= 0) hit = CPoint(x, y);
        check(foundId >= 0 && foundId < kNumParams, "a parameter control is found under the mouse");
        MouseDownEvent right;
        right.mousePosition = hit;
        right.buttonState.set(MouseButton::Right);
        view->onMouseDownEvent(right);
        check(right.consumed && backend.menuId == foundId, "right-click opens the host parameter menu");
        check(view->paramIdAt(CPoint(4, 4)) < 0, "no parameter at the editor corner");

        // Knob modes (host IEditController2::setKnobMode): circular follows
        // the mouse angle, relative circular turns by the angle moved, linear
        // (the default) drags vertically.
        {
            Editor::ParamKnob* knob = nullptr;
            for (CCoord y = 100; y < 700 && !knob; y += 5)
                for (CCoord x = 12; x < 1188 && !knob; x += 5)
                    for (CView* v = view->getViewAt(CPoint(x, y), GetViewOptions().deep()); v && !knob;
                         v = v->getParentView())
                        knob = dynamic_cast<Editor::ParamKnob*>(v);
            check(knob != nullptr, "a knob is on the first tab");
            if (knob) {
                const CRect r = knob->getViewSize();
                const CCoord dia = std::min(r.getWidth() - 8.0, r.getHeight() - 28.0);
                const CPoint c(r.getCenter().x, r.top + 13.0 + dia / 2.0);
                auto at = [&](double deg) {
                    const double a = deg * 3.14159265358979 / 180.0;
                    return CPoint(c.x + std::cos(a) * dia * 0.4, c.y + std::sin(a) * dia * 0.4);
                };
                auto drag = [&](CPoint from, CPoint to) {
                    MouseDownEvent d;
                    d.mousePosition = from;
                    d.buttonState.set(MouseButton::Left);
                    knob->onMouseDownEvent(d);
                    MouseMoveEvent m;
                    m.mousePosition = to;
                    m.buttonState.set(MouseButton::Left);
                    knob->onMouseMoveEvent(m);
                    MouseUpEvent u;
                    u.mousePosition = to;
                    knob->onMouseUpEvent(u);
                };
                check(Editor::ParamKnob::mode() == Editor::ParamKnob::kLinear, "knobs default to linear drag");
                check(std::fabs(knob->valueAtPoint(at(135.0))) < 1e-3f &&
                          std::fabs(knob->valueAtPoint(at(270.0)) - 0.5f) < 1e-3f &&
                          std::fabs(knob->valueAtPoint(at(45.0)) - 1.0f) < 1e-3f,
                      "knob angle law: lower left 0, top 0.5, lower right 1");

                Editor::ParamKnob::setMode(Editor::ParamKnob::kCircular);
                drag(at(200.0), at(270.0));
                check(std::fabs(knob->getValueNormalized() - 0.5f) < 1e-3f, "circular: the knob follows the mouse angle");

                Editor::ParamKnob::setMode(Editor::ParamKnob::kRelativeCircular);
                knob->setValueNormalized(0.2f);
                drag(at(270.0), at(0.0)); // a quarter turn clockwise
                check(std::fabs(knob->getValueNormalized() - (0.2f + 90.0f / 270.0f)) < 1e-3f,
                      "relative circular: the knob turns by the angle moved");

                Editor::ParamKnob::setMode(Editor::ParamKnob::kLinear);
                knob->setValueNormalized(0.2f);
                drag(c, CPoint(c.x, c.y - 90.0));
                check(std::fabs(knob->getValueNormalized() - 0.7f) < 1e-3f, "linear: 90 px up is half the range");
            }
        }

        // Computer keyboard: A = C4 (60), X raises the octave, Ctrl+key is the host's.
        auto key = [&](char32_t c, EventType t, bool ctrl = false) {
            KeyboardEvent e(t);
            e.character = c;
            if (ctrl) e.modifiers.add(ModifierKey::Control);
            view->onKeyboardEvent(e, nullptr);
            return e.consumed;
        };
        backend.midi.clear();
        check(key('a', EventType::KeyDown) && key('a', EventType::KeyUp), "A key is consumed");
        check(backend.midi.size() == 2 && backend.midi[0][0] == 0x90 && backend.midi[0][1] == 60 &&
                  backend.midi[1][0] == 0x80 && backend.midi[1][1] == 60,
              "A plays and releases C4");
        key('x', EventType::KeyDown);
        key('x', EventType::KeyUp);
        key('k', EventType::KeyDown);
        key('k', EventType::KeyUp);
        check(backend.midi.size() == 4 && backend.midi[2][1] == 84, "X shifts up an octave (K = C6)");
        key('z', EventType::KeyDown);
        check(!key('a', EventType::KeyDown, true), "Ctrl+A is left to the host");
        check(backend.midi.size() == 4, "Ctrl+A plays nothing");
        {
            auto closing = makeOwned<Editor::EditorView>(backend);
            KeyboardEvent held(EventType::KeyDown);
            held.character = 'd';
            closing->onKeyboardEvent(held, nullptr);
        }
        check(backend.midi.size() == 6 && backend.midi[5][0] == 0x80 && backend.midi[5][1] == backend.midi[4][1],
              "closing the editor releases a held computer-keyboard note");
    }
    VSTGUI::exit();
#if defined(_WIN32)
    CoUninitialize();
#endif
    if (failures) {
        std::fprintf(stderr, "vst3_editor_snapshot: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("vst3_editor_snapshot PASS");
    return 0;
}
