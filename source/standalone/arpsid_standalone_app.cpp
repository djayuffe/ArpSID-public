// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app: toolbar, editor backend, session (see arpsid_standalone_app.h).

#include "standalone/arpsid_standalone_app.h"

#include "gui/vstgui/arpsid_editor_view.h"
#include "gui/vstgui/arpsid_editor_widgets.h"

#include "arpsid/core/sid_parameter_presentation.h"
#include "parameter_ids.h"
#include "plugin_ids.h"
#include "arpsid/version.h"
#include "vst3/arpsid_vst3_preset_file.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/iplatformbitmap.h"
#include "vstgui/lib/platform/platformfactory.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace ArpSID::Standalone {

using namespace VSTGUI;

namespace {

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

bool writeFile(const std::filesystem::path& p, const void* data, std::size_t size) {
    if (p.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    // Write a sibling and rename, so a crash never leaves half a file.
    std::filesystem::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!f) return false;
    }
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        std::filesystem::remove(p, ec);
        std::filesystem::rename(tmp, p, ec);
    }
    return !ec;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& p) {
    std::vector<std::uint8_t> out;
    if (p.empty()) return out;
    std::ifstream f(p, std::ios::binary);
    if (!f) return out;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return out;
}

} // namespace

// ── editor backend over the standalone engine ────────────────────────────────

class Backend final : public EditorBackend {
public:
    explicit Backend(App& app) : app_(app) {}

    float param(int id) const override { return app_.engine().host().parameter(id); }
    void beginEdit(int) override {}
    void performEdit(int id, float v) override {
        app_.engine().host().setParameterNonRealtime(id, std::clamp(v, 0.0f, 1.0f));
        app_.markDirty();
    }
    void endEdit(int) override {}
    std::string paramText(int id, float v) const override {
        char buf[64] = {};
        if (!SidParameterPresentation::formatNormalized(id, v, buf, sizeof buf)) return {};
        return buf;
    }
    void selectFactoryPatch(int slot) override {
        app_.engine().selectFactoryPatch(slot);
        app_.markDirty();
    }
    int currentFactorySlot() const override { return app_.engine().currentFactorySlot(); }
    void loadPatch(const SidStateRootV1& root, const std::string& name) override {
        app_.engine().loadPatch(root, name);
        app_.markDirty();
    }
    std::string patchName() const override { return app_.engine().patchName(); }
    bool isUserPatch() const override { return app_.engine().isUserPatch(); }
    bool savePresetFile(const std::string& path, std::string& error) override {
        SidStateRootV1 root{};
        app_.engine().host().currentStateRoot(root);
        if (!Presets::savePatchPresetFile(path, ProcessorUID, root, error)) return false;
        app_.engine().setUserPatchName(Presets::presetNameFromPath(Presets::pathFromUtf8(path)));
        return true;
    }
    bool loadPresetFile(const std::string& path, std::string& error) override {
        SidStateRootV1 root{};
        std::string name;
        if (!Presets::loadPatchPresetFile(path, ProcessorUID, root, name, error)) return false;
        app_.engine().loadPatch(root, name);
        // A factory preset file shows as the factory patch.
        if (name == factoryPatchNameForSlot(std::max(0, app_.engine().currentFactorySlot())))
            app_.engine().selectFactoryPatch(app_.engine().currentFactorySlot());
        app_.markDirty();
        return true;
    }
    void sendMidi(std::uint8_t s, std::uint8_t d1, std::uint8_t d2) override {
        const std::uint8_t b[3] = {s, d1, d2};
        app_.engine().uiMidi(b, 3);
    }
    Vst3KernelHost* kernelHost() override { return &app_.engine().host(); }
    void markStateDirty() override { app_.markDirty(); }
    int savedTab() const override { return app_.settings().tab; }
    void tabChanged(int tab) override { app_.settings().tab = tab; }
    std::string trackName() const override { return {}; }

private:
    App& app_;
};

// ── toolbar ──────────────────────────────────────────────────────────────────

