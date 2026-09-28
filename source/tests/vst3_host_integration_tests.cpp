// Copyright (C) 2024-2026 Ulf Bertilsson
// VST3 host integration test: loads the built ArpSID .vst3 module through the
// Steinberg hosting library, connects processor and controller the way a DAW
// does, and checks the host-facing contracts that the SDK validator does not:
//
//   * Program parameter is a program-change list over the 180 factory slots.
//   * Selecting a program in the controller loads that factory patch into the
//     processor (IMessage, off the audio thread) and mirrors its parameters.
//   * setComponentState() syncs a fresh controller from processor state.
//   * IMidiMapping covers the shared realtime CC law on every MIDI channel.
//   * The MIDI input bus has 16 channels; the DIGI capture input is an
//     auxiliary audio bus, inactive by default.
//   * IProcessContextRequirements requests tempo/transport/musical time.
//   * On-screen keyboard notes (UiMidi message) produce audio.
//   * Host bypass (kIsBypass) fades the output and is saved in the state;
//     64-bit processing; controller (editor) state; IInfoListener; program
//     attributes.
//   * The processor runs the shared kernel: state carries the GUI models
//     (SETTINGS/MIX/KIT/DIGI), legacy Phase2 (v4) project state still loads,
//     host note events and sample-accurate automation reach the engine.
//
// usage: arpsid_vst3_host_tests <path/to/arpsid_vst3.vst3>

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "pluginterfaces/vst/vstpresetkeys.h"
#include "pluginterfaces/gui/iplugview.h"

#include "parameter_ids.h"
#include "arpsid_vst_messages.h"
#include "au3/ArpSIDStateSerializer.h"
#include "factory_patch_params.h"
#include "vst3/arpsid_vst3_kernel_host.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <chrono>
#include <poll.h>
#include <xcb/xcb.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

static int failures = 0;
#define CHECK(cond, msg)                                                         \
    do {                                                                         \
        if (!(cond)) { std::fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++failures; } \
    } while (0)

