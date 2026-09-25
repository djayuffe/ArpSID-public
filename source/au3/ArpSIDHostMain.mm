// ArpSIDHostMain.mm
// ArpSID AUv3 — Standalone Host Application Entry Point
//
// NSApplication bootstrap that starts the Cocoa run loop and
// hands control to ArpSIDHostAppDelegate.
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#import <Cocoa/Cocoa.h>
#import "ArpSIDHostAppDelegate.h"

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        app.activationPolicy = NSApplicationActivationPolicyRegular;
        // Keep a strong reference so the delegate survives the assignment to
        // app.delegate (which is a weak property under ARC).
        ArpSIDHostAppDelegate* delegate = [[ArpSIDHostAppDelegate alloc] init];
        app.delegate = delegate;
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
