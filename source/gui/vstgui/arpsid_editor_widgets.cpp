// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VSTGUI widgets for the cross-platform editor (see header).

#include "gui/vstgui/arpsid_editor_widgets.h"
#include "gui/vstgui/arpsid_editor_labels.h"

#include "arpsid/gui/theme_palette_v552.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace ArpSID::Editor {

using namespace VSTGUI;

namespace {

CColor toColor(const GUI::ThemeColorRGBA_v552& c) {
    return CColor(static_cast<uint8_t>(std::clamp(c.r, 0.f, 1.f) * 255.f),
                  static_cast<uint8_t>(std::clamp(c.g, 0.f, 1.f) * 255.f),
                  static_cast<uint8_t>(std::clamp(c.b, 0.f, 1.f) * 255.f),
                  static_cast<uint8_t>(std::clamp(c.a, 0.f, 1.f) * 255.f));
}

CColor mix(const CColor& a, const CColor& b, float t) {
    auto m = [t](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>(std::lround(x + (static_cast<float>(y) - x) * t));
    };
    return CColor(m(a.red, b.red), m(a.green, b.green), m(a.blue, b.blue), m(a.alpha, b.alpha));
}

CColor withAlpha(CColor c, uint8_t a) {
    c.alpha = a;
    return c;
}

constexpr double kPi = 3.14159265358979323846;

// Strokes an arc (VSTGUI angles: degrees, clockwise from 3 o'clock) through a
// graphics path. CDrawContext::drawArc cannot be used: VSTGUI's cairo backend
// passes the degrees to cairo_arc as radians, so on Linux every arc wraps
// into a full circle. Paths convert the angles on every platform.
void strokeArc(CDrawContext* ctx, const CRect& rect, double startDeg, double endDeg) {
    auto path = owned(ctx->createGraphicsPath());
    if (!path) return;
    path->addArc(rect, startDeg, endDeg, true);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
}

// Draws a label inside rect: shrinks the font (down to 7.5 pt) until the
// text fits, and only then shortens it, so long parameter names stay readable.
void drawFitted(CDrawContext* ctx, const std::string& text, const CRect& rect, CHoriTxtAlign align,
                CCoord size, bool bold = false) {
    const CCoord avail = rect.getWidth();
    CCoord sz = size;
    ctx->setFont(uiFont(sz, bold));
    while (sz > 7.5 && ctx->getStringWidth(text.c_str()) > avail) {
        sz -= 0.5;
        ctx->setFont(uiFont(sz, bold));
    }
    std::string t = text;
    while (t.size() > 1 && ctx->getStringWidth(t.c_str()) > avail) {
        t.pop_back();
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (ctx->getStringWidth((t + ".").c_str()) <= avail) {
            t += ".";
            break;
        }
    }
    ctx->drawString(t.c_str(), rect, align);
}

// Knob arc geometry (VSTGUI angles: degrees, 0 = 3 o'clock, clockwise).
constexpr double kArcStart = 135.0;
constexpr double kArcSweep = 270.0;

} // namespace

Theme makeTheme(GUI::Theme t) {
    const GUI::ThemeColorSet_v552 p = GUI::themePalette_v552(t);
    Theme th{};
    th.bg = toColor(p.bg);
    const bool light = (t == GUI::Theme::Light);
    const CColor black(0, 0, 0, 255), white(255, 255, 255, 255);
    th.bg = mix(th.bg, light ? white : black, light ? 0.0f : 0.55f);
    th.panel = mix(toColor(p.bg), light ? white : black, light ? 0.35f : 0.35f);
    th.panelEdge = toColor(p.border);
    th.title = toColor(p.title);
    th.label = toColor(p.label);
    th.value = toColor(p.value);
    th.border = toColor(p.border);
    th.accent = toColor(p.accent);
    th.inactive = toColor(p.inactive);
    th.ledOn = toColor(p.ledOn);
    th.ledOff = toColor(p.ledOff);
    th.text = light ? CColor(20, 20, 40, 255) : CColor(225, 225, 235, 255);
    th.warn = CColor(230, 90, 70, 255);
    return th;
}

CFontRef uiFont(CCoord size, bool bold) {
    static std::map<std::pair<int, bool>, SharedPointer<CFontDesc>> cache;
    const auto key = std::make_pair(static_cast<int>(std::lround(size * 10.0)), bold);
    auto it = cache.find(key);
    if (it == cache.end()) {
        auto f = makeOwned<CFontDesc>(kNormalFont->getName(), size, bold ? kBoldFace : kNormalFace);
        it = cache.emplace(key, f).first;
    }
    return it->second;
}

CFontRef monoFont(CCoord size) {
    static std::map<int, SharedPointer<CFontDesc>> cache;
    const int key = static_cast<int>(std::lround(size * 10.0));
    auto it = cache.find(key);
    if (it == cache.end()) {
#if defined(_WIN32)
        const char* name = "Consolas";
#else
        const char* name = "Monospace";
#endif
        it = cache.emplace(key, makeOwned<CFontDesc>(name, size)).first;
    }
    return it->second;
}

void styleMenu(COptionMenu* m, const Theme& theme) {
    if (!m) return;
    m->setFont(uiFont(11.0));
    m->setFontColor(theme.value);
    m->setBackColor(mix(theme.panel, theme.bg, 0.5f));
    m->setFrameColor(theme.border);
    m->setStyle(CParamDisplay::kRoundRectStyle);
    m->setRoundRectRadius(3.0);
    m->setHoriAlign(kCenterText);
}

// ── ParamKnob ────────────────────────────────────────────────────────────────

ParamKnob::ParamKnob(const CRect& r, IControlListener* l, int tag, std::string label, const Theme& theme,
                     ValueFormatter fmt, float defaultValue)
    : CControl(r, l, tag), label_(std::move(label)), theme_(theme), fmt_(std::move(fmt)), default_(defaultValue) {
    setWheelInc(0.01f);
}

