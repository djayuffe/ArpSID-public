// Copyright (C) 2024-2026 Ulf Bertilsson
#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>

#include "arpsid_vst_cocoa_bridge.h"
#include "arpsid_vst_messages.h"
#include "pluginterfaces/vst/ivstmessage.h"
#import "au3/ArpSIDViewController.h"
#import "au3/ArpSIDDSPKernelAdapter.h"
#include "parameter_ids.h"
#include "arpsid/core/sid_runtime_host_ops.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "vst3/arpsid_vst3_kernel_host.h"
#include "au3/ArpSIDDSPKernel.hpp"
#include "arpsid/gui/digi_record_limits.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

using Steinberg::Vst::EditController;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;


@interface ArpSIDVSTPreset : NSObject
@property(nonatomic) NSInteger number;
@property(nonatomic, copy) NSString* name;
@end
@implementation ArpSIDVSTPreset
@end

@class ArpSIDVSTBridge;


@interface ArpSIDVSTContainerView : NSView
@end

@implementation ArpSIDVSTContainerView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder { return YES; }
- (BOOL)mouseDownCanMoveWindow { return NO; }
@end

@interface ArpSIDVSTRootViewController : NSViewController
@end

@implementation ArpSIDVSTRootViewController
- (void)loadView {
    NSView* view = [[ArpSIDVSTContainerView alloc] initWithFrame:NSMakeRect(0, 0, 720, 520)];
    view.wantsLayer = YES;
    self.view = view;
}
@end


@interface ArpSIDVSTParameter : NSObject
@property(nonatomic, weak) ArpSIDVSTBridge* bridge;
@property(nonatomic) int pid;
- (void)setValue:(float)value originator:(id)originator;
@end

@interface ArpSIDVSTParameterTree : NSObject
@property(nonatomic, weak) ArpSIDVSTBridge* bridge;
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, ArpSIDVSTParameter*>* params;
@property(nonatomic, strong) NSMutableArray* observers;
- (id)tokenByAddingParameterObserver:(void (^)(unsigned long long addr, float val))observer;
- (void)removeParameterObserver:(id)token;
- (ArpSIDVSTParameter*)parameterWithAddress:(unsigned long long)addr;
- (void)broadcastParameter:(int)pid value:(float)value;
@end

// The VST3 counterpart of ArpSIDDSPKernelAdapter: the same selectors the
// ArpSIDViewController uses under AU, implemented on the processor's
// Vst3KernelHost (same engine, same GUI models, same persistence).
@interface ArpSIDVSTDebugAdapter : NSObject
@property(nonatomic, weak) ArpSIDVSTBridge* bridge;
- (void)readTelemetry:(ArpSIDTelemetry*)out;
- (void)readTelemetry:(ArpSIDTelemetry*)out includeScopes:(BOOL)includeScopes;
- (void)readTelemetry:(ArpSIDTelemetry*)out includeScopes:(BOOL)includeScopes includeC64Snapshot:(BOOL)includeC64Snapshot;
- (int)readOscilloscope:(float*)buf maxSamples:(int)maxSamples;
- (void)readVCOScope:(float*)vco0 vco1:(float*)vco1 vco2:(float*)vco2;
- (ArpSID::ArpSIDDSPKernel*)kernelPtr;
@end

@interface ArpSIDVSTBridge : NSObject
@property(nonatomic, assign) EditController* controller;
@property(nonatomic, strong) ArpSIDVSTParameterTree* parameterTree;
@property(nonatomic, strong) NSArray* factoryPresets;
@property(nonatomic, strong) ArpSIDVSTPreset* currentPreset;
@property(nonatomic, strong) ArpSIDVSTDebugAdapter* debugAdapter;
- (instancetype)initWithController:(void*)controller;
- (float)getParameterValue:(int)paramID;
- (void)setParameterValue:(float)value forID:(int)paramID;
- (void)injectMIDIBytes:(const uint8_t*)data length:(uint32_t)length;
- (NSArray*)factoryPresets;
- (void)setCurrentPreset:(ArpSIDVSTPreset*)preset;
- (void)pollParameterObservers;
- (void)writeSIDRegister:(uint8_t)regIndex value:(uint8_t)value;
- (ArpSID::Vst3KernelHost*)kernelHost;
- (void)markStateDirty;
@end

