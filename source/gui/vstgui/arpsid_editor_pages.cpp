// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — live displays and model editors for the cross-platform editor.

#include "gui/vstgui/arpsid_editor_view.h"
#include "gui/vstgui/arpsid_wav_reader.h"

#include "vst3/arpsid_vst3_kernel_host.h"

#include "arpsid/gui/kit_state_blob.h"
#include "arpsid/gui/language_strings_v553.h"
#include "arpsid/gui/mix_panel_model.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid_file_bank.h"
#include "au3/ArpSIDStateSerializer.h"
#include "factory_patch_params.h"
#include "parameter_ids.h"

#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>

namespace ArpSID::Editor {

using namespace VSTGUI;
namespace L = GUI::EditorLayout;

namespace {

const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

std::string noteName(int n) {
    n = std::clamp(n, 0, 127);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%s%d", kNoteNames[n % 12], n / 12 - 1);
    return buf;
}

#if defined(__GNUC__)
std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
#endif
std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

Label* unavailable(const CRect& r, EditorContext& ctx) {
    return new Label(r, "Needs the ArpSID engine in this process (unavailable in this host).", ctx.theme, 11.0);
}

// Opens a file selector attached to the view's frame (no-op without a frame,
// e.g. offscreen rendering).
void chooseFile(CView* anchor, bool save, const char* title, const char* extDesc, const char* ext,
                const std::string& defaultName, std::function<void(std::string)> done) {
    CFrame* frame = anchor ? anchor->getFrame() : nullptr;
    if (!frame) return;
    auto sel = owned(CNewFileSelector::create(frame, save ? CNewFileSelector::kSelectSaveFile
                                                          : CNewFileSelector::kSelectFile));
    if (!sel) return;
    sel->setTitle(title);
    if (ext) sel->addFileExtension(CFileExtension(extDesc, ext));
    if (save && !defaultName.empty()) sel->setDefaultSaveName(defaultName.c_str());
    sel->run([done = std::move(done)](CNewFileSelector* s) {
        if (s->getNumSelectedFiles() > 0 && s->getSelectedFile(0)) done(s->getSelectedFile(0));
    });
}

std::vector<uint8_t> readFileBytes(const std::string& path, std::size_t maxBytes) {
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> out;
    if (!in) return out;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (out.size() > maxBytes) out.clear();
    return out;
}

// Base container for model editors: refreshes its children when the kernel
// host's model generation changes (state load, another editor instance).
class ModelPanel : public CViewContainer {
public:
    ModelPanel(const CRect& r, EditorContext& ctx) : CViewContainer(r), ctx_(ctx) { setTransparency(true); }
    Vst3KernelHost* host() const { return ctx_.backend.kernelHost(); }
    void pollModelGeneration() {
        if (Vst3KernelHost* h = host()) {
            const auto g = h->modelGeneration();
            if (g != seen_) {
                seen_ = g;
                onModelChanged();
                invalid();
            }
        }
    }
    virtual void onModelChanged() {}
protected:
    EditorContext& ctx_;
    std::uint64_t seen_ = 0;
};

// ── simple live displays ────────────────────────────────────────────────────

DisplayInstance makeOscilloscope(const CRect& r, EditorContext& ctx) {
    auto* v = new ScopeView(r, ctx.theme, 1);
    v->setCaption("OUT");
    DisplayInstance d{v};
    d.wantsScopes = true;
    d.refresh = [v, &ctx]() {
        if (!ctx.telemetry) return;
        const auto& t = *ctx.telemetry;
        const int n = static_cast<int>(std::min<uint32_t>(t.mainOscScopeCount, 512u));
        v->setTrace(0, t.mainOscScope, n > 0 ? n : 512, ctx.theme.accent);
    };
    return d;
}

DisplayInstance makeFilterResponse(const CRect& r, EditorContext& ctx) {
    auto* v = new FilterCurveView(r, ctx.theme);
    DisplayInstance d{v};
    d.refresh = [v, &ctx]() {
        const int modeIdx = static_cast<int>(std::lround(ctx.backend.param(kParamFilterMode) *
                                                         static_cast<float>(normalizedParamStepCount(kParamFilterMode))));
        v->setFilter(ctx.backend.param(kParamFilterCutoff), ctx.backend.param(kParamFilterResonance), modeIdx);
    };
    return d;
}

DisplayInstance makeFilterScopes(const CRect& r, EditorContext& ctx) {
    auto* v = new ScopeView(r, ctx.theme, 2);
    v->setCaption("IN (dim)  /  OUT");
    DisplayInstance d{v};
    d.wantsScopes = true;
    d.refresh = [v, &ctx]() {
        if (!ctx.telemetry) return;
        CColor dim = ctx.theme.label;
        dim.alpha = 150;
        v->setTrace(0, ctx.telemetry->filterScopeIn, 256, dim);
        v->setTrace(1, ctx.telemetry->filterScopeOut, 256, ctx.theme.accent);
    };
    return d;
}

DisplayInstance makeLfoWaves(const CRect& r, EditorContext& ctx) {
    auto* v = new LfoWaveView(r, ctx.theme);
    DisplayInstance d{v};
    d.refresh = [v, &ctx]() {
        static const int kShape[4] = {kParamLFOShape, kParamLFO2Shape, kParamLFO3Shape, kParamLFO4Shape};
        static const int kDepth[4] = {kParamLFODepth, kParamLFO2Depth, kParamLFO3Depth, kParamLFO4Depth};
        for (int i = 0; i < 4; ++i) {
            const int shape = static_cast<int>(std::lround(ctx.backend.param(kShape[i]) *
                                                           static_cast<float>(normalizedParamStepCount(kShape[i]))));
            const float phase = ctx.telemetry ? ctx.telemetry->lfoPhase[i] : 0.f;
            const float value = ctx.telemetry ? ctx.telemetry->lfoValue[i] : 0.f;
            v->setLfo(i, shape, ctx.backend.param(kDepth[i]), phase, value);
        }
    };
    return d;
}

DisplayInstance makeVcoScopes(const CRect& r, EditorContext& ctx) {
    auto* v = new ScopeView(r, ctx.theme, 3);
    v->setCaption("VOICE 1 / 2 / 3");
    DisplayInstance d{v};
    d.wantsScopes = true;
    d.refresh = [v, &ctx]() {
        if (!ctx.telemetry) return;
        const CColor c[3] = {ctx.theme.accent, ctx.theme.ledOn, ctx.theme.title};
        float lane[256];
        for (int k = 0; k < 3; ++k) {
            for (int i = 0; i < 256; ++i) lane[i] = ctx.telemetry->vcoScope[k][i] * 0.33f + (1 - k) * 0.62f;
            v->setTrace(k, lane, 256, c[k]);
        }
    };
    return d;
}

DisplayInstance makeSidRegisters(const CRect& r, EditorContext& ctx) {
    auto* v = new TextGrid(r, ctx.theme, 11.0);
    DisplayInstance d{v};
    d.refresh = [v, &ctx]() {
        if (!ctx.telemetry) {
            v->setLines({"no engine telemetry"});
            return;
        }
        const uint8_t* s = ctx.telemetry->sidRegs;
        std::vector<std::string> lines;
        lines.push_back("!       FREQ   PW    CTRL AD   SR");
        for (int vv = 0; vv < 3; ++vv) {
            const int b = vv * 7;
            const int freq = s[b] | (s[b + 1] << 8), pw = s[b + 2] | ((s[b + 3] & 0x0F) << 8);
            const uint8_t ctrl = s[b + 4];
            std::string waves;
            if (ctrl & 0x10) waves += "TRI ";
            if (ctrl & 0x20) waves += "SAW ";
            if (ctrl & 0x40) waves += "PUL ";
            if (ctrl & 0x80) waves += "NOI ";
            lines.push_back(fmt("V%d $D4%02X %04X  %03X   %02X   %02X   %02X  %s%s", vv + 1, b, freq, pw, ctrl,
                                s[b + 5], s[b + 6], waves.c_str(), (ctrl & 1) ? "GATE" : ""));
        }
        const int fc = (s[0x15] & 7) | (s[0x16] << 3);
        lines.push_back(fmt("FILTER  FC %03X  RES %X  ROUTE %X  MODE %X  VOL %X", fc, s[0x17] >> 4, s[0x17] & 0xF,
                            s[0x18] >> 4, s[0x18] & 0xF));
        lines.push_back(fmt("POTX %02X  POTY %02X  OSC3 %02X  ENV3 %02X", s[0x19], s[0x1A], s[0x1B], s[0x1C]));
        std::string raw;
        for (int i = 0; i < 0x1D; ++i) raw += fmt("%02X%s", s[i], (i % 8 == 7) ? "  " : " ");
        lines.push_back(raw);
        v->setLines(std::move(lines));
    };
    return d;
}

DisplayInstance makeDrumMeters(const CRect& r, EditorContext& ctx) {
    auto* box = new CViewContainer(r);
    box->setTransparency(true);
    const CRect local(0, 0, r.getWidth(), r.getHeight());
    auto* meters = new MeterView(CRect(0, 0, local.right, local.bottom - 20), ctx.theme, 8, true);
    auto* info = new Label(CRect(0, local.bottom - 18, local.right, local.bottom), "", ctx.theme, 10.0);
    box->addView(meters);
    box->addView(info);
    DisplayInstance d{box};
    d.refresh = [meters, info, &ctx]() {
        if (!ctx.telemetry) return;
        static const char* kNames[8] = {"KICK", "SNARE", "CHAT", "OHAT", "CLAP", "TOM", "CBELL", "RIM"};
        for (int i = 0; i < 8; ++i) meters->setLevel(i, ctx.telemetry->drumLevel[i], kNames[i]);
        const auto& t = *ctx.telemetry;
        info->setText(t.lastDrumNote >= 0 ? fmt("last hit: note %d  vel %.2f", t.lastDrumNote,
                                                static_cast<double>(t.lastDrumVelocity))
                                          : std::string("no drum hits yet"));
    };
    return d;
}

DisplayInstance makeForensicMeters(const CRect& r, EditorContext& ctx) {
    auto* v = new TextGrid(r, ctx.theme, 10.5);
    DisplayInstance d{v};
    d.refresh = [v, &ctx]() {
        if (!ctx.telemetry) return;
        const auto& t = *ctx.telemetry;
        v->setLines({t.forensicEnabled ? "!FORENSIC ENGINE ACTIVE" : "forensic engine off",
                     fmt("activity    %5.2f", static_cast<double>(t.forensicActivity)),
                     fmt("intensity   %5.2f", static_cast<double>(t.forensicIntensity)),
                     fmt("jitter      %5.2f", static_cast<double>(t.forensicClockJitter)),
                     fmt("ripple      %5.2f", static_cast<double>(t.forensicSupplyRipple)),
                     fmt("thermal     %5.2f", static_cast<double>(t.forensicThermalDrift)),
                     fmt("crosstalk   %5.2f", static_cast<double>(t.forensicVoiceCrosstalk)),
                     fmt("ext bleed   %5.2f", static_cast<double>(t.forensicExternalBleed)),
                     fmt("noise       %5.2f", static_cast<double>(t.forensicSystemNoise)),
                     fmt("digifix     %s", t.forensicDigifix8580 ? "on" : "off")});
    };
    return d;
}

DisplayInstance makeHiFiMeters(const CRect& r, EditorContext& ctx) {
    auto* box = new CViewContainer(r);
    box->setTransparency(true);
    auto* meters = new MeterView(CRect(0, 0, r.getWidth(), r.getHeight() * 0.55), ctx.theme, 3);
    auto* info = new TextGrid(CRect(0, r.getHeight() * 0.58, r.getWidth(), r.getHeight()), ctx.theme, 10.5);
    box->addView(meters);
    box->addView(info);
    DisplayInstance d{box};
    d.refresh = [meters, info, &ctx]() {
        if (!ctx.telemetry) return;
        const auto& t = *ctx.telemetry;
        meters->setLevel(0, t.hifiDryPeak, "DRY");
        meters->setLevel(1, t.hifiWetPeak, "WET");
        meters->setLevel(2, std::min(1.f, t.hifiDeltaPeak * 4.f), "DELTA");
        info->setLines({t.hifiEnabled ? "!HI-FI ON" : "hi-fi off",
                        fmt("quality %d   oversampling %d", t.hifiQuality, t.hifiOversampling),
                        fmt("mono correlation %.2f   safety gain %.2f", static_cast<double>(t.hifiMonoCorrelation),
                            static_cast<double>(t.hifiSafetyGain))});
    };
    return d;
}

DisplayInstance makeModMonitor(const CRect& r, EditorContext& ctx) {
    auto* v = new MeterView(r, ctx.theme, 8);
    DisplayInstance d{v};
    d.refresh = [v, &ctx]() {
        if (!ctx.telemetry) return;
        const auto& t = *ctx.telemetry;
        for (int i = 0; i < 4; ++i) v->setLevel(i, 0.5f + 0.5f * t.lfoValue[i], fmt("LFO%d", i + 1));
        v->setLevel(4, t.modWheelNorm, "WHEEL");
        v->setLevel(5, 0.5f + 0.5f * t.focusedPitchBend, "BEND");
        v->setLevel(6, t.focusedChannelPressure, "PRESS");
        v->setLevel(7, t.randomValue, "RANDOM");
    };
    return d;
}

DisplayInstance makeSidCoreTimeline(const CRect& r, EditorContext& ctx) {
    auto* v = new ScopeView(r, ctx.theme, 5);
    v->setCaption("SID REG / VALUE / WRITE / PHI2 / IRQ-DMA  (C64 bus)");
    DisplayInstance d{v};
    d.wantsScopes = true;
    d.wantsC64 = true;
    d.refresh = [v, &ctx]() {
        if (!ctx.telemetry) return;
        const auto& t = *ctx.telemetry;
        const float* src[5] = {t.c64SidRegScope, t.c64SidValueScope, t.c64SidWritePulseScope, t.c64Phi2Scope,
                               t.c64IrqDmaScope};
        const CColor c[5] = {ctx.theme.accent, ctx.theme.ledOn, ctx.theme.title, ctx.theme.label, ctx.theme.warn};
        float lane[128];
        for (int k = 0; k < 5; ++k) {
            for (int i = 0; i < 128; ++i) lane[i] = src[k][i] * 0.18f + (0.8f - 0.4f * k);
            v->setTrace(k, lane, 128, c[k]);
        }
    };
    return d;
}

DisplayInstance makeActions(const CRect& r, EditorContext& ctx) {
    auto* box = new CViewContainer(r);
    box->setTransparency(true);
    const CCoord w = r.getWidth();
    box->addView(new ActionButton(CRect(0, 0, w, 30), "PANIC", ctx.theme, [&ctx]() {
        editorSetParam(ctx.backend, kParamPanic, 1.0f);
        editorSetParam(ctx.backend, kParamPanic, 0.0f);
    }));
    box->addView(new ActionButton(CRect(0, 36, w, 66), "ALL NOTES OFF", ctx.theme, [&ctx]() {
        for (uint8_t ch = 0; ch < 16; ++ch) ctx.backend.sendMidi(static_cast<uint8_t>(0xB0 | ch), 123, 0);
    }));
    return DisplayInstance{box};
}

// ── SEQ: 32-step note / velocity / gate editor ─────────────────────────────

DisplayInstance makeSeqSteps(const CRect& r, EditorContext& ctx) {
    auto stepParam = [](int step, int row) { return static_cast<int>(kParamSeqStep1Note) + step * 3 + row; };
    auto onClick = [&ctx, stepParam](int c, int row, bool shift, bool right, float yFrac) {
        const int id = stepParam(c, row);
        if (row == 0) {
            const int steps = static_cast<int>(normalizedParamStepCount(id));
            int note = static_cast<int>(std::lround(ctx.backend.param(id) * static_cast<float>(steps)));
            note = std::clamp(note + (right ? -1 : 1) * (shift ? 12 : 1), 0, steps);
            editorSetParam(ctx.backend, id, static_cast<float>(note) / static_cast<float>(steps));
        } else {
            editorSetParam(ctx.backend, id, right ? 0.f : yFrac);
        }
    };
    auto* seq = new CellGrid(r, ctx.theme, 32, 3, onClick);
    seq->setRowLabels({"NOTE", "VEL", "GATE"});
    seq->setDragPaint(true, true);
    DisplayInstance d{seq};
    d.refresh = [seq, &ctx, stepParam]() {
        const int length = 1 + static_cast<int>(std::lround(ctx.backend.param(kParamSeqLength) *
                                                            static_cast<float>(normalizedParamStepCount(kParamSeqLength))));
        const int playing = ctx.telemetry && ctx.telemetry->seqEnabled ? ctx.telemetry->seqStep : -1;
        for (int s = 0; s < 32; ++s) {
            const bool inLen = s < length;
            const int noteSteps = static_cast<int>(normalizedParamStepCount(stepParam(s, 0)));
            const int note = static_cast<int>(std::lround(ctx.backend.param(stepParam(s, 0)) * static_cast<float>(noteSteps)));
            CellGrid::Cell n;
            n.text = noteName(note);
            n.on = false;
            n.cursor = (s == playing);
            n.level = inLen ? 0.f : 0.f;
            seq->setCell(s, 0, n);
            for (int row = 1; row < 3; ++row) {
                CellGrid::Cell c;
                c.level = inLen ? std::max(0.02f, ctx.backend.param(stepParam(s, row))) : 0.f;
                c.cursor = (s == playing);
                seq->setCell(s, row, c);
            }
        }
    };
    return d;
}

// ── BANK: 180-slot factory browser + patch/bank files ───────────────────────

class BankPanel final : public ModelPanel {
public:
    BankPanel(const CRect& r, EditorContext& ctx) : ModelPanel(r, ctx) {
        const CCoord w = r.getWidth(), h = r.getHeight();
        grid_ = new CellGrid(CRect(0, 34, w, h), ctx.theme, 6, 30,
                             [this](int c, int row, bool, bool, float) { pick(c * 30 + row); });
        addView(grid_);
        const char* labels[] = {"FACTORY", "USER BANK", "LOAD PATCH", "SAVE PATCH", "LOAD BANK", "SAVE BANK"};
        for (int i = 0; i < 6; ++i) {
            auto* b = new ActionButton(CRect(i * 124, 0, i * 124 + 118, 28), labels[i], ctx.theme,
                                       [this, i]() { action(i); });
            buttons_.push_back(b);
            addView(b);
        }
        status_ = new Label(CRect(6 * 124 + 6, 0, w, 28), "", ctx.theme, 10.5);
        addView(status_);
    }

