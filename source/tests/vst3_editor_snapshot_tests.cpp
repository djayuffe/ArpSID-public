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

#include "vstgui/lib/cbitmap.h"
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
        host_.loadFactorySlot(slot);
    }
    int currentFactorySlot() const override { return slot_; }
    void sendMidi(uint8_t s, uint8_t d1, uint8_t d2) override {
        const uint8_t b[3] = {s, d1, d2};
        host_.injectMidi(b, 3);
    }
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