namespace {

// A menu that rebuilds its entries right before it opens (device lists).
class LiveMenu final : public COptionMenu {
public:
    LiveMenu(const CRect& r, IControlListener* l) : COptionMenu(r, l, -1) {}
    std::function<void()> beforeOpen;
    void onMouseDownEvent(MouseDownEvent& e) override {
        if (beforeOpen && e.buttonState.isLeft()) beforeOpen();
        COptionMenu::onMouseDownEvent(e);
    }
};

} // namespace

class Toolbar final : public CViewContainer, public IControlListener {
public:
    Toolbar(const CRect& r, App& app) : CViewContainer(r), app_(app), theme_(Editor::makeTheme(GUI::Theme::Dark)) {
        setTransparency(true);
        const CCoord y0 = 4, y1 = r.getHeight() - 4;
        auto menu = [&](CCoord x0, CCoord x1) {
            auto* m = new LiveMenu(CRect(x0, y0, x1, y1), this);
            Editor::styleMenu(m, theme_);
            m->setFont(Editor::uiFont(11.0));
            addView(m);
            return m;
        };
        output_ = menu(6, 300);
        rate_ = menu(304, 380);
        buffer_ = menu(384, 444);
        input_ = menu(448, 600);
        midiIn_ = menu(604, 800);
        channel_ = menu(804, 868);
        output_->beforeOpen = [this]() { fillOutput_(); };
        input_->beforeOpen = [this]() { fillInput_(); };
        midiIn_->beforeOpen = [this]() { fillMidi_(); };
        for (unsigned sr : kSampleRates) {
            char b[32];
            std::snprintf(b, sizeof b, "%.1f kHz", sr / 1000.0);
            rate_->addEntry(b);
        }
        for (unsigned bf : kBufferSizes) buffer_->addEntry(std::to_string(bf).c_str());
        channel_->addEntry("Omni");
        for (int c = 1; c <= 16; ++c) channel_->addEntry(("Ch " + std::to_string(c)).c_str());

        addView(new Editor::ActionButton(CRect(874, y0, 896, y1), "-", theme_, [this]() { bumpTempo_(-1.0); }));
        tempo_ = new Editor::Label(CRect(898, y0, 958, y1), "", theme_, 11.0, true, kCenterText);
        addView(tempo_);
        addView(new Editor::ActionButton(CRect(960, y0, 982, y1), "+", theme_, [this]() { bumpTempo_(+1.0); }));
        play_ = new Editor::ActionButton(CRect(986, y0, 1040, y1), "PLAY", theme_, [this]() {
            app_.engine().setPlaying(!app_.engine().playing());
        });
        addView(play_);
        addView(new Editor::ActionButton(CRect(1044, y0, 1100, y1), "PANIC", theme_, [this]() { app_.engine().panic(); }));
        status1_ = new Editor::Label(CRect(1104, 1, r.getWidth() - 4, 17), "", theme_, 9.5);
        status2_ = new Editor::Label(CRect(1104, 16, r.getWidth() - 4, 32), "", theme_, 9.5);
        addView(status1_);
        addView(status2_);
        fillOutput_();
        fillInput_();
        fillMidi_();
        refresh();
    }

    void drawBackgroundRect(CDrawContext* ctx, const CRect& r) override {
        ctx->setFillColor(theme_.panel);
        ctx->drawRect(r, kDrawFilled);
        ctx->setFrameColor(theme_.panelEdge);
        ctx->setLineWidth(1.0);
        const CRect s = getViewSize();
        ctx->drawLine(CPoint(0, s.getHeight() - 0.5), CPoint(s.getWidth(), s.getHeight() - 0.5));
    }