    void refresh() {
        const int current = ctx_.backend.currentFactorySlot();
        buttons_[0]->setLit(!userView_);
        buttons_[1]->setLit(userView_);
        for (int s = 0; s < 180; ++s) {
            CellGrid::Cell c;
            if (userView_) {
                if (s < static_cast<int>(userMetas_.size()))
                    c.text = fmt("U%03d %s", s + 1, userMetas_[static_cast<std::size_t>(s)].name);
            } else {
                c.text = fmt("%03d %s", s + 1, factoryPatchNameForSlot(s).c_str());
                c.cursor = (s == current);
            }
            grid_->setCell(s / 30, s % 30, c);
        }
    }

private:
    void pick(int slot) {
        if (!userView_) {
            ctx_.backend.selectFactoryPatch(slot);
            status_->setText(fmt("loaded factory %03d", slot + 1));
            return;
        }
        Vst3KernelHost* h = host();
        if (!h || slot >= static_cast<int>(userRoots_.size())) return;
        h->scheduleStateRoot(userRoots_[static_cast<std::size_t>(slot)]);
        ctx_.backend.markStateDirty();
        status_->setText(fmt("loaded user %03d %s", slot + 1, userMetas_[static_cast<std::size_t>(slot)].name));
    }

    void action(int i) {
        Vst3KernelHost* h = host();
        switch (i) {
            case 0: userView_ = false; break;
            case 1: userView_ = true; break;
            case 2:
                chooseFile(this, false, "Load ArpSID patch", "ArpSID patch", "arpsid", {}, [this, h](std::string p) {
                    if (!h) return;
                    SidStateRootV1 root{};
                    ArpSIDFilePatchMeta meta{};
                    const auto e = ArpSIDFileBank::loadPatchFromFile(p, root, meta);
                    if (e == FileBankError::OK) {
                        h->scheduleStateRoot(root);
                        ctx_.backend.markStateDirty();
                    }
                    status_->setText(e == FileBankError::OK ? fmt("loaded %s", meta.name) : fileBankErrorString(e));
                });
                break;
            case 3:
                chooseFile(this, true, "Save ArpSID patch", "ArpSID patch", "arpsid", "patch.arpsid",
                           [this, h](std::string p) {
                               if (!h) return;
                               SidStateRootV1 root{};
                               h->currentStateRoot(root);
                               ArpSIDFilePatchMeta meta{};
                               const int slot = ctx_.backend.currentFactorySlot();
                               std::snprintf(meta.name, sizeof meta.name, "%s", factoryPatchNameForSlot(std::max(0, slot)).c_str());
                               std::snprintf(meta.author, sizeof meta.author, "User");
                               const auto e = ArpSIDFileBank::savePatchToFile(p, root, meta);
                               status_->setText(e == FileBankError::OK ? "patch saved" : fileBankErrorString(e));
                           });
                break;
            case 4:
                chooseFile(this, false, "Load ArpSID bank", "ArpSID bank", "arpsidbank", {}, [this](std::string p) {
                    std::vector<SidStateRootV1> roots;
                    std::vector<ArpSIDFilePatchMeta> metas;
                    const auto e = ArpSIDFileBank::loadBankFromFile(p, roots, metas);
                    if (e == FileBankError::OK) {
                        userRoots_ = std::move(roots);
                        userMetas_ = std::move(metas);
                        userView_ = true;
                    }
                    status_->setText(e == FileBankError::OK ? fmt("bank: %zu patches", userRoots_.size())
                                                             : fileBankErrorString(e));
                });
                break;
            case 5:
                chooseFile(this, true, "Save ArpSID bank", "ArpSID bank", "arpsidbank", "ArpSID.arpsidbank",
                           [this, h](std::string p) {
                               std::vector<SidStateRootV1> roots(static_cast<std::size_t>(kFileBankMaxSlots));
                               std::vector<ArpSIDFilePatchMeta> metas(static_cast<std::size_t>(kFileBankMaxSlots));
                               const auto& defs = getFactoryPatchDefinitions();
                               for (int s = 0; s < kFileBankMaxSlots; ++s) {
                                   roots[static_cast<std::size_t>(s)] = makeFactoryPatchStateRootForSlot(s);
                                   if (static_cast<std::size_t>(s) < defs.size())
                                       metas[static_cast<std::size_t>(s)] =
                                           ArpSIDFileBank::metaFromDefinition(defs[static_cast<std::size_t>(s)]);
                               }
                               // The live patch replaces its factory slot.
                               const int slot = ctx_.backend.currentFactorySlot();
                               if (h && slot >= 0 && slot < kFileBankMaxSlots)
                                   h->currentStateRoot(roots[static_cast<std::size_t>(slot)]);
                               const auto e = ArpSIDFileBank::saveBankToFile(p, roots, metas);
                               status_->setText(e == FileBankError::OK ? "bank saved" : fileBankErrorString(e));
                           });
                break;
            default:
                break;
        }
    }

