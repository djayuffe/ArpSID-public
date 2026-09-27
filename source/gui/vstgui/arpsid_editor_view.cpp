// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — cross-platform editor root view (see header).

#include "gui/vstgui/arpsid_editor_view.h"
#include "gui/vstgui/arpsid_editor_labels.h"

#include "vst3/arpsid_vst3_kernel_host.h"

#include "arpsid/core/sid_parameter_presentation.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid/version.h"
#include "factory_patch_params.h"
#include "parameter_ids.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/controls/coptionmenu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ArpSID::Editor {

using namespace VSTGUI;
namespace L = GUI::EditorLayout;

namespace {

constexpr CCoord kMargin = 8.0;
constexpr CCoord kHeaderH = 46.0;
constexpr CCoord kTabH = 30.0;
constexpr CCoord kKeyboardH = 62.0;

// Control cell geometry inside sections.
constexpr CCoord kKnobW = 66.0, kKnobH = 76.0;
constexpr CCoord kStackW = 112.0;
constexpr CCoord kMenuH = 37.0, kToggleH = 27.0;

enum class ControlKind { Knob, Toggle, Menu };

ControlKind controlKindFor(int id) {
    const int steps = static_cast<int>(normalizedParamStepCount(id));
    if (steps == 1) return ControlKind::Toggle;
    if (steps >= 2 && steps <= 32) return ControlKind::Menu;
    return ControlKind::Knob;
}

// Short control caption: the parameter name without its section prefix.
std::string shortLabel(int id) {
    std::string n = kParamInfos[static_cast<std::size_t>(id)].name ? kParamInfos[static_cast<std::size_t>(id)].name : "";
    static const char* kPrefixes[] = {"VCO1 ", "VCO2 ", "VCO3 ", "LFO1 ", "LFO2 ", "LFO3 ", "LFO4 ", "LFO ",
                                      "Arp ", "Seq ", "Filter ", "HI-FI ", "Mod: ", "Limiter ", "Forensic ", "C64 "};
    for (const char* p : kPrefixes) {
        const std::size_t len = std::char_traits<char>::length(p);
        if (n.size() > len + 2 && n.compare(0, len, p) == 0) {
            n = n.substr(len);
            break;
        }
    }
    return n;
}

} // namespace

// ── parameter controls ─────────────────────────────────────────────────────

VSTGUI::CView* createParamControl(int id, const CRect& r, EditorContext& ctx) {
    EditorBackend* backend = &ctx.backend;
    ValueFormatter fmt = [backend](int tag, float norm) {
        std::string s = editorValueText(tag, norm);
        return s.empty() ? backend->paramText(tag, norm) : s;
    };
    switch (controlKindFor(id)) {
        case ControlKind::Toggle: {
            auto* t = new ParamToggle(r, ctx.paramListener, id, shortLabel(id), ctx.theme);
            t->setValueNormalized(ctx.backend.param(id));
            ctx.registerControl(id, t);
            return t;
        }
        case ControlKind::Menu: {
            auto* m = new ParamMenu(r, ctx.paramListener, id, shortLabel(id),
                                    static_cast<int>(normalizedParamStepCount(id)), ctx.theme, fmt);
            m->setNormalized(ctx.backend.param(id));
            ctx.registerMenu(id, m);
            return m;
        }
        case ControlKind::Knob:
        default: {
            auto* k = new ParamKnob(r, ctx.paramListener, id, shortLabel(id), ctx.theme, fmt,
                                    kParamInfos[static_cast<std::size_t>(id)].defaultNorm);
            k->setValueNormalized(ctx.backend.param(id));
            ctx.registerControl(id, k);
            return k;
        }
    }
}

// ── EditorView ─────────────────────────────────────────────────────────────