    // 30 Hz from the app timer.
    void refresh() {
        Engine& e = app_.engine();
        const Settings& s = app_.settings();
        // Follow the editor's theme (SETTINGS tab).
        const auto gen = e.host().modelGeneration();
        if (gen != themeGen_) {
            themeGen_ = gen;
            const GUI::Theme t = e.host().settings().theme;
            if (t != themeId_) {
                themeId_ = t;
                theme_ = Editor::makeTheme(t);
                for (COptionMenu* m : {static_cast<COptionMenu*>(output_), static_cast<COptionMenu*>(rate_),
                                       static_cast<COptionMenu*>(buffer_), static_cast<COptionMenu*>(input_),
                                       static_cast<COptionMenu*>(midiIn_), static_cast<COptionMenu*>(channel_)})
                    Editor::styleMenu(m, theme_);
                invalid();
            }
        }
        AudioIo* a = app_.audio();
        const unsigned rate = (a && a->running()) ? a->sampleRate() : s.sampleRate;
        const unsigned frames = (a && a->running()) ? a->bufferFrames() : s.bufferFrames;
        setMenuIndex_(rate_, indexOf_(kSampleRates, rate));
        setMenuIndex_(buffer_, indexOf_(kBufferSizes, frames));
        setMenuIndex_(channel_, s.midiChannel);
        char b[96];
        std::snprintf(b, sizeof b, "%.0f BPM", e.tempo());
        tempo_->setText(b);
        play_->setLit(e.playing());
        play_->setText(e.playing() ? "STOP" : "PLAY");
        if (a && a->running()) {
            std::snprintf(b, sizeof b, "%.1f ms  load %d%%", a->latencyMs(),
                          static_cast<int>(std::lround(e.load() * 100.0f)));
            status1_->setText(b);
        } else {
            status1_->setText(app_.audioError().empty() ? "audio off" : "audio: " + app_.audioError());
        }
        const bool midiRecent = e.lastMidiMs() != 0 && nowMs() - e.lastMidiMs() < 250;
        std::snprintf(b, sizeof b, "xruns %u  MIDI %s", e.xruns(), midiRecent ? "*" : "-");
        status2_->setText(b);
        if (outputShown_ != (a ? a->outputName() + a->api() : std::string())) fillOutput_();
    }

    void valueChanged(CControl* c) override {
        Settings& s = app_.settings();
        if (c == output_) {
            const int i = static_cast<int>(output_->getCurrentIndex(true));
            if (i >= 0 && i < static_cast<int>(outputChoices_.size())) {
                s.audioApi = outputChoices_[static_cast<std::size_t>(i)].first;
                s.audioOutput = outputChoices_[static_cast<std::size_t>(i)].second;
                app_.reopenAudio();
            }
        } else if (c == input_) {
            const int i = static_cast<int>(input_->getCurrentIndex(true));
            if (i >= 0 && i < static_cast<int>(inputChoices_.size())) {
                s.audioInput = inputChoices_[static_cast<std::size_t>(i)];
                app_.reopenAudio();
            }
        } else if (c == rate_) {
            const int i = static_cast<int>(rate_->getCurrentIndex());
            if (i >= 0 && i < static_cast<int>(std::size(kSampleRates))) {
                s.sampleRate = kSampleRates[i];
                app_.reopenAudio();
            }
        } else if (c == buffer_) {
            const int i = static_cast<int>(buffer_->getCurrentIndex());
            if (i >= 0 && i < static_cast<int>(std::size(kBufferSizes))) {
                s.bufferFrames = kBufferSizes[i];
                app_.reopenAudio();
            }
        } else if (c == midiIn_) {
            const int i = static_cast<int>(midiIn_->getCurrentIndex(true));
            if (i >= 0 && i < static_cast<int>(midiChoices_.size())) {
                s.midiInput = midiChoices_[static_cast<std::size_t>(i)];
                app_.reopenMidi();
            }
        } else if (c == channel_) {
            s.midiChannel = std::clamp(static_cast<int>(channel_->getCurrentIndex()), 0, 16);
            app_.engine().setMidiChannel(s.midiChannel);
        }
        refresh();
    }