    CellGrid* grid_ = nullptr;
    std::vector<ActionButton*> buttons_;
    Label* status_ = nullptr;
    bool userView_ = false;
    std::vector<SidStateRootV1> userRoots_;
    std::vector<ArpSIDFilePatchMeta> userMetas_;
};

// ── SETTINGS ────────────────────────────────────────────────────────────────

class SettingsPanel final : public ModelPanel {
public:
    SettingsPanel(const CRect& r, EditorContext& ctx) : ModelPanel(r, ctx) {
        using K = GUI::UIStringKey_v553;
        const GUI::Language lang = ctx.language;
        auto s = [lang](K k) { return std::string(GUI::localizedString_v553(k, lang)); };
        auto edit = [this](auto fn) {
            if (Vst3KernelHost* h = host()) {
                auto m = h->settings();
                fn(m);
                h->setSettings(m);
                ctx_.backend.markStateDirty();
                if (ctx_.themeChanged) ctx_.themeChanged();
            }
        };
        const CCoord colW = 300;
        CCoord y = 0;
        auto addRow = [&](const std::string& section, CView* control) {
            auto* l = new Label(CRect(0, y, colW, y + 18), section, ctx.theme, 11.5, true);
            l->setColor(&ctx.theme.title);
            addView(l);
            control->setViewSize(CRect(0, y + 20, colW, y + 58));
            control->setMouseableArea(control->getViewSize());
            addView(control);
            y += 70;
        };
        addRow(s(K::kSectionAudioEngine),
               new ChoiceMenu(CRect(), s(K::kLabelTopologyMode), ctx.theme,
                              {s(K::kEngineItemBitPerfect), s(K::kEngineItemSingleSid)},
                              [this]() { return host() && host()->settings().audioEngineMode ==
                                                              GUI::AudioEngineMode::SingleSid3Voice ? 1 : 0; },
                              [edit](int i) mutable {
                                  edit([i](GUI::SettingsPanelModel& m) {
                                      m.audioEngineMode = i == 1 ? GUI::AudioEngineMode::SingleSid3Voice
                                                                 : GUI::AudioEngineMode::BitPerfect;
                                  });
                              }));
        addRow(s(K::kSectionTheme),
               new ChoiceMenu(CRect(), s(K::kLabelColorScheme), ctx.theme,
                              {s(K::kThemeItemDark), s(K::kThemeItemLight), s(K::kThemeItemC64Classic),
                               s(K::kThemeItemHighContrast)},
                              [this]() { return host() ? static_cast<int>(host()->settings().theme) : 0; },
                              [edit](int i) mutable {
                                  edit([i](GUI::SettingsPanelModel& m) { m.theme = static_cast<GUI::Theme>(i); });
                              }));
        addRow(s(K::kSectionLanguage),
               new ChoiceMenu(CRect(), s(K::kLabelPluginLanguage), ctx.theme,
                              {"English", "Norsk", "Deutsch", "Fran\xc3\xa7" "ais", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"},
                              [this]() { return host() ? static_cast<int>(host()->settings().language) : 0; },
                              [edit](int i) mutable {
                                  edit([i](GUI::SettingsPanelModel& m) { m.language = static_cast<GUI::Language>(i); });
                              }));
        addRow(s(K::kSectionAdvanced),
               new ChoiceMenu(CRect(), "Diagnostic dashboard", ctx.theme, {"Off", "On"},
                              [this]() { return host() && host()->settings().diagnosticDashboardEnabled ? 1 : 0; },
                              [edit](int i) mutable {
                                  edit([i](GUI::SettingsPanelModel& m) {
                                      m.diagnosticDashboardEnabled = static_cast<uint8_t>(i);
                                  });
                              }));
        info_ = new TextGrid(CRect(colW + 30, 0, r.getWidth(), r.getHeight()), ctx.theme, 11.0);
        addView(info_);
        (void)s;
    }

    void refresh() {
        forEachChild([](CView* v) {
            if (auto* c = dynamic_cast<ChoiceMenu*>(v)) c->refresh();
        });
        using K = GUI::UIStringKey_v553;
        const GUI::Language lang = ctx_.language;
        std::vector<std::string> lines = {
            std::string("!") + GUI::localizedString_v553(K::kSectionHostSync, lang),
            std::string("  ") + GUI::localizedString_v553(K::kSyncItemHostTempo, lang) +
                "  (fixed: the host transport drives ARP / SEQ / C64 timing)",
            "",
            std::string("!") + GUI::localizedString_v553(K::kSectionMidiMapping, lang),
            std::string("  ") + GUI::localizedString_v553(K::kMidiItemGM, lang) +
                "  (fixed: GM drum map on channel 10, CC law shared with AU)",
            "",
            "!ROUTING",
            "  canonical SID engine -> MIX -> HI-FI -> limiter -> output",
        };
        if (ctx_.telemetry) {
            const auto& t = *ctx_.telemetry;
            lines.push_back("");
            lines.push_back("!ENGINE");
            lines.push_back(fmt("  render mode %d   SID model %d   voices %d", t.renderMode, t.sidModel, t.activeVoices));
            lines.push_back(fmt("  host %.1f BPM  %s   beat %.2f", t.hostTempo, t.hostPlaying ? "playing" : "stopped",
                                t.hostBeat));
            lines.push_back(fmt("  telemetry frame %llu", static_cast<unsigned long long>(t.telemetryFrameId)));
        }
        info_->setLines(std::move(lines));
    }

private:
    TextGrid* info_ = nullptr;
};

// ── MIX ─────────────────────────────────────────────────────────────────────

class MixPanel final : public ModelPanel {
public:
    MixPanel(const CRect& r, EditorContext& ctx) : ModelPanel(r, ctx) {
        const CCoord w = r.getWidth();
        const CCoord stripW = (w - 250) / GUI::kMixChannelCount;
        for (int ch = 0; ch < GUI::kMixChannelCount; ++ch) {
            const CCoord x = ch * stripW;
            auto* name = new Label(CRect(x, 0, x + stripW, 14), fmt("CH %d", ch + 1), ctx.theme, 10.0, true,
                                   kCenterText);
            name->setColor(&ctx.theme.title);
            addView(name);
            addStripKnob(ch, x, 16, stripW, "VOL", [](GUI::MixChannel& c) -> uint8_t& { return c.volume; },
                         [](int v) { return fmt("%.0f%%", v / 2.55); });
            addStripKnob(ch, x, 78, stripW, "PAN", [](GUI::MixChannel& c) -> uint8_t& { return c.pan; },
                         [](int v) { return v == 128 ? std::string("C") : fmt("%c%d", v < 128 ? 'L' : 'R', std::abs(v - 128) * 100 / 127); });
            addStripKnob(ch, x, 140, stripW, "DLY", [](GUI::MixChannel& c) -> uint8_t& { return c.sendToDelay; },
                         [](int v) { return fmt("%.0f", v / 2.55); });
            addStripKnob(ch, x, 202, stripW, "REV", [](GUI::MixChannel& c) -> uint8_t& { return c.sendToReverb; },
                         [](int v) { return fmt("%.0f", v / 2.55); });
            addStripButton(ch, x, 266, stripW, "M", [](GUI::MixChannel& c) -> uint8_t& { return c.mute; });
            addStripButton(ch, x, 292, stripW, "S", [](GUI::MixChannel& c) -> uint8_t& { return c.solo; });
            addStripButton(ch, x, 318, stripW, "ON", [](GUI::MixChannel& c) -> uint8_t& { return c.enabled; });
            auto* sel = new ActionButton(CRect(x + 2, 344, x + stripW - 2, 366), "FX", ctx.theme, [this, ch]() {
                editMix([ch](GUI::MixPanelModel& m) { m.selectedChannel = static_cast<uint16_t>(ch); });
            });
            fxSelect_.push_back(sel);
            addView(sel);
        }
        // Send buses + master on the right.
        const CCoord mx = w - 240;
        auto* busTitle = new Label(CRect(mx, 0, w, 14), "SENDS / MASTER", ctx.theme, 10.5, true);
        busTitle->setColor(&ctx.theme.title);
        addView(busTitle);
        for (int b = 0; b < GUI::kMixSendBusCount; ++b) {
            addView(new ByteKnob(CRect(mx + b * 60, 18, mx + b * 60 + 58, 78), b == 0 ? "DLY RET" : "REV RET",
                                 ctx.theme, 0, 255,
                                 [this, b]() { return host() ? host()->mix().sendBuses[static_cast<std::size_t>(b)].returnLevel : 0; },
                                 [this, b](int v) {
                                     editMix([b, v](GUI::MixPanelModel& m) {
                                         m.sendBuses[static_cast<std::size_t>(b)].returnLevel = static_cast<uint8_t>(v);
                                     });
                                 }));
        }
        addMasterKnob(mx + 120, 18, "MASTER", [](GUI::MixMaster& m) -> uint8_t& { return m.masterVolume; });
        addMasterKnob(mx + 180, 18, "WIDTH", [](GUI::MixMaster& m) -> uint8_t& { return m.stereoWidth; });
        addMasterKnob(mx, 84, "LIM THR", [](GUI::MixMaster& m) -> uint8_t& { return m.limiterThreshold; });
        addMasterKnob(mx + 60, 84, "LIM REL", [](GUI::MixMaster& m) -> uint8_t& { return m.limiterRelease; });
        auto* lim = new ActionButton(CRect(mx + 122, 94, mx + 236, 118), "LIMITER", ctx.theme, [this]() {
            editMix([](GUI::MixPanelModel& m) { m.master.limiterEnabled = m.master.limiterEnabled ? 0 : 1; });
        });
        auto* dim = new ActionButton(CRect(mx + 122, 122, mx + 236, 146), "DIM -10 dB", ctx.theme, [this]() {
            editMix([](GUI::MixPanelModel& m) { m.master.dimMonitor = m.master.dimMonitor ? 0 : 1; });
        });
        masterButtons_ = {lim, dim};
        addView(lim);
        addView(dim);
        for (int b = 0; b < GUI::kMixSendBusCount; ++b) {
            auto* on = new ActionButton(CRect(mx + b * 60, 150, mx + b * 60 + 58, 172), b == 0 ? "DLY ON" : "REV ON",
                                        ctx.theme, [this, b]() {
                                            editMix([b](GUI::MixPanelModel& m) {
                                                auto& e = m.sendBuses[static_cast<std::size_t>(b)].enabled;
                                                e = e ? 0 : 1;
                                            });
                                        });
            busButtons_.push_back(on);
            addView(on);
        }
        // FX slots of the selected channel.
        fxTitle_ = new Label(CRect(0, 376, w, 392), "", ctx.theme, 11.0, true);
        fxTitle_->setColor(&ctx.theme.title);
        addView(fxTitle_);
        const CCoord slotW = (w - 20) / GUI::kMixFxSlotsPerChannel;
        for (int s = 0; s < GUI::kMixFxSlotsPerChannel; ++s) {
            const CCoord sx = s * (slotW + 5);
            addView(new ChoiceMenu(CRect(sx, 394, sx + slotW, 432), fmt("SLOT %d", s + 1), ctx.theme,
                                   {"none", "EQ 3-band", "transient", "compressor", "saturator", "bitcrusher"},
                                   [this, s]() {
                                       if (!host()) return 0;
                                       const auto m = host()->mix();
                                       return static_cast<int>(m.channels[m.selectedChannel % 16].fxSlots[s].type);
                                   },
                                   [this, s](int i) {
                                       editMix([s, i](GUI::MixPanelModel& m) {
                                           GUI::setMixFxSlot(m.channels[m.selectedChannel % 16].fxSlots[s],
                                                             static_cast<GUI::MixFxType>(i));
                                       });
                                   }));
            for (int p = 0; p < GUI::kMixFxParamsPerSlot; ++p) {
                const CCoord px = sx + (p % 4) * (slotW / 4.0);
                const CCoord py = 438 + (p / 4) * 60;
                addView(new ByteKnob(CRect(px, py, px + slotW / 4.0 - 2, py + 58), fmt("P%d", p + 1), ctx.theme, 0, 255,
                                     [this, s, p]() {
                                         if (!host()) return 0;
                                         const auto m = host()->mix();
                                         return static_cast<int>(m.channels[m.selectedChannel % 16].fxSlots[s].params[p]);
                                     },
                                     [this, s, p](int v) {
                                         editMix([s, p, v](GUI::MixPanelModel& m) {
                                             m.channels[m.selectedChannel % 16].fxSlots[s].params[p] = static_cast<uint8_t>(v);
                                         });
                                     }));
            }
        }
    }

