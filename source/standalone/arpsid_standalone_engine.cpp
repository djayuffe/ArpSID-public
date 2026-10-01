// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSID standalone app: engine side (see arpsid_standalone_engine.h).

#include "standalone/arpsid_standalone_engine.h"

#include "arpsid/core/sid_runtime_state_root_presentation.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "au3/ArpSIDCanonicalEvents.h"
#include "au3/ArpSIDStateSerializer.h"
#include "factory_patch_params.h"
#include "parameter_ids.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace ArpSID::Standalone {

namespace {

constexpr std::uint32_t kSessionMagic = 0x53534141u; // "AASS" on disk (ArpSID Standalone Session)
constexpr std::uint32_t kSessionVersion = 1u;
constexpr std::size_t kMaxNameBytes = 255;

void putU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
std::uint32_t getU32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

} // namespace

Engine::Engine() : host_(std::make_unique<Vst3KernelHost>()) {
    host_->setup(sampleRate_, 1024);
    host_->loadFactorySlot(0);
}

Engine::~Engine() = default;

void Engine::prepare(double sampleRate, int maxFrames) {
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ? sampleRate : 48000.0;
    host_->setup(sampleRate_, std::max(1, maxFrames));
}

void Engine::process(float* const* outputs, int numOut, const float* const* inputs, int numIn,
                     int frames) noexcept {
    if (frames <= 0 || !outputs) return;
    const auto t0 = std::chrono::steady_clock::now();

    if (rewind_.exchange(false, std::memory_order_acq_rel)) beat_.store(0.0, std::memory_order_relaxed);
    TransportState t{};
    t.sampleRate = sampleRate_;
    t.frameCount = frames;
    t.bpm = bpm_.load(std::memory_order_relaxed);
    t.isPlaying = playing_.load(std::memory_order_relaxed);
    t.playStateKnown = true;
    t.beatPosition = beat_.load(std::memory_order_relaxed);

    // DIGI capture input (only read while a capture is armed).
    host_->setDigiCaptureInputActive(numIn > 0 && inputs);
    if (numIn > 0 && inputs) host_->captureDigiInput(inputs, std::min(numIn, 2), frames);

    float* outs[2] = {outputs[0], numOut > 1 ? outputs[1] : nullptr};
    host_->render(outs, std::min(numOut, 2), frames, nullptr, 0, t);
    for (int c = 2; c < numOut; ++c)
        if (outputs[c]) std::fill(outputs[c], outputs[c] + frames, 0.0f);

    if (t.isPlaying) beat_.store(t.beatPosition + frames / sampleRate_ * t.bpm / 60.0, std::memory_order_relaxed);

    const double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double budget = frames / sampleRate_;
    const float l = static_cast<float>(used / budget);
    // Fast attack, slow release, so short spikes stay visible.
    const float prev = load_.load(std::memory_order_relaxed);
    load_.store(l > prev ? l : prev + (l - prev) * 0.05f, std::memory_order_relaxed);
}

bool Engine::channelAccepted_(std::uint8_t status) const noexcept {
    const int ch = midiChannel_.load(std::memory_order_relaxed);
    return ch == 0 || (status & 0x0F) == ch - 1;
}

