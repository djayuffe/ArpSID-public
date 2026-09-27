// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID — VST3 host for the shared ArpSIDDSPKernel (see header).

#include "vst3/arpsid_vst3_kernel_host.h"

#include "au3/ArpSIDDSPKernel.hpp"
#include "au3/ArpSIDKernelTelemetryFill.h"
#include "au3/ArpSIDStateSerializer.h"
#include "factory_patch_params.h"
#include "parameter_ids.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace ArpSID {

namespace {

// ── VST3 component state, version 5 ─────────────────────────────────────────
//   u32 version (5), then tagged chunks: u32 tag, u32 length, payload.
// Unknown tags are skipped, so later versions can add chunks. Versions 1..4
// are the legacy Phase2 layout: u32 version followed by one state-root blob.
constexpr std::uint32_t kStateVersion = 5u;

constexpr std::uint32_t fourcc(char a, char b, char c, char d) noexcept {
    return (std::uint32_t(std::uint8_t(a)) << 24) | (std::uint32_t(std::uint8_t(b)) << 16) |
           (std::uint32_t(std::uint8_t(c)) << 8) | std::uint32_t(std::uint8_t(d));
}
constexpr std::uint32_t kTagRoot     = fourcc('R', 'O', 'O', 'T'); // canonical state root
constexpr std::uint32_t kTagSettings = fourcc('S', 'E', 'T', 'S'); // 32-byte settings model
constexpr std::uint32_t kTagMix      = fourcc('M', 'I', 'X', ' '); // MixPanelModel
constexpr std::uint32_t kTagKit      = fourcc('K', 'I', 'T', ' '); // KitStateBlob
constexpr std::uint32_t kTagDigi     = fourcc('D', 'I', 'G', 'M'); // DigiPanelModel
constexpr std::uint32_t kTagDigiBank = fourcc('D', 'I', 'G', 'B'); // DigiSampleBankBlob
constexpr std::uint32_t kTagDigiRt   = fourcc('D', 'I', 'G', 'R'); // D418 mode + rate + pad map
constexpr std::uint32_t kTagOutput   = fourcc('O', 'U', 'T', 'M'); // pure SID 1Q1 output mode

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
std::uint32_t getU32(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}
void putChunk(std::vector<std::uint8_t>& out, std::uint32_t tag, const void* data, std::size_t len) {
    putU32(out, tag);
    putU32(out, static_cast<std::uint32_t>(len));
    const auto* b = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), b, b + len);
}

bool decodeRoot(const std::uint8_t* blob, std::size_t len, std::uint32_t version, SidStateRootV1& root) {
    const std::uint32_t magic = (version >= 4u) ? kSidBinaryStateMagic : kSidBinaryPatchStateMagic;
    if (!decodeStateToRoot(blob, len, root, magic)) return false;
    sanitizePersistentStateRootForSerialization(root);
    return root.valid();
}

} // namespace

int Vst3KernelHost::maxKernelFrames() noexcept { return ArpSIDDSPKernel::kMaxFramesPerBlock; }

Vst3KernelHost::Vst3KernelHost()
    : kernel_(std::make_unique<ArpSIDDSPKernel>()),
      settings_(GUI::makeDefaultSettings()),
      mix_(GUI::makeDefaultMixModel()),
      kit_(GUI::makeDefaultKitStateBlob()),
      digiModel_(GUI::makeDefaultDigiPanelModel()),
      digiBank_(std::make_unique<GUI::DigiSampleBankBlob>()),
      chunkEvents_(std::make_unique<std::vector<TimedEvent>>(kMaxTimedEvents)) {
    prewarmAllSidTables();
    GUI::resetDigiSampleBankBlob(*digiBank_);
    kernel_->setComponentFlavor(static_cast<int>(ComponentFlavor::Hybrid));
    std::lock_guard<std::mutex> lock(modelMutex_);
    publishModelsLocked_(true);
}

Vst3KernelHost::~Vst3KernelHost() = default;