    void refresh() {
        Vst3KernelHost* h = host();
        if (!h) return;
        const auto m = h->mix();
        for (auto& [btn, get] : stripButtons_) btn->setLit(get(m) != 0);
        for (int ch = 0; ch < GUI::kMixChannelCount; ++ch)
            fxSelect_[static_cast<std::size_t>(ch)]->setLit(m.selectedChannel == ch);
        masterButtons_[0]->setLit(m.master.limiterEnabled != 0);
        masterButtons_[1]->setLit(m.master.dimMonitor != 0);
        for (int b = 0; b < GUI::kMixSendBusCount; ++b)
            busButtons_[static_cast<std::size_t>(b)]->setLit(m.sendBuses[static_cast<std::size_t>(b)].enabled != 0);
        fxTitle_->setText(fmt("FX CHAIN - CHANNEL %d", m.selectedChannel % 16 + 1));
        forEachChild([](CView* v) {
            if (auto* c = dynamic_cast<ChoiceMenu*>(v)) c->refresh();
        });
    }

private:
    template <typename Fn> void editMix(Fn fn) {
        Vst3KernelHost* h = host();
        if (!h) return;
        auto m = h->mix();
        fn(m);
        h->setMix(m);
        ctx_.backend.markStateDirty();
        invalid();
    }
    using FieldFn = uint8_t& (*)(GUI::MixChannel&);
    void addStripKnob(int ch, CCoord x, CCoord y, CCoord w, const char* label, FieldFn field,
                      std::function<std::string(int)> text) {
        addView(new ByteKnob(CRect(x + 1, y, x + w - 1, y + 60), label, ctx_.theme, 0, 255,
                             [this, ch, field]() {
                                 if (!host()) return 0;
                                 auto m = host()->mix();
                                 return static_cast<int>(field(m.channels[static_cast<std::size_t>(ch)]));
                             },
                             [this, ch, field](int v) {
                                 editMix([ch, field, v](GUI::MixPanelModel& m) {
                                     field(m.channels[static_cast<std::size_t>(ch)]) = static_cast<uint8_t>(v);
                                 });
                             },
                             std::move(text)));
    }
    void addStripButton(int ch, CCoord x, CCoord y, CCoord w, const char* label, FieldFn field) {
        auto* b = new ActionButton(CRect(x + 2, y, x + w - 2, y + 22), label, ctx_.theme, [this, ch, field]() {
            editMix([ch, field](GUI::MixPanelModel& m) {
                auto& f = field(m.channels[static_cast<std::size_t>(ch)]);
                f = f ? 0 : 1;
            });
        });
        stripButtons_.emplace_back(b, [ch, field](const GUI::MixPanelModel& m) {
            auto copy = m.channels[static_cast<std::size_t>(ch)];
            return field(copy);
        });
        addView(b);
    }
    using MasterFn = uint8_t& (*)(GUI::MixMaster&);
    void addMasterKnob(CCoord x, CCoord y, const char* label, MasterFn field) {
        addView(new ByteKnob(CRect(x, y, x + 58, y + 60), label, ctx_.theme, 0, 255,
                             [this, field]() {
                                 if (!host()) return 0;
                                 auto m = host()->mix();
                                 return static_cast<int>(field(m.master));
                             },
                             [this, field](int v) {
                                 editMix([field, v](GUI::MixPanelModel& m) { field(m.master) = static_cast<uint8_t>(v); });
                             }));
    }