void ParamKnob::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    const CCoord labelH = 13.0, valueH = 13.0;
    const CCoord dia = std::min(r.getWidth() - 8.0, r.getHeight() - labelH - valueH - 2.0);
    CRect knob(0, 0, dia, dia);
    knob.offset(r.left + (r.getWidth() - dia) / 2.0, r.top + labelH);

    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    ctx->setFontColor(theme_.label);
    // Drawing is clipped to the control, so the caption must fit its width.
    drawFitted(ctx, label_, CRect(r.left + 1, r.top, r.right - 1, r.top + labelH), kCenterText, 10.0);

    const float v = std::clamp(getValueNormalized(), 0.f, 1.f);
    ctx->setLineWidth(3.0);
    ctx->setFrameColor(mix(theme_.inactive, theme_.panel, 0.3f));
    CRect arc = knob;
    arc.inset(2.5, 2.5);
    strokeArc(ctx, arc, kArcStart, kArcStart + kArcSweep);
    if (v > 0.001f) {
        ctx->setFrameColor(isEditing() ? theme_.title : theme_.accent);
        strokeArc(ctx, arc, kArcStart, kArcStart + kArcSweep * v);
    }
    CRect body = knob;
    body.inset(dia * 0.22, dia * 0.22);
    ctx->setFillColor(mix(theme_.panel, theme_.bg, 0.6f));
    ctx->drawEllipse(body, kDrawFilled);
    const double a = (kArcStart + kArcSweep * v) * kPi / 180.0;
    const CPoint c = body.getCenter();
    const CCoord rad = body.getWidth() / 2.0;
    ctx->setLineWidth(2.0);
    ctx->setFrameColor(theme_.value);
    ctx->drawLine(CPoint(c.x + std::cos(a) * rad * 0.25, c.y + std::sin(a) * rad * 0.25),
                  CPoint(c.x + std::cos(a) * rad * 0.95, c.y + std::sin(a) * rad * 0.95));

    const std::string text = fmt_ ? fmt_(getTag(), v) : std::to_string(static_cast<int>(v * 100.f));
    ctx->setFontColor(theme_.value);
    drawFitted(ctx, text, CRect(r.left, r.bottom - valueH, r.right, r.bottom), kCenterText, 10.0);
    setDirty(false);
}

void ParamKnob::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    if (e.clickCount == 2 || e.modifiers.has(ModifierKey::Control)) {
        beginEdit();
        setValueNormalized(default_);
        valueChanged();
        endEdit();
        invalid();
        e.consumed = true;
        return;
    }
    anchor_ = e.mousePosition;
    anchorValue_ = getValueNormalized();
    dragging_ = true;
    beginEdit();
    e.consumed = true;
}

void ParamKnob::onMouseMoveEvent(MouseMoveEvent& e) {
    if (!dragging_) return;
    const bool fine = e.modifiers.has(ModifierKey::Shift);
    const double dy = anchor_.y - e.mousePosition.y + (e.mousePosition.x - anchor_.x) * 0.25;
    const float v = std::clamp(anchorValue_ + static_cast<float>(dy / (fine ? 1200.0 : 180.0)), 0.f, 1.f);
    if (v != getValueNormalized()) {
        setValueNormalized(v);
        valueChanged();
        invalid();
    }
    e.consumed = true;
}

void ParamKnob::onMouseUpEvent(MouseUpEvent& e) {
    if (dragging_) {
        dragging_ = false;
        endEdit();
        invalid();
        e.consumed = true;
    }
}

void ParamKnob::onMouseCancelEvent(MouseCancelEvent& e) {
    if (dragging_) {
        dragging_ = false;
        endEdit();
    }
    e.consumed = true;
}

void ParamKnob::onMouseWheelEvent(MouseWheelEvent& e) {
    const float step = e.modifiers.has(ModifierKey::Shift) ? 0.002f : 0.02f;
    const float v = std::clamp(getValueNormalized() + static_cast<float>(e.deltaY) * step, 0.f, 1.f);
    beginEdit();
    setValueNormalized(v);
    valueChanged();
    endEdit();
    invalid();
    e.consumed = true;
}

// ── ParamToggle ─────────────────────────────────────────────────────────────

ParamToggle::ParamToggle(const CRect& r, IControlListener* l, int tag, std::string label, const Theme& theme)
    : CControl(r, l, tag), label_(std::move(label)), theme_(theme) {}

void ParamToggle::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    const bool on = getValueNormalized() > 0.5f;
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    CRect box(r.left + 3, r.top + 4, r.right - 3, r.bottom - 4);
    auto path = owned(ctx->createGraphicsPath());
    if (path) {
        path->addRoundRect(box, 4.0);
        ctx->setFillColor(on ? withAlpha(theme_.ledOn, 60) : mix(theme_.panel, theme_.bg, 0.5f));
        ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
        ctx->setFrameColor(on ? theme_.ledOn : theme_.border);
        ctx->setLineWidth(1.0);
        ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    CRect led(box.left + 6, box.getCenter().y - 4, box.left + 14, box.getCenter().y + 4);
    ctx->setFillColor(on ? theme_.ledOn : theme_.ledOff);
    ctx->drawEllipse(led, kDrawFilled);
    ctx->setFontColor(on ? theme_.value : theme_.label);
    drawFitted(ctx, label_, CRect(led.right + 4, box.top, box.right - 3, box.bottom), kLeftText, 10.0, on);
    setDirty(false);
}

void ParamToggle::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    beginEdit();
    setValueNormalized(getValueNormalized() > 0.5f ? 0.f : 1.f);
    valueChanged();
    endEdit();
    invalid();
    e.consumed = true;
}

// ── ParamMenu ───────────────────────────────────────────────────────────────

ParamMenu::ParamMenu(const CRect& r, IControlListener* l, int tag, std::string label, int steps,
                     const Theme& theme, const ValueFormatter& fmt)
    : CViewContainer(r), label_(std::move(label)), theme_(theme), steps_(std::max(1, steps)) {
    setTransparency(true);
    const CCoord w = r.getWidth();
    menu_ = new COptionMenu(CRect(2, 15, w - 2, r.getHeight() - 4), l, tag);
    styleMenu(menu_, theme);
    for (int i = 0; i <= steps_; ++i) {
        const float norm = static_cast<float>(i) / static_cast<float>(steps_);
        menu_->addEntry(fmt ? fmt(tag, norm).c_str() : std::to_string(i).c_str());
    }
    addView(menu_);
}