@implementation ArpSIDVSTParameter
- (void)setValue:(float)value originator:(id)originator {
    (void)originator;
    [self.bridge setParameterValue:value forID:self.pid];
}
@end

@implementation ArpSIDVSTParameterTree
- (instancetype)init {
    self = [super init];
    if (self) {
        _params = [NSMutableDictionary dictionary];
        _observers = [NSMutableArray array];
    }
    return self;
}
- (id)tokenByAddingParameterObserver:(void (^)(unsigned long long addr, float val))observer {
    if (!observer) return nil;
    NSMutableDictionary* rec = [NSMutableDictionary dictionary];
    rec[@"id"] = [NSUUID UUID].UUIDString;
    rec[@"block"] = [observer copy];
    rec[@"shadow"] = [NSMutableDictionary dictionary];
    [_observers addObject:rec];
    return rec[@"id"];
}
- (void)removeParameterObserver:(id)token {
    if (!token) return;
    NSString* wanted = [token isKindOfClass:[NSString class]] ? (NSString*)token : [token description];
    NSIndexSet* idx = [_observers indexesOfObjectsPassingTest:^BOOL(id obj, NSUInteger idx, BOOL *stop) {
        (void)idx; (void)stop;
        return [obj[@"id"] isEqual:wanted];
    }];
    if (idx.count) [_observers removeObjectsAtIndexes:idx];
}
- (ArpSIDVSTParameter*)parameterWithAddress:(unsigned long long)addr {
    NSNumber* key = @(addr);
    ArpSIDVSTParameter* p = _params[key];
    if (!p) {
        p = [ArpSIDVSTParameter new];
        p.bridge = self.bridge;
        p.pid = (int)addr;
        _params[key] = p;
    }
    return p;
}
- (void)broadcastParameter:(int)pid value:(float)value {
    NSArray* observerSnapshot = [_observers copy];
    for (NSMutableDictionary* rec in observerSnapshot) {
        NSMutableDictionary* shadow = rec[@"shadow"];
        NSNumber* prev = shadow[@(pid)];
        if (prev && fabsf(prev.floatValue - value) < 1.0e-5f) continue;
        shadow[@(pid)] = @(value);
        void (^block)(unsigned long long, float) = rec[@"block"];
        if (block) block((unsigned long long)pid, value);
    }
}
@end