    std::vector<std::pair<ActionButton*, std::function<int(const GUI::MixPanelModel&)>>> stripButtons_;
    std::vector<ActionButton*> fxSelect_;
    std::vector<ActionButton*> masterButtons_;
    std::vector<ActionButton*> busButtons_;
    Label* fxTitle_ = nullptr;
};

// ── KIT ─────────────────────────────────────────────────────────────────────

class KitPanel final : public ModelPanel {
public:
    KitPanel(const CRect& r, EditorContext& ctx) : ModelPanel(r, ctx) {
        const CCoord w = r.getWidth();
        addView(new ChoiceMenu(CRect(0, 0, 200, 38), "ENGINE TARGET", ctx.theme, {"DrSID", "SID-808", "DIGI"},
                               [this]() { return host() ? host()->kit().panelModel.activeEngineTarget : 0; },
                               [this](int i) {
                                   editKit([i](GUI::KitStateBlob& b) { b.panelModel.activeEngineTarget = static_cast<uint8_t>(i); });
                               }));
        steps_ = new CellGrid(CRect(0, 46, w, 46 + 9 * 20), ctx.theme, GUI::kKitStepCount, GUI::kKitDrumClassCount,
                              [this](int c, int row, bool shift, bool right, float) {
                                  editKit([c, row, shift, right](GUI::KitStateBlob& b) {
                                      const auto cell = GUI::kitStepGetCell(b.stepGrid, static_cast<uint8_t>(row), static_cast<uint8_t>(c));
                                      if (shift && cell.velocity) {
                                          GUI::kitStepSetCell(b.stepGrid, static_cast<uint8_t>(row), static_cast<uint8_t>(c),
                                                              cell.velocity, cell.flags ^ GUI::kKitStepFlagAccent);
                                      } else {
                                          const uint8_t vel = cell.velocity ? 0 : (right ? 60 : GUI::kKitStepDefaultVelocity);
                                          GUI::kitStepSetCell(b.stepGrid, static_cast<uint8_t>(row), static_cast<uint8_t>(c), vel, 0);
                                      }
                                      b.panelModel.activeDrumClass = static_cast<uint8_t>(row);
                                  });
                              });
        std::vector<std::string> rows;
        for (int i = 0; i < GUI::kKitDrumClassCount; ++i) rows.emplace_back(GUI::kKitDrumClassLabel[i]);
        steps_->setRowLabels(rows);
        addView(steps_);

        const CCoord y0 = 46 + 9 * 20 + 10;
        classTitle_ = new Label(CRect(0, y0, w, y0 + 16), "", ctx.theme, 11.0, true);
        classTitle_->setColor(&ctx.theme.title);
        addView(classTitle_);
        std::vector<std::string> classNames(rows);
        addView(new ChoiceMenu(CRect(0, y0 + 20, 150, y0 + 58), "DRUM CLASS", ctx.theme, classNames,
                               [this]() { return host() ? host()->kit().panelModel.activeDrumClass : 0; },
                               [this](int i) {
                                   editKit([i](GUI::KitStateBlob& b) { b.panelModel.activeDrumClass = static_cast<uint8_t>(i); });
                               }));
        // Factory slot assignment for the active class on each engine target.
        static const char* kTargets[3] = {"DrSID SLOT", "SID-808 SLOT", "DIGI SLOT"};
        for (int t = 0; t < 3; ++t) {
            std::vector<std::string> names;
            const auto target = static_cast<GUI::KitEngineTarget>(t);
            for (int s = 0; s < GUI::kitSlotCount(target); ++s)
                names.push_back(fmt("%02d %s", s + 1,
                                    factoryPatchNameForSlot(GUI::kitAbsoluteSlot(target, static_cast<uint8_t>(s))).c_str()));
            addView(new ChoiceMenu(CRect(160 + t * 240, y0 + 20, 390 + t * 240, y0 + 58), kTargets[t], ctx.theme, names,
                                   [this, t]() {
                                       if (!host()) return 0;
                                       const auto b = host()->kit();
                                       return static_cast<int>(b.panelModel.drumAssignments[b.panelModel.activeDrumClass % 9][t].factorySlotIndex);
                                   },
                                   [this, t](int i) {
                                       editKit([t, i](GUI::KitStateBlob& b) {
                                           b.panelModel.drumAssignments[b.panelModel.activeDrumClass % 9][t].factorySlotIndex =
                                               static_cast<uint8_t>(i);
                                       });
                                   }));
        }
        // SID-808 voice of the active class.
        const CCoord vy = y0 + 70;
        auto* voiceTitle = new Label(CRect(0, vy, 400, vy + 14), "SID-808 VOICE", ctx.theme, 10.5, true);
        voiceTitle->setColor(&ctx.theme.label);
        addView(voiceTitle);
        static const char* kWaves[4] = {"TRI", "SAW", "PUL", "NOI"};
        for (int wv = 0; wv < 4; ++wv) {
            auto* b = new ActionButton(CRect(wv * 52, vy + 18, wv * 52 + 48, vy + 40), kWaves[wv], ctx.theme, [this, wv]() {
                editVoice([wv](GUI::KitVoiceConfig& v, uint8_t& mask) {
                    v.waveform = static_cast<uint8_t>(v.waveform ^ (0x10u << wv));
                    mask |= GUI::kKitVoiceOverrideWaveform;
                });
            });
            waveButtons_.push_back(b);
            addView(b);
        }
        static const char* kFlags[3] = {"RING", "SYNC", "FILTER"};
        for (int f = 0; f < 3; ++f) {
            auto* b = new ActionButton(CRect(212 + f * 62, vy + 18, 270 + f * 62, vy + 40), kFlags[f], ctx.theme, [this, f]() {
                editVoice([f](GUI::KitVoiceConfig& v, uint8_t& mask) {
                    v.flags = static_cast<uint8_t>(v.flags ^ (1u << f));
                    mask |= GUI::kKitVoiceOverrideFlags;
                });
            });
            flagButtons_.push_back(b);
            addView(b);
        }
        auto nib = [this](const char* label, int shift, bool ad, CCoord x, CCoord y) {
            addView(new ByteKnob(CRect(x, y, x + 56, y + 58), label, ctx_.theme, 0, 15,
                                 [this, shift, ad]() {
                                     if (!host()) return 0;
                                     const auto b = host()->kit();
                                     const auto& v = b.voiceConfigGrid.voiceConfigs[b.panelModel.activeDrumClass % 9];
                                     return static_cast<int>(((ad ? v.attackDecay : v.sustainRelease) >> shift) & 0x0F);
                                 },
                                 [this, shift, ad](int val) {
                                     editVoice([shift, ad, val](GUI::KitVoiceConfig& v, uint8_t& mask) {
                                         uint8_t& f = ad ? v.attackDecay : v.sustainRelease;
                                         f = static_cast<uint8_t>((f & ~(0x0F << shift)) | ((val & 0x0F) << shift));
                                         mask |= ad ? GUI::kKitVoiceOverrideAttackDecay : GUI::kKitVoiceOverrideSustainRelease;
                                     });
                                 }));
        };
        nib("ATK", 4, true, 0, vy + 46);
        nib("DEC", 0, true, 60, vy + 46);
        nib("SUS", 4, false, 120, vy + 46);
        nib("REL", 0, false, 180, vy + 46);
        addView(new ByteKnob(CRect(240, vy + 46, 300, vy + 104), "PW", ctx.theme, 0, 4095,
                             [this]() {
                                 if (!host()) return 0;
                                 const auto b = host()->kit();
                                 const auto& v = b.voiceConfigGrid.voiceConfigs[b.panelModel.activeDrumClass % 9];
                                 return static_cast<int>(v.pulseWidthLo | ((v.pulseWidthHi & 0x0F) << 8));
                             },
                             [this](int pw) {
                                 editVoice([pw](GUI::KitVoiceConfig& v, uint8_t& mask) {
                                     v.pulseWidthLo = static_cast<uint8_t>(pw & 0xFF);
                                     v.pulseWidthHi = static_cast<uint8_t>((pw >> 8) & 0x0F);
                                     mask |= GUI::kKitVoiceOverridePulseWidth;
                                 });
                             }));
        // DIGI assignment of the active class.
        const CCoord ax = 460;
        auto* assignTitle = new Label(CRect(ax, vy, ax + 400, vy + 14), "DIGI ASSIGNMENT", ctx.theme, 10.5, true);
        assignTitle->setColor(&ctx.theme.label);
        addView(assignTitle);
        auto assignKnob = [this](const char* label, int minV, int maxV, CCoord x, CCoord y,
                                 std::function<int(const GUI::KitAssignConfig&)> get,
                                 std::function<void(GUI::KitAssignConfig&, int)> set,
                                 std::function<std::string(int)> text = {}) {
            addView(new ByteKnob(CRect(x, y, x + 56, y + 58), label, ctx_.theme, minV, maxV,
                                 [this, get]() {
                                     if (!host()) return 0;
                                     const auto b = host()->kit();
                                     return get(b.assignConfigGrid.assignConfigs[b.panelModel.activeDrumClass % 9]);
                                 },
                                 [this, set](int v) {
                                     editKit([set, v](GUI::KitStateBlob& b) {
                                         set(b.assignConfigGrid.assignConfigs[b.panelModel.activeDrumClass % 9], v);
                                     });
                                 },
                                 std::move(text)));
        };
        assignKnob("TUNE", GUI::kKitAssignTuneMin, GUI::kKitAssignTuneMax, ax, vy + 46,
                   [](const GUI::KitAssignConfig& a) { return static_cast<int>(a.tuneShiftBias) - GUI::kKitAssignTuneBias; },
                   [](GUI::KitAssignConfig& a, int v) { a.tuneShiftBias = static_cast<uint8_t>(GUI::kKitAssignTuneBias + v); },
                   [](int v) { return fmt("%+d st", v); });
        assignKnob("START", 0, 255, ax + 60, vy + 46, [](const GUI::KitAssignConfig& a) { return static_cast<int>(a.startOffset); },
                   [](GUI::KitAssignConfig& a, int v) { a.startOffset = static_cast<uint8_t>(v); });
        assignKnob("LENGTH", 0, 255, ax + 120, vy + 46, [](const GUI::KitAssignConfig& a) { return static_cast<int>(a.lengthScale); },
                   [](GUI::KitAssignConfig& a, int v) { a.lengthScale = static_cast<uint8_t>(v); },
                   [](int v) { return v == 0 ? std::string("full") : fmt("%d", v); });
        assignKnob("SAMPLE", 0, GUI::kKitDigiSlotCount - 1, ax + 180, vy + 46,
                   [](const GUI::KitAssignConfig& a) { return static_cast<int>(a.digiSlotIndex); },
                   [](GUI::KitAssignConfig& a, int v) { a.digiSlotIndex = static_cast<uint8_t>(v); },
                   [](int v) { return fmt("%d", v + 1); });
        static const char* kAssignFlags[2] = {"LOOP", "REVERSE"};
        for (int f = 0; f < 2; ++f) {
            auto* b = new ActionButton(CRect(ax + 250, vy + 50 + f * 28, ax + 340, vy + 72 + f * 28), kAssignFlags[f], ctx.theme,
                                       [this, f]() {
                                           editKit([f](GUI::KitStateBlob& b) {
                                               auto& a = b.assignConfigGrid.assignConfigs[b.panelModel.activeDrumClass % 9];
                                               a.flags = static_cast<uint8_t>(a.flags ^ (1u << f));
                                           });
                                       });
            assignButtons_.push_back(b);
            addView(b);
        }
    }

