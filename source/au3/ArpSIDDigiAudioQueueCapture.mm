#import "ArpSIDDigiAudioQueueCapture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <chrono>

namespace ArpSID { namespace Digi {

static inline float clampFloat(float v) noexcept {
    if (!std::isfinite(v)) return 0.0f;
    return std::clamp(v, -1.0f, 1.0f);
}

// total input channel count of a CoreAudio device UID (0 if unknown).
// Used so a multi-input interface (e.g. mic on channel 3) is captured by
// requesting ALL its input channels and summing to mono, instead of only ch 1/2.
static AudioObjectID ArpSIDDigiDeviceForUID(NSString* uid) noexcept {
    if (uid.length == 0) return kAudioObjectUnknown;
    CFStringRef cf = (__bridge CFStringRef)uid;
    AudioObjectID translated = kAudioObjectUnknown;
    UInt32 sz = sizeof(translated);
    AudioObjectPropertyAddress tr{ kAudioHardwarePropertyTranslateUIDToDevice,
                                   kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &tr, sizeof(cf), &cf, &sz, &translated) != noErr)
        return kAudioObjectUnknown;
    return translated;
}

static NSString* ArpSIDDigiUIDForDevice(AudioObjectID dev) noexcept {
    if (dev == kAudioObjectUnknown) return nil;
    CFStringRef uid = nullptr;
    UInt32 sz = sizeof(uid);
    AudioObjectPropertyAddress addr{ kAudioDevicePropertyDeviceUID,
                                     kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    if (AudioObjectGetPropertyData(dev, &addr, 0, nullptr, &sz, &uid) != noErr || !uid) return nil;
    return CFBridgingRelease(uid);
}

static NSString* ArpSIDDigiDefaultInputDeviceUID() noexcept {
    AudioObjectID dev = kAudioObjectUnknown;
    UInt32 sz = sizeof(dev);
    AudioObjectPropertyAddress addr{ kAudioHardwarePropertyDefaultInputDevice,
                                     kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &sz, &dev) != noErr) return nil;
    return ArpSIDDigiUIDForDevice(dev);
}

static uint32_t ArpSIDDigiQueryInputChannelCount(NSString* uid) noexcept {
    AudioObjectID dev = ArpSIDDigiDeviceForUID(uid);
    if (dev == kAudioObjectUnknown) return 0u;
    AudioObjectPropertyAddress cfgAddr{ kAudioDevicePropertyStreamConfiguration,
                                        kAudioDevicePropertyScopeInput, kAudioObjectPropertyElementMain };
    UInt32 dataSize = 0;
    if (AudioObjectGetPropertyDataSize(dev, &cfgAddr, 0, nullptr, &dataSize) != noErr || dataSize == 0) return 0u;
    std::vector<uint8_t> storage(dataSize, 0);
    AudioBufferList* abl = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(dev, &cfgAddr, 0, nullptr, &dataSize, abl) != noErr) return 0u;
    uint32_t channels = 0u;
    for (UInt32 i = 0; i < abl->mNumberBuffers; ++i) channels += abl->mBuffers[i].mNumberChannels;
    return channels;
}

