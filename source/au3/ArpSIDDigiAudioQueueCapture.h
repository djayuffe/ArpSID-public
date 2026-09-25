#pragma once

#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>

#include "arpsid/gui/digi_record_limits.h"
#include "arpsid/core/realtime_atomic_contract.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

// device-bound CoreAudio input capture backend.
//
// The legacy DIGI REC/MON input path created an AVAudioEngine and tapped its
// inputNode after switching the GLOBAL macOS default input device. That is
// unreliable in a plugin (mutates the user's whole system, races on rebind, can
// be blocked by TCC/sandbox, and cannot prove the selected device is bound).
//
// This backend uses AudioQueueNewInput + kAudioQueueProperty_CurrentDevice to
// bind capture directly to a specific hardware input UID without touching the
// system default. It accepts only explicitly-supported, verified CoreAudio PCM layouts, converts them to mono float,
// and hands them to an ingest callback, and exposes rich stats for honest diagnostics.

namespace ArpSID { namespace Digi {

enum class AQCaptureMode : uint8_t { Monitor, Record };
enum class AQCaptureLifecycle : uint8_t {
    Stopped = 0,
    Starting = 1,
    Running = 2,
    Stopping = 3,
    Draining = 4,
};

// explicit channel-pick policy. AllToMono remains the safe default for
// legacy UI, but the backend no longer hardcodes channels 1/2. GUI can now wire
// Ch3/Ch4/Stereo34 without changing the CoreAudio backend.
enum class AQInputChannelMode : uint8_t {
    // /default safe auto mode for unknown/multi-channel interfaces.
    // It selects the strongest channel for each delivered buffer, avoiding
    // all-channel dilution or phase cancellation when the real microphone is on
    // ch3/ch4/etc. Explicit modes below remain available for UI wiring.
    AutoStrongest,
    AllToMono,
    Ch1, Ch2, Ch12Mono,
    Ch3, Ch4, Ch34Mono
};

struct AQCaptureStats {
    std::atomic<bool> running{false};
    std::atomic<uint8_t> lifecycle{static_cast<uint8_t>(AQCaptureLifecycle::Stopped)};
    std::atomic<bool> callbacksSeen{false};
    std::atomic<uint64_t> callbackCount{0};
    std::atomic<uint64_t> callbackFrames{0};
    std::atomic<uint64_t> droppedFrames{0};
    std::atomic<uint64_t> emptyBuffers{0};
    std::atomic<uint64_t> unsupportedFormatBuffers{0};
    std::atomic<uint64_t> unsupportedChannelBuffers{0};
    std::atomic<float> peak{0.0f};
    std::atomic<float> rms{0.0f};
    std::atomic<OSStatus> lastError{noErr};
    std::atomic<OSStatus> lastEnqueueError{noErr};
    // last observed delivered-buffer format (diagnostic).
    std::atomic<uint32_t> lastFormatFlags{0};
    std::atomic<uint32_t> lastBitsPerChannel{0};
    std::atomic<uint32_t> lastBytesPerFrame{0};
    // hardware-side level meter (independent of our conversion path).
    std::atomic<bool> queueMeterAvailable{false};
    std::atomic<float> queuePeakDB{-160.0f};
    std::atomic<float> queueAvgDB{-160.0f};
    // channel diagnostics: visible proof of which channels the backend
    // requested and, in AutoStrongest mode, which channel was selected for the
    // last converted buffer. UINT32_MAX means no channel selected yet.
    std::atomic<uint32_t> effectiveChannels{0};
    std::atomic<uint32_t> selectedChannelZeroBased{UINT32_MAX};
    std::atomic<bool> channelQueryFailed{false};
};

struct AQCaptureConfig {
    AQCaptureMode mode = AQCaptureMode::Monitor;
    NSString* selectedDeviceUID = nil; // nil/empty => system default input
    double requestedSampleRate = 48000.0;
    uint32_t requestedChannels = 2;
    AQInputChannelMode channelMode = AQInputChannelMode::AutoStrongest;
    uint32_t bufferFrames = 512;
    uint32_t bufferCount = 4;
    uint32_t maxRecordFrames = ArpSID::GUI::kDigiRecordCaptureMaxFrames;
    uint32_t clientGeneration = 0;
};

struct AQCaptureChunk {
    const float* mono = nullptr;
    uint32_t frames = 0;
    double sampleRate = 0.0;
    uint32_t clientGeneration = 0;
};

// AudioQueue-thread callback ABI: a plain noexcept function pointer plus opaque
// context. This prevents std::function type erasure/copy/destruction from
// entering the realtime ingest path.
using AQCaptureIngestFn = void (*)(void* context, const AQCaptureChunk& chunk) noexcept;
using AQCaptureMessageFn = std::function<void(NSString*)>;

class AQCapture final {
public:
    AQCapture();
    ~AQCapture();
    AQCapture(const AQCapture&) = delete;
    AQCapture& operator=(const AQCapture&) = delete;