    void refresh() {
        Vst3KernelHost* h = host();
        if (!h) return;
        const auto b = h->kit();
        const int playing = ctx_.telemetry && ctx_.telemetry->seqEnabled ? ctx_.telemetry->seqStep : -1;
        for (int dc = 0; dc < GUI::kKitDrumClassCount; ++dc)
            for (int s = 0; s < GUI::kKitStepCount; ++s) {
                const auto cell = GUI::kitStepGetCell(b.stepGrid, static_cast<uint8_t>(dc), static_cast<uint8_t>(s));
                CellGrid::Cell c;
                c.on = cell.velocity > 0;
                c.accent = (cell.flags & GUI::kKitStepFlagAccent) != 0 && c.on;
                c.cursor = (s == playing);
                c.level = c.on ? std::max(0.25f, cell.velocity / 127.f) : 0.f;
                steps_->setCell(s, dc, c);
            }
        const int dc = b.panelModel.activeDrumClass % 9;
        classTitle_->setText(fmt("%s  (GM note %d)", GUI::kKitDrumClassLabel[dc], GUI::kKitDrumClassMidiNote[dc]));
        const auto& v = b.voiceConfigGrid.voiceConfigs[dc];
        for (int wv = 0; wv < 4; ++wv) waveButtons_[static_cast<std::size_t>(wv)]->setLit((v.waveform >> (4 + wv)) & 1);
        for (int f = 0; f < 3; ++f) flagButtons_[static_cast<std::size_t>(f)]->setLit((v.flags >> f) & 1);
        const auto& a = b.assignConfigGrid.assignConfigs[dc];
        for (int f = 0; f < 2; ++f) assignButtons_[static_cast<std::size_t>(f)]->setLit((a.flags >> f) & 1);
        forEachChild([](CView* view) {
            if (auto* c = dynamic_cast<ChoiceMenu*>(view)) c->refresh();
        });
    }

private:
    template <typename Fn> void editKit(Fn fn) {
        Vst3KernelHost* h = host();
        if (!h) return;
        auto b = h->kit();
        fn(b);
        h->setKit(b);
        ctx_.backend.markStateDirty();
        invalid();
    }
    template <typename Fn> void editVoice(Fn fn) {
        editKit([&fn](GUI::KitStateBlob& b) {
            const int dc = b.panelModel.activeDrumClass % 9;
            fn(b.voiceConfigGrid.voiceConfigs[dc], b.voiceConfigGrid.voiceConfigs[dc].pad_[0]);
        });
    }