EditorView::EditorView(EditorBackend& backend)
    : CViewContainer(CRect(0, 0, kWidth, kHeight)), backend_(backend), telemetry_(std::make_unique<ArpSIDTelemetry>()) {
    if (Vst3KernelHost* host = backend_.kernelHost()) {
        const auto s = host->settings();
        themeId_ = s.theme;
    }
    theme_ = makeTheme(themeId_);
    std::memset(telemetry_.get(), 0, sizeof(ArpSIDTelemetry));
    telemetry_->lastMidiNote = -1;

    ctx_ = std::make_unique<EditorContext>(backend_, theme_, this);
    ctx_->telemetry = backend_.kernelHost() ? telemetry_.get() : nullptr;
    if (Vst3KernelHost* host = backend_.kernelHost()) ctx_->language = host->settings().language;
    ctx_->registerControl = [this](int id, CControl* c) { controls_.emplace(id, c); };
    ctx_->registerMenu = [this](int id, ParamMenu* m) { menus_.emplace(id, m); };
    ctx_->themeChanged = [this]() { applyTheme_(); };

    setTransparency(false);
    buildHeader_();

    const auto& tabs = L::tabs();
    std::vector<std::string> names;
    for (const auto& t : tabs) names.emplace_back(GUI::tabSpec(t.id).displayName);
    tabs_ = new TabStrip(CRect(kMargin, kHeaderH, kWidth - kMargin, kHeaderH + kTabH), names, theme_,
                         [this](int i) { selectTab(i); });
    addView(tabs_);

    pageArea_ = CRect(kMargin, kHeaderH + kTabH + 4, kWidth - kMargin, kHeight - kKeyboardH - kMargin - 4);
    pages_.resize(tabs.size());

    keyboard_ = new KeyboardView(CRect(kMargin, kHeight - kKeyboardH - kMargin, kWidth - kMargin, kHeight - kMargin),
                                 theme_, 36, 5,
                                 [this](int note, int vel) {
                                     backend_.sendMidi(0x90, static_cast<uint8_t>(note), static_cast<uint8_t>(vel));
                                 },
                                 [this](int note) { backend_.sendMidi(0x80, static_cast<uint8_t>(note), 0); });
    addView(keyboard_);
    selectTab(0);
}

EditorView::~EditorView() = default;

int EditorView::tabCount() const { return static_cast<int>(L::tabs().size()); }

void EditorView::drawBackgroundRect(CDrawContext* ctx, const CRect& r) {
    ctx->setFillColor(theme_.bg);
    ctx->drawRect(r, kDrawFilled);
}

void EditorView::buildHeader_() {
    const CCoord y = 6.0;
    auto* title = new Label(CRect(kMargin, y, 150, y + 32), "ArpSID", theme_, 24.0, true);
    title->setColor(&theme_.title);
    addView(title);
    auto* ver = new Label(CRect(122, y + 14, 200, y + 30), std::string("v") + ARPSID_PLUGIN_VERSION, theme_, 10.0);
    ver->setColor(&theme_.label);
    addView(ver);

    addView(new ActionButton(CRect(210, y + 4, 238, y + 30), "<", theme_, [this]() {
        backend_.selectFactoryPatch(std::max(0, backend_.currentFactorySlot() - 1));
    }));
    patchMenu_ = new COptionMenu(CRect(242, y + 4, 560, y + 30), this, -2);
    styleMenu(patchMenu_, theme_);
    patchMenu_->setFont(uiFont(12.0, true));
    for (int i = 0; i < kCanonicalFactoryPatchSlotCount; ++i) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%03d  %s", i + 1, factoryPatchNameForSlot(i).c_str());
        patchMenu_->addEntry(buf);
    }
    addView(patchMenu_);
    addView(new ActionButton(CRect(564, y + 4, 592, y + 30), ">", theme_, [this]() {
        backend_.selectFactoryPatch(std::min(kCanonicalFactoryPatchSlotMax, backend_.currentFactorySlot() + 1));
    }));

    status_ = new Label(CRect(606, y, 960, y + 34), "", theme_, 10.5);
    status_->setColor(&theme_.value);
    addView(status_);
    outMeter_ = new MeterView(CRect(970, y + 4, kWidth - kMargin, y + 30), theme_, 2);
    addView(outMeter_);
    patchLabel_ = nullptr;
}

void EditorView::selectTab(int index) {
    if (index < 0 || index >= tabCount()) return;
    if (currentTab_ == index) return;
    if (currentTab_ >= 0 && pages_[static_cast<std::size_t>(currentTab_)].view)
        pages_[static_cast<std::size_t>(currentTab_)].view->setVisible(false);
    if (!pages_[static_cast<std::size_t>(index)].view) buildPage_(index);
    pages_[static_cast<std::size_t>(index)].view->setVisible(true);
    currentTab_ = index;
    if (tabs_) tabs_->setSelected(index);
    invalid();
}

void EditorView::buildAllPages() {
    for (int i = 0; i < tabCount(); ++i)
        if (!pages_[static_cast<std::size_t>(i)].view) {
            buildPage_(i);
            pages_[static_cast<std::size_t>(i)].view->setVisible(i == currentTab_);
        }
}

