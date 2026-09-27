// SPDX-License-Identifier: BSD-3-Clause
// Copyright (C) 2024-2026 Ulf Bertilsson
// AU editor snapshot: renders every tab of the native Cocoa editor (the one
// AUv2, AUv3, the Standalone app and the macOS VST3 show) to PNG files.
//
// It loads the installed ArpSID AUv2 like a host, initializes it, holds a
// C3-G3-C4-E4 chord and renders audio between captures so meters, scopes and
// register views show live engine activity. Each tab is selected through the
// view controller's selectPresentationTabIndex: and captured from the view
// hierarchy (layer-backed views included). With --flavors it also captures the
// landing page of each of the five AU flavors.
//
// usage: arpsid_au_editor_snapshot <out-dir> [--flavors]
//   writes au-editor-<tab>.png (and au-flavor-<name>.png)

#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <AudioUnit/AudioUnit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr OSType kType = 'aumu';
constexpr OSType kManufacturer = 'ASID';
constexpr CGFloat kWidth = 1280.0, kHeight = 752.0;

int failures = 0;
void check(bool ok, const std::string& msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg.c_str());
        ++failures;
    }
}

void pump(NSTimeInterval seconds) {
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([deadline timeIntervalSinceNow] > 0.0)
        [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode
                              beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
}

NSResponder* controllerForView(NSView* view) {
    for (NSResponder* r = view.nextResponder; r; r = r.nextResponder)
        if ([r respondsToSelector:@selector(selectPresentationTabIndex:)]) return r;
    return nil;
}

// Renders one AU instance: audio blocks with an optional chord.
struct Engine {
    AudioUnit unit = nullptr;
    Float64 sampleTime = 0;
    UInt32 channels = 2;
    std::vector<std::vector<float>> buffers;

    bool start(OSType subtype) {
        AudioComponentDescription d{};
        d.componentType = kType;
        d.componentSubType = subtype;
        d.componentManufacturer = kManufacturer;
        AudioComponent c = AudioComponentFindNext(nullptr, &d);
        if (!c || AudioComponentInstanceNew(c, &unit) != noErr || !unit) return false;
        UInt32 maxFrames = 512;
        AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                             &maxFrames, sizeof maxFrames);
        AudioStreamBasicDescription fmt{};
        UInt32 size = sizeof fmt;
        if (AudioUnitGetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 0, &fmt, &size) ==
            noErr)
            channels = fmt.mChannelsPerFrame ? fmt.mChannelsPerFrame : 2;
        buffers.assign(channels, std::vector<float>(512));
        return AudioUnitInitialize(unit) == noErr;
    }

    void midi(UInt32 status, UInt32 d1, UInt32 d2) { MusicDeviceMIDIEvent(unit, status, d1, d2, 0); }

    void render(int blocks) {
        const UInt32 frames = 512;
        std::vector<std::uint8_t> storage(sizeof(AudioBufferList) + sizeof(AudioBuffer) * channels);
        auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
        for (int b = 0; b < blocks; ++b) {
            list->mNumberBuffers = channels;
            for (UInt32 c = 0; c < channels; ++c) {
                list->mBuffers[c].mNumberChannels = 1;
                list->mBuffers[c].mDataByteSize = frames * sizeof(float);
                list->mBuffers[c].mData = buffers[c].data();
            }
            AudioTimeStamp ts{};
            ts.mFlags = kAudioTimeStampSampleTimeValid;
            ts.mSampleTime = sampleTime;
            AudioUnitRenderActionFlags flags = 0;
            AudioUnitRender(unit, &flags, &ts, 0, frames, list);
            sampleTime += frames;
        }
    }

    void stop() {
        if (!unit) return;
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
        unit = nullptr;
    }
};

NSView* makeEditor(AudioUnit unit) {
    UInt32 infoSize = 0;
    Boolean writable = false;
    if (AudioUnitGetPropertyInfo(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &infoSize, &writable) !=
            noErr ||
        infoSize < sizeof(AudioUnitCocoaViewInfo))
        return nil;
    std::vector<std::uint8_t> storage(infoSize);
    if (AudioUnitGetProperty(unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, storage.data(), &infoSize) !=
        noErr)
        return nil;
    auto* info = reinterpret_cast<AudioUnitCocoaViewInfo*>(storage.data());
    NSBundle* bundle = [NSBundle bundleWithURL:(__bridge NSURL*)info->mCocoaAUViewBundleLocation];
    if (!bundle || ![bundle load]) return nil;
    Class cls = NSClassFromString((__bridge NSString*)info->mCocoaAUViewClass[0]);
    id<AUCocoaUIBase> factory = cls ? [[cls alloc] init] : nil;
    return factory ? [factory uiViewForAudioUnit:unit withSize:NSMakeSize(kWidth, kHeight)] : nil;
}

// Captures the view hierarchy (drawRect and layer-backed content) at the
// backing scale of the window.
bool writePng(NSView* view, NSString* path, NSUInteger& bytes) {
    [view layoutSubtreeIfNeeded];
    [view displayIfNeeded];
    const NSRect bounds = view.bounds;
    NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect:bounds];
    if (!rep) return false;
    [view cacheDisplayInRect:bounds toBitmapImageRep:rep];
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    bytes = png.length;
    return png && [png writeToFile:path atomically:YES];
}

std::string slug(const char* name) {
    std::string s;
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        if ((c >= 'A' && c <= 'Z')) s += static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) s += c;
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s;
}