@implementation ArpSIDVSTDebugAdapter
- (ArpSID::Vst3KernelHost*)_host { return [self.bridge kernelHost]; }
- (ArpSID::ArpSIDDSPKernel*)kernelPtr {
    ArpSID::Vst3KernelHost* h = [self _host];
    return h ? &h->kernel() : nullptr;
}
- (void)readTelemetry:(ArpSIDTelemetry*)out includeScopes:(BOOL)includeScopes includeC64Snapshot:(BOOL)includeC64Snapshot {
    if (!out) return;
    ArpSID::Vst3KernelHost* h = [self _host];
    if (h) {
        h->pollNonRealtime();
        h->readTelemetry(*out, includeScopes ? true : false, includeC64Snapshot ? true : false);
    } else {
        memset(out, 0, sizeof(*out));
        out->lastMidiNote = -1;
        out->hostTempo = 120.0;
    }
    [self.bridge pollParameterObservers];
}
- (void)readTelemetry:(ArpSIDTelemetry*)out includeScopes:(BOOL)includeScopes {
    [self readTelemetry:out includeScopes:includeScopes includeC64Snapshot:YES];
}
- (void)readTelemetry:(ArpSIDTelemetry*)out {
    [self readTelemetry:out includeScopes:YES includeC64Snapshot:YES];
}
- (int)readOscilloscope:(float*)buf maxSamples:(int)maxSamples {
    if (!buf || maxSamples <= 0) return 0;
    ArpSID::ArpSIDDSPKernel* k = [self kernelPtr];
    if (!k) { memset(buf, 0, (size_t)maxSamples * sizeof(float)); return 0; }
    const int n = k->readOscilloscope(buf, maxSamples);
    for (int i = 0; i < n; ++i) buf[i] = std::isfinite(buf[i]) ? std::clamp(buf[i], -1.25f, 1.25f) : 0.0f;
    return n;
}
- (void)readVCOScope:(float*)vco0 vco1:(float*)vco1 vco2:(float*)vco2 {
    static const int kScope = 256;
    float* dst[3] = { vco0, vco1, vco2 };
    ArpSID::ArpSIDDSPKernel* k = [self kernelPtr];
    if (!k) {
        for (float* d : dst) if (d) memset(d, 0, kScope * sizeof(float));
        return;
    }
    k->notePresentationScopeRequest();
    static thread_local float oscBuf[3][kScope];
    uint8_t activeMask = 0;
    uint32_t writePos = 0;
    k->getPresentationOscScopeSnapshot(oscBuf, activeMask, writePos);
    const uint32_t wp = writePos & 255u;
    for (int v = 0; v < 3; ++v) {
        if (!dst[v]) continue;
        for (int i = 0; i < kScope; ++i) {
            const float x = oscBuf[v][(wp + (uint32_t)i) & 255u];
            dst[v][i] = std::isfinite(x) ? std::clamp(x, -1.25f, 1.25f) : 0.0f;
        }
    }
}
- (void)readDiagnosticCounters:(ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot*)out {
    if (!out) return;
    *out = ArpSID::GUI::ArpSIDDiagnosticCounterSnapshot{};
    if (ArpSID::ArpSIDDSPKernel* k = [self kernelPtr]) k->collectDiagnosticCounters(*out);
}
- (void)setSidCorePanelModel:(ArpSID::GUI::SidCorePanelModel*)model {
    if (ArpSID::ArpSIDDSPKernel* k = [self kernelPtr]) k->setSidCorePanelModel(model);
}
- (void)getSettingsModel:(ArpSID::GUI::SettingsPanelModel*)out {
    if (!out) return;
    ArpSID::Vst3KernelHost* h = [self _host];
    *out = h ? h->settings() : ArpSID::GUI::makeDefaultSettings();
}
- (void)setSettingsModel:(const ArpSID::GUI::SettingsPanelModel*)m {
    ArpSID::Vst3KernelHost* h = [self _host];
    if (!m || !h) return;
    h->setSettings(*m);
    [self.bridge markStateDirty];
}
- (void)getKitStateBlob:(ArpSID::GUI::KitStateBlob*)out {
    if (!out) return;
    ArpSID::Vst3KernelHost* h = [self _host];
    *out = h ? h->kit() : ArpSID::GUI::makeDefaultKitStateBlob();
}
- (void)setKitStateBlob:(const ArpSID::GUI::KitStateBlob*)b {
    ArpSID::Vst3KernelHost* h = [self _host];
    if (!b || !h) return;
    h->setKit(*b);
    [self.bridge markStateDirty];
}
- (void)getMixModel:(ArpSID::GUI::MixPanelModel*)out {
    if (!out) return;
    ArpSID::Vst3KernelHost* h = [self _host];
    *out = h ? h->mix() : ArpSID::GUI::makeDefaultMixModel();
}
- (void)setMixModel:(const ArpSID::GUI::MixPanelModel*)m {
    ArpSID::Vst3KernelHost* h = [self _host];
    if (!m || !h) return;
    h->setMix(*m);
    [self.bridge markStateDirty];
}
- (void)getDigiModel:(ArpSID::GUI::DigiPanelModel*)model sampleBank:(ArpSID::GUI::DigiSampleBankBlob*)bank {
    if (!model || !bank) return;
    ArpSID::Vst3KernelHost* h = [self _host];
    if (h) {
        h->digi(*model, *bank);
    } else {
        *model = ArpSID::GUI::makeDefaultDigiPanelModel();
        ArpSID::GUI::resetDigiSampleBankBlob(*bank);
    }
}
- (void)setDigiModel:(const ArpSID::GUI::DigiPanelModel*)model sampleBank:(const ArpSID::GUI::DigiSampleBankBlob*)bank {
    ArpSID::Vst3KernelHost* h = [self _host];
    if (!model || !bank || !h) return;
    h->setDigi(*model, *bank);
    [self.bridge markStateDirty];
}
- (BOOL)setDigiUserSampleForSlot:(NSInteger)slot
                         samples:(const float*)samples
                      frameCount:(NSUInteger)frameCount
                      sampleRate:(double)sampleRate
                            name:(NSString*)name {
    ArpSID::Vst3KernelHost* h = [self _host];
    if (!h) return NO;
    const uint32_t frames = (uint32_t)std::min<NSUInteger>(frameCount, (NSUInteger)UINT32_MAX);
    const BOOL ok = h->setDigiUserSample((int)slot, samples, frames, sampleRate, name ? name.UTF8String : nullptr) ? YES : NO;
    if (ok) [self.bridge markStateDirty];
    return ok;
}
- (void)setDigiD418RuntimeMode:(uint8_t)mode rateHz:(uint32_t)rateHz {
    if (ArpSID::Vst3KernelHost* h = [self _host]) { h->setDigiD418RuntimeMode(mode, rateHz); [self.bridge markStateDirty]; }
}
- (void)getDigiD418RuntimeMode:(uint8_t*)mode rateHz:(uint32_t*)rateHz {
    uint8_t m = 0; uint32_t r = 0;
    if (ArpSID::Vst3KernelHost* h = [self _host]) h->digiD418RuntimeMode(m, r);
    if (mode) *mode = m;
    if (rateHz) *rateHz = r;
}
- (void)clearDigiD418RuntimeTelemetry {
    if (ArpSID::Vst3KernelHost* h = [self _host]) h->clearDigiD418Telemetry();
}
- (void)triggerDigiPadSlot:(uint8_t)slot velocity:(uint8_t)velocity {
    if (ArpSID::Vst3KernelHost* h = [self _host]) h->triggerDigiPad(slot, velocity);
}
- (BOOL)drainQueuedDrumBridgeSlotLoadNonRealtime {
    ArpSID::ArpSIDDSPKernel* k = [self kernelPtr];
    return (k && k->drainQueuedDrumBridgeSlotLoadNonRealtime()) ? YES : NO;
}
- (void)setDigiMidiPadRootNote:(uint8_t)rootNote channelFilter:(uint8_t)channelFilter {
    if (ArpSID::Vst3KernelHost* h = [self _host]) { h->setDigiMidiPadMapping(rootNote, channelFilter); [self.bridge markStateDirty]; }
}
- (void)getDigiMidiPadRootNote:(uint8_t*)rootNote channelFilter:(uint8_t*)channelFilter {
    uint8_t r = 36, c = 16;
    if (ArpSID::Vst3KernelHost* h = [self _host]) h->digiMidiPadMapping(r, c);
    if (rootNote) *rootNote = r;
    if (channelFilter) *channelFilter = c;
}
- (void)setPureSid1Q1OutputMode:(BOOL)enabled {
    if (ArpSID::Vst3KernelHost* h = [self _host]) { h->setPureSid1Q1OutputMode(enabled ? true : false); [self.bridge markStateDirty]; }
}
- (BOOL)pureSid1Q1OutputMode {
    ArpSID::Vst3KernelHost* h = [self _host];
    return (h && h->pureSid1Q1OutputMode()) ? YES : NO;
}
- (BOOL)startPureSid1Q1RecordCapture:(NSUInteger)maxFrames {
    ArpSID::ArpSIDDSPKernel* k = [self kernelPtr];
    if (!k) return NO;
    return k->startPureSid1Q1RecordCapture((uint32_t)std::min<NSUInteger>(maxFrames, ArpSID::GUI::kDigiRecordCaptureMaxFrames)) ? YES : NO;
}
- (BOOL)copyAndStopPureSid1Q1RecordCapture:(float*)dst
                                  maxFrames:(NSUInteger)maxFrames
                                 frameCount:(NSUInteger*)frameCount
                                 sampleRate:(double*)sampleRate
                              droppedFrames:(NSUInteger*)droppedFrames {
    ArpSID::ArpSIDDSPKernel* k = [self kernelPtr];
    if (!k) return NO;
    uint32_t frames = 0u, drops = 0u;
    double sr = 44100.0;
    const bool ok = k->copyAndStopPureSid1Q1RecordCapture(
        dst, (uint32_t)std::min<NSUInteger>(maxFrames, ArpSID::GUI::kDigiRecordCaptureMaxFrames), &frames, &sr, &drops);
    if (frameCount) *frameCount = (NSUInteger)frames;
    if (sampleRate) *sampleRate = sr;
    if (droppedFrames) *droppedFrames = (NSUInteger)drops;
    return ok ? YES : NO;
}
- (BOOL)pureSid1Q1RecordCaptureStatusFrames:(NSUInteger*)frameCount
                                  sampleRate:(double*)sampleRate
                               droppedFrames:(NSUInteger*)droppedFrames
                                        peak:(float*)peak
                                         rms:(float*)rms {
    ArpSID::ArpSIDDSPKernel* k = [self kernelPtr];
    if (!k) return NO;
    uint32_t frames = 0u, drops = 0u;
    double sr = 44100.0;
    float p = 0.0f, r = 0.0f;
    k->pureSid1Q1RecordCaptureStatus(&frames, &sr, &drops, &p, &r);
    if (frameCount) *frameCount = (NSUInteger)frames;
    if (sampleRate) *sampleRate = sr;
    if (droppedFrames) *droppedFrames = (NSUInteger)drops;
    if (peak) *peak = p;
    if (rms) *rms = r;
    return YES;
}
@end

