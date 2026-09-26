// ArpSIDHostAppDelegate.mm
// ArpSID AUv3 — Standalone Host App Delegate — Full Implementation
//
// What this does
// ──────────────
// 1. Builds an AVAudioEngine: output → ArpSIDAudioUnit → speakers.
// 2. Instantiates ArpSIDAudioUnit programmatically (no AppStore sandbox).
// 3. Creates a CoreMIDI virtual input port so any MIDI device / DAW can
// send MIDI to the standalone host.
// 4. Embeds the ArpSIDViewController inside an NSWindow.
// 5. Provides a menu bar with Panic, Preset Next/Prev, and Quit.
//
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT

#import "ArpSIDHostAppDelegate.h"

#ifndef ARPSID_UI_TRACE
#define ARPSID_UI_TRACE 0
#endif
#if ARPSID_UI_TRACE
#define ARPSID_UI_LOG(...) NSLog(__VA_ARGS__)
#else
#define ARPSID_UI_LOG(...) do {} while (0)
#endif
#import "ArpSIDAudioUnit.h"
#import "ArpSIDDSPKernelAdapter.h"
#include "ArpSIDCanonicalEvents.h"
#import "ArpSIDViewController.h"

@interface ArpSIDViewController (HostCompat)
- (void)refreshPresets;
- (void)updatePresentationContext:(NSString*)contextLine;
- (void)selectPresentationTabIndex:(NSInteger)index;
@end
#include "parameter_ids.h"

#import <Cocoa/Cocoa.h>
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#include <atomic>
#import <CoreMIDI/CoreMIDI.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// ─── MIDI Thunk ───────────────────────────────────────────────────────────────
//
// CoreMIDI calls the read proc on an arbitrary kernel thread. We bounce
// events to the main queue and then dispatch them to the AUAudioUnit's
// scheduleMIDIEventImmediate API (available in macOS 12+) or via a manual
// AUMIDIEvent injection on older SDKs.

// Returns YES if the MIDI status byte passes the channel filter.
// channelFilter==0 means omni (pass all); otherwise only pass the matching channel.
static BOOL ArpSIDShouldForwardPacket(uint8_t status, NSInteger channelFilter) {
    if (channelFilter <= 0) return YES;
    const uint8_t msgType = status & 0xF0u;
    if (msgType < 0x80u || msgType == 0xF0u) return YES;  // sysex/realtime always pass
    const uint8_t ch = (status & 0x0Fu) + 1;              // 1-based
    return (ch == (uint8_t)channelFilter);
}

static void ArpSIDMIDIReadProc(const MIDIPacketList* pktList,
                                void* readProcRefCon,
                                void* srcConnRefCon);


@class ArpSIDHostAppDelegate;

// v778 — CoreMIDI callback lifetime guard. CoreMIDI notification/read procs can
// enter on system threads and may race teardown. Passing (__bridge void*)self as
// refCon was a raw, unretained pointer. Keep a retained context box alive until
// after MIDI objects are disposed, and weak-load the delegate inside callbacks.
@interface ArpSIDHostMIDIContext_v778 : NSObject
@property (atomic, weak) ArpSIDHostAppDelegate* delegate;
@end
@implementation ArpSIDHostMIDIContext_v778
@end

static ArpSIDHostAppDelegate* ArpSIDHostDelegateFromMIDIContext_v778(void* context) {
    if (!context) return nil;
    ArpSIDHostMIDIContext_v778* box = (__bridge ArpSIDHostMIDIContext_v778*)context;
    return box.delegate;
}

static void ArpSIDReleaseRetainedMIDIContext_v778(void*& context) {
    if (!context) return;
    CFRelease(context);
    context = nullptr;
}

// ─── Interface extension ──────────────────────────────────────────────────────

@interface ArpSIDHostAppDelegate () {
    AVAudioEngine*          _engine;
    AVAudioUnit*            _avAudioUnit;
    AVAudioSourceNode*      _sourceNode;
    ArpSIDAudioUnit*        _audioUnit;
    ArpSIDViewController*   _viewController;
    NSWindow*               _mainWindow;

    MIDIClientRef           _midiClient;
    MIDIPortRef             _midiInputPort;
    MIDIEndpointRef         _midiDest;         // virtual destination endpoint
    void*                   _midiCallbackContext_v778; // retained weak-box for CoreMIDI callbacks
    NSMutableArray*         _midiSourceNames;  // display names of connected sources
    NSMutableArray*         _midiEndpoints;    // parallel array of MIDIEndpointRef (boxed)
    NSTimer*                _midiActivityTimer;// decays activity LED after 80ms
    BOOL                    _midiActivity;     // recent MIDI received

    // Preferred device + channel filtering (persisted in NSUserDefaults)
    NSString*               _preferredDeviceName; // nil = omni (all sources)
    NSInteger               _midiChannelFilter;   // 0 = omni, 1-16 = specific channel

    // MIDI menu (kept to update it on hot-plug)
    NSMenu*                 _midiMenu;
    NSMenuItem*             _midiDeviceParent;
    NSMenuItem*             _midiChannelParent;

    // CC67 (soft pedal) save/restore state.
    // FIX 2.1: was static std::atomic<float> — shared across ALL standalone instances.
    // Now a per-instance ivar; two standalone windows no longer bleed into each other.
    float                   _softPedalSavedCutoff; // -1.f = not saved
    NSInteger               _currentPresetIndex;   // per-instance current factory preset index
}
- (BOOL)_isAUv3WrapperPresentationApp;
- (NSString*)_presentationContextLine;
@end


// ─── Implementation ───────────────────────────────────────────────────────────

@interface ArpSIDHostAppDelegate () <NSWindowDelegate> @end

@implementation ArpSIDHostAppDelegate

@synthesize engine    = _engine;
@synthesize audioUnit = _audioUnit;