void EditorView::buildPage_(int index) {
    const L::Tab& tab = L::tabs()[static_cast<std::size_t>(index)];
    auto* page = new CViewContainer(pageArea_);
    page->setTransparency(true);
    Page& p = pages_[static_cast<std::size_t>(index)];
    p.view = page;

    float totalH = 0.f;
    for (const auto& row : tab.rows) {
        if (row.heightWeight <= 0.f) break;
        totalH += row.heightWeight;
    }
    const CCoord gap = 6.0;
    const CCoord W = pageArea_.getWidth(), H = pageArea_.getHeight();
    int rowCount = 0;
    for (const auto& row : tab.rows) {
        if (row.heightWeight <= 0.f) break;
        ++rowCount;
    }
    CCoord y = 0.0;
    for (int ri = 0; ri < rowCount; ++ri) {
        const auto& row = tab.rows[static_cast<std::size_t>(ri)];
        const CCoord rowH = (H - gap * (rowCount - 1)) * row.heightWeight / totalH;
        float totalW = 0.f;
        int secCount = 0;
        for (const auto& s : row.sections) {
            if (!s.title) break;
            totalW += s.widthWeight;
            ++secCount;
        }
        CCoord x = 0.0;
        for (int si = 0; si < secCount; ++si) {
            const auto& sec = row.sections[static_cast<std::size_t>(si)];
            const CCoord w = (W - gap * (secCount - 1)) * sec.widthWeight / totalW;
            auto* panel = new SectionPanel(CRect(x, y, x + w, y + rowH), sec.title, theme_);
            const CRect content = panel->contentRect();
            const bool hasParams = sec.params[0] != L::kEnd;
            if (sec.display == L::Display::None) {
                placeSectionParams_(panel, sec, content);
            } else if (hasParams) {
                // Parameters on the left, the display on the right.
                CRect left = content, right = content;
                left.right = content.left + content.getWidth() * 0.56;
                right.left = left.right + 6;
                placeSectionParams_(panel, sec, left);
                DisplayInstance d = createDisplay(sec.display, right, *ctx_);
                if (d.view) panel->addView(d.view);
                p.displays.push_back(std::move(d));
            } else {
                DisplayInstance d = createDisplay(sec.display, content, *ctx_);
                if (d.view) panel->addView(d.view);
                p.displays.push_back(std::move(d));
            }
            page->addView(panel);
            x += w + gap;
        }
        y += rowH + gap;
    }
    addView(page);
}

void EditorView::placeSectionParams_(SectionPanel* panel, const L::Section& sec, CRect area) {
    // Flow layout: knobs take a cell each; toggles and menus are stacked
    // two-high in wider cells.
    CCoord x = area.left, y = area.top;
    const CCoord rowH = kKnobH;
    CCoord stackX = -1, stackUsed = 0;
    auto newCell = [&](CCoord w) {
        if (x + w > area.right + 0.5 && x > area.left) {
            x = area.left;
            y += rowH + 4;
        }
        const CCoord cx = x;
        x += w + 2;
        return cx;
    };
    for (int id : sec.params) {
        if (id == L::kEnd) break;
        const ControlKind kind = controlKindFor(id);
        if (kind == ControlKind::Knob) {
            stackX = -1;
            const CCoord cx = newCell(kKnobW);
            panel->addView(createParamControl(id, CRect(cx, y, cx + kKnobW, y + kKnobH), *ctx_));
            continue;
        }
        const CCoord h = (kind == ControlKind::Menu) ? kMenuH : kToggleH;
        if (stackX < 0 || stackUsed + h > rowH) {
            stackX = newCell(kStackW);
            stackUsed = 0;
        }
        const CCoord top = y + stackUsed + (kind == ControlKind::Toggle ? 4.0 : 0.0);
        panel->addView(createParamControl(id, CRect(stackX, top, stackX + kStackW, top + h), *ctx_));
        stackUsed += h + 4;
    }
}

void EditorView::valueChanged(CControl* control) {
    const int tag = control->getTag();
    if (control == patchMenu_) {
        backend_.selectFactoryPatch(static_cast<int>(patchMenu_->getCurrentIndex()));
        return;
    }
    if (tag < 0 || tag >= kNumParams) return;
    if (auto* menu = dynamic_cast<COptionMenu*>(control)) {
        const int steps = std::max(1, static_cast<int>(normalizedParamStepCount(tag)));
        const float norm = static_cast<float>(menu->getCurrentIndex()) / static_cast<float>(steps);
        editorSetParam(backend_, tag, norm);
        return;
    }
    const float v = control->getValueNormalized();
    if (editing_.count(control)) backend_.performEdit(tag, v);
    else editorSetParam(backend_, tag, v);
    // Keep sibling controls of the same parameter (on other tabs) in sync.
    auto range = controls_.equal_range(tag);
    for (auto it = range.first; it != range.second; ++it)
        if (it->second != control) {
            it->second->setValueNormalized(v);
            it->second->invalid();
        }
}