namespace {

// Minimal IComponentHandler: records edits and restart requests.
class RecordingHandler final : public IComponentHandler {
public:
    int restartFlags = 0;
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override { restartFlags |= flags; return kResultOk; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IComponentHandler)
        QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
};

double renderRms(IAudioProcessor* proc, int blocks, int frames,
                 IEventList* firstEvents = nullptr, IParameterChanges* firstParams = nullptr) {
    std::vector<float> l((size_t)frames), r((size_t)frames);
    float* chans[2] = {l.data(), r.data()};
    AudioBusBuffers out{};
    out.numChannels = 2;
    out.channelBuffers32 = chans;
    ProcessContext ctx{};
    ctx.sampleRate = 44100.0;
    ctx.tempo = 120.0;
    ctx.state = ProcessContext::kTempoValid;
    double sum = 0.0;
    size_t n = 0;
    for (int b = 0; b < blocks; ++b) {
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = frames;
        data.numOutputs = 1;
        data.outputs = &out;
        data.processContext = &ctx;
        if (b == 0) {
            data.inputEvents = firstEvents;
            data.inputParameterChanges = firstParams;
        }
        proc->process(data);
        for (int i = 0; i < frames; ++i) {
            sum += (double)l[(size_t)i] * l[(size_t)i] + (double)r[(size_t)i] * r[(size_t)i];
            n += 2;
        }
        ctx.projectTimeSamples += frames;
    }
    return n ? std::sqrt(sum / (double)n) : 0.0;
}

// Same as renderRms with 64-bit buffers; also reports non-finite samples.
double renderRms64(IAudioProcessor* proc, int blocks, int frames, IEventList* firstEvents, bool& finite) {
    std::vector<double> l((size_t)frames), r((size_t)frames);
    double* chans[2] = {l.data(), r.data()};
    AudioBusBuffers out{};
    out.numChannels = 2;
    out.channelBuffers64 = chans;
    double sum = 0.0;
    size_t n = 0;
    finite = true;
    for (int b = 0; b < blocks; ++b) {
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample64;
        data.numSamples = frames;
        data.numOutputs = 1;
        data.outputs = &out;
        if (b == 0) data.inputEvents = firstEvents;
        proc->process(data);
        for (int i = 0; i < frames; ++i) {
            finite &= std::isfinite(l[(size_t)i]) && std::isfinite(r[(size_t)i]);
            sum += l[(size_t)i] * l[(size_t)i] + r[(size_t)i] * r[(size_t)i];
            n += 2;
        }
    }
    return n ? std::sqrt(sum / (double)n) : 0.0;
}

ParameterChanges* oneChange(ParameterChanges& pc, ParamID id, double value) {
    int32 qi = 0, pi = 0;
    if (IParamValueQueue* q = pc.addParameterData(id, qi)) q->addPoint(0, value, pi);
    return &pc;
}

void sendMessage(IConnectionPoint* to, const char* id, const std::vector<std::pair<const char*, int64>>& ints) {
    IPtr<IMessage> msg = owned(new HostMessage);
    msg->setMessageID(id);
    for (const auto& kv : ints) msg->getAttributes()->setInt(kv.first, kv.second);
    to->notify(msg);
}

#if defined(__linux__)
// A Linux host's editor window: an X11 window plus the IPlugFrame a Linux
// VST3 host gives the plug-in, which also serves Linux::IRunLoop (fd watches
// and timers pumped by the host), as the VST3 Linux hosting contract asks.
class X11PlugFrame final : public IPlugFrame, public Linux::IRunLoop {
public:
    struct Fd { IPtr<Linux::IEventHandler> h; int fd; };
    struct Timer { IPtr<Linux::ITimerHandler> h; int ms; std::chrono::steady_clock::time_point due; };
    std::vector<Fd> fds;
    std::vector<Timer> timers;
    int resizes = 0;
    int timerFires = 0;

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* r) override {
        ++resizes;
        return view && r ? view->onSize(r) : kInvalidArgument;
    }
    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* h, Linux::FileDescriptor fd) override {
        if (!h) return kInvalidArgument;
        fds.push_back({h, fd});
        return kResultTrue;
    }
    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* h) override {
        for (auto it = fds.begin(); it != fds.end(); ++it)
            if (it->h == h) { fds.erase(it); return kResultTrue; }
        return kResultFalse;
    }
    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* h, Linux::TimerInterval ms) override {
        if (!h) return kInvalidArgument;
        timers.push_back({h, (int)std::max<Linux::TimerInterval>(ms, 1),
                          std::chrono::steady_clock::now() + std::chrono::milliseconds(ms)});
        return kResultTrue;
    }
    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* h) override {
        for (auto it = timers.begin(); it != timers.end(); ++it)
            if (it->h == h) { timers.erase(it); return kResultTrue; }
        return kResultFalse;
    }

    // Runs the loop for about `ms` milliseconds.
    void pump(int ms) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (std::chrono::steady_clock::now() < end) {
            std::vector<pollfd> p;
            for (const auto& f : fds) p.push_back({f.fd, POLLIN, 0});
            ::poll(p.data(), (nfds_t)p.size(), 5);
            // Copies: handlers may (un)register while being called.
            const auto fdsNow = fds;
            for (size_t i = 0; i < p.size() && i < fdsNow.size(); ++i)
                if (p[i].revents & (POLLIN | POLLHUP)) fdsNow[i].h->onFDIsSet(fdsNow[i].fd);
            const auto now = std::chrono::steady_clock::now();
            const auto timersNow = timers;
            for (const auto& t : timersNow) {
                if (t.due > now) continue;
                for (auto& live : timers)
                    if (live.h == t.h) live.due = now + std::chrono::milliseconds(live.ms);
                ++timerFires;
                t.h->onTimer();
            }
        }
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(iid, obj, IPlugFrame::iid, IPlugFrame)
        QUERY_INTERFACE(iid, obj, Linux::IRunLoop::iid, Linux::IRunLoop)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
};