void ParamMenu::setNormalized(float v) {
    const int idx = std::clamp(stepIndexForParam(menu_->getTag(), v), 0, steps_);
    if (menu_->getCurrentIndex() != idx) menu_->setCurrent(idx);
}

void ParamMenu::setViewSize(const CRect& r, bool inv) {
    CViewContainer::setViewSize(r, inv);
    if (menu_) {
        const CRect m(2, 15, r.getWidth() - 2, r.getHeight() - 4);
        menu_->setViewSize(m, inv);
        menu_->setMouseableArea(m);
    }
}

void ParamMenu::drawBackgroundRect(CDrawContext* ctx, const CRect&) {
    ctx->setFontColor(theme_.label);
    drawFitted(ctx, label_, CRect(0, 0, getWidth(), 14), kCenterText, 10.0);
}

// ── SectionPanel ────────────────────────────────────────────────────────────

SectionPanel::SectionPanel(const CRect& r, std::string title, const Theme& theme)
    : CViewContainer(r), title_(std::move(title)), theme_(theme) {
    setTransparency(true);
}

CRect SectionPanel::contentRect() const {
    return CRect(6, 22, getWidth() - 6, getHeight() - 6);
}

void SectionPanel::drawBackgroundRect(CDrawContext* ctx, const CRect&) {
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    CRect r(0.5, 0.5, getWidth() - 0.5, getHeight() - 0.5);
    auto path = owned(ctx->createGraphicsPath());
    if (path) {
        path->addRoundRect(r, 6.0);
        ctx->setFillColor(theme_.panel);
        ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
        ctx->setFrameColor(withAlpha(theme_.panelEdge, 150));
        ctx->setLineWidth(1.0);
        ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    ctx->setFillColor(theme_.title);
    ctx->drawRect(CRect(8, 8, 12, 12), kDrawFilled);
    ctx->setFont(uiFont(11.0, true));
    ctx->setFontColor(theme_.title);
    ctx->drawString(title_.c_str(), CRect(17, 2, getWidth() - 6, 18), kLeftText);
}

// ── Label ───────────────────────────────────────────────────────────────────

Label::Label(const CRect& r, std::string text, const Theme& theme, CCoord size, bool bold, CHoriTxtAlign align)
    : CView(r), text_(std::move(text)), theme_(theme), size_(size), bold_(bold), align_(align) {}

void Label::setText(std::string t) {
    if (t != text_) {
        text_ = std::move(t);
        invalid();
    }
}

void Label::draw(CDrawContext* ctx) {
    ctx->setFont(uiFont(size_, bold_));
    ctx->setFontColor(color_ ? *color_ : theme_.text);
    ctx->drawString(text_.c_str(), getViewSize(), align_);
    setDirty(false);
}

// ── ActionButton ────────────────────────────────────────────────────────────

ActionButton::ActionButton(const CRect& r, std::string text, const Theme& theme, std::function<void()> onClick)
    : CView(r), text_(std::move(text)), theme_(theme), onClick_(std::move(onClick)) {}

void ActionButton::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    auto path = owned(ctx->createGraphicsPath());
    CRect b(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    if (path) {
        path->addRoundRect(b, 4.0);
        ctx->setFillColor(pressed_ ? theme_.accent : (lit_ ? withAlpha(theme_.accent, 90) : mix(theme_.panel, theme_.bg, 0.4f)));
        ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
        ctx->setFrameColor(lit_ ? theme_.accent : theme_.border);
        ctx->setLineWidth(1.0);
        ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    ctx->setFont(uiFont(10.5, true));
    ctx->setFontColor(pressed_ ? theme_.bg : theme_.text);
    ctx->drawString(text_.c_str(), b, kCenterText);
    setDirty(false);
}

void ActionButton::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    pressed_ = true;
    invalid();
    e.consumed = true;
}

void ActionButton::onMouseUpEvent(MouseUpEvent& e) {
    if (!pressed_) return;
    pressed_ = false;
    invalid();
    e.consumed = true;
    if (getViewSize().pointInside(e.mousePosition) && onClick_) onClick_();
}

// ── TabStrip ────────────────────────────────────────────────────────────────

TabStrip::TabStrip(const CRect& r, std::vector<std::string> names, const Theme& theme, std::function<void(int)> onSelect)
    : CView(r), names_(std::move(names)), theme_(theme), onSelect_(std::move(onSelect)) {}

CRect TabStrip::tabRect_(int i) const {
    const CRect r = getViewSize();
    const CCoord w = r.getWidth() / static_cast<CCoord>(std::max<std::size_t>(1, names_.size()));
    return CRect(r.left + w * i, r.top, r.left + w * (i + 1), r.bottom);
}

void TabStrip::draw(CDrawContext* ctx) {
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    for (int i = 0; i < static_cast<int>(names_.size()); ++i) {
        CRect t = tabRect_(i);
        t.inset(1.5, 2.0);
        const bool sel = (i == sel_);
        auto path = owned(ctx->createGraphicsPath());
        if (path) {
            path->addRoundRect(t, 4.0);
            ctx->setFillColor(sel ? theme_.accent : mix(theme_.panel, theme_.bg, 0.3f));
            ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
            ctx->setFrameColor(sel ? theme_.title : withAlpha(theme_.border, 140));
            ctx->setLineWidth(1.0);
            ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
        ctx->setFont(uiFont(10.0, true));
        ctx->setFontColor(sel ? theme_.bg : theme_.label);
        ctx->drawString(names_[static_cast<std::size_t>(i)].c_str(), t, kCenterText);
    }
    setDirty(false);
}

void TabStrip::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    for (int i = 0; i < static_cast<int>(names_.size()); ++i) {
        if (tabRect_(i).pointInside(e.mousePosition)) {
            setSelected(i);
            if (onSelect_) onSelect_(i);
            break;
        }
    }
    e.consumed = true;
}

// ── ScopeView ───────────────────────────────────────────────────────────────

ScopeView::ScopeView(const CRect& r, const Theme& theme, int traces)
    : CView(r), theme_(theme), traces_(static_cast<std::size_t>(traces)), colors_(static_cast<std::size_t>(traces)) {}

void ScopeView::setTrace(int i, const float* data, int n, CColor color) {
    if (i < 0 || i >= static_cast<int>(traces_.size())) return;
    auto& t = traces_[static_cast<std::size_t>(i)];
    t.assign(data, data + std::max(0, n));
    colors_[static_cast<std::size_t>(i)] = color;
    invalid();
}

void ScopeView::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    ctx->setFillColor(mix(theme_.bg, CColor(0, 0, 0, 255), 0.5f));
    ctx->drawRect(r, kDrawFilled);
    ctx->setLineWidth(1.0);
    ctx->setFrameColor(withAlpha(theme_.border, 60));
    if (laneNames_.empty())
        for (int g = 1; g < 4; ++g) {
            const CCoord y = r.top + r.getHeight() * g / 4.0;
            ctx->drawLine(CPoint(r.left, y), CPoint(r.right, y));
        }
    for (int g = 1; g < 8; ++g) {
        const CCoord x = r.left + r.getWidth() * g / 8.0;
        ctx->drawLine(CPoint(x, r.top), CPoint(x, r.bottom));
    }
    const int nTraces = static_cast<int>(traces_.size());
    const bool stacked = !laneNames_.empty();
    const CCoord nameW = stacked ? 44.0 : 0.0;
    const CCoord captionH = caption_.empty() ? 0.0 : 14.0;
    for (int t = 0; t < nTraces; ++t) {
        const auto& d = traces_[static_cast<std::size_t>(t)];
        // Lane geometry: stacked traces each get a band below the caption.
        CCoord laneTop = r.top, laneH = r.getHeight();
        if (stacked) {
            laneH = (r.getHeight() - captionH) / nTraces;
            laneTop = r.top + captionH + laneH * t;
            if (t > 0) {
                ctx->setFrameColor(withAlpha(theme_.border, 90));
                ctx->drawLine(CPoint(r.left, laneTop), CPoint(r.right, laneTop));
            }
            if (t < static_cast<int>(laneNames_.size())) {
                ctx->setFontColor(colors_[static_cast<std::size_t>(t)]);
                drawFitted(ctx, laneNames_[static_cast<std::size_t>(t)],
                           CRect(r.left + 3, laneTop, r.left + nameW - 2, laneTop + laneH), kLeftText, 9.0, true);
            }
        }
        if (d.size() < 2) continue;
        const CCoord mid = laneTop + laneH / 2.0;
        const CCoord x0 = r.left + nameW, span = r.getWidth() - nameW;
        auto path = owned(ctx->createGraphicsPath());
        if (!path) continue;
        for (std::size_t i = 0; i < d.size(); ++i) {
            const CCoord x = x0 + span * static_cast<CCoord>(i) / static_cast<CCoord>(d.size() - 1);
            const CCoord y = mid - std::clamp(static_cast<CCoord>(d[i]), -1.0, 1.0) * laneH * (stacked ? 0.40 : 0.45);
            if (i == 0) path->beginSubpath(CPoint(x, y));
            else path->addLine(CPoint(x, y));
        }
        ctx->setFrameColor(colors_[static_cast<std::size_t>(t)]);
        ctx->setLineWidth(1.4);
        ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }
    ctx->setFrameColor(withAlpha(theme_.border, 160));
    ctx->setLineWidth(1.0);
    ctx->drawRect(r, kDrawStroked);
    if (!caption_.empty()) {
        ctx->setFont(uiFont(9.5));
        ctx->setFontColor(theme_.label);
        ctx->drawString(caption_.c_str(), CRect(r.left + 4, r.top + 2, r.right - 4, r.top + 14), kLeftText);
    }
    setDirty(false);
}