void Vst3KernelHost::setup(double sampleRate, int maxFrames) {
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 44100.0;
    maxFrames_ = std::max(1, maxFrames);
    // Keep the audible state across re-setup (sample-rate / block-size change).
    std::array<float, kNumParams> snapshot{};
    for (int i = 0; i < kNumParams; ++i) snapshot[static_cast<std::size_t>(i)] = kernel_->getParameter(i);
    const int stickySlot = kernel_->stickyPresetDisplaySlot();
    std::lock_guard<std::mutex> lock(modelMutex_);
    kernel_->requestAudioEngineMode(
        settings_.audioEngineMode == GUI::AudioEngineMode::SingleSid3Voice ? 1u : 0u);
    kernel_->setup(sampleRate_, std::min(maxFrames_, ArpSIDDSPKernel::kMaxFramesPerBlock));
    kernel_->restoreHostParameterSnapshotImmediate(snapshot.data(), kNumParams);
    kernel_->setStickyPresetDisplaySlot(stickySlot);
    publishModelsLocked_(true);
}

void Vst3KernelHost::reset() noexcept { kernel_->reset(); }

void Vst3KernelHost::render(float** outputs, int numChannels, int frameCount,
                            const TimedEvent* events, int eventCount,
                            const TransportState& transport) noexcept {
    if (frameCount <= 0) return;
    // Any root scheduled before this point is drained by the first processBlock.
    const std::uint64_t seq = scheduledSeq_.load(std::memory_order_acquire);
    renderBlocks_(outputs, numChannels, frameCount, events, eventCount, transport);
    renderedSeq_.store(seq, std::memory_order_release);
}

void Vst3KernelHost::renderBlocks_(float** outputs, int numChannels, int frameCount,
                                   const TimedEvent* events, int eventCount,
                                   const TransportState& transport) noexcept {
    const int chunkMax = ArpSIDDSPKernel::kMaxFramesPerBlock;
    if (frameCount <= chunkMax) {
        TransportState t = transport;
        t.frameCount = frameCount;
        if (numChannels >= 2) kernel_->processBlock(outputs, numChannels, frameCount, events, eventCount, t);
        else if (numChannels == 1) kernel_->processBlockMono(outputs[0], frameCount, events, eventCount, t);
        else kernel_->processBlock(nullptr, 0, frameCount, events, eventCount, t);
        return;
    }
    // Hosts may exceed the kernel chunk size: split, rebasing event offsets and
    // advancing the musical position per chunk (same law as the AUv3 wrapper).
    std::vector<TimedEvent>& chunkEvents = *chunkEvents_;
    int next = 0;
    for (int start = 0; start < frameCount; start += chunkMax) {
        const int frames = std::min(chunkMax, frameCount - start);
        int n = 0;
        while (next < eventCount && (events[next].sampleOffset < start + frames)) {
            if (n < static_cast<int>(chunkEvents.size())) {
                TimedEvent ev = events[next];
                ev.sampleOffset = ev.sampleOffset < 0 ? -1 : std::max(0, ev.sampleOffset - start);
                chunkEvents[static_cast<std::size_t>(n++)] = ev;
            }
            ++next;
        }
        TransportState t = transport;
        t.frameCount = frames;
        if (t.sampleRate > 0.0 && t.bpm > 0.0)
            t.beatPosition = transport.beatPosition + double(start) * t.bpm / (t.sampleRate * 60.0);
        float* chunkOut[2] = {nullptr, nullptr};
        for (int c = 0; c < std::min(numChannels, 2); ++c) chunkOut[c] = outputs[c] + start;
        if (numChannels >= 2) kernel_->processBlock(chunkOut, 2, frames, chunkEvents.data(), n, t);
        else if (numChannels == 1) kernel_->processBlockMono(chunkOut[0], frames, chunkEvents.data(), n, t);
        else kernel_->processBlock(nullptr, 0, frames, chunkEvents.data(), n, t);
    }
}