- (BOOL)_isAUv3WrapperPresentationApp {
    NSString* bundleName = ([[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"] != nil) ? [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"] : @"";
    if ([bundleName rangeOfString:@"AUv3" options:NSCaseInsensitiveSearch].location != NSNotFound) return YES;
    NSString* pluginsDir = [[[NSBundle mainBundle] builtInPlugInsPath] stringByStandardizingPath];
    return [[NSFileManager defaultManager] fileExistsAtPath:[pluginsDir stringByAppendingPathComponent:@"arpsid_auv3.appex"]];
}

- (NSString*)_presentationContextLine {
    return [self _isAUv3WrapperPresentationApp] ? @"AUv3 Extension Presentation" : @"AUv3 Standalone Presentation";
}

- (void)_presentFatalStartupErrorWithTitle:(NSString*)title error:(NSError*)error details:(NSString*)details {
    NSString* informativeText = nil;
    if (error.localizedDescription.length > 0 && details.length > 0) {
        informativeText = [NSString stringWithFormat:@"%@\n\n%@", error.localizedDescription, details];
    } else if (error.localizedDescription.length > 0) {
        informativeText = error.localizedDescription;
    } else if (details.length > 0) {
        informativeText = details;
    } else {
        informativeText = @"Unknown standalone startup error.";
    }

    ARPSID_UI_LOG(@"[ArpSIDHost] FATAL: %@ | %@", title, informativeText);

    NSAlert* alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleCritical;
    alert.messageText = (title.length > 0) ? title : @"ArpSID standalone startup failed";
    alert.informativeText = informativeText;
    [alert addButtonWithTitle:@"Quit"];
    [alert runModal];
    [NSApp terminate:nil];
}

// ─── NSApplicationDelegate ────────────────────────────────────────────────────

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    _softPedalSavedCutoff = -1.f;  // FIX 2.1: per-instance init (was static atomic)
    _currentPresetIndex = 0;    // per-instance default factory preset = Patch 1 / slot 0
    // Restore preferences
    NSUserDefaults* ud = [NSUserDefaults standardUserDefaults];
    _preferredDeviceName = [ud stringForKey:@"ArpSIDPreferredMIDIDevice"];
    _midiChannelFilter   = [ud integerForKey:@"ArpSIDMIDIChannel"];  // 0=omni
    [self _buildMenuBar];
    [self _instantiateAudioUnit];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)app {
    (void)app;
    return YES;
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    [self _teardownAudio];
    [self _teardownMIDI];
}

// ─── Audio Engine Setup ───────────────────────────────────────────────────────

- (void)_instantiateAudioUnit {
    AudioComponentDescription desc;
    desc.componentType         = 'aumu';
    desc.componentSubType      = 'ArpS';
    desc.componentManufacturer = 'ASID';
    desc.componentFlags        = 0;
    desc.componentFlagsMask    = 0;

    [ArpSIDAudioUnit registerSubclass:[ArpSIDAudioUnit class]
          asComponentDescription:desc
                            name:@"Uber Sound Solutions: ArpSID AUv3"
                         version:10501];

    // Create AU directly — no async completion handler needed for standalone.
    NSError* auErr = nil;
    _audioUnit = [[ArpSIDAudioUnit alloc] initWithComponentDescription:desc options:0 error:&auErr];
    if (!_audioUnit || auErr) {
        [self _presentFatalStartupErrorWithTitle:@"ArpSID standalone startup failed"
                                           error:auErr
                                         details:@"Direct ArpSIDAudioUnit initialization returned no audio unit."];
        return;
    }
    ARPSID_UI_LOG(@"[ArpSIDHost] Direct ArpSIDAudioUnit created: %@", _audioUnit);

    [self _buildWindowWithAU:_audioUnit];
    [self _setupMIDI];

    _engine = [[AVAudioEngine alloc] init];
    const double hwRate = [[_engine.outputNode outputFormatForBus:0] sampleRate];
    const double safeRate = (hwRate > 8000 && hwRate <= 384000) ? hwRate : 44100.0;
    AVAudioFormat* stereo = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:safeRate channels:2];
    [_audioUnit prepareStandaloneWithSampleRate:safeRate maxFrames:128];

    __weak ArpSIDHostAppDelegate* weakSelf = self;
    _sourceNode = [[AVAudioSourceNode alloc] initWithRenderBlock:
        ^OSStatus(BOOL* isSilence, const AudioTimeStamp* ts,
                  AVAudioFrameCount fc, AudioBufferList* outputData) {
        ArpSIDHostAppDelegate* s = weakSelf;
        if (!s || !s->_audioUnit) return noErr;
        ArpSIDDSPKernelAdapter* ad = (ArpSIDDSPKernelAdapter*)[s->_audioUnit debugAdapter];
        if (!ad) return noErr;
        [ad processWithOutputBufferList:outputData frameCount:fc timestamp:ts];
        if (isSilence) *isSilence = NO;
        return noErr;
    }];

    [_engine attachNode:_sourceNode];
    [_engine connect:_sourceNode to:_engine.mainMixerNode format:stereo];
    if (_engine.outputNode && _engine.mainMixerNode)
        [_engine connect:_engine.mainMixerNode to:_engine.outputNode format:stereo];
    [_engine prepare];
    _engine.mainMixerNode.outputVolume = 1.0f;
    NSError* startErr = nil;
    BOOL started = [_engine startAndReturnError:&startErr];
    if (!started) {
        [self _presentFatalStartupErrorWithTitle:@"ArpSID audio engine failed to start"
                                           error:startErr
                                         details:@"AVAudioEngine could not start the standalone render graph."];
        return;
    }

    ARPSID_UI_LOG(@"[ArpSIDHost] Standalone source-node engine started out=%f mixer=%f",
          [[_engine.outputNode outputFormatForBus:0] sampleRate],
          [[_engine.mainMixerNode outputFormatForBus:0] sampleRate]);

    [_viewController connectAudioUnit:_audioUnit];
    if ([_viewController respondsToSelector:@selector(updatePresentationContext:)])
        [_viewController updatePresentationContext:[self _presentationContextLine]];
    [self _updateMIDIHUD];
    // v794 — Standalone startup repaint/focus continuation must not retain the
    // app delegate after teardown. The immediate startup path has already
    // connected the AU/view; this queued AppKit polish is best-effort only.
    __weak ArpSIDHostAppDelegate* weakSelf_v794_start = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        ArpSIDHostAppDelegate* strongSelf_v794 = weakSelf_v794_start;
        if (!strongSelf_v794) return;
        if (!strongSelf_v794->_viewController || !strongSelf_v794->_mainWindow) return;
        [strongSelf_v794->_viewController.view setNeedsDisplay:YES];
        [strongSelf_v794->_mainWindow makeFirstResponder:strongSelf_v794->_viewController.view];
    });
}

- (void)_teardownAudio {
    if (_engine.isRunning) [_engine stop];
    if (_sourceNode) [_engine detachNode:_sourceNode];
    if (_avAudioUnit) [_engine detachNode:_avAudioUnit];
    _engine = nil;
    _sourceNode = nil;
    _avAudioUnit = nil;
    if (_audioUnit) [_audioUnit deallocateRenderResources];
    _audioUnit = nil;
}

// ─── Window & View ────────────────────────────────────────────────────────────