void Engine::midiIn(const std::uint8_t* data, std::size_t length) noexcept {
    if (!data || length == 0) return;
    const std::uint8_t status = data[0];
    if (status < 0x80) return; // running status is resolved by the MIDI layer
    if (status >= 0xF0) {
        switch (status) {
            case 0xFA: rewind_.store(true, std::memory_order_release); playing_.store(true); break; // Start
            case 0xFB: playing_.store(true); break;                                                // Continue
            case 0xFC: playing_.store(false); break;                                               // Stop
            default: return;                                                                       // clock, sysex, ...
        }
        lastMidiMs_.store(nowMs(), std::memory_order_relaxed);
        return;
    }
    if (!channelAccepted_(status)) return;
    const std::size_t want = ((status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0) ? 2u : 3u;
    if (length < want) return;
    const int ch = status & 0x0F;
    if ((status & 0xF0) == 0xB0 && data[1] == 0) bankMsb_[ch].store(data[2] & 0x7F, std::memory_order_relaxed);
    if ((status & 0xF0) == 0xC0) {
        const int slot = bankMsb_[ch].load(std::memory_order_relaxed) * 128 + (data[1] & 0x7F);
        if (slot <= kCanonicalFactoryPatchSlotMax) pendingProgram_.store(slot, std::memory_order_release);
        lastMidiMs_.store(nowMs(), std::memory_order_relaxed);
        return;
    }
    host_->injectMidi(data, static_cast<std::uint8_t>(want));
    lastMidiMs_.store(nowMs(), std::memory_order_relaxed);
}

bool Engine::applyPendingProgramChange() {
    const int slot = pendingProgram_.exchange(-1, std::memory_order_acq_rel);
    if (slot < 0) return false;
    selectFactoryPatch(slot);
    return true;
}

void Engine::uiMidi(const std::uint8_t* data, std::size_t length) noexcept {
    if (!data || length == 0 || length > 3 || data[0] < 0x80 || data[0] >= 0xF0) return;
    host_->injectMidi(data, static_cast<std::uint8_t>(length));
}

void Engine::setMidiChannel(int channel) noexcept { midiChannel_.store(std::clamp(channel, 0, 16)); }

void Engine::panic() noexcept {
    for (std::uint8_t ch = 0; ch < 16; ++ch) {
        const std::uint8_t sustainOff[3] = {static_cast<std::uint8_t>(0xB0 | ch), 64, 0};
        const std::uint8_t allNotesOff[3] = {static_cast<std::uint8_t>(0xB0 | ch), 123, 0};
        const std::uint8_t allSoundOff[3] = {static_cast<std::uint8_t>(0xB0 | ch), 120, 0};
        host_->injectMidi(sustainOff, 3);
        host_->injectMidi(allNotesOff, 3);
        host_->injectMidi(allSoundOff, 3);
    }
}

void Engine::setTempo(double bpm) noexcept {
    if (std::isfinite(bpm)) bpm_.store(std::clamp(bpm, 20.0, 300.0), std::memory_order_relaxed);
}

void Engine::setPlaying(bool playing) noexcept {
    if (playing && !playing_.load()) rewind_.store(true, std::memory_order_release);
    playing_.store(playing, std::memory_order_relaxed);
}

void Engine::selectFactoryPatch(int slot) {
    slot = std::clamp(slot, 0, kCanonicalFactoryPatchSlotMax);
    userName_.clear();
    userSlot_ = -1;
    host_->loadFactorySlot(slot);
}

void Engine::loadPatch(const SidStateRootV1& root, const std::string& name) {
    host_->scheduleStateRoot(root);
    userName_ = name.empty() ? std::string("User patch") : name.substr(0, kMaxNameBytes);
    std::array<float, kNumParams> params{};
    exportPersistentPresentationParamsFromStateRoot(root, params.data(), kNumParams);
    userSlot_ = canonicalFactorySlotFromNormalizedBankSlot(params[static_cast<std::size_t>(kParamBankSlot)]);
}

int Engine::currentFactorySlot() const {
    SidStateRootV1 root{};
    host_->currentStateRoot(root);
    std::array<float, kNumParams> params{};
    exportPersistentPresentationParamsFromStateRoot(root, params.data(), kNumParams);
    return canonicalFactorySlotFromNormalizedBankSlot(params[static_cast<std::size_t>(kParamBankSlot)]);
}

bool Engine::isUserPatch() const { return !userName_.empty() && currentFactorySlot() == userSlot_; }

std::string Engine::patchName() const {
    return isUserPatch() ? userName_ : factoryPatchNameForSlot(std::max(0, currentFactorySlot()));
}

void Engine::setUserPatchName(const std::string& name) {
    if (name.empty()) return;
    userName_ = name.substr(0, kMaxNameBytes);
    userSlot_ = currentFactorySlot();
}

std::vector<std::uint8_t> Engine::saveSession() const {
    std::vector<std::uint8_t> out;
    const std::vector<std::uint8_t> state = host_->saveState();
    const std::string name = isUserPatch() ? userName_ : std::string();
    putU32(out, kSessionMagic);
    putU32(out, kSessionVersion);
    putU32(out, static_cast<std::uint32_t>(name.size()));
    out.insert(out.end(), name.begin(), name.end());
    putU32(out, static_cast<std::uint32_t>(state.size()));
    out.insert(out.end(), state.begin(), state.end());
    return out;
}

bool Engine::loadSession(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 16 || getU32(data) != kSessionMagic || getU32(data + 4) < 1) return false;
    std::size_t pos = 8;
    const std::uint32_t nameLen = getU32(data + pos);
    pos += 4;
    if (nameLen > kMaxNameBytes || nameLen > size - pos) return false;
    const std::string name(reinterpret_cast<const char*>(data + pos), nameLen);
    pos += nameLen;
    if (size - pos < 4) return false;
    const std::uint32_t stateLen = getU32(data + pos);
    pos += 4;
    if (stateLen > size - pos) return false;
    if (!host_->loadState(data + pos, stateLen)) return false;
    // The session's own bypass flag is not a standalone concept.
    host_->setBypass(false);
    userName_.clear();
    userSlot_ = -1;
    if (!name.empty()) {
        SidStateRootV1 root{};
        if (Vst3KernelHost::decodeStateRoot(data + pos, stateLen, root)) {
            std::array<float, kNumParams> params{};
            exportPersistentPresentationParamsFromStateRoot(root, params.data(), kNumParams);
            userName_ = name;
            userSlot_ = canonicalFactorySlotFromNormalizedBankSlot(params[static_cast<std::size_t>(kParamBankSlot)]);
        }
    }
    return true;
}

} // namespace ArpSID::Standalone