@implementation ArpSIDVSTBridge
@synthesize currentPreset = _currentPreset;

- (void)_refreshCurrentPresetFromProgram {
    NSArray* presets = _factoryPresets;
    if (!presets.count) { _currentPreset = nil; return; }
    NSInteger slot = (NSInteger)std::clamp((int)ArpSID::canonicalFactorySlotFromNormalizedBankSlot([self getParameterValue:ArpSID::kParamBankSlot]), 0, ArpSID::kCanonicalFactoryPatchSlotMax);
    if (slot >= 0 && slot < (NSInteger)presets.count) _currentPreset = presets[(NSUInteger)slot];
    else _currentPreset = nil;
}

- (instancetype)initWithController:(void*)controllerPtr {
    self = [super init];
    if (self) {
        _controller = reinterpret_cast<EditController*>(controllerPtr);
        _parameterTree = [ArpSIDVSTParameterTree new];
        _parameterTree.bridge = self;
        _debugAdapter = [ArpSIDVSTDebugAdapter new];
        _debugAdapter.bridge = self;
        NSMutableArray* presets = [NSMutableArray arrayWithCapacity:(NSUInteger)ArpSID::kCanonicalFactoryPatchSlotCount];
        for (int i = 0; i < ArpSID::kCanonicalFactoryPatchSlotCount; ++i) {
            ArpSIDVSTPreset* p = [ArpSIDVSTPreset new];
            p.number = i;
            p.name = [NSString stringWithUTF8String:ArpSID::factoryPatchNameForSlot(i).c_str()];
            [presets addObject:p];
        }
        _factoryPresets = presets;
        [self _refreshCurrentPresetFromProgram];
    }
    return self;
}
- (float)getParameterValue:(int)paramID {
    if (!_controller || paramID < 0 || paramID >= ArpSID::kNumParams) return ArpSID::defaultNormalizedParamValue(paramID);
    return ArpSID::sanitizeNormalizedParamValue(paramID, (float)_controller->getParamNormalized((ParamID)paramID),
                                               ArpSID::defaultNormalizedParamValue(paramID));
}
- (void)setParameterValue:(float)value forID:(int)paramID {
    if (!_controller || paramID < 0 || paramID >= ArpSID::kNumParams) return;
    const ParamValue v = (ParamValue)ArpSID::sanitizeNormalizedParamValue(paramID, value, ArpSID::defaultNormalizedParamValue(paramID));
    // VST3 editor contract: update the controller's own value, then tell the
    // host. setParamNormalized() is also where Program/BankSlot selections
    // become factory patch loads.
    _controller->setParamNormalized((ParamID)paramID, v);
    _controller->beginEdit((ParamID)paramID);
    _controller->performEdit((ParamID)paramID, v);
    _controller->endEdit((ParamID)paramID);
    [_parameterTree broadcastParameter:paramID value:(float)v];
}
- (void)writeSIDRegister:(uint8_t)regIndex value:(uint8_t)value {
    if (regIndex > 0x18u) return; // writable SID hardware regs only; reject RO/system pseudo regs
    const int pid = (int)ArpSID::kParamSidRegD400 + (int)regIndex;
    if (pid < 0 || pid >= ArpSID::kNumParams) return;
    [self setParameterValue:((float)value / 255.0f) forID:pid];
}
- (void)injectMIDIBytes:(const uint8_t*)data length:(uint32_t)length {
    // On-screen keyboard: forward note on/off to the processor, which queues
    // them for the audio thread (arpsid_vst_messages.h).
    if (!_controller || !data) return;
    for (uint32_t i = 0; i + 3 <= length; ) {
        const uint8_t status = data[i];
        const uint8_t kind = status & 0xF0u;
        if (kind != 0x80u && kind != 0x90u) { ++i; continue; }
        Steinberg::IPtr<Steinberg::Vst::IMessage> msg = Steinberg::owned(_controller->allocateMessage());
        if (msg) {
            msg->setMessageID(ArpSID::kVstMsgUiMidi);
            msg->getAttributes()->setInt(ArpSID::kVstMsgAttrStatus, status);
            msg->getAttributes()->setInt(ArpSID::kVstMsgAttrData1, data[i + 1] & 0x7F);
            msg->getAttributes()->setInt(ArpSID::kVstMsgAttrData2, data[i + 2] & 0x7F);
            _controller->sendMessage(msg);
        }
        i += 3;
    }
}
- (NSArray*)factoryPresets { [self _refreshCurrentPresetFromProgram]; return _factoryPresets; }
- (ArpSIDVSTPreset*)currentPreset { [self _refreshCurrentPresetFromProgram]; return _currentPreset; }
- (void)setCurrentPreset:(ArpSIDVSTPreset*)preset {
    if (!preset) return;
    _currentPreset = preset;
    [self setParameterValue:ArpSID::canonicalNormalizedBankSlotValue((int)preset.number) forID:ArpSID::kParamBankSlot];
}
- (ArpSID::Vst3KernelHost*)kernelHost {
    return _controller ? ArpSID::arpsidControllerKernelHost(_controller) : nullptr;
}
- (void)markStateDirty {
    if (_controller) ArpSID::arpsidControllerMarkStateDirty(_controller);
}
// AU-parity entry points the view controller probes with respondsToSelector:.
- (id)kernelAdapter { return [self kernelHost] ? _debugAdapter : nil; }
- (NSInteger)componentFlavor { return 0; }
- (void)performC64ControlHubCommand:(NSInteger)command {
    if (ArpSID::Vst3KernelHost* h = [self kernelHost]) h->c64ControlHubCommand((int)command);
}
- (BOOL)loadSidFileData:(NSData*)data { return [self loadSidFileData:data subtune:0u]; }
- (BOOL)loadSidFileData:(NSData*)data subtune:(uint16_t)subtune {
    ArpSID::Vst3KernelHost* h = [self kernelHost];
    if (!h || !data.length) return NO;
    return h->loadSidFile(data.bytes, (size_t)data.length, subtune) ? YES : NO;
}
- (void)unloadSidFile {
    if (ArpSID::Vst3KernelHost* h = [self kernelHost]) h->unloadSidFile();
}
- (void)resetC64SidPlayerForEject {
    if (ArpSID::Vst3KernelHost* h = [self kernelHost]) h->kernel().resetC64SidPlayerForEject();
}
- (BOOL)applyUserFactoryPresetNumber:(NSInteger)slot {
    if (slot < 0 || slot > ArpSID::kCanonicalFactoryPatchSlotMax) return NO;
    [self setParameterValue:ArpSID::canonicalNormalizedBankSlotValue((int)slot) forID:ArpSID::kParamBankSlot];
    return YES;
}
- (void)pollParameterObservers {
    [self _refreshCurrentPresetFromProgram];
    std::vector<float> values((size_t)ArpSID::kNumParams);
    for (int pid = 0; pid < ArpSID::kNumParams; ++pid) {
        values[(size_t)pid] = [self getParameterValue:pid];
    }
    for (int pid = 0; pid < ArpSID::kNumParams; ++pid) {
        [_parameterTree broadcastParameter:pid value:values[(size_t)pid]];
    }
}
@end

