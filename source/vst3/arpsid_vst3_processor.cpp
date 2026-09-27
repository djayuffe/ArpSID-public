// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VST3 audio processor on the shared ArpSIDDSPKernel (see header).

#include "vst3/arpsid_vst3_processor.h"

#include "arpsid_vst_messages.h"
#include "au3/ArpSIDStateSerializer.h"
#include "parameter_ids.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_WIN32)
#include <process.h>
#define ARPSID_GETPID _getpid
#else
#include <unistd.h>
#define ARPSID_GETPID getpid
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace ArpSID {

ArpSIDVst3Processor::ArpSIDVst3Processor()
    : host_(std::make_unique<Vst3KernelHost>()), events_(kMaxTimedEvents) {
    setControllerClass(ControllerUID);
}

ArpSIDVst3Processor::~ArpSIDVst3Processor() = default;

tresult PLUGIN_API ArpSIDVst3Processor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;

    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    // All 16 MIDI channels: ArpSID is multi-channel (GM channel-10 drums,
    // per-channel host controllers).
    addEventInput(STR16("MIDI In"), 16);
    // Host timing for the arpeggiator, sequencer and C64 player sync.
    processContextRequirements.needTempo()
                              .needTransportState()
                              .needProjectTimeMusic()
                              .needCycleMusic()
                              .needBarPositionMusic()
                              .needTimeSignature();

    // Deterministic startup patch (factory slot 0), applied at the first block.
    host_->loadFactorySlot(0);
    return kResultOk;
}

tresult PLUGIN_API ArpSIDVst3Processor::terminate() { return AudioEffect::terminate(); }

tresult PLUGIN_API ArpSIDVst3Processor::connect(IConnectionPoint* other) {
    const tresult result = AudioEffect::connect(other);
    if (result == kResultOk) sendKernelHost_();
    return result;
}

void ArpSIDVst3Processor::sendKernelHost_() {
    IMessage* msg = allocateMessage();
    if (!msg) return;
    msg->setMessageID(kVstMsgKernelHost);
    msg->getAttributes()->setInt(kVstMsgAttrHostPtr,
                                 static_cast<int64>(reinterpret_cast<std::uintptr_t>(host_.get())));
    msg->getAttributes()->setInt(kVstMsgAttrPid, static_cast<int64>(ARPSID_GETPID()));
    sendMessage(msg);
    msg->release();
}

tresult PLUGIN_API ArpSIDVst3Processor::notify(IMessage* message) {
    if (!message || !message->getMessageID()) return AudioEffect::notify(message);
    IAttributeList* attrs = message->getAttributes();
    const FIDString id = message->getMessageID();
    if (FIDStringsEqual(id, kVstMsgLoadFactoryPatch)) {
        int64 slot = -1;
        if (!attrs || attrs->getInt(kVstMsgAttrSlot, slot) != kResultOk) return kInvalidArgument;
        return host_->loadFactorySlot(static_cast<int>(slot)) ? kResultOk : kInvalidArgument;
    }
    if (FIDStringsEqual(id, kVstMsgUiMidi)) {
        int64 status = 0, data1 = 0, data2 = 0;
        if (!attrs || attrs->getInt(kVstMsgAttrStatus, status) != kResultOk ||
            attrs->getInt(kVstMsgAttrData1, data1) != kResultOk ||
            attrs->getInt(kVstMsgAttrData2, data2) != kResultOk)
            return kInvalidArgument;
        const std::uint8_t bytes[3] = {static_cast<std::uint8_t>(status & 0xFF),
                                       static_cast<std::uint8_t>(data1 & 0x7F),
                                       static_cast<std::uint8_t>(data2 & 0x7F)};
        host_->injectMidi(bytes, 3);
        return kResultOk;
    }
    if (FIDStringsEqual(id, kVstMsgRequestKernelHost)) {
        sendKernelHost_();
        return kResultOk;
    }
    return AudioEffect::notify(message);
}

