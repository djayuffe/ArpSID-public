// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VST3 audio processor on the shared ArpSIDDSPKernel (see header).

#include "vst3/arpsid_vst3_processor.h"

#include "arpsid_vst_messages.h"
#include "arpsid/core/sid_realtime_guard.h"
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
    // Optional side-chain input for DIGI sample capture (the DIGI tab's REC).
    // Auxiliary and inactive by default, so hosts treat ArpSID as a plain
    // instrument until the user routes audio into it.
    addAudioInput(STR16("DIGI Capture In"), SpeakerArr::kStereo, BusTypes::kAux, 0);
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

tresult PLUGIN_API ArpSIDVst3Processor::terminate() {
    // The controller and its editor use the kernel host directly; withdraw it
    // before this instance goes away, whatever order the host tears down in.
    if (peerConnection) sendKernelHost_(false);
    return AudioEffect::terminate();
}

tresult PLUGIN_API ArpSIDVst3Processor::connect(IConnectionPoint* other) {
    const tresult result = AudioEffect::connect(other);
    if (result == kResultOk) sendKernelHost_();
    return result;
}

tresult PLUGIN_API ArpSIDVst3Processor::disconnect(IConnectionPoint* other) {
    if (peerConnection) sendKernelHost_(false);
    return AudioEffect::disconnect(other);
}