void Vst3KernelHost::setParameterNonRealtime(int paramId, float normalized) noexcept {
    if (paramId < 0 || paramId >= kNumParams || !std::isfinite(normalized)) return;
    kernel_->enqueueParameterIntent(paramId, std::clamp(normalized, 0.0f, 1.0f));
}

float Vst3KernelHost::parameter(int paramId) const noexcept { return kernel_->getParameter(paramId); }

bool Vst3KernelHost::loadFactorySlot(int slot) {
    if (slot < 0 || slot > kCanonicalFactoryPatchSlotMax) return false;
    SidStateRootV1 root = makeFactoryPatchStateRootForSlot(slot);
    if (!root.valid()) return false;
    kernel_->setStickyPresetDisplaySlot(slot);
    scheduleRoot_(root);
    return true;
}

void Vst3KernelHost::scheduleStateRoot(const SidStateRootV1& root) {
    if (!root.valid()) return;
    scheduleRoot_(root);
}

void Vst3KernelHost::scheduleRoot_(const SidStateRootV1& root) {
    std::lock_guard<std::mutex> lock(pendingRootMutex_);
    if (!pendingRoot_) pendingRoot_ = std::make_unique<SidStateRootV1>();
    *pendingRoot_ = root;
    kernel_->schedulePendingStateRestore(root);
    scheduledSeq_.fetch_add(1, std::memory_order_acq_rel);
}

void Vst3KernelHost::currentStateRoot(SidStateRootV1& out) const noexcept {
    {
        std::lock_guard<std::mutex> lock(pendingRootMutex_);
        if (pendingRoot_ && renderedSeq_.load(std::memory_order_acquire) <
                                scheduledSeq_.load(std::memory_order_acquire)) {
            out = *pendingRoot_;
            return;
        }
    }
    kernel_->buildSerializableStateRootFromShadow(out);
}

std::vector<std::uint8_t> Vst3KernelHost::saveState() const {
    std::vector<std::uint8_t> out;
    out.reserve(sizeof(GUI::DigiSampleBankBlob) + 64 * 1024);
    putU32(out, kStateVersion);

    SidStateRootV1 root{};
    currentStateRoot(root);
    const std::size_t cap = encodedSidStateRootBinarySize(root);
    if (cap > 0) {
        std::vector<std::uint8_t> blob(cap);
        const std::size_t len = encodeStateRoot(root, kSidBinaryStateMagic, blob.data(), cap);
        if (len > 0) putChunk(out, kTagRoot, blob.data(), len);
    }

    std::lock_guard<std::mutex> lock(modelMutex_);
    std::array<std::uint8_t, 32> settingsBytes{};
    GUI::serializeSettings(settings_, settingsBytes);
    putChunk(out, kTagSettings, settingsBytes.data(), settingsBytes.size());
    putChunk(out, kTagMix, &mix_, sizeof(mix_));
    putChunk(out, kTagKit, &kit_, sizeof(kit_));
    putChunk(out, kTagDigi, &digiModel_, sizeof(digiModel_));
    putChunk(out, kTagDigiBank, digiBank_.get(), sizeof(GUI::DigiSampleBankBlob));
    const std::uint8_t digiRt[6] = {
        kernel_->digiD418RuntimeMode(),
        static_cast<std::uint8_t>(kernel_->digiD418RuntimeRateHz() & 0xFFu),
        static_cast<std::uint8_t>((kernel_->digiD418RuntimeRateHz() >> 8) & 0xFFu),
        static_cast<std::uint8_t>((kernel_->digiD418RuntimeRateHz() >> 16) & 0xFFu),
        kernel_->digiMidiRootNote(),
        kernel_->digiMidiChannelFilter()};
    putChunk(out, kTagDigiRt, digiRt, sizeof(digiRt));
    const std::uint8_t pure = kernel_->pureSid1Q1OutputModeEnabled() ? 1u : 0u;
    putChunk(out, kTagOutput, &pure, 1);
    return out;
}