- (void)_buildWindowWithAU:(AUAudioUnit*)au {
    // ── 1. Create VC and force view load ──────────────────────────────────
    // Accessing .view triggers loadView → viewDidLoad → _buildUI.
    // The control tree is fully built BEFORE we touch AU state or the window.
    _viewController = [[ArpSIDViewController alloc] init];
    (void)_viewController.view;   // force loadView / viewDidLoad / _buildUI

    // ── 2. Build window ───────────────────────────────────────────────────
    NSRect screenRect = [[NSScreen mainScreen] visibleFrame];
    CGFloat initH = screenRect.size.height * 0.93f;
    CGFloat initW = initH * (1600.0 / 940.0);
    NSRect initRect = NSMakeRect(screenRect.origin.x + (screenRect.size.width-initW)*0.5f,
                                  screenRect.origin.y + (screenRect.size.height-initH)*0.5f,
                                  initW, initH);
    _mainWindow = [[NSWindow alloc]
        initWithContentRect:initRect // scales to screen; native 1280×720
                  styleMask:NSWindowStyleMaskTitled    |
                             NSWindowStyleMaskClosable |
                             NSWindowStyleMaskMiniaturizable |
                             NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    _mainWindow.title = [self _isAUv3WrapperPresentationApp] ? @"ArpSID AUv3 – Full Visual Host" : @"ArpSID Standalone – Full Visual Host";
    if ([_mainWindow respondsToSelector:@selector(setSubtitle:)]) _mainWindow.subtitle = [self _presentationContextLine];
    _mainWindow.releasedWhenClosed = NO;
    _mainWindow.backgroundColor = [NSColor colorWithSRGBRed:.055 green:.047 blue:.275 alpha:1];
    _mainWindow.minSize = NSMakeSize(1180, 690);   // keep the denser editor comfortably usable
    _mainWindow.delegate = self;

    // ── 3. Install VC — AppKit owns appearance lifecycle from here ─────────
    // contentViewController= is the ONLY root content assignment.
    // AppKit will call viewWillAppear / viewDidAppear naturally.
    // Do NOT also set contentView. Do NOT call viewWillAppear manually.
    _mainWindow.contentViewController = _viewController;

    // ── 4. Show ────────────────────────────────────────────────────────────
    [_mainWindow center];
    [_mainWindow makeKeyAndOrderFront:nil];
    [_mainWindow orderFrontRegardless];
    [NSApp activateIgnoringOtherApps:YES];
    // v794 — The delayed window-show polish is a weak continuation. It should
    // not retain the standalone delegate/window after teardown; if the delegate
    // is gone before the main queue runs, there is no UI left to polish.
    __weak ArpSIDHostAppDelegate* weakSelf_v794_window = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        ArpSIDHostAppDelegate* strongSelf_v794 = weakSelf_v794_window;
        if (!strongSelf_v794 || !strongSelf_v794->_mainWindow) return;
        [strongSelf_v794->_mainWindow makeKeyAndOrderFront:nil];
        [strongSelf_v794->_mainWindow orderFrontRegardless];
        [NSApp activateIgnoringOtherApps:YES];
        ARPSID_UI_LOG(@"[ArpSIDHost] window visible=%d key=%d frame=%@",
              (int)strongSelf_v794->_mainWindow.isVisible, (int)strongSelf_v794->_mainWindow.isKeyWindow,
              NSStringFromRect(strongSelf_v794->_mainWindow.frame));
    });

    // ── 5. Connect AU — view is built, window is live ──────────────────────
    // connectAudioUnit: only wires observers and syncs knob state.
    // The control tree already exists from step 1.
    if ([_viewController respondsToSelector:@selector(updatePresentationContext:)])
        [_viewController updatePresentationContext:[self _presentationContextLine]];
    if (au) [_viewController connectAudioUnit:au];
    else ARPSID_UI_LOG(@"[ArpSIDHost] UI created before AU ready; deferring connectAudioUnit");
}

// ─── Window Resize ───────────────────────────────────────────────────────────

- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    // viewDidLayout on the VC fires automatically when the window resizes
    // because contentViewController's view autoresizes. Nothing extra needed.
}

- (NSSize)windowWillResize:(NSWindow*)sender toSize:(NSSize)frameSize {
    (void)sender;
    // Enforce 11:7 aspect ratio (1060:680 = ~1.558:1)
    const CGFloat aspect = 1600.0 / 940.0;  // match the larger native editor canvas
    const CGFloat contentH = frameSize.height - 28.0;  // approx title bar
    frameSize.width = contentH * aspect + 0.0;
    return frameSize;
}

// ─── CoreMIDI ─────────────────────────────────────────────────────────────────

// ─── MIDI source display name helper ─────────────────────────────────────────

static NSString* ArpSIDEndpointName(MIDIEndpointRef ep) {
    CFStringRef name = nil;
    // Try display name first (includes device name for USB keyboards)
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &name) != noErr || !name)
        MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &name);
    return name ? CFBridgingRelease(name) : @"Unknown MIDI Device";
}

// ─── CoreMIDI hot-plug notification ──────────────────────────────────────────

static void ArpSIDMIDINotifyProc(const MIDINotification* msg, void* refCon) {
    // Only care about add/remove events — bounce to main queue. The refCon is a
    // retained weak-box, not the delegate object itself. v790: keep the
    // continuation weak as well, so a hot-plug notification cannot retain the
    // standalone delegate after teardown while waiting on the main queue.
    if (msg->messageID == kMIDIMsgObjectAdded || msg->messageID == kMIDIMsgObjectRemoved) {
        __weak ArpSIDHostAppDelegate* weakDelegate = ArpSIDHostDelegateFromMIDIContext_v778(refCon);
        dispatch_async(dispatch_get_main_queue(), ^{
            ArpSIDHostAppDelegate* strongDelegate = weakDelegate;
            if (!strongDelegate) return;
            [strongDelegate _reconnectMIDISources];
        });
    }
}