tresult PLUGIN_API ArpSIDVst3Processor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                           SpeakerArrangement* outputs, int32 numOuts) {
    // Instrument: no audio inputs, one stereo (or mono) output.
    if (numIns != 0 || numOuts != 1 || !outputs) return kResultFalse;
    (void)inputs;
    if (outputs[0] == SpeakerArr::kStereo || outputs[0] == SpeakerArr::kMono)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API ArpSIDVst3Processor::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0;
    host_->setup(sampleRate_, setup.maxSamplesPerBlock);
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API ArpSIDVst3Processor::setActive(TBool state) {
    if (!state) host_->reset();
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API ArpSIDVst3Processor::canProcessSampleSize(int32 symbolicSampleSize) {
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

uint32 PLUGIN_API ArpSIDVst3Processor::getTailSamples() {
    // Envelope release, reverb and delay can ring after the last note.
    return kInfiniteTail;
}

void ArpSIDVst3Processor::readTransport_(const ProcessContext* ctx, double sampleRate, int frameCount,
                                         TransportState& out) noexcept {
    out = TransportState{};
    out.sampleRate = sampleRate;
    out.frameCount = frameCount;
    if (ctx) {
        if (ctx->state & ProcessContext::kTempoValid) out.bpm = ctx->tempo;
        if (ctx->state & ProcessContext::kProjectTimeMusicValid) out.beatPosition = ctx->projectTimeMusic;
        out.isPlaying = (ctx->state & ProcessContext::kPlaying) != 0;
        out.playStateKnown = true;
        out.isLooping = (ctx->state & ProcessContext::kCycleActive) != 0;
        if (ctx->state & ProcessContext::kCycleValid) {
            out.loopStart = ctx->cycleStartMusic;
            out.loopEnd = ctx->cycleEndMusic;
        }
    }
    out.sanitize();
    out.sampleRate = sampleRate; // render rate is owned by setupProcessing
}

void ArpSIDVst3Processor::collectEvents_(ProcessData& data, int frameCount) {
    int n = 0;
    const int cap = static_cast<int>(events_.size());
    const int lastFrame = std::max(0, frameCount - 1);

    // Parameter automation: every point, sample-accurate. Program / BankSlot
    // are selections handled by the controller (factory patch load message).
    if (IParameterChanges* changes = data.inputParameterChanges) {
        const int32 queues = changes->getParameterCount();
        for (int32 q = 0; q < queues; ++q) {
            IParamValueQueue* queue = changes->getParameterData(q);
            if (!queue) continue;
            const Steinberg::Vst::ParamID pid = queue->getParameterId();
            if (pid >= static_cast<Steinberg::Vst::ParamID>(kNumParams) || pid == static_cast<Steinberg::Vst::ParamID>(kParamProgram) ||
                pid == static_cast<Steinberg::Vst::ParamID>(kParamBankSlot))
                continue;
            const int32 points = queue->getPointCount();
            for (int32 p = 0; p < points && n < cap; ++p) {
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint(p, offset, value) != kResultOk || !std::isfinite(value)) continue;
                TimedEvent ev{};
                ev.sampleOffset = std::clamp<int32>(offset, 0, lastFrame);
                ev.kind = EventKind::ParameterSet;
                ev.target = static_cast<std::uint32_t>(pid);
                ev.value = static_cast<float>(std::clamp(value, 0.0, 1.0));
                ev.value_f32 = ev.value;
                ev.rawOrder = ++eventOrder_;
                events_[static_cast<std::size_t>(n++)] = ev;
            }
        }
    }

    // Note events.
    if (IEventList* list = data.inputEvents) {
        const int32 count = list->getEventCount();
        for (int32 i = 0; i < count && n < cap; ++i) {
            Event e{};
            if (list->getEvent(i, e) != kResultOk) continue;
            TimedEvent ev{};
            ev.sampleOffset = std::clamp<int32>(e.sampleOffset, 0, lastFrame);
            ev.rawOrder = ++eventOrder_;
            switch (e.type) {
                case Event::kNoteOnEvent:
                    ev.kind = e.noteOn.velocity > 0.0f ? EventKind::NoteOn : EventKind::NoteOff;
                    ev.channel = static_cast<std::uint8_t>(std::clamp<int16>(e.noteOn.channel, 0, 15));
                    ev.pitch = static_cast<std::int16_t>(std::clamp<int16>(e.noteOn.pitch, 0, 127));
                    ev.value = std::clamp(e.noteOn.velocity, 0.0f, 1.0f);
                    ev.noteId = e.noteOn.noteId;
                    break;
                case Event::kNoteOffEvent:
                    ev.kind = EventKind::NoteOff;
                    ev.channel = static_cast<std::uint8_t>(std::clamp<int16>(e.noteOff.channel, 0, 15));
                    ev.pitch = static_cast<std::int16_t>(std::clamp<int16>(e.noteOff.pitch, 0, 127));
                    ev.value = std::clamp(e.noteOff.velocity, 0.0f, 1.0f);
                    ev.noteId = e.noteOff.noteId;
                    break;
                case Event::kPolyPressureEvent:
                    ev.kind = EventKind::PolyPressure;
                    ev.channel = static_cast<std::uint8_t>(std::clamp<int16>(e.polyPressure.channel, 0, 15));
                    ev.pitch = static_cast<std::int16_t>(std::clamp<int16>(e.polyPressure.pitch, 0, 127));
                    ev.value = std::clamp(e.polyPressure.pressure, 0.0f, 1.0f);
                    ev.noteId = e.polyPressure.noteId;
                    break;
                default:
                    continue;
            }
            events_[static_cast<std::size_t>(n++)] = ev;
        }
    }

    std::stable_sort(events_.begin(), events_.begin() + n, [](const TimedEvent& a, const TimedEvent& b) {
        return a.sampleOffset < b.sampleOffset;
    });
    eventCount_ = n;
}

tresult PLUGIN_API ArpSIDVst3Processor::process(ProcessData& data) {
    const int frames = std::max<int32>(0, data.numSamples);

    if (frames == 0) {
        // Parameter flush without audio: stage the last value of each change.
        if (IParameterChanges* changes = data.inputParameterChanges) {
            for (int32 q = 0; q < changes->getParameterCount(); ++q) {
                IParamValueQueue* queue = changes->getParameterData(q);
                if (!queue || queue->getPointCount() <= 0) continue;
                const Steinberg::Vst::ParamID pid = queue->getParameterId();
                if (pid >= static_cast<Steinberg::Vst::ParamID>(kNumParams) || pid == static_cast<Steinberg::Vst::ParamID>(kParamProgram) ||
                pid == static_cast<Steinberg::Vst::ParamID>(kParamBankSlot))
                    continue;
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultOk)
                    host_->setParameterNonRealtime(static_cast<int>(pid), static_cast<float>(value));
            }
        }
        return kResultOk;
    }

    collectEvents_(data, frames);
    TransportState transport{};
    readTransport_(data.processContext, sampleRate_, frames, transport);

    float* out[2] = {nullptr, nullptr};
    int channels = 0;
    if (data.numOutputs > 0 && data.outputs && data.outputs[0].channelBuffers32) {
        channels = std::min<int32>(2, data.outputs[0].numChannels);
        for (int c = 0; c < channels; ++c) {
            out[c] = data.outputs[0].channelBuffers32[c];
            if (out[c]) std::memset(out[c], 0, static_cast<std::size_t>(frames) * sizeof(float));
            else channels = c;
        }
    }
    host_->render(channels > 0 ? out : nullptr, channels, frames, events_.data(), eventCount_, transport);

    if (channels > 0) {
        bool silent = true;
        for (int c = 0; c < channels && silent; ++c)
            for (int i = 0; i < frames; ++i)
                if (out[c][i] != 0.0f) { silent = false; break; }
        data.outputs[0].silenceFlags = silent ? ((1ull << channels) - 1ull) : 0ull;
    }
    return kResultOk;
}

tresult PLUGIN_API ArpSIDVst3Processor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    const std::vector<std::uint8_t> bytes = host_->saveState();
    int32 written = 0;
    return (state->write(const_cast<std::uint8_t*>(bytes.data()), static_cast<int32>(bytes.size()), &written) ==
                kResultOk &&
            written == static_cast<int32>(bytes.size()))
        ? kResultOk
        : kResultFalse;
}

tresult PLUGIN_API ArpSIDVst3Processor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> chunk(64 * 1024);
    for (;;) {
        int32 got = 0;
        if (state->read(chunk.data(), static_cast<int32>(chunk.size()), &got) != kResultOk || got <= 0) break;
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + got);
        if (bytes.size() > 64u * 1024u * 1024u) return kResultFalse;
    }
    return host_->loadState(bytes.data(), bytes.size()) ? kResultOk : kResultFalse;
}

} // namespace ArpSID
