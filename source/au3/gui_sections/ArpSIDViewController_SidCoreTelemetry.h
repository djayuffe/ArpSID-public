// Copyright (C) 2024-2026 Ulf Bertilsson
// ArpSIDViewController+SidCoreTelemetry section interface.
// SIDCORE register-write timeline and telemetry panel.
#pragma once
#import "ArpSIDViewController_Private.h"

@interface ArpSIDViewController (SidCoreTelemetry)
- (NSView*)_sidCorePanel:(NSRect)r;
- (NSView*)_sidRegPanel:(NSRect)r;
@end