    CellGrid* steps_ = nullptr;
    Label* classTitle_ = nullptr;
    std::vector<ActionButton*> waveButtons_, flagButtons_, assignButtons_;
};

// ── DIGI ────────────────────────────────────────────────────────────────────

class DigiPanel final : public ModelPanel {
public:
    DigiPanel(const CRect& r, EditorContext& ctx) : ModelPanel(r, ctx) {
        const CCoord w = r.getWidth();
        // Audition pads.
        for (int s = 0; s < GUI::kDigiActiveSlotCount; ++s) {
            auto* pad = new ActionButton(CRect(s * 74, 0, s * 74 + 70, 44), fmt("PAD %d", s + 1), ctx.theme, [this, s]() {
                if (Vst3KernelHost* h = host()) {
                    h->triggerDigiPad(static_cast<uint8_t>(s), 110);
                    editDigi([s](GUI::DigiPanelModel& m, GUI::DigiSampleBankBlob&) { m.activeSlot = static_cast<uint8_t>(s); });
                }
            });
            pads_.push_back(pad);
            addView(pad);
        }
        const CCoord rx = 8 * 74 + 10;
        addView(new ActionButton(CRect(rx, 0, rx + 130, 44), "IMPORT WAV...", ctx.theme, [this]() { importWav(); }));
        scope_ = new ScopeView(CRect(rx + 140, 0, w, 44), ctx.theme, 1);
        addView(scope_);

        steps_ = new CellGrid(CRect(0, 52, w, 52 + 8 * 18), ctx.theme, GUI::kDigiStepCount, GUI::kDigiActiveSlotCount,
                              [this](int c, int row, bool shift, bool right, float) {
                                  editDigi([c, row, shift, right](GUI::DigiPanelModel& m, GUI::DigiSampleBankBlob&) {
                                      auto& v = m.steps[row][c];
                                      v = v ? 0 : static_cast<uint8_t>(right ? 60 : (shift ? GUI::kDigiStepMaxVelocity
                                                                                           : GUI::kDigiStepDefaultVelocity));
                                      m.activeSlot = static_cast<uint8_t>(row);
                                  });
                              });
        std::vector<std::string> rows;
        for (int s = 0; s < GUI::kDigiActiveSlotCount; ++s) rows.push_back(fmt("SLOT %d", s + 1));
        steps_->setRowLabels(rows);
        steps_->setDragPaint(true);
        addView(steps_);

        const CCoord y0 = 52 + 8 * 18 + 10;
        slotTitle_ = new Label(CRect(0, y0, w, y0 + 16), "", ctx.theme, 11.0, true);
        slotTitle_->setColor(&ctx.theme.title);
        addView(slotTitle_);
        std::vector<std::string> sources = {"(empty)"};
        for (int f = 0; f < GUI::kKitDigiSlotCount; ++f)
            sources.push_back(fmt("F%02d %s", f + 1,
                                  factoryPatchNameForSlot(GUI::kitAbsoluteSlot(GUI::KitEngineTarget::Digi, static_cast<uint8_t>(f))).c_str()));
        sources.push_back("user sample");
        addView(new ChoiceMenu(CRect(0, y0 + 20, 260, y0 + 58), "SOURCE", ctx.theme, sources,
                               [this]() {
                                   if (!host()) return 0;
                                   const auto& s = activeSlot();
                                   if (s.sourceType == GUI::DigiSourceType::FactorySlot) return 1 + s.factorySlotIndex;
                                   if (s.sourceType == GUI::DigiSourceType::UserImport) return 1 + GUI::kKitDigiSlotCount;
                                   return 0;
                               },
                               [this](int i) {
                                   editDigi([i](GUI::DigiPanelModel& m, GUI::DigiSampleBankBlob& bank) {
                                       auto& s = m.slots[m.activeSlot % GUI::kDigiActiveSlotCount];
                                       if (i == 0) GUI::digiSetNoSource(s);
                                       else if (i <= GUI::kKitDigiSlotCount) GUI::digiSetFactorySlot(s, static_cast<uint8_t>(i - 1));
                                       else {
                                           const uint8_t slot = m.activeSlot % GUI::kDigiActiveSlotCount;
                                           if (bank.clips[slot].handle != 0) GUI::digiSetUserSampleSlot(s, slot, bank.clips[slot].handle);
                                       }
                                   });
                               }));
        auto slotKnob = [this](const char* label, int minV, int maxV, CCoord x, std::function<int(const GUI::DigiSampleSlot&)> get,
                               std::function<void(GUI::DigiSampleSlot&, int)> set, std::function<std::string(int)> text = {}) {
            const CCoord y = slotTitle_->getViewSize().top + 64;
            addView(new ByteKnob(CRect(x, y, x + 60, y + 60), label, ctx_.theme, minV, maxV,
                                 [this, get]() { return host() ? get(activeSlot()) : 0; },
                                 [this, set](int v) {
                                     editDigi([set, v](GUI::DigiPanelModel& m, GUI::DigiSampleBankBlob&) {
                                         set(m.slots[m.activeSlot % GUI::kDigiActiveSlotCount], v);
                                     });
                                 },
                                 std::move(text)));
        };
        slotKnob("TUNE", GUI::kDigiTuneMin, GUI::kDigiTuneMax, 0,
                 [](const GUI::DigiSampleSlot& s) { return static_cast<int>(s.tuneShiftBias) - GUI::kDigiTuneBias; },
                 [](GUI::DigiSampleSlot& s, int v) { GUI::digiSetTuneShift(s, v); }, [](int v) { return fmt("%+d st", v); });
        slotKnob("START", 0, 255, 64, [](const GUI::DigiSampleSlot& s) { return static_cast<int>(s.startOffset); },
                 [](GUI::DigiSampleSlot& s, int v) { s.startOffset = static_cast<uint8_t>(v); });
        slotKnob("LENGTH", 0, 255, 128, [](const GUI::DigiSampleSlot& s) { return static_cast<int>(s.lengthScale); },
                 [](GUI::DigiSampleSlot& s, int v) { s.lengthScale = static_cast<uint8_t>(v); },
                 [](int v) { return v == 0 ? std::string("full") : fmt("%d", v); });
        slotKnob("VOLUME", 0, 255, 192, [](const GUI::DigiSampleSlot& s) { return static_cast<int>(s.volume); },
                 [](GUI::DigiSampleSlot& s, int v) { s.volume = static_cast<uint8_t>(v); },
                 [](int v) { return fmt("%.0f%%", v / 2.55); });
        const CCoord by = y0 + 70;
        loop_ = new ActionButton(CRect(262, by, 350, by + 24), "LOOP", ctx.theme, [this]() {
            editDigi([](GUI::DigiPanelModel& m, GUI::DigiSampleBankBlob&) {
                auto& s = m.slots[m.activeSlot % GUI::kDigiActiveSlotCount];
                GUI::digiSetLoop(s, !(s.flags & GUI::kDigiFlagLoop));
            });
        });
        reverse_ = new ActionButton(CRect(262, by + 30, 350, by + 54), "REVERSE", ctx.theme, [this]() {
            editDigi([](GUI::DigiPanelModel& m, GUI::DigiSampleBankBlob&) {
                auto& s = m.slots[m.activeSlot % GUI::kDigiActiveSlotCount];
                GUI::digiSetReverse(s, !(s.flags & GUI::kDigiFlagReverse));
            });
        });
        addView(loop_);
        addView(reverse_);
        addView(new ActionButton(CRect(262, by + 60, 350, by + 84), "CLEAR ROW", ctx.theme, [this]() {
            editDigi([](GUI::DigiPanelModel& m, GUI::DigiSampleBankBlob&) { GUI::digiStepClearSlot(m, m.activeSlot); });
        }));
        // $D418 runtime policy and MIDI pad map.
        const CCoord px = 380;
        addView(new ChoiceMenu(CRect(px, y0 + 20, px + 200, y0 + 58), "$D418 MODE", ctx.theme,
                               {"legacy float", "authentic $D418", "fast $D418"},
                               [this]() {
                                   uint8_t mode = 0;
                                   uint32_t rate = 0;
                                   if (host()) host()->digiD418RuntimeMode(mode, rate);
                                   return static_cast<int>(mode);
                               },
                               [this](int i) {
                                   if (Vst3KernelHost* h = host()) {
                                       uint8_t mode = 0;
                                       uint32_t rate = 0;
                                       h->digiD418RuntimeMode(mode, rate);
                                       h->setDigiD418RuntimeMode(static_cast<uint8_t>(i), rate);
                                       ctx_.backend.markStateDirty();
                                   }
                               }));
        addView(new ByteKnob(CRect(px + 210, y0 + 16, px + 270, y0 + 76), "PAD ROOT", ctx.theme, 0, 127,
                             [this]() {
                                 uint8_t root = 36, ch = 16;
                                 if (host()) host()->digiMidiPadMapping(root, ch);
                                 return static_cast<int>(root);
                             },
                             [this](int v) {
                                 if (Vst3KernelHost* h = host()) {
                                     uint8_t root = 36, ch = 16;
                                     h->digiMidiPadMapping(root, ch);
                                     h->setDigiMidiPadMapping(static_cast<uint8_t>(v), ch);
                                     ctx_.backend.markStateDirty();
                                 }
                             },
                             [](int v) { return noteName(v); }));
        addView(new ByteKnob(CRect(px + 276, y0 + 16, px + 336, y0 + 76), "PAD CH", ctx.theme, 0, 16,
                             [this]() {
                                 uint8_t root = 36, ch = 16;
                                 if (host()) host()->digiMidiPadMapping(root, ch);
                                 return static_cast<int>(ch);
                             },
                             [this](int v) {
                                 if (Vst3KernelHost* h = host()) {
                                     uint8_t root = 36, ch = 16;
                                     h->digiMidiPadMapping(root, ch);
                                     h->setDigiMidiPadMapping(root, static_cast<uint8_t>(v));
                                     ctx_.backend.markStateDirty();
                                 }
                             },
                             [](int v) { return v >= 16 ? std::string("OMNI") : fmt("%d", v + 1); }));
        info_ = new TextGrid(CRect(px, y0 + 90, w, r.getHeight()), ctx.theme, 10.5);
        addView(info_);
    }

    void refresh() {
        Vst3KernelHost* h = host();
        if (!h) return;
        h->digi(model_, *bank_);
        const int playing = ctx_.telemetry && ctx_.telemetry->seqEnabled ? ctx_.telemetry->digiStep : -1;
        for (int s = 0; s < GUI::kDigiActiveSlotCount; ++s) {
            pads_[static_cast<std::size_t>(s)]->setLit(model_.activeSlot == s);
            for (int st = 0; st < GUI::kDigiStepCount; ++st) {
                CellGrid::Cell c;
                c.on = model_.steps[s][st] > 0;
                c.level = c.on ? std::max(0.25f, model_.steps[s][st] / 127.f) : 0.f;
                c.cursor = (st == playing);
                steps_->setCell(st, s, c);
            }
        }
        const auto& s = activeSlot();
        std::string src = "empty";
        if (s.sourceType == GUI::DigiSourceType::FactorySlot) src = fmt("factory %d", s.factorySlotIndex + 1);
        if (s.sourceType == GUI::DigiSourceType::UserImport)
            src = fmt("user: %s", bank_->clips[s.userSampleIndex % GUI::kDigiActiveSlotCount].displayName);
        slotTitle_->setText(fmt("SLOT %d  -  %s", model_.activeSlot + 1, src.c_str()));
        loop_->setLit(s.flags & GUI::kDigiFlagLoop);
        reverse_->setLit(s.flags & GUI::kDigiFlagReverse);
        if (ctx_.telemetry) {
            const auto& t = *ctx_.telemetry;
            scope_->setTrace(0, t.digiScope, 128, ctx_.theme.accent);
            info_->setLines({fmt("playing voices %d   triggers %u   midi %u", t.digiPlayingVoices, t.digiTriggerCount,
                                 t.digiMidiTriggerCount),
                             fmt("$D418 writes %u   SID accepted %u   blocked %u", t.digiD418WriteCount,
                                 t.digiD418SidAcceptedWriteCount, t.digiD418WritesBlockedByIo),
                             fmt("last nibble %X   last $D418 %02X   io %s", t.digiD418LastNibble, t.digiD418LastD418,
                                 t.digiD418LastIoVisible ? "visible" : "banked"),
                             statusLine_});
        }
        forEachChild([](CView* v) {
            if (auto* c = dynamic_cast<ChoiceMenu*>(v)) c->refresh();
        });
    }

private:
    const GUI::DigiSampleSlot& activeSlot() const { return model_.slots[model_.activeSlot % GUI::kDigiActiveSlotCount]; }

    template <typename Fn> void editDigi(Fn fn) {
        Vst3KernelHost* h = host();
        if (!h) return;
        h->digi(model_, *bank_);
        fn(model_, *bank_);
        h->setDigi(model_, *bank_);
        ctx_.backend.markStateDirty();
        invalid();
    }

    void importWav() {
        chooseFile(this, false, "Import sample into DIGI slot", "WAV audio", "wav", {}, [this](std::string path) {
            Vst3KernelHost* h = host();
            if (!h) return;
            WavData wav;
            std::string err;
            if (!readWavMono(path, wav, err)) {
                statusLine_ = "import failed: " + err;
                return;
            }
            const std::string name = path.substr(path.find_last_of("/\\") + 1);
            const bool ok = h->setDigiUserSample(model_.activeSlot, wav.samples.data(),
                                                 static_cast<uint32_t>(wav.samples.size()), wav.sampleRate, name.c_str());
            statusLine_ = ok ? fmt("imported %s (%zu frames @ %.0f Hz)", name.c_str(), wav.samples.size(), wav.sampleRate)
                             : std::string("import rejected by the DIGI bank");
            if (ok) ctx_.backend.markStateDirty();
        });
    }