// A frame without Linux::IRunLoop (a host that only offers it through the
// factory host context, or not at all).
class BarePlugFrame final : public IPlugFrame {
public:
    tresult PLUGIN_API resizeView(IPlugView*, ViewRect*) override { return kResultFalse; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(iid, obj, IPlugFrame::iid, IPlugFrame)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
};

// Opens the editor in a real X11 window the way a Linux DAW does and runs its
// event loop; checks it paints, resizes, closes and opens again.
void runX11EditorChecks(IEditController* controller) {
    const char* display = std::getenv("DISPLAY");
    const char* required = std::getenv("ARPSID_REQUIRE_X11_EDITOR");
    const bool mustRun = required && *required && *required != '0';
    if (!display || !*display) {
        std::printf("  no DISPLAY: X11 editor checks skipped (run under xvfb-run)\n");
        CHECK(!mustRun, "X11: ARPSID_REQUIRE_X11_EDITOR is set but there is no DISPLAY");
        return;
    }
    int screenNo = 0;
    xcb_connection_t* xc = xcb_connect(nullptr, &screenNo);
    if (!xc || xcb_connection_has_error(xc)) {
        std::printf("  cannot connect to X server %s: X11 editor checks skipped\n", display);
        CHECK(!mustRun, "X11: ARPSID_REQUIRE_X11_EDITOR is set but the X server is unreachable");
        if (xc) xcb_disconnect(xc);
        return;
    }
    const xcb_setup_t* setup = xcb_get_setup(xc);
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for (int i = 0; i < screenNo; ++i) xcb_screen_next(&it);
    xcb_screen_t* screen = it.data;

    auto openWindow = [&]() {
        const xcb_window_t w = xcb_generate_id(xc);
        xcb_create_window(xc, XCB_COPY_FROM_PARENT, w, screen->root, 0, 0, 1200, 800, 0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);
        xcb_map_window(xc, w);
        xcb_flush(xc);
        return w;
    };

    for (int round = 0; round < 2; ++round) {
        IPlugView* view = controller->createView(ViewType::kEditor);
        CHECK(view, "X11: editor view created");
        if (!view) break;
        CHECK(view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) == kResultTrue, "X11: X11 embedding supported");
        CHECK(view->isPlatformTypeSupported(kPlatformTypeWaylandSurfaceID) != kResultTrue,
              "X11: Wayland is not claimed (VSTGUI is built without it)");
        X11PlugFrame frame;
        view->setFrame(&frame);
        const xcb_window_t parent = openWindow();
        const tresult attached =
            view->attached(reinterpret_cast<void*>(static_cast<uintptr_t>(parent)), kPlatformTypeX11EmbedWindowID);
        CHECK(attached == kResultOk, "X11: editor attaches to the host window");
        frame.pump(400);
        CHECK(!frame.fds.empty(), "X11: editor registered its X connection with the host run loop");
        CHECK(frame.timerFires > 0, "X11: editor timers run on the host run loop");
        ViewRect bigger(0, 0, 1500, 1000);
        CHECK(view->checkSizeConstraint(&bigger) == kResultOk && view->onSize(&bigger) == kResultOk,
              "X11: live resize");
        frame.pump(150);
        CHECK(view->removed() == kResultOk, "X11: editor detaches");
        view->setFrame(nullptr);
        view->release();
        CHECK(frame.fds.empty() && frame.timers.empty(), "X11: editor left no handlers on the host run loop");
        xcb_destroy_window(xc, parent);
        xcb_flush(xc);
        std::printf("  X11 editor round %d: timers fired %d, resize requests %d\n", round + 1, frame.timerFires,
                    frame.resizes);
    }

    // A host frame without a run loop: the editor must refuse, not crash.
    {
        IPlugView* view = controller->createView(ViewType::kEditor);
        if (view) {
            BarePlugFrame bare;
            view->setFrame(&bare);
            const xcb_window_t parent = openWindow();
            const tresult r =
                view->attached(reinterpret_cast<void*>(static_cast<uintptr_t>(parent)), kPlatformTypeX11EmbedWindowID);
            std::printf("  X11 editor without host run loop: attached -> %d\n", (int)r);
            if (r == kResultOk) view->removed();
            view->setFrame(nullptr);
            view->release();
            xcb_destroy_window(xc, parent);
            xcb_flush(xc);
        }
    }
    xcb_disconnect(xc);
}
#endif

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <arpsid_vst3.vst3>\n", argv[0]);
        return 2;
    }
    std::string err;
    auto module = VST3::Hosting::Module::create(argv[1], err);
    if (!module) {
        std::fprintf(stderr, "cannot load module: %s\n", err.c_str());
        return 2;
    }
    IPtr<HostApplication> host = owned(new HostApplication);
    const auto& factory = module->getFactory();

    IPtr<IComponent> component;
    for (const auto& ci : factory.classInfos()) {
        if (ci.category() == kVstAudioEffectClass) {
            component = factory.createInstance<IComponent>(ci.ID());
            break;
        }
    }
    CHECK(component, "audio effect class instantiates");
    if (!component) return 1;
    CHECK(component->initialize(host) == kResultOk, "processor initialize");

    TUID ctrlCid{};
    CHECK(component->getControllerClassId(ctrlCid) == kResultOk, "controller class id");
    auto makeController = [&]() {
        IPtr<IEditController> c = factory.createInstance<IEditController>(VST3::UID::fromTUID(ctrlCid));
        if (c) c->initialize(host);
        return c;
    };
    IPtr<IEditController> controller = makeController();
    CHECK(controller, "controller instantiates");
    if (!controller) return 1;
    RecordingHandler handler;
    controller->setComponentHandler(&handler);

    FUnknownPtr<IConnectionPoint> procCp(component);
    FUnknownPtr<IConnectionPoint> ctrlCp(controller);
    CHECK(procCp && ctrlCp, "both sides are IConnectionPoints");
    procCp->connect(ctrlCp);
    ctrlCp->connect(procCp);

    // ── Program parameter is a program-change list ─────────────────────────
    {
        bool found = false;
        for (int32 i = 0; i < controller->getParameterCount(); ++i) {
            ParameterInfo info{};
            controller->getParameterInfo(i, info);
            if (info.id != (ParamID)ArpSID::kParamProgram) continue;
            found = true;
            CHECK(info.flags & ParameterInfo::kIsProgramChange, "Program has kIsProgramChange");
            CHECK(info.flags & ParameterInfo::kIsList, "Program has kIsList");
            CHECK(info.stepCount == ArpSID::kCanonicalFactoryPatchSlotMax, "Program has 180 steps");
        }
        CHECK(found, "Program parameter registered");
    }

    // ── MIDI input bus and context requirements ────────────────────────────
    {
        BusInfo bus{};
        CHECK(component->getBusInfo(kEvent, kInput, 0, bus) == kResultOk, "event bus info");
        CHECK(bus.channelCount == 16, "MIDI input bus has 16 channels");
        CHECK(component->getBusCount(kAudio, kInput) == 1, "one audio input bus (DIGI capture)");
        BusInfo in{};
        CHECK(component->getBusInfo(kAudio, kInput, 0, in) == kResultOk && in.busType == kAux &&
                  (in.flags & BusInfo::kDefaultActive) == 0,
              "DIGI capture input is an auxiliary bus, inactive by default");
        FUnknownPtr<IProcessContextRequirements> req(component);
        CHECK(req, "IProcessContextRequirements available");
        if (req) {
            const uint32 f = req->getProcessContextRequirements();
            CHECK(f & IProcessContextRequirements::kNeedTempo, "requests tempo");
            CHECK(f & IProcessContextRequirements::kNeedTransportState, "requests transport state");
            CHECK(f & IProcessContextRequirements::kNeedProjectTimeMusic, "requests musical position");
        }
    }

