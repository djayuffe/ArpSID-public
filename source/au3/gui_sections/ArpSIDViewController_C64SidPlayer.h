// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSIDViewController+C64SidPlayer section interface.
// Declares C64 SID player panel action methods extracted from
// ArpSIDViewController.mm into a dedicated section file.
#pragma once
#import "ArpSIDViewController_Private.h"

@interface ArpSIDViewController (C64SidPlayer)
- (IBAction)_c64LoadSidFile:(id)sender;
- (IBAction)_c64EjectSidFile:(id)sender;
- (IBAction)_c64UnloadSidFile:(id)sender;
- (IBAction)_c64PrevSubtune:(id)sender;
- (IBAction)_c64NextSubtune:(id)sender;
- (IBAction)_c64BootC64:(id)sender;
- (IBAction)_c64StartPlayback:(id)sender;
- (IBAction)_c64StopPlayback:(id)sender;
- (IBAction)_c64ResetC64:(id)sender;
- (NSView*)_c64Panel:(NSRect)r;
- (NSView*)_c64StatePanel_v544_:(NSRect)r;
@end
