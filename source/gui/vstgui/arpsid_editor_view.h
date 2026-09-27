// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — cross-platform editor root view (VSTGUI).
//
// Header (patch browser, output meters, status), the 17 canonical tabs, an
// on-screen keyboard, and a 30 Hz refresh that mirrors host parameter
// changes and live kernel telemetry into the controls and displays.
#pragma once

#include "gui/vstgui/arpsid_editor_backend.h"
#include "gui/vstgui/arpsid_editor_layout.h"
#include "gui/vstgui/arpsid_editor_widgets.h"

#include "common/arpsid_telemetry_snapshot.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace ArpSID::Editor {

// Services shared by all pages and displays.
struct EditorContext {
    EditorContext(EditorBackend& b, Theme& t, VSTGUI::IControlListener* l) : backend(b), theme(t), paramListener(l) {}
    EditorBackend& backend;
    Theme& theme;
    VSTGUI::IControlListener* paramListener;
    const ArpSIDTelemetry* telemetry = nullptr; // null when no kernel host
    GUI::Language language = GUI::Language::English;
    // Registers a parameter control so host changes are mirrored into it.
    std::function<void(int paramId, VSTGUI::CControl*)> registerControl;
    std::function<void(int paramId, ParamMenu*)> registerMenu;
    std::function<void()> themeChanged;
};

// A live display or model editor placed in a section.
struct DisplayInstance {
    DisplayInstance() = default;
    explicit DisplayInstance(VSTGUI::CView* v) : view(v) {}
    VSTGUI::CView* view = nullptr;
    std::function<void()> refresh; // called at the refresh rate while visible
    bool wantsScopes = false;
    bool wantsC64 = false;
};

// Implemented in arpsid_editor_pages.cpp.
DisplayInstance createDisplay(GUI::EditorLayout::Display d, const VSTGUI::CRect& r, EditorContext& ctx);

// Parameter control for id inside rect (knob / toggle / menu by cardinality).
VSTGUI::CView* createParamControl(int paramId, const VSTGUI::CRect& r, EditorContext& ctx);

class EditorView final : public VSTGUI::CViewContainer,
                         public VSTGUI::IControlListener,
                         public VSTGUI::IKeyboardHook {
public:
    static constexpr VSTGUI::CCoord kWidth = 1200.0;
    static constexpr VSTGUI::CCoord kHeight = 800.0;

    explicit EditorView(EditorBackend& backend);
    ~EditorView() override;

    // Called by the host view at ~30 Hz on the UI thread.
    void refresh();
    void selectTab(int visibleIndex);
    int selectedTab() const { return currentTab_; }
    int tabCount() const;

    // IControlListener
    void valueChanged(VSTGUI::CControl* control) override;
    void controlBeginEdit(VSTGUI::CControl* control) override;
    void controlEndEdit(VSTGUI::CControl* control) override;

    // Test support: number of parameter controls built so far.
    std::size_t parameterControlCount() const { return controls_.size() + menus_.size(); }
    // Builds every tab page (normally built on first visit).
    void buildAllPages();

    void drawBackgroundRect(VSTGUI::CDrawContext* ctx, const VSTGUI::CRect& r) override;

    // Right-click on a parameter control opens the host's parameter menu.
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    // Computer keyboard plays notes (registered by the plug view as the
    // frame's keyboard hook): A W S E D F T G Y H U J K O L = C..D one octave
    // up, Z / X shift the octave. Keys with Ctrl/Alt/Cmd go to the host.
    void onKeyboardEvent(VSTGUI::KeyboardEvent& e, VSTGUI::CFrame* frame) override;
    // Parameter id under the editor point, or -1.
    int paramIdAt(const VSTGUI::CPoint& where) const;
    int keyboardOctave() const { return keyOctave_; }

private:
    struct Page {
        VSTGUI::CViewContainer* view = nullptr;
        std::vector<DisplayInstance> displays;
    };
    void buildHeader_();
    void buildPage_(int visibleIndex);
    // Places the section's parameter controls in area (flow layout) and
    // returns the height used; with panel == nullptr it only measures.
    VSTGUI::CCoord flowSectionParams_(SectionPanel* panel, const GUI::EditorLayout::Section& sec, VSTGUI::CRect area) const;
    VSTGUI::CCoord naturalSectionHeight_(const GUI::EditorLayout::Section& sec, VSTGUI::CCoord width) const;
    VSTGUI::CCoord sectionWidth_(const GUI::EditorLayout::Row& row, int si, int secCount, float totalW) const;
    void refreshParams_();
    void refreshHeader_();
    void applyTheme_();

    EditorBackend& backend_;
    Theme theme_;
    GUI::Theme themeId_ = GUI::Theme::Dark;
    std::unique_ptr<ArpSIDTelemetry> telemetry_;
    std::unique_ptr<EditorContext> ctx_;
    std::multimap<int, VSTGUI::CControl*> controls_;
    std::multimap<int, ParamMenu*> menus_;
    std::map<VSTGUI::CControl*, bool> editing_;
    std::vector<Page> pages_;
    int currentTab_ = -1;

    TabStrip* tabs_ = nullptr;
    VSTGUI::COptionMenu* patchMenu_ = nullptr;
    Label* status_ = nullptr;
    MeterView* outMeter_ = nullptr;
    KeyboardView* keyboard_ = nullptr;
    VSTGUI::CRect pageArea_;
    int shownPatch_ = -1;
    Label* track_ = nullptr;
    CColor trackColour_{};
    std::string shownTrack_;
    std::uint32_t shownTrackColour_ = 0;
    int keyOctave_ = 4;                       // C4 = MIDI 60 on the A key
    std::map<char32_t, int> keysDown_;        // key -> note it started
};

} // namespace ArpSID::Editor
