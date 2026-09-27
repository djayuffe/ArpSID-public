// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VSTGUI widgets for the cross-platform editor.
#pragma once

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"

#include "arpsid/gui/settings_panel_model.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ArpSID::Editor {

using VSTGUI::CColor;
using VSTGUI::CCoord;
using VSTGUI::CDrawContext;
using VSTGUI::CPoint;
using VSTGUI::CRect;

// ── theme ───────────────────────────────────────────────────────────────────
// Colours for one GUI::Theme (Dark, Light, C64 Classic, High Contrast), built
// from the shared palette (theme_palette_v552.h) so the editor matches the
// macOS GUI. Widgets keep a reference, so a theme change is a reassignment
// of the EditorView's Theme plus a redraw (menus are restyled explicitly).
struct Theme {
    CColor bg, panel, panelEdge, title, label, value, border, accent, inactive, ledOn, ledOff, text, warn;
};
Theme makeTheme(GUI::Theme t);

// Fonts shared by all widgets (created on first use).
VSTGUI::CFontRef uiFont(CCoord size, bool bold = false);
VSTGUI::CFontRef monoFont(CCoord size);

// ── parameter controls ──────────────────────────────────────────────────────
// All parameter controls use the parameter id as tag. Text comes from a
// formatter so they display the canonical host strings.
using ValueFormatter = std::function<std::string(int tag, float normalized)>;

// Rotary control for a continuous parameter (or one with more than 32 steps).
// Drag vertically (horizontal counts a quarter): 180 px spans 0..1, Shift is
// fine (1200 px). Wheel: 2 % per notch (Shift 0.2 %). Double-click or
// Ctrl-click resets to the default. Every gesture is wrapped in
// beginEdit/endEdit so hosts record one automation gesture. The arc shows the
// value (270 degrees, from the lower left); caption and value text are fitted
// inside the control.
class ParamKnob : public VSTGUI::CControl {
public:
    ParamKnob(const CRect& r, VSTGUI::IControlListener* l, int tag, std::string label, const Theme& theme,
              ValueFormatter fmt, float defaultValue);
    void draw(CDrawContext* ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& e) override;
    CLASS_METHODS(ParamKnob, CControl)
private:
    std::string label_;
    const Theme& theme_;
    ValueFormatter fmt_;
    float default_;
    CPoint anchor_;
    float anchorValue_ = 0.f;
    bool dragging_ = false;
};