#if !defined(__APPLE__)
    // ── Cross-platform editor sizing (no window needed) ────────────────────
    {
        IPlugView* view = controller->createView(ViewType::kEditor);
        if (!view) std::printf("  no editor in this build (ARPSID_VST3_EDITOR=OFF): sizing checks skipped\n");
        if (view) {
            CHECK(view->canResize() == kResultTrue, "editor can resize");
            ViewRect r(0, 0, 1000, 1000);
            CHECK(view->checkSizeConstraint(&r) == kResultOk && r.getWidth() == 1000 && r.getHeight() == 667,
                  "size constraint keeps the 3:2 editor inside the offered rect");
            ViewRect tiny(0, 0, 100, 100);
            view->checkSizeConstraint(&tiny);
            CHECK(tiny.getWidth() == 600 && tiny.getHeight() == 400, "editor is at least half size");
            ViewRect big(0, 0, 1800, 1200);
            CHECK(view->onSize(&big) == kResultOk, "onSize without an open window");
            view->release();
            IPlugView* again = controller->createView(ViewType::kEditor);
            ViewRect cur{};
            CHECK(again && again->getSize(&cur) == kResultOk && cur.getWidth() == 1800 && cur.getHeight() == 1200,
                  "a reopened editor keeps the chosen size");
            if (again) again->release();
#if defined(__linux__)
            runX11EditorChecks(controller);
#endif
        }
    }