bool Vst3KernelHost::decodeStateRoot(const std::uint8_t* data, std::size_t size, SidStateRootV1& out) {
    if (!data || size < 4) return false;
    const std::uint32_t version = getU32(data);
    if (version < kStateVersion) return decodeRoot(data + 4, size - 4, version, out);
    std::size_t pos = 4;
    while (pos + 8 <= size) {
        const std::uint32_t tag = getU32(data + pos);
        const std::uint32_t len = getU32(data + pos + 4);
        pos += 8;
        if (len > size - pos) return false;
        if (tag == kTagRoot) return decodeRoot(data + pos, len, 4u, out);
        pos += len;
    }
    return false;
}

bool Vst3KernelHost::loadState(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 4) return false;
    const std::uint32_t version = getU32(data);

    if (version < kStateVersion) {
        // Legacy Phase2 VST3 state: one canonical root blob after the version.
        SidStateRootV1 root{};
        if (!decodeRoot(data + 4, size - 4, version, root)) return false;
        scheduleRoot_(root);
        return true;
    }

    bool haveRoot = false;
    bool haveDigiModel = false, haveDigiBank = false;
    GUI::DigiPanelModel digiModel{};
    std::unique_ptr<GUI::DigiSampleBankBlob> digiBank;
    std::lock_guard<std::mutex> lock(modelMutex_);
    std::size_t pos = 4;
    while (pos + 8 <= size) {
        const std::uint32_t tag = getU32(data + pos);
        const std::uint32_t len = getU32(data + pos + 4);
        pos += 8;
        if (len > size - pos) return haveRoot; // truncated: keep what was applied
        const std::uint8_t* p = data + pos;
        pos += len;
        switch (tag) {
            case kTagRoot: {
                SidStateRootV1 root{};
                if (decodeRoot(p, len, 4u, root)) {
                    scheduleRoot_(root);
                    haveRoot = true;
                }
                break;
            }
            case kTagSettings:
                if (len == 32) {
                    std::array<std::uint8_t, 32> bytes{};
                    std::memcpy(bytes.data(), p, 32);
                    settings_ = GUI::sanitizeSettings(GUI::deserializeSettings(bytes));
                    kernel_->requestAudioEngineMode(
                        settings_.audioEngineMode == GUI::AudioEngineMode::SingleSid3Voice ? 1u : 0u);
                }
                break;
            case kTagMix:
                if (len == sizeof(GUI::MixPanelModel)) {
                    std::memcpy(&mix_, p, len);
                    GUI::sanitizeMixModel(mix_);
                }
                break;
            case kTagKit: {
                GUI::KitStateBlob blob{};
                if (GUI::kitStateBlobDeserialize(p, len, blob)) {
                    kit_ = blob;
                    GUI::kitStateBlobSanitize(kit_);
                }
                break;
            }
            case kTagDigi:
                haveDigiModel = GUI::deserializeDigiPanelModel(p, len, digiModel);
                break;
            case kTagDigiBank:
                if (len == sizeof(GUI::DigiSampleBankBlob)) {
                    digiBank = std::make_unique<GUI::DigiSampleBankBlob>();
                    std::memcpy(digiBank.get(), p, len);
                    haveDigiBank = true;
                }
                break;
            case kTagDigiRt:
                if (len >= 6) {
                    kernel_->setDigiD418RuntimePolicy(
                        p[0], std::uint32_t(p[1]) | (std::uint32_t(p[2]) << 8) | (std::uint32_t(p[3]) << 16));
                    kernel_->setDigiMidiPadMapping(p[4], p[5]);
                }
                break;
            case kTagOutput:
                if (len >= 1) kernel_->setPureSid1Q1OutputMode(p[0] != 0);
                break;
            default:
                break; // forward compatibility: skip unknown chunks
        }
    }
    // DIGI model and bank are restored only as a matched pair (same rule as AU).
    if (haveDigiModel && haveDigiBank) {
        digiModel_ = digiModel;
        *digiBank_ = *digiBank;
        GUI::sanitizeDigiSampleBankBlob(*digiBank_);
        GUI::sanitizeDigiPanelModel(digiModel_);
        GUI::digiRepairUserSampleReferences(digiModel_, *digiBank_);
    }
    publishModelsLocked_(true);
    return haveRoot;
}