// ── MeterView ───────────────────────────────────────────────────────────────

MeterView::MeterView(const CRect& r, const Theme& theme, int bars, bool vertical)
    : CView(r), theme_(theme), levels_(static_cast<std::size_t>(bars), 0.f),
      peaks_(static_cast<std::size_t>(bars), 0.f), labels_(static_cast<std::size_t>(bars)),
      bipolar_(static_cast<std::size_t>(bars), false), vertical_(vertical) {}

void MeterView::setBipolar(int i, bool bipolar) {
    if (i >= 0 && i < static_cast<int>(bipolar_.size())) bipolar_[static_cast<std::size_t>(i)] = bipolar;
}

void MeterView::setLevel(int i, float level, std::string label) {
    if (i < 0 || i >= static_cast<int>(levels_.size())) return;
    const bool bip = bipolar_[static_cast<std::size_t>(i)];
    const float l = std::clamp(std::isfinite(level) ? level : 0.f, bip ? -1.f : 0.f, 1.f);
    auto& pk = peaks_[static_cast<std::size_t>(i)];
    pk = bip ? 0.f : std::max(l, pk * 0.92f);
    levels_[static_cast<std::size_t>(i)] = l;
    if (!label.empty()) labels_[static_cast<std::size_t>(i)] = std::move(label);
    invalid();
}

