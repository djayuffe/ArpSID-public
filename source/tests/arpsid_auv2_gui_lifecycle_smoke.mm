// SPDX-License-Identifier: BSD-3-Clause
// Runtime smoke for AUv2 raw-NSView reuse after a host detach/reopen cycle.

#import <AppKit/AppKit.h>
#import <AudioUnit/AudioUnit.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <objc/message.h>

#include <cstdint>
#include <cstdio>
#include <vector>

static constexpr OSType kType = 'aumu';
static constexpr OSType kSubtype = 'ArpS';
static constexpr OSType kManufacturer = 'ASID';

static int fail(const char* message, OSStatus status = noErr) {
    if (status == noErr) std::fprintf(stderr, "FAIL: %s\n", message);
    else std::fprintf(stderr, "FAIL: %s (OSStatus %d)\n", message, (int)status);
    return 1;
}

static NSResponder* lifecycleResponderForView(NSView* view) {
    for (NSResponder* responder = view.nextResponder;
         responder != nil;
         responder = responder.nextResponder) {
        if ([responder respondsToSelector:@selector(pauseEditorViewForHostDetach)] &&
            [responder respondsToSelector:@selector(prepareForFinalEditorDisposal)]) {
            return responder;
        }
    }
    return nil;
}

static void pumpMainRunLoop(NSTimeInterval seconds) {
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([deadline timeIntervalSinceNow] > 0.0) {
        [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode
                              beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
}

int main() {
    @autoreleasepool {
        [NSApplication sharedApplication];

        AudioComponentDescription desc{};
        desc.componentType = kType;
        desc.componentSubType = kSubtype;
        desc.componentManufacturer = kManufacturer;
        AudioComponent component = AudioComponentFindNext(nullptr, &desc);
        if (!component) return fail("installed ArpSID AUv2 was not discovered");

        AudioUnit audioUnit = nullptr;
        OSStatus status = AudioComponentInstanceNew(component, &audioUnit);
        if (status != noErr || !audioUnit) return fail("could not instantiate ArpSID AUv2", status);

        UInt32 infoSize = 0;
        Boolean writable = false;
        status = AudioUnitGetPropertyInfo(audioUnit, kAudioUnitProperty_CocoaUI,
                                          kAudioUnitScope_Global, 0,
                                          &infoSize, &writable);
        if (status != noErr || infoSize < sizeof(AudioUnitCocoaViewInfo)) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("Cocoa UI property is unavailable", status);
        }

        std::vector<std::uint8_t> storage(infoSize);
        status = AudioUnitGetProperty(audioUnit, kAudioUnitProperty_CocoaUI,
                                      kAudioUnitScope_Global, 0,
                                      storage.data(), &infoSize);
        if (status != noErr) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("could not read Cocoa UI property", status);
        }

        auto* info = reinterpret_cast<AudioUnitCocoaViewInfo*>(storage.data());
        NSURL* bundleURL = (__bridge NSURL*)info->mCocoaAUViewBundleLocation;
        NSString* className = (__bridge NSString*)info->mCocoaAUViewClass[0];
        NSBundle* bundle = [NSBundle bundleWithURL:bundleURL];
        NSError* loadError = nil;
        if (!bundle || ![bundle loadAndReturnError:&loadError]) {
            const char* reason = loadError.localizedDescription.UTF8String;
            std::fprintf(stderr, "FAIL: Cocoa UI bundle load: %s\n",
                         reason ? reason : "unknown");
            AudioComponentInstanceDispose(audioUnit);
            return 1;
        }

        Class factoryClass = NSClassFromString(className);
        id<AUCocoaUIBase> factory = factoryClass ? [[factoryClass alloc] init] : nil;
        if (!factory) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("Cocoa UI factory class could not be created");
        }

        NSView* first = [factory uiViewForAudioUnit:audioUnit withSize:NSMakeSize(1280, 752)];
        if (!first || first.subviews.count == 0 || !first.layer) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("first Cocoa editor view is incomplete");
        }

        NSView* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1280, 752)];
        container.wantsLayer = YES;
        first.frame = container.bounds;
        [container addSubview:first];
        pumpMainRunLoop(0.20);
        // Logic/AUv2 bootstrap intentionally builds chrome + MAIN only. Validate
        // a drawable editor/control hierarchy without forcing inactive tab
        // effects or eye-candy layers to exist during first presentation.
        if (!first.layer || first.subviews.count < 4) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("lazy MAIN editor chrome/control hierarchy was not constructed");
        }

        NSResponder* lifecycle = lifecycleResponderForView(first);
        if (!lifecycle) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("editor lifecycle responder was not found");
        }
        ((void (*)(id, SEL))objc_msgSend)(lifecycle, @selector(pauseEditorViewForHostDetach));
        [first removeFromSuperview];

        NSView* second = [factory uiViewForAudioUnit:audioUnit withSize:NSMakeSize(1280, 752)];
        if (second != first) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("AUv2 factory did not reuse the detached editor view");
        }
        if (!second.layer || second.subviews.count < 4) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("reused editor lost its lazy chrome/control hierarchy");
        }

        BOOL paused = ((BOOL (*)(id, SEL))objc_msgSend)(
            lifecycle, NSSelectorFromString(@"_isEditorRuntimePaused_v322_"));
        if (paused) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("reused editor remained paused after reconnect");
        }

        second.frame = container.bounds;
        [container addSubview:second];
        pumpMainRunLoop(0.30);

        // audit P0-10: request a NEW view while the current editor view is STILL
        // ATTACHED to a host hierarchy. The factory must not hand back the attached
        // view; it must cleanly detach the outgoing view from its superview and build
        // a fresh editor — never leaving the host displaying a view whose controller
        // was torn down.
        NSView* third = [factory uiViewForAudioUnit:audioUnit withSize:NSMakeSize(1280, 752)];
        if (!third) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("attached-view replacement returned no editor");
        }
        if (third == second) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("factory reused a still-attached editor view for a new request");
        }
        if (second.superview != nil) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("outgoing attached editor was not detached on replacement (orphan view)");
        }
        if (third.subviews.count == 0 || !third.layer) {
            AudioComponentInstanceDispose(audioUnit);
            return fail("replacement editor view is incomplete");
        }

        NSResponder* lifecycle3 = lifecycleResponderForView(third);
        third.frame = container.bounds;
        [container addSubview:third];
        pumpMainRunLoop(0.20);
        if (lifecycle3)
            ((void (*)(id, SEL))objc_msgSend)(lifecycle3, @selector(prepareForFinalEditorDisposal));
        [third removeFromSuperview];
        AudioComponentInstanceDispose(audioUnit);
        std::puts("ArpSID AUv2 GUI lifecycle smoke PASS");
        return 0;
    }
}