    bool start(const AQCaptureConfig& cfg,
               AQCaptureIngestFn ingest,
               void* ingestContext,
               AQCaptureMessageFn message);
    void stop(bool immediate = false);

    AQCaptureStats& stats() noexcept { return stats_; }
    const AQCaptureStats& stats() const noexcept { return stats_; }

    // These mutable fields are written under queueMutex_ in start()/stopLocked().
    // The getters are UI/control-thread only (never the AudioQueue callback, which
    // does not take queueMutex_), so locking here cannot stall the RT path. The
    // NSString getters copy-out under lock so a concurrent stop() setting the ivar
    // to nil cannot hand back a dangling/torn object pointer.
    double sampleRate() const noexcept {
        std::lock_guard<std::mutex> lk(queueMutex_);
        return actualSampleRate_;
    }
    uint32_t channels() const noexcept {
        std::lock_guard<std::mutex> lk(queueMutex_);
        return actualChannels_;
    }
    NSString* requestedUID() const noexcept {
        std::lock_guard<std::mutex> lk(queueMutex_);
        return [requestedUID_ copy];
    }
    NSString* actualUID() const noexcept {
        std::lock_guard<std::mutex> lk(queueMutex_);
        return [actualUID_ copy];
    }

    // poll the AudioQueue hardware level meter into stats (call from a
    // UI timer, not the audio callback). Returns true if a meter was available.
    bool pollQueueMeters() noexcept;

private:
    struct AQBufferContext {
        AQCapture* owner = nullptr;
        AudioQueueRef queue = nullptr;
        AudioQueueBufferRef buffer = nullptr;
        std::vector<float>* scratch = nullptr;
        uint32_t index = 0;
    };

    struct AQCallbackContext {
        std::atomic<AQCapture*> owner{nullptr};
        std::atomic<uintptr_t> activeQueuePtr{0};
        std::atomic<bool> stopping{true};
        std::atomic<uint32_t> callbacksInFlight_{0};
    };

    static void inputCallback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer,
                              const AudioTimeStamp* startTime, UInt32 numPackets,
                              const AudioStreamPacketDescription* packetDescs);
    static void propertyListener(void* userData, AudioQueueRef queue, AudioQueuePropertyID propertyID);

    void handleInput(AudioQueueRef queue,
                     AudioQueueBufferRef buffer,
                     AQBufferContext* context) noexcept;
    void stopLocked(bool immediate) noexcept;
    bool convertBufferToMonoFloat(const AudioQueueBufferRef buffer, std::vector<float>& out, uint32_t& framesOut) noexcept;
    void updateMeters(const float* mono, uint32_t frames) noexcept;
    void setError(OSStatus st) noexcept;

    AudioQueueRef queue_ = nullptr;
    std::vector<AudioQueueBufferRef> buffers_;
    std::vector<AQBufferContext> bufferContexts_;
    AudioStreamBasicDescription asbd_{};
    AQCaptureConfig cfg_{};
    AQCaptureStats stats_{};
    AQCaptureIngestFn ingest_ = nullptr;
    void* ingestContext_ = nullptr;
    AQCaptureMessageFn message_;
    // One mono scratch per AudioQueue buffer. Each AudioQueueBuffer's mUserData
    // points directly at its stable bufferContexts_ entry, so callbacks perform
    // no vector scan and overlapping callbacks never share storage.
    std::vector<std::vector<float>> perBufferScratch_;
    uint32_t scratchCapacity_ = 0;
    // guards non-callback queue lifetime operations such as stop() and
    // pollQueueMeters(). The AudioQueue callback never takes this mutex.
    mutable std::mutex queueMutex_;
    std::atomic<bool> stopping_{false};
    // Defensive callback lifetime gate. AudioQueue callbacks receive only this
    // context, never a raw AQCapture*; stop() invalidates owner/queue and waits
    // for callbacks already inside the static ABI trampoline before clearing
    // callback-addressable vectors.
    AQCallbackContext callbackContext_{};
    // sticky AutoStrongest selection prevents low-level channel chatter
    // across buffers. The backend changes channels only when the new channel is
    // materially stronger, so a quiet multi-input interface does not click/hop
    // between adjacent noise floors.
    std::atomic<uint32_t> autoStrongestChannel_{UINT32_MAX};
    std::atomic<double> autoStrongestEnergy_{0.0};
    // Hard lifetime gate for member code after the ABI trampoline has resolved
    // the context owner. Mirrored into callbackContext_ so the static callback
    // can reject stale queues before touching AQCapture.
    std::atomic<uintptr_t> activeQueuePtr_{0};
    // The callback cannot take queueMutex_. Publish the negotiated rate before
    // AudioQueueStart so even an immediate first callback observes valid data.
    std::atomic<double> callbackSampleRate_{0.0};
    double actualSampleRate_ = 0.0;
    uint32_t actualChannels_ = 0;
    NSString* requestedUID_ = nil;
    NSString* actualUID_ = nil;
};

NSString* AudioQueueStatusToNSString(OSStatus st);

} } // namespace ArpSID::Digi