- (void)_setupMIDI {
    if (_midiClient || _midiInputPort) { ARPSID_UI_LOG(@"[ArpSIDHost] _setupMIDI already initialized"); [self _reconnectMIDISources]; return; }
    _midiSourceNames = [NSMutableArray array];

    // Use notification proc for hot-plug support. v778: CoreMIDI receives a
    // retained weak-box refCon instead of a raw, unretained delegate pointer.
    ArpSIDHostMIDIContext_v778* midiContext = [ArpSIDHostMIDIContext_v778 new];
    midiContext.delegate = self;
    _midiCallbackContext_v778 = (__bridge_retained void*)midiContext;

    OSStatus status = MIDIClientCreate(CFSTR("ArpSIDHost"),
                                       ArpSIDMIDINotifyProc,
                                       _midiCallbackContext_v778,
                                       &_midiClient);
    if (status != noErr) {
        ARPSID_UI_LOG(@"[ArpSIDHost] MIDIClientCreate failed: %d", (int)status);
        ArpSIDReleaseRetainedMIDIContext_v778(_midiCallbackContext_v778);
        return;
    }

    status = MIDIInputPortCreate(_midiClient,
                                 CFSTR("ArpSIDInput"),
                                 ArpSIDMIDIReadProc,
                                 _midiCallbackContext_v778,
                                 &_midiInputPort);
    if (status != noErr) {
        ARPSID_UI_LOG(@"[ArpSIDHost] MIDIInputPortCreate failed: %d", (int)status);
        if (_midiClient) { MIDIClientDispose(_midiClient); _midiClient = 0; }
        ArpSIDReleaseRetainedMIDIContext_v778(_midiCallbackContext_v778);
        return;
    }

    // Create a virtual destination so DAWs / apps can route MIDI to ArpSID
    status = MIDIDestinationCreate(_midiClient,
                                   CFSTR("ArpSID"),
                                   ArpSIDMIDIReadProc,
                                   _midiCallbackContext_v778,
                                   &_midiDest);
    if (status != noErr)
        ARPSID_UI_LOG(@"[ArpSIDHost] MIDIDestinationCreate failed: %d", (int)status);

    // Connect all currently-present sources
    [self _reconnectMIDISources];
}

/// Called on launch and on every hot-plug event.
/// Disconnects all sources, re-enumerates, reconnects, updates HUD.
- (void)_reconnectMIDISources {
    if (!_midiInputPort) return;

    // Fix: explicitly disconnect ALL existing sources before reconnecting.
    // Without this, hot-plug/reconnect delivers duplicate packets per source.
    if (_midiEndpoints) {
        for (NSNumber* epNum in _midiEndpoints) {
            MIDIEndpointRef ep = (MIDIEndpointRef)(uintptr_t)epNum.unsignedIntegerValue;
            MIDIPortDisconnectSource(_midiInputPort, ep);
        }
    }
    if (!_midiEndpoints) _midiEndpoints = [NSMutableArray array];
    [_midiSourceNames removeAllObjects];
    [_midiEndpoints   removeAllObjects];

    const ItemCount srcCount = MIDIGetNumberOfSources();
    ARPSID_UI_LOG(@"[ArpSIDHost] MIDI source scan: %lu source(s)", (unsigned long)srcCount);
    for (ItemCount i = 0; i < srcCount; ++i) {
        MIDIEndpointRef ep = MIDIGetSource(i);
        NSString* name = ArpSIDEndpointName(ep);
        if ([name isEqualToString:@"ArpSID"]) {
            ARPSID_UI_LOG(@"[ArpSIDHost] Skipping self MIDI endpoint: %@", name);
            continue;
        }
        const OSStatus cst = MIDIPortConnectSource(_midiInputPort, ep, (void*)(uintptr_t)ep);
        if (cst != noErr) {
            ARPSID_UI_LOG(@"[ArpSIDHost] MIDIPortConnectSource failed for %@: %d", name, (int)cst);
            continue;
        }
        [_midiSourceNames addObject:name];
        [_midiEndpoints   addObject:@((NSUInteger)(uintptr_t)ep)];
        ARPSID_UI_LOG(@"[ArpSIDHost] MIDI source connected: %@", name);
    }

    if (_preferredDeviceName && ![_midiSourceNames containsObject:_preferredDeviceName]) {
        ARPSID_UI_LOG(@"[ArpSIDHost] preferred MIDI device '%@' no longer present; clearing filter", _preferredDeviceName);
        _preferredDeviceName = nil;
        [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"ArpSIDPreferredMIDIDevice"];
    }

    [self _rebuildMIDIMenuItems];
    [self _updateMIDIHUD];
}

/// Push current source list + activity state to the ViewController HUD.
- (void)_updateMIDIHUD {
    NSString* label;
    BOOL active = _midiActivity;
    if (_midiSourceNames.count == 0) {
        label = @"No MIDI input";
    } else if (_preferredDeviceName) {
        // Preferred device selected — show it by name with channel info
        NSString* chStr = (_midiChannelFilter > 0)
            ? [NSString stringWithFormat:@" ch%ld", (long)_midiChannelFilter]
            : @"";
        label = [NSString stringWithFormat:@"%@%@", _preferredDeviceName, chStr];
    } else if (_midiSourceNames.count == 1) {
        NSString* chStr = (_midiChannelFilter > 0)
            ? [NSString stringWithFormat:@" ch%ld", (long)_midiChannelFilter]
            : @"";
        label = [_midiSourceNames[0] stringByAppendingString:chStr];
    } else {
        NSString* chStr = (_midiChannelFilter > 0)
            ? [NSString stringWithFormat:@" ch%ld", (long)_midiChannelFilter]
            : @"";
        label = [NSString stringWithFormat:@"%lu keyboards%@",
                 (unsigned long)_midiSourceNames.count, chStr];
    }
    [_viewController updateMIDIStatus:label activity:active];
}

/// Called from the MIDI read proc after any MIDI packet arrives.
/// Lights the activity LED for 80 ms then extinguishes it.
- (void)_flashMIDIActivity {
    _midiActivity = YES;
    [self _updateMIDIHUD];
    [_midiActivityTimer invalidate];
    _midiActivityTimer = [NSTimer scheduledTimerWithTimeInterval:0.08
        target:self selector:@selector(_clearMIDIActivity) userInfo:nil repeats:NO];
}

- (void)_clearMIDIActivity {
    _midiActivity = NO;
    [self _updateMIDIHUD];
}

- (void)_teardownMIDI {
    // Null-safe teardown: guards against calling twice or before init
    [_midiActivityTimer invalidate];
    _midiActivityTimer = nil;
    if (_midiDest)       { MIDIEndpointDispose(_midiDest);     _midiDest = 0;      }
    if (_midiInputPort)  { MIDIPortDispose(_midiInputPort);    _midiInputPort = 0; }
    if (_midiClient)     { MIDIClientDispose(_midiClient);     _midiClient = 0;    }
    ArpSIDReleaseRetainedMIDIContext_v778(_midiCallbackContext_v778);
}

