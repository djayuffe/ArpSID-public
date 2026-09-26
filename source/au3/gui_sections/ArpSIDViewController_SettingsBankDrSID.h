// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSIDViewController+SettingsBankDrSID section interface.
// Panel builders and action methods for SETTINGS, BANK, and DrSID tabs.
#pragma once
#import "ArpSIDViewController_Private.h"

@interface ArpSIDViewController (SettingsBankDrSID)
- (NSView*)_settingsPanel_v544_:(NSRect)r;
- (NSView*)_bankPanel:(NSRect)r;
- (NSView*)_drsidPanel:(NSRect)r;
- (NSView*)_seqPanel:(NSRect)r;
- (NSView*)_mainPanel:(NSRect)r;
- (NSView*)_lfoPanel:(NSRect)r;
- (NSView*)_filterPanel:(NSRect)r;
- (NSView*)_macroPanel:(NSRect)r;
- (NSView*)_optionsPanel:(NSRect)r;
@end