@interface ArpSIDVSTRichCocoaHandle : NSObject
@property(nonatomic, strong) ArpSIDVSTRootViewController* rootVC;
@property(nonatomic, strong) ArpSIDViewController* controllerVC;
@property(nonatomic, strong) ArpSIDVSTBridge* bridge;
@property(nonatomic, weak) NSView* parentView;
@end
@implementation ArpSIDVSTRichCocoaHandle
- (void)teardownEmbeddedViews {
    if ([_controllerVC respondsToSelector:@selector(pauseEditorViewForHostDetach)])
        [_controllerVC pauseEditorViewForHostDetach];
    if (_controllerVC.view.superview) [_controllerVC.view removeFromSuperview];
    if (_rootVC.view.superview) [_rootVC.view removeFromSuperview];
    if (_controllerVC.parentViewController == _rootVC) [_controllerVC removeFromParentViewController];
}
- (void)dealloc {
    [self teardownEmbeddedViews];
}
@end

namespace ArpSID {
void* arpsidOpenRichCocoaEditor(void* parentNSView, void* editController) noexcept {
    if (!parentNSView || !editController) return nullptr;
    @autoreleasepool {
        NSView* parent = (__bridge NSView*)parentNSView;
        if (![parent isKindOfClass:[NSView class]]) return nullptr;

        ArpSIDVSTRichCocoaHandle* handle = [ArpSIDVSTRichCocoaHandle new];
        handle.parentView = parent;
        handle.bridge = [[ArpSIDVSTBridge alloc] initWithController:editController];

        handle.rootVC = [ArpSIDVSTRootViewController new];
        [handle.rootVC loadView];

        handle.controllerVC = [ArpSIDViewController new];
        [handle.controllerVC loadView];
        [handle.controllerVC connectBridge:handle.bridge];

        NSView* rootView = handle.rootVC.view;
        NSView* child = handle.controllerVC.view;
        rootView.translatesAutoresizingMaskIntoConstraints = NO;
        child.translatesAutoresizingMaskIntoConstraints = NO;

        [handle.rootVC addChildViewController:handle.controllerVC];
        /* didMoveToParentViewController: is not consistently visible on the SDK surface used here.
           addChildViewController: + view containment is sufficient for this embedded host path. */
        rootView.nextResponder = handle.controllerVC;
        child.nextResponder = handle.controllerVC;
        rootView.frame = parent.bounds;
        child.frame = rootView.bounds;
        [rootView addSubview:child];
        [NSLayoutConstraint activateConstraints:@[
            [child.leadingAnchor constraintEqualToAnchor:rootView.leadingAnchor],
            [child.trailingAnchor constraintEqualToAnchor:rootView.trailingAnchor],
            [child.topAnchor constraintEqualToAnchor:rootView.topAnchor],
            [child.bottomAnchor constraintEqualToAnchor:rootView.bottomAnchor],
        ]];

        [parent addSubview:rootView];
        [NSLayoutConstraint activateConstraints:@[
            [rootView.leadingAnchor constraintEqualToAnchor:parent.leadingAnchor],
            [rootView.trailingAnchor constraintEqualToAnchor:parent.trailingAnchor],
            [rootView.topAnchor constraintEqualToAnchor:parent.topAnchor],
            [rootView.bottomAnchor constraintEqualToAnchor:parent.bottomAnchor],
        ]];

        [handle.controllerVC prepareForEmbeddedVSTPresentation];
        [rootView setNeedsLayout:YES];
        [child setNeedsDisplay:YES];
        // v793 — Embedded VST focus continuation must not retain the host parent,
        // child view, handle, or controller after the editor has been closed.
        // The returned handle owns the view hierarchy; this delayed focus polish should
        // be a best-effort weak continuation only.
        __weak NSView* weakParent_v793 = parent;
        __weak NSView* weakChild_v793 = child;
        __weak ArpSIDViewController* weakController_v793 = handle.controllerVC;
        dispatch_async(dispatch_get_main_queue(), ^{
            NSView* parent_v793 = weakParent_v793;
            NSView* child_v793 = weakChild_v793;
            ArpSIDViewController* controller_v793 = weakController_v793;
            if (!parent_v793 || !child_v793 || !controller_v793) return;

            NSWindow* w = parent_v793.window;
            if (w) {
                [w recalculateKeyViewLoop];
                NSView* first = child_v793;
                if ([controller_v793 respondsToSelector:@selector(embeddedVSTPreferredFirstResponder)]) {
                    NSView* preferred = [controller_v793 embeddedVSTPreferredFirstResponder];
                    if (preferred) first = preferred;
                }
                [w makeFirstResponder:first];
            }
        });
        return (__bridge_retained void*)handle;
    }
}

void arpsidCloseRichCocoaEditor(void* opaque) noexcept {
    if (!opaque) return;
    @autoreleasepool {
        ArpSIDVSTRichCocoaHandle* handle = (__bridge_transfer ArpSIDVSTRichCocoaHandle*)opaque;
        [handle teardownEmbeddedViews];
        (void)handle;
    }
}

void arpsidResizeRichCocoaEditor(void* opaque, double width, double height) noexcept {
    if (!opaque) return;
    @autoreleasepool {
        ArpSIDVSTRichCocoaHandle* handle = (__bridge ArpSIDVSTRichCocoaHandle*)opaque;
        if (!handle.rootVC.view) return;
        NSRect frame = NSMakeRect(0, 0, width, height);
        handle.rootVC.view.frame = frame;
        [handle.rootVC.view setNeedsLayout:YES];
        [handle.controllerVC.view setNeedsLayout:YES];
        if ([handle.controllerVC respondsToSelector:@selector(embeddedVSTHostDidResize)])
            [handle.controllerVC embeddedVSTHostDidResize];
    }
}
} // namespace ArpSID