void MeterView::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    const int n = static_cast<int>(levels_.size());
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const bool labelled = std::any_of(labels_.begin(), labels_.end(), [](const std::string& s) { return !s.empty(); });
    for (int i = 0; i < n; ++i) {
        CRect lane;
        CCoord labelSpan = labelled ? 50.0 : 0.0;
        if (vertical_) {
            const CCoord w = r.getWidth() / n;
            lane = CRect(r.left + w * i + 2, r.top, r.left + w * (i + 1) - 2, r.bottom - (labelled ? 14.0 : 0.0));
        } else {
            const CCoord h = r.getHeight() / n;
            lane = CRect(r.left + labelSpan, r.top + h * i + 1, r.right, r.top + h * (i + 1) - 1);
        }
        ctx->setFillColor(mix(theme_.bg, CColor(0, 0, 0, 255), 0.4f));
        ctx->drawRect(lane, kDrawFilled);
        const float l = levels_[static_cast<std::size_t>(i)];
        const float pk = peaks_[static_cast<std::size_t>(i)];
        CRect fill = lane;
        CRect peakMark = lane;
        const bool bip = bipolar_[static_cast<std::size_t>(i)];
        if (bip) {
            // Fill from the centre towards the signed value; mark the centre.
            if (vertical_) {
                const CCoord c = lane.getCenter().y;
                fill.top = std::min(c, c - lane.getHeight() * 0.5 * l);
                fill.bottom = std::max(c, c - lane.getHeight() * 0.5 * l);
            } else {
                const CCoord c = lane.getCenter().x;
                fill.left = std::min(c, c + lane.getWidth() * 0.5 * l);
                fill.right = std::max(c, c + lane.getWidth() * 0.5 * l);
            }
        } else if (vertical_) {
            fill.top = lane.bottom - lane.getHeight() * l;
            peakMark.top = lane.bottom - lane.getHeight() * pk;
            peakMark.bottom = peakMark.top + 2;
        } else {
            fill.right = lane.left + lane.getWidth() * l;
            peakMark.left = lane.left + lane.getWidth() * pk - 2;
            peakMark.right = peakMark.left + 2;
        }
        ctx->setFillColor(std::fabs(l) > 0.95f && !bip ? theme_.warn : theme_.ledOn);
        ctx->drawRect(fill, kDrawFilled);
        if (bip) {
            ctx->setFillColor(withAlpha(theme_.border, 160));
            const CPoint c = lane.getCenter();
            ctx->drawRect(vertical_ ? CRect(lane.left, c.y - 0.5, lane.right, c.y + 0.5)
                                    : CRect(c.x - 0.5, lane.top, c.x + 0.5, lane.bottom),
                          kDrawFilled);
        }
        ctx->setFillColor(theme_.title);
        if (pk > 0.01f) ctx->drawRect(peakMark, kDrawFilled);
        if (labelled) {
            ctx->setFontColor(theme_.label);
            const auto& text = labels_[static_cast<std::size_t>(i)];
            if (vertical_)
                drawFitted(ctx, text, CRect(lane.left - 4, r.bottom - 13, lane.right + 4, r.bottom), kCenterText, 9.0);
            else
                drawFitted(ctx, text, CRect(r.left, lane.top, r.left + labelSpan - 3, lane.bottom), kRightText, 9.0);
        }
    }
    setDirty(false);
}

// ── TextGrid ────────────────────────────────────────────────────────────────

TextGrid::TextGrid(const CRect& r, const Theme& theme, CCoord fontSize)
    : CView(r), theme_(theme), fontSize_(fontSize) {}

void TextGrid::setLines(std::vector<std::string> lines) {
    if (lines != lines_) {
        lines_ = std::move(lines);
        invalid();
    }
}

void TextGrid::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    ctx->setFillColor(mix(theme_.bg, CColor(0, 0, 0, 255), 0.5f));
    ctx->drawRect(r, kDrawFilled);
    ctx->setFont(monoFont(fontSize_));
    const CCoord lh = fontSize_ + 3.0;
    CCoord y = r.top + 3;
    for (const auto& line : lines_) {
        if (y + lh > r.bottom) break;
        // A leading '!' highlights the line.
        const bool hi = !line.empty() && line[0] == '!';
        ctx->setFontColor(hi ? theme_.title : theme_.value);
        ctx->drawString(hi ? line.c_str() + 1 : line.c_str(), CRect(r.left + 5, y, r.right - 4, y + lh), kLeftText);
        y += lh;
    }
    setDirty(false);
}

// ── KeyboardView ────────────────────────────────────────────────────────────

namespace {
constexpr bool kIsBlack[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
constexpr int kWhiteIndex[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
}

KeyboardView::KeyboardView(const CRect& r, const Theme& theme, int firstNote, int octaves,
                           std::function<void(int, int)> noteOn, std::function<void(int)> noteOff)
    : CView(r), theme_(theme), first_(firstNote), octaves_(octaves), noteOn_(std::move(noteOn)),
      noteOff_(std::move(noteOff)) {}

int KeyboardView::noteAt_(const CPoint& p, int* velocity) const {
    const CRect r = getViewSize();
    const int whites = octaves_ * 7 + 1;
    const CCoord ww = r.getWidth() / whites;
    const CCoord bh = r.getHeight() * 0.62;
    const CCoord rel = p.y - r.top;
    if (velocity) *velocity = std::clamp(static_cast<int>(40 + 87 * rel / r.getHeight()), 1, 127);
    const int wi = std::clamp(static_cast<int>((p.x - r.left) / ww), 0, whites - 1);
    if (rel < bh) {
        // Black keys sit between whites.
        for (int o = 0; o <= octaves_; ++o)
            for (int k = 0; k < 12; ++k) {
                if (!kIsBlack[k]) continue;
                const int note = first_ + o * 12 + k;
                if (note > first_ + octaves_ * 12) continue;
                const CCoord cx = r.left + (o * 7 + kWhiteIndex[k] + 1) * ww;
                if (p.x >= cx - ww * 0.32 && p.x <= cx + ww * 0.32) return note;
            }
    }
    const int octave = wi / 7, idx = wi % 7;
    static constexpr int kWhiteSemis[7] = {0, 2, 4, 5, 7, 9, 11};
    return first_ + octave * 12 + kWhiteSemis[idx];
}

void KeyboardView::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    const int whites = octaves_ * 7 + 1;
    const CCoord ww = r.getWidth() / whites;
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    static constexpr int kWhiteSemis[7] = {0, 2, 4, 5, 7, 9, 11};
    for (int w = 0; w < whites; ++w) {
        const int note = first_ + (w / 7) * 12 + kWhiteSemis[w % 7];
        CRect k(r.left + w * ww, r.top, r.left + (w + 1) * ww - 1, r.bottom);
        const bool down = lit_(note);
        ctx->setFillColor(down ? theme_.accent : CColor(236, 236, 240, 255));
        ctx->drawRect(k, kDrawFilled);
        if (w % 7 == 0) {
            ctx->setFont(uiFont(8.5));
            ctx->setFontColor(CColor(90, 90, 110, 255));
            char buf[16];
            std::snprintf(buf, sizeof buf, "C%d", std::clamp(note / 12 - 1, -1, 9));
            ctx->drawString(buf, CRect(k.left, k.bottom - 12, k.right, k.bottom - 1), kCenterText);
        }
    }
    for (int o = 0; o <= octaves_; ++o)
        for (int k = 0; k < 12; ++k) {
            if (!kIsBlack[k]) continue;
            const int note = first_ + o * 12 + k;
            if (note > first_ + octaves_ * 12) continue;
            const CCoord cx = r.left + (o * 7 + kWhiteIndex[k] + 1) * ww;
            CRect b(cx - ww * 0.32, r.top, cx + ww * 0.32, r.top + r.getHeight() * 0.62);
            const bool down = lit_(note);
            ctx->setFillColor(down ? theme_.accent : CColor(24, 24, 30, 255));
            ctx->drawRect(b, kDrawFilled);
        }
    setDirty(false);
}

void KeyboardView::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    int vel = 100;
    held_ = noteAt_(e.mousePosition, &vel);
    if (noteOn_) noteOn_(held_, vel);
    invalid();
    e.consumed = true;
}