GUI::SettingsPanelModel Vst3KernelHost::settings() const {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return settings_;
}

void Vst3KernelHost::setSettings(const GUI::SettingsPanelModel& m) {
    std::lock_guard<std::mutex> lock(modelMutex_);
    settings_ = GUI::sanitizeSettings(m);
    kernel_->requestAudioEngineMode(
        settings_.audioEngineMode == GUI::AudioEngineMode::SingleSid3Voice ? 1u : 0u);
    ++modelGeneration_;
}

GUI::MixPanelModel Vst3KernelHost::mix() const {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return mix_;
}

void Vst3KernelHost::setMix(const GUI::MixPanelModel& m) {
    std::lock_guard<std::mutex> lock(modelMutex_);
    mix_ = m;
    GUI::sanitizeMixModel(mix_);
    publishModelsLocked_(false);
}

GUI::KitStateBlob Vst3KernelHost::kit() const {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return kit_;
}

void Vst3KernelHost::setKit(const GUI::KitStateBlob& b) {
    std::lock_guard<std::mutex> lock(modelMutex_);
    kit_ = b;
    GUI::kitStateBlobSanitize(kit_);
    publishModelsLocked_(false);
}

void Vst3KernelHost::digi(GUI::DigiPanelModel& model, GUI::DigiSampleBankBlob& bank) const {
    {
        std::lock_guard<std::mutex> lock(modelMutex_);
        model = digiModel_;
        bank = *digiBank_;
    }
    GUI::sanitizeDigiSampleBankBlob(bank);
    GUI::sanitizeDigiPanelModel(model);
    GUI::digiRepairUserSampleReferences(model, bank);
}

void Vst3KernelHost::setDigi(const GUI::DigiPanelModel& model, const GUI::DigiSampleBankBlob& bank) {
    std::lock_guard<std::mutex> lock(modelMutex_);
    digiModel_ = model;
    *digiBank_ = bank;
    GUI::sanitizeDigiSampleBankBlob(*digiBank_);
    GUI::sanitizeDigiPanelModel(digiModel_);
    GUI::digiRepairUserSampleReferences(digiModel_, *digiBank_);
    publishModelsLocked_(true);
}

bool Vst3KernelHost::setDigiUserSample(int slot, const float* samples, std::uint32_t frameCount,
                                       double sampleRate, const char* name) {
    if (slot < 0 || slot >= GUI::kDigiActiveSlotCount || !samples || frameCount == 0) return false;
    const std::uint32_t rateHz = static_cast<std::uint32_t>(
        std::clamp<double>(std::isfinite(sampleRate) ? std::round(sampleRate) : 44100.0, 1000.0, 384000.0));
    const std::uint32_t nameLen = name
        ? static_cast<std::uint32_t>(std::min<std::size_t>(std::strlen(name), std::numeric_limits<std::uint32_t>::max()))
        : 0u;
    const auto slotIndex = static_cast<std::uint8_t>(slot);
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!GUI::digiLoadUserSampleFromFloatMono(*digiBank_, slotIndex, samples, frameCount, rateHz, name, nameLen))
        return false;
    const GUI::DigiUserSampleClip& clip = digiBank_->clips[slotIndex];
    GUI::digiSetUserSampleSlot(digiModel_.slots[slotIndex], slotIndex, clip.handle);
    digiModel_.activeSlot = slotIndex;
    GUI::sanitizeDigiPanelModel(digiModel_);
    GUI::digiRepairUserSampleReferences(digiModel_, *digiBank_);
    publishModelsLocked_(true);
    return true;
}

void Vst3KernelHost::setDigiD418RuntimeMode(std::uint8_t mode, std::uint32_t rateHz) noexcept {
    kernel_->setDigiD418RuntimePolicy(mode, rateHz);
}

