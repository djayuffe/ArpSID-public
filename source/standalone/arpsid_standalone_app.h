// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app (Linux, Windows): everything above the native window.
//
// The window holds one VSTGUI frame: a toolbar (audio output / sample rate /
// buffer / capture input, MIDI input and channel, tempo, play, panic, load
// meter) above the same editor the VST3 plug-in shows (EditorView), driven
// by a backend over the standalone engine instead of a VST3 controller.
// Patches, presets (.vstpreset, shared with the plug-in's preset folders),
// patch files and banks work as in the plug-in. Settings and the session
// are saved on exit and restored on launch.
#pragma once

#include "standalone/arpsid_standalone_devices.h"
#include "standalone/arpsid_standalone_engine.h"
#include "standalone/arpsid_standalone_settings.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ArpSID::Editor {
class EditorView;
}

namespace ArpSID::Standalone {

class Toolbar;
class Backend;

struct Options {
    bool audio = true;            // --no-audio: no audio device (CI, tests)
    bool midi = true;             // --no-midi
    int quitAfterMs = -1;         // --quit-after: close the window after a while
    std::string screenshotPath;   // --screenshot: save the window as PNG before quitting
};

class App {
public:
    static constexpr double kToolbarHeight = 34.0;
    static double contentWidth();  // 1200
    static double contentHeight(); // toolbar + editor

    explicit App(Options options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Load settings and session, open audio and MIDI. Device problems are not
    // fatal (the toolbar shows them); false only when the app cannot run.
    bool start(std::string& error);
    // Stop audio and MIDI, save settings and session.
    void shutdown();

    // Build the toolbar and editor inside <frame> (sized contentWidth x
    // contentHeight at zoom 1) and start the UI timer.
    void attach(VSTGUI::CFrame* frame);
    void detach();

    // Window size the user chose (scale of the 1200 x 834 content, without
    // the screen's DPI scale); the platform layer zooms the frame.
    double zoom() const { return settings_.zoom; }
    void setZoom(double z);

    // Title for the native window ("ArpSID - <patch>").
    std::string windowTitle() const;
    // Set when the app wants the window closed (--quit-after).
    std::function<void()> requestQuit;
    // Set by the platform layer: resize the native window to the zoom.
    std::function<void(double zoom)> requestWindowZoom;

    Engine& engine() { return *engine_; }
    Settings& settings() { return settings_; }
    AudioIo* audio() { return audio_.get(); }
    MidiIo* midi() { return midi_.get(); }
    const std::string& audioError() const { return audioError_; }

    // Toolbar actions.
    void reopenAudio();
    void reopenMidi();
    void markDirty() { dirty_ = true; }

    // Save the window content as a PNG (offscreen render at zoom 1).
    bool saveScreenshot(const std::string& path);

private:
    void tick_();
    void saveSession_();
    void saveSettings_();

    Options options_;
    Settings settings_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<AudioIo> audio_;
    std::unique_ptr<MidiIo> midi_;
    std::unique_ptr<Backend> backend_;
    std::string audioError_;
    VSTGUI::CFrame* frame_ = nullptr;
    VSTGUI::SharedPointer<Editor::EditorView> editor_;
    VSTGUI::SharedPointer<Toolbar> toolbar_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    bool dirty_ = false;
    std::uint64_t ticks_ = 0;
    std::uint64_t startMs_ = 0;
    bool quitSent_ = false;
    std::uint64_t idleRenderMs_ = 0;       // last silent render (no audio stream)
    std::vector<float> idleBuffer_;
    bool shutDown_ = true; // nothing to save until start()
};

} // namespace ArpSID::Standalone
