// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VST3 audio processor on the shared ArpSIDDSPKernel.
//
// Same engine as AUv2/AUv3/Standalone (see arpsid_vst3_kernel_host.h). The
// processor translates VST3 process data (sample-accurate parameter points,
// note events, ProcessContext transport) into the kernel's canonical
// TimedEvent/TransportState contract, persists the full kernel + GUI-model
// state, and hands its kernel host to the edit controller (same process) so
// the editor can read telemetry and edit the non-parameter models.
#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include "au3/ArpSIDCanonicalEvents.h"
#include "vst3/arpsid_vst3_kernel_host.h"

#include <array>
#include <memory>
#include <vector>

namespace ArpSID {

class ArpSIDVst3Processor final : public Steinberg::Vst::AudioEffect {
public:
    ArpSIDVst3Processor();
    ~ArpSIDVst3Processor() override;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new ArpSIDVst3Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                                     Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs,
                                                     Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
    Steinberg::uint32 PLUGIN_API getTailSamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    Vst3KernelHost& host() noexcept { return *host_; }

private:
    // Tells the controller where the kernel host is (nullptr: it is gone).
    void sendKernelHost_(bool available = true);
    void collectEvents_(Steinberg::Vst::ProcessData& data, int frameCount);
    // Renders frames [start, start + frames) of the host block; returns the
    // output channel count and raises `peak` to the slice's largest |sample|.
    int renderSlice_(Steinberg::Vst::ProcessData& data, bool is64, int start, int frames, const TimedEvent* events,
                     int eventCount, const TransportState& transport, float& peak) noexcept;
    static bool isAutomatableTarget_(Steinberg::Vst::ParamID pid) noexcept;
    void readBypass_(Steinberg::Vst::ProcessData& data) noexcept;
    void applyBypass_(float** out, int channels, int frames) noexcept;
    static void readTransport_(const Steinberg::Vst::ProcessContext* ctx, double sampleRate,
                               int frameCount, TransportState& out) noexcept;

    std::unique_ptr<Vst3KernelHost> host_;
    std::vector<TimedEvent> events_;   // preallocated render scratch
    // Parameter points per block (shared with the notes) that reach the
    // kernel; denser automation is thinned per queue (see collectEvents_).
    static constexpr int kParamPointBudget = 256;
    int eventCount_ = 0;
    std::uint32_t eventOrder_ = 0;
    double sampleRate_ = 44100.0;
    // 64-bit processing scratch (float render, converted at the bus edge).
    std::array<std::vector<float>, 2> scratchOut_{};
    std::array<std::vector<float>, 2> scratchIn_{};
    // Host bypass fade (audio thread).
    static constexpr double kBypassRampSeconds = 0.01;
    float bypassGain_ = 1.0f;
    float bypassRampStep_ = 1.0f / 441.0f;
};

} // namespace ArpSID