void Vst3KernelHost::digiD418RuntimeMode(std::uint8_t& mode, std::uint32_t& rateHz) const noexcept {
    mode = kernel_->digiD418RuntimeMode();
    rateHz = kernel_->digiD418RuntimeRateHz();
}

void Vst3KernelHost::setDigiMidiPadMapping(std::uint8_t rootNote, std::uint8_t channelFilter) noexcept {
    kernel_->setDigiMidiPadMapping(rootNote, channelFilter);
}

void Vst3KernelHost::digiMidiPadMapping(std::uint8_t& rootNote, std::uint8_t& channelFilter) const noexcept {
    rootNote = kernel_->digiMidiRootNote();
    channelFilter = kernel_->digiMidiChannelFilter();
}

void Vst3KernelHost::triggerDigiPad(std::uint8_t slot, std::uint8_t velocity) noexcept {
    kernel_->triggerDigiPadForGui(slot, velocity);
}

void Vst3KernelHost::clearDigiD418Telemetry() noexcept { kernel_->clearDigiD418RuntimeTelemetryForGui(); }

bool Vst3KernelHost::loadSidFile(const void* data, std::size_t size, std::uint16_t subtune) {
    if (!data || size == 0) return false;
    return kernel_->loadPsidData(data, size, subtune);
}

void Vst3KernelHost::unloadSidFile() noexcept { kernel_->unloadPsid(); }

void Vst3KernelHost::c64ControlHubCommand(int command) noexcept {
    using Cmd = ArpSIDDSPKernel::C64ControlHubCommand;
    switch (command) {
        case 1: kernel_->performC64ControlHubCommand(Cmd::Boot); break;
        case 2: kernel_->performC64ControlHubCommand(Cmd::Start); break;
        case 3: kernel_->performC64ControlHubCommand(Cmd::Stop); break;
        case 4: kernel_->performC64ControlHubCommand(Cmd::Reset); break;
        case 5: kernel_->performC64ControlHubCommand(Cmd::LoadProjectionBootstrap); break;
        case 6: kernel_->setC64VicFast(true); break;
        case 7: kernel_->setC64VicFast(false); break;
        case 8: kernel_->setC64CpuFast(true); break;
        case 9: kernel_->setC64CpuFast(false); break;
        default: break;
    }
}

bool Vst3KernelHost::isSidFileLoaded() const noexcept { return kernel_->isPsidLoaded(); }

void Vst3KernelHost::setPureSid1Q1OutputMode(bool on) noexcept { kernel_->setPureSid1Q1OutputMode(on); }

bool Vst3KernelHost::pureSid1Q1OutputMode() const noexcept { return kernel_->pureSid1Q1OutputModeEnabled(); }

void Vst3KernelHost::injectMidi(const std::uint8_t* data, std::uint8_t length) noexcept {
    if (!data || length < 1 || length > 3) return;
    kernel_->pushMidi(data, length);
}

void Vst3KernelHost::readTelemetry(ArpSIDTelemetry& out, bool includeScopes, bool includeC64Snapshot) const noexcept {
    TelemetryFill::fillTelemetryFromKernel(*kernel_, &out, includeScopes, includeC64Snapshot);
}

void Vst3KernelHost::pollNonRealtime() noexcept { kernel_->drainQueuedDrumBridgeSlotLoadNonRealtime(); }

std::uint64_t Vst3KernelHost::modelGeneration() const noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    return modelGeneration_;
}

void Vst3KernelHost::publishModelsLocked_(bool includeDigiSampleBank) noexcept {
    GUI::sanitizeDigiSampleBankBlob(*digiBank_);
    GUI::sanitizeDigiPanelModel(digiModel_);
    GUI::digiRepairUserSampleReferences(digiModel_, *digiBank_);
    kernel_->publishGuiRealtimeModels(&mix_, &kit_, &digiModel_, includeDigiSampleBank ? digiBank_.get() : nullptr);
    ++modelGeneration_;
}

} // namespace ArpSID