void KeyboardView::onMouseMoveEvent(MouseMoveEvent& e) {
    if (held_ < 0) return;
    int vel = 100;
    const int n = noteAt_(e.mousePosition, &vel);
    if (n != held_) {
        if (noteOff_) noteOff_(held_);
        held_ = n;
        if (noteOn_) noteOn_(held_, vel);
        invalid();
    }
    e.consumed = true;
}

void KeyboardView::onMouseUpEvent(MouseUpEvent& e) {
    if (held_ >= 0) {
        if (noteOff_) noteOff_(held_);
        held_ = -1;
        invalid();
    }
    e.consumed = true;
}

void KeyboardView::onMouseCancelEvent(MouseCancelEvent& e) {
    if (held_ >= 0 && noteOff_) noteOff_(held_);
    held_ = -1;
    e.consumed = true;
}

// ── CellGrid ────────────────────────────────────────────────────────────────

CellGrid::CellGrid(const CRect& r, const Theme& theme, int cols, int rows, ClickFn onClick)
    : CView(r), theme_(theme), cols_(cols), rows_(rows),
      cells_(static_cast<std::size_t>(cols * rows)), onClick_(std::move(onClick)) {}

void CellGrid::setCell(int col, int row, Cell c) {
    if (col < 0 || row < 0 || col >= cols_ || row >= rows_) return;
    auto& dst = cells_[static_cast<std::size_t>(row * cols_ + col)];
    if (dst.level != c.level || dst.on != c.on || dst.accent != c.accent || dst.cursor != c.cursor ||
        dst.dim != c.dim || dst.text != c.text) {
        dst = std::move(c);
        invalid();
    }
}

CRect CellGrid::cellRect_(int c, int r) const {
    const CRect v = getViewSize();
    const CCoord labelW = rowLabels_.empty() ? 0.0 : 52.0;
    const CCoord w = (v.getWidth() - labelW) / cols_;
    const CCoord h = v.getHeight() / rows_;
    // Whole-pixel edges keep cell borders and text crisp at 1x.
    return CRect(std::floor(v.left + labelW + c * w), std::floor(v.top + r * h),
                 std::floor(v.left + labelW + (c + 1) * w), std::floor(v.top + (r + 1) * h));
}

bool CellGrid::cellAt_(const CPoint& p, int& c, int& r) const {
    for (r = 0; r < rows_; ++r)
        for (c = 0; c < cols_; ++c)
            if (cellRect_(c, r).pointInside(p)) return true;
    return false;
}

void CellGrid::draw(CDrawContext* ctx) {
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CRect v = getViewSize();
    for (int r = 0; r < rows_; ++r) {
        if (!rowLabels_.empty() && r < static_cast<int>(rowLabels_.size())) {
            const CRect first = cellRect_(0, r);
            ctx->setFont(uiFont(9.5, true));
            ctx->setFontColor(theme_.label);
            ctx->drawString(rowLabels_[static_cast<std::size_t>(r)].c_str(),
                            CRect(v.left, first.top, first.left - 4, first.bottom), kRightText);
        }
        for (int c = 0; c < cols_; ++c) {
            CRect cr = cellRect_(c, r);
            cr.inset(1.5, 1.5);
            const Cell& cell = cells_[static_cast<std::size_t>(r * cols_ + c)];
            const bool beat = (c % 4) == 0;
            ctx->setFillColor(beat ? mix(theme_.panel, theme_.bg, 0.2f) : mix(theme_.panel, theme_.bg, 0.55f));
            ctx->drawRect(cr, kDrawFilled);
            if (cell.on || cell.level > 0.f) {
                CRect f = cr;
                if (cell.level > 0.f && cell.level < 1.f) f.top = cr.bottom - cr.getHeight() * cell.level;
                CColor fc = cell.accent ? theme_.title : (cell.on ? theme_.ledOn : withAlpha(theme_.ledOn, 90));
                if (cell.dim) fc.alpha = static_cast<uint8_t>(fc.alpha / 3);
                ctx->setFillColor(fc);
                ctx->drawRect(f, kDrawFilled);
            }
            if (cell.cursor) {
                ctx->setFrameColor(theme_.accent);
                ctx->setLineWidth(2.0);
                ctx->drawRect(cr, kDrawStroked);
            }
            if (!cell.text.empty()) {
                ctx->setFont(uiFont(8.5));
                ctx->setFontColor(cell.on ? theme_.bg : (cell.dim ? withAlpha(theme_.label, 90) : theme_.label));
                ctx->drawString(cell.text.c_str(), cr, kCenterText);
            }
        }
    }
    setDirty(false);
}

void CellGrid::onMouseDownEvent(MouseDownEvent& e) {
    int c = 0, r = 0;
    if (!cellAt_(e.mousePosition, c, r)) return;
    const bool right = e.buttonState.isRight();
    const CRect cr = cellRect_(c, r);
    const float yFrac = static_cast<float>(std::clamp((cr.bottom - e.mousePosition.y) / cr.getHeight(), 0.0, 1.0));
    if (onClick_) onClick_(c, r, e.modifiers.has(ModifierKey::Shift), right, yFrac);
    dragging_ = dragPaint_ && !right;
    lastC_ = c;
    lastR_ = r;
    e.consumed = true;
}