// On/off parameter (step count 1): LED plus caption; a click flips it inside
// one begin/perform/end edit gesture.
class ParamToggle : public VSTGUI::CControl {
public:
    ParamToggle(const CRect& r, VSTGUI::IControlListener* l, int tag, std::string label, const Theme& theme);
    void draw(CDrawContext* ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    CLASS_METHODS(ParamToggle, CControl)
private:
    std::string label_;
    const Theme& theme_;
};

// Stepped parameter shown as a labelled pop-up menu of its value strings.
class ParamMenu : public VSTGUI::CViewContainer {
public:
    ParamMenu(const CRect& r, VSTGUI::IControlListener* l, int tag, std::string label, int steps,
              const Theme& theme, const ValueFormatter& fmt);
    // Selects the entry the engine plays for v (stepIndexForParam).
    void setNormalized(float v);
    VSTGUI::COptionMenu* menu() const { return menu_; }
    void drawBackgroundRect(CDrawContext* ctx, const CRect& r) override;
    // Keeps the inner menu under the caption when the container is resized.
    void setViewSize(const CRect& r, bool invalid = true) override;
    CLASS_METHODS_NOCOPY(ParamMenu, CViewContainer)
private:
    std::string label_;
    const Theme& theme_;
    VSTGUI::COptionMenu* menu_ = nullptr;
    int steps_;
};

// ── containers / decoration ─────────────────────────────────────────────────
// Titled, rounded group box. Children are placed in contentRect() (local
// coordinates below the 22 px title band).
class SectionPanel : public VSTGUI::CViewContainer {
public:
    SectionPanel(const CRect& r, std::string title, const Theme& theme);
    void drawBackgroundRect(CDrawContext* ctx, const CRect& r) override;
    CRect contentRect() const; // local coordinates
    CLASS_METHODS_NOCOPY(SectionPanel, CViewContainer)
private:
    std::string title_;
    const Theme& theme_;
};

// Static or live text. setColor points at a Theme colour so the label follows
// theme changes without being rebuilt.
class Label : public VSTGUI::CView {
public:
    Label(const CRect& r, std::string text, const Theme& theme, CCoord size = 11.0, bool bold = false,
          VSTGUI::CHoriTxtAlign align = VSTGUI::kLeftText);
    void setText(std::string t);
    void setColor(const CColor* c) { color_ = c; }
    void draw(CDrawContext* ctx) override;
private:
    std::string text_;
    const Theme& theme_;
    CCoord size_;
    bool bold_;
    VSTGUI::CHoriTxtAlign align_;
    const CColor* color_ = nullptr;
};

// Momentary / latching button with a callback.
class ActionButton : public VSTGUI::CView {
public:
    ActionButton(const CRect& r, std::string text, const Theme& theme, std::function<void()> onClick);
    void setText(std::string t) {
        if (t != text_) {
            text_ = std::move(t);
            invalid();
        }
    }
    void setLit(bool lit) { if (lit_ != lit) { lit_ = lit; invalid(); } }
    void draw(CDrawContext* ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
private:
    std::string text_;
    const Theme& theme_;
    std::function<void()> onClick_;
    bool pressed_ = false;
    bool lit_ = false;
};

// Horizontal tab strip; calls onSelect(index).
class TabStrip : public VSTGUI::CView {
public:
    TabStrip(const CRect& r, std::vector<std::string> names, const Theme& theme, std::function<void(int)> onSelect);
    void setSelected(int i) { if (sel_ != i) { sel_ = i; invalid(); } }
    int selected() const { return sel_; }
    void draw(CDrawContext* ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
private:
    CRect tabRect_(int i) const;
    std::vector<std::string> names_;
    const Theme& theme_;
    std::function<void(int)> onSelect_;
    int sel_ = 0;
};

// ── displays ────────────────────────────────────────────────────────────────
// Oscilloscope for one or more -1..+1 traces (oldest sample left). Traces
// either share the whole view around the centre line or, with setStacked,
// get one labelled lane each.
class ScopeView : public VSTGUI::CView {
public:
    ScopeView(const CRect& r, const Theme& theme, int traces);
    void setTrace(int i, const float* data, int n, CColor color);
    void setCaption(std::string c) { caption_ = std::move(c); }
    // Stacked lanes: trace i gets its own horizontal band with a name at the
    // left edge (used for the VCO scopes and the SID bus timeline). Unstacked
    // traces share the full height around the centre line.
    void setStacked(std::vector<std::string> laneNames) { laneNames_ = std::move(laneNames); }
    void draw(CDrawContext* ctx) override;
private:
    const Theme& theme_;
    std::vector<std::vector<float>> traces_;
    std::vector<CColor> colors_;
    std::string caption_;
    std::vector<std::string> laneNames_;
};

// Bar meters (horizontal with labels on the left, or vertical with labels
// below). Unipolar bars keep a decaying peak mark and turn to the warning
// colour above 0.95.
class MeterView : public VSTGUI::CView {
public:
    MeterView(const CRect& r, const Theme& theme, int bars, bool vertical = false);
    // Unipolar bars take 0..1; a bipolar bar takes -1..+1 and fills from the
    // centre (LFO values, pitch bend).
    void setLevel(int i, float level, std::string label = {});
    void setBipolar(int i, bool bipolar);
    void draw(CDrawContext* ctx) override;
private:
    const Theme& theme_;
    std::vector<float> levels_;
    std::vector<float> peaks_;
    std::vector<std::string> labels_;
    std::vector<bool> bipolar_;
    bool vertical_;
};

// Monospaced multi-line text readout.
class TextGrid : public VSTGUI::CView {
public:
    TextGrid(const CRect& r, const Theme& theme, CCoord fontSize = 11.0);
    void setLines(std::vector<std::string> lines);
    void draw(CDrawContext* ctx) override;
private:
    const Theme& theme_;
    CCoord fontSize_;
    std::vector<std::string> lines_;
};

// Piano keyboard; noteOn(note, velocity) / noteOff(note).
class KeyboardView : public VSTGUI::CView {
public:
    KeyboardView(const CRect& r, const Theme& theme, int firstNote, int octaves,
                 std::function<void(int, int)> noteOn, std::function<void(int)> noteOff);
    // Notes the engine is sounding (drawn lit in addition to the key held
    // with the mouse). Bit n of the mask is MIDI note n.
    void setActiveNotes(const std::array<std::uint64_t, 2>& mask) {
        if (active_ != mask) {
            active_ = mask;
            invalid();
        }
    }
    void draw(CDrawContext* ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;
private:
    int noteAt_(const CPoint& local, int* velocity) const;
    const Theme& theme_;
    int first_, octaves_;
    std::function<void(int, int)> noteOn_;
    std::function<void(int)> noteOff_;
    bool lit_(int note) const {
        return note == held_ || (note >= 0 && note < 128 && ((active_[static_cast<std::size_t>(note >> 6)] >> (note & 63)) & 1u));
    }
    int held_ = -1;
    std::array<std::uint64_t, 2> active_{};
};

// Generic grid of cells (step sequencers, pads, register maps). The owner
// supplies cell contents and receives clicks.
class CellGrid : public VSTGUI::CView {
public:
    struct Cell {
        float level = 0.f;       // 0..1 fill
        bool on = false;
        bool accent = false;
        bool cursor = false;
        bool dim = false;        // outside the active range (drawn faded)
        std::string text;
    };
    // onClick(col, row, shift, right, yFrac): yFrac is the click height inside
    // the cell (0 = bottom, 1 = top), for level-style cells.
    using ClickFn = std::function<void(int col, int row, bool shift, bool right, float yFrac)>;
    CellGrid(const CRect& r, const Theme& theme, int cols, int rows, ClickFn onClick);
    void setRowLabels(std::vector<std::string> l) { rowLabels_ = std::move(l); invalid(); }
    void setCell(int col, int row, Cell c);
    // Drag across cells repeats the click (paint); with level mode the click
    // also repeats while moving inside one cell (drawing a level).
    void setDragPaint(bool on, bool level = false) { dragPaint_ = on; dragLevel_ = level; }
    void draw(CDrawContext* ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
private:
    bool cellAt_(const CPoint& local, int& c, int& r) const;
    CRect cellRect_(int c, int r) const;
    const Theme& theme_;
    int cols_, rows_;
    std::vector<Cell> cells_;
    std::vector<std::string> rowLabels_;
    ClickFn onClick_;
    bool dragPaint_ = false;
    bool dragLevel_ = false;
    bool dragging_ = false;
    int lastC_ = -1, lastR_ = -1;
};

// Filter response curve from normalized cutoff / resonance / mode index
// (SID LP/BP/HP bits). An approximate display: cutoff maps 30 Hz..12 kHz
// exponentially; the audio path is the engine's SID filter model.
class FilterCurveView : public VSTGUI::CView {
public:
    FilterCurveView(const CRect& r, const Theme& theme);
    void setFilter(float cutoff, float resonance, int modeMask);
    void draw(CDrawContext* ctx) override;
private:
    const Theme& theme_;
    float cutoff_ = 0.5f, res_ = 0.f;
    int mode_ = 1;
};

// LFO shapes with a live phase marker.
class LfoWaveView : public VSTGUI::CView {
public:
    LfoWaveView(const CRect& r, const Theme& theme);
    void setLfo(int i, int shape, float depth, float phase, float value);
    void draw(CDrawContext* ctx) override;
private:
    const Theme& theme_;
    struct L { int shape = 0; float depth = 0.f, phase = 0.f, value = 0.f; };
    std::array<L, 4> lfo_{};
};

// Knob over an integer model field (MIX/KIT/DIGI: bytes, semitones, nibbles,
// 12-bit pulse width) read and written through get/set callbacks. Drag: 180 px
// spans the range, Shift is 6x finer; wheel steps by 1. The owning panel
// publishes the model and marks the project dirty in set.
class ByteKnob : public VSTGUI::CView {
public:
    ByteKnob(const CRect& r, std::string label, const Theme& theme, int minV, int maxV,
             std::function<int()> get, std::function<void(int)> set, std::function<std::string(int)> text = {});
    void draw(CDrawContext* ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& e) override;
private:
    std::string label_;
    const Theme& theme_;
    int min_, max_;
    std::function<int()> get_;
    std::function<void(int)> set_;
    std::function<std::string(int)> text_;
    CPoint anchor_;
    int anchorValue_ = 0;
    bool dragging_ = false;
};

// Captioned pop-up choice over a model field (get returns the index, set
// receives the chosen one). Used by the model panels and SETTINGS.
class ChoiceMenu : public VSTGUI::CViewContainer, public VSTGUI::IControlListener {
public:
    ChoiceMenu(const CRect& r, std::string label, const Theme& theme, std::vector<std::string> items,
               std::function<int()> get, std::function<void(int)> set);
    void refresh();
    void valueChanged(VSTGUI::CControl* c) override;
    void drawBackgroundRect(CDrawContext* ctx, const CRect& r) override;
    // Keeps the inner menu under the caption when the container is resized
    // (panels may create the control first and place it afterwards).
    void setViewSize(const CRect& r, bool invalid = true) override;
    VSTGUI::COptionMenu* menu() const { return menu_; }
    CLASS_METHODS_NOCOPY(ChoiceMenu, CViewContainer)
private:
    std::string label_;
    const Theme& theme_;
    VSTGUI::COptionMenu* menu_ = nullptr;
    std::function<int()> get_;
    std::function<void(int)> set_;
};

// Styles an option menu for the theme.
void styleMenu(VSTGUI::COptionMenu* m, const Theme& theme);

} // namespace ArpSID::Editor