/// Called from the CoreMIDI read proc (any thread).
/// All UI/AU writes are bounced to the main queue.
- (void)_handleMIDIPacketList:(const MIDIPacketList*)pktList fromSource:(MIDIEndpointRef)srcEp {
    // v784: CoreMIDI callbacks enter on system threads; the read proc already
    // weak-loads the delegate, but any main-queue UI/control continuations must
    // not retain the standalone delegate after teardown. Capture a weak token and
    // strong-load only on the main queue.
    __weak ArpSIDHostAppDelegate* weakSelf = self;
    NSString* __strong preferredDevice = _preferredDeviceName;
    // Device filter: reject packets from non-preferred sources.
    // srcEp==0 = virtual destination, always pass.
    if (srcEp != 0 && preferredDevice) {
        NSString* srcName = ArpSIDEndpointName(srcEp);
        if (![srcName isEqualToString:preferredDevice]) { ARPSID_UI_LOG(@"[ArpSIDHost] MIDI filtered out from %@ (preferred=%@)", srcName, preferredDevice); return; }
    }
    // Note: MIDI activity flash happens per-event inside the loop below (fix finding 22)
    const NSInteger chFilter = _midiChannelFilter;  // snapshot for this thread
    const MIDIPacket* pkt = &pktList->packet[0];
    for (UInt32 i = 0; i < pktList->numPackets; ++i) {
        if (pkt->length >= 1 && pkt->length <= 256) {  // guard absurd lengths
            // Channel filter: 0=omni, 1-16=specific channel
            if (!ArpSIDShouldForwardPacket(pkt->data[0], chFilter)) {
                pkt = MIDIPacketNext(pkt);
                continue;
            }
            ARPSID_UI_LOG(@"[ArpSIDHost] MIDI packet len=%u status=0x%02x", (unsigned)pkt->length, pkt->data[0]);
            const uint8_t status = pkt->data[0] & 0xF0u;
            const uint8_t ch     = pkt->data[0] & 0x0Fu;
            switch (status) {

                case 0x90:   // Note On (vel=0 treated as Note Off)
                case 0x80: { // Note Off
                    if (pkt->length < 2) break;
                    // Guard: note number must be 0-127
                    if ((pkt->data[1] & 0x80u) != 0) break;  // high bit = invalid
                    // Direct injection — thread-safe, no dispatch needed
                    if (!self->_audioUnit) { ARPSID_UI_LOG(@"[ArpSIDHost] Note event dropped: audioUnit=nil"); break; }
                    [self->_audioUnit injectMIDIBytes:pkt->data length:pkt->length];
                    dispatch_async(dispatch_get_main_queue(), ^{
                        ArpSIDHostAppDelegate* strongSelf = weakSelf;
                        if (!strongSelf) return;
                        [strongSelf _flashMIDIActivity];
                    });
                    break;
                }

                case 0xB0: {  // Control Change — single path via injectMIDIBytes
                    if (pkt->length < 3) break;
                    // Fix: removed duplicate dispatch_async param path (claim 4).
                    // The kernel's handleCC() handles all standard CCs including
                    // mod wheel (→ kParamHostCtrlModWheelBase), sustain, sostenuto, etc.
                    // Special host-UI bridges for CC65 and CC67 handled inline:
                    const uint8_t cc2 = pkt->data[1]; const uint8_t val2 = pkt->data[2];
                    if (cc2 == 65) {  // CC65 = portamento switch ON/OFF
                        const float enable = (val2 >= 64) ? 1.f : 0.f;
                        dispatch_async(dispatch_get_main_queue(), ^{
                            ArpSIDHostAppDelegate* strongSelf = weakSelf;
                            if (!strongSelf || !strongSelf->_audioUnit) return;
                            [strongSelf->_audioUnit setParameterValue:enable forID:ArpSID::kParamPortamentoTime];
                        });
                    } else if (cc2 == 67) {  // CC67 = soft pedal: save/restore filter cutoff
                        // FIX 2.1: was static std::atomic<float> — bleed across instances.
                        // Now uses per-instance ivar _softPedalSavedCutoff.
                        const BOOL pedalDown = (val2 >= 64);
                        dispatch_async(dispatch_get_main_queue(), ^{
                            ArpSIDHostAppDelegate* strongSelf = weakSelf;
                            if (!strongSelf || !strongSelf->_audioUnit) return;
                            if (pedalDown) {
                                const float saved = [strongSelf->_audioUnit getParameterValue:ArpSID::kParamFilterCutoff];
                                strongSelf->_softPedalSavedCutoff = saved;
                                [strongSelf->_audioUnit setParameterValue:std::max(0.f, saved - 0.15f)
                                                                    forID:ArpSID::kParamFilterCutoff];
                            } else {
                                if (strongSelf->_softPedalSavedCutoff >= 0.f) {
                                    [strongSelf->_audioUnit setParameterValue:strongSelf->_softPedalSavedCutoff
                                                                        forID:ArpSID::kParamFilterCutoff];
                                    strongSelf->_softPedalSavedCutoff = -1.f;
                                }
                            }
                        });
                    }
                    if (!self->_audioUnit) { ARPSID_UI_LOG(@"[ArpSIDHost] MIDI CC dropped: audioUnit=nil status=0x%02x", pkt->data[0]); break; }
                    [self->_audioUnit injectMIDIBytes:pkt->data length:pkt->length];
                    break;
                }

                case 0xD0:   // Channel Pressure
                case 0xA0: { // Poly Aftertouch
                    if (pkt->length < 2) break;
                    // Canonical path: injectMIDIBytes → kernel ring buffer → dispatchEvent_ via processBlock
                    [self->_audioUnit injectMIDIBytes:pkt->data length:pkt->length];
                    break;
                }

                case 0xC0: {  // Program Change — single path via inject (fix 16)
                    if(pkt->length < 2) break;
                    // Canonical path: injectMIDIBytes → kernel ring buffer → dispatchEvent_ via processBlock
                    [self->_audioUnit injectMIDIBytes:pkt->data length:pkt->length];
                    break;
                }

                case 0xE0: {  // Pitch Bend
                    if (pkt->length < 3) break;
                    // Canonical path: injectMIDIBytes → kernel ring buffer → dispatchEvent_ via processBlock
                    [self->_audioUnit injectMIDIBytes:pkt->data length:pkt->length];
                    break;
                }

                default:
                    (void)ch;
                    break;
            }
        }
        pkt = MIDIPacketNext(pkt);
    }
}

// ─── Menu Bar ─────────────────────────────────────────────────────────────────

