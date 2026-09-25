// Local diagnostic: instantiate ArpSID AUv2 through AUAudioUnit's out-of-process
// bridge and request the editor view, matching Logic's AUHostingService path.

#import <AppKit/AppKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudioKit/CoreAudioKit.h>

static OSType fourcc(const char* s) {
    return ((OSType)(unsigned char)s[0] << 24) |
           ((OSType)(unsigned char)s[1] << 16) |
           ((OSType)(unsigned char)s[2] << 8) |
           ((OSType)(unsigned char)s[3]);
}

@interface ArpSIDAUv2OOPProbeDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) AUAudioUnit* audioUnit;
@property(nonatomic, strong) AUViewControllerBase* viewController;
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, copy) NSString* subtype;
@end

@implementation ArpSIDAUv2OOPProbeDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)note {
    (void)note;
    const char* subtypeUTF8 = self.subtype.UTF8String ?: "ArpS";
    AudioComponentDescription desc{};
    desc.componentType = fourcc("aumu");
    desc.componentSubType = fourcc(subtypeUTF8);
    desc.componentManufacturer = fourcc("ASID");

    NSLog(@"ARPSID_OOP_PROBE begin subtype=%@", self.subtype);
    [AUAudioUnit instantiateWithComponentDescription:desc
                                             options:kAudioComponentInstantiation_LoadOutOfProcess
                                   completionHandler:^(AUAudioUnit* _Nullable au, NSError* _Nullable error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!au || error) {
                NSLog(@"ARPSID_OOP_PROBE instantiate failed au=%@ error=%@", au, error);
                [NSApp terminate:nil];
                return;
            }
            self.audioUnit = au;
            NSLog(@"ARPSID_OOP_PROBE instantiated %@", au);
            NSDate* start = [NSDate date];
            [au requestViewControllerWithCompletionHandler:^(AUViewControllerBase* _Nullable vc) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    NSTimeInterval elapsed = -[start timeIntervalSinceNow];
                    NSLog(@"ARPSID_OOP_PROBE requestViewController completed elapsed=%.3f vc=%@", elapsed, vc);
                    if (!vc) {
                        [NSApp terminate:nil];
                        return;
                    }
                    self.viewController = vc;
                    NSView* view = vc.view;
                    view.frame = NSMakeRect(0, 0, 1600, 940);
                    view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
                    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(80, 80, 1600, 940)
                                                               styleMask:(NSWindowStyleMaskTitled |
                                                                          NSWindowStyleMaskClosable |
                                                                          NSWindowStyleMaskResizable)
                                                                 backing:NSBackingStoreBuffered
                                                                   defer:NO];
                    self.window.contentView = view;
                    [self.window makeKeyAndOrderFront:nil];
                    NSLog(@"ARPSID_OOP_PROBE view mounted subviews=%lu window=%@",
                          (unsigned long)view.subviews.count, self.window);
                    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(8.0 * NSEC_PER_SEC)),
                                   dispatch_get_main_queue(), ^{
                        NSLog(@"ARPSID_OOP_PROBE done");
                        [NSApp terminate:nil];
                    });
                });
            }];
        });
    }];
}

@end

int main(int argc, char** argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        ArpSIDAUv2OOPProbeDelegate* delegate = [[ArpSIDAUv2OOPProbeDelegate alloc] init];
        delegate.subtype = (argc > 1) ? [NSString stringWithUTF8String:argv[1]] : @"ArpS";
        NSApp.delegate = delegate;
        [NSApp run];
    }
    return 0;
}