    void fillMidi_() {
        midiIn_->removeAllEntry();
        midiChoices_.clear();
        const Settings& s = app_.settings();
        MidiIo* m = app_.midi();
        auto add = [&](const std::string& title, const std::string& value) {
            midiIn_->addEntry(title.c_str());
            midiChoices_.push_back(value);
        };
        std::string all = "MIDI: all inputs";
        if (m && s.midiInput == "*") all += " (" + std::to_string(m->openPorts().size()) + ")";
        add(all, "*");
        add("MIDI: none", "");
        const std::vector<std::string> ports = m ? m->ports() : std::vector<std::string>{};
        if (!ports.empty()) {
            midiIn_->addSeparator();
            midiChoices_.push_back("\x01"); // separator placeholder (never chosen)
        }
        for (const auto& p : ports) add("MIDI: " + p, p);
        if (s.midiInput != "*" && !s.midiInput.empty() &&
            std::find(ports.begin(), ports.end(), s.midiInput) == ports.end())
            add("MIDI: " + s.midiInput + " (missing)", s.midiInput);
        for (std::size_t i = 0; i < midiChoices_.size(); ++i)
            if (midiChoices_[i] == s.midiInput) midiIn_->setCurrent(static_cast<int32_t>(i));
    }

private:
    template <std::size_t N>
    static int indexOf_(const unsigned (&list)[N], unsigned v) {
        for (std::size_t i = 0; i < N; ++i)
            if (list[i] == v) return static_cast<int>(i);
        return -1;
    }
    static void setMenuIndex_(COptionMenu* m, int i) {
        if (i >= 0 && static_cast<int>(m->getCurrentIndex()) != i) {
            m->setCurrent(i);
            m->invalid();
        }
    }

    void bumpTempo_(double d) {
        Engine& e = app_.engine();
        e.setTempo(std::round(e.tempo()) + d);
        app_.settings().bpm = e.tempo();
    }

    void fillOutput_() {
        output_->removeAllEntry();
        outputChoices_.clear();
        AudioIo* a = app_.audio();
        const Settings& s = app_.settings();
        if (!a) { // started with --no-audio
            output_->addEntry("Audio off (--no-audio)");
            outputChoices_.push_back({"\x01", {}});
            output_->setCurrent(0);
            outputShown_.clear();
            return;
        }
        const auto devices = listAudioDevices();
        const auto apis = compiledAudioApis();
        int current = -1;
        for (const std::string& api : apis) {
            const std::string disp = audioApiDisplayName(api);
            if (!outputChoices_.empty()) {
                output_->addSeparator();
                outputChoices_.push_back({"\x01", {}});
            }
            output_->addEntry((disp + ": default output").c_str());
            outputChoices_.push_back({api, {}});
            if (a && a->running() && a->api() == api && s.audioOutput.empty()) current = static_cast<int>(outputChoices_.size()) - 1;
            for (const AudioDevice& d : devices) {
                if (d.api != api || d.outputs == 0) continue;
                output_->addEntry((disp + ": " + d.name).c_str());
                outputChoices_.push_back({api, d.name});
                if (a && a->running() && a->api() == api && !s.audioOutput.empty() && d.name == a->outputName())
                    current = static_cast<int>(outputChoices_.size()) - 1;
            }
        }
        if (!a || !a->running()) {
            output_->addSeparator();
            outputChoices_.push_back({"\x01", {}});
            output_->addEntry("Audio off - choose an output");
            outputChoices_.push_back({"\x01", {}});
            current = static_cast<int>(outputChoices_.size()) - 1;
        }
        if (current >= 0) output_->setCurrent(current);
        outputShown_ = a ? a->outputName() + a->api() : std::string();
    }

    void fillInput_() {
        input_->removeAllEntry();
        inputChoices_.clear();
        AudioIo* a = app_.audio();
        input_->addEntry("Capture in: none");
        inputChoices_.push_back({});
        if (!a) {
            input_->setCurrent(0);
            return;
        }
        const std::string api = a ? a->api() : app_.settings().audioApi;
        int current = 0;
        for (const AudioDevice& d : listAudioDevices(api)) {
            if (d.inputs == 0) continue;
            input_->addEntry(("Capture in: " + d.name).c_str());
            inputChoices_.push_back(d.name);
            if (a && d.name == a->inputName()) current = static_cast<int>(inputChoices_.size()) - 1;
        }
        input_->setCurrent(current);
    }

