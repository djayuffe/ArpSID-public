// ArpSIDHostAppDelegate.h
// ArpSID AUv3 — Standalone Host App Delegate
//
// Manages the NSApplication lifecycle, the main window, and the
// AVAudioEngine graph that hosts the ArpSIDAudioUnit.
//
// Copyright (c) 2024 ArpSID Project. All rights reserved.
// SPDX-License-Identifier: MIT

#pragma once

#import <Cocoa/Cocoa.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

@class ArpSIDAudioUnit;
@class ArpSIDViewController;

NS_ASSUME_NONNULL_BEGIN

@interface ArpSIDHostAppDelegate : NSObject <NSApplicationDelegate>

/// The running AVAudioEngine powering the instrument.
@property (nonatomic, strong, readonly) AVAudioEngine* engine;

/// The live AUAudioUnit instance loaded inside the engine.
@property (nonatomic, strong, readonly, nullable) ArpSIDAudioUnit* audioUnit;

@end

NS_ASSUME_NONNULL_END