void CellGrid::onMouseMoveEvent(MouseMoveEvent& e) {
    if (!dragging_) return;
    int c = 0, r = 0;
    if (cellAt_(e.mousePosition, c, r) && (c != lastC_ || r != lastR_ || dragLevel_)) {
        const CRect cr = cellRect_(c, r);
        const float yFrac = static_cast<float>(std::clamp((cr.bottom - e.mousePosition.y) / cr.getHeight(), 0.0, 1.0));
        if (onClick_) onClick_(c, r, e.modifiers.has(ModifierKey::Shift), false, yFrac);
        lastC_ = c;
        lastR_ = r;
    }
    e.consumed = true;
}

void CellGrid::onMouseUpEvent(MouseUpEvent& e) {
    dragging_ = false;
    e.consumed = true;
}

// ── FilterCurveView ─────────────────────────────────────────────────────────

FilterCurveView::FilterCurveView(const CRect& r, const Theme& theme) : CView(r), theme_(theme) {}

void FilterCurveView::setFilter(float cutoff, float resonance, int modeMask) {
    if (cutoff != cutoff_ || resonance != res_ || modeMask != mode_) {
        cutoff_ = cutoff;
        res_ = resonance;
        mode_ = modeMask;
        invalid();
    }
}

void FilterCurveView::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    ctx->setFillColor(mix(theme_.bg, CColor(0, 0, 0, 255), 0.5f));
    ctx->drawRect(r, kDrawFilled);
    ctx->setFrameColor(withAlpha(theme_.border, 60));
    ctx->setLineWidth(1.0);
    for (int d = 1; d < 4; ++d) {
        const CCoord x = r.left + r.getWidth() * d / 4.0;
        ctx->drawLine(CPoint(x, r.top), CPoint(x, r.bottom));
    }
    // SID-style multimode: LP=1, BP=2, HP=4 (combinations add).
    const double fc = 30.0 * std::pow(12000.0 / 30.0, std::clamp(static_cast<double>(cutoff_), 0.0, 1.0));
    const double q = 0.7 + 6.0 * std::clamp(static_cast<double>(res_), 0.0, 1.0);
    auto path = owned(ctx->createGraphicsPath());
    if (!path) return;
    const int n = static_cast<int>(r.getWidth());
    for (int i = 0; i <= n; ++i) {
        const double f = 20.0 * std::pow(1000.0, static_cast<double>(i) / n);
        const double x = f / fc;
        const double denomRe = 1.0 - x * x, denomIm = x / q;
        const double mag2 = denomRe * denomRe + denomIm * denomIm;
        double lp = 1.0 / std::sqrt(mag2), bp = (x / q) / std::sqrt(mag2), hp = (x * x) / std::sqrt(mag2);
        double g = 0.0;
        if (mode_ & 1) g += lp;
        if (mode_ & 2) g += bp;
        if (mode_ & 4) g += hp;
        if ((mode_ & 7) == 0) g = 1.0;
        const double db = 20.0 * std::log10(std::max(g, 1e-4));
        const CCoord y = r.top + r.getHeight() * (0.35 - std::clamp(db, -48.0, 24.0) / 48.0 * 0.6);
        const CPoint p(r.left + i, std::clamp(y, r.top + 1.0, r.bottom - 1.0));
        if (i == 0) path->beginSubpath(p);
        else path->addLine(p);
    }
    ctx->setFrameColor(theme_.accent);
    ctx->setLineWidth(2.0);
    ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
    char buf[48];
    std::snprintf(buf, sizeof buf, "fc %.0f Hz  Q %.1f", fc, q);
    ctx->setFont(uiFont(9.5));
    ctx->setFontColor(theme_.label);
    ctx->drawString(buf, CRect(r.left + 4, r.top + 2, r.right - 4, r.top + 14), kLeftText);
    ctx->setFrameColor(withAlpha(theme_.border, 160));
    ctx->setLineWidth(1.0);
    ctx->drawRect(r, kDrawStroked);
    setDirty(false);
}

// ── LfoWaveView ─────────────────────────────────────────────────────────────

LfoWaveView::LfoWaveView(const CRect& r, const Theme& theme) : CView(r), theme_(theme) {}

void LfoWaveView::setLfo(int i, int shape, float depth, float phase, float value) {
    if (i < 0 || i > 3) return;
    lfo_[static_cast<std::size_t>(i)] = {shape, depth, phase, value};
    invalid();
}

void LfoWaveView::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    const CColor colors[4] = {theme_.accent, theme_.ledOn, theme_.title, theme_.label};
    const CCoord w = r.getWidth() / 4.0;
    for (int i = 0; i < 4; ++i) {
        CRect lane(r.left + w * i + 3, r.top, r.left + w * (i + 1) - 3, r.bottom);
        ctx->setFillColor(mix(theme_.bg, CColor(0, 0, 0, 255), 0.5f));
        ctx->drawRect(lane, kDrawFilled);
        const auto& l = lfo_[static_cast<std::size_t>(i)];
        auto shapeAt = [&](double ph) {
            ph -= std::floor(ph);
            switch (l.shape) {
                case 0: return std::sin(ph * 2.0 * kPi);                      // sine
                case 1: return ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph;    // triangle
                case 2: return 2.0 * ph - 1.0;                                // saw up
                case 3: return 1.0 - 2.0 * ph;                                // saw down
                case 4: return ph < 0.5 ? 1.0 : -1.0;                         // square
                case 5: return std::sin(ph * 37.0) * std::cos(ph * 11.0);     // S&H preview
                default: return std::sin(ph * 2.0 * kPi) * (1.0 - ph);        // decaying
            }
        };
        auto path = owned(ctx->createGraphicsPath());
        if (path) {
            const int n = static_cast<int>(lane.getWidth());
            const double amp = 0.2 + 0.8 * std::clamp(static_cast<double>(l.depth), 0.0, 1.0);
            for (int x = 0; x <= n; ++x) {
                const double y = shapeAt(static_cast<double>(x) / n) * amp;
                const CPoint p(lane.left + x, lane.getCenter().y - y * lane.getHeight() * 0.4);
                if (x == 0) path->beginSubpath(p);
                else path->addLine(p);
            }
            ctx->setFrameColor(colors[i]);
            ctx->setLineWidth(1.6);
            ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
        const CCoord px = lane.left + lane.getWidth() * std::clamp(static_cast<double>(l.phase), 0.0, 1.0);
        ctx->setFrameColor(withAlpha(theme_.text, 160));
        ctx->setLineWidth(1.0);
        ctx->drawLine(CPoint(px, lane.top), CPoint(px, lane.bottom));
        char buf[32];
        std::snprintf(buf, sizeof buf, "LFO %d  %+.2f", i + 1, static_cast<double>(l.value));
        ctx->setFont(uiFont(9.5));
        ctx->setFontColor(theme_.label);
        ctx->drawString(buf, CRect(lane.left + 4, lane.top + 2, lane.right, lane.top + 14), kLeftText);
    }
    setDirty(false);
}

