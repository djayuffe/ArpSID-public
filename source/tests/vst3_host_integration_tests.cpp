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
//   * The MIDI input bus has 16 channels.
//   * IProcessContextRequirements requests tempo/transport/musical time.
//   * On-screen keyboard notes (UiMidi message) produce audio.
//
// usage: arpsid_vst3_host_tests <path/to/arpsid_vst3.vst3>

#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "parameter_ids.h"
#include "arpsid_vst_messages.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

double renderRms(IAudioProcessor* proc, int blocks, int frames) {
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
        proc->process(data);
        for (int i = 0; i < frames; ++i) {
            sum += (double)l[(size_t)i] * l[(size_t)i] + (double)r[(size_t)i] * r[(size_t)i];
            n += 2;
        }
        ctx.projectTimeSamples += frames;
    }
    return n ? std::sqrt(sum / (double)n) : 0.0;
}

void sendMessage(IConnectionPoint* to, const char* id, const std::vector<std::pair<const char*, int64>>& ints) {
    IPtr<IMessage> msg = owned(new HostMessage);
    msg->setMessageID(id);
    for (const auto& kv : ints) msg->getAttributes()->setInt(kv.first, kv.second);
    to->notify(msg);
}

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
        FUnknownPtr<IProcessContextRequirements> req(component);
        CHECK(req, "IProcessContextRequirements available");
        if (req) {
            const uint32 f = req->getProcessContextRequirements();
            CHECK(f & IProcessContextRequirements::kNeedTempo, "requests tempo");
            CHECK(f & IProcessContextRequirements::kNeedTransportState, "requests transport state");
            CHECK(f & IProcessContextRequirements::kNeedProjectTimeMusic, "requests musical position");
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
        proc->setProcessing(false);
        component->setActive(false);
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