    App& app_;
    Editor::Theme theme_;
    GUI::Theme themeId_ = GUI::Theme::Dark;
    std::uint64_t themeGen_ = ~0ull;
    LiveMenu* output_ = nullptr;
    LiveMenu* rate_ = nullptr;
    LiveMenu* buffer_ = nullptr;
    LiveMenu* input_ = nullptr;
    LiveMenu* midiIn_ = nullptr;
    LiveMenu* channel_ = nullptr;
    Editor::Label* tempo_ = nullptr;
    Editor::ActionButton* play_ = nullptr;
    Editor::Label* status1_ = nullptr;
    Editor::Label* status2_ = nullptr;
    std::vector<std::pair<std::string, std::string>> outputChoices_; // (api, device; "" = default)
    std::vector<std::string> inputChoices_;
    std::vector<std::string> midiChoices_;
    std::string outputShown_;
};

// ── app ──────────────────────────────────────────────────────────────────────

double App::contentWidth() { return Editor::EditorView::kWidth; }
double App::contentHeight() { return Editor::EditorView::kHeight + kToolbarHeight; }

App::App(Options options) : options_(std::move(options)), engine_(std::make_unique<Engine>()) {
    backend_ = std::make_unique<Backend>(*this);
}

App::~App() {
    detach();
    shutdown();
}

bool App::start(std::string& error) {
    (void)error;
    shutDown_ = false;
    startMs_ = nowMs();
    {
        const std::vector<std::uint8_t> text = readFile(settingsFile());
        settings_ = parse(std::string(text.begin(), text.end()));
    }
    engine_->setTempo(settings_.bpm);
    engine_->setMidiChannel(settings_.midiChannel);
    engine_->prepare(settings_.sampleRate, static_cast<int>(std::max(settings_.bufferFrames, 4096u)));
    {
        const std::vector<std::uint8_t> session = readFile(sessionFile());
        if (session.empty() || !engine_->loadSession(session.data(), session.size())) engine_->selectFactoryPatch(0);
    }
    if (options_.audio) {
        audio_ = std::make_unique<AudioIo>(*engine_);
        if (!audio_->open(settings_, audioError_) && audioError_.empty()) audioError_ = "no audio output";
    }
    if (options_.midi) {
        midi_ = std::make_unique<MidiIo>(*engine_);
        midi_->open(settings_.midiInput);
    }
    return true;
}

void App::shutdown() {
    if (shutDown_) return;
    shutDown_ = true;
    if (audio_) audio_->close();
    if (midi_) midi_->close();
    if (engine_) {
        saveSettings_();
        saveSession_();
    }
    audio_.reset();
    midi_.reset();
}

void App::reopenAudio() {
    if (!audio_) return;
    audioError_.clear();
    if (!audio_->open(settings_, audioError_) && audioError_.empty()) audioError_ = "cannot open audio";
    if (audio_->running()) {
        // Keep what actually opened.
        settings_.audioApi = audio_->api();
        if (!settings_.audioOutput.empty()) settings_.audioOutput = audio_->outputName();
        settings_.audioInput = audio_->inputName();
        settings_.sampleRate = audio_->sampleRate();
    }
    saveSettings_();
}

void App::reopenMidi() {
    if (!midi_) return;
    midi_->open(settings_.midiInput);
    saveSettings_();
}

void App::attach(CFrame* frame) {
    frame_ = frame;
    toolbar_ = makeOwned<Toolbar>(CRect(0, 0, contentWidth(), kToolbarHeight), *this);
    frame->addView(toolbar_.get());
    toolbar_->remember();
    editor_ = makeOwned<Editor::EditorView>(*backend_);
    editor_->setViewSize(CRect(0, kToolbarHeight, Editor::EditorView::kWidth, contentHeight()));
    editor_->setMouseableArea(editor_->getViewSize());
    frame->addView(editor_.get());
    editor_->remember();
    frame->registerKeyboardHook(editor_.get());
    timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { tick_(); }, 33);
}