- (void)_buildMenuBar {
    NSMenu* menuBar = [[NSMenu alloc] init];
    [NSApp setMainMenu:menuBar];

    // ── App menu ──────────────────────────────────────────────────────────
    NSMenuItem* appItem = [[NSMenuItem alloc] init];
    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"About ArpSID"
                       action:@selector(_menuAbout:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Quit ArpSID"
                       action:@selector(terminate:) keyEquivalent:@"q"];
    appItem.submenu = appMenu;
    [menuBar addItem:appItem];

    // ── ArpSID menu ───────────────────────────────────────────────────────
    NSMenuItem* sidItem = [[NSMenuItem alloc] init];
    NSMenu* sidMenu = [[NSMenu alloc] initWithTitle:@"ArpSID"];
    [sidMenu addItemWithTitle:@"Panic (All Notes Off)"
                       action:@selector(_menuPanic:) keyEquivalent:@"p"];
    [sidMenu addItem:[NSMenuItem separatorItem]];
    [sidMenu addItemWithTitle:@"Next Preset"
                       action:@selector(_menuNextPreset:) keyEquivalent:@"]"];
    [sidMenu addItemWithTitle:@"Previous Preset"
                       action:@selector(_menuPrevPreset:) keyEquivalent:@"["];
    [sidMenu addItem:[NSMenuItem separatorItem]];
    [sidMenu addItemWithTitle:@"Save User Preset…"
                       action:@selector(_menuSaveUserPreset:) keyEquivalent:@"s"];
    [sidMenu addItemWithTitle:@"Export Preset to File…"
                       action:@selector(_menuExportPreset:) keyEquivalent:@"e"];
    [sidMenu addItemWithTitle:@"Import Preset from File…"
                       action:@selector(_menuImportPreset:) keyEquivalent:@"i"];
    [sidMenu addItem:[NSMenuItem separatorItem]];
    [sidMenu addItemWithTitle:@"Reset All Parameters"
                       action:@selector(_menuReset:) keyEquivalent:@"0"];
    [sidMenu addItemWithTitle:@"Random Patch"
                       action:@selector(_menuRandomPatch:) keyEquivalent:@"r"];
    sidItem.submenu = sidMenu;
    [menuBar addItem:sidItem];

    // ── MIDI menu ─────────────────────────────────────────────────────────
    NSMenuItem* midiItem = [[NSMenuItem alloc] init];
    _midiMenu = [[NSMenu alloc] initWithTitle:@"MIDI"];

    // Input device submenu (populated dynamically on hot-plug)
    _midiDeviceParent = [_midiMenu addItemWithTitle:@"Input Device"
                                             action:nil keyEquivalent:@""];
    NSMenu* devMenu = [[NSMenu alloc] initWithTitle:@"Input Device"];
    NSMenuItem* omniItem = [[NSMenuItem alloc]
        initWithTitle:@"All Devices (Omni)"
               action:@selector(_menuSelectMIDIDevice:) keyEquivalent:@""];
    omniItem.tag = -1;
    omniItem.state = (!_preferredDeviceName) ? NSControlStateValueOn : NSControlStateValueOff;
    [devMenu addItem:omniItem];
    [devMenu addItem:[NSMenuItem separatorItem]];
    _midiDeviceParent.submenu = devMenu;

    // Channel filter submenu
    _midiChannelParent = [_midiMenu addItemWithTitle:@"MIDI Channel"
                                              action:nil keyEquivalent:@""];
    NSMenu* chMenu = [[NSMenu alloc] initWithTitle:@"MIDI Channel"];
    NSMenuItem* omniCh = [[NSMenuItem alloc]
        initWithTitle:@"All Channels (Omni)"
               action:@selector(_menuSelectMIDIChannel:) keyEquivalent:@""];
    omniCh.tag = 0;
    omniCh.state = (_midiChannelFilter == 0) ? NSControlStateValueOn : NSControlStateValueOff;
    [chMenu addItem:omniCh];
    [chMenu addItem:[NSMenuItem separatorItem]];
    for (int c = 1; c <= 16; ++c) {
        NSMenuItem* ci = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithFormat:@"Channel %d", c]
                   action:@selector(_menuSelectMIDIChannel:) keyEquivalent:@""];
        ci.tag = c;
        ci.state = (_midiChannelFilter == c)
            ? NSControlStateValueOn : NSControlStateValueOff;
        [chMenu addItem:ci];
    }
    _midiChannelParent.submenu = chMenu;

    [_midiMenu addItem:[NSMenuItem separatorItem]];
    [_midiMenu addItemWithTitle:@"Reconnect All Sources"
                         action:@selector(_menuReconnectMIDI:) keyEquivalent:@"r"];
    midiItem.submenu = _midiMenu;
    [menuBar addItem:midiItem];

    // ── View menu — full tab roster (v586) ───────────────────────────────────
    // Tags are ArpSIDTab enum values (not display-segment indices) so that
    // selectPresentationTabIndex: resolves each entry correctly regardless of
    // which component flavor is loaded.
    // ArpSIDTabSidProjection (2) is a legacy compat slot and is intentionally
    // absent. New tabs 13–18 were absent from the pre-v586 menu.
    NSMenuItem* viewItem = [[NSMenuItem alloc] initWithTitle:@"View" action:nil keyEquivalent:@""];
    NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];

    // ⌘1-⌘9: nine core tabs with keyboard shortcuts (ordered by enum value).
    struct { const char* title; NSInteger tag; const char* key; } coreEntries[] = {
        { "MAIN",     0,  "1" },   // ArpSIDTabMain
        { "LFO/ARP",  1,  "2" },   // ArpSIDTabLfoArp
        { "SID REG",  3,  "3" },   // ArpSIDTabSidReg
        { "SEQ",      4,  "4" },   // ArpSIDTabSeq
        { "FILTER",   5,  "5" },   // ArpSIDTabFilter
        { "MACRO",    6,  "6" },   // ArpSIDTabMacro
        { "FORENSIC", 7,  "7" },   // ArpSIDTabForensic
        { "SIDCORE",  8,  "8" },   // ArpSIDTabSidCore
        { "BANK",     9,  "9" },   // ArpSIDTabBank
    };
    for (const auto& e : coreEntries) {
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithUTF8String:e.title]
                   action:@selector(_menuSelectTab:)
            keyEquivalent:[NSString stringWithUTF8String:e.key]];
        item.keyEquivalentModifierMask = NSEventModifierFlagCommand;
        item.tag = e.tag;
        [viewMenu addItem:item];
    }
    [viewMenu addItem:[NSMenuItem separatorItem]];

    // Legacy + extended tabs (no keyboard shortcut — too many to number).
    struct { const char* title; NSInteger tag; } extEntries[] = {
        { "OPTS",      10 },   // ArpSIDTabOptions
        { "C64",       11 },   // ArpSIDTabC64
        { "HI-FI",     12 },   // ArpSIDTabHiFi
        { "DRSID",     13 },   // ArpSIDTabDrsid
    };
    for (const auto& e : extEntries) {
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithUTF8String:e.title]
                   action:@selector(_menuSelectTab:)
            keyEquivalent:@""];
        item.tag = e.tag;
        [viewMenu addItem:item];
    }
    [viewMenu addItem:[NSMenuItem separatorItem]];

    // New v544–v564 tabs.
    struct { const char* title; NSInteger tag; } newEntries[] = {
        { "SETTINGS",  14 },   // ArpSIDTabSettingsV544
        { "MIX",       16 },   // ArpSIDTabMixV547
        { "KIT EDIT",  17 },   // ArpSIDTabKitV555
        { "DIGI",      18 },   // ArpSIDTabDigiV563
    };
    for (const auto& e : newEntries) {
        NSMenuItem* item = [[NSMenuItem alloc]
            initWithTitle:[NSString stringWithUTF8String:e.title]
                   action:@selector(_menuSelectTab:)
            keyEquivalent:@""];
        item.tag = e.tag;
        [viewMenu addItem:item];
    }
    [viewMenu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* fs = [[NSMenuItem alloc] initWithTitle:@"Toggle Full Screen"
                                                action:@selector(toggleFullScreen:)
                                         keyEquivalent:@"f"];
    fs.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
    [viewMenu addItem:fs];
    viewItem.submenu = viewMenu;
    [menuBar addItem:viewItem];
}