// ── ByteKnob ────────────────────────────────────────────────────────────────

ByteKnob::ByteKnob(const CRect& r, std::string label, const Theme& theme, int minV, int maxV,
                   std::function<int()> get, std::function<void(int)> set, std::function<std::string(int)> text)
    : CView(r), label_(std::move(label)), theme_(theme), min_(minV), max_(maxV), get_(std::move(get)),
      set_(std::move(set)), text_(std::move(text)) {}

void ByteKnob::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    const int v = get_ ? get_() : min_;
    const float norm = max_ > min_ ? static_cast<float>(v - min_) / static_cast<float>(max_ - min_) : 0.f;
    const CCoord labelH = 12.0, valueH = 12.0;
    const CCoord dia = std::min(r.getWidth() - 6.0, r.getHeight() - labelH - valueH);
    CRect knob(0, 0, dia, dia);
    knob.offset(r.left + (r.getWidth() - dia) / 2.0, r.top + labelH);
    ctx->setDrawMode(kAntiAliasing | kNonIntegralMode);
    ctx->setFontColor(theme_.label);
    drawFitted(ctx, label_, CRect(r.left + 1, r.top, r.right - 1, r.top + labelH), kCenterText, 9.5);
    CRect arc = knob;
    arc.inset(2.0, 2.0);
    ctx->setLineWidth(2.5);
    ctx->setFrameColor(mix(theme_.inactive, theme_.panel, 0.3f));
    strokeArc(ctx, arc, kArcStart, kArcStart + kArcSweep);
    if (norm > 0.001f) {
        ctx->setFrameColor(dragging_ ? theme_.title : theme_.accent);
        strokeArc(ctx, arc, kArcStart, kArcStart + kArcSweep * norm);
    }
    const std::string t = text_ ? text_(v) : std::to_string(v);
    ctx->setFontColor(theme_.value);
    drawFitted(ctx, t, CRect(r.left, r.bottom - valueH, r.right, r.bottom), kCenterText, 10.0);
    setDirty(false);
}

void ByteKnob::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    anchor_ = e.mousePosition;
    anchorValue_ = get_ ? get_() : min_;
    dragging_ = true;
    e.consumed = true;
}

void ByteKnob::onMouseMoveEvent(MouseMoveEvent& e) {
    if (!dragging_) return;
    // 180 px of vertical drag spans the range; shift drags 6x finer.
    const double range = static_cast<double>(max_ - min_);
    const double dy = anchor_.y - e.mousePosition.y;
    const double span = e.modifiers.has(ModifierKey::Shift) ? 1080.0 : 180.0;
    const int v = std::clamp(anchorValue_ + static_cast<int>(std::lround(dy / span * range)), min_, max_);
    if (set_ && (!get_ || get_() != v)) {
        set_(v);
        invalid();
    }
    e.consumed = true;
}

void ByteKnob::onMouseUpEvent(MouseUpEvent& e) {
    dragging_ = false;
    invalid();
    e.consumed = true;
}

void ByteKnob::onMouseCancelEvent(MouseCancelEvent& e) {
    dragging_ = false;
    invalid();
    e.consumed = true;
}

void ByteKnob::onMouseWheelEvent(MouseWheelEvent& e) {
    const int v = std::clamp((get_ ? get_() : min_) + (e.deltaY > 0 ? 1 : -1), min_, max_);
    if (set_) set_(v);
    invalid();
    e.consumed = true;
}

// ── ChoiceMenu ──────────────────────────────────────────────────────────────

ChoiceMenu::ChoiceMenu(const CRect& r, std::string label, const Theme& theme, std::vector<std::string> items,
                       std::function<int()> get, std::function<void(int)> set)
    : CViewContainer(r), label_(std::move(label)), theme_(theme), get_(std::move(get)), set_(std::move(set)) {
    setTransparency(true);
    menu_ = new COptionMenu(CRect(2, 15, r.getWidth() - 2, r.getHeight() - 3), this, 0);
    styleMenu(menu_, theme);
    for (const auto& it : items) menu_->addEntry(it.c_str());
    addView(menu_);
    refresh();
}

void ChoiceMenu::setViewSize(const CRect& r, bool inv) {
    CViewContainer::setViewSize(r, inv);
    if (menu_) {
        const CRect m(2, 15, r.getWidth() - 2, r.getHeight() - 3);
        menu_->setViewSize(m, inv);
        menu_->setMouseableArea(m);
    }
}

void ChoiceMenu::refresh() {
    if (!get_) return;
    const int v = get_();
    if (menu_->getCurrentIndex() != v) menu_->setCurrent(v);
}

void ChoiceMenu::valueChanged(CControl* c) {
    if (c == menu_ && set_) set_(menu_->getCurrentIndex());
}

void ChoiceMenu::drawBackgroundRect(CDrawContext* ctx, const CRect&) {
    ctx->setFontColor(theme_.label);
    drawFitted(ctx, label_, CRect(0, 0, getWidth(), 14), kCenterText, 10.0);
}

} // namespace ArpSID::Editor