#endif

    // ── IUnitInfo: parameter groups ────────────────────────────────────────
    {
        FUnknownPtr<IUnitInfo> ui(controller);
        CHECK(ui, "IUnitInfo available");
        if (ui) {
            // root + 17 tabs + host MIDI/read-only + 13 host controller kinds
            CHECK(ui->getUnitCount() == 32, "32 units (root, 17 tabs, host MIDI group and its 13 sub-units)");
            UnitInfo u{};
            CHECK(ui->getUnitInfo(1, u) == kResultOk && u.id == 1 && u.parentUnitId == kRootUnitId,
                  "tab units hang off the root unit");
            CHECK(ui->selectUnit(3) == kResultOk && ui->getSelectedUnit() == 3, "selectUnit is remembered");
            ParameterInfo pi{};
            CHECK(controller->getParameterInfo(ArpSID::kParamMasterVolume, pi) == kResultOk && pi.unitId == 1,
                  "Master Volume is in the MAIN unit");
            CHECK(controller->getParameterInfo(ArpSID::kParamProgram, pi) == kResultOk && pi.unitId == kRootUnitId,
                  "Program stays in the root unit with the program list");
            CHECK(controller->getParameterInfo(ArpSID::kParamHostCtrlPitchBendBase, pi) == kResultOk && pi.unitId > 19,
                  "host pitch-bend mirrors are in a MIDI sub-unit");
            int32 count = controller->getParameterCount();
            bool allKnown = true;
            for (int32 i = 0; i < count; ++i) {
                if (controller->getParameterInfo(i, pi) != kResultOk) continue;
                bool found = false;
                for (int32 k = 0; k < ui->getUnitCount() && !found; ++k)
                    if (ui->getUnitInfo(k, u) == kResultOk && u.id == pi.unitId) found = true;
                allKnown = allKnown && found;
            }
            CHECK(allKnown, "every parameter names an existing unit");
        }
    }

    // ── IMidiMapping ───────────────────────────────────────────────────────
    {
        FUnknownPtr<IMidiMapping> mm(controller);
        CHECK(mm, "IMidiMapping available");
        if (mm) {
            ParamID id = 0;
            CHECK(mm->getMidiControllerAssignment(0, 9, 74, id) == kResultOk && id == (ParamID)ArpSID::kParamFilterLFOAmount,
                  "CC74 on channel 10 -> Filter LFO Amount (shared CC law)");
            CHECK(mm->getMidiControllerAssignment(0, 3, 70, id) == kResultOk && id == (ParamID)ArpSID::kParamFilterCutoff,
                  "CC70 -> Filter Cutoff");
            CHECK(mm->getMidiControllerAssignment(0, 0, kCtrlVolume, id) == kResultOk && id == (ParamID)ArpSID::kParamMasterVolume,
                  "CC7 -> Master Volume");
            CHECK(mm->getMidiControllerAssignment(0, 5, kCtrlFoot, id) == kResultOk &&
                  id == (ParamID)((int)ArpSID::kParamHostCtrlExpressionBase + 5), "CC4 -> channel expression");
            CHECK(mm->getMidiControllerAssignment(0, 15, kCtrlModWheel, id) == kResultOk &&
                  id == (ParamID)((int)ArpSID::kParamHostCtrlModWheelBase + 15), "CC1 on channel 16 -> mod wheel");
            CHECK(mm->getMidiControllerAssignment(0, 0, 3, id) != kResultOk, "unmapped CC3 stays unmapped");
        }
    }

    // ── Factory preset load through the controller ─────────────────────────
    const int slot = 37;
    const ParamValue slotNorm = ArpSID::canonicalNormalizedFactoryProgramValue(slot);
    handler.restartFlags = 0;
    CHECK(controller->setParamNormalized((ParamID)ArpSID::kParamProgram, slotNorm) == kResultOk, "select program");
    CHECK(handler.restartFlags & kParamValuesChanged, "controller asked host to refresh parameter values");
    CHECK(std::fabs(controller->getParamNormalized((ParamID)ArpSID::kParamBankSlot) - slotNorm) < 1e-6,
          "controller BankSlot mirrors selected program");

    MemoryStream procState;
    CHECK(component->getState(&procState) == kResultOk, "processor getState");

    // A fresh controller restored from the processor state must agree with the
    // controller that performed the load -> the processor really loaded it.
    IPtr<IEditController> restored = makeController();
    procState.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(restored && restored->setComponentState(&procState) == kResultOk, "setComponentState");
    if (restored) {
        CHECK(std::fabs(restored->getParamNormalized((ParamID)ArpSID::kParamBankSlot) - slotNorm) < 1e-6,
              "processor state carries the loaded factory slot");
        int mismatches = 0;
        for (int pid = 0; pid < ArpSID::kNumParams; ++pid) {
            if (ArpSID::isRuntimeOnlyOrTransientParam(pid)) continue;
            const double a = controller->getParamNormalized((ParamID)pid);
            const double b = restored->getParamNormalized((ParamID)pid);
            if (std::fabs(a - b) > 1e-5) {
                if (mismatches < 5) std::fprintf(stderr, "  param %d: loaded %.6f restored %.6f\n", pid, a, b);
                ++mismatches;
            }
        }
        CHECK(mismatches == 0, "controller mirror matches processor state after preset load");
        restored->terminate();
    }

    // ── On-screen keyboard notes reach the audio thread ────────────────────
    {
        FUnknownPtr<IAudioProcessor> proc(component);
        CHECK(proc, "IAudioProcessor");
        // Slot 0 is the default synth patch.
        controller->setParamNormalized((ParamID)ArpSID::kParamProgram, ArpSID::canonicalNormalizedFactoryProgramValue(0));
        ProcessSetup setup{kRealtime, kSample32, 512, 44100.0};
        proc->setupProcessing(setup);
        component->setActive(true);
        proc->setProcessing(true);
        const double silent = renderRms(proc, 8, 512);
        sendMessage(procCp, ArpSID::kVstMsgUiMidi,
                    {{ArpSID::kVstMsgAttrStatus, 0x90}, {ArpSID::kVstMsgAttrData1, 60}, {ArpSID::kVstMsgAttrData2, 110}});
        const double playing = renderRms(proc, 16, 512);
        sendMessage(procCp, ArpSID::kVstMsgUiMidi,
                    {{ArpSID::kVstMsgAttrStatus, 0x80}, {ArpSID::kVstMsgAttrData1, 60}, {ArpSID::kVstMsgAttrData2, 0}});
        (void)renderRms(proc, 4, 512);
        std::printf("keyboard note: rms before %.6f, while held %.6f\n", silent, playing);
        CHECK(playing > 1e-4 && playing > silent * 4.0, "UiMidi note-on produces audio");

        // Host note events (IEventList) and sample-accurate automation.
        EventList notes;
        Event on{};
        on.type = Event::kNoteOnEvent;
        on.sampleOffset = 100;
        on.noteOn.channel = 0;
        on.noteOn.pitch = 64;
        on.noteOn.velocity = 0.9f;
        on.noteOn.noteId = 7;
        notes.addEvent(on);
        const double hostNote = renderRms(proc, 16, 512, &notes);
        std::printf("host note: rms %.6f\n", hostNote);
        CHECK(hostNote > 1e-4, "host note-on event produces audio");

        ParameterChanges mute;
        int32 queueIndex = 0;
        if (IParamValueQueue* q = mute.addParameterData((ParamID)ArpSID::kParamMasterVolume, queueIndex)) {
            int32 pointIndex = 0;
            q->addPoint(0, 0.0, pointIndex);
        }
        (void)renderRms(proc, 4, 512, nullptr, &mute);
        const double muted = renderRms(proc, 8, 512);
        std::printf("master volume 0: rms %.6f\n", muted);
        CHECK(muted < hostNote * 0.05, "automation of Master Volume reaches the engine");

        // ── Host bypass: fades out while the engine keeps running ─────────
        const ParamID bypassId = (ParamID)ArpSID::kVst3BypassParamId;
        {
            ParameterInfo bi{};
            bool found = false;
            for (int32 i = 0; i < controller->getParameterCount(); ++i)
                if (controller->getParameterInfo(i, bi) == kResultOk && bi.id == bypassId) { found = true; break; }
            CHECK(found && (bi.flags & ParameterInfo::kIsBypass) && bi.stepCount == 1 && bi.unitId == kRootUnitId,
                  "Bypass parameter: kIsBypass, on/off, root unit");
            String128 txt{};
            CHECK(controller->getParamStringByValue(bypassId, 1.0, txt) == kResultOk && txt[0] != 0,
                  "Bypass has host text");
        }
        ParameterChanges vol;
        (void)renderRms(proc, 2, 512, nullptr, oneChange(vol, (ParamID)ArpSID::kParamMasterVolume, 0.8));
        const double live = renderRms(proc, 8, 512);
        ParameterChanges byOn;
        (void)renderRms(proc, 2, 512, nullptr, oneChange(byOn, bypassId, 1.0));
        const double bypassed = renderRms(proc, 8, 512);
        std::printf("bypass: live %.6f, bypassed %.6f\n", live, bypassed);
        CHECK(live > 1e-4 && bypassed == 0.0, "bypass silences the output");
        {
            MemoryStream bst;
            CHECK(component->getState(&bst) == kResultOk, "getState while bypassed");
            bst.seek(0, IBStream::kIBSeekSet, nullptr);
            IPtr<IEditController> c = makeController();
            CHECK(c && c->setComponentState(&bst) == kResultOk && c->getParamNormalized(bypassId) > 0.5,
                  "bypass is saved in the state and mirrored by the controller");
            if (c) c->terminate();
        }
        // ── A factory .vstpreset (patch-only state) changes only the patch ──
        {
            const int presetSlot = 12;
            ArpSID::SidStateRootV1 root = ArpSID::makeFactoryPatchStateRootForSlot(presetSlot);
            std::vector<uint8_t> blob(ArpSID::encodedSidStateRootBinarySize(root));
            const size_t len = ArpSID::encodeStateRoot(root, ArpSID::kSidBinaryStateMagic, blob.data(), blob.size());
            std::vector<uint8_t> st;
            auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) st.push_back((uint8_t)(v >> (8 * i))); };
            u32(ArpSID::kVst3StateVersion);
            u32(ArpSID::kVst3StateTagPreset);
            u32(0);
            u32(ArpSID::kVst3StateTagRoot);
            u32((uint32_t)len);
            st.insert(st.end(), blob.begin(), blob.begin() + (std::ptrdiff_t)len);
            MemoryStream ps;
            int32 pw = 0;
            ps.write(st.data(), (int32)st.size(), &pw);
            ps.seek(0, IBStream::kIBSeekSet, nullptr);
            CHECK(component->setState(&ps) == kResultOk, "processor loads a patch-only preset state");
            controller->setParamNormalized(bypassId, 1.0);
            ps.seek(0, IBStream::kIBSeekSet, nullptr);
            CHECK(controller->setComponentState(&ps) == kResultOk, "controller follows a preset state");
            CHECK(controller->getParamNormalized(bypassId) > 0.5, "a preset leaves the controller's bypass alone");
            CHECK(std::fabs(controller->getParamNormalized((ParamID)ArpSID::kParamBankSlot) -
                            ArpSID::canonicalNormalizedBankSlotValue(presetSlot)) < 1e-6,
                  "controller shows the preset's patch");
            MemoryStream after;
            CHECK(component->getState(&after) == kResultOk, "getState after preset");
            int64 asz = 0;
            after.seek(0, IBStream::kIBSeekEnd, &asz);
            std::vector<uint8_t> ab((size_t)asz);
            after.seek(0, IBStream::kIBSeekSet, nullptr);
            int32 ar = 0;
            after.read(ab.data(), (int32)ab.size(), &ar);
            // Chunk scan: 'BYPS' holds the bypass byte, 'PRST' marks a preset.
            auto chunk = [&](uint32_t want, const uint8_t*& at) {
                auto g = [&](size_t i) {
                    return (uint32_t)ab[i] | ((uint32_t)ab[i + 1] << 8) | ((uint32_t)ab[i + 2] << 16) |
                           ((uint32_t)ab[i + 3] << 24);
                };
                for (size_t pos = 4; pos + 8 <= ab.size();) {
                    const uint32_t tag = g(pos), clen = g(pos + 4);
                    if (tag == want) { at = ab.data() + pos + 8; return clen; }
                    pos += 8 + clen;
                }
                return 0xFFFFFFFFu;
            };
            const uint8_t* at = nullptr;
            const uint32_t byLen = chunk(0x42595053u /* 'BYPS' */, at);
            CHECK(byLen == 1 && at && *at == 1, "a preset leaves the processor bypassed");
            CHECK(chunk(ArpSID::kVst3StateTagPreset, at) == 0xFFFFFFFFu, "saved project state is a full state");
            after.seek(0, IBStream::kIBSeekSet, nullptr);
            IPtr<IEditController> c = makeController();
            CHECK(c && c->setComponentState(&after) == kResultOk &&
                      std::fabs(c->getParamNormalized((ParamID)ArpSID::kParamBankSlot) -
                                ArpSID::canonicalNormalizedBankSlotValue(presetSlot)) < 1e-6,
                  "the processor now runs the preset's patch");
            if (c) c->terminate();
        }

        ParameterChanges byOff;
        (void)renderRms(proc, 2, 512, nullptr, oneChange(byOff, bypassId, 0.0));
        const double back = renderRms(proc, 8, 512);
        CHECK(back > live * 0.25, "un-bypass restores the output");

        Event off{};
        off.type = Event::kNoteOffEvent;
        off.noteOff.pitch = 64;
        off.noteOff.noteId = 7;
        EventList offs;
        offs.addEvent(off);
        (void)renderRms(proc, 2, 512, &offs);
        proc->setProcessing(false);
        component->setActive(false);

        // ── 64-bit processing ─────────────────────────────────────────────
        CHECK(proc->canProcessSampleSize(kSample64) == kResultTrue, "supports 64-bit samples");
        ProcessSetup setup64{kRealtime, kSample64, 512, 48000.0};
        CHECK(proc->setupProcessing(setup64) == kResultOk, "setupProcessing (64-bit)");
        component->setActive(true);
        proc->setProcessing(true);
        EventList notes64;
        Event on64 = on;
        on64.sampleOffset = 0;
        notes64.addEvent(on64);
        bool finite = false;
        const double rms64 = renderRms64(proc, 16, 512, &notes64, finite);
        std::printf("64-bit note: rms %.6f\n", rms64);
        CHECK(finite && rms64 > 1e-4, "64-bit processing renders the note");
        // A block larger than announced must not overrun the scratch buffers.
        (void)renderRms64(proc, 1, 2048, nullptr, finite);
        CHECK(finite, "oversized 64-bit block stays finite");
        EventList offs64;
        offs64.addEvent(off);
        (void)renderRms64(proc, 2, 512, &offs64, finite);
        proc->setProcessing(false);
        component->setActive(false);
    }

    // ── Controller (editor) state, track info, program attributes ─────────
    {
        MemoryStream cs;
        IBStreamer w(&cs, kLittleEndian);
        w.writeInt32u(0x41534543u);
        w.writeInt32u(1u);
        w.writeDouble(1.5);
        w.writeInt32(3);
        cs.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(controller->setState(&cs) == kResultOk, "controller setState (editor state)");
        MemoryStream out;
        CHECK(controller->getState(&out) == kResultOk, "controller getState");
        out.seek(0, IBStream::kIBSeekSet, nullptr);
        IBStreamer r(&out, kLittleEndian);
        uint32 magic = 0, version = 0;
        double zoom = 0.0;
        int32 tab = -1;
        CHECK(r.readInt32u(magic) && r.readInt32u(version) && r.readDouble(zoom) && r.readInt32(tab) &&
                  magic == 0x41534543u && version == 1u && std::fabs(zoom - 1.5) < 1e-9 && tab == 3,
              "editor size and tab round-trip through the controller state");
        MemoryStream junk;
        int32 jw = 0;
        const char garbage[5] = {'x', 'y', 'z', 'w', 'q'};
        junk.write(const_cast<char*>(garbage), 5, &jw);
        junk.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(controller->setState(&junk) == kResultOk, "foreign controller state is ignored");

        FUnknownPtr<ChannelContext::IInfoListener> info(controller);
        CHECK(info, "IInfoListener available");
        if (info) {
            IPtr<IAttributeList> attrs = HostAttributeList::make();
            const char16 name[] = u"Lead SID";
            attrs->setString(ChannelContext::kChannelNameKey, reinterpret_cast<const TChar*>(name));
            attrs->setInt(ChannelContext::kChannelColorKey, (int64)0xFF3366CC);
            CHECK(info->setChannelContextInfos(attrs) == kResultOk, "setChannelContextInfos");
        }

        FUnknownPtr<IUnitInfo> ui(controller);
        if (ui) {
            String128 v{};
            CHECK(ui->getProgramInfo(1, 0, PresetAttributes::kPlugInCategory, v) == kResultOk && v[0] == 'I',
                  "program category is Instrument|Synth");
        }
    }

    // ── Kernel state format: canonical root + every GUI model ──────────────
    {
        MemoryStream st;
        CHECK(component->getState(&st) == kResultOk, "getState (v5)");
        int64 size = 0;
        st.seek(0, IBStream::kIBSeekEnd, &size);
        std::vector<uint8_t> bytes((size_t)size);
        st.seek(0, IBStream::kIBSeekSet, nullptr);
        int32 got = 0;
        st.read(bytes.data(), (int32)bytes.size(), &got);
        auto u32 = [&](size_t at) {
            return (uint32_t)bytes[at] | ((uint32_t)bytes[at + 1] << 8) | ((uint32_t)bytes[at + 2] << 16) |
                   ((uint32_t)bytes[at + 3] << 24);
        };
        CHECK(bytes.size() > 8 && u32(0) == 5u, "state version 5");
        std::vector<std::string> tags;
        for (size_t pos = 4; pos + 8 <= bytes.size();) {
            const uint32_t tag = u32(pos), len = u32(pos + 4);
            tags.push_back(std::string{(char)(tag >> 24), (char)(tag >> 16), (char)(tag >> 8), (char)tag});
            pos += 8 + len;
        }
        for (const char* want : {"ROOT", "SETS", "MIX ", "KIT ", "DIGM", "DIGB"}) {
            bool have = false;
            for (const auto& t : tags) have |= (t == want);
            CHECK(have, want);
        }
        // A second instance accepts it and reports the same patch.
        IPtr<IComponent> other;
        for (const auto& ci : factory.classInfos())
            if (ci.category() == kVstAudioEffectClass) { other = factory.createInstance<IComponent>(ci.ID()); break; }
        if (other) {
            other->initialize(host);
            st.seek(0, IBStream::kIBSeekSet, nullptr);
            CHECK(other->setState(&st) == kResultOk, "second instance setState (v5)");
            other->terminate();
        }
    }

    // ── Legacy Phase2 project state (v4: version + root blob) ──────────────
    {
        const int legacySlot = 5;
        ArpSID::SidStateRootV1 root = ArpSID::makeFactoryPatchStateRootForSlot(legacySlot);
        std::vector<uint8_t> blob(ArpSID::encodedSidStateRootBinarySize(root));
        const size_t len = ArpSID::encodeStateRoot(root, ArpSID::kSidBinaryStateMagic, blob.data(), blob.size());
        CHECK(len > 0, "encode legacy root");
        MemoryStream legacy;
        const uint32_t v4 = 4u;
        int32 w = 0;
        legacy.write(const_cast<uint32_t*>(&v4), 4, &w);
        legacy.write(blob.data(), (int32)len, &w);
        legacy.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(component->setState(&legacy) == kResultOk, "processor accepts legacy v4 state");
        legacy.seek(0, IBStream::kIBSeekSet, nullptr);
        IPtr<IEditController> c = makeController();
        CHECK(c && c->setComponentState(&legacy) == kResultOk, "controller accepts legacy v4 state");
        if (c) {
            CHECK(std::fabs(c->getParamNormalized((ParamID)ArpSID::kParamBankSlot) -
                            ArpSID::canonicalNormalizedBankSlotValue(legacySlot)) < 1e-6,
                  "legacy state restores its factory slot");
            c->terminate();
        }
        MemoryStream after;
        CHECK(component->getState(&after) == kResultOk, "getState after legacy load");
        IPtr<IEditController> c2 = makeController();
        after.seek(0, IBStream::kIBSeekSet, nullptr);
        if (c2 && c2->setComponentState(&after) == kResultOk) {
            CHECK(std::fabs(c2->getParamNormalized((ParamID)ArpSID::kParamBankSlot) -
                            ArpSID::canonicalNormalizedBankSlotValue(legacySlot)) < 1e-6,
                  "legacy state survives re-save in the v5 format");
            c2->terminate();
        }
    }

    procCp->disconnect(ctrlCp);
    ctrlCp->disconnect(procCp);
    controller->terminate();
    component->terminate();

    if (failures) {
        std::fprintf(stderr, "Vst3HostIntegrationTests: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("Vst3HostIntegrationTests PASS");
    return 0;
}