- (void)_menuSelectTab:(NSMenuItem*)item {
    if (_viewController && [_viewController respondsToSelector:@selector(selectPresentationTabIndex:)]) {
        [_viewController selectPresentationTabIndex:item.tag];
    }
}

- (void)_menuAbout:(id)sender {
    (void)sender;
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = @"ArpSID AUv3";
    a.informativeText = @"MOS 6581/8580 SID synthesiser\nVersion 1.5.1\n© 2024-2026 Ulf Bertilsson";
    [a runModal];
}

/// Called when user picks a device from the MIDI > Input Device menu.
- (void)_menuSelectMIDIDevice:(NSMenuItem*)item {
    const BOOL isOmni = (item.tag == -1);
    _preferredDeviceName = isOmni ? nil : item.title;

    // Persist
    NSUserDefaults* ud = [NSUserDefaults standardUserDefaults];
    if (_preferredDeviceName)
        [ud setObject:_preferredDeviceName forKey:@"ArpSIDPreferredMIDIDevice"];
    else
        [ud removeObjectForKey:@"ArpSIDPreferredMIDIDevice"];
    [ud synchronize];

    [self _rebuildMIDIMenuItems];
    [self _updateMIDIHUD];
}

/// Called when user picks a channel from the MIDI > Channel menu.
- (void)_menuSelectMIDIChannel:(NSMenuItem*)item {
    _midiChannelFilter = item.tag;
    [[NSUserDefaults standardUserDefaults] setInteger:_midiChannelFilter
                                               forKey:@"ArpSIDMIDIChannel"];
    // Update checkmarks
    for (NSMenuItem* ci in _midiChannelParent.submenu.itemArray)
        ci.state = (ci.tag == _midiChannelFilter)
            ? NSControlStateValueOn : NSControlStateValueOff;
    [self _updateMIDIHUD];
}

- (void)_menuReconnectMIDI:(id)sender {
    (void)sender;
    [self _reconnectMIDISources];
}

/// Rebuild the device submenu to reflect current sources + preferred selection.
- (void)_rebuildMIDIMenuItems {
    if (!_midiDeviceParent) return;
    NSMenu* devMenu = _midiDeviceParent.submenu;
    // Remove all items after the first two (Omni + separator)
    while (devMenu.numberOfItems > 2)
        [devMenu removeItemAtIndex:2];

    for (NSUInteger i = 0; i < _midiSourceNames.count; ++i) {
        NSString* name = _midiSourceNames[i];
        NSMenuItem* mi = [[NSMenuItem alloc]
            initWithTitle:name
                   action:@selector(_menuSelectMIDIDevice:)
            keyEquivalent:@""];
        mi.tag = (NSInteger)i;
        mi.state = ([name isEqualToString:_preferredDeviceName])
            ? NSControlStateValueOn : NSControlStateValueOff;
        [devMenu addItem:mi];
    }
    // Update Omni checkmark
    devMenu.itemArray[0].state = (!_preferredDeviceName)
        ? NSControlStateValueOn : NSControlStateValueOff;
}


// ─── File / Preset operations ─────────────────────────────────────────────────

- (void)_menuSaveUserPreset:(id)sender {
    (void)sender;
    if (!_audioUnit) return;
    // Ask user for a name via text input alert
    NSAlert* a = [[NSAlert alloc] init];
    a.messageText = @"Save User Preset";
    a.informativeText = @"Enter a name for the preset:";
    NSTextField* tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0,0,240,22)];
    tf.stringValue = [NSString stringWithFormat:@"My Preset %ld", (long)(((NSInteger)[[NSDate date] timeIntervalSince1970]) % 10000)];
    a.accessoryView = tf;
    [a addButtonWithTitle:@"Save"];
    [a addButtonWithTitle:@"Cancel"];
    if ([a runModal] != NSAlertFirstButtonReturn) return;
    AUAudioUnitPreset* p = [[AUAudioUnitPreset alloc] init];
    p.name = tf.stringValue.length ? tf.stringValue : @"User Preset";
    p.number = -((NSInteger)(((NSInteger)[[NSDate date] timeIntervalSince1970]) % 100000) + 1);
    NSError* err = nil;
    if ([_audioUnit saveUserPreset:p error:&err]) {
        [_viewController refreshPresets];
        ARPSID_UI_LOG(@"[ArpSID] Saved user preset: %@", p.name);
    } else {
        ARPSID_UI_LOG(@"[ArpSID] Save preset failed: %@", err);
    }
}

- (void)_menuExportPreset:(id)sender {
    (void)sender;
    if (!_audioUnit) return;
    NSSavePanel* sp = [NSSavePanel savePanel];
    sp.nameFieldStringValue = @"ArpSIDPatch.aupreset";
    sp.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"aupreset"],
                                [UTType typeWithFilenameExtension:@"json"]];
    sp.title = @"Export ArpSID Preset";
    __weak ArpSIDHostAppDelegate* weakSelf = self;
    [sp beginSheetModalForWindow:_mainWindow completionHandler:^(NSModalResponse r){
        NSURL* exportURL = (r == NSModalResponseOK) ? [sp.URL copy] : nil;
        NSString* exportExtension = [exportURL.pathExtension.lowercaseString copy];
        if (!exportURL) return;
        ArpSIDHostAppDelegate* strongSelf = weakSelf;
        if (!strongSelf || !strongSelf->_audioUnit) return;
        NSDictionary* state = [strongSelf->_audioUnit fullState];
        NSError* err = nil;
        NSData* data = nil;
        // Fix finding 26 + v783: snapshot panel URL/extension before using it and
        // weak-load the standalone delegate instead of retaining it through the
        // AppKit completion block.
        if ([exportExtension isEqualToString:@"json"]) {
            data = [NSJSONSerialization dataWithJSONObject:state
                options:NSJSONWritingPrettyPrinted error:&err];
        } else {
            data = [NSPropertyListSerialization dataWithPropertyList:state
                format:NSPropertyListXMLFormat_v1_0 options:0 error:&err];
        }
        if (data) [data writeToURL:exportURL atomically:YES];
        else if (err) ARPSID_UI_LOG(@"[ArpSID] Export failed: %@", err);
    }];
}