void ArpSIDVst3Processor::sendKernelHost_(bool available) {
    IMessage* msg = allocateMessage();
    if (!msg) return;
    msg->setMessageID(kVstMsgKernelHost);
    msg->getAttributes()->setInt(kVstMsgAttrHostPtr,
                                 available ? static_cast<int64>(reinterpret_cast<std::uintptr_t>(host_.get())) : 0);
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
    if (FIDStringsEqual(id, kVstMsgLoadPresetState)) {
        // A user patch (editor preset browser, patch file, user bank): a
        // patch-only state, applied like a .vstpreset load.
        const void* data = nullptr;
        uint32 size = 0;
        if (!attrs || attrs->getBinary(kVstMsgAttrData, data, size) != kResultOk || !data || size == 0)
            return kInvalidArgument;
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        if (!Vst3KernelHost::isPresetState(bytes, size)) return kInvalidArgument;
        return host_->loadState(bytes, size) ? kResultOk : kInvalidArgument;
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
    // One stereo (or mono) output; the optional DIGI capture input may be
    // mono or stereo. Hosts that pass no input arrangement keep the default.
    if (numOuts != 1 || !outputs || numIns < 0 || numIns > 1) return kResultFalse;
    const auto monoOrStereo = [](SpeakerArrangement a) { return a == SpeakerArr::kStereo || a == SpeakerArr::kMono; };
    if (!monoOrStereo(outputs[0])) return kResultFalse;
    if (numIns == 1 && (!inputs || !monoOrStereo(inputs[0]))) return kResultFalse;
    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API ArpSIDVst3Processor::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate > 0.0 ? setup.sampleRate : 44100.0;
    host_->setup(sampleRate_, setup.maxSamplesPerBlock);
    // 64-bit hosts: the kernel renders float, converted at the bus edge.
    const std::size_t frames = static_cast<std::size_t>(std::max<int32>(1, setup.maxSamplesPerBlock));
    for (auto& b : scratchOut_) b.assign(frames, 0.0f);
    for (auto& b : scratchIn_) b.assign(frames, 0.0f);
    bypassRampStep_ = static_cast<float>(1.0 / std::max(1.0, kBypassRampSeconds * sampleRate_));
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API ArpSIDVst3Processor::setActive(TBool state) {
    if (!state) host_->reset();
    bypassGain_ = host_->bypass() ? 0.0f : 1.0f; // no ramp across activation
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API ArpSIDVst3Processor::canProcessSampleSize(int32 symbolicSampleSize) {
    return (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64) ? kResultTrue : kResultFalse;
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
    eventOrder_ = 0;
    int n = 0;
    const int cap = static_cast<int>(events_.size());
    const int lastFrame = std::max(0, frameCount - 1);

    // Note events first: they are never dropped in favour of automation (a
    // lost note-off is a stuck note). Pass 1 collects note-ons/offs; pass 2
    // fills remaining capacity with pressure events, so under a polyphony
    // storm the pressure is sacrificed before any note-off.
    if (IEventList* list = data.inputEvents) {
        const int32 count = list->getEventCount();
        for (int32 pass = 0; pass < 2 && n < cap; ++pass) {
            for (int32 i = 0; i < count && n < cap; ++i) {
                Event e{};
                if (list->getEvent(i, e) != kResultOk) continue;
                const bool isNote = (e.type == Event::kNoteOnEvent || e.type == Event::kNoteOffEvent);
                const bool isPressure = (e.type == Event::kPolyPressureEvent || e.type == Event::kChannelPressureEvent);
                if (pass == 0 ? !isNote : !isPressure) continue;
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
                    case Event::kChannelPressureEvent:
                        ev.kind = EventKind::ChannelPressure;
                        ev.channel = static_cast<std::uint8_t>(std::clamp<int16>(e.channelPressure.channel, 0, 15));
                        ev.value = std::clamp(e.channelPressure.pressure, 0.0f, 1.0f);
                        break;
                    default:
                        continue;
                }
                events_[static_cast<std::size_t>(n++)] = ev;
            }
        }
    }

    // Parameter automation, sample-accurate. Program / BankSlot are selections
    // handled by the controller (factory patch load message). The points share
    // a budget with the notes (kParamPointBudget, below the kernel's per-block
    // event lanes). Past it, each queue is thinned to evenly spaced points
    // that always include its last point, so every parameter still ends the
    // block on the host's value.
    if (IParameterChanges* changes = data.inputParameterChanges) {
        const int32 queues = changes->getParameterCount();
        int usable = 0;
        long totalPoints = 0;
        for (int32 q = 0; q < queues; ++q) {
            IParamValueQueue* queue = changes->getParameterData(q);
            if (!queue || !isAutomatableTarget_(queue->getParameterId())) continue;
            const int32 points = queue->getPointCount();
            if (points <= 0) continue;
            ++usable;
            totalPoints += points;
        }
        const int budget = std::max(0, std::min(cap - n, kParamPointBudget - n));
        // Points each queue may keep (at least its last one).
        const int perQueue = (usable > 0 && totalPoints > budget) ? std::max(1, budget / usable) : 0x7fffffff;
        for (int32 q = 0; q < queues && n < cap; ++q) {
            IParamValueQueue* queue = changes->getParameterData(q);
            if (!queue) continue;
            const Steinberg::Vst::ParamID pid = queue->getParameterId();
            if (!isAutomatableTarget_(pid)) continue;
            const int32 points = queue->getPointCount();
            if (points <= 0) continue;
            const int keep = std::min<int32>(points, perQueue);
            for (int k = 0; k < keep && n < cap; ++k) {
                // k-th of `keep` evenly spaced points, ending on the last one.
                const int32 p = keep == points ? k
                                               : static_cast<int32>((static_cast<long>(k + 1) * points) / keep) - 1;
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

    // The kernel's canonical total order (sample offset, then kind priority,
    // then arrival). std::sort on a total order is deterministic and, unlike
    // std::stable_sort, never allocates a temporary buffer.
    std::sort(events_.begin(), events_.begin() + n, TimedEvent::before);
    eventCount_ = n;
}

bool ArpSIDVst3Processor::isAutomatableTarget_(Steinberg::Vst::ParamID pid) noexcept {
    return pid < static_cast<Steinberg::Vst::ParamID>(kNumParams) &&
           pid != static_cast<Steinberg::Vst::ParamID>(kParamProgram) &&
           pid != static_cast<Steinberg::Vst::ParamID>(kParamBankSlot);
}

void ArpSIDVst3Processor::readBypass_(ProcessData& data) noexcept {
    IParameterChanges* changes = data.inputParameterChanges;
    if (!changes) return;
    for (int32 q = 0; q < changes->getParameterCount(); ++q) {
        IParamValueQueue* queue = changes->getParameterData(q);
        if (!queue || queue->getParameterId() != static_cast<ParamID>(kVst3BypassParamId)) continue;
        int32 offset = 0;
        ParamValue value = 0.0;
        const int32 points = queue->getPointCount();
        if (points > 0 && queue->getPoint(points - 1, offset, value) == kResultOk && std::isfinite(value))
            host_->setBypass(value >= 0.5);
    }
}

void ArpSIDVst3Processor::applyBypass_(float** out, int channels, int frames) noexcept {
    const float target = host_->bypass() ? 0.0f : 1.0f;
    if (bypassGain_ == target) {
        if (target == 0.0f)
            for (int c = 0; c < channels; ++c) std::memset(out[c], 0, static_cast<std::size_t>(frames) * sizeof(float));
        return;
    }
    // Short linear fade so bypass switching never clicks; the engine keeps
    // running (notes, arp and sequencer stay in time) while bypassed.
    for (int i = 0; i < frames; ++i) {
        bypassGain_ = target > bypassGain_ ? std::min(target, bypassGain_ + bypassRampStep_)
                                           : std::max(target, bypassGain_ - bypassRampStep_);
        for (int c = 0; c < channels; ++c) out[c][i] *= bypassGain_;
    }
}

tresult PLUGIN_API ArpSIDVst3Processor::process(ProcessData& data) {
    // Audio thread: the kernel's lock/allocation guards count violations
    // for the whole callback, not only inside the kernel.
    ArpSID::SidRealtimeScope realtimeScope("ArpSIDVst3Processor::process");
    const int frames = std::max<int32>(0, data.numSamples);
    readBypass_(data);

    if (frames == 0) {
        // Parameter flush without audio: stage the last value of each change.
        if (IParameterChanges* changes = data.inputParameterChanges) {
            for (int32 q = 0; q < changes->getParameterCount(); ++q) {
                IParamValueQueue* queue = changes->getParameterData(q);
                if (!queue || queue->getPointCount() <= 0) continue;
                const Steinberg::Vst::ParamID pid = queue->getParameterId();
                if (!isAutomatableTarget_(pid)) continue;
                int32 offset = 0;
                ParamValue value = 0.0;
                if (queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultOk)
                    host_->setParameterNonRealtime(static_cast<int>(pid), static_cast<float>(value));
            }
        }
        return kResultOk;
    }

    const bool is64 = data.symbolicSampleSize == kSample64;
    collectEvents_(data, frames);
    TransportState transport{};
    readTransport_(data.processContext, sampleRate_, frames, transport);

    // 32-bit blocks render in one call (the kernel splits blocks larger than
    // its 4096-frame chunk itself, events and musical position included).
    // 64-bit blocks go through the float scratch sized in setupProcessing; a
    // host that exceeds its announced block size is rendered in scratch-sized
    // slices instead of being dropped.
    const int slice = is64 ? static_cast<int>(scratchOut_[0].size()) : frames;
    if (slice <= 0) return kResultOk;
    float peak = 0.0f;
    int channels = 0;
    int ev = 0;
    for (int start = 0; start < frames; start += slice) {
        const int n = std::min(slice, frames - start);
        // Events of this slice (sorted), rebased to the slice.
        const int evBegin = ev;
        while (ev < eventCount_ && events_[static_cast<std::size_t>(ev)].sampleOffset < start + n) {
            events_[static_cast<std::size_t>(ev)].sampleOffset -= start;
            ++ev;
        }
        TransportState t = transport;
        t.frameCount = n;
        if (start > 0 && sampleRate_ > 0.0 && t.bpm > 0.0)
            t.beatPosition += static_cast<double>(start) * t.bpm / (sampleRate_ * 60.0);
        channels = renderSlice_(data, is64, start, n, events_.data() + evBegin, ev - evBegin, t, peak);
    }

    // Silence: once the voices have released, the SID output stage settles
    // on a tiny constant residual (about -150 dBFS), never exact zero. A
    // block below -120 dBFS is written as zero and flagged silent, so hosts
    // can skip processing further down the chain.
    constexpr float kSilenceGate = 1.0e-6f;
    if (channels > 0) {
        const bool silent = peak <= kSilenceGate;
        if (silent && peak > 0.0f)
            for (int c = 0; c < channels; ++c) {
                if (is64) std::memset(data.outputs[0].channelBuffers64[c], 0, static_cast<std::size_t>(frames) * sizeof(double));
                else std::memset(data.outputs[0].channelBuffers32[c], 0, static_cast<std::size_t>(frames) * sizeof(float));
            }
        data.outputs[0].silenceFlags = silent ? ((1ull << channels) - 1ull) : 0ull;
    }
    return kResultOk;
}

int ArpSIDVst3Processor::renderSlice_(ProcessData& data, bool is64, int start, int frames, const TimedEvent* events,
                                      int eventCount, const TransportState& transport, float& peak) noexcept {
    // DIGI capture input (side-chain). Only read while a capture is armed.
    const float* in[2] = {nullptr, nullptr};
    int inChannels = 0;
    if (data.numInputs > 0 && data.inputs && data.inputs[0].numChannels > 0) {
        inChannels = std::min<int32>(2, data.inputs[0].numChannels);
        for (int c = 0; c < inChannels; ++c) {
            if (is64) {
                const double* src = data.inputs[0].channelBuffers64 ? data.inputs[0].channelBuffers64[c] : nullptr;
                if (!src) { inChannels = c; break; }
                for (int i = 0; i < frames; ++i)
                    scratchIn_[c][static_cast<std::size_t>(i)] = static_cast<float>(src[start + i]);
                in[c] = scratchIn_[c].data();
            } else {
                const float* src = data.inputs[0].channelBuffers32 ? data.inputs[0].channelBuffers32[c] : nullptr;
                if (!src) { inChannels = c; break; }
                in[c] = src + start;
            }
        }
    }
    host_->setDigiCaptureInputActive(inChannels > 0);
    if (inChannels > 0) host_->captureDigiInput(in, inChannels, frames);

    float* out[2] = {nullptr, nullptr};
    int channels = 0;
    if (data.numOutputs > 0 && data.outputs) {
        const int32 busChannels = data.outputs[0].numChannels;
        channels = std::min<int32>(2, busChannels);
        for (int c = 0; c < channels; ++c) {
            if (is64) {
                out[c] = (data.outputs[0].channelBuffers64 && data.outputs[0].channelBuffers64[c])
                             ? scratchOut_[c].data() : nullptr;
            } else {
                out[c] = (data.outputs[0].channelBuffers32 && data.outputs[0].channelBuffers32[c])
                             ? data.outputs[0].channelBuffers32[c] + start : nullptr;
            }
            if (!out[c]) { channels = c; break; }
            std::memset(out[c], 0, static_cast<std::size_t>(frames) * sizeof(float));
        }
    }
    host_->render(channels > 0 ? out : nullptr, channels, frames, events, eventCount, transport);
    if (channels <= 0) return 0;
    applyBypass_(out, channels, frames);

    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(out[c][i]));
    if (is64)
        for (int c = 0; c < channels; ++c) {
            double* d = data.outputs[0].channelBuffers64[c] + start;
            for (int i = 0; i < frames; ++i) d[i] = static_cast<double>(out[c][i]);
        }
    return channels;
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