    std::vector<ActionButton*> pads_;
    ScopeView* scope_ = nullptr;
    CellGrid* steps_ = nullptr;
    Label* slotTitle_ = nullptr;
    ActionButton* loop_ = nullptr;
    ActionButton* reverse_ = nullptr;
    TextGrid* info_ = nullptr;
    std::string statusLine_;
    GUI::DigiPanelModel model_ = GUI::makeDefaultDigiPanelModel();
    std::unique_ptr<GUI::DigiSampleBankBlob> bank_ = std::make_unique<GUI::DigiSampleBankBlob>();
};

// ── C64: SID player + machine readout ───────────────────────────────────────

class C64PlayerPanel final : public ModelPanel {
public:
    C64PlayerPanel(const CRect& r, EditorContext& ctx) : ModelPanel(r, ctx) {
        const char* labels[] = {"LOAD .SID...", "EJECT", "< SONG", "SONG >", "BOOT", "START", "STOP", "RESET"};
        for (int i = 0; i < 8; ++i) {
            auto* b = new ActionButton(CRect(i * 112, 0, i * 112 + 106, 30), labels[i], ctx.theme, [this, i]() { action(i); });
            addView(b);
        }
        vicFast_ = new ActionButton(CRect(0, 38, 150, 64), "VIC-II FAST", ctx.theme, [this]() {
            if (Vst3KernelHost* h = host()) h->c64ControlHubCommand(h->c64VicFast() ? 7 : 6);
        });
        cpuFast_ = new ActionButton(CRect(156, 38, 306, 64), "6510 FAST", ctx.theme, [this]() {
            if (Vst3KernelHost* h = host()) h->c64ControlHubCommand(h->c64CpuFast() ? 9 : 8);
        });
        addView(vicFast_);
        addView(cpuFast_);
        info_ = new TextGrid(CRect(0, 72, r.getWidth(), r.getHeight()), ctx.theme, 11.0);
        addView(info_);
    }

    void refresh() {
        Vst3KernelHost* h = host();
        if (!h || !ctx_.telemetry) return;
        vicFast_->setLit(h->c64VicFast());
        cpuFast_->setLit(h->c64CpuFast());
        const auto& t = *ctx_.telemetry;
        std::vector<std::string> lines;
        if (h->isSidFileLoaded()) {
            lines.push_back(fmt("!%s", t.psidTitle));
            lines.push_back(fmt("%s   (%s)", t.psidAuthor, t.psidReleased));
            lines.push_back(fmt("song %u / %u    load $%04X  init $%04X  play $%04X", t.psidCurrentSubtune + 1u,
                                std::max<unsigned>(1u, t.psidSongs), t.psidLoadAddress, t.psidInitAddress,
                                t.psidPlayAddress));
            lines.push_back(fmt("%s  %s   play calls %u   %.1f Hz", t.c64Pal ? "PAL" : "NTSC",
                                t.c64SidModel ? "8580" : "6581", t.c64PlayCalls, static_cast<double>(t.c64PlayRateHz)));
        } else {
            lines.push_back("!No .sid file loaded - use LOAD .SID...");
            lines.push_back("PSID and RSID tunes play through the emulated C64 (6510, VIC-II, CIAs, SID).");
            lines.push_back("RSID tunes need your own KERNAL/BASIC/CHARGEN ROM dumps; none are bundled.");
        }
        lines.push_back(status_);
        info_->setLines(std::move(lines));
    }

private:
    void action(int i) {
        Vst3KernelHost* h = host();
        if (!h) return;
        switch (i) {
            case 0:
                chooseFile(this, false, "Load C64 SID tune", "C64 SID tune", "sid", {}, [this](std::string p) {
                    Vst3KernelHost* hh = host();
                    if (!hh) return;
                    auto bytes = readFileBytes(p, 1u << 20);
                    if (bytes.empty()) {
                        status_ = "could not read " + p;
                        return;
                    }
                    sidFile_ = std::move(bytes);
                    subtune_ = 0;
                    status_ = hh->loadSidFile(sidFile_.data(), sidFile_.size(), 0) ? "loaded" : "not a valid PSID/RSID file";
                });
                break;
            case 1:
                h->unloadSidFile();
                sidFile_.clear();
                status_ = "ejected";
                break;
            case 2:
            case 3: {
                if (sidFile_.empty()) break;
                const int songs = std::max<int>(1, ctx_.telemetry ? ctx_.telemetry->psidSongs : 1);
                subtune_ = (subtune_ + (i == 3 ? 1 : songs - 1)) % songs;
                h->loadSidFile(sidFile_.data(), sidFile_.size(), static_cast<uint16_t>(subtune_));
                break;
            }
            default:
                h->c64ControlHubCommand(i - 3); // BOOT=1 START=2 STOP=3 RESET=4
                break;
        }
    }

    ActionButton* vicFast_ = nullptr;
    ActionButton* cpuFast_ = nullptr;
    TextGrid* info_ = nullptr;
    std::vector<uint8_t> sidFile_;
    int subtune_ = 0;
    std::string status_;
};

DisplayInstance makeC64Machine(const CRect& r, EditorContext& ctx) {
    auto* v = new TextGrid(r, ctx.theme, 11.0);
    DisplayInstance d{v};
    d.wantsC64 = true;
    d.refresh = [v, &ctx]() {
        if (!ctx.telemetry) return;
        const auto& t = *ctx.telemetry;
        std::vector<std::string> lines;
        lines.push_back(fmt("!6510  PC %04X  A %02X  X %02X  Y %02X  SP %02X  P %02X  %s%s%s", t.c64CpuPc, t.c64CpuA,
                            t.c64CpuX, t.c64CpuY, t.c64CpuSp, t.c64CpuStatus, t.c64CpuJammed ? "JAM " : "",
                            t.c64IrqLine ? "IRQ " : "", t.c64NmiLine ? "NMI" : ""));
        for (int i = 0; i < 3; ++i)
            if (t.c64DisasmLine[i][0]) lines.push_back(std::string("  ") + t.c64DisasmLine[i]);
        lines.push_back(fmt("!VIC-II  raster %3u  cycle %2u  %s%s%s  bank %u  frame %llu", t.c64VicRaster, t.c64VicCycle,
                            t.c64VicBadline ? "BADLINE " : "", t.c64VicBa ? "BA " : "", t.c64VicIrq ? "IRQ" : "",
                            t.c64VicMemoryBank, static_cast<unsigned long long>(t.c64VicFrame)));
        for (int c = 0; c < 2; ++c)
            lines.push_back(fmt("!CIA%d  TA %04X/%04X  TB %04X/%04X  ICR %02X  TOD %02X:%02X:%02X.%X", c + 1,
                                t.c64CiaTimerA[c], t.c64CiaLatchA[c], t.c64CiaTimerB[c], t.c64CiaLatchB[c],
                                c == 0 ? t.c64Cia1Irq : t.c64Cia2Irq, t.c64CiaTod[c][3], t.c64CiaTod[c][2],
                                t.c64CiaTod[c][1], t.c64CiaTod[c][0]));
        lines.push_back(fmt("SID  last write $D4%02X = %02X   open bus %02X   chips %u", t.c64LastSidReg,
                            t.c64LastSidValue, t.c64OpenBus, t.c64SidChipCount));
        lines.push_back(fmt("PHI2 %llu   block %llu   %s   ROMs: %s", static_cast<unsigned long long>(t.c64Phi2Cycle),
                            static_cast<unsigned long long>(t.c64BlockIndex), t.c64RealtimeRunning ? "running" : "halted",
                            t.c64ExternalCompleteRomSet ? "user set loaded" : "not loaded"));
        v->setLines(std::move(lines));
    };
    return d;
}

template <typename Panel> DisplayInstance panelDisplay(const CRect& r, EditorContext& ctx, bool scopes, bool c64) {
    if (!ctx.backend.kernelHost()) return DisplayInstance{unavailable(r, ctx)};
    auto* p = new Panel(r, ctx);
    DisplayInstance d{p};
    d.refresh = [p]() {
        p->pollModelGeneration();
        p->refresh();
    };
    d.wantsScopes = scopes;
    d.wantsC64 = c64;
    return d;
}

} // namespace

DisplayInstance createDisplay(L::Display d, const CRect& r, EditorContext& ctx) {
    switch (d) {
        case L::Display::Oscilloscope: return makeOscilloscope(r, ctx);
        case L::Display::FilterResponse: return makeFilterResponse(r, ctx);
        case L::Display::FilterScopes: return makeFilterScopes(r, ctx);
        case L::Display::LfoWaves: return makeLfoWaves(r, ctx);
        case L::Display::VcoScopes: return makeVcoScopes(r, ctx);
        case L::Display::SidRegisters: return makeSidRegisters(r, ctx);
        case L::Display::SeqSteps: return makeSeqSteps(r, ctx);
        case L::Display::DrumMeters: return makeDrumMeters(r, ctx);
        case L::Display::ForensicMeters: return makeForensicMeters(r, ctx);
        case L::Display::HiFiMeters: return makeHiFiMeters(r, ctx);
        case L::Display::ModMatrixMonitor: return makeModMonitor(r, ctx);
        case L::Display::SidCoreTimeline: return makeSidCoreTimeline(r, ctx);
        case L::Display::C64Machine: return makeC64Machine(r, ctx);
        case L::Display::Actions: return makeActions(r, ctx);
        case L::Display::C64Player: return panelDisplay<C64PlayerPanel>(r, ctx, false, true);
        case L::Display::Bank: return panelDisplay<BankPanel>(r, ctx, false, false);
        case L::Display::Settings: return panelDisplay<SettingsPanel>(r, ctx, false, false);
        case L::Display::Mix: return panelDisplay<MixPanel>(r, ctx, false, false);
        case L::Display::Kit: return panelDisplay<KitPanel>(r, ctx, false, false);
        case L::Display::Digi: return panelDisplay<DigiPanel>(r, ctx, true, false);
        case L::Display::None:
        default: return DisplayInstance{};
    }
}

} // namespace ArpSID::Editor