- (void)_menuImportPreset:(id)sender {
    (void)sender;
    if (!_audioUnit) return;
    NSOpenPanel* op = [NSOpenPanel openPanel];
    op.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"aupreset"],
                                [UTType typeWithFilenameExtension:@"json"]];
    op.title = @"Import ArpSID Preset";
    __weak ArpSIDHostAppDelegate* weakSelf = self;
    [op beginSheetModalForWindow:_mainWindow completionHandler:^(NSModalResponse r){
        NSURL* importURL = (r == NSModalResponseOK) ? [op.URL copy] : nil;
        if (!importURL) return;
        NSData* data = [NSData dataWithContentsOfURL:importURL];
        if (!data) return;
        NSError* err2 = nil;
        id parsed = nil;
        // Fix finding 27 + v783: snapshot panel URL before IO and weak-load the
        // standalone delegate before publishing imported state.
        parsed = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if (!parsed)
            parsed = [NSPropertyListSerialization propertyListWithData:data
                options:NSPropertyListImmutable format:nil error:&err2];
        ArpSIDHostAppDelegate* strongSelf = weakSelf;
        if (!strongSelf || !strongSelf->_audioUnit) return;
        if ([parsed isKindOfClass:[NSDictionary class]]) {
            NSDictionary* d = (NSDictionary*)parsed;
            // Validate: must have ArpSIDStateVersion or ArpSIDDocumentCurrentPreset key
            BOOL valid = (d[@"ArpSIDStateVersion"] != nil || d[@"ArpSIDDocumentCurrentPreset"] != nil
                          || d[@"ArpSIDStateParams"] != nil);
            if (valid)
                [strongSelf->_audioUnit setFullState:d];
            else
                ARPSID_UI_LOG(@"[ArpSID] Import rejected: missing schema keys");
        } else {
            ARPSID_UI_LOG(@"[ArpSID] Import failed: not a dictionary (%@)", err2);
        }
    }];
}

- (void)_menuReset:(id)sender {
    (void)sender;
    if (!_audioUnit) return;
    for (int i=0; i<ArpSID::kNumParams; ++i)
        [_audioUnit setParameterValue:ArpSID::kParamInfos[(size_t)i].defaultNorm forID:i];
}

- (void)_menuRandomPatch:(id)sender {
    (void)sender;
    if (!_audioUnit) return;
    // Randomise tonal parameters only — keep transport/system params unchanged
    static const int kToRandomise[] = {
        ArpSID::kParamVCO1Waveform, ArpSID::kParamVCO1PulseWidth, ArpSID::kParamVCO1Detune,
        ArpSID::kParamVCO2Waveform, ArpSID::kParamVCO2PulseWidth, ArpSID::kParamVCO2Detune,
        ArpSID::kParamVCO3Waveform, ArpSID::kParamVCO3PulseWidth, ArpSID::kParamVCO3Detune,
        ArpSID::kParamFilterCutoff, ArpSID::kParamFilterResonance, ArpSID::kParamFilterMode,
        ArpSID::kParamAttack, ArpSID::kParamDecay, ArpSID::kParamSustain, ArpSID::kParamRelease,
        ArpSID::kParamVCO1SyncEnable, ArpSID::kParamVCO1RingModEnable,
        ArpSID::kParamVCO2SyncEnable, ArpSID::kParamVCO2RingModEnable,
    };
    for (int pid : kToRandomise)
        [_audioUnit setParameterValue:(float)arc4random_uniform(1000)/1000.f forID:pid];
}

- (void)_menuPanic:(id)sender {
    (void)sender;
    if (_audioUnit)
        [_audioUnit setParameterValue:1.f forID:ArpSID::kParamPanic];
}

static const NSInteger kNumFactoryPresets =
    (NSInteger)ArpSID::kCanonicalFactoryPatchSlotCount;

- (void)_menuNextPreset:(id)sender {
    (void)sender;
    _currentPresetIndex = (_currentPresetIndex + 1) % kNumFactoryPresets;
    if (_audioUnit) {
        AUAudioUnitPreset* p = [[AUAudioUnitPreset alloc] init];
        p.number = _currentPresetIndex;
        p.name   = @"";
        __weak ArpSIDHostAppDelegate* weakSelf = self;
        dispatch_async(dispatch_get_main_queue(), ^{
            ArpSIDHostAppDelegate* strongSelf = weakSelf;
            if (!strongSelf || !strongSelf->_audioUnit) return;
            [strongSelf->_audioUnit setCurrentPreset:p];
        });
    }
}

- (void)_menuPrevPreset:(id)sender {
    (void)sender;
    _currentPresetIndex = (_currentPresetIndex - 1 + kNumFactoryPresets) % kNumFactoryPresets;
    if (_audioUnit) {
        AUAudioUnitPreset* p = [[AUAudioUnitPreset alloc] init];
        p.number = _currentPresetIndex;
        p.name   = @"";
        __weak ArpSIDHostAppDelegate* weakSelf = self;
        dispatch_async(dispatch_get_main_queue(), ^{
            ArpSIDHostAppDelegate* strongSelf = weakSelf;
            if (!strongSelf || !strongSelf->_audioUnit) return;
            [strongSelf->_audioUnit setCurrentPreset:p];
        });
    }
}

@end


// ─── CoreMIDI Read Proc ───────────────────────────────────────────────────────
// Called from CoreMIDI thread — must be async-safe. We bounce to main queue
// inside _handleMIDIPacketList:.

// Returns YES if the MIDI status byte passes the channel filter.
// channelFilter==0 means omni (pass all); otherwise only pass the matching channel.
static void ArpSIDMIDIReadProc(const MIDIPacketList* pktList,
                                void* readProcRefCon,
                                void* srcConnRefCon) {
    ArpSIDHostAppDelegate* delegate = ArpSIDHostDelegateFromMIDIContext_v778(readProcRefCon);
    if (!delegate) return;
    // Pass source endpoint so the delegate can filter by preferred device
    const MIDIEndpointRef srcEp = (MIDIEndpointRef)(uintptr_t)srcConnRefCon;
    [delegate _handleMIDIPacketList:pktList fromSource:srcEp];
}