static uint32_t ArpSIDDigiAQBytesPerSample(const AudioStreamBasicDescription& a) noexcept {
    const bool isFloat = (a.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
    const bool isSigned = (a.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0;
    if (isFloat && a.mBitsPerChannel == 32) return 4u;
    if (isSigned && a.mBitsPerChannel == 16) return 2u;
    if (isSigned && a.mBitsPerChannel == 32) return 4u;
    return 0u;
}

// accept only tightly packed interleaved linear PCM that the converter
// reads exactly. If CoreAudio negotiates padding, endian/high-aligned or planar
// storage, fail closed before callbacks instead of mis-addressing bytes.
static bool ArpSIDDigiAQFormatSupported(const AudioStreamBasicDescription& a) noexcept {
    if (a.mFormatID != kAudioFormatLinearPCM) return false;
    if (!std::isfinite(a.mSampleRate) || a.mSampleRate <= 0.0) return false;
    if (a.mChannelsPerFrame == 0u || a.mChannelsPerFrame > 8u) return false;
    if (a.mFramesPerPacket != 1u) return false;
    if (a.mFormatFlags & kAudioFormatFlagIsNonInterleaved) return false;
    if (a.mFormatFlags & kAudioFormatFlagIsBigEndian) return false;
    if (a.mFormatFlags & kAudioFormatFlagIsAlignedHigh) return false;
    const uint32_t bps = ArpSIDDigiAQBytesPerSample(a);
    if (bps == 0u) return false;
    const uint32_t expected = a.mChannelsPerFrame * bps;
    if (a.mBytesPerFrame != expected) return false;
    if (a.mBytesPerPacket != expected) return false;
    return true;
}

static NSString* ArpSIDDigiAQStartHint(OSStatus st) noexcept {
    if (st == kAudioQueueErr_Permissions) {
        return @" — grant Microphone/Audio Recording permission to the HOST app, not only ArpSID.component";
    }
    if (st == kAudioQueueErr_InvalidDevice) {
        return @" — selected CoreAudio input UID is gone; reselect the input device";
    }
    if (st == kAudioQueueErr_CannotStart) {
        return @" — CoreAudio could not start the selected input; check host permission, sample rate, and device ownership";
    }
    return @"";
}

static uint32_t ArpSIDDigiAQSelectChannels(AQInputChannelMode mode, uint32_t available, uint8_t out[8]) noexcept {
    if (!out || available == 0u) return 0u;
    const auto add = [&](uint32_t oneBased, uint32_t& n) {
        if (oneBased >= 1u && oneBased <= available && n < 8u) out[n++] = static_cast<uint8_t>(oneBased - 1u);
    };
    uint32_t n = 0u;
    switch (mode) {
        case AQInputChannelMode::AutoStrongest:
            for (uint32_t i = 0; i < available && i < 8u; ++i) out[n++] = static_cast<uint8_t>(i);
            break;
        case AQInputChannelMode::Ch1: add(1, n); break;
        case AQInputChannelMode::Ch2: add(2, n); break;
        case AQInputChannelMode::Ch12Mono: add(1, n); add(2, n); break;
        case AQInputChannelMode::Ch3: add(3, n); break;
        case AQInputChannelMode::Ch4: add(4, n); break;
        case AQInputChannelMode::Ch34Mono: add(3, n); add(4, n); break;
        case AQInputChannelMode::AllToMono:
        default:
            for (uint32_t i = 0; i < available && i < 8u; ++i) out[n++] = static_cast<uint8_t>(i);
            break;
    }
    return n;
}

NSString* AudioQueueStatusToNSString(OSStatus st) {
    switch (st) {
        case noErr: return @"noErr";
        case kAudioQueueErr_InvalidBuffer: return @"kAudioQueueErr_InvalidBuffer";
        case kAudioQueueErr_BufferEmpty: return @"kAudioQueueErr_BufferEmpty";
        case kAudioQueueErr_DisposalPending: return @"kAudioQueueErr_DisposalPending";
        case kAudioQueueErr_InvalidProperty: return @"kAudioQueueErr_InvalidProperty";
        case kAudioQueueErr_InvalidPropertySize: return @"kAudioQueueErr_InvalidPropertySize";
        case kAudioQueueErr_InvalidParameter: return @"kAudioQueueErr_InvalidParameter";
        case kAudioQueueErr_CannotStart: return @"kAudioQueueErr_CannotStart";
        case kAudioQueueErr_InvalidDevice: return @"kAudioQueueErr_InvalidDevice";
        case kAudioQueueErr_BufferInQueue: return @"kAudioQueueErr_BufferInQueue";
        case kAudioQueueErr_InvalidRunState: return @"kAudioQueueErr_InvalidRunState";
        case kAudioQueueErr_InvalidQueueType: return @"kAudioQueueErr_InvalidQueueType";
        case kAudioQueueErr_Permissions: return @"kAudioQueueErr_Permissions";
        case kAudioQueueErr_InvalidPropertyValue: return @"kAudioQueueErr_InvalidPropertyValue";
        case kAudioQueueErr_PrimeTimedOut: return @"kAudioQueueErr_PrimeTimedOut";
        case kAudioQueueErr_CodecNotFound: return @"kAudioQueueErr_CodecNotFound";
        case kAudioQueueErr_InvalidCodecAccess: return @"kAudioQueueErr_InvalidCodecAccess";
        case kAudioQueueErr_QueueInvalidated: return @"kAudioQueueErr_QueueInvalidated";
        case kAudioQueueErr_RecordUnderrun: return @"kAudioQueueErr_RecordUnderrun";
        case kAudioQueueErr_EnqueueDuringReset: return @"kAudioQueueErr_EnqueueDuringReset";
        case kAudioQueueErr_InvalidOfflineMode: return @"kAudioQueueErr_InvalidOfflineMode";
        case kAudioFormatUnsupportedDataFormatError: return @"kAudioFormatUnsupportedDataFormatError";
        default: {
            char fourcc[5] = {0,0,0,0,0};
            fourcc[0] = (char)((st >> 24) & 0xff);
            fourcc[1] = (char)((st >> 16) & 0xff);
            fourcc[2] = (char)((st >> 8) & 0xff);
            fourcc[3] = (char)(st & 0xff);
            bool printable = true;
            for (int i = 0; i < 4; ++i) if (fourcc[i] < 32 || fourcc[i] > 126) printable = false;
            if (printable) return [NSString stringWithFormat:@"OSStatus %d ('%s')", (int)st, fourcc];
            return [NSString stringWithFormat:@"OSStatus %d", (int)st];
        }
    }
}

AQCapture::AQCapture() = default;
AQCapture::~AQCapture() { stop(true); }

void AQCapture::setError(OSStatus st) noexcept { stats_.lastError.store(st, std::memory_order_release); }

bool AQCapture::start(const AQCaptureConfig& cfg,
                      AQCaptureIngestFn ingest,
                      void* ingestContext,
                      AQCaptureMessageFn message) {
    // P1-2: never invoke the user message callback while holding queueMutex_.
    // Messages are collected under the lock and flushed only after it is released.
    // Declaration order matters here: deferredMessages, then the flusher, then the
    // lock. Destruction runs in reverse, so the lock unlocks BEFORE the flusher
    // delivers — a callback body that re-enters start()/stop()/a getter can no
    // longer deadlock on queueMutex_.
    std::vector<NSString*> deferredMessages;
    struct DeferredFlush {
        AQCapture* self;
        std::vector<NSString*>* msgs;
        ~DeferredFlush() {
            if (!self->message_) return;
            for (NSString* m : *msgs) self->message_(m);
        }
    } deferredFlush{this, &deferredMessages};
    std::unique_lock<std::mutex> lk(queueMutex_);
    stopLocked(true);
    if (callbackContext_.callbacksInFlight_.load(std::memory_order_acquire) != 0u) {
        deferredMessages.push_back(@"DIGI AQ START BLOCKED — previous callback still in flight");
        setError(kAudioQueueErr_DisposalPending);
        return false;
    }
    ingest_ = ingest;
    ingestContext_ = ingestContext;
    message_ = std::move(message);

    cfg_ = cfg;
    requestedUID_ = cfg.selectedDeviceUID ? [cfg.selectedDeviceUID copy] : nil;
    actualUID_ = nil;
    stopping_.store(false, std::memory_order_release);

    stats_.running.store(false, std::memory_order_release);
    stats_.lifecycle.store(static_cast<uint8_t>(AQCaptureLifecycle::Starting),
                           std::memory_order_release);
    stats_.callbacksSeen.store(false, std::memory_order_release);
    stats_.callbackCount.store(0, std::memory_order_release);
    stats_.callbackFrames.store(0, std::memory_order_release);
    stats_.droppedFrames.store(0, std::memory_order_release);
    stats_.emptyBuffers.store(0, std::memory_order_release);
    stats_.unsupportedFormatBuffers.store(0, std::memory_order_release);
    stats_.unsupportedChannelBuffers.store(0, std::memory_order_release);
    stats_.peak.store(0.0f, std::memory_order_release);
    stats_.rms.store(0.0f, std::memory_order_release);
    stats_.lastError.store(noErr, std::memory_order_release);
    stats_.lastEnqueueError.store(noErr, std::memory_order_release);
    stats_.lastFormatFlags.store(0, std::memory_order_release);
    stats_.lastBitsPerChannel.store(0, std::memory_order_release);
    stats_.lastBytesPerFrame.store(0, std::memory_order_release);
    stats_.queueMeterAvailable.store(false, std::memory_order_release);
    stats_.queuePeakDB.store(-160.0f, std::memory_order_release);
    stats_.queueAvgDB.store(-160.0f, std::memory_order_release);
    stats_.effectiveChannels.store(0u, std::memory_order_release);
    stats_.selectedChannelZeroBased.store(UINT32_MAX, std::memory_order_release);
    stats_.channelQueryFailed.store(false, std::memory_order_release);
    callbackContext_.owner.store(this, std::memory_order_release);
    callbackContext_.stopping.store(false, std::memory_order_release);
    callbackContext_.activeQueuePtr.store(0u, std::memory_order_release);
    callbackContext_.callbacksInFlight_.store(0u, std::memory_order_release);
    activeQueuePtr_.store(0u, std::memory_order_release);
    callbackSampleRate_.store(0.0, std::memory_order_release);
    autoStrongestChannel_.store(UINT32_MAX, std::memory_order_release);
    autoStrongestEnergy_.store(0.0, std::memory_order_release);

    const double sr = (std::isfinite(cfg.requestedSampleRate) && cfg.requestedSampleRate > 0.0) ? cfg.requestedSampleRate : 48000.0;
    // probe channel count for both explicit UID and System Default Input.
    // only did this for explicit UID, so default multi-channel interfaces
    // still captured only ch1/2 and could read zero while signal was on ch3/4.
    uint32_t ch = std::clamp<uint32_t>(cfg.requestedChannels, 1u, 8u);
    NSString* channelProbeUID = requestedUID_.length ? requestedUID_ : ArpSIDDigiDefaultInputDeviceUID();
    const uint32_t devCh = ArpSIDDigiQueryInputChannelCount(channelProbeUID);
    if (devCh > 0u) ch = std::clamp<uint32_t>(devCh, 1u, 8u);
    else {
        stats_.channelQueryFailed.store(true, std::memory_order_release);
        deferredMessages.push_back(@"DIGI AQ CHANNEL QUERY FAILED — using requested channel count");
    }
    stats_.effectiveChannels.store(ch, std::memory_order_release);

    std::memset(&asbd_, 0, sizeof(asbd_));
    asbd_.mSampleRate = sr;
    asbd_.mFormatID = kAudioFormatLinearPCM;
    asbd_.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    asbd_.mBitsPerChannel = 32;
    asbd_.mChannelsPerFrame = ch;
    asbd_.mFramesPerPacket = 1;
    asbd_.mBytesPerFrame = ch * sizeof(float);
    asbd_.mBytesPerPacket = asbd_.mBytesPerFrame;

    OSStatus st = AudioQueueNewInput(&asbd_, &AQCapture::inputCallback, &callbackContext_, nullptr, nullptr, 0, &queue_);
    if (st != noErr || !queue_) {
        setError(st);
        deferredMessages.push_back([NSString stringWithFormat:@"DIGI AQ NEW INPUT FAILED: %@%@", AudioQueueStatusToNSString(st), ArpSIDDigiAQStartHint(st)]);
        stopLocked(true);
        return false;
    }

    // Bind to selected hardware UID. This is the core fix — no default switching.
    if (requestedUID_.length > 0) {
        CFStringRef uid = (__bridge CFStringRef)requestedUID_;
        st = AudioQueueSetProperty(queue_, kAudioQueueProperty_CurrentDevice, &uid, sizeof(uid));
        if (st != noErr) {
            setError(st);
            deferredMessages.push_back([NSString stringWithFormat:@"DIGI AQ DEVICE BIND FAILED %@: %@%@", requestedUID_, AudioQueueStatusToNSString(st), ArpSIDDigiAQStartHint(st)]);
            stopLocked(true);
            return false;
        }
    }

    // Read back actual device UID to prove the queue is bound.
    // kAudioQueueProperty_CurrentDevice returns a +1 (owned) CFStringRef; hand it
    // to ARC with CFBridgingRelease (correct ownership transfer, no over-release).
    CFStringRef actual = nullptr;
    UInt32 actualSize = sizeof(actual);
    st = AudioQueueGetProperty(queue_, kAudioQueueProperty_CurrentDevice, &actual, &actualSize);
    if (st == noErr && actual) { actualUID_ = CFBridgingRelease(actual); }

    // fail CLOSED if the bound device is not the requested one. Warning
    // alone let REC/MON silently capture the wrong source ("armed but zero/wrong").
    if (requestedUID_.length > 0) {
        if (actualUID_.length == 0) {
            setError(kAudioQueueErr_InvalidDevice);
            deferredMessages.push_back(@"DIGI AQ DEVICE VERIFY FAILED — no actual device UID returned");
            stopLocked(true);
            return false;
        }
        if (![requestedUID_ isEqualToString:actualUID_]) {
            setError(kAudioQueueErr_InvalidDevice);
            deferredMessages.push_back([NSString stringWithFormat:@"DIGI AQ DEVICE MISMATCH — requested=%@ actual=%@", requestedUID_, actualUID_]);
            stopLocked(true);
            return false;
        }
    }

    // read back the actually-negotiated stream description rather than
    // assuming our requested 48 kHz/2ch float was honored.
    {
        AudioStreamBasicDescription neg{};
        UInt32 negSize = sizeof(neg);
        OSStatus negSt = AudioQueueGetProperty(queue_, kAudioQueueProperty_StreamDescription, &neg, &negSize);
        if (negSt == noErr && neg.mFormatID == kAudioFormatLinearPCM && neg.mChannelsPerFrame > 0 && neg.mSampleRate > 0.0) {
            asbd_ = neg;
        }
    }

    // fail CLOSED if the negotiated format is one we cannot convert
    // (non-interleaved / big-endian / unsupported bit depth), instead of starting
    // the queue and then rejecting every buffer (which presented as silent zero).
    if (!ArpSIDDigiAQFormatSupported(asbd_)) {
        setError(kAudioFormatUnsupportedDataFormatError);
        deferredMessages.push_back([NSString stringWithFormat:@"DIGI AQ UNSUPPORTED STREAM FORMAT — %u-bit flags=0x%x %uch %@",
                                                          (unsigned)asbd_.mBitsPerChannel, (unsigned)asbd_.mFormatFlags,
                                                          (unsigned)asbd_.mChannelsPerFrame,
                                                          (asbd_.mFormatFlags & kAudioFormatFlagIsNonInterleaved) ? @"non-interleaved" : @"interleaved"]);
        stopLocked(true);
        return false;
    }

    // enable the AudioQueue hardware level meter so diagnostics can tell
    // "device has signal but our conversion is broken" from "device is silent".
    {
        UInt32 enableMeter = 1;
        OSStatus mSt = AudioQueueSetProperty(queue_, kAudioQueueProperty_EnableLevelMetering, &enableMeter, sizeof(enableMeter));
        stats_.queueMeterAvailable.store(mSt == noErr, std::memory_order_release);
    }

    {
        OSStatus listenSt = AudioQueueAddPropertyListener(queue_, kAudioQueueProperty_IsRunning, &AQCapture::propertyListener, &callbackContext_);
        if (listenSt != noErr) setError(listenSt); // non-fatal; running-state accuracy degrades
    }

    UInt32 bufferFrames = std::clamp<uint32_t>(cfg.bufferFrames, 128u, 4096u);
    UInt32 bufferBytes = bufferFrames * std::max<UInt32>(1u, asbd_.mBytesPerFrame);
    UInt32 bufferCount = std::clamp<uint32_t>(cfg.bufferCount, 3u, 8u);

    // one preallocated mono scratch PER AudioQueue buffer (RT-safe; no
    // shared-scratch race). Sized to bufferFrames + margin; never resized later.
    scratchCapacity_ = bufferFrames + 64u;
    perBufferScratch_.assign(bufferCount, std::vector<float>(scratchCapacity_, 0.0f));
    bufferContexts_.assign(bufferCount, AQBufferContext{});

    buffers_.clear();
    buffers_.reserve(bufferCount);
    for (UInt32 i = 0; i < bufferCount; ++i) {
        AudioQueueBufferRef b = nullptr;
        st = AudioQueueAllocateBuffer(queue_, bufferBytes, &b);
        if (st != noErr || !b) {
            setError(st);
            deferredMessages.push_back([NSString stringWithFormat:@"DIGI AQ ALLOC BUFFER FAILED: %@", AudioQueueStatusToNSString(st)]);
            stopLocked(true);
            return false;
        }
        buffers_.push_back(b);
        AQBufferContext& context = bufferContexts_[i];
        context.owner = this;
        context.queue = queue_;
        context.buffer = b;
        context.scratch = &perBufferScratch_[i];
        context.index = i;
        b->mUserData = &context;
        st = AudioQueueEnqueueBuffer(queue_, b, 0, nullptr);
        if (st != noErr) {
            stats_.lastEnqueueError.store(st, std::memory_order_release);
            setError(st);
            deferredMessages.push_back([NSString stringWithFormat:@"DIGI AQ ENQUEUE FAILED: %@", AudioQueueStatusToNSString(st)]);
            stopLocked(true);
            return false;
        }
    }

    actualSampleRate_ = asbd_.mSampleRate;
    actualChannels_ = asbd_.mChannelsPerFrame;
    callbackSampleRate_.store(actualSampleRate_, std::memory_order_release);

    // publish the exact queue pointer only after all buffers exist and
    // before start; callbacks for any other queue pointer are stale and must not
    // touch member-owned vectors.
    const uintptr_t activeQueue = reinterpret_cast<uintptr_t>(queue_);
    activeQueuePtr_.store(activeQueue, std::memory_order_release);
    callbackContext_.activeQueuePtr.store(activeQueue, std::memory_order_release);

    st = AudioQueueStart(queue_, nullptr);
    if (st != noErr) {
        activeQueuePtr_.store(0u, std::memory_order_release);
        callbackContext_.activeQueuePtr.store(0u, std::memory_order_release);
        setError(st);
        deferredMessages.push_back([NSString stringWithFormat:@"DIGI AQ START FAILED: %@%@", AudioQueueStatusToNSString(st), ArpSIDDigiAQStartHint(st)]);
        stopLocked(true);
        return false;
    }

    stats_.running.store(true, std::memory_order_release);
    stats_.lifecycle.store(static_cast<uint8_t>(AQCaptureLifecycle::Running),
                           std::memory_order_release);

    {
        NSString* dev = actualUID_.length ? actualUID_ : (requestedUID_.length ? requestedUID_ : @"system default input");
        deferredMessages.push_back([NSString stringWithFormat:@"DIGI AQ INPUT STARTED %@ %.0f Hz %u ch", dev, actualSampleRate_, (unsigned)actualChannels_]);
    }
    return true;
}

void AQCapture::stop(bool immediate) {
    std::lock_guard<std::mutex> lk(queueMutex_);
    stopLocked(immediate);
}

void AQCapture::stopLocked(bool immediate) noexcept {
    stats_.lifecycle.store(static_cast<uint8_t>(AQCaptureLifecycle::Stopping),
                           std::memory_order_release);
    stats_.running.store(false, std::memory_order_release);
    stopping_.store(true, std::memory_order_release);
    // invalidate callback ownership before stopping/disposal. Late callbacks
    // can still enter, but they will exit before reading buffers_/scratch.
    activeQueuePtr_.store(0u, std::memory_order_release);
    callbackContext_.activeQueuePtr.store(0u, std::memory_order_release);
    callbackContext_.stopping.store(true, std::memory_order_release);
    callbackContext_.owner.store(nullptr, std::memory_order_release);
    callbackSampleRate_.store(0.0, std::memory_order_release);
    if (queue_) {
        AudioQueueRemovePropertyListener(queue_, kAudioQueueProperty_IsRunning, &AQCapture::propertyListener, &callbackContext_);
        // this backend is INPUT-only. Always stop synchronously/immediately
        // (inImmediate = TRUE) so stop is deterministic and the capture buffer is
        // safe to read right after; the "drain queued playback" semantics of
        // inImmediate=FALSE are meaningless for recording and only add latency.
        (void)immediate;
        AudioQueueStop(queue_, true);
        AudioQueueDispose(queue_, true);
        queue_ = nullptr;
    }
    stats_.lifecycle.store(static_cast<uint8_t>(AQCaptureLifecycle::Draining),
                           std::memory_order_release);
    // do not clear callback-owned vectors until any callback that had
    // already entered handleInput() has left. This is a defensive belt-and-braces
    // guard around AudioQueueDispose(TRUE)'s synchronous contract.
    for (uint32_t spin = 0; callbackContext_.callbacksInFlight_.load(std::memory_order_acquire) != 0u && spin < 200u; ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool callbacksDrained = callbackContext_.callbacksInFlight_.load(std::memory_order_acquire) == 0u;
    if (callbacksDrained) {
        ingest_ = nullptr;
        ingestContext_ = nullptr;
        buffers_.clear();
        bufferContexts_.clear();
        perBufferScratch_.clear();
        scratchCapacity_ = 0;
        stats_.lifecycle.store(static_cast<uint8_t>(AQCaptureLifecycle::Stopped),
                               std::memory_order_release);
    } else {
        // Do not free callback-addressable vectors while a callback is still inside
        // handleInput(). This should not happen after Dispose(TRUE), but fail safe.
        setError(kAudioQueueErr_DisposalPending);
    }
    actualSampleRate_ = 0.0;
    actualChannels_ = 0;
    requestedUID_ = nil;
    actualUID_ = nil;
}

void AQCapture::propertyListener(void* userData, AudioQueueRef queue, AudioQueuePropertyID propertyID) {
    auto* ctx = static_cast<AQCallbackContext*>(userData);
    if (!ctx || propertyID != kAudioQueueProperty_IsRunning) return;
    ctx->callbacksInFlight_.fetch_add(1u, std::memory_order_acq_rel);
    struct InFlightGuard { AQCallbackContext* c; ~InFlightGuard(){ c->callbacksInFlight_.fetch_sub(1u, std::memory_order_acq_rel); } } guard{ctx};
    if (ctx->stopping.load(std::memory_order_acquire)) return;
    if (ctx->activeQueuePtr.load(std::memory_order_acquire) != reinterpret_cast<uintptr_t>(queue)) return;
    AQCapture* self = ctx->owner.load(std::memory_order_acquire);
    if (!self) return;
    UInt32 isRunning = 0;
    UInt32 size = sizeof(isRunning);
    OSStatus st = AudioQueueGetProperty(queue, kAudioQueueProperty_IsRunning, &isRunning, &size);
    if (st == noErr) self->stats_.running.store(isRunning != 0, std::memory_order_release);
    else self->setError(st);
}

void AQCapture::inputCallback(void* userData, AudioQueueRef queue, AudioQueueBufferRef buffer,
                              const AudioTimeStamp* /*startTime*/, UInt32 /*numPackets*/,
                              const AudioStreamPacketDescription* /*packetDescs*/) {
    auto* ctx = static_cast<AQCallbackContext*>(userData);
    if (!ctx) return;
    ctx->callbacksInFlight_.fetch_add(1u, std::memory_order_acq_rel);
    struct InFlightGuard { AQCallbackContext* c; ~InFlightGuard(){ c->callbacksInFlight_.fetch_sub(1u, std::memory_order_acq_rel); } } guard{ctx};
    if (ctx->stopping.load(std::memory_order_acquire)) return;
    if (ctx->activeQueuePtr.load(std::memory_order_acquire) != reinterpret_cast<uintptr_t>(queue)) return;
    AQCapture* self = ctx->owner.load(std::memory_order_acquire);
    if (!self) return;
    AQBufferContext* context =
        buffer ? static_cast<AQBufferContext*>(buffer->mUserData) : nullptr;
    self->handleInput(queue, buffer, context);
}

void AQCapture::handleInput(AudioQueueRef queue,
                            AudioQueueBufferRef buffer,
                            AQBufferContext* context) noexcept {
    // The static AudioQueue trampoline already accounted for in-flight
    // callback lifetime in callbackContext_.callbacksInFlight_ before resolving
    // owner. Do not take locks or mutate ownership counters here.
    if (!queue || !buffer) return;
    if (stopping_.load(std::memory_order_acquire)) return;
    if (activeQueuePtr_.load(std::memory_order_acquire) != reinterpret_cast<uintptr_t>(queue)) return;

    stats_.callbacksSeen.store(true, std::memory_order_release);
    stats_.callbackCount.fetch_add(1, std::memory_order_acq_rel);

    if (!context ||
        context->owner != this ||
        context->queue != queue ||
        context->buffer != buffer ||
        !context->scratch) {
        // unknown buffer means stale callback, foreign queue, or memory
        // corruption. Never re-enqueue an unknown buffer into a queue pointer that
        // may already belong to an old/disposed queue; fail closed instead.
        stats_.droppedFrames.fetch_add(1, std::memory_order_acq_rel);
        setError(kAudioQueueErr_InvalidBuffer);
        return;
    }
    std::vector<float>* scratch = context->scratch;

    uint32_t frames = 0;
    bool conversionOk = convertBufferToMonoFloat(buffer, *scratch, frames);
    if (!conversionOk || frames == 0) {
        // Empty buffers are benign, but an invalid/unsupported route-change buffer
        // must fail closed. Re-enqueuing it forever creates a silent bad-callback
        // loop and hides the real fault from REC/MON diagnostics.
        const OSStatus err = stats_.lastError.load(std::memory_order_acquire);
        if (err == kAudioFormatUnsupportedDataFormatError ||
            err == kAudioQueueErr_InvalidBuffer ||
            err == kAudioQueueErr_InvalidParameter) {
            stopping_.store(true, std::memory_order_release);
            return;
        }
        stats_.emptyBuffers.fetch_add(1, std::memory_order_acq_rel);
    } else {
        stats_.callbackFrames.fetch_add(frames, std::memory_order_acq_rel);
        updateMeters(scratch->data(), frames);
        if (ingest_) {
            AQCaptureChunk chunk;
            chunk.mono = scratch->data();
            chunk.frames = frames;
            const double publishedRate =
                callbackSampleRate_.load(std::memory_order_acquire);
            chunk.sampleRate = publishedRate > 0.0 ? publishedRate : asbd_.mSampleRate;
            chunk.clientGeneration = cfg_.clientGeneration;
            ingest_(ingestContext_, chunk);
        }
    }

    if (!stopping_.load(std::memory_order_acquire)) {
        OSStatus st = AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
        if (st != noErr) { stats_.lastEnqueueError.store(st, std::memory_order_release); setError(st); }
    }
}

bool AQCapture::convertBufferToMonoFloat(const AudioQueueBufferRef buffer, std::vector<float>& out, uint32_t& framesOut) noexcept {
    framesOut = 0;
    if (!buffer || !buffer->mAudioData || buffer->mAudioDataByteSize == 0) return false;

    if (!ArpSIDDigiAQFormatSupported(asbd_)) {
        stats_.unsupportedFormatBuffers.fetch_add(1, std::memory_order_acq_rel);
        setError(kAudioFormatUnsupportedDataFormatError);
        return false;
    }

    const uint32_t channels = asbd_.mChannelsPerFrame;
    const uint32_t bytesPerFrame = asbd_.mBytesPerFrame;
    const uint32_t bytesPerSample = ArpSIDDigiAQBytesPerSample(asbd_);
    if ((buffer->mAudioDataByteSize % bytesPerFrame) != 0u) {
        stats_.droppedFrames.fetch_add(1, std::memory_order_acq_rel);
        setError(kAudioQueueErr_InvalidBuffer);
        return false;
    }
    const uint32_t frames = buffer->mAudioDataByteSize / bytesPerFrame;
    if (frames == 0) return false;

    stats_.lastFormatFlags.store(asbd_.mFormatFlags, std::memory_order_release);
    stats_.lastBitsPerChannel.store(asbd_.mBitsPerChannel, std::memory_order_release);
    stats_.lastBytesPerFrame.store(asbd_.mBytesPerFrame, std::memory_order_release);

    if (frames > out.size()) {
        stats_.droppedFrames.fetch_add(frames, std::memory_order_acq_rel);
        setError(kAudioQueueErr_InvalidBuffer);
        return false;
    }

    uint8_t selected[8] = {};
    const uint32_t selectedCount = ArpSIDDigiAQSelectChannels(cfg_.channelMode, channels, selected);
    if (selectedCount == 0u) {
        stats_.unsupportedChannelBuffers.fetch_add(1, std::memory_order_acq_rel);
        setError(kAudioQueueErr_InvalidParameter);
        return false;
    }

    std::fill(out.begin(), out.begin() + frames, 0.0f);
    const bool isFloat = (asbd_.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
    const bool isSignedInt = (asbd_.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0;
    const uint8_t* raw = static_cast<const uint8_t*>(buffer->mAudioData);

    const auto readSample = [&](const uint8_t* sample) noexcept -> float {
        if (isFloat && asbd_.mBitsPerChannel == 32) {
            float v = 0.0f;
            std::memcpy(&v, sample, sizeof(v));
            return clampFloat(v);
        }
        if (isSignedInt && asbd_.mBitsPerChannel == 16) {
            int16_t v = 0;
            std::memcpy(&v, sample, sizeof(v));
            return std::clamp(static_cast<float>(v) / 32768.0f, -1.0f, 1.0f);
        }
        if (isSignedInt && asbd_.mBitsPerChannel == 32) {
            int32_t v = 0;
            std::memcpy(&v, sample, sizeof(v));
            return std::clamp(static_cast<float>((double)v / 2147483648.0), -1.0f, 1.0f);
        }
        return 0.0f;
    };

    if (cfg_.channelMode == AQInputChannelMode::AutoStrongest) {
        // choose the strongest channel for the whole buffer, not per
        // sample. Per-frame channel hopping can create distortion/phasey output
        // and makes meters unstable on multi-input interfaces.
        double energy[8] = {};
        for (uint32_t f = 0; f < frames; ++f) {
            for (uint32_t c = 0; c < channels && c < 8u; ++c) {
                const uint8_t* sample = raw + static_cast<size_t>(f) * bytesPerFrame + static_cast<size_t>(c) * bytesPerSample;
                const float v = readSample(sample);
                energy[c] += static_cast<double>(v) * static_cast<double>(v);
            }
        }
        uint32_t bestChannel = 0u;
        double bestEnergy = -1.0;
        for (uint32_t c = 0; c < channels && c < 8u; ++c) {
            if (energy[c] > bestEnergy) { bestEnergy = energy[c]; bestChannel = c; }
        }
        // sticky AutoStrongest with hysteresis. Keep the previous channel
        // unless a new channel is clearly stronger. This avoids audible clicks and
        // unstable meters when multiple disconnected inputs carry similar noise.
        const uint32_t previousChannel = autoStrongestChannel_.load(std::memory_order_acquire);
        const double previousEnergy = autoStrongestEnergy_.load(std::memory_order_acquire);
        if (previousChannel < channels && previousChannel < 8u) {
            const double prevNow = energy[previousChannel];
            const double switchThreshold = std::max(prevNow * 1.995262315, previousEnergy * 0.25); // ~+3 dB or prior decayed floor
            if (bestChannel != previousChannel && bestEnergy < switchThreshold) {
                bestChannel = previousChannel;
                bestEnergy = prevNow;
            }
        }
        autoStrongestChannel_.store(bestChannel, std::memory_order_release);
        autoStrongestEnergy_.store(bestEnergy, std::memory_order_release);
        stats_.selectedChannelZeroBased.store(bestChannel, std::memory_order_release);
        for (uint32_t f = 0; f < frames; ++f) {
            const uint8_t* sample = raw + static_cast<size_t>(f) * bytesPerFrame + static_cast<size_t>(bestChannel) * bytesPerSample;
            out[f] = readSample(sample);
        }
    } else {
        stats_.selectedChannelZeroBased.store(selectedCount == 1u ? (uint32_t)selected[0] : UINT32_MAX, std::memory_order_release);
        for (uint32_t f = 0; f < frames; ++f) {
            float acc = 0.0f;
            for (uint32_t si = 0; si < selectedCount; ++si) {
                const uint32_t c = selected[si];
                const uint8_t* sample = raw + static_cast<size_t>(f) * bytesPerFrame + static_cast<size_t>(c) * bytesPerSample;
                acc += readSample(sample);
            }
            out[f] = std::clamp(acc / static_cast<float>(selectedCount), -1.0f, 1.0f);
        }
    }

    framesOut = frames;
    return true;
}

// poll the AudioQueue hardware level meter (UI-thread; not the callback).
bool AQCapture::pollQueueMeters() noexcept {
    std::lock_guard<std::mutex> lk(queueMutex_);
    if (!queue_ || !stats_.queueMeterAvailable.load(std::memory_order_acquire)) return false;
    const uint32_t ch = std::clamp<uint32_t>(actualChannels_ ? actualChannels_ : asbd_.mChannelsPerFrame, 1u, 8u);
    AudioQueueLevelMeterState meters[8] = {};
    UInt32 size = ch * (UInt32)sizeof(AudioQueueLevelMeterState);
    OSStatus st = AudioQueueGetProperty(queue_, kAudioQueueProperty_CurrentLevelMeterDB, meters, &size);
    if (st != noErr) return false;
    const uint32_t got = std::min<uint32_t>(ch, size / (UInt32)sizeof(AudioQueueLevelMeterState));
    float peakDB = -160.0f, avgDB = -160.0f;
    for (uint32_t i = 0; i < got; ++i) {
        peakDB = std::max(peakDB, meters[i].mPeakPower);
        avgDB = std::max(avgDB, meters[i].mAveragePower);
    }
    stats_.queuePeakDB.store(peakDB, std::memory_order_release);
    stats_.queueAvgDB.store(avgDB, std::memory_order_release);
    return true;
}

void AQCapture::updateMeters(const float* mono, uint32_t frames) noexcept {
    if (!mono || frames == 0) return;
    double sumSq = 0.0;
    float peak = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = clampFloat(mono[i]);
        peak = std::max(peak, std::fabs(v));
        sumSq += static_cast<double>(v) * static_cast<double>(v);
    }
    const float rms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(frames)));
    float oldPeak = stats_.peak.load(std::memory_order_acquire);
    while (peak > oldPeak && !stats_.peak.compare_exchange_weak(oldPeak, peak, std::memory_order_acq_rel)) {}
    stats_.rms.store(rms, std::memory_order_release);
}

} } // namespace ArpSID::Digi
