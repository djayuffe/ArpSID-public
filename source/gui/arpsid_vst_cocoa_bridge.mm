// Copyright (C) 2024-2026 Ulf Bertilsson
#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>

#include "arpsid_vst_cocoa_bridge.h"
#import "au3/ArpSIDViewController.h"
#import "au3/ArpSIDDSPKernelAdapter.h"
#include "parameter_ids.h"
#include "arpsid/core/sid_runtime_host_ops.h"
#include "arpsid/patchbank/forensic_patch_bank.h"
#include "arpsid_telemetry_iface.h"
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

namespace ArpSID {
static std::atomic<IArpSIDTelemetryProvider*> gActiveTelemetryProvider { nullptr };
void arpsidSetActiveTelemetryProvider(IArpSIDTelemetryProvider* provider) noexcept {
    gActiveTelemetryProvider.store(provider, std::memory_order_release);
}
IArpSIDTelemetryProvider* arpsidGetActiveTelemetryProvider() noexcept {
    return gActiveTelemetryProvider.load(std::memory_order_acquire);
}
}

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

@interface ArpSIDVSTDebugAdapter : NSObject
@property(nonatomic, weak) ArpSIDVSTBridge* bridge;
- (void)readTelemetry:(ArpSIDTelemetry*)out;
- (void)readTelemetry:(ArpSIDTelemetry*)out includeScopes:(BOOL)includeScopes;
- (int)readOscilloscope:(float*)buf maxSamples:(int)maxSamples;
- (void)readVCOScope:(float*)vco0 vco1:(float*)vco1 vco2:(float*)vco2;
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
- (void)readTelemetry:(ArpSIDTelemetry*)out includeScopes:(BOOL)includeScopes {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->lastMidiNote = -1;
    out->hostTempo = 120.0;
    auto* provider = ArpSID::arpsidGetActiveTelemetryProvider();
    if (provider && provider->arpGetLatestFullTelemetry(*out, includeScopes ? true : false)) {
        [self.bridge pollParameterObservers];
        return;
    }
    [self readTelemetry:out];
}
- (void)readTelemetry:(ArpSIDTelemetry*)out {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    auto* provider = ArpSID::arpsidGetActiveTelemetryProvider();
    if (!provider) return;
    if (provider->arpGetLatestFullTelemetry(*out, true)) {
        [self.bridge pollParameterObservers];
        return;
    }
    ArpSID::MeterSnapshot snap{};
    if (!provider->arpGetLatestSnapshot(snap)) provider->arpGetTelemetryShadowSnapshot(snap);
    out->peakL = std::clamp(std::isfinite(snap.peakL) ? snap.peakL : 0.0f, 0.0f, 1.0f);
    out->peakR = std::clamp(std::isfinite(snap.peakR) ? snap.peakR : 0.0f, 0.0f, 1.0f);
    out->rmsL = std::clamp(std::isfinite(snap.rmsL) ? snap.rmsL : 0.0f, 0.0f, 1.0f);
    out->rmsR = std::clamp(std::isfinite(snap.rmsR) ? snap.rmsR : 0.0f, 0.0f, 1.0f);
    out->activeVoices = snap.activeVoices;
    out->arpStep = snap.arpStep;
    out->lastMidiNote = (int)snap.lastMidiNote;
    out->hostTempo = 120.0;
    out->hostBeat = 0.0;
    out->hostPlaying = snap.arpPlaying;
    for (size_t i = 0; i < snap.sidRegs.size() && i < 30; ++i) out->sidRegs[i] = snap.sidRegs[i];
    [self.bridge pollParameterObservers];
}
- (int)readOscilloscope:(float*)buf maxSamples:(int)maxSamples {
    if (!buf || maxSamples <= 0) return 0;
    memset(buf, 0, (size_t)maxSamples * sizeof(float));
    auto* provider = ArpSID::arpsidGetActiveTelemetryProvider();
    ArpSIDTelemetry tel{};
    int copied = 0;
    if (provider && provider->arpGetLatestFullTelemetry(tel, true)) {
        copied = std::min(maxSamples, (int)std::min<uint32_t>(tel.mainOscScopeCount, 512u));
        for (int i = 0; i < copied; ++i)
            buf[i] = std::clamp(std::isfinite(tel.mainOscScope[i]) ? tel.mainOscScope[i] : 0.0f, -1.0f, 1.0f);
    }
    [self.bridge pollParameterObservers];
    return copied;
}
- (void)readVCOScope:(float*)vco0 vco1:(float*)vco1 vco2:(float*)vco2 {
    if (vco0) memset(vco0, 0, 256 * sizeof(float));
    if (vco1) memset(vco1, 0, 256 * sizeof(float));
    if (vco2) memset(vco2, 0, 256 * sizeof(float));
    auto* provider = ArpSID::arpsidGetActiveTelemetryProvider();
    ArpSIDTelemetry tel{};
    if (provider && provider->arpGetLatestFullTelemetry(tel, true)) {
        for (int i = 0; i < 256; ++i) {
            if (vco0) vco0[i] = std::clamp(std::isfinite(tel.vcoScope[0][i]) ? tel.vcoScope[0][i] : 0.0f, -1.0f, 1.0f);
            if (vco1) vco1[i] = std::clamp(std::isfinite(tel.vcoScope[1][i]) ? tel.vcoScope[1][i] : 0.0f, -1.0f, 1.0f);
            if (vco2) vco2[i] = std::clamp(std::isfinite(tel.vcoScope[2][i]) ? tel.vcoScope[2][i] : 0.0f, -1.0f, 1.0f);
        }
    }
    [self.bridge pollParameterObservers];
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
    (void)data; (void)length;
}
- (NSArray*)factoryPresets { [self _refreshCurrentPresetFromProgram]; return _factoryPresets; }
- (ArpSIDVSTPreset*)currentPreset { [self _refreshCurrentPresetFromProgram]; return _currentPreset; }
- (void)setCurrentPreset:(ArpSIDVSTPreset*)preset {
    if (!preset) return;
    _currentPreset = preset;
    [self setParameterValue:ArpSID::canonicalNormalizedBankSlotValue((int)preset.number) forID:ArpSID::kParamBankSlot];
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
