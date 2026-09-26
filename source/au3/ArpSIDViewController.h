// ArpSIDViewController.h
// ArpSID AUv3 — AUViewController Subclass
//
// Hosts embed this view controller's view into their track/mixer UI.
// On macOS it is a full NSViewController subclass using AppKit controls.
// The view is created fully in code — no XIB/storyboard dependencies.
//
// Copyright (C) 2024-2026 Ulf Bertilsson
// SPDX-License-Identifier: MIT

#pragma once

#import <CoreAudioKit/CoreAudioKit.h>

NS_ASSUME_NONNULL_BEGIN

/// AUv3 view controller. The AUv3 framework principal class must be
/// "ArpSID_AU3Framework.ArpSIDViewController" (set in Info-AU3Extension.plist).
@interface ArpSIDViewController : AUViewController <AUAudioUnitFactory>

@property (nonatomic, strong, nullable) AUAudioUnit* audioUnit;

/// Called by the host (or entry host) with the AUAudioUnit instance so the view
/// can observe parameter changes and forward UI interactions.
- (void)connectAudioUnit:(AUAudioUnit*)audioUnit;
- (void)connectBridge:(id)bridge;
- (void)beginHostPresetApply;
- (void)endHostPresetApply;

/// Called by the standalone host to update the MIDI keyboard HUD in the footer.
/// statusLine: short description e.g. "Arturia KeyStep 37" or "No MIDI input"
/// active: YES if MIDI was received recently (blinks the activity LED)
- (void)updateMIDIStatus:(NSString*)statusLine activity:(BOOL)active;
- (void)updatePresentationContext:(NSString*)contextLine;
- (void)selectPresentationTabIndex:(NSInteger)index;
- (void)refreshPresets;
- (void)prepareForEmbeddedVSTPresentation;
- (NSView*)embeddedVSTPreferredFirstResponder;
- (void)embeddedVSTHostDidResize;

/// Host lifecycle hooks. A normal editor close/reparent is a reusable pause,
/// while final disposal is the only path allowed to destroy observers, capture
/// resources, the display link, and Metal state.
- (void)pauseEditorViewForHostDetach;
- (void)prepareForFinalEditorDisposal;
- (void)reconnectExistingEditorViewToAudioUnit:(AUAudioUnit*)audioUnit
                                   contextLine:(nullable NSString*)contextLine;

@end

NS_ASSUME_NONNULL_END