struct TabShot {
    NSInteger id;     // ArpSID::GUI::ArpSIDTab raw value
    const char* name; // segmented-control label
};

// kProductionVisibleTabs order (include/arpsid/gui/tab_architecture.h).
const TabShot kTabs[] = {
    {0, "MAIN"},  {1, "LFO / ARP"}, {3, "SID REG"}, {4, "SEQ"},      {13, "DRSID"},   {5, "FILTER"},
    {6, "MACRO"}, {7, "FORENSIC"},  {8, "SIDCORE"}, {11, "C64"},     {12, "HI-FI"},   {9, "BANK"},
    {10, "OPTIONS"}, {14, "SETTINGS"}, {16, "MIX"}, {17, "KIT"}, {18, "DIGI"},
};

NSWindow* hostWindow(NSView* editor) {
    NSWindow* w = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, kWidth, kHeight)
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    w.releasedWhenClosed = NO;
    NSView* content = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, kWidth, kHeight)];
    content.wantsLayer = YES;
    w.contentView = content;
    editor.frame = content.bounds;
    [content addSubview:editor];
    [w orderFrontRegardless];
    return w;
}

void holdChord(Engine& e) {
    for (UInt32 n : {48u, 55u, 60u, 64u}) e.midi(0x90, n, 100);
}

} // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 2) {
            std::fprintf(stderr, "usage: %s <out-dir> [--flavors]\n", argv[0]);
            return 2;
        }
        NSString* outDir = [NSString stringWithUTF8String:argv[1]];
        const bool flavors = argc > 2 && std::string(argv[2]) == "--flavors";
        [[NSFileManager defaultManager] createDirectoryAtPath:outDir withIntermediateDirectories:YES
                                                   attributes:nil error:nil];
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        // ── Every tab of the Classic (hybrid) flavor ─────────────────────────
        {
            Engine e;
            check(e.start('ArpS'), "ArpSID AUv2 (ArpS) instantiates and initializes");
            if (!e.unit) return 1;
            e.render(8);
            holdChord(e);
            e.render(80);
            NSView* editor = makeEditor(e.unit);
            check(editor != nil, "Cocoa editor view");
            if (!editor) return 1;
            NSWindow* window = hostWindow(editor);
            pump(0.5);
            NSResponder* vc = controllerForView(editor);
            check(vc != nil, "view controller responds to selectPresentationTabIndex:");
            for (const TabShot& t : kTabs) {
                if (vc) ((void (*)(id, SEL, NSInteger))objc_msgSend)(vc, @selector(selectPresentationTabIndex:), t.id);
                for (int i = 0; i < 6; ++i) {
                    e.render(16);
                    pump(0.1);
                }
                NSString* path = [outDir stringByAppendingPathComponent:
                                             [NSString stringWithFormat:@"au-editor-%s.png", slug(t.name).c_str()]];
                NSUInteger bytes = 0;
                check(writePng(editor, path, bytes), std::string("write ") + path.UTF8String);
                check(bytes > 30000, std::string("tab ") + t.name + " renders real content");
                std::printf("  %-10s -> %s (%lu KB)\n", t.name, path.UTF8String, (unsigned long)(bytes / 1024));
            }
            [window orderOut:nil];
            if ([vc respondsToSelector:@selector(prepareForFinalEditorDisposal)])
                ((void (*)(id, SEL))objc_msgSend)(vc, @selector(prepareForFinalEditorDisposal));
            [editor removeFromSuperview];
            e.stop();
        }

        // ── Landing page of each AU flavor ──────────────────────────────────
        if (flavors) {
            struct Flavor { OSType subtype; const char* name; } list[] = {
                {'ArpS', "classic"}, {'ArIn', "instrument"}, {'DrSD', "drum-machine"},
                {'S808', "sid-808"}, {'C64P', "c64-sid-player"},
            };
            for (const auto& f : list) {
                Engine e;
                if (!e.start(f.subtype)) {
                    check(false, std::string("flavor ") + f.name + " instantiates");
                    continue;
                }
                e.render(8);
                holdChord(e);
                e.render(80);
                NSView* editor = makeEditor(e.unit);
                if (!editor) {
                    check(false, std::string("flavor ") + f.name + " editor");
                    e.stop();
                    continue;
                }
                NSWindow* window = hostWindow(editor);
                for (int i = 0; i < 8; ++i) {
                    e.render(16);
                    pump(0.1);
                }
                NSString* path = [outDir stringByAppendingPathComponent:
                                             [NSString stringWithFormat:@"au-flavor-%s.png", f.name]];
                NSUInteger bytes = 0;
                check(writePng(editor, path, bytes), std::string("write ") + path.UTF8String);
                std::printf("  flavor %-15s -> %s (%lu KB)\n", f.name, path.UTF8String, (unsigned long)(bytes / 1024));
                [window orderOut:nil];
                NSResponder* vc = controllerForView(editor);
                if ([vc respondsToSelector:@selector(prepareForFinalEditorDisposal)])
                    ((void (*)(id, SEL))objc_msgSend)(vc, @selector(prepareForFinalEditorDisposal));
                [editor removeFromSuperview];
                e.stop();
            }
        }

        if (failures) {
            std::fprintf(stderr, "arpsid_au_editor_snapshot: %d failure(s)\n", failures);
            return 1;
        }
        std::puts("arpsid_au_editor_snapshot PASS");
        return 0;
    }
}