void App::detach() {
    if (timer_) {
        timer_->stop();
        timer_ = nullptr;
    }
    if (frame_ && editor_) frame_->unregisterKeyboardHook(editor_.get());
    editor_ = nullptr;
    toolbar_ = nullptr;
    frame_ = nullptr;
}

void App::setZoom(double z) { settings_.zoom = std::clamp(z, 0.5, 3.0); }

std::string App::windowTitle() const {
    return std::string("ArpSID ") + ARPSID_PLUGIN_VERSION + " - " + engine_->patchName();
}

void App::tick_() {
    ++ticks_;
    // Without a running audio stream the engine still has to run (patch and
    // state changes apply at render time, the displays read its telemetry):
    // render the elapsed time into a scratch buffer, silently.
    if (!audio_ || !audio_->running()) {
        const std::uint64_t now = nowMs();
        if (idleRenderMs_ == 0) idleRenderMs_ = now;
        const double sr = engine_->sampleRate();
        int frames = static_cast<int>(std::min<std::uint64_t>(now - idleRenderMs_, 100) * sr / 1000.0);
        idleRenderMs_ = now;
        idleBuffer_.resize(2 * 4096);
        float* outs[2] = {idleBuffer_.data(), idleBuffer_.data() + 4096};
        while (frames > 0) {
            const int n = std::min(frames, 4096);
            engine_->process(outs, 2, nullptr, 0, n);
            frames -= n;
        }
    } else {
        idleRenderMs_ = 0;
    }
    if (engine_->applyPendingProgramChange()) dirty_ = true;
    engine_->host().pollNonRealtime();
    if (editor_) editor_->refresh();
    if (toolbar_) toolbar_->refresh();
    // MIDI hot-plug (every 2 s).
    if (midi_ && ticks_ % 60 == 0) midi_->rescan();
    // Session autosave (every 30 s when something changed).
    if (dirty_ && ticks_ % 900 == 0) {
        dirty_ = false;
        saveSession_();
    }
    if (options_.quitAfterMs >= 0 && !quitSent_ &&
        nowMs() - startMs_ >= static_cast<std::uint64_t>(options_.quitAfterMs)) {
        quitSent_ = true;
        if (!options_.screenshotPath.empty()) {
            if (saveScreenshot(options_.screenshotPath)) std::printf("screenshot: %s\n", options_.screenshotPath.c_str());
            else std::fprintf(stderr, "screenshot failed: %s\n", options_.screenshotPath.c_str());
        }
        if (requestQuit) requestQuit();
    }
}

void App::saveSession_() {
    const std::vector<std::uint8_t> s = engine_->saveSession();
    if (!writeFile(sessionFile(), s.data(), s.size()))
        std::fprintf(stderr, "ArpSID: cannot save the session to %s\n", Presets::pathToUtf8(sessionFile()).c_str());
}

void App::saveSettings_() {
    settings_.bpm = engine_->tempo();
    const std::string text = serialize(settings_);
    (void)writeFile(settingsFile(), text.data(), text.size());
}

bool App::saveScreenshot(const std::string& path) {
    if (!frame_) return false;
    const CPoint size(contentWidth(), contentHeight());
    auto ctx = COffscreenContext::create(size, 1.0);
    if (!ctx) return false;
    ctx->beginDraw();
    // Containers draw their children at their own position in the frame.
    if (toolbar_) toolbar_->drawRect(ctx, toolbar_->getViewSize());
    if (editor_) editor_->drawRect(ctx, editor_->getViewSize());
    ctx->endDraw();
    auto platformBitmap = ctx->getBitmap() ? ctx->getBitmap()->getPlatformBitmap() : nullptr;
    if (!platformBitmap) return false;
    const auto png = getPlatformFactory().createBitmapMemoryPNGRepresentation(platformBitmap);
    return !png.empty() && writeFile(Presets::pathFromUtf8(path), png.data(), png.size());
}

} // namespace ArpSID::Standalone
