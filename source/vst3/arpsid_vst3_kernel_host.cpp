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
#include <thread>

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
constexpr std::uint32_t kTagSidFile  = fourcc('S', 'I', 'D', 'F'); // u16 subtune + loaded .sid file
constexpr std::uint32_t kTagBypass   = fourcc('B', 'Y', 'P', 'S'); // host bypass (1 byte)
constexpr std::uint32_t kTagPreset   = fourcc('P', 'R', 'S', 'T'); // patch-only state (no payload)
static_assert(kTagRoot == kVst3StateTagRoot && kTagPreset == kVst3StateTagPreset && kStateVersion == kVst3StateVersion,
              "public state constants match the codec");
constexpr std::size_t kMaxSidFileBytes = 1u << 20;                    // PSID/RSID files are far smaller

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
      digiBank_(std::make_unique<GUI::DigiSampleBankBlob>()) {
    prewarmAllSidTables();
    GUI::resetDigiSampleBankBlob(*digiBank_);
    kernel_->setComponentFlavor(static_cast<int>(ComponentFlavor::Hybrid));
    std::lock_guard<std::mutex> lock(modelMutex_);
    publishModelsLocked_(true);
}

Vst3KernelHost::~Vst3KernelHost() = default;

void Vst3KernelHost::setup(double sampleRate, int maxFrames) {
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 44100.0;
    // Keep the audible state across re-setup (sample-rate / block-size change).
    std::array<float, kNumParams> snapshot{};
    for (int i = 0; i < kNumParams; ++i) snapshot[static_cast<std::size_t>(i)] = kernel_->getParameter(i);
    const int stickySlot = kernel_->stickyPresetDisplaySlot();
    std::lock_guard<std::mutex> lock(modelMutex_);
    kernel_->requestAudioEngineMode(
        settings_.audioEngineMode == GUI::AudioEngineMode::SingleSid3Voice ? 1u : 0u);
    kernel_->setup(sampleRate_, std::clamp(maxFrames, 1, ArpSIDDSPKernel::kMaxFramesPerBlock));
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
    // The whole host block goes to the kernel. Blocks larger than its
    // 4096-frame chunk are split there, which also drains the editor's queued
    // MIDI and parameter intents once against the full block (splitting here
    // would drain them per chunk and shift their timing).
    TransportState t = transport;
    t.frameCount = frameCount;
    if (numChannels >= 2) kernel_->processBlock(outputs, 2, frameCount, events, eventCount, t);
    else if (numChannels == 1) kernel_->processBlockMono(outputs[0], frameCount, events, eventCount, t);
    else kernel_->processBlock(nullptr, 0, frameCount, events, eventCount, t);
    renderedSeq_.store(seq, std::memory_order_release);
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
    const std::uint8_t bypassed = bypass() ? 1u : 0u;
    putChunk(out, kTagBypass, &bypassed, 1);
    {
        std::lock_guard<std::mutex> sidLock(sidMutex_);
        if (!sidFile_.empty() && kernel_->isPsidLoaded()) {
            std::vector<std::uint8_t> payload;
            payload.reserve(2 + sidFile_.size());
            payload.push_back(static_cast<std::uint8_t>(sidSubtune_ & 0xFFu));
            payload.push_back(static_cast<std::uint8_t>(sidSubtune_ >> 8));
            payload.insert(payload.end(), sidFile_.begin(), sidFile_.end());
            putChunk(out, kTagSidFile, payload.data(), payload.size());
        }
    }
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

bool Vst3KernelHost::isPresetState(const std::uint8_t* data, std::size_t size) noexcept {
    if (!data || size < 4 || getU32(data) < kStateVersion) return false;
    std::size_t pos = 4;
    while (pos + 8 <= size) {
        const std::uint32_t tag = getU32(data + pos);
        const std::uint32_t len = getU32(data + pos + 4);
        pos += 8;
        if (len > size - pos) return false;
        if (tag == kTagPreset) return true;
        pos += len;
    }
    return false;
}

bool Vst3KernelHost::decodeBypass(const std::uint8_t* data, std::size_t size) noexcept {
    if (!data || size < 4 || getU32(data) < kStateVersion) return false;
    std::size_t pos = 4;
    while (pos + 8 <= size) {
        const std::uint32_t tag = getU32(data + pos);
        const std::uint32_t len = getU32(data + pos + 4);
        pos += 8;
        if (len > size - pos) return false;
        if (tag == kTagBypass) return len >= 1 && data[pos] != 0;
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
    bool bypassed = false; // a state without the chunk is not bypassed
    const bool presetOnly = isPresetState(data, size);
    bool haveDigiModel = false, haveDigiBank = false;
    std::vector<std::uint8_t> sidBytes;
    std::uint16_t sidSubtune = 0;
    GUI::DigiPanelModel digiModel{};
    std::unique_ptr<GUI::DigiSampleBankBlob> digiBank;
    std::unique_lock<std::mutex> lock(modelMutex_);
    std::size_t pos = 4;
    while (pos + 8 <= size) {
        const std::uint32_t tag = getU32(data + pos);
        const std::uint32_t len = getU32(data + pos + 4);
        pos += 8;
        if (len > size - pos) break; // truncated: keep what was applied
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
            case kTagBypass:
                bypassed = len >= 1 && p[0] != 0;
                break;
            case kTagSidFile:
                if (len > 2 && len - 2 <= kMaxSidFileBytes) {
                    sidSubtune = static_cast<std::uint16_t>(p[0] | (p[1] << 8));
                    sidBytes.assign(p + 2, p + len);
                }
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
    // A preset (patch-only state) changes the patch, nothing else.
    if (presetOnly) return haveRoot;
    publishModelsLocked_(true);
    lock.unlock();
    setBypass(bypassed);
    // The C64 tune saved with the project; a state without one unloads any
    // tune left from before, so a restore is deterministic.
    if (!sidBytes.empty()) {
        if (!loadSidFile(sidBytes.data(), sidBytes.size(), sidSubtune)) unloadSidFile();
    } else if (sidFileSize() > 0 || kernel_->isPsidLoaded()) {
        unloadSidFile();
    }
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

// ── DIGI capture ────────────────────────────────────────────────────────────

bool Vst3KernelHost::armDigiCapture(int slot) {
    if (slot < 0 || slot >= GUI::kDigiActiveSlotCount) return false;
    cancelDigiCapture();
    if (captureBuffer_.size() != GUI::kDigiRecordCaptureMaxFrames)
        captureBuffer_.assign(GUI::kDigiRecordCaptureMaxFrames, 0.0f); // allocated here, never on the audio thread
    captureSlot_ = slot;
    captureFrames_.store(0, std::memory_order_relaxed);
    capturePeak_.store(0.f, std::memory_order_relaxed);
    captureArmed_.store(true, std::memory_order_release);
    return true;
}

void Vst3KernelHost::cancelDigiCapture() noexcept {
    // Store-then-check pairs with captureDigiInput's busy-then-check (both
    // seq_cst): either the audio block sees disarmed, or we see it busy.
    captureArmed_.store(false);
    while (captureBusy_.load()) {
        // An audio block is writing; it finishes within one block. Yield so a
        // preempted audio thread gets the core back.
        std::this_thread::yield();
    }
}

bool Vst3KernelHost::stopDigiCapture(const char* name) {
    const bool wasArmed = captureArmed_.load(std::memory_order_acquire);
    cancelDigiCapture();
    const std::uint32_t frames = captureFrames_.load(std::memory_order_acquire);
    if (!wasArmed || frames == 0) return false;
    // Normalise the take so quiet inputs still use the 4-bit range.
    const float peak = capturePeak_.load(std::memory_order_relaxed);
    if (peak > 1e-4f && peak < 0.99f) {
        const float g = 0.99f / peak;
        for (std::uint32_t i = 0; i < frames; ++i) captureBuffer_[i] *= g;
    }
    return setDigiUserSample(captureSlot_, captureBuffer_.data(), frames, sampleRate_, name ? name : "capture");
}

void Vst3KernelHost::captureDigiInput(const float* const* inputs, int numChannels, int frameCount) noexcept {
    if (!captureArmed_.load(std::memory_order_acquire) || !inputs || numChannels <= 0 || frameCount <= 0) return;
    captureBusy_.store(true);
    if (captureArmed_.load()) {
        const std::uint32_t cap = static_cast<std::uint32_t>(captureBuffer_.size());
        std::uint32_t pos = captureFrames_.load(std::memory_order_relaxed);
        float peak = capturePeak_.load(std::memory_order_relaxed);
        const float scale = 1.0f / static_cast<float>(std::min(numChannels, 2));
        for (int i = 0; i < frameCount && pos < cap; ++i, ++pos) {
            float v = 0.f;
            for (int c = 0; c < std::min(numChannels, 2); ++c)
                if (inputs[c]) v += inputs[c][i];
            v *= scale;
            if (!std::isfinite(v)) v = 0.f;
            peak = std::max(peak, std::fabs(v));
            captureBuffer_[pos] = v;
        }
        capturePeak_.store(peak, std::memory_order_relaxed);
        captureFrames_.store(pos, std::memory_order_release);
    }
    captureBusy_.store(false, std::memory_order_release);
}

Vst3KernelHost::DigiCaptureStatus Vst3KernelHost::digiCaptureStatus() const noexcept {
    DigiCaptureStatus s;
    s.armed = captureArmed_.load(std::memory_order_acquire);
    s.inputActive = captureInputActive_.load(std::memory_order_relaxed);
    s.slot = captureSlot_;
    s.frames = captureFrames_.load(std::memory_order_acquire);
    s.seconds = sampleRate_ > 0.0 ? s.frames / sampleRate_ : 0.0;
    s.peak = capturePeak_.load(std::memory_order_relaxed);
    s.full = !captureBuffer_.empty() && s.frames >= captureBuffer_.size();
    return s;
}

bool Vst3KernelHost::loadSidFile(const void* data, std::size_t size, std::uint16_t subtune) {
    if (!data || size == 0 || size > kMaxSidFileBytes) return false;
    if (!kernel_->loadPsidData(data, size, subtune)) return false;
    std::lock_guard<std::mutex> lock(sidMutex_);
    const auto* b = static_cast<const std::uint8_t*>(data);
    sidFile_.assign(b, b + size);
    sidSubtune_ = subtune;
    return true;
}

void Vst3KernelHost::unloadSidFile() noexcept {
    kernel_->unloadPsid();
    std::lock_guard<std::mutex> lock(sidMutex_);
    sidFile_.clear();
    sidFile_.shrink_to_fit();
    sidSubtune_ = 0;
}

bool Vst3KernelHost::selectSidSubtune(std::uint16_t subtune) {
    std::vector<std::uint8_t> bytes;
    {
        std::lock_guard<std::mutex> lock(sidMutex_);
        if (sidFile_.empty()) return false;
        bytes = sidFile_;
    }
    return loadSidFile(bytes.data(), bytes.size(), subtune);
}

std::uint16_t Vst3KernelHost::sidSubtune() const noexcept {
    std::lock_guard<std::mutex> lock(sidMutex_);
    return sidSubtune_;
}

std::size_t Vst3KernelHost::sidFileSize() const noexcept {
    std::lock_guard<std::mutex> lock(sidMutex_);
    return sidFile_.size();
}

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

bool Vst3KernelHost::c64VicFast() const noexcept { return kernel_->c64VicFast(); }

bool Vst3KernelHost::c64CpuFast() const noexcept { return kernel_->c64CpuFast(); }

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