void EditorView::controlBeginEdit(CControl* control) {
    const int tag = control->getTag();
    if (tag < 0 || tag >= kNumParams || dynamic_cast<COptionMenu*>(control)) return;
    editing_[control] = true;
    backend_.beginEdit(tag);
}

void EditorView::controlEndEdit(CControl* control) {
    const int tag = control->getTag();
    if (!editing_.erase(control)) return;
    backend_.endEdit(tag);
}

void EditorView::refreshParams_() {
    for (auto& [id, c] : controls_) {
        if (editing_.count(c)) continue;
        const float v = backend_.param(id);
        if (std::fabs(c->getValueNormalized() - v) > 1e-5f) {
            c->setValueNormalized(v);
            c->invalid();
        }
    }
    for (auto& [id, m] : menus_) m->setNormalized(backend_.param(id));
}

void EditorView::refreshHeader_() {
    const int slot = backend_.currentFactorySlot();
    if (slot != shownPatch_ && slot >= 0) {
        shownPatch_ = slot;
        patchMenu_->setCurrent(slot);
        patchMenu_->invalid();
    }
    const ArpSIDTelemetry& t = *telemetry_;
    if (ctx_->telemetry) {
        outMeter_->setLevel(0, t.peakL);
        outMeter_->setLevel(1, t.peakR);
        static const char* kModes[] = {"CLASSIC", "SYNTH", "DRSID", "C64"};
        const char* mode = t.psidActive ? "C64 PLAYER"
                                        : (t.renderMode >= 0 && t.renderMode < 4 ? kModes[t.renderMode] : "?");
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s   voices %d   %.1f BPM %s   %s%s", mode, t.activeVoices,
                      t.hostTempo, t.hostPlaying ? "PLAY" : "STOP", t.arpEnabled ? "ARP " : "",
                      t.seqEnabled ? "SEQ" : "");
        status_->setText(buf);
        keyboard_->setActiveNote(t.lastMidiNote);
    } else {
        status_->setText("engine telemetry unavailable (processor out of process)");
    }
}

void EditorView::applyTheme_() {
    if (Vst3KernelHost* host = backend_.kernelHost()) {
        const auto s = host->settings();
        ctx_->language = s.language;
        if (s.theme != themeId_) {
            themeId_ = s.theme;
            theme_ = makeTheme(themeId_);
            for (auto& p : pages_)
                if (p.view) {
                    // Menus copy their colours at creation: restyle them.
                    p.view->forEachChild([this](CView* v) {
                        if (auto* c = dynamic_cast<CViewContainer*>(v))
                            c->forEachChild([this](CView* inner) {
                                if (auto* m = dynamic_cast<COptionMenu*>(inner)) styleMenu(m, theme_);
                                if (auto* pm = dynamic_cast<ParamMenu*>(inner)) styleMenu(pm->menu(), theme_);
                                if (auto* cm = dynamic_cast<ChoiceMenu*>(inner))
                                    cm->forEachChild([this](CView* x) {
                                        if (auto* m = dynamic_cast<COptionMenu*>(x)) styleMenu(m, theme_);
                                    });
                            });
                    });
                }
            styleMenu(patchMenu_, theme_);
            patchMenu_->setFont(uiFont(12.0, true));
            invalid();
        }
    }
}

void EditorView::refresh() {
    Page* page = (currentTab_ >= 0) ? &pages_[static_cast<std::size_t>(currentTab_)] : nullptr;
    if (Vst3KernelHost* host = backend_.kernelHost()) {
        bool scopes = false, c64 = false;
        if (page)
            for (const auto& d : page->displays) {
                scopes |= d.wantsScopes;
                c64 |= d.wantsC64;
            }
        host->pollNonRealtime();
        host->readTelemetry(*telemetry_, scopes, c64);
    }
    refreshParams_();
    refreshHeader_();
    if (page)
        for (auto& d : page->displays)
            if (d.refresh) d.refresh();
}

} // namespace ArpSID::Editor
